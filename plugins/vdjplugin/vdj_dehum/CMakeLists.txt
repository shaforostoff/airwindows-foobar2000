# One plug-in, which also compiles the scout - see dehum_vdj_scout.h.
#
# dehum_core.{h,cpp} is a mirror, not a source: it is copied here from
# ../../foobar2000_dsp/foo_dsp_dehum by scripts/sync_cores.ps1 (or .sh) in that
# tree, and an edit made here is an edit that will be overwritten. Change the
# canonical copy.

add_vdj_plugin(Dehum
    SOURCES dehum_plugin.cpp
            dehum_core.cpp
            dehum_core.h
            dehum_engine.h
            dehum_vdj_scout.h)
