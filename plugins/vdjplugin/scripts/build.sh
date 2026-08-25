#!/bin/sh
#
# Builds the VirtualDJ plug-ins on macOS.
#
# Produces, as universal (x86_64 + arm64) bundles:
#
#     plugins/dist/vdj/mac/Declick.bundle          live effect, 784 samples late
#     plugins/dist/vdj/mac/DeclickBuffer.bundle    deck effect, reads ahead, no delay
#     plugins/dist/vdj/mac/Dehum.bundle            live effect, no delay
#     plugins/dist/vdj/mac/DehumBuffer.bundle      deck effect, scouts the record
#
# Which of each pair to use is not a matter of taste; see README.md.
#
# One universal bundle rather than two thin ones because VirtualDJ keeps Intel
# plug-ins in Plugins64 and Apple Silicon ones in PluginsArm, and a universal
# bundle is loadable from either - so there is one thing to build, one thing to
# test, and install.sh can drop the same artefact in both.
#
# Unless --skip-tests is given, declick_vdj_verify and dehum_vdj_verify are
# built and run. What they check is the part of this project that is not shared
# with any other port - the slider mappings and BufferPipeline - and in
# particular that the audio the buffer plug-ins hand back is what the core
# produces running straight through the song, to the bit, at any block size.
# See tests/vdj_test_support.h.
#
# The VirtualDJ SDK is fetched on the first configure; nothing needs installing
# but CMake and the Xcode command line tools.
#
# Usage:
#   scripts/build.sh                  build, verify, package
#   scripts/build.sh --skip-tests     build only; not recommended
#   scripts/build.sh --clean          wipe the build tree first
#   scripts/build.sh --sign           ad-hoc codesign the bundles afterwards
#   scripts/build.sh --install        hand off to install.sh when done
#
# Environment:
#   ARCHS   override CMAKE_OSX_ARCHITECTURES, e.g. ARCHS=arm64 for a thin build
#   OUTDIR  where the bundles go. plugins/dist/vdj/mac by default.

set -eu

root=$(cd "$(dirname "$0")/.." && pwd)      # plugins/vdjplugin
plugins=$(cd "$root/.." && pwd)             # plugins
build="$root/build/mac"
out="${OUTDIR:-$plugins/dist/vdj/mac}"

skip_tests=0
clean=0
sign=0
install=0

for arg in "$@"; do
    case "$arg" in
        --skip-tests) skip_tests=1 ;;
        --clean)      clean=1 ;;
        --sign)       sign=1 ;;
        --install)    install=1 ;;
        -h|--help)
            sed -n '2,40p' "$0" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

command -v cmake >/dev/null 2>&1 || {
    echo "cmake was not found on PATH. Install CMake 3.16 or newer." >&2
    exit 1
}

[ "$clean" -eq 1 ] && rm -rf "$build"

cfg="-DCMAKE_BUILD_TYPE=Release"
[ "$skip_tests" -eq 1 ] && cfg="$cfg -DVDJ_BUILD_TESTS=OFF"
[ -n "${ARCHS:-}" ] && cfg="$cfg -DCMAKE_OSX_ARCHITECTURES=${ARCHS}"

echo "=== configure ==="
# shellcheck disable=SC2086
cmake -S "$root" -B "$build" $cfg

echo "=== build ==="
cmake --build "$build" --parallel

if [ "$skip_tests" -eq 0 ]; then
    echo "=== verify ==="
    ctest --test-dir "$build" --output-on-failure
fi

echo "=== package ==="
mkdir -p "$out"
for name in Declick DeclickBuffer Dehum DehumBuffer; do
    bundle="$build/plugins/$name.bundle"
    [ -d "$bundle" ] || { echo "expected $bundle" >&2; exit 1; }
    rm -rf "$out/$name.bundle"
    cp -R "$bundle" "$out/"
    if [ "$sign" -eq 1 ]; then
        # Ad-hoc. Enough to satisfy a host that insists a loadable bundle be
        # signed at all; it establishes nothing about who built it, and a
        # bundle meant for anyone else's machine needs a real identity and
        # notarisation - the same argument as
        # ../AirwindowsVSTToSignedVSTProcess.txt makes for the VSTs.
        codesign --force --sign - "$out/$name.bundle"
    fi
    printf '  %-22s %s\n' "$name.bundle" "$(lipo -archs "$out/$name.bundle/Contents/MacOS/$name" 2>/dev/null || echo '?')"
done

echo
echo "plug-ins in $out"

[ "$install" -eq 1 ] && exec "$root/scripts/install.sh"
exit 0
