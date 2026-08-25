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
 *
 *  Every call is traced when the build asks for it - see vdj_trace.h. This is
 *  the first place worth watching when a host does not list a plug-in, because
 *  it is where "the host never looked" and "the host looked and wanted
 *  something else" stop being indistinguishable.
 * ======================================== */

#ifndef VDJ_ENTRY_H
#define VDJ_ENTRY_H

#include "vdjPlugin8.h"

#include "vdj_trace.h"

#include <new>
#include <string.h>

//! `iid` is the interface constant from vdjDsp8.h - IID_IVdjPluginDsp8 for a
//! live effect, IID_IVdjPluginBuffer8 for a buffer one. `label` names the
//! module in the trace, since all of them write to one log.
#define VDJ_PLUGIN_ENTRY_NAMED(PluginClass, iid, label)                         \
    extern "C" VDJ_EXPORT HRESULT VDJ_API                                      \
    DllGetClassObject(const GUID & rclsid, const GUID & riid, void ** ppObject) \
    {                                                                          \
        VDJ_TRACE_ENTRY(label, rclsid, riid);                                  \
        if (ppObject == NULL) return CLASS_E_CLASSNOTAVAILABLE;                \
        *ppObject = NULL;                                                      \
        if (memcmp(&rclsid, &CLSID_VdjPlugin8, sizeof(GUID)) != 0) {           \
            VDJ_TRACEF("%s: declined - not CLSID_VdjPlugin8", label);          \
            return CLASS_E_CLASSNOTAVAILABLE;                                  \
        }                                                                      \
        if (memcmp(&riid, &(iid), sizeof(GUID)) != 0) {                        \
            VDJ_TRACEF("%s: declined - asked for an interface this module "    \
                       "does not implement", label);                          \
            return CLASS_E_CLASSNOTAVAILABLE;                                  \
        }                                                                      \
        /* nothrow because this is an ABI boundary: an exception unwinding      \
           into VirtualDJ across it is undefined, and a plug-in that fails to   \
           load is a message in the log rather than a crash. */                 \
        PluginClass * p = new (std::nothrow) PluginClass();                     \
        if (p == NULL) {                                                       \
            VDJ_TRACEF("%s: out of memory", label);                            \
            return CLASS_E_CLASSNOTAVAILABLE;                                  \
        }                                                                      \
        VDJ_TRACEF("%s: created", label);                                      \
        *ppObject = p;                                                         \
        return NO_ERROR;                                                       \
    }

#define VDJ_PLUGIN_ENTRY(PluginClass, iid) \
    VDJ_PLUGIN_ENTRY_NAMED(PluginClass, iid, #PluginClass)

#if defined(VDJ_TRACE)
#define VDJ_TRACE_ENTRY(label, rclsid, riid)                                   \
    do {                                                                       \
        char _c[48], _i[48];                                                   \
        VDJ_TRACEF("%s: DllGetClassObject clsid %s iid %s", (label),            \
                   VDJ_TRACE_GUID((rclsid), _c), VDJ_TRACE_GUID((riid), _i));   \
    } while (0)
#else
#define VDJ_TRACE_ENTRY(label, rclsid, riid) ((void)0)
#endif

#endif // VDJ_ENTRY_H
