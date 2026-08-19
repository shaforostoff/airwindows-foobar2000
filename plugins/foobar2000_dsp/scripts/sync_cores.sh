#!/bin/sh
#
# Keeps the portable DSP cores byte-identical across plug-in formats.
#
# A POSIX sh port of sync_cores.ps1, for editing cores from macOS or Linux where
# there is no PowerShell. Same canonical copy, same mirror list, same --check
# semantics and exit codes, so either script can front the other's CI gate.
#
# The reason it exists: the cores are now consumed by a macOS build as well, so
# they get edited on machines that cannot run the .ps1, and the mirrors then drift
# silently until the next Windows build - which is exactly what this script pair
# exists to prevent.
#
# ADDING A FORMAT means adding one line to $mirrors below AND to $mirrors in
# sync_cores.ps1. The list is the only thing duplicated between the two; keep
# them in step.
#
# Out-of-tree consumers - anything that vendors a core from outside this
# repository - are not mirrors and are not checked here. They have to be updated
# by hand when a core changes.
#
# Usage:
#   scripts/sync_cores.sh              push the canonical copies out to every mirror
#   scripts/sync_cores.sh --check      compare only; report and exit 1 on drift

set -eu

root=$(cd "$(dirname "$0")/.." && pwd)      # plugins/foobar2000_dsp
plugins=$(cd "$root/.." && pwd)             # plugins

check=0
case "${1:-}" in
    --check|-Check) check=1 ;;
    "") ;;
    *) echo "usage: $0 [--check]" >&2; exit 2 ;;
esac

# from_dir|file[,file...]|to_dir[,to_dir...]
mirrors="
foo_dsp_declick|declick_core.h,declick_core.cpp|WinVST/Declick
foo_dsp_dehum|dehum_core.h,dehum_core.cpp|WinVST/Dehum
"

drifted=0
copied=0

for entry in $mirrors; do
    from=$(printf '%s' "$entry" | cut -d'|' -f1)
    files=$(printf '%s' "$entry" | cut -d'|' -f2 | tr ',' ' ')
    tos=$(printf '%s' "$entry" | cut -d'|' -f3 | tr ',' ' ')

    for file in $files; do
        src="$root/$from/$file"

        if [ ! -f "$src" ]; then
            echo "canonical core missing: $src" >&2
            exit 1
        fi

        for to in $tos; do
            dst="$plugins/$to/$file"
            rel="$to/$file"

            if [ -f "$dst" ]; then
                if cmp -s "$src" "$dst"; then
                    echo "  same     $rel"
                    continue
                fi
                state="differs"
            else
                state="missing"
            fi

            if [ "$check" -eq 1 ]; then
                printf '  %-8s %s\n' "$state" "$rel"
                drifted=$((drifted + 1))
            else
                mkdir -p "$plugins/$to"
                cp -f "$src" "$dst"
                echo "  updated  $rel"
                copied=$((copied + 1))
            fi
        done
    done
done

if [ "$check" -eq 1 ]; then
    if [ "$drifted" -gt 0 ]; then
        echo
        echo "$drifted mirrored core file(s) do not match the canonical copy in" \
             "foobar2000_dsp. Run scripts/sync_cores.sh to update them, or move the" \
             "edit into the canonical copy first if that is where it belongs." >&2
        exit 1
    fi
    echo
    echo "every mirrored core matches"
    exit 0
fi

echo
echo "$copied file(s) updated"
exit 0
