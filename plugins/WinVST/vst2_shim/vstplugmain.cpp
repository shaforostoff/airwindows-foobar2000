/* ========================================
 *  vst2_shim/vstplugmain.cpp
 *
 *  The module entry point. A VST2 host loads the library, looks up one symbol,
 *  calls it, and everything else in the plug-in's life happens through the
 *  AEffect it gets back. MIT licensed with the rest of this tree; see
 *  vst2_abi.h for why this is not Steinberg's file.
 * ======================================== */

#include "audioeffectx.h"

#if defined(_WIN32)
  #define VST_EXPORT extern "C" __declspec(dllexport)
#else
  /*  Everything else here is compiled -fvisibility=hidden, which is what keeps
   *  a .so from exporting the plug-in's whole C++ surface to the host. */
  #define VST_EXPORT extern "C" __attribute__((visibility("default")))
#endif

/*  The one symbol.
 *
 *  Hosts old enough to predate the rename look for "main" instead, so that name
 *  is aliased to the same function. On Windows the plug-in's vstplug.def does
 *  it, which is why the .def is on the link line at all - the __declspec above
 *  is enough to export VSTPluginMain by itself. An .so has no .def, so the
 *  alias is made below instead.
 *
 *  One deliberate difference from the SDK: the SDK opens by asking the host
 *  audioMasterVersion and returning null if the answer is 0, which is meant to
 *  turn away hosts too old to have processReplacing. In practice a plug-in
 *  scanner with a stub callback that answers 0 to everything will also be
 *  turned away, and it reports the plug-in as broken rather than as
 *  incompatible. So the version is not consulted. JUCE's VST2 wrapper made the
 *  same call, for the same reason. The audioMaster pointer is still required to
 *  be non-null, because ioChanged() needs somewhere to call.
 */
VST_EXPORT AEffect * VSTPluginMain(audioMasterCallback audioMaster)
{
    if (!audioMaster) return 0;

    AudioEffect * effect = createEffectInstance(audioMaster);
    if (!effect) return 0;

    return effect->getAeffect();
    //and from here the host owns it, until it sends effClose - which is where
    //DispatcherProc deletes it. Nothing on this side keeps a list.
}

#if !defined(_WIN32)
/*  The "main" alias, as a forwarding function with its assembler name set: the
 *  C++ identifier is not main, so this is not the entry point of a program and
 *  the compiler does not treat it as one. Declaring the asm name on the
 *  declaration is the only way to define a symbol that C++ will not let you
 *  name; a .def file's job, done in the source. Nothing calls this by its C++
 *  name, and a plug-in load costs one extra jump.
 */
VST_EXPORT AEffect * vstPluginMainLegacyAlias(audioMasterCallback audioMaster) asm("main");

AEffect * vstPluginMainLegacyAlias(audioMasterCallback audioMaster)
{
    return VSTPluginMain(audioMaster);
}
#endif
