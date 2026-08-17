# ---------------------------------------------------------------------------
# Tests
#
#   decrackle_verify   - DSP core vs. a literal transcription of the Airwindows
#                        VST source, plus robustness and throughput. No SDK.
#   declick_verify     - the declicker's AR linear algebra vs. dense brute
#                        force, plus streaming properties. No SDK.
#   declick_rt_verify  - counts heap allocations on the processing path and
#                        requires zero. No SDK.
#   declick_vst_verify - the WinVST port of the declicker vs. the core it
#                        shares with this one. Built against tests/vst2_stub,
#                        not Steinberg's SDK. No SDK.
#   dehum_verify       - the dehummer's FFT, notch, frequency tracker and
#                        detector against independent references. No SDK.
#   dehum_rt_verify    - same allocation count for the dehummer, whose detector
#                        runs on the audio thread. No SDK.
#   dehum_vst_verify   - the WinVST port of the dehummer vs. the core it shares.
#                        No SDK.
#   preset_roundtrip   - every parameter survives save/load. Links the SDK, so
#                        it needs foobar2000's shared.dll; skips if absent.
#   component_smoke    - loads the built DLL through foobar2000's own service
#                        plumbing. Needs foobar2000 installed; skips if absent.
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
# The declicker's linear algebra, against dense brute force. No SDK.

add_executable(declick_verify
    declick_verify.cpp
    "${CMAKE_CURRENT_SOURCE_DIR}/../foo_dsp_declick/declick_core.cpp"
)

target_include_directories(declick_verify PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../foo_dsp_declick")

if(MSVC)
    target_compile_options(declick_verify PRIVATE /W4)
endif()

add_test(NAME declick_verify COMMAND declick_verify)
set_target_properties(declick_verify PROPERTIES FOLDER "tests")

# ---------------------------------------------------------------------------
# The audio thread must not touch the heap. Replaces global operator new and
# requires the count to be zero across the processing path and every parameter
# change. No SDK.

add_executable(declick_rt_verify
    declick_rt_verify.cpp
    "${CMAKE_CURRENT_SOURCE_DIR}/../foo_dsp_declick/declick_core.cpp"
)

target_include_directories(declick_rt_verify PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../foo_dsp_declick")

if(MSVC)
    target_compile_options(declick_rt_verify PRIVATE /W4)
endif()

add_test(NAME declick_rt_verify COMMAND declick_rt_verify)
set_target_properties(declick_rt_verify PROPERTIES FOLDER "tests")

# ---------------------------------------------------------------------------
# The dehummer: the hand-rolled FFT, the notch depth against its analytic floor,
# the frequency tracker against tones of known frequency, and the detector
# against material with and without a line in it. No SDK.

add_executable(dehum_verify
    dehum_verify.cpp
    "${CMAKE_CURRENT_SOURCE_DIR}/../foo_dsp_dehum/dehum_core.cpp"
)

target_include_directories(dehum_verify PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../foo_dsp_dehum")

if(MSVC)
    target_compile_options(dehum_verify PRIVATE /W4)
endif()

add_test(NAME dehum_verify COMMAND dehum_verify)
set_target_properties(dehum_verify PROPERTIES FOLDER "tests")

# ---------------------------------------------------------------------------
# Dehum's audio thread must not touch the heap either - and it has more to get
# wrong, since a whole FFT detector runs on it. No SDK.

add_executable(dehum_rt_verify
    dehum_rt_verify.cpp
    "${CMAKE_CURRENT_SOURCE_DIR}/../foo_dsp_dehum/dehum_core.cpp"
)

target_include_directories(dehum_rt_verify PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../foo_dsp_dehum")

if(MSVC)
    target_compile_options(dehum_rt_verify PRIVATE /W4)
endif()

add_test(NAME dehum_rt_verify COMMAND dehum_rt_verify)
set_target_properties(dehum_rt_verify PROPERTIES FOLDER "tests")

# ---------------------------------------------------------------------------
# The WinVST port of the declicker, against the core it shares with this tree.
#
# It compiles the WinVST folder's OWN copy of declick_core.cpp, deliberately:
# that is the file the VST project builds, so this is testing the artefact that
# ships rather than a stand-in for it. Whether that copy is still byte-identical
# to the canonical one is a different question, and the answer is
# scripts/sync_cores.ps1 -Check, which build_release.ps1 runs first.
#
# vst2_stub must come first on the include path: that is where the
# "audioeffectx.h" the plug-in asks for is found. Steinberg's real one is not in
# this repository, so this test cannot establish that the plug-in compiles in
# the real VSTProject - only that the DSP, the latency contract, the parameter
# mapping and the preset chunk behave. See tests/vst2_stub/audioeffectx.h.

set(WINVST_DECLICK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../WinVST/Declick")

if(EXISTS "${WINVST_DECLICK_DIR}/Declick.cpp")
    add_executable(declick_vst_verify
        declick_vst_verify.cpp
        vst2_stub/audioeffectx.h
        "${WINVST_DECLICK_DIR}/Declick.cpp"
        "${WINVST_DECLICK_DIR}/DeclickProc.cpp"
        "${WINVST_DECLICK_DIR}/declick_core.cpp"
    )

    target_include_directories(declick_vst_verify PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/vst2_stub"
        "${WINVST_DECLICK_DIR}")

    if(MSVC)
        target_compile_options(declick_vst_verify PRIVATE /W4)
        # The wrapper follows the Airwindows house pattern - `A = 0.5;` on a
        # float, `*out1 = inputSampleL;` from a double, and setChunk() ignoring
        # byteSize on purpose - so it warns exactly where the other 519 plug-ins
        # do. Those are not ours to fix either.
        set_source_files_properties(
            "${WINVST_DECLICK_DIR}/Declick.cpp"
            "${WINVST_DECLICK_DIR}/DeclickProc.cpp"
            PROPERTIES COMPILE_OPTIONS "/wd4244;/wd4305;/wd4100")
    endif()

    add_test(NAME declick_vst_verify COMMAND declick_vst_verify)
    set_target_properties(declick_vst_verify PROPERTIES FOLDER "tests")
else()
    message(STATUS "plugins/WinVST/Declick not present - skipping declick_vst_verify")
endif()

# ---------------------------------------------------------------------------
# The WinVST port of the dehummer, against the core it shares with this tree.
# Same arrangement as declick_vst_verify above: it compiles the WinVST folder's
# own copy of dehum_core.cpp, because that is the file the VST project builds.
# Whether that copy is still byte-identical is scripts/sync_cores.ps1 -Check.

set(WINVST_DEHUM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../WinVST/Dehum")

if(EXISTS "${WINVST_DEHUM_DIR}/Dehum.cpp")
    add_executable(dehum_vst_verify
        dehum_vst_verify.cpp
        vst2_stub/audioeffectx.h
        "${WINVST_DEHUM_DIR}/Dehum.cpp"
        "${WINVST_DEHUM_DIR}/DehumProc.cpp"
        "${WINVST_DEHUM_DIR}/dehum_core.cpp"
    )

    target_include_directories(dehum_vst_verify PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/vst2_stub"
        "${WINVST_DEHUM_DIR}")

    if(MSVC)
        target_compile_options(dehum_vst_verify PRIVATE /W4)
        # The wrapper follows the Airwindows house pattern - `A = 0.5;` on a
        # float, `out1[i] = inputSampleL;` from a double, and setChunk()
        # ignoring byteSize on purpose - so it warns exactly where the other 519
        # plug-ins do. Those are not ours to fix.
        set_source_files_properties(
            "${WINVST_DEHUM_DIR}/Dehum.cpp"
            "${WINVST_DEHUM_DIR}/DehumProc.cpp"
            PROPERTIES COMPILE_OPTIONS "/wd4244;/wd4305;/wd4100")
    endif()

    add_test(NAME dehum_vst_verify COMMAND dehum_vst_verify)
    set_target_properties(dehum_vst_verify PROPERTIES FOLDER "tests")
else()
    message(STATUS "plugins/WinVST/Dehum not present - skipping dehum_vst_verify")
endif()

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
    "${CMAKE_CURRENT_SOURCE_DIR}/../foo_dsp_dehum/dehum_preset.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../foo_dsp_dehum/dehum_core.cpp"
)

target_include_directories(preset_roundtrip PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../foo_dsp_declick"
    "${CMAKE_CURRENT_SOURCE_DIR}/../foo_dsp_decrackle"
    "${CMAKE_CURRENT_SOURCE_DIR}/../foo_dsp_dehum")

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

    add_dependencies(component_smoke foo_dsp_decrackle foo_dsp_declick foo_dsp_dehum)

    add_test(NAME smoke_decrackle
             COMMAND component_smoke "$<TARGET_FILE:foo_dsp_decrackle>" "${FB2K_RUNTIME_DIR}"
                     --dialog 101 --slider 1001 --label 1011)
    add_test(NAME smoke_declick
             COMMAND component_smoke "$<TARGET_FILE:foo_dsp_declick>" "${FB2K_RUNTIME_DIR}"
                     --dialog 201 --slider 2001 --label 2011)
    add_test(NAME smoke_dehum
             COMMAND component_smoke "$<TARGET_FILE:foo_dsp_dehum>" "${FB2K_RUNTIME_DIR}"
                     --dialog 301 --slider 3001 --label 3011)
    # These open a real modal window; do not let them wedge CI.
    set_tests_properties(smoke_decrackle smoke_declick smoke_dehum
                         PROPERTIES TIMEOUT 120)
    set_target_properties(component_smoke PROPERTIES FOLDER "tests")
endif()

set_target_properties(decrackle_verify PROPERTIES FOLDER "tests")
