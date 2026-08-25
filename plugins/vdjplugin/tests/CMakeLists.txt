# Verification harnesses. See vdj_test_support.h for what they do and do not
# claim to cover.
#
# declick_vdj_verify and dehum_vdj_verify link the wrappers directly, so they
# run with no VirtualDJ installed and no plug-in built. vdj_host_verify loads
# the finished modules through the C ABI, so it needs them; it still does not
# need VirtualDJ.

function(add_vdj_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;ARGS" ${ARGN})
    add_executable(${name} ${ARG_SOURCES})
    target_include_directories(${name} PRIVATE
        "${VDJ_SDK_DIR}"
        "${PROJECT_SOURCE_DIR}/common"
        "${CMAKE_CURRENT_SOURCE_DIR}")
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY                "${CMAKE_BINARY_DIR}/tests"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG          "${CMAKE_BINARY_DIR}/tests"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE        "${CMAKE_BINARY_DIR}/tests"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/tests"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL     "${CMAKE_BINARY_DIR}/tests"
        FOLDER "tests")
    add_test(NAME ${name} COMMAND ${name} ${ARG_ARGS})
endfunction()

add_vdj_test(declick_vdj_verify
    SOURCES declick_vdj_verify.cpp
            "${PROJECT_SOURCE_DIR}/vdj_declick/declick_core.cpp")
target_include_directories(declick_vdj_verify PRIVATE
    "${PROJECT_SOURCE_DIR}/vdj_declick")

add_vdj_test(dehum_vdj_verify
    SOURCES dehum_vdj_verify.cpp
            "${PROJECT_SOURCE_DIR}/vdj_dehum/dehum_core.cpp")
target_include_directories(dehum_vdj_verify PRIVATE
    "${PROJECT_SOURCE_DIR}/vdj_dehum")

# The ABI test. It links both cores, because what it compares each loaded module
# against is the same wrapper linked in. The plug-in directory is a plain path
# rather than a generator expression because add_vdj_plugin() pins every
# configuration's output to it - see cmake/vdj_plugin.cmake.
add_vdj_test(vdj_host_verify
    SOURCES vdj_host_verify.cpp
            "${PROJECT_SOURCE_DIR}/vdj_declick/declick_core.cpp"
            "${PROJECT_SOURCE_DIR}/vdj_dehum/dehum_core.cpp"
    ARGS    "${CMAKE_BINARY_DIR}/plugins")
target_include_directories(vdj_host_verify PRIVATE
    "${PROJECT_SOURCE_DIR}/vdj_declick"
    "${PROJECT_SOURCE_DIR}/vdj_dehum")
add_dependencies(vdj_host_verify Declick DeclickBuffer Dehum DehumBuffer)
if(UNIX)
    target_link_libraries(vdj_host_verify PRIVATE ${CMAKE_DL_LIBS})
endif()
