#!/bin/sh
#
# Downloads and unpacks the VirtualDJ 8 plugin SDK into external/virtualdj_sdk.
#
# A POSIX sh port of get_sdk.ps1, for the macOS side of the build. Both are thin
# wrappers around cmake/vdj_download_sdk.cmake, which the top level
# CMakeLists.txt also calls on its own when the SDK is missing - so you normally
# never need to run this by hand.
#
# Nothing but CMake is required: its bundled libarchive reads the .zip. The
# archive is three header files and 7 kB; there is no library to link, because a
# VirtualDJ plug-in is a bundle that exports one C function and implements one
# abstract class, and the headers are the whole of the ABI.
#
# Usage:
#   scripts/get_sdk.sh [destination] [--force]

set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
dest="$root/external/virtualdj_sdk"
force=""

for arg in "$@"; do
    case "$arg" in
        --force) force="-DVDJ_SDK_FORCE=ON" ;;
        -*)      echo "usage: $0 [destination] [--force]" >&2; exit 2 ;;
        *)       dest="$arg" ;;
    esac
done

command -v cmake >/dev/null 2>&1 || {
    echo "cmake was not found on PATH. Install CMake 3.16 or newer." >&2
    exit 1
}

exec cmake "-DVDJ_SDK_DEST=$dest" ${force:+$force} -P "$root/cmake/vdj_download_sdk.cmake"
