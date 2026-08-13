# ---------------------------------------------------------------------------
# fb2k_download_sdk.cmake
#
# Downloads and unpacks the foobar2000 SDK. Run it directly:
#
#   cmake -DFB2K_SDK_DEST=<dir> -P cmake/fb2k_download_sdk.cmake
#
# The top level CMakeLists.txt invokes this same script automatically when the
# SDK is not already present, so there is nothing to run by hand in the normal
# case.
#
# No external tools are needed: CMake bundles libarchive, which reads .7z.
# ---------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.16)

set(FB2K_SDK_VERSION "2025-03-07")
set(FB2K_SDK_URL     "https://www.foobar2000.org/downloads/SDK-${FB2K_SDK_VERSION}.7z")
set(FB2K_SDK_SHA256  "ccda3c5840e66e0e28a7e4fe36407c4e78581aa30c40c362a188fcbaae799a3e")

if(NOT DEFINED FB2K_SDK_DEST)
    message(FATAL_ERROR "FB2K_SDK_DEST is required, e.g. -DFB2K_SDK_DEST=external/foobar2000_sdk")
endif()

get_filename_component(_dest "${FB2K_SDK_DEST}" ABSOLUTE)
get_filename_component(_parent "${_dest}" DIRECTORY)
set(_archive "${_parent}/SDK-${FB2K_SDK_VERSION}.7z")
set(_stamp   "${_dest}/.sdk-${FB2K_SDK_VERSION}.stamp")

if(EXISTS "${_stamp}" AND NOT FB2K_SDK_FORCE)
    message(STATUS "foobar2000 SDK ${FB2K_SDK_VERSION} already unpacked in ${_dest}")
    return()
endif()

file(MAKE_DIRECTORY "${_parent}")

# --- fetch ----------------------------------------------------------------
set(_have_archive FALSE)
if(EXISTS "${_archive}")
    file(SHA256 "${_archive}" _got)
    if(_got STREQUAL FB2K_SDK_SHA256)
        set(_have_archive TRUE)
        message(STATUS "Reusing ${_archive}")
    else()
        message(STATUS "Discarding ${_archive} (checksum mismatch)")
        file(REMOVE "${_archive}")
    endif()
endif()

if(NOT _have_archive)
    message(STATUS "Downloading ${FB2K_SDK_URL}")
    file(DOWNLOAD "${FB2K_SDK_URL}" "${_archive}"
         EXPECTED_HASH SHA256=${FB2K_SDK_SHA256}
         TLS_VERIFY ON
         SHOW_PROGRESS
         STATUS _dl_status)
    list(GET _dl_status 0 _dl_code)
    if(NOT _dl_code EQUAL 0)
        list(GET _dl_status 1 _dl_msg)
        file(REMOVE "${_archive}")
        message(FATAL_ERROR
            "Failed to download the foobar2000 SDK: ${_dl_msg}\n"
            "Fetch ${FB2K_SDK_URL} by hand, drop it in ${_parent}, and re-run.")
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

foreach(_need foobar2000/SDK pfc)
    if(NOT EXISTS "${_tmp}/${_need}")
        file(REMOVE_RECURSE "${_tmp}")
        message(FATAL_ERROR "Unexpected archive layout: ${_need} is missing")
    endif()
endforeach()

file(REMOVE_RECURSE "${_dest}")
file(RENAME "${_tmp}" "${_dest}")
file(WRITE "${_stamp}" "${FB2K_SDK_VERSION}\n${FB2K_SDK_URL}\n")

message(STATUS "foobar2000 SDK ${FB2K_SDK_VERSION} ready in ${_dest}")
