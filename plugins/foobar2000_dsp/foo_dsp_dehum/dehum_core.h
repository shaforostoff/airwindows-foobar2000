/* ========================================
 *  foo_dsp_dehum - portable DSP core
 *
 *  Removes continuous narrowband tones - mains hum and its harmonics, and the
 *  off-frequency drones that turn up on speed-corrected disc transfers - from
 *  mono or stereo material, without being told which frequency to look at.
 *
 *  Two parts.
 *
 *  1. A detector, off the signal path. Every hop it takes the magnitude spectrum
 *     of a long sliding window and pushes it into a per-bin history; the median
 *     of that history is the part of the spectrum that is always there, and a
 *     peak standing proud of a local baseline in that median is a candidate
 *     line. Candidates must recur before they are acted on.
 *
 *     The window is long - 1.5 s at 44.1 kHz - and that is the whole trick. A
 *     coherent line's peak grows with the window length while noise and music
 *     grow with its square root, so prominence separates a hum from a musical
 *     peak 3 dB better per doubling. Measured on the reference transfers, the
 *     41.3 Hz line gains 24.7 -> 26.8 -> 28.2 dB across 0.37/0.74/1.49 s
 *     windows while the loudest music peak stays at 19-22 dB and does not grow.
 *     The same length gives the 0.67 Hz bins the notch needs to be placed on.
 *
 *  2. A canceller per line. Heterodyne the signal so the line sits at DC,
 *     lowpass to recover its complex amplitude, rotate back and subtract:
 *
 *         z = x * exp(-i*theta)
 *         w += lam * (z - w)                 lam = 2*pi*halfWidth/rate
 *         y = x - 2*Re{w * exp(i*theta)}
 *
 *     theta is a deterministic phase ramp, so nothing adapts on the signal:
 *     this is a linear time-invariant notch whose 3 dB half width is the lowpass
 *     cutoff, and it cannot ring or go unstable. Away from the line the response
 *     is |d|/sqrt(d^2 + halfWidth^2), which is 0.15 dB five half widths out.
 *
 *     Its depth is not infinite. Heterodyning also puts an image of the line at
 *     -2*f0, and what the one-pole lets through of that lands back on f0 when it
 *     is rotated up again, so the floor is halfWidth/(2*f0): 40 dB for a 1 Hz
 *     notch at 50 Hz, and deeper the lower the bandwidth or the higher the line.
 *     That is far below the residue any real detector leaves, so a second pole
 *     to square the term would buy nothing worth the state.
 *
 *     The frequency is then refined from the rotation of w. A residual error d
 *     makes w rotate at d Hz, so reading its phase advance over a quarter second
 *     measures d directly. This matters more than anything else here: the notch
 *     is narrow, so being 0.5 Hz off leaves the tone only 14 dB down, and
 *     tracking takes the same case to 92 dB down.
 *
 *  Latency is zero. The detector only reads the signal, so the canceller runs on
 *  the live sample and this core processes in place - no push/pull FIFO, unlike
 *  declick.
 *
 *  No foobar2000, VST or Win32 dependency.
 * ======================================== */

#ifndef DEHUM_CORE_H
#define DEHUM_CORE_H

#include <stddef.h>
#include <stdint.h>

#include <vector>

namespace dehum {

enum {
    kMaxLines     = 4,    //!< simultaneous fundamentals the detector may hold
    kMaxHarmonics = 8,    //!< multiples cancelled per fundamental

    //! Analysis window, as a power of two picked so the bin spacing lands near
    //! kBinTargetHz whatever the sample rate. A fixed size would give 0.67 Hz
    //! bins at 44.1 kHz and 2.9 Hz at 192 kHz, and 2.9 Hz bins cannot place a
    //! 50 Hz line well enough for a 1 Hz notch to catch it.
    kMinFftOrder  = 11,
    kMaxFftOrder  = 17,

    kHistory      = 24,   //!< detector frames behind the median

    kSearchFloor  = 16,   //!< Hz, bottom of the automatic search range
    kSearchCeil   = 500   //!< Hz, highest the top of the range may be set to
};

//! Bin spacing the analysis window aims for, in Hz. 0.7 gives a 1.5 s window at
//! 44.1 kHz; see the note on window length above for why it is not shorter.
const double kBinTargetHz = 0.7;

//! Half width of the local baseline the detector measures prominence against.
const double kBaselineHz = 20.0;

//! Evidence counter, and the part that decides what counts as hum.
//!
//! It is a duty cycle test rather than a level test, because on real material
//! the levels overlap. On the reference transfers the 41.3 Hz line's prominence
//! has a median of 19 dB and a 5th percentile of 14 dB, while the loudest
//! momentary peak in the hum-free controls reaches 22 dB - so no threshold both
//! catches the line continuously and never fires on a control. What separates
//! them is that the line clears 16 dB on 87% of hops and the best a control
//! manages is 10%.
//!
//! Each hop a candidate is seen it gains 1 plus a bonus for how far past the
//! threshold it is, and each hop it is not it loses kScoreFall. The bonus is
//! what makes this fast enough to be usable: the hum sits 3 dB past the
//! threshold on average and the control peaks that reach it barely clear it, so
//! grading the evidence roughly halves the time to engage while widening the
//! gap rather than narrowing it.
//!
//! The asymmetry matters too: at 1 up and 1 down a 50% duty cycle is a random
//! walk that reaches any threshold eventually, while at 1 up and 2 down anything
//! below a 2/3 duty cycle drifts to zero and stays there.
const double kScoreBonusPerDb = 0.25;  //!< extra credit per dB past the threshold
const double kScoreBonusMax   = 3.0;
const double kScoreFall       = 2.0;   //!< per hop missed, before a line engages
const double kScoreActivate   = 24.0;
const double kScoreCap        = 48.0;

//! Once a line is engaged it is held on much weaker evidence than it took to
//! establish: sightings count from kRetainMarginDb below the threshold, and a
//! miss costs kScoreFallActive instead of kScoreFall, so a total absence takes
//! about 36 s to give up rather than 2 s.
//!
//! Without this the line was acquired and dropped ten times over one side, and
//! removal of the hum was intermittent - 16 dB below the programme rather than
//! on top of it. Hum does not come and go; a gap in the evidence means the music
//! got loud, not that the hum stopped.
const double kRetainMarginDb   = 6.0;
const double kScoreFallActive  = 0.25;

//! User-facing controls.
struct Params {
    float sensitivity;  //!< 0 = only blatant lines, 1 = anything that stands out
    float bandwidth;    //!< notch half width, Hz
    float searchTo;     //!< top of the automatic search range, Hz
    int   harmonics;    //!< 1..8 multiples of each line to cancel
    float frequency;    //!< 0 = detect automatically, otherwise pin here (Hz)
    float rumbleHz;     //!< 0 = off, otherwise high-pass corner (Hz)
    float dryWet;       //!< 0 = bypass, 1 = full removal

    static Params defaults() {
        Params p;
        // Maps to a 16 dB prominence threshold. On the reference transfers the
        // real line clears that on 87% of hops while the best any hum-free
        // control manages is 10%, which the evidence counter turns into a
        // saturated 48 against 16 - so the gap the threshold has to sit in is
        // one of duty cycle, not of level. Raising this past about 0.7 starts
        // letting control material reach the activation score.
        p.sensitivity = 0.5f;
        // 1 Hz. The reference line is stable to +-0.02 Hz once tracked, so a
        // narrower notch would serve it, but 1 Hz absorbs the detector's own
        // error before the tracker converges and still costs nothing musically:
        // a 2 Hz hole at 41 Hz is Q = 20, and a partial 5 Hz away loses 0.15 dB.
        p.bandwidth   = 1.0f;
        // Hum lives low. Searching further up finds sustained musical notes
        // instead - during calibration a bandoneon E4 at 329 Hz was detected as
        // hum in both transfers of the same piece and duly cancelled.
        p.searchTo    = 150.0f;
        p.harmonics   = 4;
        p.frequency   = 0.0f;
        // Off. The broadband low-frequency rumble that shares this band with hum
        // is a different defect - see the README - and whether to filter it is a
        // judgement about the transfer, not something to do to every file.
        p.rumbleHz    = 0.0f;
        p.dryWet      = 1.0f;
        return p;
    }
    void sanitize();
    bool operator==(const Params & o) const;
    bool operator!=(const Params & o) const { return !(*this == o); }
};

//! Everything derived from (params, sample rate).
struct Config {
    double sampleRate  = 44100.0;
    int    fftOrder    = 16;
    int    fftSize     = 65536;
    int    hop         = 8192;
    int    binLo       = 24;     //!< first bin of the search range
    int    binHi       = 223;    //!< last bin of the search range
    int    baselineBins = 30;    //!< kBaselineHz either side, in bins

    double promDb      = 24.0;   //!< prominence a candidate needs, dB
    double halfWidth   = 1.0;    //!< notch 3 dB half width, Hz
    double lamNotch    = 0.0;    //!< one-pole coefficient, 2*pi*halfWidth/rate
    int    harmonics   = 4;
    double manualFreq  = 0.0;    //!< 0 = automatic
    double rumbleHz    = 0.0;    //!< 0 = off
    double wet         = 1.0;

    //! Score a candidate must reach before it is cancelled - see kScoreActivate.
    //! On the reference transfer the score climbs about 1.3 per hop, so the line
    //! engages some 19 hops after the window fills: about 5 s at 44.1 kHz. That
    //! is the price of a duty cycle test, and it is why Frequency exists for
    //! anyone who already knows what they are removing.
    double scoreActivate = kScoreActivate;

    //! Samples the frequency tracker integrates before reading the rotation of
    //! the notch weight. A quarter second turns a 0.1 Hz error into 0.157 rad,
    //! which is measurable; per block it would be 0.004 rad, which is noise -
    //! and an early prototype that did it per block random-walked 2 Hz off the
    //! line and left the hum untouched.
    int    trackSamples = 11025;
    double trackGain    = 0.7;   //!< fraction of the measured error applied
    double trackClampHz = 2.0;   //!< furthest a line may be pulled from where
                                 //!< the detector put it

    //! The tracker reads the rotation of the notch weight, so it is only
    //! meaningful while that weight is actually holding the line. Where the
    //! signal drops away - a run-out groove, a gap between movements - the
    //! weight collapses to noise and the reading becomes a random walk that
    //! wanders off at up to trackGain per interval. So the weight is compared
    //! against its own recent peak and the tracker freezes below this fraction
    //! of it, leaving the notch where it was.
    double trackFloor   = 0.3;
    double trackPeakDecay = 0.975;  //!< per tracker interval, about 10 s

    //! Buffer envelope. Every allocation Channel makes is sized from these, and
    //! they follow from the sample rate alone - never from the parameters. That
    //! is what lets any parameter change be handled by retune(), which does not
    //! touch the heap, so a slider move on the audio thread never allocates.
    //! Only a sample rate change reallocates.
    int    bufFftSize  = 65536;
    int    bufBins     = 720;   //!< bins spanned with searchTo at kSearchCeil
    int    bufHistory  = (int)kHistory;

    void compute(const Params & p, double sampleRate);

    //! True if `o` needs exactly the buffers this config already has. Only the
    //! sample rate sizes anything, so every parameter move can be retuned live.
    bool structurallyEquals(const Config & o) const {
        return fftSize == o.fftSize && bufBins == o.bufBins
            && bufHistory == o.bufHistory;
    }
};

//! Flush-to-zero for the duration of a scope. The notch integrator runs a
//! recursion towards zero, so denormals are reachable and slow. FTZ changes
//! results in the last bits, so it is part of the numerical contract rather than
//! an optimisation, and every wrapper holds one across its processing loop.
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

//! What the detector currently believes. Diagnostics, and what the CLI prints.
struct LineReport {
    double frequency  = 0.0;  //!< Hz, as tracked
    double detected   = 0.0;  //!< Hz, where the detector first put it
    double prominence = 0.0;  //!< dB above the local baseline of the median
    double amplitude  = 0.0;  //!< 2*|w|, the tone amplitude being subtracted
    int    harmonics  = 1;    //!< multiples engaged
};

//! One independent channel of processing.
class Channel {
public:
    Channel();

    void configure(const Config & cfg);

    //! Everything to zero, including what the detector has learned.
    void reset();

    //! A discontinuity in the input - a seek - without forgetting the lines. The
    //! analysis window is stale so it is dropped, but the hum on the far side of
    //! a seek is the same hum, and re-acquiring it every time the user moves the
    //! playback position would be worse than doing nothing.
    void flush();

    //! Swap in a config needing the same buffers, keeping state. Returns false
    //! if the new config is structurally different, in which case the caller has
    //! to configure() and accept the discontinuity. Since only the sample rate
    //! sizes anything, every parameter move takes this path.
    bool retune(const Config & cfg);

    //! In place, interleaved by `stride`. Zero latency, so there is no FIFO:
    //! what goes in comes out, same count, same alignment.
    template<typename Sample>
    void process(Sample * io, size_t frames, size_t stride);

    const Config & config() const { return m_cfg; }

    //! Diagnostics.
    int      lineCount() const;
    void     report(LineReport * out, int max, int * count) const;
    uint64_t seenSamples() const { return m_seen; }
    size_t   heapBytes() const;
    //! How many times a line has been confirmed, and how many times one has been
    //! forgotten again. A dropout count above zero on steady material means the
    //! retain threshold is too high for it.
    uint32_t confirmations() const { return m_confirmations; }
    uint32_t dropouts() const { return m_dropouts; }

private:
    //! One cancelled sinusoid: a recursive rotator for exp(i*theta), the complex
    //! amplitude estimate, and the tracker's reference.
    struct Osc {
        double freq   = 0.0;
        double cosInc = 1.0, sinInc = 0.0;
        double cosPh  = 1.0, sinPh  = 0.0;
        double wRe = 0.0, wIm = 0.0;
        double refRe = 0.0, refIm = 0.0;
        double wPeak = 0.0;      //!< decaying peak of |w|, gates the tracker
        int    trackAcc = 0;
        int    renorm   = 0;
        bool   live     = false;
    };

    struct Line {
        double detected = 0.0;   //!< where the detector put it
        double prom     = 0.0;
        double score    = 0.0;   //!< evidence counter, see kScoreActivate
        bool   active   = false;
        bool   manual   = false;
        Osc    osc[kMaxHarmonics];
    };

    void   runDetector();
    void   detectPeaks(int bins);
    void   clearHistory();
    double scoreFor(double prominence) const;
    void   syncManual();
    void   startOsc(Osc & o, double freq);
    void   setOscFreq(Osc & o, double freq);
    double runOsc(Osc & o, double x);
    double runLine(Line & line, double x);
    void   syncHarmonics(Line & line);
    void   designRumble();
    double runRumble(double x);
    void   realFftMagnitudes();

    Config m_cfg;

    // --- detector ---
    std::vector<double> m_win;    //!< sliding analysis window, a ring
    std::vector<double> m_taper;  //!< Blackman-Harris, precomputed - rebuilding
                                  //!< it per hop would be a cos() per sample
    int m_winPos = 0;
    int m_hopAcc = 0;
    int m_filled = 0;

    std::vector<double> m_fftRe, m_fftIm;  //!< N/2 complex working buffer
    std::vector<double> m_twRe, m_twIm;    //!< FFT twiddles
    std::vector<int>    m_rev;             //!< bit-reversal permutation
    std::vector<double> m_mag;             //!< |X| over the search range

    std::vector<double> m_hist;    //!< bufHistory x bufBins magnitudes
    int m_histPos = 0, m_histFill = 0;
    std::vector<double> m_med;     //!< median over history, dB, per bin
    std::vector<double> m_base;    //!< local baseline of m_med, per bin
    std::vector<double> m_sortBuf; //!< median scratch, kHistory long
    std::vector<double> m_baseBuf; //!< baseline median scratch

    Line m_line[kMaxLines];
    int  m_lines = 0;

    // --- rumble high-pass: two biquads, Butterworth order 4 ---
    double m_hpB[2][3] = { { 1, 0, 0 }, { 1, 0, 0 } };
    double m_hpA[2][2] = { { 0, 0 }, { 0, 0 } };
    double m_hpZ[2][2] = { { 0, 0 }, { 0, 0 } };
    bool   m_hpOn = false;

    uint64_t m_seen = 0;
    uint32_t m_confirmations = 0;
    uint32_t m_dropouts = 0;
};

//! Median of `n` values, reordering `buf`. Exposed so the tests can pin it.
double medianInPlace(double * buf, int n);

} // namespace dehum

#endif // DEHUM_CORE_H
