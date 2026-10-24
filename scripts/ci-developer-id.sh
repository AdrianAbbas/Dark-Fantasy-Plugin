#!/usr/bin/env bash
# OPTIONAL: Developer ID signing with hardened runtime, notarisation and stapling,
# for distributing VEIL to other people. CI runs this only when the signing
# secrets exist; without them the build is ad-hoc signed instead.
# UNTESTED: no signing identity was available when this was written.
#
# Required environment (GitHub secrets):
#   MACOS_CERTIFICATE           base64 of the Developer ID Application .p12
#   MACOS_CERTIFICATE_PASSWORD  password of that .p12
#   MACOS_SIGNING_IDENTITY      e.g. "Developer ID Application: Your Name (TEAMID)"
#   NOTARY_APPLE_ID, NOTARY_TEAM_ID, NOTARY_PASSWORD (app-specific password)
# Usage: scripts/ci-developer-id.sh <artefacts dir>
set -euo pipefail
artefacts="$1"
keychain="$RUNNER_TEMP/veil-signing.keychain-db"
keychain_password="$(/usr/bin/uuidgen)"
/usr/bin/security create-keychain -p "$keychain_password" "$keychain"
/usr/bin/security set-keychain-settings -lut 21600 "$keychain"
/usr/bin/security unlock-keychain -p "$keychain_password" "$keychain"
echo "$MACOS_CERTIFICATE" | /usr/bin/base64 --decode > "$RUNNER_TEMP/veil-cert.p12"
/usr/bin/security import "$RUNNER_TEMP/veil-cert.p12" -k "$keychain" -P "$MACOS_CERTIFICATE_PASSWORD" -T /usr/bin/codesign
rm -f "$RUNNER_TEMP/veil-cert.p12"
# shellcheck disable=SC2046
/usr/bin/security list-keychains -d user -s "$keychain" $(/usr/bin/security list-keychains -d user | tr -d '"')
/usr/bin/security set-key-partition-list -S apple-tool:,apple: -s -k "$keychain_password" "$keychain" > /dev/null

for bundle in "$artefacts/AU/VEIL.component" "$artefacts/VST3/VEIL.vst3" "$artefacts/Standalone/VEIL.app"; do
    /usr/bin/codesign --force --deep --options runtime --timestamp --sign "$MACOS_SIGNING_IDENTITY" "$bundle"
    /usr/bin/codesign --verify --strict --verbose=2 "$bundle"
    archive="$RUNNER_TEMP/$(basename "$bundle").zip"
    /usr/bin/ditto -c -k --keepParent "$bundle" "$archive"
    /usr/bin/xcrun notarytool submit "$archive" --apple-id "$NOTARY_APPLE_ID" --team-id "$NOTARY_TEAM_ID" \
        --password "$NOTARY_PASSWORD" --wait
    /usr/bin/xcrun stapler staple "$bundle"
done
