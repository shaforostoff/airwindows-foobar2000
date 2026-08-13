/* ========================================
 *  vst2_stub/audioeffectx.h
 *
 *  A minimal stand-in for the VST 2.4 SDK header of the same name, written
 *  from the documented API purely so that plugins/WinVST/Declick can be
 *  compiled and exercised by declick_vst_verify.
 *
 *  This is NOT Steinberg's header. Steinberg's vst2.x sources are not
 *  redistributable and are not in this repository - see
 *  plugins/AirwindowsWinVSTTemplate.txt. What is here is the handful of
 *  declarations the Airwindows plug-in pattern actually touches, and nothing
 *  else: no host side, no dispatcher, no editor, no AEffect layout.
 *
 *  What that buys, and what it does not:
 *
 *    it does     let the DSP, the latency contract, the parameter mapping and
 *                the preset chunk be tested on every build, on both
 *                architectures, with no SDK present;
 *    it does not prove the plug-in compiles against the real SDK. Signatures
 *                here were written to match, but only a build in the real
 *                VSTProject settles that.
 *
 *  Two members - sampleRate and initialDelay - are exposed as plain fields
 *  rather than hidden behind the SDK's accessors, and ioChangedCount does not
 *  exist in the SDK at all. They are how the test observes what the plug-in
 *  told the host. Marked below.
 * ======================================== */

#ifndef __audioeffectx__
#define __audioeffectx__
#define __audioeffect__

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef int32_t VstInt32;
typedef intptr_t VstIntPtr;

enum {
    kVstMaxProgNameLen   = 24,
    kVstMaxParamStrLen   = 8,
    kVstMaxProductStrLen = 64,
    kVstMaxVendorStrLen  = 64
};

enum VstPlugCategory {
    kPlugCategUnknown = 0,
    kPlugCategEffect,
    kPlugCategSynth,
    kPlugCategAnalysis,
    kPlugCategMastering,
    kPlugCategSpacializer,
    kPlugCategRoomFx,
    kPlugSurroundFx,
    kPlugCategRestoration,
    kPlugCategOfflineProcess,
    kPlugCategShell,
    kPlugCategGenerator
};

struct AEffect;
typedef VstIntPtr (*audioMasterCallback)(AEffect *, VstInt32, VstInt32, VstIntPtr,
                                         void *, float);

inline void vst_strncpy(char * dst, const char * src, size_t maxLen) {
#if defined(_MSC_VER)
    strncpy_s(dst, maxLen + 1, src, maxLen);
#else
    strncpy(dst, src, maxLen);
#endif
    dst[maxLen] = 0;
}

class AudioEffect {
public:
    AudioEffect(audioMasterCallback, VstInt32, VstInt32) {}
    virtual ~AudioEffect() {}

    virtual void resume() {}
    virtual void suspend() {}

    void setNumInputs(VstInt32) {}
    void setNumOutputs(VstInt32) {}
    void setUniqueID(VstInt32) {}
    void canProcessReplacing(bool = true) {}
    void programsAreChunks(bool = true) {}

    void setInitialDelay(VstInt32 d) { initialDelay = d; }
    float getSampleRate() { return sampleRate; }

    // The SDK's own hand-rolled formatters; these only have to be close enough
    // for the test to read a number back out, and the test parses rather than
    // string-compares for exactly that reason.
    void float2string(float v, char * t, VstInt32 maxLen) {
        char b[64];
        snprintf(b, sizeof b, "%.3f", (double)v);
        vst_strncpy(t, b, (size_t)maxLen);
    }
    void int2string(VstInt32 v, char * t, VstInt32 maxLen) {
        char b[64];
        snprintf(b, sizeof b, "%d", (int)v);
        vst_strncpy(t, b, (size_t)maxLen);
    }

    // --- not SDK API: how the test sees what the host would have been told ---
    float sampleRate = 44100.0f;
    VstInt32 initialDelay = 0;
    int ioChangedCount = 0;
};

class AudioEffectX : public AudioEffect {
public:
    AudioEffectX(audioMasterCallback master, VstInt32 programs, VstInt32 params)
        : AudioEffect(master, programs, params) {}
    void canDoubleReplacing(bool = true) {}
    bool ioChanged() { ++ioChangedCount; return true; }
};

#endif // __audioeffectx__
