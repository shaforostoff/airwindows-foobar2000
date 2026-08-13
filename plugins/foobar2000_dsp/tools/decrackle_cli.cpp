/* ========================================
 *  decrackle_cli - run the DeCrackle core over a WAV file offline.
 *
 *  A development utility: it exists so parameter sweeps and A/B comparisons
 *  can be driven from a script without going through foobar2000.
 *
 *    decrackle_cli in.wav out.wav [-A f] [-B f] [-C f] [-D f] [-E f]
 *                                 [--no-align] [--delta] [--quiet]
 *
 *  By default the output is latency compensated: the DSP's group delay is
 *  trimmed off the front and the file is padded at the end, so out.wav lines
 *  up sample for sample with in.wav.
 * ======================================== */

#include "../foo_dsp_decrackle/decrackle_core.h"
#include "wav_io.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <memory>
#include <string>
#include <vector>

using airwindows::DeCrackleCoeffs;
using airwindows::DeCracklePair;
using airwindows::DeCrackleParams;

namespace {

void usage() {
    printf(
        "decrackle_cli - offline Airwindows DeCrackle\n"
        "\n"
        "  decrackle_cli <in.wav> <out.wav> [options]\n"
        "\n"
        "  -A <0..1>    Filter    (default 0.5)\n"
        "  -B <0..1>    Window    (default 0.5)\n"
        "  -C <0..1>    Thresld   (default 0.5)\n"
        "  -D <0..1>    Surface   (default 0.5)\n"
        "  -E <0..1>    Dry/Wet   (default 1.0)\n"
        "  --no-align   Do not compensate the DSP's latency\n"
        "  --delta      Write what was removed (input minus output) instead\n"
        "  --quiet      No progress output\n"
        "\n"
        "Output is always 32 bit float.\n");
}

bool parseFloat(const char * s, float & out) {
    char * end = NULL;
    const double v = strtod(s, &end);
    if (end == s || *end != '\0') return false;
    out = (float)v;
    return true;
}

} // anonymous namespace

int main(int argc, char ** argv) {
    if (argc < 3) { usage(); return 2; }

    const std::string inPath = argv[1];
    const std::string outPath = argv[2];
    DeCrackleParams params = DeCrackleParams::defaults();
    bool align = true, delta = false, quiet = false;

    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        const bool hasValue = (i + 1 < argc);
        if (a == "--no-align") { align = false; continue; }
        if (a == "--delta")    { delta = true;  continue; }
        if (a == "--quiet")    { quiet = true;  continue; }
        if (a == "-h" || a == "--help") { usage(); return 0; }
        if (!hasValue) { fprintf(stderr, "missing value for %s\n", a.c_str()); return 2; }
        float v = 0.0f;
        if (!parseFloat(argv[i + 1], v)) {
            fprintf(stderr, "not a number: %s\n", argv[i + 1]);
            return 2;
        }
        ++i;
        if      (a == "-A") params.filter = v;
        else if (a == "-B") params.window = v;
        else if (a == "-C") params.threshold = v;
        else if (a == "-D") params.surface = v;
        else if (a == "-E") params.dryWet = v;
        else { fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }
    params.sanitize();

    wavio::Audio audio;
    std::string err;
    if (!wavio::read(inPath, audio, err)) {
        fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }

    const unsigned channels = audio.channels;
    const size_t frames = audio.frames();

    DeCrackleCoeffs k;
    k.compute(params, (double)audio.sampleRate);

    // Run the delay line past the end so the tail is not lost, then trim the
    // group delay off the front. The result lines up with the input.
    const size_t pad = align ? (size_t)k.latencySamples : 0;
    std::vector<double> work = audio.samples;
    work.resize((frames + pad) * channels, 0.0);

    const size_t pairCount = (channels + 1) / 2;
    std::vector<std::unique_ptr<DeCracklePair> > state;
    for (size_t i = 0; i < pairCount; ++i) {
        state.push_back(std::unique_ptr<DeCracklePair>(new DeCracklePair()));
    }

    const size_t total = frames + pad;
    size_t slot = 0;
    unsigned c = 0;
    for (; c + 1 < channels; c += 2, ++slot) {
        state[slot]->processStereo(k, &work[c], &work[c + 1], channels, total);
    }
    if (c < channels) {
        state[slot]->processMono(k, &work[c], channels, total);
    }

    wavio::Audio out;
    out.channels = channels;
    out.sampleRate = audio.sampleRate;
    out.samples.assign(work.begin() + (ptrdiff_t)(pad * channels), work.end());

    if (delta) {
        for (size_t i = 0; i < out.samples.size() && i < audio.samples.size(); ++i) {
            out.samples[i] = audio.samples[i] - out.samples[i];
        }
    }

    if (!wavio::writeFloat32(outPath, out, err)) {
        fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }

    if (!quiet) {
        printf("%s -> %s\n", inPath.c_str(), outPath.c_str());
        printf("  %u Hz, %u ch, %zu frames, %u-bit%s source\n",
               audio.sampleRate, channels, frames, audio.sourceBits,
               audio.sourceFloat ? " float" : "");
        printf("  A=%.4f B=%.4f C=%.4f D=%.4f E=%.4f\n",
               params.filter, params.window, params.threshold,
               params.surface, params.dryWet);
        printf("  window %d samples, latency %d samples (%.2f ms)%s\n",
               k.adjDelay, k.latencySamples,
               1000.0 * k.latencySamples / (double)audio.sampleRate,
               align ? ", compensated" : "");
    }
    return 0;
}
