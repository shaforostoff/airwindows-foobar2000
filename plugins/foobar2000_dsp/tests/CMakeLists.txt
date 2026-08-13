# ---------------------------------------------------------------------------
# Tests
#
#   decrackle_verify  - DSP core vs. a literal transcription of the Airwindows
#                       VST source, plus robustness and throughput. No SDK.
#   preset_roundtrip  - every parameter survives save/load. Links the SDK, so
#                       it needs foobar2000's shared.dll; skips if absent.
#   component_smoke   - loads the built DLL through foobar2000's own service
#                       plumbing. Needs foobar2000 installed; skips if absent.
# ---------------------------------------------------------------------------

add_executable(decrackle_verify
    decrackle_verify.cpp
    decrackle_reference.h
    "${CMAKE_CURRENT_SOURCE_DIR}/../foo_dsp_decrackle/decrackle_core.cpp"
)

target_include_directories(decrackle_verify PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../foo_dsp_decrackle")

if(MSVC)
    target_compile_options(decrackle_verify PRIVATE /W4)
    # The oracle is a verbatim copy of Airwindows code; its warnings are not
    # ours to fix.
    set_source_files_properties(decrackle_verify.cpp PROPERTIES
        COMPILE_OPTIONS "/wd4244;/wd4305;/wd4101")
endif()

add_test(NAME decrackle_verify COMMAND decrackle_verify)

# ---------------------------------------------------------------------------
# Preset serialisation. Always built - a compile break here is worth catching
# on every architecture - but only registered as a test where shared.dll can
# be placed beside it (see the smoke-test block below). The logic under test
# is architecture-independent, so running it on one architecture is enough.

include("${CMAKE_SOURCE_DIR}/cmake/fb2k_find_runtime.cmake")
fb2k_find_runtime("${FOO_DSP_TARGET_ARCH}" FB2K_RUNTIME_DIR)

add_executable(preset_roundtrip
    preset_roundtrip.cpp
    stdafx_host.h
    "${CMAKE_CURRENT_SOURCE_DIR}/../foo_dsp_declick/declick_preset.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../foo_dsp_declick/declick_core.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../foo_dsp_decrackle/decrackle_preset.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../foo_dsp_decrackle/decrackle_core.cpp"
)

target_include_directories(preset_roundtrip PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../foo_dsp_declick"
    "${CMAKE_CURRENT_SOURCE_DIR}/../foo_dsp_decrackle")

target_link_libraries(preset_roundtrip PRIVATE fb2k::sdk)

if(MSVC)
    target_compile_options(preset_roundtrip PRIVATE /W3)
endif()

if(FB2K_RUNTIME_DIR)
    add_custom_command(TARGET preset_roundtrip POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${FB2K_RUNTIME_DIR}/shared.dll" "$<TARGET_FILE_DIR:preset_roundtrip>/"
        VERBATIM)
    add_test(NAME preset_roundtrip COMMAND preset_roundtrip)
endif()

set_target_properties(preset_roundtrip PROPERTIES FOLDER "tests")

# ---------------------------------------------------------------------------

if(NOT FB2K_RUNTIME_DIR)
    message(STATUS "No ${FOO_DSP_TARGET_ARCH} foobar2000 install found "
                   "- skipping the smoke and preset tests "
                   "(set FOOBAR2000_DIR to point at one)")
else()
    message(STATUS "Component smoke test will use ${FB2K_RUNTIME_DIR}")

    add_executable(component_smoke component_smoke.cpp stdafx_host.h)
    target_link_libraries(component_smoke PRIVATE fb2k::sdk)
    if(MSVC)
        target_compile_options(component_smoke PRIVATE /W3)
    endif()

    # The host links against shared.dll just like foobar2000's own binaries do,
    # and the SDK touches it during static initialisation - so it has to sit
    # next to the executable rather than being found later at run time.
    add_custom_command(TARGET component_smoke POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${FB2K_RUNTIME_DIR}/shared.dll" "$<TARGET_FILE_DIR:component_smoke>/"
        VERBATIM)

    add_dependencies(component_smoke foo_dsp_decrackle foo_dsp_declick)

    add_test(NAME smoke_decrackle
             COMMAND component_smoke "$<TARGET_FILE:foo_dsp_decrackle>" "${FB2K_RUNTIME_DIR}"
                     --dialog 101 --slider 1001 --label 1011)
    add_test(NAME smoke_declick
             COMMAND component_smoke "$<TARGET_FILE:foo_dsp_declick>" "${FB2K_RUNTIME_DIR}"
                     --dialog 201 --slider 2001 --label 2011)
    # These open a real modal window; do not let them wedge CI.
    set_tests_properties(smoke_decrackle smoke_declick PROPERTIES TIMEOUT 120)
    set_target_properties(component_smoke PROPERTIES FOLDER "tests")
endif()

set_target_properties(decrackle_verify PROPERTIES FOLDER "tests")
