/* ========================================
 *  vst2_shim/vstplugmain.cpp
 *
 *  The DLL entry point. A VST2 host loads the library, looks up one symbol,
 *  calls it, and everything else in the plug-in's life happens through the
 *  AEffect it gets back. MIT licensed with the rest of this tree; see
 *  vst2_abi.h for why this is not Steinberg's file.
 * ======================================== */

#include "audioeffectx.h"

#define VST_EXPORT extern "C" __declspec(dllexport)

/*  The one symbol. Each plug-in's vstplug.def also aliases the name "main" to
 *  it, for hosts that predate the rename - that alias is why the .def file is
 *  on the link line at all, since the __declspec above is enough to export
 *  VSTPluginMain by itself.
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
