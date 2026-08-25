/* ========================================
 *  Dehum - live VirtualDJ sound effect
 *
 *  IVdjPluginDsp8, which for this core costs nothing: the detector reads the
 *  signal but does not sit in the path, so there is no lookahead to pay for and
 *  no delay to declare. That makes this the natural form of the dehummer, and
 *  the only one that will run on a microphone or on the master - there is no
 *  song to read ahead of in either case.
 *
 *  What it does not get is DehumBuffer's head start. Unaided, the detector needs
 *  9 s to confirm a line the prominence route can see and 43 s for one only the
 *  coherence route can reach; see the acquisition note at the top of
 *  dehum_core.h. On a loaded record, prefer DehumBuffer.
 * ======================================== */

#include "vdj_realtime_dsp.h"

#include "dehum_engine.h"

#include "vdj_entry.h"

typedef vdj::RealtimeDsp<vdj::DehumEngine> DehumLivePlugin;

VDJ_PLUGIN_ENTRY(DehumLivePlugin, IID_IVdjPluginDsp8)
