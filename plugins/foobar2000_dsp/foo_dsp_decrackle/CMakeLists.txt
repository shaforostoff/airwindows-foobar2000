# ---------------------------------------------------------------------------
# foo_dsp_decrackle
# ---------------------------------------------------------------------------

add_library(foo_dsp_decrackle SHARED
    component.cpp
    config_dialog.cpp
    decrackle_core.cpp
    decrackle_preset.cpp
    dsp_decrackle.cpp
    foo_dsp_decrackle.rc

    decrackle_core.h
    decrackle_preset.h
    resource.h
    stdafx.h
    version.h
)

target_link_libraries(foo_dsp_decrackle PRIVATE fb2k::sdk comctl32)
target_include_directories(foo_dsp_decrackle PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

# foobar2000 identifies components by file name; it must be exactly this.
set_target_properties(foo_dsp_decrackle PROPERTIES
    OUTPUT_NAME "foo_dsp_decrackle"
    PREFIX ""
    SUFFIX ".dll")

if(MSVC)
    target_compile_options(foo_dsp_decrackle PRIVATE /W4)
    # The resource compiler needs to find resource.h and version.h.
    target_include_directories(foo_dsp_decrackle PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

# A .fb2k-component archive carries the 32 and 64 bit binaries together, so
# packaging cannot happen inside a single-architecture build tree. It lives in
# scripts/build_release.ps1 instead. `install` is here so a single-architecture
# build can still be collected on its own.
if(FOO_DSP_TARGET_ARCH STREQUAL "x64")
    set(_component_subdir "x64")
elseif(FOO_DSP_TARGET_ARCH STREQUAL "ARM64EC")
    set(_component_subdir "arm64ec")
else()
    set(_component_subdir ".")
endif()

install(TARGETS foo_dsp_decrackle RUNTIME DESTINATION "${_component_subdir}")
install(FILES "$<TARGET_PDB_FILE:foo_dsp_decrackle>"
        DESTINATION "${_component_subdir}" OPTIONAL)
