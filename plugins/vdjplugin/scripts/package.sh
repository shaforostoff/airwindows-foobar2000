#!/bin/sh
#
# Builds one macOS installer package that installs both VirtualDJ plug-ins.
#
#     plugins/dist/vdj/mac/Airwindows-VirtualDJ-<version>.pkg
#
# Double-clickable, and it puts Declick and Dehum where VirtualDJ will find
# them without the person installing having to know which of the four possible
# folders that is.
#
# Why a .pkg with a script rather than a payload that lands straight in
# Plugins64/SoundEffect: an installer payload is one fixed path, and there is no
# one fixed path here. The plug-in folder moved between VirtualDJ versions,
#
#     ~/Library/Application Support/VirtualDJ    2023 and later
#     ~/Documents/VirtualDJ                      8 through 2021
#
# and under whichever one is in use the same universal bundle has to appear in
# both Plugins64/SoundEffect and PluginsArm/SoundEffect, because VirtualDJ looks
# in one or the other depending on how it was launched - Rosetta included. So
# the payload is a staging copy under
#
#     ~/Library/Application Support/Airwindows/VirtualDJ/
#
# and a postinstall script fans it out into every VirtualDJ home that exists,
# which is the same decision install.sh makes and for the same reasons. The
# staging copy stays behind on purpose: it is what uninstall.sh, dropped in
# beside it, removes things with, and what a repair reinstall copies from.
#
# The package installs into the user's home, not into /Library. VirtualDJ reads
# plug-ins per user, so a system-wide install would be a payload nobody loads;
# the installer asks for no admin password for the same reason.
#
# Signing. --sign takes a "Developer ID Installer" identity, which productbuild
# signs the finished package with. Unsigned is fine for your own machine and
# for a build you hand someone with instructions; for anything downloaded, the
# package wants that identity and then notarising, the same argument
# ../AirwindowsVSTToSignedVSTProcess.txt makes for the VSTs. --codesign signs
# the bundles inside first, with codesign; pass "-" for ad-hoc.
#
# Usage:
#   scripts/package.sh                        build, verify, package
#   scripts/package.sh --skip-build           package whatever is in dist already
#   scripts/package.sh --skip-tests           forwarded to build.sh
#   scripts/package.sh --version 1.0.1        override the version in CMakeLists
#   scripts/package.sh --codesign -           ad-hoc sign the bundles first
#   scripts/package.sh --sign "Developer ID Installer: Some One (TEAMID)"
#   scripts/package.sh --out DIR              somewhere other than dist/vdj/mac
#
# Environment:
#   OUTDIR  where the built bundles are read from. plugins/dist/vdj/mac.
#   PKGOUT  where the .pkg is written. Same, unless --out says otherwise.

set -eu

case "$(uname -s)" in
    Darwin) ;;
    *) echo "package.sh builds a macOS .pkg and needs pkgbuild/productbuild." >&2
       echo "On Windows the equivalent is to zip plugins\\dist\\vdj\\x64 or to" >&2
       echo "point an installer builder at it; there is no script for that yet." >&2
       exit 1 ;;
esac

root=$(cd "$(dirname "$0")/.." && pwd)      # plugins/vdjplugin
plugins=$(cd "$root/.." && pwd)             # plugins
repo=$(cd "$plugins/.." && pwd)             # the tree
src="${OUTDIR:-$plugins/dist/vdj/mac}"
out="${PKGOUT:-$plugins/dist/vdj/mac}"
stage="$root/build/pkg"

identifier="com.airwindows.vdj.plugins"
skip_build=0
skip_tests=0
version=""
sign=""
codesign_id=""

while [ $# -gt 0 ]; do
    case "$1" in
        --skip-build) skip_build=1 ;;
        --skip-tests) skip_tests=1 ;;
        --version)    shift; version="${1:-}" ;;
        --sign)       shift; sign="${1:-}" ;;
        --codesign)   shift; codesign_id="${1:-}" ;;
        --out)        shift; out="${1:-}" ;;
        --identifier) shift; identifier="${1:-}" ;;
        -h|--help)
            sed -n '2,50p' "$0" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done

for tool in pkgbuild productbuild ditto; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "$tool was not found on PATH. Install the Xcode command line tools." >&2
        exit 1
    }
done

# The version the plug-ins were compiled with, so the receipt and the file name
# say the same thing the bundles do. project() rather than the first VERSION in
# the file, which belongs to cmake_minimum_required.
if [ -z "$version" ]; then
    version=$(awk '/project\(airwindows_virtualdj/,/LANGUAGES/' "$root/CMakeLists.txt" |
              sed -n 's/^[[:space:]]*VERSION[[:space:]]*\([0-9][0-9.]*\).*/\1/p' | head -1)
fi
[ -n "$version" ] || { echo "could not work out a version; pass --version" >&2; exit 1; }

if [ "$skip_build" -eq 0 ]; then
    if [ "$skip_tests" -eq 1 ]; then
        OUTDIR="$src" "$root/scripts/build.sh" --skip-tests
    else
        OUTDIR="$src" "$root/scripts/build.sh"
    fi
fi

echo "=== collect ==="
for name in Declick Dehum; do
    bundle="$src/$name.bundle"
    [ -x "$bundle/Contents/MacOS/$name" ] || {
        echo "missing $bundle/Contents/MacOS/$name" >&2
        echo "run scripts/build.sh first, or drop --skip-build." >&2
        exit 1
    }
    archs=$(lipo -archs "$bundle/Contents/MacOS/$name" 2>/dev/null || echo '?')
    printf '  %-16s %s\n' "$name.bundle" "$archs"
    case "$archs" in
        *x86_64*arm64*|*arm64*x86_64*) ;;
        *) echo "  note: not universal - VirtualDJ loads Plugins64 under Rosetta" >&2
           echo "        and PluginsArm natively, and this package installs the" >&2
           echo "        same bundle in both." >&2 ;;
    esac
done

# One clean tree per run: a stale bundle left in the payload from a previous
# build would be packaged without anything noticing.
rm -rf "$stage"
mkdir -p "$stage/root" "$stage/scripts" "$stage/resources" "$stage/pkgs"

for name in Declick Dehum; do
    ditto "$src/$name.bundle" "$stage/root/$name.bundle"
done

if [ -n "$codesign_id" ]; then
    echo "=== codesign ==="
    for name in Declick Dehum; do
        codesign --force --timestamp --options runtime \
                 --sign "$codesign_id" "$stage/root/$name.bundle" 2>/dev/null ||
        codesign --force --sign "$codesign_id" "$stage/root/$name.bundle"
        printf '  %-16s %s\n' "$name.bundle" "$codesign_id"
    done
fi

# --- what ships beside the plug-ins ----------------------------------------

cat > "$stage/root/uninstall.sh" <<'UNINSTALL'
#!/bin/sh
#
# Removes Declick and Dehum from every VirtualDJ plug-in folder, then removes
# this staging copy and the installer receipt. Run it from the Terminal:
#
#     ~/Library/Application\ Support/Airwindows/VirtualDJ/uninstall.sh
#
# Restart VirtualDJ afterwards; the plug-in folder is scanned at startup.

set -eu

home="${HOME:?}"
staged="$home/Library/Application Support/Airwindows/VirtualDJ"

for vdjhome in "$home/Library/Application Support/VirtualDJ" \
               "$home/Documents/VirtualDJ"; do
    [ -d "$vdjhome" ] || continue
    for arch in Plugins64 PluginsArm; do
        for name in Declick Dehum; do
            bundle="$vdjhome/$arch/SoundEffect/$name.bundle"
            if [ -d "$bundle" ]; then
                rm -rf "$bundle"
                echo "  removed  $bundle"
            fi
        done
    done
done

pkgutil --forget com.airwindows.vdj.plugins >/dev/null 2>&1 || true
rm -rf "$staged"
echo "  removed  $staged"
UNINSTALL
chmod 755 "$stage/root/uninstall.sh"

# --- the postinstall fan-out ------------------------------------------------
#
# $2 is the install destination, which for a home directory install is the home
# itself. It is the only thing here that knows whose machine this is: the script
# may run as root, so $HOME is not to be trusted, and the console user is the
# fallback rather than the first choice because a fast user switch would make it
# the wrong answer.

cat > "$stage/scripts/postinstall" <<'POSTINSTALL'
#!/bin/sh
set -eu

home="${2:-}"
case "$home" in
    ""|"/")
        user=$(stat -f '%Su' /dev/console 2>/dev/null || echo "")
        [ -n "$user" ] && home=$(dscl . -read "/Users/$user" NFSHomeDirectory 2>/dev/null |
                                 awk '{print $2}')
        [ -n "${home:-}" ] || home="${HOME:-}"
        ;;
esac
[ -n "$home" ] && [ -d "$home" ] || {
    echo "postinstall: no home directory to install into" >&2
    exit 1
}

staged="$home/Library/Application Support/Airwindows/VirtualDJ"
owner=$(stat -f '%Su:%Sg' "$home" 2>/dev/null || echo "")

# Both locations, because both can exist - someone who has run VirtualDJ 2021
# and VirtualDJ 2025 on the same account has two of these, and which one is
# live depends on which VirtualDJ they open.
homes=""
for candidate in "$home/Library/Application Support/VirtualDJ" \
                 "$home/Documents/VirtualDJ"; do
    [ -d "$candidate" ] && homes="$homes$candidate
"
done

# Nothing found means VirtualDJ has not been run on this account yet. Create
# the modern location rather than failing: the folder is scanned at startup, so
# a first run after this picks the plug-ins up.
[ -n "$homes" ] || homes="$home/Library/Application Support/VirtualDJ
"

printf '%s' "$homes" | while IFS= read -r vdjhome; do
    [ -n "$vdjhome" ] || continue
    for arch in Plugins64 PluginsArm; do
        target="$vdjhome/$arch/SoundEffect"
        mkdir -p "$target"
        for name in Declick Dehum; do
            [ -d "$staged/$name.bundle" ] || continue
            rm -rf "$target/$name.bundle"
            /usr/bin/ditto "$staged/$name.bundle" "$target/$name.bundle"
            echo "installed $target/$name.bundle"
        done
    done
    [ -n "$owner" ] && chown -R "$owner" "$vdjhome" 2>/dev/null || true
done

[ -n "$owner" ] && chown -R "$owner" "$home/Library/Application Support/Airwindows" 2>/dev/null || true
exit 0
POSTINSTALL
chmod 755 "$stage/scripts/postinstall"

# --- installer text ---------------------------------------------------------

cat > "$stage/resources/welcome.txt" <<WELCOME
Declick and Dehum for VirtualDJ $version

Two ShellacFilters DSP plug-ins for VirtualDJ:

Declick detect-and-interpolate declicker for records and worn digital
Dehum listens to the opening of the record to find the hum before the record starts playing

Neither delays the deck. Both read ahead in the song file instead, which is what a VirtualDJ buffer plug-in can do and a plain effect cannot - a delay on a deck moves it off everything it is being mixed against.

This installs into your home folder, not the system, because VirtualDJ reads plug-ins per user. You will not be asked for an administrator password.
WELCOME

cat > "$stage/resources/conclusion.txt" <<'CONCLUSION'
Installed.

Restart VirtualDJ - the plug-in folder is scanned at startup - and both appear
under Settings > Extensions > Effects, as Declick and Dehum.

To remove them again, run this from the Terminal:

  ~/Library/Application\ Support/Airwindows/VirtualDJ/uninstall.sh
CONCLUSION

[ -f "$repo/LICENSE" ] && cp "$repo/LICENSE" "$stage/resources/LICENSE.txt"

# --- build the package ------------------------------------------------------

echo "=== pkgbuild ==="
pkgbuild --root "$stage/root" \
         --scripts "$stage/scripts" \
         --identifier "$identifier" \
         --version "$version" \
         --install-location "Library/Application Support/Airwindows/VirtualDJ" \
         "$stage/pkgs/component.pkg" >/dev/null

# customize="never" because there is one thing in here and nothing to choose
# between; require-scripts because without the postinstall the payload is a
# staging folder nothing reads. The domains line is what makes this a home
# directory install - no admin password, and $2 in the postinstall is the home.
license_line=""
[ -f "$stage/resources/LICENSE.txt" ] &&
    license_line='<license file="LICENSE.txt" mime-type="text/plain"/>'

cat > "$stage/distribution.xml" <<DISTRIBUTION
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>Declick and Dehum for VirtualDJ</title>
    <organization>com.airwindows</organization>
    <options customize="never" require-scripts="true" hostArchitectures="x86_64,arm64"/>
    <domains enable_anywhere="false" enable_currentUserHome="true" enable_localSystem="false"/>
    <welcome file="welcome.txt" mime-type="text/plain"/>
    <conclusion file="conclusion.txt" mime-type="text/plain"/>
    $license_line
    <choices-outline>
        <line choice="default">
            <line choice="$identifier"/>
        </line>
    </choices-outline>
    <choice id="default"/>
    <choice id="$identifier" visible="false">
        <pkg-ref id="$identifier"/>
    </choice>
    <pkg-ref id="$identifier" version="$version" onConclusion="none">component.pkg</pkg-ref>
</installer-gui-script>
DISTRIBUTION

pkg="$out/Airwindows-VirtualDJ-$version.pkg"
mkdir -p "$out"
rm -f "$pkg"

echo "=== productbuild ==="
if [ -n "$sign" ]; then
    productbuild --distribution "$stage/distribution.xml" \
                 --package-path "$stage/pkgs" \
                 --resources "$stage/resources" \
                 --sign "$sign" \
                 "$pkg" >/dev/null
else
    productbuild --distribution "$stage/distribution.xml" \
                 --package-path "$stage/pkgs" \
                 --resources "$stage/resources" \
                 "$pkg" >/dev/null
fi

echo "=== verify ==="
# Both plug-ins in the payload, or the package installs half of itself.
payload=$(pkgutil --payload-files "$pkg" 2>/dev/null || echo "")
for name in Declick Dehum; do
    printf '%s\n' "$payload" | grep -q "$name.bundle/Contents/MacOS/$name" || {
        echo "$name is not in the package payload" >&2
        exit 1
    }
done
if [ -n "$sign" ]; then
    pkgutil --check-signature "$pkg" | sed 's/^/  /'
else
    echo "  unsigned - fine for this machine; Gatekeeper will complain about a"
    echo "  copy that has been downloaded. See --sign."
fi

printf '  %-38s %s\n' "$(basename "$pkg")" "$(du -h "$pkg" | awk '{print $1}')"
shasum -a 256 "$pkg" | sed 's/^/  /'

echo
echo "installer at $pkg"
echo
echo "Install it by double-clicking, or:"
echo "  installer -pkg \"$pkg\" -target CurrentUserHomeDirectory"
