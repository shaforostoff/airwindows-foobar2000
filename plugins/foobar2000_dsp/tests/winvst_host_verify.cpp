/* ========================================
 *  winvst_host_verify - a host, loading the finished DLL.
 *
 *  Every other test in this directory links the plug-in's source straight into
 *  the test binary. That checks the DSP and the parameter mapping, and it
 *  cannot check the thing a VST actually has to get right: the ABI. A wrong
 *  offset in AEffect or a wrong opcode number compiles perfectly and links
 *  perfectly and then hands a real host a function pointer read out of the
 *  middle of an integer field.
 *
 *  So this test is the other side. It does what a host does and nothing else:
 *
 *      LoadLibrary -> GetProcAddress("VSTPluginMain") -> call it
 *      -> read the AEffect -> dispatcher(opcodes) -> processDoubleReplacing
 *      -> effClose
 *
 *  and it never touches the plug-in's C++ classes through that path. It also
 *  links the same plug-in statically, drives that copy through the identical
 *  call sequence, and requires the two output streams to be equal to the bit.
 *  That comparison is the point of the file: the static copy and the DLL copy
 *  are the same source compiled the same way, so any difference between them is
 *  the ABI, the dispatcher, the thunks or the calling convention - the four
 *  things nothing else here can see.
 *
 *  What it still cannot establish: that plugins/WinVST/vst2_shim agrees with
 *  Steinberg's headers, because the shim and this test read the same
 *  vst2_abi.h and so agree with each other by construction. sizeof(AEffect) is
 *  asserted against the two documented values at compile time and the opcode
 *  numbers are asserted as literals, which is as close as this repository can
 *  get on its own. A real host is the last word.
 *
 *  Built once per plug-in, since two plug-ins in one binary would collide on
 *  createEffectInstance and on kNumParameters:
 *
 *      cl /D WINVST_DEHUM   ... winvst_host_verify.cpp Dehum.cpp   ...
 *      cl /D WINVST_DECLICK ... winvst_host_verify.cpp Declick.cpp ...
 *
 *  Usage: winvst_host_verify <path-to-dll>
 * ======================================== */

#include <windows.h>
#include <psapi.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#if defined(WINVST_DEHUM)
  #include "Dehum.h"
  typedef Dehum Plugin;
  static const char * kPluginName    = "Dehum";
  static const char * kProductString = "Dehum (line detection)";
  static const bool   kZeroLatency   = true;
  static const char * kParamNames[7] =
      { "Sensitv", "Bandwid", "SrchTo", "Harmncs", "Freq", "Rumble", "Dry/Wet" };
#elif defined(WINVST_DECLICK)
  #include "Declick.h"
  typedef Declick Plugin;
  static const char * kPluginName    = "Declick";
  static const char * kProductString = "Declick (AR interpolation)";
  static const bool   kZeroLatency   = false;
  static const char * kParamNames[7] =
      { "Sensitv", "Extent", "MaxLen", "Depth", "Passes", "Order", "Dry/Wet" };
#else
  #error define WINVST_DEHUM or WINVST_DECLICK
#endif

namespace {

int g_failures = 0;
int g_ioChanged = 0;

void check(bool ok, const char * what, const char * detail = "") {
    printf("  %-58s %-4s %s\n", what, ok ? "ok" : "FAIL", detail);
    if (!ok) ++g_failures;
}

const int    kRate   = 44100;
const int    kFrames = 44100 * 20;    //long enough for the detectors to engage
const int    kBlock  = 512;
const double kPi     = 3.14159265358979323846;

/* ---------------------------------------------------------------------------
 *  The host side of the callback. Only audioMasterIOChanged is ever reached
 *  from this tree, and counting it is how the latency contract gets checked.
 * ------------------------------------------------------------------------ */
VstIntPtr VSTCALLBACK hostCallback(AEffect * effect, VstInt32 opcode, VstInt32 index,
                                   VstIntPtr value, void * ptr, float opt) {
    (void)effect; (void)index; (void)value; (void)ptr; (void)opt;
    switch (opcode) {
        case audioMasterVersion:       return 2400;
        case audioMasterIOChanged:     ++g_ioChanged; return 1;
        case audioMasterGetSampleRate: return (VstIntPtr)kRate;
        case audioMasterGetBlockSize:  return (VstIntPtr)kBlock;
        default: break;
    }
    return 0;
}

VstIntPtr send(AEffect * e, VstInt32 opcode, VstInt32 index = 0, VstIntPtr value = 0,
               void * ptr = 0, float opt = 0.0f) {
    return e->dispatcher(e, opcode, index, value, ptr, opt);
}

/*  Hosts hand over a generous buffer and expect the plug-in to respect
 *  kVstMaxParamStrLen inside it. Filling it with a sentinel first means an
 *  unwritten string shows up as garbage rather than as a stale empty one. */
struct TextBuf {
    char s[128];
    TextBuf() { memset(s, '?', sizeof s); s[sizeof s - 1] = 0; }
};

// ---------------------------------------------------------------------------

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 1u) {}
    double centred() { s ^= s << 13; s ^= s >> 17; s ^= s << 5;
                       return (double)s / 4294967296.0 - 0.5; }
};

//! Hum, a note, surface noise and the odd click - enough for either plug-in to
//! have real work to do, which is what stops the bit-comparison below from
//! being satisfied by two copies that both do nothing.
void makeSignal(std::vector<double> & l, std::vector<double> & r) {
    Rng rng(20240813u);
    l.assign((size_t)kFrames, 0.0);
    r.assign((size_t)kFrames, 0.0);
    for (int i = 0; i < kFrames; ++i) {
        const double t = (double)i / (double)kRate;
        const double hum  = 0.02 * sin(2.0 * kPi * 49.58 * t);
        const double note = 0.10 * sin(2.0 * kPi * 220.0 * t)
                          * ((fmod(t, 0.5) < 0.35) ? 1.0 : 0.0);
        l[(size_t)i] = hum + note + 0.004 * rng.centred();
        r[(size_t)i] = hum * 0.9 + note * 1.1 + 0.004 * rng.centred();
    }
    for (int c = 0; c < 120; ++c) {
        const size_t at = (size_t)(kRate / 2 + c * (kRate / 4));
        if (at + 8 >= (size_t)kFrames) break;
        for (int k = 0; k < 5; ++k) {
            const double s = (k & 1) ? -0.55 : 0.55;
            l[at + (size_t)k] += s;
            r[at + (size_t)k] += s * 0.8;
        }
    }
}

double worstDiff(const std::vector<double> & a, const std::vector<double> & b) {
    double w = 0.0;
    const size_t n = a.size() < b.size() ? a.size() : b.size();
    for (size_t i = 0; i < n; ++i) {
        const double d = fabs(a[i] - b[i]);
        if (d > w) w = d;
    }
    return w;
}

double rmsDiff(const std::vector<double> & a, const std::vector<double> & b) {
    double s = 0.0;
    const size_t n = a.size() < b.size() ? a.size() : b.size();
    for (size_t i = 0; i < n; ++i) { const double d = a[i] - b[i]; s += d * d; }
    return n ? sqrt(s / (double)n) : 0.0;
}

/*  The same opening sequence a host performs, so the DLL copy and the static
 *  copy start from identical state. Order matters: rate before block size
 *  before resume, because resume() is where each plug-in decides what to keep. */
void openThroughAbi(AEffect * e) {
    send(e, effOpen);
    send(e, effSetSampleRate, 0, 0, 0, (float)kRate);
    send(e, effSetBlockSize, 0, (VstIntPtr)kBlock);
    send(e, effSetProcessPrecision, 0, (VstIntPtr)kVstProcessPrecision64);
    send(e, effMainsChanged, 0, 1);
    send(e, effStartProcess);
}

void openStatically(Plugin & fx) {
    fx.open();
    fx.setSampleRate((float)kRate);
    fx.setBlockSize(kBlock);
    fx.setProcessPrecision(kVstProcessPrecision64);
    fx.resume();
    fx.startProcess();
}

void runThroughAbi(AEffect * e, const std::vector<double> & inL,
                   const std::vector<double> & inR,
                   std::vector<double> & outL, std::vector<double> & outR, int block) {
    outL.assign(inL.size(), 0.0);
    outR.assign(inR.size(), 0.0);
    std::vector<double> bl((size_t)block), br((size_t)block);
    size_t pos = 0;
    while (pos < inL.size()) {
        const size_t n = (inL.size() - pos < (size_t)block) ? inL.size() - pos : (size_t)block;
        for (size_t i = 0; i < n; ++i) { bl[i] = inL[pos + i]; br[i] = inR[pos + i]; }
        double * bufs[2] = { &bl[0], &br[0] };
        e->processDoubleReplacing(e, bufs, bufs, (VstInt32)n);   //in place, as hosts do
        for (size_t i = 0; i < n; ++i) { outL[pos + i] = bl[i]; outR[pos + i] = br[i]; }
        pos += n;
    }
}

void runStatically(Plugin & fx, const std::vector<double> & inL,
                   const std::vector<double> & inR,
                   std::vector<double> & outL, std::vector<double> & outR, int block) {
    outL.assign(inL.size(), 0.0);
    outR.assign(inR.size(), 0.0);
    std::vector<double> bl((size_t)block), br((size_t)block);
    size_t pos = 0;
    while (pos < inL.size()) {
        const size_t n = (inL.size() - pos < (size_t)block) ? inL.size() - pos : (size_t)block;
        for (size_t i = 0; i < n; ++i) { bl[i] = inL[pos + i]; br[i] = inR[pos + i]; }
        double * bufs[2] = { &bl[0], &br[0] };
        fx.processDoubleReplacing(bufs, bufs, (VstInt32)n);
        for (size_t i = 0; i < n; ++i) { outL[pos + i] = bl[i]; outR[pos + i] = br[i]; }
        pos += n;
    }
}

size_t privateBytes() {
    PROCESS_MEMORY_COUNTERS_EX pmc;
    memset(&pmc, 0, sizeof pmc);
    pmc.cb = sizeof pmc;
    if (!GetProcessMemoryInfo(GetCurrentProcess(),
                              (PROCESS_MEMORY_COUNTERS *)&pmc, sizeof pmc)) return 0;
    return (size_t)pmc.PrivateUsage;
}

// ---------------------------------------------------------------------------

typedef AEffect * (*VSTPluginMainProc)(audioMasterCallback);

HMODULE       g_dll  = 0;
VSTPluginMainProc g_entry = 0;

void testItLoads(const char * path) {
    printf("\nthe DLL, as a host sees it\n");

    g_dll = LoadLibraryA(path);
    char d[512];
    if (!g_dll) {
        snprintf(d, sizeof d, "LoadLibrary failed, error %lu", (unsigned long)GetLastError());
        check(false, "the DLL loads", d);
        return;
    }
    snprintf(d, sizeof d, "%s", path);
    check(true, "the DLL loads", d);

    g_entry = (VSTPluginMainProc)GetProcAddress(g_dll, "VSTPluginMain");
    check(g_entry != 0, "it exports VSTPluginMain");

    /*  The .def file aliases "main" to the same function for hosts that predate
     *  the rename. Same address, or the alias is not doing its job. */
    FARPROC legacy = GetProcAddress(g_dll, "main");
    check(legacy != 0 && (void *)legacy == (void *)g_entry,
          "and aliases main to the same address");
}

void testTheAEffect(AEffect * e) {
    printf("\nthe AEffect it hands back\n");
    char d[128];

    snprintf(d, sizeof d, "0x%08lX", (unsigned long)(uint32_t)e->magic);
    check(e->magic == kEffectMagic, "magic is 'VstP'", d);

    snprintf(d, sizeof d, "%d bytes", (int)sizeof(AEffect));
    check(sizeof(AEffect) == (sizeof(void *) == 8 ? 192u : 144u),
          "sizeof(AEffect) is what a host compiled in", d);

    check(e->object != 0, "object points at the plug-in");
    check(e->dispatcher != 0 && e->setParameter != 0 && e->getParameter != 0,
          "dispatcher, setParameter and getParameter are all set");
    check(e->processReplacing != 0, "processReplacing is set");
    check(e->processDoubleReplacing != 0, "processDoubleReplacing is set");
    check(e->process != 0,
          "the deprecated process is a no-op, not a null pointer");

    snprintf(d, sizeof d, "%d in, %d out, %d params, %d programs",
             (int)e->numInputs, (int)e->numOutputs, (int)e->numParams, (int)e->numPrograms);
    check(e->numInputs == 2 && e->numOutputs == 2
          && e->numParams == kNumParameters && e->numPrograms == 0,
          "the counts are the plug-in's", d);

    const VstInt32 want = effFlagsCanReplacing | effFlagsProgramChunks
                        | effFlagsCanDoubleReplacing;
    snprintf(d, sizeof d, "0x%04lX", (unsigned long)(uint32_t)e->flags);
    check(e->flags == want, "flags are canReplacing | programChunks | canDoubleReplacing", d);
    check((e->flags & effFlagsHasEditor) == 0, "and no editor is claimed");

    snprintf(d, sizeof d, "0x%08lX", (unsigned long)(uint32_t)e->uniqueID);
    check(e->uniqueID == (VstInt32)kUniqueId, "uniqueID is the plug-in's four character code", d);

    check(e->resvd1 == 0 && e->resvd2 == 0, "the host's reserved fields are left alone");
    bool futureZero = true;
    for (size_t i = 0; i < sizeof e->future; ++i) if (e->future[i]) futureZero = false;
    check(futureZero, "future[56] is zeroed");
}

void testIdentityOpcodes(AEffect * e) {
    printf("\nwhat it answers\n");
    TextBuf t;

    check(send(e, effGetEffectName, 0, 0, t.s) == 1 && strcmp(t.s, kPluginName) == 0,
          "effGetEffectName", t.s);

    TextBuf v;
    check(send(e, effGetVendorString, 0, 0, v.s) == 1
          && strcmp(v.s, "airwindows-foobar2000") == 0, "effGetVendorString", v.s);

    TextBuf p;
    check(send(e, effGetProductString, 0, 0, p.s) == 1
          && strcmp(p.s, kProductString) == 0, "effGetProductString", p.s);

    char d[64];
    const VstIntPtr ver = send(e, effGetVendorVersion);
    snprintf(d, sizeof d, "%d", (int)ver);
    check(ver == 1000, "effGetVendorVersion", d);

    const VstIntPtr vst = send(e, effGetVstVersion);
    snprintf(d, sizeof d, "%d", (int)vst);
    check(vst == 2400, "effGetVstVersion", d);

    const VstIntPtr cat = send(e, effGetPlugCategory);
    snprintf(d, sizeof d, "%d", (int)cat);
    check(cat == (VstIntPtr)kPlugCategEffect, "effGetPlugCategory is kPlugCategEffect", d);

    /*  canDo is a three way answer and the middle value matters: a host reads 0
     *  as "the plug-in has no opinion", not as "no". */
    char yes[] = "plugAsChannelInsert";
    char no[]  = "offline";
    check(send(e, effCanDo, 0, 0, yes) == 1, "effCanDo plugAsChannelInsert -> yes");
    check(send(e, effCanDo, 0, 0, no) == -1, "effCanDo offline -> no");

    check(send(e, effCanBeAutomated, 3) == 1, "effCanBeAutomated -> yes");

    check(send(e, effGetNumMidiInputChannels) == 0
          && send(e, effGetNumMidiOutputChannels) == 0, "no MIDI channels claimed");

    /*  Both precisions, because the plug-in claims both in its flags and a host
     *  that is told otherwise here will believe the second answer. */
    check(send(e, effSetProcessPrecision, 0, (VstIntPtr)kVstProcessPrecision32) == 1
          && send(e, effSetProcessPrecision, 0, (VstIntPtr)kVstProcessPrecision64) == 1,
          "effSetProcessPrecision accepts 32 and 64 bit");
}

void testParameterOpcodes(AEffect * e) {
    printf("\nparameters, over the ABI\n");

    bool named = true;
    char firstWrong[128] = "";
    for (int i = 0; i < kNumParameters; ++i) {
        TextBuf t;
        send(e, effGetParamName, i, 0, t.s);
        if (strcmp(t.s, kParamNames[i]) != 0) {
            named = false;
            if (!firstWrong[0])
                snprintf(firstWrong, sizeof firstWrong, "index %d gave '%s', wanted '%s'",
                         i, t.s, kParamNames[i]);
        }
    }
    check(named, "every parameter name arrives at the right index", firstWrong);

    /*  Not a formatting check - the point is that index and ptr survive the
     *  trip, and that the plug-in respects kVstMaxParamStrLen inside a bigger
     *  buffer than it was promised. */
    bool bounded = true;
    for (int i = 0; i < kNumParameters; ++i) {
        TextBuf t;
        send(e, effGetParamDisplay, i, 0, t.s);
        if (strlen(t.s) > (size_t)kVstMaxParamStrLen) bounded = false;
        TextBuf l;
        send(e, effGetParamLabel, i, 0, l.s);
        if (strlen(l.s) > (size_t)kVstMaxParamStrLen) bounded = false;
    }
    check(bounded, "displays and labels stay inside kVstMaxParamStrLen");

    /*  setParameter and getParameter are their own function pointers in
     *  AEffect, not dispatcher opcodes, so they are a separate piece of ABI. */
    bool roundTrip = true;
    for (int i = 0; i < kNumParameters; ++i) {
        const float was = e->getParameter(e, i);
        const float to = (was > 0.5f) ? 0.25f : 0.75f;
        e->setParameter(e, i, to);
        if (e->getParameter(e, i) != to) roundTrip = false;
        e->setParameter(e, i, was);
        if (e->getParameter(e, i) != was) roundTrip = false;
    }
    check(roundTrip, "the setParameter / getParameter thunks round trip");

    TextBuf prog;
    send(e, effGetProgramName, 0, 0, prog.s);
    check(strcmp(prog.s, "Default") == 0, "effGetProgramName", prog.s);

    char rename[] = "renamed";
    send(e, effSetProgramName, 0, 0, rename);
    TextBuf back;
    send(e, effGetProgramName, 0, 0, back.s);
    check(strcmp(back.s, "renamed") == 0, "effSetProgramName is read back", back.s);
    char restore[] = "Default";
    send(e, effSetProgramName, 0, 0, restore);
}

void testLatencyContract(AEffect * e) {
    printf("\nlatency, over the ABI\n");
    char d[96];

    snprintf(d, sizeof d, "%d samples", (int)e->initialDelay);
    if (kZeroLatency) {
        check(e->initialDelay == 0, "initialDelay is zero and declared as such", d);
        check(send(e, effGetTailSize) == 0, "effGetTailSize is zero - nothing to flush");
    } else {
        check(e->initialDelay > 0, "initialDelay is declared before any audio flows", d);
        const VstIntPtr tail = send(e, effGetTailSize);
        snprintf(d, sizeof d, "%d vs %d samples", (int)tail, (int)e->initialDelay);
        check(tail == e->initialDelay,
              "effGetTailSize matches it, so an offline bounce collects the end", d);
    }
}

void testTheChunk(AEffect * e) {
    printf("\nthe preset chunk, over the ABI\n");

    void * blob = 0;
    const VstInt32 size = (VstInt32)send(e, effGetChunk, 1, 0, &blob);
    char d[96];
    snprintf(d, sizeof d, "%d bytes", (int)size);
    check(size == (VstInt32)(kNumParameters * sizeof(float)) && blob != 0,
          "effGetChunk returns one float per parameter", d);
    if (!blob || size <= 0) return;

    /*  Copy it out immediately. The blob was allocated by the DLL's own CRT
     *  heap - these plug-ins are built /MT, so each module has its own - and
     *  freeing it here would be a crash rather than a leak. VST2 says the
     *  plug-in owns chunk memory; the Airwindows pattern never frees it, so a
     *  host that saves a session repeatedly leaks 28 bytes a time. Upstream
     *  behaviour, left alone deliberately, noted here so it is not mistaken
     *  for this test's doing. */
    std::vector<unsigned char> saved((size_t)size);
    memcpy(&saved[0], blob, (size_t)size);

    std::vector<float> was((size_t)kNumParameters);
    for (int i = 0; i < kNumParameters; ++i) was[(size_t)i] = e->getParameter(e, i);

    for (int i = 0; i < kNumParameters; ++i) e->setParameter(e, i, 0.123f);
    send(e, effSetChunk, 1, (VstIntPtr)size, &saved[0]);

    bool restored = true;
    for (int i = 0; i < kNumParameters; ++i) {
        if (e->getParameter(e, i) != was[(size_t)i]) restored = false;
    }
    check(restored, "effSetChunk puts every parameter back");
}

//! The one that can only be seen from here: is the DLL the same plug-in?
void testAudioMatchesTheStaticBuild(const std::vector<double> & inL,
                                    const std::vector<double> & inR) {
    printf("\naudio through the ABI vs the same source linked in\n");

    /*  A fresh instance, not the one the opcode tests have been poking: both
     *  copies have to start from the same state for a bit-comparison to mean
     *  anything, and the only state either of them is guaranteed to share is
     *  the state just after opening. */
    AEffect * e = g_entry(hostCallback);
    if (!e) { check(false, "a fresh instance is created"); return; }
    openThroughAbi(e);

    std::vector<double> dllL, dllR;
    runThroughAbi(e, inL, inR, dllL, dllR, kBlock);

    Plugin fx(hostCallback);
    openStatically(fx);
    std::vector<double> refL, refR;
    runStatically(fx, inL, inR, refL, refR, kBlock);

    char d[112];
    const double w = worstDiff(dllL, refL) + worstDiff(dllR, refR);
    snprintf(d, sizeof d, "worst deviation %.3e", w);
    check(w == 0.0, "the DLL's double path is identical to the bit", d);

    /*  Guards against the way this check could pass for the wrong reason: two
     *  copies that both hand the input straight back would also be identical. */
    const double moved = rmsDiff(dllL, inL);
    snprintf(d, sizeof d, "rms difference from input %.3e", moved);
    check(moved > 1.0e-4, "and both of them actually processed the signal", d);

    send(e, effClose);
}

void testBlockSizeThroughTheAbi(const std::vector<double> & inL,
                                const std::vector<double> & inR) {
    printf("\nblock sizes, over the ABI\n");

    /*  1025 is one past Dehum's internal scratch, so the chunking loop has to
     *  split a buffer and resume exactly. A fresh instance per block size,
     *  because both plug-ins carry state forward and a warm one would be
     *  answering a different question. Five seconds rather than the whole
     *  signal: block 1 means one call per sample, and the point here is the
     *  chunking, not the endurance. */
    const size_t frames = (size_t)(kRate * 5);
    std::vector<double> shortL(inL.begin(), inL.begin() + (ptrdiff_t)frames);
    std::vector<double> shortR(inR.begin(), inR.begin() + (ptrdiff_t)frames);

    const int blocks[3] = { 1, 512, 1025 };
    std::vector<double> refL, refR;
    for (int k = 0; k < 3; ++k) {
        AEffect * fresh = g_entry(hostCallback);
        if (!fresh) { check(false, "a fresh instance is created"); return; }
        openThroughAbi(fresh);

        std::vector<double> vL, vR;
        runThroughAbi(fresh, shortL, shortR, vL, vR, blocks[k]);
        if (k == 0) { refL = vL; refR = vR; }
        else {
            const double w = worstDiff(vL, refL) + worstDiff(vR, refR);
            char what[96], d[64];
            snprintf(what, sizeof what, "block %d gives the same stream as block 1", blocks[k]);
            snprintf(d, sizeof d, "worst %.3e", w);
            check(w == 0.0, what, d);
        }
        send(fresh, effClose);
    }
}

void testFloatPath(const std::vector<double> & inL, const std::vector<double> & inR) {
    printf("\nthe float path, over the ABI\n");

    AEffect * a = g_entry(hostCallback);
    AEffect * b = g_entry(hostCallback);
    if (!a || !b) { check(false, "two instances are created"); return; }
    openThroughAbi(a);
    openThroughAbi(b);

    std::vector<double> dL, dR;
    runThroughAbi(a, inL, inR, dL, dR, kBlock);

    /*  processReplacing is the double path plus a dither, so this is a
     *  tolerance rather than an identity - and the dither is seeded from rand(),
     *  which means it is not even repeatable between instances. What is being
     *  checked is that the 32 bit entry point is wired to the same DSP, not to
     *  something else. */
    std::vector<float> fl((size_t)kBlock), fr((size_t)kBlock);
    double worst = 0.0;
    size_t pos = 0;
    while (pos + (size_t)kBlock <= inL.size()) {
        for (size_t i = 0; i < (size_t)kBlock; ++i) {
            fl[i] = (float)inL[pos + i];
            fr[i] = (float)inR[pos + i];
        }
        float * bufs[2] = { &fl[0], &fr[0] };
        b->processReplacing(b, bufs, bufs, (VstInt32)kBlock);
        for (size_t i = 0; i < (size_t)kBlock; ++i) {
            const double d = fabs((double)fl[i] - dL[pos + i]);
            if (d > worst) worst = d;
        }
        pos += (size_t)kBlock;
    }
    char d[96];
    snprintf(d, sizeof d, "worst %.3e, tolerance 2.0e-07", worst);
    check(worst <= 2.0e-7, "processReplacing is processDoubleReplacing plus dither", d);

    send(a, effClose);
    send(b, effClose);
}

//! effClose destroys the plug-in. If the shim forgets that, a host that opens
//! and closes a plug-in - which is every plug-in scan, every project load -
//! leaks the whole instance, and for Dehum that is megabytes at a time.
void testCloseFreesTheInstance() {
    printf("\neffClose really destroys it\n");

    //warm the heap up first, so what is measured is growth and not first use
    for (int i = 0; i < 3; ++i) {
        AEffect * e = g_entry(hostCallback);
        if (!e) { check(false, "an instance is created"); return; }
        openThroughAbi(e);
        send(e, effClose);
    }
    const size_t before = privateBytes();

    const int cycles = 24;
    for (int i = 0; i < cycles; ++i) {
        AEffect * e = g_entry(hostCallback);
        if (!e) { check(false, "an instance is created"); return; }
        openThroughAbi(e);
        std::vector<double> bl((size_t)kBlock, 0.0), br((size_t)kBlock, 0.0);
        double * bufs[2] = { &bl[0], &br[0] };
        e->processDoubleReplacing(e, bufs, bufs, (VstInt32)kBlock);
        send(e, effClose);
    }
    const size_t after = privateBytes();
    const double grewMB = ((double)after - (double)before) / (1024.0 * 1024.0);

    char d[128];
    snprintf(d, sizeof d, "%d open/close cycles grew private bytes by %.2f MB",
             cycles, grewMB);
    check(grewMB < 4.0, "24 open/close cycles do not accumulate", d);
}

void testIoChangedContract() {
    printf("\nwho renegotiates latency\n");

    AEffect * e = g_entry(hostCallback);
    if (!e) { check(false, "an instance is created"); return; }
    openThroughAbi(e);

    g_ioChanged = 0;
    for (int i = 0; i < kNumParameters; ++i) {
        const float was = e->getParameter(e, i);
        e->setParameter(e, i, was > 0.5f ? 0.25f : 0.75f);
        std::vector<double> bl((size_t)kBlock, 0.0), br((size_t)kBlock, 0.0);
        double * bufs[2] = { &bl[0], &br[0] };
        e->processDoubleReplacing(e, bufs, bufs, (VstInt32)kBlock);
    }
    char d[96];
    snprintf(d, sizeof d, "audioMasterIOChanged called %d time%s",
             g_ioChanged, g_ioChanged == 1 ? "" : "s");
    if (kZeroLatency) {
        check(g_ioChanged == 0, "nothing in this plug-in ever renegotiates it", d);
    } else {
        check(g_ioChanged > 0, "the parameters that resize the pipeline renegotiate it", d);
    }
    send(e, effClose);
}

} // anonymous namespace

int main(int argc, char ** argv) {
    printf("winvst_host_verify: %s\n", kPluginName);

    if (argc < 2) {
        printf("\nusage: winvst_host_verify <path-to-dll>\n");
        return 2;
    }

    testItLoads(argv[1]);
    if (g_failures || !g_entry) {
        printf("\nFAILED (%d failure%s)\n", g_failures, g_failures == 1 ? "" : "s");
        return 1;
    }

    /*  A null audioMaster is refused on purpose - a plug-in with nowhere to
     *  call back is a plug-in that cannot report a latency change. */
    check(g_entry(0) == 0, "VSTPluginMain refuses a null audioMaster");

    AEffect * e = g_entry(hostCallback);
    if (!e) {
        check(false, "VSTPluginMain returns an AEffect");
        printf("\nFAILED (%d failure%s)\n", g_failures, g_failures == 1 ? "" : "s");
        return 1;
    }
    check(true, "VSTPluginMain returns an AEffect");

    testTheAEffect(e);
    openThroughAbi(e);

    testIdentityOpcodes(e);
    testParameterOpcodes(e);
    testLatencyContract(e);
    testTheChunk(e);

    //done with this one; the audio tests each want an instance that has not been
    //poked at, so they open their own
    send(e, effStopProcess);
    send(e, effMainsChanged, 0, 0);
    send(e, effClose);

    std::vector<double> inL, inR;
    makeSignal(inL, inR);

    testAudioMatchesTheStaticBuild(inL, inR);
    testBlockSizeThroughTheAbi(inL, inR);
    testFloatPath(inL, inR);

    testCloseFreesTheInstance();
    testIoChangedContract();

    printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "passed",
           g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
