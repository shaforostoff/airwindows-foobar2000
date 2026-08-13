/* ========================================
 *  foo_dsp_declick - portable DSP core
 * ======================================== */

#include "declick_core.h"

#include <algorithm>
#include <math.h>
#include <string.h>

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
#include <xmmintrin.h>
#define DECLICK_HAVE_MXCSR 1
#endif

namespace declick {

#if defined(DECLICK_HAVE_MXCSR)
// FTZ only. DAZ lives in a bit that early SSE2 parts treat as reserved.
scoped_flush_denormals::scoped_flush_denormals() : m_saved(_mm_getcsr()) {
    if ((m_saved & 0x8000u) == 0u) _mm_setcsr(m_saved | 0x8000u);
}
scoped_flush_denormals::~scoped_flush_denormals() {
    if ((m_saved & 0x8000u) == 0u) _mm_setcsr(m_saved);
}
#endif

namespace {

inline double dmin(double a, double b) { return a < b ? a : b; }
inline double dmax(double a, double b) { return a > b ? a : b; }

inline float clampf(float v, float lo, float hi) {
    if (!(v > lo)) return lo;        // negated, so NaN lands on lo
    if (!(v < hi)) return hi;
    return v;
}

const double kAbsurd = 1.0e30;

inline bool finite(double v) { return v > -kAbsurd && v < kAbsurd; }

} // anonymous namespace

// ---------------------------------------------------------------------------

void Params::sanitize() {
    sensitivity = clampf(sensitivity, 0.0f, 1.0f);
    extent      = clampf(extent, 0.0f, 1.0f);
    depth       = clampf(depth, 0.0f, 1.0f);
    maxLengthMs = clampf(maxLengthMs, 0.2f, 20.0f);
    dryWet      = clampf(dryWet, 0.0f, 1.0f);
    if (passes < 1) passes = 1;
    if (passes > 3) passes = 3;
    if (order < (int)kMinOrder) order = (int)kMinOrder;
    if (order > (int)kMaxOrder) order = (int)kMaxOrder;
    order &= ~1;                                   // keep it even
}

bool Params::operator==(const Params & o) const {
    return sensitivity == o.sensitivity && extent == o.extent
        && maxLengthMs == o.maxLengthMs && depth == o.depth && passes == o.passes
        && order == o.order && dryWet == o.dryWet;
}

// ---------------------------------------------------------------------------

void Config::compute(const Params & pIn, double sampleRate) {
    Params p = pIn;
    p.sanitize();

    if (!(sampleRate >= 1000.0)) sampleRate = 44100.0;
    if (!(sampleRate <= 20000000.0)) sampleRate = 20000000.0;

    order  = p.order;
    passes = p.passes;
    wet    = p.dryWet;
    depth  = p.depth;

    // Sensitivity 0..1 maps onto the trigger threshold, in robust sigmas.
    // 6.0 catches only unmistakable clicks; 2.5 is aggressive enough to start
    // costing real music.
    thresholdHi = 6.0 - 3.5 * (double)p.sensitivity;
    // The hysteresis floor: how far a detection spreads into its own tail.
    thresholdLo = dmin(3.0 - 2.0 * (double)p.extent, thresholdHi);

    maxRun = (int)(p.maxLengthMs * 1e-3 * sampleRate);
    if (maxRun < 2) maxRun = 2;
    if (maxRun > 2048) maxRun = 2048;

    // The robust noise estimate must span much more than the average gap
    // between clicks, or it ends up measuring the crackle instead of the
    // music and the detector loses its contrast.
    madWindow = (int)(0.030 * sampleRate);
    if (madWindow < 256) madWindow = 256;
    if (madWindow > 1 << 16) madWindow = 1 << 16;

    // Context either side of a block: enough for the longest repair plus the
    // interpolation window, which itself needs at least `order` on each side
    // for the normal equations to stay Toeplitz.
    pad = maxRun + 3 * order;
    latency = pad + (int)kBlock;

    // The envelope the buffers are sized from. Same clamps as maxRun above, but
    // at Params::sanitize()'s 20 ms ceiling and kMaxOrder, so no parameter
    // setting at this sample rate can outgrow them.
    bufOrder  = (int)kMaxOrder;
    bufMaxRun = (int)(20.0 * 1e-3 * sampleRate);
    if (bufMaxRun < 2) bufMaxRun = 2;
    if (bufMaxRun > 2048) bufMaxRun = 2048;
    if (bufMaxRun < maxRun) bufMaxRun = maxRun;      // belt and braces
    bufPad = bufMaxRun + 3 * bufOrder;

    if (maxBlock < (int)kBlock) maxBlock = (int)kBlock;
}

// ---------------------------------------------------------------------------

bool levinson(const double * r, int order, double * a) {
    a[0] = 1.0;
    for (int i = 1; i <= order; ++i) a[i] = 0.0;
    double err = r[0];
    if (!(err > 0.0)) return false;

    for (int i = 1; i <= order; ++i) {
        double acc = r[i];
        for (int j = 1; j < i; ++j) acc += a[j] * r[i - j];
        const double k = -acc / err;
        if (!(k > -1.0 && k < 1.0)) return i > 1;   // lost positive definiteness
        // Symmetric update; the second half must read pre-update values.
        for (int j = 1; j <= i / 2; ++j) {
            const double aj = a[j];
            const double ai = a[i - j];
            a[j]     = aj + k * ai;
            a[i - j] = ai + k * aj;
        }
        a[i] = k;
        err *= (1.0 - k * k);
        if (!(err > 0.0)) return i > 1;
    }
    return true;
}

bool solveBandedToeplitz(const double * d, int band, double * b, int n,
                         std::vector<double> & scratch) {
    if (n <= 0) return false;
    if (band >= n) band = n - 1;
    const int W = band + 1;
    scratch.assign((size_t)n * (size_t)W, 0.0);
    double * L = scratch.empty() ? NULL : &scratch[0];
    if (L == NULL) return false;

    // L(i, j) lives at L[i*W + (i-j)], valid for 0 <= i-j <= band.
    for (int i = 0; i < n; ++i) {
        const int jlo = (i - band > 0) ? (i - band) : 0;
        for (int j = jlo; j <= i; ++j) {
            const int dij = i - j;
            double s = d[dij];
            for (int k = jlo; k < j; ++k) {
                s -= L[(size_t)i * W + (i - k)] * L[(size_t)j * W + (j - k)];
            }
            if (j == i) {
                if (!(s > 0.0) || !finite(s)) return false;
                L[(size_t)i * W] = sqrt(s);
            } else {
                L[(size_t)i * W + dij] = s / L[(size_t)j * W];
            }
        }
    }

    for (int i = 0; i < n; ++i) {                       // forward substitution
        double s = b[i];
        const int jlo = (i - band > 0) ? (i - band) : 0;
        for (int j = jlo; j < i; ++j) s -= L[(size_t)i * W + (i - j)] * b[j];
        b[i] = s / L[(size_t)i * W];
    }
    for (int i = n - 1; i >= 0; --i) {                  // back substitution
        double s = b[i];
        const int jhi = (i + band < n - 1) ? (i + band) : (n - 1);
        for (int j = i + 1; j <= jhi; ++j) s -= L[(size_t)j * W + (j - i)] * b[j];
        b[i] = s / L[(size_t)i * W];
    }
    for (int i = 0; i < n; ++i) {
        if (!finite(b[i])) return false;
    }
    return true;
}

bool bandedInverseDiagonal(const std::vector<double> & chol, int band, int n,
                           std::vector<double> & work, double * diag) {
    if (n <= 0 || diag == NULL) return false;
    if (band >= n) band = n - 1;                  // same clipping as the solve
    const int W = band + 1;
    if (chol.size() < (size_t)n * (size_t)W) return false;
    const double * L = &chol[0];

    // S holds the banded slice of the inverse: S(i,j), 0 <= j-i <= band, at
    // work[i*W + (j-i)]. Rows are filled bottom to top and, within a row,
    // right to left - the j == i entry is last because it is the one that
    // needs the rest of its own row.
    work.assign((size_t)n * (size_t)W, 0.0);
    double * S = &work[0];

    for (int i = n - 1; i >= 0; --i) {
        const double lii = L[(size_t)i * W];
        if (!(lii > 0.0) || !finite(lii)) return false;
        const int khi = (i + band < n - 1) ? (i + band) : (n - 1);

        for (int j = khi; j >= i; --j) {
            double s = (i == j) ? 1.0 / (lii * lii) : 0.0;
            for (int k = i + 1; k <= khi; ++k) {
                const double lki = L[(size_t)k * W + (k - i)] / lii;
                const double skj = (j >= k) ? S[(size_t)k * W + (j - k)]
                                            : S[(size_t)j * W + (k - j)];
                s -= lki * skj;
            }
            S[(size_t)i * W + (j - i)] = s;
        }

        const double v = S[(size_t)i * W];
        if (!(v > 0.0) || !finite(v)) return false;   // G is PD; this must hold
        diag[i] = v;
    }
    return true;
}

// ---------------------------------------------------------------------------

Channel::Channel() {
    Config c;
    c.compute(Params::defaults(), 44100.0);
    configure(c);
}

void Channel::configure(const Config & cfg) {
    m_cfg = cfg;
    m_pad = cfg.pad;

    // Everything below is sized from cfg's buf* envelope rather than from the
    // parameters in force, so that reconfiguring after a Max repair or Model
    // order change reassigns each vector to the size it already has. See the
    // note on Config::bufOrder: the audio thread must not allocate, and this is
    // what buys that. The algorithm still runs at cfg.order / cfg.maxRun; these
    // only decide how much room is standing by.
    const size_t bo   = (size_t)cfg.bufOrder;
    const size_t bm   = (size_t)cfg.bufMaxRun;
    const size_t need = (size_t)(2 * cfg.bufPad + (int)kBlock) + 8;

    m_win.assign(need, 0.0);
    m_dry.assign(need, 0.0);
    m_flag.assign(need, 0);
    m_fwd.assign(need, 0.0);
    m_bwd.assign(need, 0.0);
    m_det.assign(need, 0.0);
    m_a.assign(bo + 1, 0.0);
    m_ra.assign(bo + 1, 0.0);
    m_madRing.assign((size_t)cfg.madWindow, 0.0);

    // The Wiener path only runs when wienerAlpha > 0, and its two buffers are
    // the largest things here - at 192 kHz they are 1.06 MB of a 1.3 MB total -
    // so there is no reason to hold them when it cannot run. interpolate()
    // short-circuits on wienerAlpha before it touches either.
    if (cfg.wienerAlpha > 0.0) {
        m_pvar.assign(bm + 1, 0.0);
        m_sinv.assign((bm + 1) * (bo + 1), 0.0);
    } else {
        m_pvar.clear();
        m_sinv.clear();
    }

    // Per-block and per-click scratch, at the worst case interpolate() and
    // fitModel() can ask for. They are still assign()ed and resize()d to the
    // exact length needed each time, which is what zeroes them; reserving here
    // is what stops that from ever reallocating.
    const size_t ctx = 3 * bo;
    m_scratch.reserve(need > (size_t)cfg.madWindow ? need : (size_t)cfg.madWindow);
    m_seg.reserve(bm + 2 * ctx + 1);          // L    = m + 2*ctx
    m_err.reserve(bm + 2 * ctx + 1);          // rows = L - order
    m_rhs.reserve(bm + 1);                    // m
    m_solve.reserve((bm + 1) * (bo + 1));     // n * (band+1)

    // The output ring. Between one pull and the next a caller can push maxBlock
    // samples, which emit at most maxBlock + kBlock, on top of the up to kBlock
    // already waiting.
    const size_t ring = (size_t)cfg.maxBlock + 2 * (size_t)kBlock;
    if (m_out.size() != ring) m_out.assign(ring, 0.0);

    reset();
}

size_t Channel::heapBytes() const {
    return (m_win.capacity() + m_dry.capacity() + m_fwd.capacity()
            + m_bwd.capacity() + m_det.capacity() + m_a.capacity()
            + m_ra.capacity() + m_pvar.capacity() + m_sinv.capacity()
            + m_madRing.capacity() + m_scratch.capacity() + m_seg.capacity()
            + m_err.capacity() + m_rhs.capacity() + m_solve.capacity()
            + m_out.capacity()) * sizeof(double)
           + m_flag.capacity();
}

bool Channel::retune(const Config & cfg) {
    if (!m_cfg.structurallyEquals(cfg)) return false;
    // Everything left is read fresh per block, so there is nothing to rebuild:
    // thresholds and depth are picked up by the next detect()/interpolate(),
    // wet by the next emit, passes by the next processBlock().
    m_cfg = cfg;
    return true;
}

void Channel::reset() {
    std::fill(m_win.begin(), m_win.end(), 0.0);
    std::fill(m_dry.begin(), m_dry.end(), 0.0);
    std::fill(m_flag.begin(), m_flag.end(), (uint8_t)0);
    std::fill(m_madRing.begin(), m_madRing.end(), 0.0);
    m_madPos = 0;
    m_madFull = false;
    m_scale = 1e-6;
    // m_out.size() is the ring's capacity, so this empties it without giving
    // the storage back - clear() would leave a zero-length ring.
    m_outHead = 0;
    m_outCount = 0;
    m_repaired = 0;
    m_seen = 0;
    // Prime with `pad` zeros so the first real sample lands at window index
    // pad and nothing is dropped off the front of the stream.
    m_fill = m_pad;
    m_primed = true;
}

void Channel::fitModel(int from, int to) {
    const int order = m_cfg.order;
    const int n = to - from;
    if (n < 4 * order + 4) {
        m_a[0] = 1.0;
        for (int i = 1; i <= order; ++i) m_a[i] = 0.0;
        return;
    }

    // Guard against gross outliers only - a dropout, a splice, a digital
    // full-scale spike. It does NOT protect the fit from crackle, despite
    // looking like it should: on 78 rpm material the threshold lands near
    // -8 dBFS while the clicks themselves sit at -40 to -50 dBFS, so not one
    // of them is touched. That turns out not to matter. Fitting on the true
    // clean signal instead - the best any robust estimator could ever do - is
    // worth at most +0.3 dB of reconstruction SNR, measured against
    // injected-click ground truth at contamination from 0.7% to 5.2%.
    // Crackle is dense but tiny, and a sum over a thousand lag products
    // barely notices it. See the README.
    m_scratch.resize((size_t)n);
    double acc = 0.0;
    for (int i = 0; i < n; ++i) acc += fabs(m_win[from + i]);
    const double lim = 6.0 * (acc / (double)n) + 1e-12;

    const double twoPi = 6.283185307179586;
    for (int i = 0; i < n; ++i) {
        double v = m_win[from + i];
        if (v > lim) v = lim;
        else if (v < -lim) v = -lim;
        // Hann, so the autocorrelation stays positive definite.
        const double w = 0.5 - 0.5 * cos(twoPi * (double)i / (double)(n - 1));
        m_scratch[i] = v * w;
    }

    m_ra.assign((size_t)order + 1, 0.0);
    for (int d = 0; d <= order; ++d) {
        double s = 0.0;
        for (int i = d; i < n; ++i) s += m_scratch[i] * m_scratch[i - d];
        m_ra[d] = s;
    }
    if (!(m_ra[0] > 0.0)) {
        m_a[0] = 1.0;
        for (int i = 1; i <= order; ++i) m_a[i] = 0.0;
        return;
    }
    m_ra[0] *= 1.0000001;                       // ridge, keeps Levinson stable
    if (!levinson(&m_ra[0], order, &m_a[0])) {
        // Partial model is fine; levinson() leaves what it managed to build.
    }
    for (int i = 0; i <= order; ++i) {
        if (!finite(m_a[i])) {
            m_a[0] = 1.0;
            for (int j = 1; j <= order; ++j) m_a[j] = 0.0;
            break;
        }
    }
}

void Channel::detect(int from, int to, int pass) {
    const int order = m_cfg.order;

    // Forward and backward prediction error. A click blows up the forward
    // error at its onset and the backward error at its offset; taking the max
    // of the two captures the whole burst instead of just its leading edge.
    for (int i = from; i < to; ++i) {
        double s = 0.0;
        for (int k = 0; k <= order; ++k) s += m_a[k] * m_win[i - k];
        m_fwd[i] = s;
    }
    for (int i = to - 1; i >= from; --i) {
        double s = 0.0;
        for (int k = 0; k <= order; ++k) s += m_a[k] * m_win[i + k];
        m_bwd[i] = s;
    }

    if (pass == 0) {
        // Refresh the robust noise estimate from this block's forward error.
        for (int i = from; i < to; ++i) {
            m_madRing[(size_t)m_madPos] = fabs(m_fwd[i]);
            if (++m_madPos >= (int)m_madRing.size()) { m_madPos = 0; m_madFull = true; }
        }
        const size_t have = m_madFull ? m_madRing.size() : (size_t)m_madPos;
        if (have >= 16) {
            m_scratch.assign(m_madRing.begin(), m_madRing.begin() + (ptrdiff_t)have);
            const size_t mid = have / 2;
            std::nth_element(m_scratch.begin(), m_scratch.begin() + (ptrdiff_t)mid,
                             m_scratch.end());
            const double med = m_scratch[mid] * 1.4826;
            m_scale = dmax(med, 1e-12);
        }
    }

    const double inv = 1.0 / m_scale;
    for (int i = from; i < to; ++i) {
        m_det[i] = dmax(fabs(m_fwd[i]), fabs(m_bwd[i])) * inv;
    }
}

void Channel::interpolate(int from, int to) {
    const int order = m_cfg.order;
    const int ctx = 3 * order;
    const double hi = m_cfg.thresholdHi;
    const double lo = m_cfg.thresholdLo;

    int i = from;
    while (i < to) {
        if (m_det[i] <= hi || m_flag[i]) { ++i; continue; }

        // Hysteresis: walk outwards while the detection function is still
        // above the low threshold.
        int s = i, e = i;
        while (s - 1 >= from && m_det[s - 1] > lo && !m_flag[s - 1]) --s;
        while (e + 1 < to && m_det[e + 1] > lo && !m_flag[e + 1]) ++e;
        // One sample of slack either side; a repair that stops exactly at the
        // damage boundary tends to leave a step.
        if (s > from) --s;
        if (e < to - 1) ++e;

        const int m = e - s + 1;
        i = e + 1;
        if (m <= 0 || m > m_cfg.maxRun) continue;      // too long: that is music

        const int lo0 = s - ctx;
        const int hi0 = e + ctx;
        if (lo0 < 0 || hi0 >= m_fill) continue;

        const int L = hi0 - lo0 + 1;
        const int rows = L - order;
        if (rows <= m) continue;

        // Prediction error of the segment with the unknown run zeroed.
        m_seg.assign((size_t)L, 0.0);
        for (int t = 0; t < L; ++t) {
            const int idx = lo0 + t;
            m_seg[(size_t)t] = (idx >= s && idx <= e) ? 0.0 : m_win[idx];
        }
        m_err.assign((size_t)rows, 0.0);
        for (int r = 0; r < rows; ++r) {
            double acc = 0.0;
            for (int k = 0; k <= order; ++k) acc += m_a[k] * m_seg[(size_t)(r + order - k)];
            m_err[(size_t)r] = acc;
        }

        // rhs[j] = -sum over the rows touching column c of a[order-(c-i)]*err[i]
        m_rhs.assign((size_t)m, 0.0);
        for (int j = 0; j < m; ++j) {
            const int c = (s - lo0) + j;
            double acc = 0.0;
            const int r0 = (c - order > 0) ? (c - order) : 0;
            const int r1 = (c < rows - 1) ? c : (rows - 1);
            for (int r = r0; r <= r1; ++r) acc += m_a[order - (c - r)] * m_err[(size_t)r];
            m_rhs[(size_t)j] = -acc;
        }

        // G is symmetric banded Toeplitz with first column ra[0..order],
        // where ra is the autocorrelation of the AR coefficients. That holds
        // exactly as long as ctx >= order, which it is.
        for (int d = 0; d <= order; ++d) {
            double acc = 0.0;
            for (int k = 0; k + d <= order; ++k) acc += m_a[k] * m_a[k + d];
            m_ra[(size_t)d] = acc;
        }
        m_ra[0] *= 1.0 + 1e-9;

        if (!solveBandedToeplitz(&m_ra[0], order, &m_rhs[0], m, m_solve)) continue;

        // Optional per-sample confidence. Under the model's Gaussian
        // innovation the posterior covariance of the interpolated run is
        // sigma^2 * G^-1, and the solve already produced the factor of G, so
        // the diagonal costs one more banded recursion. Off by default; see
        // Config::wienerAlpha for why.
        // The && matters: with wienerAlpha at 0 the two Wiener buffers are not
        // even allocated, and nothing to the right of it is evaluated.
        const bool haveVar =
            m_cfg.wienerAlpha > 0.0 && !m_pvar.empty() &&
            bandedInverseDiagonal(m_solve, order, m, m_sinv, &m_pvar[0]);

        // Never let a repair leave the local dynamic range.
        double localMax = 0.0;
        for (int t = lo0; t <= hi0; ++t) {
            if (t >= s && t <= e) continue;
            const double v = fabs(m_win[t]);
            if (v > localMax) localMax = v;
        }
        const double limit = 1.5 * dmax(localMax, 1e-9);

        bool ok = true;
        for (int j = 0; j < m; ++j) {
            if (!finite(m_rhs[(size_t)j])) { ok = false; break; }
        }
        if (!ok) continue;

        // Subtractive repair. A click ADDS to the music, so the damaged
        // samples still carry the signal underneath: what to remove is the
        // discrepancy d = x - v, not the sample. Subtracting all of d means
        // trusting the estimate completely, and over a hole of any size the
        // estimate is not that good - 78 rpm material interpolates at about
        // 30 dB SNR over 3 samples, 22 dB over 6 and 8 dB over 16, while the
        // click itself sits only ~18 dB below the music. Removing a fixed
        // fraction keeps most of the click while leaving the underlying signal
        // where the estimate would have done more harm than good.
        const double sigma2 = m_scale * m_scale;
        const double depth = m_cfg.depth;
        const double alpha = m_cfg.wienerAlpha;
        const double wcap = m_cfg.wienerMax;
        int touched = 0;

        for (int j = 0; j < m; ++j) {
            double v = m_rhs[(size_t)j];
            if (v > limit) v = limit;
            else if (v < -limit) v = -limit;

            const double x = m_win[s + j];
            const double dj = x - v;

            // The Wiener gain reduces to 1 wherever the discrepancy dwarfs the
            // estimate's own uncertainty, which on this material is almost
            // everywhere - hence the flat default.
            double w = 1.0;
            if (haveVar) {
                const double P = alpha * sigma2 * m_pvar[(size_t)j];
                const double d2 = dj * dj;
                w = (d2 > P) ? (1.0 - P / d2) : 0.0;
            }
            if (w > wcap) w = wcap;
            w += depth * (1.0 - w);   // at depth 1 this is outright replacement

            if (w > 0.0) {
                m_win[s + j] = x - w * dj;
                ++touched;
            }
            // Flagged either way: the run has been dealt with, and a later
            // pass must not come back and attack it again.
            m_flag[s + j] = 1;
        }
        m_repaired += (uint64_t)touched;
    }
}

void Channel::processBlock() {
    const int order = m_cfg.order;
    const int blk = (int)kBlock;

    // Detect over the block plus a margin, so a burst that starts near the end
    // of the block can still be followed into the next one.
    const int from = m_pad;
    const int to = m_pad + blk + m_cfg.maxRun;
    const int dFrom = (from - order > 0) ? (from - order) : order;
    const int dTo = (to < m_fill - order) ? to : (m_fill - order);

    for (int pass = 0; pass < m_cfg.passes; ++pass) {
        if (dTo <= dFrom) break;
        // Refit each pass: once the worst clicks are gone the model fits the
        // music better, which is the whole point of running more than one.
        fitModel(0, m_fill);
        detect(dFrom, dTo, pass);
        // Only commit repairs whose burst STARTS inside this block; anything
        // starting later belongs to the next block, which will see it with
        // full context on both sides.
        interpolate(m_pad, (m_pad + blk < dTo) ? (m_pad + blk) : dTo);
    }
}

void Channel::pushOne(double v) {
    const int blk = (int)kBlock;
    const int capacity = 2 * m_pad + blk;

    if (!finite(v)) v = 0.0;
    m_win[(size_t)m_fill] = v;
    m_dry[(size_t)m_fill] = v;
    m_flag[(size_t)m_fill] = 0;
    ++m_fill;
    ++m_seen;

    if (m_fill < capacity) return;

    processBlock();

    const double wet = m_cfg.wet;
    const double dry = 1.0 - wet;
    for (int i = 0; i < blk; ++i) {
        const double y = m_win[(size_t)(m_pad + i)];
        const double d = m_dry[(size_t)(m_pad + i)];
        outPush(wet * y + dry * d);
    }

    // Slide along by one block, keeping the repairs that reached past its end.
    const size_t keep = (size_t)(m_fill - blk);
    memmove(&m_win[0], &m_win[(size_t)blk], sizeof(double) * keep);
    memmove(&m_dry[0], &m_dry[(size_t)blk], sizeof(double) * keep);
    memmove(&m_flag[0], &m_flag[(size_t)blk], sizeof(uint8_t) * keep);
    m_fill -= blk;
}

template<typename Sample>
void Channel::push(const Sample * in, size_t frames, size_t stride) {
    for (size_t i = 0; i < frames; ++i) pushOne((double)in[i * stride]);
}

void Channel::outPush(double v) {
    if (m_outCount >= m_out.size()) growOut();
    size_t w = m_outHead + m_outCount;
    if (w >= m_out.size()) w -= m_out.size();
    m_out[w] = v;
    ++m_outCount;
}

//! Only reachable by pushing more than Config::maxBlock between pulls. Doubling
//! rather than growing by a block keeps it from happening repeatedly: the ring
//! ends up big enough for whatever the caller is actually doing and then stays
//! that way for the life of the Channel.
void Channel::growOut() {
    const size_t was = m_out.size();
    std::vector<double> bigger(was ? was * 2 : (size_t)kBlock * 4, 0.0);
    for (size_t i = 0; i < m_outCount; ++i) {
        size_t r = m_outHead + i;
        if (r >= was) r -= was;
        bigger[i] = m_out[r];
    }
    m_out.swap(bigger);
    m_outHead = 0;
}

template<typename Sample>
void Channel::pull(Sample * out, size_t frames, size_t stride) {
    const size_t cap = m_out.size();
    const size_t give = (frames < m_outCount) ? frames : m_outCount;
    for (size_t i = 0; i < give; ++i) {
        out[i * stride] = (Sample)m_out[m_outHead];
        if (++m_outHead >= cap) m_outHead = 0;
    }
    m_outCount -= give;
    for (size_t i = give; i < frames; ++i) out[i * stride] = (Sample)0;
}

void Channel::drain() {
    for (int i = 0; i < m_cfg.latency; ++i) pushOne(0.0);
}

template void Channel::push<float>(const float *, size_t, size_t);
template void Channel::push<double>(const double *, size_t, size_t);
template void Channel::pull<float>(float *, size_t, size_t);
template void Channel::pull<double>(double *, size_t, size_t);

} // namespace declick
