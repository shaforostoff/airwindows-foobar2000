/* ========================================
 *  DeCrackle - portable DSP core
 *
 *  Algorithm by Chris Johnson / Airwindows (MIT license), transcribed from
 *  plugins/WinVST/DeCrackle/DeCrackleProc.cpp (processReplacing, the 32 bit
 *  float path - foobar2000's audio_sample is float, so that is the faithful
 *  variant, dither included).
 *
 *  This header is deliberately free of any foobar2000 / VST / Win32
 *  dependency so the maths can be unit tested and reused.
 * ======================================== */

#ifndef AIRWINDOWS_DECRACKLE_CORE_H
#define AIRWINDOWS_DECRACKLE_CORE_H

#include <stddef.h>
#include <stdint.h>

namespace airwindows {

//! Longest delay line the algorithm can ask for. Taken verbatim from the
//! Airwindows source (`kshort`); the +5 slack is from the original too.
enum {
    kDeCrackleShort   = 1600,
    kDeCrackleBufSize = kDeCrackleShort + 5,
    kDeCrackleBufLast = kDeCrackleBufSize - 1
};

//! The five user parameters, all normalised to 0..1 exactly like the VST.
struct DeCrackleParams {
    float filter;    //!< A - "Filter":  bass-only .. full range replacement audio
    float window;    //!< B - "Window":  narrow .. very wide detection window
    float threshold; //!< C - "Thresld": lower = more aggressive
    float surface;   //!< D - "Surface": surface noise / micro-crackle taming
    float dryWet;    //!< E - "Dry/Wet": 0.0 is the special "clicks only" delta mode

    static DeCrackleParams defaults() {
        DeCrackleParams p;
        p.filter = 0.5f; p.window = 0.5f; p.threshold = 0.5f;
        p.surface = 0.5f; p.dryWet = 1.0f;
        return p;
    }

    //! Force every field into 0..1 and replace NaNs. Applied to anything that
    //! arrives from stored configuration, which may be corrupt.
    void sanitize();

    bool operator==(const DeCrackleParams & o) const {
        return filter == o.filter && window == o.window && threshold == o.threshold
            && surface == o.surface && dryWet == o.dryWet;
    }
    bool operator!=(const DeCrackleParams & o) const { return !(*this == o); }
};

//! Everything derived from (parameters, sample rate). Recomputed only when one
//! of those actually changes rather than once per buffer like the VST does.
struct DeCrackleCoeffs {
    int    adjDelay;          //!< ring buffer wrap point, <= kDeCrackleShort-1
    int    halfRaw;           //!< the original's halfRaw and halfBez are always equal
    int    halfTrig;
    int    offset;
    double maxHeight;
    double filterOut, oneMinusFilterOut;
    double filterRef, oneMinusFilterRef;
    double iirCut;
    double threshold;
    double surface;
    double wet;
    bool   blendWet;          //!< 0 < wet < 1
    bool   deltaOnly;         //!< wet == 0, "monitor the clicks" mode
    bool   surfaceOn;         //!< surface > 0 && wet > 0
    int    latencySamples;    //!< group delay introduced by the delay line

    void compute(const DeCrackleParams & p, double sampleRate);
};

//! State for one stereo pair (or, via processMono, one lone channel).
//! ~64 kB, so it is always heap allocated by the caller.
class DeCracklePair {
public:
    DeCracklePair();

    //! Back to the exact state of a freshly constructed object.
    void reset();

    //! `left`/`right` point at the first sample of each channel inside an
    //! interleaved buffer; `stride` is the total channel count.
    //!
    //! foobar2000's audio_sample is float in 32 bit builds and double in 64 bit
    //! ones, which maps onto the VST's processReplacing / processDoubleReplacing
    //! split: the float form dithers to 24 bits of mantissa, the double form
    //! does not (the original has that code commented out).
    //!
    //! On x86 and x64 this runs an SSE2 path that carries L in the low lane and
    //! R in the high lane. That is lane parallelism, not reassociation, so the
    //! results are bit-identical to processStereoScalar().
    void processStereo(const DeCrackleCoeffs & k, float * left, float * right,
                       size_t stride, size_t frames);
    void processStereo(const DeCrackleCoeffs & k, double * left, double * right,
                       size_t stride, size_t frames);

    //! The portable reference path. processStereo() falls back to this where
    //! SSE2 is unavailable; the test harness runs both against the Airwindows
    //! source to prove they agree.
    void processStereoScalar(const DeCrackleCoeffs & k, float * left, float * right,
                             size_t stride, size_t frames);
    void processStereoScalar(const DeCrackleCoeffs & k, double * left, double * right,
                             size_t stride, size_t frames);

    //! True if processStereo() is using the SSE2 path.
    static bool haveVectorPath();

    //! Degenerate single-channel case: the algorithm's L*R cross detector
    //! collapses to x*x and both halves of the state stay in lockstep, so only
    //! the left half is evaluated.
    void processMono(const DeCrackleCoeffs & k, float * chan,
                     size_t stride, size_t frames);
    void processMono(const DeCrackleCoeffs & k, double * chan,
                     size_t stride, size_t frames);

    //! Cheap post-chunk paranoia check; false means something went non-finite
    //! and the caller should reset(). Only scans the scalar state - the delay
    //! lines can only ever hold values derived from it.
    bool stateIsFinite() const;

    //! Keep the write cursor legal after the window size shrinks.
    void clampCount(int adjDelay);

private:
    template<typename Sample, bool Mono>
    void run(const DeCrackleCoeffs & k, Sample * left, Sample * right,
             size_t stride, size_t frames);

    template<typename Sample>
    void runStereoSSE2(const DeCrackleCoeffs & k, Sample * left, Sample * right,
                       size_t stride, size_t frames);

    //! aA and aB are always read at identical L/R indices, so interleaving them
    //! halves the number of cache lines the inner loop touches.
    struct Frame { double l, r; };

    Frame    m_a[kDeCrackleBufSize];   //!< raw input delay line
    Frame    m_b[kDeCrackleBufSize];   //!< lowpassed ("bezier") delay line
    double   m_c[kDeCrackleBufSize];   //!< smoothed rectified control line

    double   m_iirSampleL[6], m_iirSampleR[6];
    double   m_iirAngleL[6],  m_iirAngleR[6];
    double   m_iirC[6];
    double   m_iirTargetL, m_iirTargetR;
    double   m_iirClickL,  m_iirClickR;
    double   m_prevOutL,   m_prevOutR;
    int      m_count;
    uint32_t m_fpdL, m_fpdR;
};

} // namespace airwindows

#endif // AIRWINDOWS_DECRACKLE_CORE_H
