/* ========================================
 *  DeCrackle - portable DSP core
 *  Algorithm by Chris Johnson / Airwindows, MIT license.
 * ======================================== */

#include "decrackle_core.h"

#include <math.h>
#include <string.h>

// x64 always has SSE2; on x86 the build forces at least /arch:SSE2.
#if defined(_M_X64) || defined(_M_IX86) || defined(__SSE2__)
#define DECRACKLE_SSE2 1
#include <emmintrin.h>
#else
#define DECRACKLE_SSE2 0
#endif

namespace airwindows {

namespace {

// The Airwindows source uses fmin/fmax everywhere. Every value that reaches
// them here has already been through sanitizeInput(), so NaN can not occur and
// the plain comparison form - which MSVC turns into a single minsd/maxsd - is
// equivalent and considerably cheaper than a libm call.
inline double dmin(double a, double b) { return a < b ? a : b; }
inline double dmax(double a, double b) { return a > b ? a : b; }

const double kPi = 3.14159265358979;

// Below this the VST substitutes a tiny amount of noise, which is what keeps
// the recursive filters out of denormal territory.
const double kDenormalFloor = 1.18e-23;
const double kNoiseScale    = 1.18e-17;
// Nothing that is audio can be this large. Anything above it (including NaN and
// infinity from a broken decoder or an upstream DSP) is replaced by silence so
// it can never get trapped inside the recursive state.
const double kAbsurd = 1.0e30;

inline double sanitizeInput(double v, uint32_t fpd) {
    const double a = fabs(v);
    if (a < kDenormalFloor) return (double)fpd * kNoiseScale;
    // Deliberately written as a negated comparison so that NaN takes this path.
    if (!(a < kAbsurd)) return 0.0;
    return v;
}

// 2^e for e comfortably inside the normal double range, built straight from the
// exponent field. Replaces the pow(2, expon+62) call the VST makes per sample.
inline double pow2i(int e) {
    uint64_t bits = (uint64_t)(uint32_t)(e + 1023) << 52;
    double d;
    memcpy(&d, &bits, sizeof(d));
    return d;
}

// The exponent frexpf() would report for `v`, i.e. v = m * 2^e with
// 0.5 <= |m| < 1, without the libm call. Matches frexpf exactly for normals,
// subnormals and zero - including frexpf(+-0) reporting 0, which the VST
// relies on to keep dithering a silent output. Infinity and NaN leave the
// exponent unspecified in the C standard, so those return false and skip the
// dither entirely.
inline bool frexpExponent(float v, int & e) {
    uint32_t bits;
    memcpy(&bits, &v, sizeof(bits));
    const uint32_t biased = (bits >> 23) & 0xFFu;
    if (biased == 0xFFu) return false;
    if (biased != 0u) { e = (int)biased - 126; return true; }

    const uint32_t mantissa = bits & 0x7FFFFFu;
    if (mantissa == 0u) { e = 0; return true; }   // +-0
    uint32_t m = mantissa;
    int shift = 0;
    while ((m & 0x400000u) == 0u) { m <<= 1; ++shift; }
    e = -126 - shift;
    return true;
}

inline void advanceFpd(uint32_t & fpd) {
    fpd ^= fpd << 13; fpd ^= fpd >> 17; fpd ^= fpd << 5;
}

//! One step of the Airwindows 32 bit floating point dither. The generator is
//! stepped either way - the denormal guard on the input reads it - but the
//! dither term itself is only added when the output really is 32 bit float,
//! matching processReplacing vs processDoubleReplacing in the VST.
template<bool Dither>
inline double ditherStep(double sample, uint32_t & fpd);

template<> inline double ditherStep<true>(double sample, uint32_t & fpd) {
    int expon;
    const bool ok = frexpExponent((float)sample, expon);
    advanceFpd(fpd);
    if (!ok) return sample;
    return sample + (((double)fpd - 2147483647.0) * 5.5e-36 * pow2i(expon + 62));
}

template<> inline double ditherStep<false>(double sample, uint32_t & fpd) {
    advanceFpd(fpd);
    return sample;
}

// The VST's index arithmetic: a single conditional subtract rather than a true
// modulo. Reproduced exactly, then clamped so that pathological sample rate /
// window combinations can not walk off the delay line.
inline int wrapIdx(int x, int adjDelay) {
    int i = x - ((x > adjDelay) ? (adjDelay + 1) : 0);
    if (i < 0) i = 0;
    else if (i > (int)kDeCrackleBufLast) i = (int)kDeCrackleBufLast;
    return i;
}

inline bool isFinite(double v) { return v > -kAbsurd && v < kAbsurd; }

inline float sanitizeParam(float v) {
    // Negated comparisons so NaN lands on the default rather than propagating.
    if (!(v > 0.0f)) return 0.0f;
    if (!(v < 1.0f)) return 1.0f;
    return v;
}

} // anonymous namespace

// ---------------------------------------------------------------------------

void DeCrackleParams::sanitize() {
    filter    = sanitizeParam(filter);
    window    = sanitizeParam(window);
    threshold = sanitizeParam(threshold);
    surface   = sanitizeParam(surface);
    dryWet    = sanitizeParam(dryWet);
}

// ---------------------------------------------------------------------------

void DeCrackleCoeffs::compute(const DeCrackleParams & p, double sampleRate) {
    // audio_chunk's own limits; guards the divisions below.
    if (!(sampleRate >= 1000.0)) sampleRate = 44100.0;
    if (!(sampleRate <= 20000000.0)) sampleRate = 20000000.0;

    const double overallscale = sampleRate / 44100.0;
    const double A = p.filter, B = p.window, C = p.threshold;
    const double D = p.surface, E = p.dryWet;

    // These run once per parameter change, never per sample, so they stay in
    // the exact pow() form the VST uses.
    offset    = (int)(overallscale * 1.1);
    maxHeight = 1.0 * overallscale;

    filterOut = pow((A * 0.618) + 0.1, 2.0) / overallscale;
    filterRef = pow(((1.0 - B) * 0.618) + 0.1, 2.0) / overallscale;
    // Both of these are one-pole coefficients divided by overallscale, so below
    // roughly 22.7 kHz they can exceed 1 and the recursion diverges - the VST
    // has the same problem, it just never gets fed a 22 kHz file. Clamping at 1
    // keeps low rate material stable and cannot alter anything at 44.1 kHz or
    // above, where the unclamped values are 0.52 at the very most.
    if (filterOut > 1.0) filterOut = 1.0;
    if (filterRef > 1.0) filterRef = 1.0;
    oneMinusFilterOut = 1.0 - filterOut;
    oneMinusFilterRef = 1.0 - filterRef;

    iirCut = (pow(1.0 - B, 2.0) * 0.2) / overallscale;

    // kshort/8 == 200 in the original.
    adjDelay = (int)(B * (200.0 * overallscale)) - 2;
    if (adjDelay > (int)kDeCrackleShort - 1) adjDelay = (int)kDeCrackleShort - 1;

    halfTrig = (int)(dmin(0.5 + pow(B, 3.0), 0.999) * adjDelay);
    halfRaw  = (int)(0.5 * adjDelay);   // the original's halfBez is identical

    threshold = pow(C * 0.618, 2.0) - 0.1;
    surface   = (1.0 - pow(1.0 - D, 3.0)) * 0.9;
    wet       = E;

    blendWet  = (wet < 1.0 && wet > 0.0);
    deltaOnly = (wet == 0.0);
    surfaceOn = (surface > 0.0 && wet > 0.0);

    // outL is read (adjDelay - halfRaw) samples behind the write cursor.
    latencySamples = adjDelay - halfRaw;
    if (latencySamples < 0) latencySamples = 0;
}

// ---------------------------------------------------------------------------

DeCracklePair::DeCracklePair() {
    reset();
}

void DeCracklePair::reset() {
    memset(m_a, 0, sizeof(m_a));
    memset(m_b, 0, sizeof(m_b));
    memset(m_c, 0, sizeof(m_c));
    for (int x = 0; x < 6; ++x) {
        m_iirSampleL[x] = m_iirSampleR[x] = 0.0;
        m_iirAngleL[x]  = m_iirAngleR[x]  = 0.0;
        m_iirC[x]       = 0.0;
    }
    m_iirTargetL = m_iirTargetR = 0.0;
    m_iirClickL  = m_iirClickR  = 0.0;
    m_prevOutL   = m_prevOutR   = 0.0;
    m_count = 1;
    // Fixed seeds instead of the VST's rand(): a media player should render the
    // same file to the same bits every time.
    m_fpdL = 0x2C6A1F3Bu;
    m_fpdR = 0x5D8E2A97u;
}

bool DeCracklePair::stateIsFinite() const {
    for (int x = 0; x < 6; ++x) {
        if (!isFinite(m_iirSampleL[x]) || !isFinite(m_iirSampleR[x])) return false;
        if (!isFinite(m_iirAngleL[x])  || !isFinite(m_iirAngleR[x]))  return false;
        if (!isFinite(m_iirC[x])) return false;
    }
    return isFinite(m_iirTargetL) && isFinite(m_iirTargetR)
        && isFinite(m_iirClickL)  && isFinite(m_iirClickR)
        && isFinite(m_prevOutL)   && isFinite(m_prevOutR)
        && m_count >= 0 && m_count <= (int)kDeCrackleBufLast;
}

void DeCracklePair::clampCount(int adjDelay) {
    if (m_count < 0 || m_count > adjDelay) m_count = 0;
    if (m_count > (int)kDeCrackleBufLast) m_count = 0;
}

bool DeCracklePair::haveVectorPath() { return DECRACKLE_SSE2 != 0; }

void DeCracklePair::processStereoScalar(const DeCrackleCoeffs & k, float * left,
                                        float * right, size_t stride, size_t frames) {
    run<float, false>(k, left, right, stride, frames);
}

void DeCracklePair::processStereoScalar(const DeCrackleCoeffs & k, double * left,
                                        double * right, size_t stride, size_t frames) {
    run<double, false>(k, left, right, stride, frames);
}

void DeCracklePair::processStereo(const DeCrackleCoeffs & k, float * left,
                                  float * right, size_t stride, size_t frames) {
#if DECRACKLE_SSE2
    runStereoSSE2<float>(k, left, right, stride, frames);
#else
    run<float, false>(k, left, right, stride, frames);
#endif
}

void DeCracklePair::processStereo(const DeCrackleCoeffs & k, double * left,
                                  double * right, size_t stride, size_t frames) {
#if DECRACKLE_SSE2
    runStereoSSE2<double>(k, left, right, stride, frames);
#else
    run<double, false>(k, left, right, stride, frames);
#endif
}

void DeCracklePair::processMono(const DeCrackleCoeffs & k, float * chan,
                                size_t stride, size_t frames) {
    run<float, true>(k, chan, chan, stride, frames);
}

void DeCracklePair::processMono(const DeCrackleCoeffs & k, double * chan,
                                size_t stride, size_t frames) {
    run<double, true>(k, chan, chan, stride, frames);
}

template<typename Sample, bool Mono>
void DeCracklePair::run(const DeCrackleCoeffs & k, Sample * left, Sample * right,
                        size_t stride, size_t frames) {
    const bool kDither = (sizeof(Sample) == sizeof(float));
    const int    adjDelay  = k.adjDelay;
    const int    halfRaw   = k.halfRaw;
    const int    halfTrig  = k.halfTrig;
    const int    offset    = k.offset;
    const double fO        = k.filterOut;
    const double imFO      = k.oneMinusFilterOut;
    const double fR        = k.filterRef;
    const double imFR      = k.oneMinusFilterRef;
    const double iirCut    = k.iirCut;
    const double maxHeight = k.maxHeight;
    const double threshold = k.threshold;
    const double surface   = k.surface;
    const double wet       = k.wet;
    const double dry       = 1.0 - k.wet;
    const bool   blendWet  = k.blendWet;
    const bool   deltaOnly = k.deltaOnly;
    const bool   surfaceOn = k.surfaceOn;

    Frame  * const aA = m_a;
    Frame  * const aB = m_b;
    double * const aC = m_c;

    // Pulled into locals so the compiler can keep them in registers and does
    // not have to assume the float buffers alias our members.
    double iirSampleL[6], iirSampleR[6], iirAngleL[6], iirAngleR[6], iirC[6];
    for (int x = 0; x < 6; ++x) {
        iirSampleL[x] = m_iirSampleL[x]; iirAngleL[x] = m_iirAngleL[x];
        iirSampleR[x] = m_iirSampleR[x]; iirAngleR[x] = m_iirAngleR[x];
        iirC[x]       = m_iirC[x];
    }
    int      count      = m_count;
    uint32_t fpdL       = m_fpdL;
    uint32_t fpdR       = m_fpdR;
    double   iirTargetL = m_iirTargetL, iirTargetR = m_iirTargetR;
    double   iirClickL  = m_iirClickL,  iirClickR  = m_iirClickR;
    double   prevOutL   = m_prevOutL,   prevOutR   = m_prevOutR;

    for (size_t n = 0; n < frames; ++n) {
        const double drySampleL = sanitizeInput((double)*left, fpdL);
        const double drySampleR = Mono ? drySampleL
                                       : sanitizeInput((double)*right, fpdR);

        // Six pole lowpass producing the darkened audio that clicks are
        // replaced with.
        double bezL = drySampleL;
        for (int x = 0; x < 6; ++x) {
            iirAngleL[x] = (iirAngleL[x] * imFO) + ((bezL - iirSampleL[x]) * fO);
            const double u = (iirSampleL[x] + (iirAngleL[x] * fO)) * imFO;
            bezL          = u + (bezL * fO);
            iirSampleL[x] = u + (bezL * fO);
        }
        double bezR = 0.0;
        if (!Mono) {
            bezR = drySampleR;
            for (int x = 0; x < 6; ++x) {
                iirAngleR[x] = (iirAngleR[x] * imFO) + ((bezR - iirSampleR[x]) * fO);
                const double u = (iirSampleR[x] + (iirAngleR[x] * fO)) * imFO;
                bezR          = u + (bezR * fO);
                iirSampleR[x] = u + (bezR * fO);
            }
        }

        // Six pole lowpass on the rectified L*R cross product: the reference
        // level that clicks have to stand out from.
        double rect = fabs(drySampleL * drySampleR * 64.0);
        for (int x = 0; x < 6; ++x) {
            const double base = iirC[x] * imFR;
            rect    = fabs(base + (rect * fR));
            iirC[x] = base + (rect * fR);
        }

        aA[count].l = drySampleL;
        aB[count].l = bezL;
        if (!Mono) { aA[count].r = drySampleR; aB[count].r = bezR; }
        aC[count] = rect;

        ++count;
        if (count < 0 || count > adjDelay) count = 0;

        const double nearV = rect;
        const double farV  = aC[wrapIdx(count, adjDelay)];
        const double loud  = dmax(nearV, farV);

        // halfBez == halfRaw in the original, so this index serves both.
        const int    idxRaw  = wrapIdx(count + halfRaw, adjDelay);
        const int    idxPrev = wrapIdx(count + halfRaw + offset, adjDelay);
        const int    idxTrig = wrapIdx(count + halfTrig, adjDelay);

        const double prevL   = aA[idxPrev].l;
        const double outL    = aA[idxRaw].l;
        const double outBezL = aB[idxRaw].l;
        const double trigL   = aA[idxTrig].l;

        const double prevR   = Mono ? prevL   : aA[idxPrev].r;
        const double outR    = Mono ? outL    : aA[idxRaw].r;
        const double outBezR = Mono ? outBezL : aB[idxRaw].r;

        // pow(x, 3.0) with a known non-negative base is just the cube.
        const double dL = dmax((fabs(trigL) - threshold) - loud, 0.0) * 16.0;
        const double deClickL = dL * dL * dL;
        double deClickR = deClickL;
        if (!Mono) {
            const double trigR = aA[idxTrig].r;
            const double dR = dmax((fabs(trigR) - threshold) - loud, 0.0) * 16.0;
            deClickR = dR * dR * dR;
        }

        iirTargetL = dmax(iirTargetL - iirCut, 0.0);
        if (deClickL > iirTargetL) iirTargetL = dmin(deClickL, maxHeight);
        {   // the opposite channel gets to open the window too, at 0.618
            const double cross = deClickR * 0.618;
            if (cross > iirTargetL) iirTargetL = dmin(cross, maxHeight);
        }
        iirClickL = dmin(iirClickL + iirCut, iirTargetL);
        const double mixL = dmin(iirClickL, 1.0);
        double sampleL = (outBezL * mixL) + (outL * (1.0 - mixL));

        double sampleR = sampleL;
        if (!Mono) {
            iirTargetR = dmax(iirTargetR - iirCut, 0.0);
            if (deClickR > iirTargetR) iirTargetR = dmin(deClickR, maxHeight);
            const double cross = deClickL * 0.618;
            if (cross > iirTargetR) iirTargetR = dmin(cross, maxHeight);
            iirClickR = dmin(iirClickR + iirCut, iirTargetR);
            const double mixR = dmin(iirClickR, 1.0);
            sampleR = (outBezR * mixR) + (outR * (1.0 - mixR));
        }

        if (blendWet) {
            sampleL = (sampleL * wet) + (outL * dry);
            if (!Mono) sampleR = (sampleR * wet) + (outR * dry);
        } else if (deltaOnly) {
            // Full dry is the special "monitor only the clicks" mode.
            sampleL = outL - sampleL;
            if (!Mono) sampleR = outR - sampleR;
        }

        if (surfaceOn) {
            // Only evaluated when Surface is actually in use - this is where
            // the two sin() calls live.
            const double recordVolume = dmax(loud, dmax(prevL, prevR)) + 0.001;
            const double scale = surface / recordVolume;
            const double sL = sin(dmin(fabs(outL - prevL) * scale, kPi)) * 0.5;
            const double gate = dmax(surface - (recordVolume * surface * 4.0), 0.0);
            const double invGate = 1.0 - gate;

            sampleL  = (prevOutL * sL) + (sampleL * (1.0 - sL));
            sampleL  = (prevOutL * gate) + (sampleL * invGate);
            prevOutL = (prevOutL * gate) + (sampleL * invGate);

            if (!Mono) {
                const double sR = sin(dmin(fabs(outR - prevR) * scale, kPi)) * 0.5;
                sampleR  = (prevOutR * sR) + (sampleR * (1.0 - sR));
                sampleR  = (prevOutR * gate) + (sampleR * invGate);
                prevOutR = (prevOutR * gate) + (sampleR * invGate);
            }
        }

        sampleL = ditherStep<kDither>(sampleL, fpdL);
        *left = (Sample)sampleL;
        left += stride;

        if (!Mono) {
            sampleR = ditherStep<kDither>(sampleR, fpdR);
            *right = (Sample)sampleR;
            right += stride;
        }
    }

    for (int x = 0; x < 6; ++x) {
        m_iirSampleL[x] = iirSampleL[x]; m_iirAngleL[x] = iirAngleL[x];
        m_iirSampleR[x] = iirSampleR[x]; m_iirAngleR[x] = iirAngleR[x];
        m_iirC[x]       = iirC[x];
    }
    m_count      = count;
    m_fpdL       = fpdL;
    m_fpdR       = fpdR;
    m_iirTargetL = iirTargetL; m_iirTargetR = iirTargetR;
    m_iirClickL  = iirClickL;  m_iirClickR  = iirClickR;
    m_prevOutL   = prevOutL;   m_prevOutR   = prevOutR;
}

template void DeCracklePair::run<float,  false>(const DeCrackleCoeffs &, float *,  float *,  size_t, size_t);
template void DeCracklePair::run<float,  true >(const DeCrackleCoeffs &, float *,  float *,  size_t, size_t);
template void DeCracklePair::run<double, false>(const DeCrackleCoeffs &, double *, double *, size_t, size_t);
template void DeCracklePair::run<double, true >(const DeCrackleCoeffs &, double *, double *, size_t, size_t);

// ---------------------------------------------------------------------------
// SSE2 stereo kernel
//
// L lives in the low lane, R in the high lane. Every operation below is the
// packed twin of the scalar code above, in the same order and with the same
// associativity, so the two produce bit-identical results - packed IEEE-754
// doubles round exactly like scalar ones, and nothing here is reassociated or
// contracted into an FMA.
//
// Two things stay scalar because they genuinely are: the rectified control
// band (it is one value derived from L*R, then six serial poles) and sin().
// ---------------------------------------------------------------------------
#if DECRACKLE_SSE2

namespace {

//! mask ? a : b, without needing SSE4.1's blendvpd.
inline __m128d selectPd(__m128d mask, __m128d a, __m128d b) {
    return _mm_or_pd(_mm_and_pd(mask, a), _mm_andnot_pd(mask, b));
}

inline __m128d swapLanes(__m128d v) { return _mm_shuffle_pd(v, v, 1); }

inline double lowLane(__m128d v)  { return _mm_cvtsd_f64(v); }
inline double highLane(__m128d v) { return _mm_cvtsd_f64(_mm_unpackhi_pd(v, v)); }

template<typename Sample>
inline __m128d loadPair(const Sample * l, const Sample * r) {
    return _mm_set_pd((double)*r, (double)*l);      // hi = R, lo = L
}

} // anonymous namespace

template<typename Sample>
void DeCracklePair::runStereoSSE2(const DeCrackleCoeffs & k, Sample * left,
                                  Sample * right, size_t stride, size_t frames) {
    const bool kDither = (sizeof(Sample) == sizeof(float));

    const int    adjDelay  = k.adjDelay;
    const int    halfRaw   = k.halfRaw;
    const int    halfTrig  = k.halfTrig;
    const int    offset    = k.offset;
    const double fRs       = k.filterRef;
    const double imFRs     = k.oneMinusFilterRef;
    const double surface   = k.surface;
    const bool   blendWet  = k.blendWet;
    const bool   deltaOnly = k.deltaOnly;
    const bool   surfaceOn = k.surfaceOn;

    const __m128d vFO    = _mm_set1_pd(k.filterOut);
    const __m128d vImFO  = _mm_set1_pd(k.oneMinusFilterOut);
    const __m128d vCut   = _mm_set1_pd(k.iirCut);
    const __m128d vMaxH  = _mm_set1_pd(k.maxHeight);
    const __m128d vThr   = _mm_set1_pd(k.threshold);
    const __m128d vWet   = _mm_set1_pd(k.wet);
    const __m128d vDry   = _mm_set1_pd(1.0 - k.wet);
    const __m128d vZero  = _mm_setzero_pd();
    const __m128d vOne   = _mm_set1_pd(1.0);
    const __m128d vP618  = _mm_set1_pd(0.618);
    const __m128d v16    = _mm_set1_pd(16.0);
    const __m128d vPi    = _mm_set1_pd(kPi);
    const __m128d vFloor = _mm_set1_pd(kDenormalFloor);
    const __m128d vHuge  = _mm_set1_pd(kAbsurd);
    const __m128d vNoise = _mm_set1_pd(kNoiseScale);
    const __m128d vAbs   = _mm_castsi128_pd(_mm_set_epi32(0x7FFFFFFF, ~0, 0x7FFFFFFF, ~0));

    Frame  * const aA = m_a;
    Frame  * const aB = m_b;
    double * const aC = m_c;

    __m128d vSample[6], vAngle[6];
    for (int x = 0; x < 6; ++x) {
        vSample[x] = _mm_set_pd(m_iirSampleR[x], m_iirSampleL[x]);
        vAngle[x]  = _mm_set_pd(m_iirAngleR[x],  m_iirAngleL[x]);
    }
    double iirC[6];
    for (int x = 0; x < 6; ++x) iirC[x] = m_iirC[x];

    __m128d vTarget  = _mm_set_pd(m_iirTargetR, m_iirTargetL);
    __m128d vClick   = _mm_set_pd(m_iirClickR,  m_iirClickL);
    __m128d vPrevOut = _mm_set_pd(m_prevOutR,   m_prevOutL);
    int      count = m_count;
    uint32_t fpdL = m_fpdL, fpdR = m_fpdR;

    for (size_t n = 0; n < frames; ++n) {
        // --- input + denormal / poison guard, both lanes at once ------------
        __m128d dry = loadPair<Sample>(left, right);
        {
            const __m128d mag   = _mm_and_pd(dry, vAbs);
            const __m128d small = _mm_cmplt_pd(mag, vFloor);
            // cmplt is false for NaN, so this also zeroes NaN and infinity.
            const __m128d inRange = _mm_cmplt_pd(mag, vHuge);
            const __m128d noise = _mm_mul_pd(
                _mm_set_pd((double)fpdR, (double)fpdL), vNoise);
            dry = selectPd(small, noise, _mm_and_pd(inRange, dry));
        }

        // --- six pole lowpass, L and R in parallel ---------------------------
        __m128d bez = dry;
        for (int x = 0; x < 6; ++x) {
            vAngle[x] = _mm_add_pd(_mm_mul_pd(vAngle[x], vImFO),
                                   _mm_mul_pd(_mm_sub_pd(bez, vSample[x]), vFO));
            const __m128d u = _mm_mul_pd(
                _mm_add_pd(vSample[x], _mm_mul_pd(vAngle[x], vFO)), vImFO);
            bez        = _mm_add_pd(u, _mm_mul_pd(bez, vFO));
            vSample[x] = _mm_add_pd(u, _mm_mul_pd(bez, vFO));
        }

        // --- rectified control band: one value, six serial poles -------------
        double rect = fabs(lowLane(_mm_mul_sd(dry, _mm_unpackhi_pd(dry, dry))) * 64.0);
        for (int x = 0; x < 6; ++x) {
            const double base = iirC[x] * imFRs;
            rect    = fabs(base + (rect * fRs));
            iirC[x] = base + (rect * fRs);
        }

        _mm_storeu_pd(&aA[count].l, dry);
        _mm_storeu_pd(&aB[count].l, bez);
        aC[count] = rect;

        ++count;
        if (count < 0 || count > adjDelay) count = 0;

        const double farV = aC[wrapIdx(count, adjDelay)];
        const double loud = dmax(rect, farV);
        const __m128d vLoud = _mm_set1_pd(loud);

        const int idxRaw  = wrapIdx(count + halfRaw, adjDelay);
        const int idxPrev = wrapIdx(count + halfRaw + offset, adjDelay);
        const int idxTrig = wrapIdx(count + halfTrig, adjDelay);

        const __m128d prev   = _mm_loadu_pd(&aA[idxPrev].l);
        const __m128d out    = _mm_loadu_pd(&aA[idxRaw].l);
        const __m128d outBez = _mm_loadu_pd(&aB[idxRaw].l);
        const __m128d trig   = _mm_loadu_pd(&aA[idxTrig].l);

        // --- click detector ---------------------------------------------------
        __m128d d = _mm_max_pd(
            _mm_sub_pd(_mm_sub_pd(_mm_and_pd(trig, vAbs), vThr), vLoud), vZero);
        d = _mm_mul_pd(d, v16);
        const __m128d deClick = _mm_mul_pd(_mm_mul_pd(d, d), d);

        vTarget = _mm_max_pd(_mm_sub_pd(vTarget, vCut), vZero);
        vTarget = selectPd(_mm_cmpgt_pd(deClick, vTarget),
                           _mm_min_pd(deClick, vMaxH), vTarget);
        // The opposite channel gets to open the window too, at 0.618.
        const __m128d cross = _mm_mul_pd(swapLanes(deClick), vP618);
        vTarget = selectPd(_mm_cmpgt_pd(cross, vTarget),
                           _mm_min_pd(cross, vMaxH), vTarget);

        vClick = _mm_min_pd(_mm_add_pd(vClick, vCut), vTarget);
        const __m128d mix = _mm_min_pd(vClick, vOne);
        __m128d sample = _mm_add_pd(_mm_mul_pd(outBez, mix),
                                    _mm_mul_pd(out, _mm_sub_pd(vOne, mix)));

        if (blendWet) {
            sample = _mm_add_pd(_mm_mul_pd(sample, vWet), _mm_mul_pd(out, vDry));
        } else if (deltaOnly) {
            sample = _mm_sub_pd(out, sample);
        }

        if (surfaceOn) {
            const double recordVolume =
                dmax(loud, dmax(lowLane(prev), highLane(prev))) + 0.001;
            const double scale = surface / recordVolume;
            const __m128d arg = _mm_min_pd(
                _mm_mul_pd(_mm_and_pd(_mm_sub_pd(out, prev), vAbs),
                           _mm_set1_pd(scale)), vPi);
            // sin() is the one thing with no packed equivalent here.
            const __m128d s = _mm_set_pd(sin(highLane(arg)) * 0.5,
                                         sin(lowLane(arg)) * 0.5);
            const double gate = dmax(surface - (recordVolume * surface * 4.0), 0.0);
            const __m128d vGate    = _mm_set1_pd(gate);
            const __m128d vInvGate = _mm_set1_pd(1.0 - gate);

            sample   = _mm_add_pd(_mm_mul_pd(vPrevOut, s),
                                  _mm_mul_pd(sample, _mm_sub_pd(vOne, s)));
            sample   = _mm_add_pd(_mm_mul_pd(vPrevOut, vGate),
                                  _mm_mul_pd(sample, vInvGate));
            vPrevOut = _mm_add_pd(_mm_mul_pd(vPrevOut, vGate),
                                  _mm_mul_pd(sample, vInvGate));
        }

        // --- dither and store -------------------------------------------------
        double sl = lowLane(sample);
        double sr = highLane(sample);
        sl = ditherStep<kDither>(sl, fpdL);
        sr = ditherStep<kDither>(sr, fpdR);
        *left  = (Sample)sl;
        *right = (Sample)sr;
        left  += stride;
        right += stride;
    }

    for (int x = 0; x < 6; ++x) {
        m_iirSampleL[x] = lowLane(vSample[x]);
        m_iirSampleR[x] = highLane(vSample[x]);
        m_iirAngleL[x]  = lowLane(vAngle[x]);
        m_iirAngleR[x]  = highLane(vAngle[x]);
        m_iirC[x]       = iirC[x];
    }
    m_count      = count;
    m_fpdL       = fpdL;
    m_fpdR       = fpdR;
    m_iirTargetL = lowLane(vTarget);  m_iirTargetR = highLane(vTarget);
    m_iirClickL  = lowLane(vClick);   m_iirClickR  = highLane(vClick);
    m_prevOutL   = lowLane(vPrevOut); m_prevOutR   = highLane(vPrevOut);
}

template void DeCracklePair::runStereoSSE2<float>(const DeCrackleCoeffs &, float *, float *, size_t, size_t);
template void DeCracklePair::runStereoSSE2<double>(const DeCrackleCoeffs &, double *, double *, size_t, size_t);

#endif // DECRACKLE_SSE2

} // namespace airwindows
