# ---------------------------------------------------------------------------
# vdj_download_sdk.cmake
#
# Downloads and unpacks the VirtualDJ 8 plugin SDK. Run it directly:
#
#   cmake -DVDJ_SDK_DEST=<dir> -P cmake/vdj_download_sdk.cmake
#
# The top level CMakeLists.txt invokes this same script automatically when the
# SDK is not already present, so there is nothing to run by hand in the normal
# case. This is deliberately the same shape as
# ../foobar2000_dsp/cmake/fb2k_download_sdk.cmake, and for the same reason: the
# SDK is somebody else's copyrighted material, so it is fetched rather than
# vendored, and the fetch has to be one command with a checksum on it.
#
# Unlike the foobar2000 SDK this is not a source distribution - the archive is
# 7 kB and holds three headers and nothing else. There is no library to link
# and no pfc to build: a VirtualDJ plug-in is a DLL (or a .bundle) that exports
# one C function and implements one abstract class, and the headers are the
# whole of the ABI.
#
# The archive is a .zip, which CMake's bundled libarchive reads, so no external
# tool is involved.
# ---------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.16)

set(VDJ_SDK_VERSION "20211003")
set(VDJ_SDK_URL     "https://virtualdj.com/developers/VirtualDJ8_SDK_${VDJ_SDK_VERSION}.zip")
# Verified against the file served by virtualdj.com. If Atomix reissue the
# archive under the same name this hash stops matching, which is the point: a
# silent change to the ABI headers is exactly the thing that should stop a
# build rather than be picked up unnoticed.
set(VDJ_SDK_SHA256  "77c23717509838f518d3176b088b97e8242ce357c2d78ce851d70386ae9f341e")

if(NOT DEFINED VDJ_SDK_DEST)
    message(FATAL_ERROR "VDJ_SDK_DEST is required, e.g. -DVDJ_SDK_DEST=external/virtualdj_sdk")
endif()

get_filename_component(_dest "${VDJ_SDK_DEST}" ABSOLUTE)
get_filename_component(_parent "${_dest}" DIRECTORY)
set(_archive "${_parent}/VirtualDJ8_SDK_${VDJ_SDK_VERSION}.zip")
set(_stamp   "${_dest}/.sdk-${VDJ_SDK_VERSION}.stamp")

if(EXISTS "${_stamp}" AND NOT VDJ_SDK_FORCE)
    message(STATUS "VirtualDJ SDK ${VDJ_SDK_VERSION} already unpacked in ${_dest}")
    return()
endif()

file(MAKE_DIRECTORY "${_parent}")

# --- fetch ----------------------------------------------------------------
set(_have_archive FALSE)
if(EXISTS "${_archive}")
    file(SHA256 "${_archive}" _got)
    if(_got STREQUAL VDJ_SDK_SHA256)
        set(_have_archive TRUE)
        message(STATUS "Reusing ${_archive}")
    else()
        message(STATUS "Discarding ${_archive} (checksum mismatch)")
        file(REMOVE "${_archive}")
    endif()
endif()

if(NOT _have_archive)
    message(STATUS "Downloading ${VDJ_SDK_URL}")
    file(DOWNLOAD "${VDJ_SDK_URL}" "${_archive}"
         EXPECTED_HASH SHA256=${VDJ_SDK_SHA256}
         TLS_VERIFY ON
         SHOW_PROGRESS
         STATUS _dl_status)
    list(GET _dl_status 0 _dl_code)
    if(NOT _dl_code EQUAL 0)
        list(GET _dl_status 1 _dl_msg)
        file(REMOVE "${_archive}")
        message(FATAL_ERROR
            "Failed to download the VirtualDJ SDK: ${_dl_msg}\n"
            "Fetch ${VDJ_SDK_URL} by hand, drop it in ${_parent}, and re-run.\n"
            "The download page is https://virtualdj.com/wiki/PluginSDK8.html")
    endif()
endif()

# --- unpack ---------------------------------------------------------------
# Extract to a scratch directory first so a half-finished unpack can never be
# mistaken for a usable SDK.
set(_tmp "${_parent}/.sdk-unpack")
file(REMOVE_RECURSE "${_tmp}")
file(MAKE_DIRECTORY "${_tmp}")

message(STATUS "Unpacking into ${_dest}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar xf "${_archive}"
    WORKING_DIRECTORY "${_tmp}"
    RESULT_VARIABLE _untar
    OUTPUT_VARIABLE _untar_out
    ERROR_VARIABLE  _untar_out)
if(NOT _untar EQUAL 0)
    file(REMOVE_RECURSE "${_tmp}")
    message(FATAL_ERROR "Could not unpack ${_archive}:\n${_untar_out}")
endif()

# The archive is flat: the three headers sit at its root.
foreach(_need vdjPlugin8.h vdjDsp8.h)
    if(NOT EXISTS "${_tmp}/${_need}")
        file(REMOVE_RECURSE "${_tmp}")
        message(FATAL_ERROR "Unexpected archive layout: ${_need} is missing")
    endif()
endforeach()

file(REMOVE_RECURSE "${_dest}")
file(RENAME "${_tmp}" "${_dest}")
file(WRITE "${_stamp}" "${VDJ_SDK_VERSION}\n${VDJ_SDK_URL}\n")

message(STATUS "VirtualDJ SDK ${VDJ_SDK_VERSION} ready in ${_dest}")
