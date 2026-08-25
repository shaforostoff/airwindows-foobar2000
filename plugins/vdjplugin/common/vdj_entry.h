/* ========================================
 *  vdjplugin - the one exported function
 *
 *  A VirtualDJ plug-in is a module that exports DllGetClassObject and answers
 *  it with an object implementing the interface whose IID was asked for. That
 *  is the entire loading protocol - there is no registration, no manifest and
 *  no factory beyond this - so the same eight lines appear in every plug-in and
 *  live here instead.
 *
 *  One plug-in per module, deliberately. DllGetClassObject is handed a single
 *  IID and has one object to give back, so there is no way for one DLL to offer
 *  two differently named effects; and it is the IID, not the file name or the
 *  folder, that decides which kind of plug-in VirtualDJ thinks it has. Hence
 *  four modules for two cores.
 * ======================================== */

#ifndef VDJ_ENTRY_H
#define VDJ_ENTRY_H

#include "vdjPlugin8.h"

#include <new>
#include <string.h>

//! `iid` is the interface constant from vdjDsp8.h - IID_IVdjPluginDsp8 for a
//! live effect, IID_IVdjPluginBuffer8 for a buffer one.
#define VDJ_PLUGIN_ENTRY(PluginClass, iid)                                     \
    extern "C" VDJ_EXPORT HRESULT VDJ_API                                      \
    DllGetClassObject(const GUID & rclsid, const GUID & riid, void ** ppObject) \
    {                                                                          \
        if (ppObject == NULL) return CLASS_E_CLASSNOTAVAILABLE;                \
        *ppObject = NULL;                                                      \
        if (memcmp(&rclsid, &CLSID_VdjPlugin8, sizeof(GUID)) != 0) {           \
            return CLASS_E_CLASSNOTAVAILABLE;                                  \
        }                                                                      \
        if (memcmp(&riid, &(iid), sizeof(GUID)) != 0) {                        \
            return CLASS_E_CLASSNOTAVAILABLE;                                  \
        }                                                                      \
        /* nothrow because this is an ABI boundary: an exception unwinding      \
           into VirtualDJ across it is undefined, and a plug-in that fails to   \
           load is a message in the log rather than a crash. */                 \
        PluginClass * p = new (std::nothrow) PluginClass();                     \
        if (p == NULL) return CLASS_E_CLASSNOTAVAILABLE;                        \
        *ppObject = p;                                                         \
        return NO_ERROR;                                                       \
    }

#endif // VDJ_ENTRY_H
