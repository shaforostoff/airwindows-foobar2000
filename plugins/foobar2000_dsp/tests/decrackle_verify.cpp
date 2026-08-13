/* ========================================
 *  decrackle_verify
 *
 *  1. Checks the optimised core against a literal transcription of the
 *     Airwindows VST across a spread of parameter settings, sample rates and
 *     signal types.
 *  2. Feeds it hostile input (NaN, infinity, denormals, huge values) and
 *     confirms the output stays finite.
 *  3. Reports throughput so the cost on a slow machine is a known quantity.
 *
 *  Exit code 0 means everything passed.
 * ======================================== */

#include "decrackle_reference.h"
#include "../foo_dsp_decrackle/decrackle_core.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

using airwindows::DeCrackleCoeffs;
using airwindows::DeCracklePair;
using airwindows::DeCrackleParams;

namespace {

// xorshift, so the test vectors are identical on every machine and run.
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 1u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uniform() { return (float)((double)next() / 4294967295.0 * 2.0 - 1.0); }
};

//! Interleaved stereo test material with the shape DeCrackle is aimed at:
//! quiet music plus sharp isolated clicks.
std::vector<float> makeSignal(size_t frames, int kind, uint32_t seed) {
    std::vector<float> buf(frames * 2, 0.0f);
    Rng rng(seed);
    switch (kind) {
    case 0: // vinyl-ish: low level tone bed, surface noise, periodic clicks
        for (size_t n = 0; n < frames; ++n) {
            const double t = (double)n / 44100.0;
            double v = 0.25 * std::sin(6.2831853 * 220.0 * t)
                     + 0.02 * rng.uniform();
            double w = 0.25 * std::sin(6.2831853 * 277.0 * t)
                     + 0.02 * rng.uniform();
            if ((n % 3313) == 0) { v += 0.8; w -= 0.7; }
            if ((n % 7919) == 0) { v -= 0.9; w += 0.85; }
            buf[n * 2]     = (float)v;
            buf[n * 2 + 1] = (float)w;
        }
        break;
    case 1: // full scale noise
        for (size_t i = 0; i < buf.size(); ++i) buf[i] = rng.uniform();
        break;
    case 2: // digital silence, to exercise the denormal guard
        break;
    case 3: // very quiet noise, right down at the guard threshold
        for (size_t i = 0; i < buf.size(); ++i) buf[i] = rng.uniform() * 1e-20f;
        break;
    default: // impulse train on silence
        for (size_t n = 0; n < frames; ++n) {
            if ((n % 512) == 0) { buf[n * 2] = 1.0f; buf[n * 2 + 1] = -1.0f; }
        }
        break;
    }
    return buf;
}

struct Case {
    const char *    name;
    DeCrackleParams params;
    double          rate;
};

DeCrackleParams P(float a, float b, float c, float d, float e) {
    DeCrackleParams p; p.filter = a; p.window = b; p.threshold = c;
    p.surface = d; p.dryWet = e; return p;
}

int g_failures = 0;

void check(bool ok, const char * what) {
    if (!ok) { std::printf("  FAIL: %s\n", what); ++g_failures; }
}

// -- 1. agreement with the VST transcription --------------------------------

void compareAgainstReference() {
    const Case cases[] = {
        { "defaults @44.1k",        P(0.5f, 0.5f, 0.5f, 0.5f, 1.0f),  44100.0 },
        { "all zero @44.1k",        P(0.0f, 0.0f, 0.0f, 0.0f, 0.0f),  44100.0 },
        { "all one @44.1k",         P(1.0f, 1.0f, 1.0f, 1.0f, 1.0f),  44100.0 },
        { "wide window @44.1k",     P(0.2f, 1.0f, 0.3f, 0.9f, 1.0f),  44100.0 },
        { "narrow window @44.1k",   P(0.9f, 0.02f, 0.1f, 0.0f, 0.75f),44100.0 },
        { "delta monitor @44.1k",   P(0.5f, 0.6f, 0.2f, 0.4f, 0.0f),  44100.0 },
        { "half wet @48k",          P(0.3f, 0.4f, 0.6f, 0.2f, 0.5f),  48000.0 },
        { "defaults @96k",          P(0.5f, 0.5f, 0.5f, 0.5f, 1.0f),  96000.0 },
        { "wide window @192k",      P(0.4f, 0.95f, 0.35f, 0.7f, 1.0f),192000.0 },
        { "tiny window @192k",      P(0.5f, 0.005f, 0.5f, 0.5f, 1.0f),192000.0 },
        { "defaults @8k",           P(0.5f, 0.5f, 0.5f, 0.5f, 1.0f),   8000.0 },
        { "defaults @384k",         P(0.6f, 0.5f, 0.4f, 0.3f, 1.0f), 384000.0 },
    };

    const size_t frames = 24000;
    double worstAbs = 0.0;
    const char * worstCase = "(none)";
    int trivialCases = 0;

    for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); ++c) {
        for (int kind = 0; kind < 5; ++kind) {
            std::vector<float> a = makeSignal(frames, kind, 0x1234u + (uint32_t)c * 7u + kind);
            const std::vector<float> original = a;
            std::vector<float> b = a;

            reference::DeCrackleReference ref;
            ref.A = cases[c].params.filter;
            ref.B = cases[c].params.window;
            ref.C = cases[c].params.threshold;
            ref.D = cases[c].params.surface;
            ref.E = cases[c].params.dryWet;
            // Chunked the way a host would call it, to prove the state carries
            // across buffer boundaries.
            size_t pos = 0;
            while (pos < frames) {
                const size_t n = (frames - pos > 1024) ? 1024 : frames - pos;
                ref.processReplacing(&a[pos * 2], (int)n, cases[c].rate);
                pos += n;
            }

            DeCrackleCoeffs k;
            k.compute(cases[c].params, cases[c].rate);
            std::unique_ptr<DeCracklePair> dut(new DeCracklePair());
            pos = 0;
            while (pos < frames) {
                const size_t n = (frames - pos > 1024) ? 1024 : frames - pos;
                dut->processStereo(k, &b[pos * 2], &b[pos * 2 + 1], 2, n);
                pos += n;
            }

            double maxAbs = 0.0;
            for (size_t i = 0; i < a.size(); ++i) {
                const double d = std::fabs((double)a[i] - (double)b[i]);
                if (d > maxAbs) maxAbs = d;
            }
            if (maxAbs > worstAbs) { worstAbs = maxAbs; worstCase = cases[c].name; }

            // Guard against a vacuous pass: if neither side altered the signal
            // the comparison proves nothing. (Digital silence legitimately
            // stays near silence, so it is excluded.)
            if (kind != 2) {
                double changed = 0.0;
                for (size_t i = 0; i < a.size(); ++i) {
                    const double d = std::fabs((double)a[i] - (double)original[i]);
                    if (d > changed) changed = d;
                }
                if (changed < 1e-12) ++trivialCases;
            }

            char label[160];
            std::snprintf(label, sizeof(label), "%s / signal %d: max |diff| = %.3e",
                          cases[c].name, kind, maxAbs);
            // A 24 bit LSB at full scale is 6e-8. Anything under 1e-9 is a
            // rounding artefact of the algebraic rewrites, not a change in
            // behaviour.
            check(maxAbs < 1e-9, label);
        }
    }
    std::printf("  worst deviation overall: %.3e (%s)\n", worstAbs, worstCase);
    check(trivialCases == 0, "every comparison ran on audio the DSP actually changed");
}

// -- 2. hostile input --------------------------------------------------------

void hostileInput() {
    const float poison[] = {
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
        1e30f, -1e30f, 3.4e38f,
        1.4e-45f, -1.4e-45f, 0.0f, -0.0f,
    };
    const size_t poisonCount = sizeof(poison) / sizeof(poison[0]);

    DeCrackleParams p = DeCrackleParams::defaults();
    DeCrackleCoeffs k;
    k.compute(p, 44100.0);

    std::unique_ptr<DeCracklePair> dut(new DeCracklePair());
    std::vector<float> buf(8192 * 2);
    Rng rng(99);
    for (size_t i = 0; i < buf.size(); ++i) {
        buf[i] = (i % 5 == 0) ? poison[rng.next() % poisonCount] : rng.uniform();
    }
    dut->processStereo(k, &buf[0], &buf[1], 2, 8192);

    bool allFinite = true;
    for (size_t i = 0; i < buf.size(); ++i) {
        if (!std::isfinite(buf[i])) { allFinite = false; break; }
    }
    check(allFinite, "output stays finite when fed NaN / infinity / extremes");
    check(dut->stateIsFinite(), "internal state stays finite after hostile input");

    // Clean audio afterwards must come back clean.
    std::vector<float> clean = makeSignal(8192, 0, 5);
    dut->processStereo(k, &clean[0], &clean[1], 2, 8192);
    allFinite = true;
    for (size_t i = 0; i < clean.size(); ++i) {
        if (!std::isfinite(clean[i])) { allFinite = false; break; }
    }
    check(allFinite, "recovers and passes clean audio after hostile input");
}

// -- 3. parameter and rate sweep, bounds only --------------------------------

void exhaustiveSweep() {
    std::unique_ptr<DeCracklePair> dut(new DeCracklePair());
    const double rates[] = { 1000.0, 8000.0, 22050.0, 44100.0, 48000.0, 88200.0,
                             96000.0, 176400.0, 192000.0, 352800.0, 384000.0,
                             768000.0, 2822400.0, 20000000.0 };
    std::vector<float> buf = makeSignal(2048, 0, 7);
    bool ok = true;

    for (size_t r = 0; r < sizeof(rates) / sizeof(rates[0]); ++r) {
        for (int i = 0; i <= 20; ++i) {
            const float v = (float)i / 20.0f;
            const DeCrackleParams sweeps[] = {
                P(v, 0.5f, 0.5f, 0.5f, 1.0f),
                P(0.5f, v, 0.5f, 0.5f, 1.0f),
                P(0.5f, 0.5f, v, 0.5f, 1.0f),
                P(0.5f, 0.5f, 0.5f, v, 1.0f),
                P(0.5f, 0.5f, 0.5f, 0.5f, v),
                P(v, v, v, v, v),
            };
            for (size_t s = 0; s < sizeof(sweeps) / sizeof(sweeps[0]); ++s) {
                DeCrackleCoeffs k;
                k.compute(sweeps[s], rates[r]);
                if (k.adjDelay > (int)airwindows::kDeCrackleShort - 1) ok = false;
                if (k.latencySamples < 0) ok = false;
                std::vector<float> work = buf;
                dut->reset();
                dut->processStereo(k, &work[0], &work[1], 2, 2048);
                for (size_t n = 0; n < work.size(); ++n) {
                    if (!std::isfinite(work[n])) { ok = false; break; }
                }
                if (!ok) {
                    std::printf("  broke at rate %.0f, sweep %d, value %.2f\n",
                                rates[r], (int)s, (double)v);
                    break;
                }
            }
            if (!ok) break;
        }
        if (!ok) break;
    }
    check(ok, "full parameter x sample rate sweep stays in bounds and finite");
}

// -- 4. mono path -------------------------------------------------------------

void monoPath() {
    DeCrackleCoeffs k;
    k.compute(DeCrackleParams::defaults(), 44100.0);
    std::unique_ptr<DeCracklePair> dut(new DeCracklePair());
    std::vector<float> buf(4096);
    Rng rng(31337);
    for (size_t i = 0; i < buf.size(); ++i) buf[i] = rng.uniform() * 0.5f;
    dut->processMono(k, &buf[0], 1, buf.size());
    bool ok = true;
    for (size_t i = 0; i < buf.size(); ++i) if (!std::isfinite(buf[i])) ok = false;
    check(ok, "single channel path produces finite output");

    // A mono signal duplicated into both channels must give the same result
    // through the stereo path (bar the independent dither generators).
    std::vector<float> mono(4096), stereo(4096 * 2);
    for (size_t i = 0; i < mono.size(); ++i) {
        const float v = rng.uniform() * 0.5f;
        mono[i] = v; stereo[i * 2] = v; stereo[i * 2 + 1] = v;
    }
    std::unique_ptr<DeCracklePair> m(new DeCracklePair());
    std::unique_ptr<DeCracklePair> s(new DeCracklePair());
    m->processMono(k, &mono[0], 1, mono.size());
    s->processStereo(k, &stereo[0], &stereo[1], 2, mono.size());
    double maxAbs = 0.0;
    for (size_t i = 0; i < mono.size(); ++i) {
        const double d = std::fabs((double)mono[i] - (double)stereo[i * 2]);
        if (d > maxAbs) maxAbs = d;
    }
    char label[128];
    std::snprintf(label, sizeof(label),
                  "mono path matches duplicated stereo: max |diff| = %.3e", maxAbs);
    check(maxAbs < 1e-6, label);
}

// -- 5. throughput -----------------------------------------------------------

void benchmark() {
    const size_t frames = 44100 * 30;   // half a minute of 44.1k stereo
    std::vector<float> buf = makeSignal(frames, 0, 11);

    struct Setting { const char * name; DeCrackleParams p; };
    const Setting settings[] = {
        { "surface off (D=0)", P(0.5f, 0.5f, 0.5f, 0.0f, 1.0f) },
        { "defaults",          P(0.5f, 0.5f, 0.5f, 0.5f, 1.0f) },
        { "worst case",        P(1.0f, 1.0f, 0.0f, 1.0f, 1.0f) },
    };

    for (size_t i = 0; i < sizeof(settings) / sizeof(settings[0]); ++i) {
        DeCrackleCoeffs k;
        k.compute(settings[i].p, 44100.0);
        std::unique_ptr<DeCracklePair> dut(new DeCracklePair());
        std::vector<float> work = buf;

        const auto t0 = std::chrono::steady_clock::now();
        size_t pos = 0;
        while (pos < frames) {
            const size_t n = (frames - pos > 1024) ? 1024 : frames - pos;
            dut->processStereo(k, &work[pos * 2], &work[pos * 2 + 1], 2, n);
            pos += n;
        }
        const auto t1 = std::chrono::steady_clock::now();
        const double secs = std::chrono::duration<double>(t1 - t0).count();
        const double audioSecs = (double)frames / 44100.0;
        std::printf("  %-18s %6.1f ms for %.0f s of audio  (%.0fx realtime, %.1f%% of one core)\n",
                    settings[i].name, secs * 1000.0, audioSecs,
                    audioSecs / secs, 100.0 * secs / audioSecs);
    }
}

} // anonymous namespace

int main() {
    std::printf("== agreement with the Airwindows VST source ==\n");
    compareAgainstReference();
    std::printf("== hostile input ==\n");
    hostileInput();
    std::printf("== parameter / sample rate sweep ==\n");
    exhaustiveSweep();
    std::printf("== single channel path ==\n");
    monoPath();
    std::printf("== throughput ==\n");
    benchmark();

    if (g_failures == 0) {
        std::printf("\nAll checks passed.\n");
        return 0;
    }
    std::printf("\n%d check(s) FAILED.\n", g_failures);
    return 1;
}
