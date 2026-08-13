/* ========================================
 *  foo_dsp_decrackle - preset (de)serialisation and dialog entry point
 *
 *  Include after stdafx.h; this header assumes the foobar2000 SDK is visible.
 * ======================================== */

#ifndef FOO_DSP_DECRACKLE_PRESET_H
#define FOO_DSP_DECRACKLE_PRESET_H

#include "decrackle_core.h"

namespace decrackle_preset {

//! Identity of this DSP in the stored chain configuration. Never change it.
GUID guid();

//! Current on-disk preset revision.
enum { version = 1 };

void make(const airwindows::DeCrackleParams & params, dsp_preset & out);

//! Never throws: anything unreadable falls back to the defaults.
airwindows::DeCrackleParams parse(const dsp_preset & in);

} // namespace decrackle_preset

//! Blocking modal configuration dialog. Main thread only.
void decrackle_config_popup(const dsp_preset & data, HWND parent,
                            dsp_preset_edit_callback & callback);

#endif // FOO_DSP_DECRACKLE_PRESET_H
