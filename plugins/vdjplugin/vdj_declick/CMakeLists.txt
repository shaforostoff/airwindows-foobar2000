# Two plug-ins, one core. See ../CMakeLists.txt for which is which and
# ../common/vdj_realtime_dsp.h for why there are two.
#
# declick_core.{h,cpp} is a mirror, not a source: it is copied here from
# ../../foobar2000_dsp/foo_dsp_declick by scripts/sync_cores.ps1 (or .sh) in
# that tree, and an edit made here is an edit that will be overwritten. Change
# the canonical copy.

add_vdj_plugin(Declick
    SOURCES declick_dsp.cpp
            declick_core.cpp
            declick_core.h
            declick_engine.h)

add_vdj_plugin(DeclickBuffer
    SOURCES declick_buffer.cpp
            declick_core.cpp
            declick_core.h
            declick_engine.h)
