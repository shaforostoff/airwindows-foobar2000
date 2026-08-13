# ---------------------------------------------------------------------------
# Development utilities. Not part of the shipped component.
# ---------------------------------------------------------------------------

add_executable(decrackle_cli
    decrackle_cli.cpp
    wav_io.h
    "${CMAKE_CURRENT_SOURCE_DIR}/../foo_dsp_decrackle/decrackle_core.cpp"
)

target_include_directories(decrackle_cli PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../foo_dsp_decrackle")

if(MSVC)
    target_compile_options(decrackle_cli PRIVATE /W4)
endif()

set_target_properties(decrackle_cli PROPERTIES FOLDER "tools")

add_executable(declick_cli
    declick_cli.cpp
    wav_io.h
    "${CMAKE_CURRENT_SOURCE_DIR}/../foo_dsp_declick/declick_core.cpp"
)

target_include_directories(declick_cli PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../foo_dsp_declick")

if(MSVC)
    target_compile_options(declick_cli PRIVATE /W4)
endif()

set_target_properties(declick_cli PROPERTIES FOLDER "tools")
