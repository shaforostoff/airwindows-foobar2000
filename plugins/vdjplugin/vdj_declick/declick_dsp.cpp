/* ========================================
 *  Declick - live VirtualDJ sound effect
 *
 *  IVdjPluginDsp8. Processes whatever it is put on - a deck, a microphone, the
 *  master - and plays declick::Config::latency samples late for it, because
 *  this interface has no lookahead and the repair needs one. 20 ms at 44.1 kHz
 *  with the default Max repair and Model order.
 *
 *  On a deck that is being beatmatched that delay is the wrong trade and
 *  DeclickBuffer is the right one; see the comment at the top of
 *  ../common/vdj_realtime_dsp.h for the whole argument. Everything specific to
 *  this plug-in is in declick_engine.h, and everything about the interface is
 *  in the wrapper it is handed to.
 * ======================================== */

#include "vdj_realtime_dsp.h"

#include "declick_engine.h"

#include "vdj_entry.h"

typedef vdj::RealtimeDsp<vdj::DeclickEngine> DeclickLivePlugin;

VDJ_PLUGIN_ENTRY(DeclickLivePlugin, IID_IVdjPluginDsp8)
