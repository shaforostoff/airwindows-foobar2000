/* ========================================
 *  vdjplugin - test support
 *
 *  Enough of VirtualDJ to drive the wrappers without it. What these harnesses
 *  are for is the part of this project that is not shared with any other port:
 *  the cores themselves are already pinned by declick_verify, dehum_verify and
 *  friends in ../../foobar2000_dsp/tests, and nothing here re-tests the maths.
 *  What is new, and therefore what is checked, is
 *
 *    - the slider mappings, which have to agree with Params::defaults() and
 *      with the VST2 port or the tuning figures in the foobar2000 README stop
 *      describing this build,
 *
 *    - BufferPipeline, which is the only genuinely new machinery: a cache
 *      addressed by song position, a sequential engine behind it, and a caller
 *      that jumps about. Its central claim is that the audio it hands back is
 *      exactly what the core would have produced running straight through the
 *      song, whatever order it was asked for it in, and that is what
 *      referenceRun below exists to compare against - written directly against
 *      the core so that it shares no indexing arithmetic with the thing it is
 *      checking.
 * ======================================== */

#ifndef VDJ_TEST_SUPPORT_H
#define VDJ_TEST_SUPPORT_H

// vdjPlugin8.h declares DllGetClassObject __declspec(dllexport); an executable
// that never defines it should not be exporting it.
#define NODLLEXPORT 1

#include "vdjPlugin8.h"

#include "vdj_buffer_dsp.h"
#include "vdj_engine.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

namespace vdjtest {

// ---------------------------------------------------------------------------
// Reporting

extern int g_failures;

inline void check(bool ok, const char * what) {
    if (ok) return;
    printf("  FAIL  %s\n", what);
    ++g_failures;
}

inline void checkf(bool ok, const char * fmt, double a, double b) {
    if (ok) return;
    printf("  FAIL  ");
    printf(fmt, a, b);
    printf("\n");
    ++g_failures;
}

inline void note(const char * what) { printf("  ok    %s\n", what); }

inline int finish(const char * name) {
    if (g_failures == 0) {
        printf("%s: all checks passed\n", name);
        return 0;
    }
    printf("%s: %d check(s) FAILED\n", name, g_failures);
    return 1;
}

// ---------------------------------------------------------------------------
// A host, for the parameter declarations

//! Records what a plug-in declares, so the harness can check that the sliders
//! exist, are the right kind, and open where they are meant to.
struct StubCallbacks : IVdjCallbacks8 {
    struct Declared {
        void * ptr;
        int    type;
        int    id;
        std::string name;
        std::string shortName;
        float  def;
    };
    std::vector<Declared> declared;
    std::vector<short> * song = NULL;
    //! How many times the plug-in read the song back through us. Zero from a
    //! buffer plug-in would mean it never used its readahead.
    int songBufferCalls = 0;

    HRESULT SendCommand(const char *) override { return S_OK; }
    HRESULT GetInfo(const char *, double *) override { return E_NOTIMPL; }
    HRESULT GetStringInfo(const char *, void *, int) override { return E_NOTIMPL; }

    HRESULT DeclareParameter(void * parameter, int type, int id, const char * name,
                             const char * shortName, float defaultvalue) override {
        Declared d;
        d.ptr = parameter;
        d.type = type;
        d.id = id;
        d.name = name ? name : "";
        d.shortName = shortName ? shortName : "";
        d.def = defaultvalue;
        declared.push_back(d);
        return S_OK;
    }

    HRESULT GetSongBuffer(int pos, int nb, short ** buffer) override {
        if (song == NULL || pos < 0 || nb <= 0) return E_NOTIMPL;
        const size_t frames = song->size() / vdj::kChannels;
        if ((size_t)pos + (size_t)nb > frames) return E_NOTIMPL;
        ++songBufferCalls;
        *buffer = song->data() + (size_t)pos * vdj::kChannels;
        return S_OK;
    }
};

struct StubHost : IVdjPlugin8 {
    explicit StubHost(StubCallbacks & callbacks) { cb = &callbacks; }
};

// ---------------------------------------------------------------------------
// A song, in memory

//! What VirtualDJ's decoded song cache looks like from a plug-in's side:
//! interleaved stereo shorts, random access, and a failure past the end.
class MemorySong : public vdj::SongSource {
public:
    explicit MemorySong(std::vector<short> & pcm) : m_pcm(pcm) {}

    size_t frames() const { return m_pcm.size() / vdj::kChannels; }
    int    reads() const { return m_reads; }

    bool read(int pos, int nb, const short ** out) override {
        if (pos < 0 || nb <= 0) return false;
        if ((size_t)pos + (size_t)nb > frames()) return false;
        ++m_reads;
        *out = m_pcm.data() + (size_t)pos * vdj::kChannels;
        return true;
    }

private:
    std::vector<short> & m_pcm;
    int m_reads = 0;
};

// ---------------------------------------------------------------------------
// Signals

//! Something with enough structure that the declicker's model has work to do
//! and enough level that a click stands out from it: four partials, a slow
//! amplitude envelope, and a little noise.
inline std::vector<short> music(size_t frames, double rate, uint32_t seed = 12345) {
    std::vector<short> pcm(frames * vdj::kChannels);
    uint32_t r = seed;
    for (size_t f = 0; f < frames; ++f) {
        const double t = (double)f / rate;
        const double env = 0.5 + 0.35 * sin(2.0 * 3.14159265358979 * 0.7 * t);
        double v = 0.28 * sin(2.0 * 3.14159265358979 * 220.0 * t)
                 + 0.16 * sin(2.0 * 3.14159265358979 * 441.0 * t + 0.4)
                 + 0.09 * sin(2.0 * 3.14159265358979 * 1319.0 * t + 1.1)
                 + 0.04 * sin(2.0 * 3.14159265358979 * 2637.0 * t + 2.2);
        v *= env;
        r ^= r << 13; r ^= r >> 17; r ^= r << 5;
        const double n = ((double)r / 4294967296.0 - 0.5) * 0.004;
        const double l = v + n;
        r ^= r << 13; r ^= r >> 17; r ^= r << 5;
        const double rr = v * 0.9 + ((double)r / 4294967296.0 - 0.5) * 0.004;
        pcm[f * 2 + 0] = (short)(l * 20000.0);
        pcm[f * 2 + 1] = (short)(rr * 20000.0);
    }
    return pcm;
}

//! Impulsive damage, at a fixed spacing so the count is known.
inline void injectClicks(std::vector<short> & pcm, double rate, double perSecond,
                         double amp = 0.55) {
    const size_t frames = pcm.size() / vdj::kChannels;
    const size_t step = (size_t)(rate / perSecond);
    if (step == 0) return;
    uint32_t r = 777;
    for (size_t f = step; f + 4 < frames; f += step) {
        r ^= r << 13; r ^= r >> 17; r ^= r << 5;
        const double sign = (r & 1) ? 1.0 : -1.0;
        for (int k = 0; k < 3; ++k) {
            const double a = amp * sign * (1.0 - 0.3 * k) * 32767.0;
            for (int c = 0; c < 2; ++c) {
                double v = (double)pcm[(f + k) * 2 + c] + a;
                if (v > 32767.0) v = 32767.0;
                if (v < -32768.0) v = -32768.0;
                pcm[(f + k) * 2 + c] = (short)v;
            }
        }
    }
}

//! A continuous line, which is what Dehum is looking for.
inline void addTone(std::vector<short> & pcm, double rate, double hz, double amp) {
    const size_t frames = pcm.size() / vdj::kChannels;
    for (size_t f = 0; f < frames; ++f) {
        const double s = amp * 32767.0 * sin(2.0 * 3.14159265358979 * hz * (double)f / rate);
        for (int c = 0; c < 2; ++c) {
            double v = (double)pcm[f * 2 + c] + s;
            if (v > 32767.0) v = 32767.0;
            if (v < -32768.0) v = -32768.0;
            pcm[f * 2 + c] = (short)v;
        }
    }
}

// ---------------------------------------------------------------------------

//! First difference of one channel where two buffers disagree, or -1.
inline int64_t firstDifference(const std::vector<short> & a, const std::vector<short> & b) {
    const size_t n = (a.size() < b.size()) ? a.size() : b.size();
    for (size_t i = 0; i < n; ++i) {
        if (a[i] != b[i]) return (int64_t)(i / vdj::kChannels);
    }
    return (a.size() == b.size()) ? -1 : (int64_t)(n / vdj::kChannels);
}

inline bool allZero(const short * p, size_t frames) {
    for (size_t i = 0; i < frames * vdj::kChannels; ++i) {
        if (p[i] != 0) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------

//! Drives BufferPipeline the way a well-behaved deck would: forward, in equal
//! blocks, from the start. `nb` is the block size, and varying it is the point -
//! nothing about the result is allowed to depend on it.
template<class Engine>
std::vector<short> driveSequential(Engine & engine, vdj::SongSource & src,
                                  size_t frames, int nb, double rate) {
    vdj::BufferPipeline<Engine> pipe;
    engine.setRate(rate);
    pipe.configure(engine, rate);

    std::vector<short> out(frames * vdj::kChannels, 0);
    size_t pos = 0;
    while (pos < frames) {
        size_t n = (size_t)nb;
        if (n > frames - pos) n = frames - pos;
        const short * p = pipe.serve(engine, src, (int)pos, (int)n);
        if (p == NULL) { check(false, "serve returned NULL"); break; }
        memcpy(&out[pos * vdj::kChannels], p, n * vdj::kChannels * sizeof(short));
        pos += n;
    }
    return out;
}

} // namespace vdjtest

#endif // VDJ_TEST_SUPPORT_H
