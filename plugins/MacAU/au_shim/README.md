# au_shim

Enough of Apple's Audio Unit base classes to compile `plugins/MacAU/Declick`
and `plugins/MacAU/Dehum` and drive them from a test. MIT licensed, like the
rest of the tree.

## Why it exists

`plugins/AirwindowsAUTemplate.txt` builds these plug-ins against the CoreAudio
`AUPublic` and `PublicUtility` sources that shipped with Xcode 3.2.6, which the
`.xcodeproj` reaches for at `$(SYSTEM_DEVELOPER_DIR)/Extras/CoreAudio/...`.
Those files are Apple's, they are not redistributable, they are not in this
repository, and they have not shipped with Xcode for well over a decade. So
`plugins/MacAU` is 542 folders of source that only builds on a machine that
still has that SDK — which is fine for the 540 that were written on one, and no
use at all for checking that the two written here are correct.

This is not the Xcode SDK, and it is not trying to be. It is the smallest thing
that lets `Declick.cpp` and `Dehum.cpp` compile unmodified and be driven the way
a host drives them, so that `declick_au_verify` and `dehum_au_verify` can
compare their output against the core those files share with
`foo_dsp_declick`/`foo_dsp_dehum`.

## What is here

One header, `AUEffectBase.h`, holding:

| | |
|---|---|
| scalars and handles | `ComponentResult`, `OSStatus`, `Float32/64`, `AudioUnit`, the `AudioUnitScope`/`Element`/`ParameterID` family |
| structures | `AudioBufferList`, `AudioBuffer`, `AUChannelInfo`, `AudioUnitParameterInfo` |
| constants | the handful of `kAudioUnit*` values these two plug-ins name |
| `AUBase` | parameter storage, `Globals()`, `GetSampleRate()`, `FillInParameterName()`, `PropertyChanged()` |
| `AUEffectBase` | the virtuals the plug-ins override, each defaulting to the "not supported" answer |
| `COMPONENT_ENTRY` | defines the `<Name>Entry` symbol, so a missing or misspelled one is a link error |

Plus four methods a real AU does not have, spelled `AUShim*` so they cannot be
mistaken for SDK surface: set the sample rate, and read back how many times the
plug-in called `PropertyChanged` and with what. That last pair is the point of
the whole file — it is what an AU says instead of the VST's
`audioMasterIOChanged`, and the tests assert on exactly when it happens.

## The part to be suspicious of

Unlike `plugins/WinVST/vst2_shim`, **this shim is not an ABI**. Nothing here is
loaded by anything; the tests link the plug-in's C++ classes directly. So the
numeric values below are inputs to a comparison and nothing more, and none of
them is asserted:

- The `kAudioUnit*` constants are written from the published headers but are
  never round-tripped through anything that would notice if one were wrong.
- `AudioUnitParameterInfo` has the right *fields*, not necessarily the right
  layout, and nothing depends on its layout.
- `AudioBufferList` ends in a fixed `mBuffers[2]` rather than the real flexible
  `mBuffers[1]` that callers over-allocate. Both plug-ins are stereo only —
  `SupportedNumChannels()` says 2 in, 2 out and nothing else — so two is the
  whole story here.
- Parameter names are `const char *` and `CFSTR` is the identity, because
  nothing in this tree does anything with a parameter name but hand it straight
  back. That is also what lets the tests build on a machine with no
  CoreFoundation.

What that leaves unproven is everything between the compiler and a DAW: the
Component Manager registration, the `.r` resource, the entry point, the
`Info.plist`, `GetPropertyInfo`/`GetProperty` beyond the fact that they forward.
There is no AU equivalent here of `vst_host_verify`, which loads a finished
`.dll` or `.so` and talks to it over the C ABI alone — building a `.component`
needs the Apple sources this file exists because of. **These two plug-ins have not been
loaded by a real host.** What the shim establishes is that the wrapper compiles
and that its DSP, its latency and tail arithmetic, its parameter table and its
`Reset()` behaviour are what they are supposed to be.

## Building

There is no build script: the two tests are ordinary targets in
`plugins/foobar2000_dsp/tests/CMakeLists.txt`. To run them from a Mac, where
that project does not configure, compile them directly:

    cd plugins
    clang++ -std=c++11 -O2 -I MacAU/au_shim -I MacAU/Declick \
        foobar2000_dsp/tests/declick_au_verify.cpp \
        MacAU/Declick/Declick.cpp MacAU/Declick/declick_core.cpp \
        -o declick_au_verify && ./declick_au_verify

    clang++ -std=c++11 -O2 -I MacAU/au_shim -I MacAU/Dehum \
        foobar2000_dsp/tests/dehum_au_verify.cpp \
        MacAU/Dehum/Dehum.cpp MacAU/Dehum/dehum_core.cpp \
        -o dehum_au_verify && ./dehum_au_verify
