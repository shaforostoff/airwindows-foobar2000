/* ========================================
 *  foo_dsp_decrackle - component declaration
 * ======================================== */

#include "stdafx.h"
#include "version.h"

DECLARE_COMPONENT_VERSION(
    "DeCrackle",
    FOO_DSP_DECRACKLE_VERSION_STRING,
    "DeCrackle - isolates and removes vinyl clicks and crackles.\n"
    "\n"
    "A foobar2000 DSP port of the Airwindows DeCrackle plug-in by Chris Johnson.\n"
    "Airwindows code is MIT licensed; see https://www.airwindows.com/\n"
    "\n"
    "Controls:\n"
    "  Filter   - how dark the audio that replaces a click is. Tune it to hide\n"
    "             the transitions; full bass is not always the best setting.\n"
    "  Window   - width of the detection window, from very narrow to very wide.\n"
    "  Thresld  - lower it to catch more. Be careful about it triggering on\n"
    "             actual music, which sounds bad.\n"
    "  Surface  - 0 is off; higher settings apply increasing treble filtering\n"
    "             aimed at general surface noise in quiet passages.\n"
    "  Dry/Wet  - at exactly 0.000 this becomes delta monitoring: you hear only\n"
    "             what is being removed. If music comes through, raise Thresld.\n"
    "\n"
    "Note that this DSP is not zero latency; the delay grows with Window.\n"
);

VALIDATE_COMPONENT_FILENAME("foo_dsp_decrackle.dll");
