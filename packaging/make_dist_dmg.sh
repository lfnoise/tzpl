#!/bin/bash
# Sign (optionally), package, and notarize (optionally) the distribution DMG.
# Called by the `dist` CMake target after the stage directory is populated.
#
#   make_dist_dmg.sh <stage-dir> <dmg-path> <entitlements-plist> [min-macos]
#
# <min-macos> is the deployment target the binaries are supposed to carry
# (CMAKE_OSX_DEPLOYMENT_TARGET); the script refuses to package a binary
# whose Mach-O minos is higher, so a stale cache or a dependency built for
# a newer macOS cannot silently raise the floor of a shipped DMG.
#
# Controlled by environment variables so one pipeline serves everyone:
#
#   TZPL_CODESIGN_IDENTITY   "Developer ID Application: Name (TEAMID)".
#                            Unset: binaries keep their ad-hoc signatures
#                            (local/contributor builds). "-" re-signs ad-hoc
#                            with the entitlements, for testing this path.
#   TZPL_NOTARY_PROFILE      notarytool keychain profile name (created once
#                            with `xcrun notarytool store-credentials`).
#                            Unset: skip notarization/stapling.
#
# Notarization covers the whole DMG (all executables inside must be signed
# with hardened runtime + timestamp). The DMG is stapled, so Gatekeeper can
# verify offline; the app inside gets its ticket from Apple's servers on
# first launch when online (staple the .app separately only if fully-offline
# first launch matters).

set -euo pipefail

STAGE="$1"      # .../dist-stage (contains Tzopilotl/)
DMG="$2"
ENTITLEMENTS="$3"
MIN_MACOS="${4:-}"

APP="$STAGE/Tzopilotl/Tzopilotl.app"
CLI="$STAGE/Tzopilotl/bin/tzpl"

IDENTITY="${TZPL_CODESIGN_IDENTITY:-}"
PROFILE="${TZPL_NOTARY_PROFILE:-}"

# A distributable binary must reference only system libraries: a stray
# Homebrew dylib path (e.g. libnats when the static-link config fell back
# to dynamic) would make the app fail to launch on end-user machines.
check_no_external_dylibs() {
    local bin="$1"
    local bad
    bad=$(otool -L "$bin" | tail -n +2 | awk '{print $1}' \
          | grep -v -e '^/usr/lib/' -e '^/System/' -e '^@' || true)
    if [[ -n "$bad" ]]; then
        echo "dist: ERROR: $bin references non-system dylibs:" >&2
        echo "$bad" >&2
        echo "dist: (for NATS, install static libs: brew install cnats openssl@3)" >&2
        exit 1
    fi
}
check_no_external_dylibs "$CLI"
check_no_external_dylibs "$APP/Contents/MacOS/Tzopilotl"

# The floor a binary actually declares (LC_BUILD_VERSION minos) must not
# exceed the configured deployment target. Checked per architecture slice.
check_min_macos() {
    local bin="$1" minos
    [[ -z "$MIN_MACOS" ]] && return 0
    minos=$(vtool -show-build "$bin" | awk '$1 == "minos" {print $2}')
    if [[ -z "$minos" ]]; then
        echo "dist: ERROR: $bin has no LC_BUILD_VERSION minos" >&2
        exit 1
    fi
    local v
    for v in $minos; do
        # highest of (v, MIN_MACOS) must be MIN_MACOS itself
        if [[ "$(printf '%s\n%s\n' "$v" "$MIN_MACOS" | sort -V | tail -1)" != "$MIN_MACOS" ]]; then
            echo "dist: ERROR: $bin requires macOS $v, above the configured minimum $MIN_MACOS" >&2
            echo "dist: (reconfigure: CMAKE_OSX_DEPLOYMENT_TARGET is FORCEd from TZPL_MACOS_DEPLOYMENT_TARGET)" >&2
            exit 1
        fi
    done
    echo "dist: $bin: minimum macOS $minos"
}
check_min_macos "$CLI"
check_min_macos "$APP/Contents/MacOS/Tzopilotl"

if [[ -n "$IDENTITY" ]]; then
    SIGN_FLAGS=(--force --options runtime --entitlements "$ENTITLEMENTS")
    # A real identity gets a secure timestamp (required for notarization);
    # the ad-hoc identity "-" (testing) cannot use the timestamp service.
    [[ "$IDENTITY" != "-" ]] && SIGN_FLAGS+=(--timestamp)

    echo "dist: signing with identity: $IDENTITY"
    codesign "${SIGN_FLAGS[@]}" -s "$IDENTITY" "$CLI"
    codesign "${SIGN_FLAGS[@]}" -s "$IDENTITY" "$APP"
    codesign --verify --strict --deep "$APP"
    codesign --verify --strict "$CLI"
else
    echo "dist: TZPL_CODESIGN_IDENTITY not set; leaving ad-hoc signatures"
fi

# Build the image through a read/write scratch image attached at a private
# mountpoint, then compress it. `hdiutil create -srcfolder` would mount its
# scratch volume under /Volumes named after the image; with a Tzopilotl DMG
# already mounted there (the previous draft, opened for testing) that
# becomes "/Volumes/Tzopilotl 1" and macOS refuses to write into it, so the
# dist target failed with a bare "Error 1" whenever a draft was being tried.
# Scratch image and mountpoint live in the system temp dir, not next to
# the DMG: a build tree under Dropbox gets the fresh volume's .fseventsd
# held open by the Dropbox daemon, and the detach below then fails with
# "Resource busy" indefinitely.
SCRATCH="$(mktemp -d "${TMPDIR:-/tmp}/tzpl-dist.XXXXXX")"
RW_DMG="$SCRATCH/dist-rw.dmg"
MNT="$SCRATCH/mnt"
mkdir -p "$MNT"
# Size: the staged folder plus headroom for the filesystem.
STAGE_KB=$(du -sk "$STAGE" | awk '{print $1}')
hdiutil create -size "$((STAGE_KB / 1024 + 64))m" -fs HFS+ -volname "Tzopilotl" \
    -layout NONE -ov -quiet "$RW_DMG"
hdiutil attach -nobrowse -mountpoint "$MNT" -quiet "$RW_DMG"
cp -R "$STAGE"/. "$MNT"/
# Detach can still fail with "Resource busy" (exit 16) right after the
# copy if Spotlight is indexing the fresh volume. Retry before giving up.
for attempt in 1 2 3; do
    if hdiutil detach -quiet "$MNT"; then break; fi
    if [[ $attempt -eq 3 ]]; then
        echo "dist: ERROR: could not detach $MNT (still busy)" >&2
        exit 1
    fi
    echo "dist: $MNT busy, retrying detach ($attempt)..."
    sleep 2
done
hdiutil convert "$RW_DMG" -format UDZO -ov -quiet -o "$DMG"
rm -rf "$SCRATCH"

if [[ -n "$IDENTITY" && "$IDENTITY" != "-" ]]; then
    codesign --force --timestamp -s "$IDENTITY" "$DMG"
fi

if [[ -n "$PROFILE" ]]; then
    if [[ -z "$IDENTITY" || "$IDENTITY" == "-" ]]; then
        echo "dist: ERROR: notarization requires a real TZPL_CODESIGN_IDENTITY" >&2
        exit 1
    fi
    echo "dist: submitting to Apple notary service (profile: $PROFILE)..."
    xcrun notarytool submit "$DMG" --keychain-profile "$PROFILE" --wait
    xcrun stapler staple "$DMG"
    echo "dist: notarized and stapled"
else
    echo "dist: TZPL_NOTARY_PROFILE not set; skipping notarization"
fi

echo "dist: $DMG"
