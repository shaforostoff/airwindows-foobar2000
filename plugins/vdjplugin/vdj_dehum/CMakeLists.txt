# Two plug-ins, one core. The buffer one also compiles the scout, which has no
# counterpart in the live plug-in - see dehum_vdj_scout.h.
#
# dehum_core.{h,cpp} is a mirror, not a source: it is copied here from
# ../../foobar2000_dsp/foo_dsp_dehum by scripts/sync_cores.ps1 (or .sh) in that
# tree, and an edit made here is an edit that will be overwritten. Change the
# canonical copy.

add_vdj_plugin(Dehum
    SOURCES dehum_dsp.cpp
            dehum_core.cpp
            dehum_core.h
            dehum_engine.h)

add_vdj_plugin(DehumBuffer
    SOURCES dehum_buffer.cpp
            dehum_core.cpp
            dehum_core.h
            dehum_engine.h
            dehum_vdj_scout.h)
