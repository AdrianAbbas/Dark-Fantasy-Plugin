#!/usr/bin/env bash
# Sign the built bundles ad hoc (whole bundle, --deep), verify them, and stage
# dist/VEIL-macOS-Universal.zip with the AU, the VST3 and the install scripts.
# Usage: scripts/package-mac.sh <artefacts dir> <dist dir>
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
artefacts="$1"
dist="$2"
stage="$dist/VEIL-macOS-Universal"
rm -rf "$stage" "$dist/VEIL-macOS-Universal.zip"
mkdir -p "$stage"

for bundle in "$artefacts/AU/VEIL.component" "$artefacts/VST3/VEIL.vst3" "$artefacts/Standalone/VEIL.app"; do
    # Developer ID signing (optional CI path) runs before this script; keep it.
    if ! /usr/bin/codesign -dv --verbose=2 "$bundle" 2>&1 | /usr/bin/grep -q "Authority=Developer ID Application"; then
        /usr/bin/codesign --force --deep --sign - --timestamp=none "$bundle"
    fi
    /usr/bin/codesign --verify --strict --verbose=4 "$bundle"
    /usr/bin/codesign -dv --verbose=2 "$bundle" 2>&1 | /usr/bin/grep -E "^(Identifier|Format|Signature|Authority|Sealed Resources)"
done

/usr/bin/ditto "$artefacts/AU/VEIL.component" "$stage/VEIL.component"
/usr/bin/ditto "$artefacts/VST3/VEIL.vst3" "$stage/VEIL.vst3"
install -m 755 "$here/scripts/install-mac.sh" "$here/scripts/uninstall-mac.sh" "$stage/"
install -m 644 "$here/packaging/INSTALL.txt" "$stage/"
# ditto keeps permissions and bundle metadata, unlike a plain zip of the artifact.
/usr/bin/ditto -c -k --sequesterRsrc --keepParent "$stage" "$dist/VEIL-macOS-Universal.zip"
echo "Packaged $dist/VEIL-macOS-Universal.zip"
/usr/bin/unzip -l "$dist/VEIL-macOS-Universal.zip" | /usr/bin/grep -E "install|INSTALL|MacOS/VEIL$"
