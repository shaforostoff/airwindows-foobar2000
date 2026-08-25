/* ========================================
 *  Declick - VirtualDJ buffer effect
 *
 *  IVdjPluginBufferDsp8, which is the interesting one: it can read the song at
 *  any position, so the lookahead the repair needs is satisfied by reading
 *  ahead rather than by delaying the output, and the deck stays sample-aligned
 *  with everything it is being mixed against.
 *
 *  ../common/vdj_buffer_dsp.h has the details, including the three things the
 *  interface imposes that are not obvious from its one method: the song buffer
 *  is 16 bit, the buffer handed back must not be VirtualDJ's own, and `pos` is
 *  not monotonic because it follows a deck that can be scratched.
 * ======================================== */

#include "vdj_buffer_dsp.h"

#include "declick_engine.h"

#include "vdj_entry.h"

typedef vdj::BufferDsp<vdj::DeclickEngine> DeclickBufferPlugin;

VDJ_PLUGIN_ENTRY(DeclickBufferPlugin, IID_IVdjPluginBuffer8)
