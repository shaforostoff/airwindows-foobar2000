/* ========================================
 *  foo_dsp_dehum - preset (de)serialisation and dialog entry point
 *
 *  Include after stdafx.h; assumes the foobar2000 SDK is visible.
 * ======================================== */

#ifndef FOO_DSP_DEHUM_PRESET_H
#define FOO_DSP_DEHUM_PRESET_H

#include "dehum_core.h"

namespace dehum_preset {

//! Identity of this DSP in the stored chain configuration. Never change it.
GUID guid();

enum { version = 1 };

void make(const dehum::Params & params, dsp_preset & out);

//! Never throws: anything unreadable falls back to the defaults.
dehum::Params parse(const dsp_preset & in);

} // namespace dehum_preset

//! Blocking modal configuration dialog. Main thread only.
void dehum_config_popup(const dsp_preset & data, HWND parent,
                        dsp_preset_edit_callback & callback);

#endif // FOO_DSP_DEHUM_PRESET_H
