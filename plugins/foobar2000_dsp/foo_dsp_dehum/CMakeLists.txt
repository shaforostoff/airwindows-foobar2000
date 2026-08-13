# ---------------------------------------------------------------------------
# foo_dsp_dehum
# ---------------------------------------------------------------------------

add_library(foo_dsp_dehum SHARED
    component.cpp
    config_dialog.cpp
    dehum_core.cpp
    dehum_preset.cpp
    dsp_dehum.cpp
    foo_dsp_dehum.rc

    dehum_core.h
    dehum_preset.h
    resource.h
    stdafx.h
    version.h
)

target_link_libraries(foo_dsp_dehum PRIVATE fb2k::sdk comctl32)
target_include_directories(foo_dsp_dehum PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

set_target_properties(foo_dsp_dehum PROPERTIES
    OUTPUT_NAME "foo_dsp_dehum"
    PREFIX ""
    SUFFIX ".dll")

if(MSVC)
    target_compile_options(foo_dsp_dehum PRIVATE /W4)
endif()

if(FOO_DSP_TARGET_ARCH STREQUAL "x64")
    set(_component_subdir "x64")
elseif(FOO_DSP_TARGET_ARCH STREQUAL "ARM64EC")
    set(_component_subdir "arm64ec")
else()
    set(_component_subdir ".")
endif()

install(TARGETS foo_dsp_dehum RUNTIME DESTINATION "${_component_subdir}")
install(FILES "$<TARGET_PDB_FILE:foo_dsp_dehum>"
        DESTINATION "${_component_subdir}" OPTIONAL)
