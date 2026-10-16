#!/bin/bash
# Builds the macOS installer (.pkg) from the plugin build artefacts. Runs on macOS only.
#
#   installer/macos/build_pkg.sh <cmake build dir> <output dir>
#
# The package offers three choices (all ticked by default):
#   VST3         -> /Library/Audio/Plug-Ins/VST3/Ottoclone.vst3
#   Audio Unit   -> /Library/Audio/Plug-Ins/Components/Ottoclone.component
#   Standalone   -> /Applications/Ottoclone.app
#
# Without any credentials the bundles are ad-hoc signed and the .pkg is unsigned, so macOS will
# show an "unidentified developer" warning on first open (see the conclusion page). To produce a
# properly signed + notarized installer, export these before running (they are optional):
#   APPLE_APP_IDENTITY        "Developer ID Application: Your Name (TEAMID)"   sign the bundles
#   APPLE_INSTALLER_IDENTITY  "Developer ID Installer: Your Name (TEAMID)"     sign the .pkg
#   APPLE_ID, APPLE_TEAM_ID, APPLE_APP_PASSWORD   also notarize + staple (needs both identities)
# NOTE: the signing / notarization paths have not been exercised yet (no Apple Developer account).

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
BUILD="${1:?usage: build_pkg.sh <cmake build dir> <output dir>}"
OUT="${2:?usage: build_pkg.sh <cmake build dir> <output dir>}"
VERSION="$(tr -d '[:space:]' < "$ROOT/VERSION")"
ART="$BUILD/Ottoclone_artefacts/Release"

VST3="$ART/VST3/Ottoclone.vst3"
AU="$ART/AU/Ottoclone.component"
APP="$ART/Standalone/Ottoclone.app"
for b in "$VST3" "$AU" "$APP"; do
    [[ -d "$b" ]] || { echo "error: missing build artefact: $b" >&2; exit 1; }
done

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/pkgs" "$WORK/resources" "$OUT"

sign_bundle() {
    local bundle="$1"; shift
    if [[ -n "${APPLE_APP_IDENTITY:-}" ]]; then
        codesign --force --deep --options runtime --timestamp --sign "$APPLE_APP_IDENTITY" "$@" "$bundle"
    else
        # Ad-hoc signature: required for the code to load on Apple Silicon.
        codesign --force --deep --sign - "$bundle"
    fi
    codesign --verify --deep --strict "$bundle"
}

echo "== Signing bundles"
sign_bundle "$VST3"
sign_bundle "$AU"
sign_bundle "$APP" --entitlements "$HERE/Standalone.entitlements"

echo "== Architectures"
for b in "$VST3/Contents/MacOS/Ottoclone" "$AU/Contents/MacOS/Ottoclone" "$APP/Contents/MacOS/Ottoclone"; do
    lipo -info "$b"
done

# make_component <name> <package identifier> <install location> <bundle>
make_component() {
    local name="$1" id="$2" location="$3" bundle="$4"
    local root="$WORK/root-$name" plist="$WORK/$name.plist"
    mkdir -p "$root"
    ditto "$bundle" "$root/$(basename "$bundle")"

    # Not relocatable: always install exactly where we say, never "update" a copy found elsewhere.
    pkgbuild --analyze --root "$root" "$plist" > /dev/null
    local n
    n=$(/usr/libexec/PlistBuddy -c Print "$plist" | grep -c "BundleIsRelocatable" || true)
    for ((i = 0; i < n; i++)); do
        /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$plist"
    done

    pkgbuild --root "$root" --component-plist "$plist" --identifier "$id" \
        --version "$VERSION" --install-location "$location" "$WORK/pkgs/$name.pkg"
}

echo "== Component packages"
make_component vst3       com.ottoclone.pkg.vst3       /Library/Audio/Plug-Ins/VST3       "$VST3"
make_component au         com.ottoclone.pkg.au         /Library/Audio/Plug-Ins/Components "$AU"
make_component standalone com.ottoclone.pkg.standalone /Applications                      "$APP"

echo "== Installer"
sed "s/@VERSION@/$VERSION/g" "$HERE/distribution.xml.in" > "$WORK/distribution.xml"
cp "$HERE/resources/welcome.html" "$HERE/resources/conclusion.html" "$WORK/resources/"
cp "$ROOT/LICENSE" "$WORK/resources/license.txt"

PKG="$OUT/Ottoclone-$VERSION-macOS.pkg"
rm -f "$PKG"
args=(--distribution "$WORK/distribution.xml" --resources "$WORK/resources" --package-path "$WORK/pkgs")
if [[ -n "${APPLE_INSTALLER_IDENTITY:-}" ]]; then
    args+=(--sign "$APPLE_INSTALLER_IDENTITY")
fi
productbuild "${args[@]}" "$PKG"

if [[ -n "${APPLE_INSTALLER_IDENTITY:-}" && -n "${APPLE_ID:-}" && -n "${APPLE_TEAM_ID:-}" && -n "${APPLE_APP_PASSWORD:-}" ]]; then
    echo "== Notarizing"
    xcrun notarytool submit "$PKG" --apple-id "$APPLE_ID" --team-id "$APPLE_TEAM_ID" \
        --password "$APPLE_APP_PASSWORD" --wait
    xcrun stapler staple "$PKG"
fi

echo "== Result"
ls -l "$PKG"
pkgutil --check-signature "$PKG" || true
pkgutil --expand-full "$PKG" "$WORK/expanded"
echo "Payload layout:"
(cd "$WORK/expanded" && find . -maxdepth 4 -path '*Payload*' \( -name '*.vst3' -o -name '*.component' -o -name '*.app' \))
