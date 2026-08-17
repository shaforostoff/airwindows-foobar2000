# vst2_shim

A clean-room implementation of the VST 2.4 plug-in ABI, enough to build the
plug-ins in this folder into DLLs that real hosts load. MIT licensed, like the
rest of the tree.

## Why it exists

`plugins/AirwindowsWinVSTTemplate.txt` is blunt about it:

> the builds have been intentionally broken because Steinberg doesn't want you
> to develop VST2s, so you're on your own

The six files it says you need — `aeffeditor.h`, `audioeffect.{h,cpp}`,
`audioeffectx.{h,cpp}`, `vstplugmain.cpp` — are not redistributable and are not
here. Without them, `plugins/WinVST` is source code that cannot be built by
anybody who does not already have a copy of a discontinued SDK.

So this is written from the published description of the interface instead.
JUCE, Ardour and LMMS all arrived at the same place and did the same thing.

## What is here

| file | what it is |
|---|---|
| `vst2_abi.h` | the ABI: `AEffect`, the opcode enums, the flags. Mostly comments and `static_assert`s. |
| `audioeffectx.h` | `AudioEffect` and `AudioEffectX`, the classes a plug-in derives from. |
| `audioeffectx.cpp` | the opcode dispatcher and the five C thunks in `AEffect`. |
| `vstplugmain.cpp` | `VSTPluginMain`, the single exported symbol. |

No editor, no MIDI, no offline processing, no speaker arrangements, no
parameter properties. Those opcodes are answered "not supported", which is what
the stock Airwindows plug-ins answered anyway by not overriding them.

## The part to be suspicious of

An ABI is not an interface you get to design. Every byte offset in `AEffect`
and every integer in the opcode enums was fixed in 1999 by hosts that are still
in use, and getting one wrong does not fail to compile — it loads, and then a
host reads a function pointer out of the middle of an integer field and jumps
to it.

Three things push back on that:

- **Every field offset is `static_assert`ed**, along with `sizeof(AEffect)` —
  144 bytes on 32 bit, 192 on 64 bit. Those two numbers are the one thing an
  outsider can check this against without reading it.
- **The opcode enums keep their deprecated entries.** `effGetVu` is unused and
  deleting it would silently move the ten opcodes after it. The values the
  plug-ins depend on are additionally asserted as literals.
- **`tests/winvst_host_verify.cpp` loads a finished DLL** and drives it through
  the C ABI and nothing else — `LoadLibrary`, `GetProcAddress`, opcodes, the
  function pointers — comparing the audio that comes back against the same core
  driven directly, and checking that repeated open/close does not leak.

What none of that can prove is that 144 and 192 and `effGetChunk == 23` are
themselves right, because the shim and its test agree with each other by
construction. Only a real host settles that. What the assertions do is stop the
numbers moving once they are set.

## Building

    scripts\build_winvst.ps1

from `plugins/foobar2000_dsp`. It compiles with `cl.exe` directly rather than
through each plug-in's `VSTProject.vcxproj`, because those ask for toolset v140
and Windows SDK 8.1 and expect the SDK at a path that is not in this tree. The
`.vcxproj`, `.sln` and `.def` files are left exactly as Airwindows ships them,
so the documented "drag the folder into VSTProject and press build" route still
works for anyone who does have the real SDK.
