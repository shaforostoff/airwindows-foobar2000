#!/bin/sh
#
# Copies the built plug-ins into VirtualDJ's plug-in folder on macOS.
#
# VirtualDJ loads plug-ins from a per-user folder, and which one depends on the
# version:
#
#     ~/Library/Application Support/VirtualDJ    2023 and later
#     ~/Documents/VirtualDJ                      8 through 2021
#
# Under it, plug-ins go in an architecture folder and then a category
# sub-folder:
#
#     Plugins64/SoundEffect      Intel
#     PluginsArm/SoundEffect     Apple Silicon
#
# build.sh produces one universal bundle, so it goes in both and the right half
# is used whichever way VirtualDJ was launched - including under Rosetta.
#
# All four plug-ins go in SoundEffect, the buffer ones included. That is the
# documented home for a DSP plug-in and the SDK gives no separate folder for a
# buffer one - VirtualDJ decides what a plug-in is by asking it for an interface
# by IID, not by where it sits (see common/vdj_entry.h). If a buffer plug-in
# does not appear in the effects list on your build, that is the first thing to
# try moving.
#
# Restart VirtualDJ afterwards: the folder is scanned at startup.
#
# Usage:
#   scripts/install.sh                 install into every VirtualDJ home found
#   scripts/install.sh --uninstall     remove them again
#   scripts/install.sh --home DIR      install into DIR instead of searching

set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
plugins=$(cd "$root/.." && pwd)
src="${OUTDIR:-$plugins/dist/vdj/mac}"

uninstall=0
forced=""

while [ $# -gt 0 ]; do
    case "$1" in
        --uninstall) uninstall=1 ;;
        --home)      shift; forced="${1:-}" ;;
        *) echo "usage: $0 [--uninstall] [--home DIR]" >&2; exit 2 ;;
    esac
    shift
done

homes=""
if [ -n "$forced" ]; then
    homes="$forced"
else
    for candidate in "$HOME/Library/Application Support/VirtualDJ" "$HOME/Documents/VirtualDJ"; do
        [ -d "$candidate" ] && homes="$homes
$candidate"
    done
fi

if [ -z "$(printf '%s' "$homes" | tr -d '[:space:]')" ]; then
    echo "No VirtualDJ home found. Looked in" >&2
    echo "  ~/Library/Application Support/VirtualDJ" >&2
    echo "  ~/Documents/VirtualDJ" >&2
    echo "Run VirtualDJ once so it creates its folder, or pass --home." >&2
    exit 1
fi

printf '%s\n' "$homes" | while IFS= read -r vdjhome; do
    [ -n "$vdjhome" ] || continue
    for arch in Plugins64 PluginsArm; do
        target="$vdjhome/$arch/SoundEffect"
        for name in Declick Dehum; do
            if [ "$uninstall" -eq 1 ]; then
                if [ -d "$target/$name.bundle" ]; then
                    rm -rf "$target/$name.bundle"
                    echo "  removed    $target/$name.bundle"
                fi
                continue
            fi
            [ -d "$src/$name.bundle" ] || continue
            mkdir -p "$target"
            rm -rf "$target/$name.bundle"
            cp -R "$src/$name.bundle" "$target/"
            echo "  installed  $target/$name.bundle"
        done
    done
done

if [ "$uninstall" -eq 0 ]; then
    echo
    echo "Restart VirtualDJ; the plug-in folder is scanned at startup."
    echo "They appear under Settings > Extensions > Effects as"
    echo "Declick and Dehum."
fi
