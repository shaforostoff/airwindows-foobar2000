# ---------------------------------------------------------------------------
# vdj_plugin.cmake
#
# add_vdj_plugin(<target> SOURCES <files...> [PLUGIN_NAME <name>])
#
# One VirtualDJ plug-in. There is nothing to link: the SDK is three headers,
# and what makes a DLL a VirtualDJ plug-in is that it exports
#
#     HRESULT VDJ_API DllGetClassObject(const GUID&, const GUID&, void**)
#
# and hands back an object implementing the interface whose IID was asked for.
# So this is a MODULE library plus the two platform details that a loadable
# module for VirtualDJ has that other modules do not:
#
#   - Windows 32 bit needs a .def file. VDJ_API is __stdcall there, and
#     extern "C" __stdcall exports get decorated as _Name@12, which is not the
#     name VirtualDJ looks up. On x64 and ARM64 there is no decoration and the
#     .def is inert, so it is applied unconditionally rather than guarded -
#     one fewer thing to be conditionally wrong.
#
#   - macOS wants a .bundle, not a .dylib, because vdjPlugin8.h types
#     VDJ_HINSTANCE as CFBundleRef and VirtualDJ loads plug-ins through
#     CFBundle. BUNDLE TRUE plus BUNDLE_EXTENSION gets CMake to lay out
#     Foo.bundle/Contents/MacOS/Foo with a generated Info.plist.
#
# Everything lands in ${CMAKE_BINARY_DIR}/plugins so that the build and install
# scripts have one directory to look in whatever the generator does with
# per-config subdirectories.
# ---------------------------------------------------------------------------

function(add_vdj_plugin target)
    cmake_parse_arguments(ARG "" "PLUGIN_NAME" "SOURCES" ${ARGN})
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "add_vdj_plugin(${target}): SOURCES is required")
    endif()

    add_library(${target} MODULE ${ARG_SOURCES})

    target_include_directories(${target} PRIVATE
        "${VDJ_SDK_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${PROJECT_SOURCE_DIR}/common")

    set_target_properties(${target} PROPERTIES
        PREFIX ""                       # no "lib" on the .bundle
        # Per-config subdirectories would put Release and Debug builds of the
        # same plug-in in different places, which every script downstream would
        # then have to know about. One directory.
        LIBRARY_OUTPUT_DIRECTORY                    "${CMAKE_BINARY_DIR}/plugins"
        LIBRARY_OUTPUT_DIRECTORY_DEBUG              "${CMAKE_BINARY_DIR}/plugins"
        LIBRARY_OUTPUT_DIRECTORY_RELEASE            "${CMAKE_BINARY_DIR}/plugins"
        LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO     "${CMAKE_BINARY_DIR}/plugins"
        LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL         "${CMAKE_BINARY_DIR}/plugins"
        RUNTIME_OUTPUT_DIRECTORY                    "${CMAKE_BINARY_DIR}/plugins"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG              "${CMAKE_BINARY_DIR}/plugins"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE            "${CMAKE_BINARY_DIR}/plugins"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO     "${CMAKE_BINARY_DIR}/plugins"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL         "${CMAKE_BINARY_DIR}/plugins"
        PDB_OUTPUT_DIRECTORY                        "${CMAKE_BINARY_DIR}/symbols"
        FOLDER "plug-ins")

    if(WIN32)
        target_sources(${target} PRIVATE "${PROJECT_SOURCE_DIR}/common/vdjplugin.def")
    elseif(APPLE)
        set_target_properties(${target} PROPERTIES
            BUNDLE TRUE
            BUNDLE_EXTENSION "bundle"
            MACOSX_BUNDLE_BUNDLE_NAME       "${target}"
            MACOSX_BUNDLE_GUI_IDENTIFIER    "com.shellacfilters.vdj.${target}"
            MACOSX_BUNDLE_BUNDLE_VERSION    "${PROJECT_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
        # vdjPlugin8.h pulls in CoreFoundation on this platform.
        target_link_libraries(${target} PRIVATE "-framework CoreFoundation")
    endif()
endfunction()
