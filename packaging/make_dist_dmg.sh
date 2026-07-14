#!/bin/bash
# Sign (optionally), package, and notarize (optionally) the distribution DMG.
# Called by the `dist` CMake target after the stage directory is populated.
#
#   make_dist_dmg.sh <stage-dir> <dmg-path> <entitlements-plist>
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

APP="$STAGE/Tzopilotl/Tzopilotl.app"
CLI="$STAGE/Tzopilotl/bin/tzpl"

IDENTITY="${TZPL_CODESIGN_IDENTITY:-}"
PROFILE="${TZPL_NOTARY_PROFILE:-}"

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

hdiutil create -volname "Tzopilotl" -srcfolder "$STAGE" -ov -format UDZO \
    -quiet "$DMG"

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
