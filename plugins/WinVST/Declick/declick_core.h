/* ========================================
 *  foo_dsp_declick - portable DSP core
 *
 *  A detect-and-interpolate declicker for mono/stereo transfers of shellac and
 *  vinyl. This is the classic autoregressive approach (Vaseghi, Godsill &
 *  Rayner) and it is what dedicated declick tools do:
 *
 *    1. Fit an AR model to a block of audio.
 *    2. Clicks are broadband impulses, so they blow up the AR prediction
 *       residual while music does not. Threshold the residual against a robust
 *       local scale, with hysteresis so the whole burst is captured.
 *    3. Replace the flagged samples with the least squares interpolation
 *       implied by the model and the surrounding good audio - reconstruct what
 *       the waveform should have been, rather than smoothing what is there.
 *
 *  Step 3 is what Airwindows DeCrackle has no equivalent of, and step 2 is why
 *  DeCrackle cannot work on mono material at all: its detector keys on the
 *  L*R cross product, which collapses to x^2 when both channels are the same.
 *
 *  No foobar2000, VST or Win32 dependency, so the maths can be tested against
 *  the Python reference implementation directly.
 * ======================================== */

#ifndef DECLICK_CORE_H
#define DECLICK_CORE_H

#include <stddef.h>
#include <stdint.h>

#include <vector>

namespace declick {

enum {
    kMinOrder = 8,
    kMaxOrder = 64,
    kBlock    = 512      //!< analysis hop; also sets most of the latency
};

//! User-facing controls, all normalised 0..1 except where noted.
struct Params {
    float sensitivity;  //!< 0 = only the most obvious clicks, 1 = aggressive
    float extent;       //!< how far a detection spreads outwards into its tail
    float maxLengthMs;  //!< longest single repair, in milliseconds
    float depth;        //!< 0 = least added error, 1 = replace damaged samples outright
    int   passes;       //!< 1..3; a second pass catches clicks the first uncovers
    int   order;        //!< AR model order
    float dryWet;       //!< 0 = bypass, 1 = full repair

    static Params defaults() {
        Params p;
        // 0.6 puts the trigger at 3.9 sigma. On 78 rpm tango transfers,
        // that takes impulsive events from ~71/s to ~14/s.
        // Raise it towards 0.8 for more aggressiveness.
        p.sensitivity = 0.6f;
        p.extent      = 0.5f;
        p.maxLengthMs = 4.0f;
        // 0 subtracts the fraction of each click calibrated against a clean
        // master transfer with real clicks injected at known positions - the
        // setting that adds the least error of its own. Raising it removes
        // more of each click but substitutes more guesswork; see the README.
        p.depth       = 0.0f;
        p.passes      = 2;
        p.order       = 32;
        p.dryWet      = 1.0f;
        return p;
    }
    void sanitize();
    bool operator==(const Params & o) const;
    bool operator!=(const Params & o) const { return !(*this == o); }
};

//! Everything derived from (params, sample rate).
struct Config {
    int    order        = 32;
    int    maxRun       = 176;   //!< longest repair, samples
    int    passes       = 2;
    int    madWindow    = 6615;  //!< samples used for the robust noise estimate
    double thresholdHi  = 3.5;   //!< trigger
    double thresholdLo  = 1.6;   //!< hysteresis extension
    double depth        = 0.0;   //!< pushes the weighting towards replacement
    double wet          = 1.0;

    //! Subtractive repair. Calibration constants, not derived from params and
    //! not user-facing.
    //!
    //! `wienerMax` is the fraction of the click estimate that gets subtracted.
    //! 0.45 was measured against injected-click ground truth and against four
    //! reference transfers; it beats the length-indexed curve it replaced on
    //! every axis of both.
    //!
    //! `wienerAlpha` scales the model's own per-sample estimate variance,
    //! turning the flat fraction into a true Wiener gain. It is 0 - i.e. off -
    //! because it was measured and does not pay on 78 rpm material: the click
    //! is nearly always far larger than the estimate's uncertainty, so the
    //! gain saturates and only the cap binds. Retained because it costs
    //! nothing when off and material with different click statistics may
    //! behave differently; declick_cli can sweep it. See the README.
    double wienerAlpha  = 0.0;
    double wienerMax    = 0.45;
    int    pad          = 272;   //!< context needed either side of a block
    int    latency      = 784;

    //! Buffer envelope: every allocation Channel makes is sized from these
    //! three, and they are derived from the sample rate alone - never from the
    //! parameters.
    //!
    //! That is what makes a parameter change allocation-free even when it
    //! resizes the pipeline. configure() reassigns its vectors to sizes they
    //! already have, and neither assign() nor resize() reallocates below the
    //! capacity it is already holding. Only a sample rate change - or the first
    //! configure() - is allowed to touch the heap, because a real-time audio
    //! thread must not.
    //!
    //! bufMaxRun is the longest repair Params::sanitize() can produce (its
    //! 20 ms ceiling), and bufOrder is kMaxOrder, so no setting of Max repair
    //! or Model order can exceed them.
    int    bufOrder     = (int)kMaxOrder;
    int    bufMaxRun    = 882;
    int    bufPad       = 1074;

    //! Largest single push() the caller promises to make between pulls. The
    //! output ring is sized for it; a longer push is handled correctly but
    //! grows the ring once, which allocates. Callers that push and pull in
    //! lockstep never come near it.
    int    maxBlock     = 16384;

    void compute(const Params & p, double sampleRate);

    //! True if `o` needs exactly the buffers this config already has, i.e. it
    //! can be swapped into a running Channel with retune() instead of
    //! configure(), which reallocates and therefore resets.
    //!
    //! Only `order`, `maxRun` and `madWindow` size anything (`pad` and
    //! `latency` are derived from the first two); thresholds, depth, wet and
    //! passes are read per block and can change under a live stream.
    bool structurallyEquals(const Config & o) const {
        return order == o.order && maxRun == o.maxRun && madWindow == o.madWindow;
    }
};

//! Flush-to-zero for the duration of a scope. The AR fit and the Cholesky
//! solve both run recursions down towards zero, so denormals are reachable and
//! expensive. Every wrapper must hold one of these across its processing loop:
//! FTZ changes results in the last bits, so it is part of the numerical
//! contract rather than an optimisation, and the ports are only comparable to
//! each other while they all agree about it.
class scoped_flush_denormals {
public:
    scoped_flush_denormals(const scoped_flush_denormals &) = delete;
    void operator=(const scoped_flush_denormals &) = delete;
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
    scoped_flush_denormals();
    ~scoped_flush_denormals();
private:
    unsigned m_saved;
#else
    scoped_flush_denormals() {}
#endif
};

//! One independent channel of processing.
class Channel {
public:
    Channel();

    void configure(const Config & cfg);
    void reset();

    //! Swap in a config that needs the same buffers, without resetting - so a
    //! host moving Sensitivity, Extent, Repair depth, Passes or Dry/Wet does
    //! not punch a `latency`-sample hole in the audio. Returns false if the new
    //! config is structurally different, in which case the caller has to
    //! configure() and accept the discontinuity.
    bool retune(const Config & cfg);

    //! Streaming, push/pull because the pipeline holds config().latency
    //! samples: feed everything, then take whatever is ready.
    //!
    //!   ch.push(in, n, stride);
    //!   size_t k = ch.available();
    //!   ch.pull(out, k, stride);
    //!
    //! All channels configured alike consume and produce in lockstep, so one
    //! available() query covers the whole frame.
    template<typename Sample>
    void push(const Sample * in, size_t frames, size_t stride);

    size_t available() const { return m_outCount; }

    template<typename Sample>
    void pull(Sample * out, size_t frames, size_t stride);

    //! End of stream: run `latency` zeros through so the tail comes out.
    void drain();

    //! Pre-roll, for a host that insists on n samples out for n samples in.
    //! Feeding `latency` zeros up front makes `available() >= n` hold after
    //! every subsequent push of n - for any n, any block size - so push/pull
    //! pairs can run forever without underrunning. The cost is that the output
    //! is then delayed by `latency` relative to the input, which is what a VST
    //! declares through setInitialDelay(). Mechanically the same operation as
    //! drain(); named separately because the intent is the opposite one.
    void prime() { drain(); }

    const Config & config() const { return m_cfg; }

    //! Diagnostics: how many samples this channel has repaired.
    uint64_t repairedSamples() const { return m_repaired; }
    uint64_t seenSamples() const { return m_seen; }

    //! Diagnostics: total bytes this channel is holding. Nothing in the
    //! processing path adds to this - see the buffer envelope in Config.
    size_t heapBytes() const;

private:
    void pushOne(double v);
    void outPush(double v);
    void growOut();               //!< only reachable by overrunning maxBlock
    void processBlock();          //!< repairs m_win[m_pad .. m_pad+kBlock)
    void detect(int from, int to, int pass);
    void interpolate(int from, int to);
    void fitModel(int from, int to);

    Config m_cfg;

    std::vector<double> m_win;    //!< sliding window of input/repaired audio
    std::vector<uint8_t> m_flag;  //!< parallel to m_win: repair mask
    std::vector<double> m_dry;    //!< untouched copy, for the dry/wet mix
    int    m_fill = 0;            //!< samples currently held in m_win
    int    m_pad = 0;

    std::vector<double> m_fwd, m_bwd, m_det;
    std::vector<double> m_a, m_ra;
    std::vector<double> m_scratch;   //!< model fitting / median
    std::vector<double> m_seg;       //!< interpolation segment
    std::vector<double> m_err;       //!< interpolation prediction error
    std::vector<double> m_rhs;       //!< interpolation right hand side
    std::vector<double> m_solve;     //!< Cholesky factor
    std::vector<double> m_sinv;      //!< banded slice of G^-1
    std::vector<double> m_pvar;      //!< diag(G^-1), per damaged sample

    std::vector<double> m_madRing; //!< |forward residual| history
    int    m_madPos = 0;
    bool   m_madFull = false;
    double m_scale = 1e-6;

    //! Finished samples, as a fixed-capacity ring - m_out.size() IS the
    //! capacity, so reset() must not clear() it. It used to be a vector that
    //! grew by push_back to 65536 samples and was then compacted with erase(),
    //! which meant a 720 kB reallocation a second or so into every stream, on
    //! the audio thread.
    std::vector<double> m_out;
    size_t m_outHead = 0;          //!< read position
    size_t m_outCount = 0;         //!< samples held

    uint64_t m_repaired = 0, m_seen = 0;
    bool m_primed = false;
};

//! Levinson-Durbin. `a` receives order+1 coefficients with a[0] == 1.
//! Returns false if the autocorrelation is degenerate.
bool levinson(const double * r, int order, double * a);

//! Banded symmetric Toeplitz Cholesky solve, half-bandwidth `band`.
//! `d` holds the Toeplitz first column (band+1 entries). Solves G x = b in
//! place on `b`. Returns false if not positive definite. `scratch` is left
//! holding the Cholesky factor, for bandedInverseDiagonal() below.
bool solveBandedToeplitz(const double * d, int band, double * b, int n,
                         std::vector<double> & scratch);

//! Diagonal of G^-1, from the factor solveBandedToeplitz() left behind.
//! Pass the same `band` and `n`; `chol` is that call's scratch vector and
//! `work` is separate scratch of the caller's. Writes n entries to `diag`.
//!
//! Uses Takahashi's recursion, which computes the banded slice of the inverse
//! in O(n * band^2) rather than inverting outright: filling from the bottom
//! right, every term needed is either already written or the symmetric
//! counterpart of one that is.
bool bandedInverseDiagonal(const std::vector<double> & chol, int band, int n,
                           std::vector<double> & work, double * diag);

} // namespace declick

#endif // DECLICK_CORE_H
