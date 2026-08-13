# ---------------------------------------------------------------------------
# Tests
#
#   decrackle_verify  - DSP core vs. a literal transcription of the Airwindows
#                       VST source, plus robustness and throughput. No SDK.
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

include("${CMAKE_SOURCE_DIR}/cmake/fb2k_find_runtime.cmake")
fb2k_find_runtime("${FOO_DSP_TARGET_ARCH}" FB2K_RUNTIME_DIR)

if(NOT FB2K_RUNTIME_DIR)
    message(STATUS "No ${FOO_DSP_TARGET_ARCH} foobar2000 install found "
                   "- skipping the component smoke test "
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

    add_dependencies(component_smoke foo_dsp_decrackle)
    add_test(NAME component_smoke
             COMMAND component_smoke "$<TARGET_FILE:foo_dsp_decrackle>" "${FB2K_RUNTIME_DIR}")
    # The dialog part opens a real modal window; do not let it wedge CI.
    set_tests_properties(component_smoke PROPERTIES TIMEOUT 120)
    set_target_properties(component_smoke PROPERTIES FOLDER "tests")
endif()

set_target_properties(decrackle_verify PROPERTIES FOLDER "tests")
