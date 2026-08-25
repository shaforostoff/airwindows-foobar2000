/* ========================================
 *  Dehum - VirtualDJ buffer effect
 *
 *  IVdjPluginBufferDsp8. The cancellation is identical to the live plug-in's -
 *  same core, same zero latency - and what this interface adds is the scout:
 *  GetSongBuffer will hand over any part of the decoded song, so the opening of
 *  the record is analysed at eight times playback speed while it plays, and the
 *  lines that turn up are handed to Channel::adopt().
 *
 *  That takes acquisition from tens of seconds to a few. See
 *  dehum_vdj_scout.h for the whole argument and the cost.
 * ======================================== */

#include "vdj_buffer_dsp.h"

#include "dehum_engine.h"

#include "vdj_entry.h"

typedef vdj::BufferDsp<vdj::DehumEngine> DehumPlugin;

VDJ_PLUGIN_ENTRY(DehumPlugin, IID_IVdjPluginBuffer8)
