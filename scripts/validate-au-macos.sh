#!/usr/bin/env bash
# Register and validate the already-built native Apple Silicon AUv2 bundle.
# Keep registration failures separate from actual Audio Unit validation failures.
set -euo pipefail
veil_component="${1:-build/VEIL_artefacts/Release/AU/VEIL.component}"
veil_logs="${2:-build/au-validation}"
mkdir -p "$veil_logs"
veil_plist="$veil_component/Contents/Info.plist"
veil_binary="$veil_component/Contents/MacOS/VEIL"
if [[ "$(uname -s)" != Darwin ]]; then
    echo 'This script requires macOS and Apple Audio Unit tools.' >&2
    exit 1
fi
{
    sw_vers
    uname -m
    /usr/bin/file "$veil_binary"
    /usr/bin/lipo -archs "$veil_binary"
    /usr/libexec/PlistBuddy -c Print "$veil_plist"
} > "$veil_logs/bundle-info.txt" 2>&1
cat "$veil_logs/bundle-info.txt"
/usr/bin/lipo "$veil_binary" -verify_arch arm64
for veil_pair in 'type aufx' 'subtype Vel1' 'manufacturer Veil' 'factoryFunction VEILAUFactory'; do
    read -r veil_key veil_expected <<< "$veil_pair"
    veil_actual=$(/usr/libexec/PlistBuddy -c "Print :AudioComponents:0:$veil_key" "$veil_plist")
    if [[ "$veil_actual" != "$veil_expected" ]]; then
        echo "AU identifier mismatch: $veil_key expected $veil_expected, got $veil_actual" >&2
        exit 1
    fi
done
# arm64 linking can leave a binary signature without a sealed bundle resource
# envelope. Sign the complete .component, then verify it before installation.
set +e
/usr/bin/codesign --verify --strict --verbose=4 "$veil_component" > "$veil_logs/signature-before.txt" 2>&1
veil_original_signature=$?
set -e
printf 'Original bundle signature verification exit: %s\n' "$veil_original_signature"
cat "$veil_logs/signature-before.txt"
/usr/bin/codesign --force --sign - --timestamp=none "$veil_component" 2>&1 | tee "$veil_logs/signature-signing.txt"
/usr/bin/codesign --verify --strict --verbose=4 "$veil_component" 2>&1 | tee "$veil_logs/signature-after.txt"
veil_components="$HOME/Library/Audio/Plug-Ins/Components"
veil_installed="$veil_components/VEIL.component"
mkdir -p "$veil_components"
/usr/bin/ditto "$veil_component" "$veil_installed"
/usr/bin/codesign --verify --strict --verbose=4 "$veil_installed" 2>&1 | tee "$veil_logs/installed-signature.txt"
touch "$veil_components"
# A newly copied AU may not yet be in AudioComponentRegistrar's cached inventory.
# Restart only the current user's registrar, then wait for the exact identity.
/usr/bin/arch -arm64 /usr/bin/auval -a > "$veil_logs/discovery-before.txt" 2>&1 || true
/usr/bin/killall -u "$(id -un)" AudioComponentRegistrar 2>/dev/null || true
veil_found=0
for veil_attempt in {1..30}; do
    /usr/bin/arch -arm64 /usr/bin/auval -a > "$veil_logs/discovery.txt" 2>&1 || true
    if /usr/bin/grep -Eq '^[[:space:]]*aufx[[:space:]]+Vel1[[:space:]]+Veil([[:space:]]|$)' "$veil_logs/discovery.txt"; then
        veil_found=1
        printf 'AU registered after discovery attempt %s.\n' "$veil_attempt"
        break
    fi
    sleep 1
done
cat "$veil_logs/discovery.txt"
if [[ "$veil_found" != 1 ]]; then
    echo 'Registration failed: aufx/Vel1/Veil is absent from the arm64 AU inventory.' >&2
    exit 2
fi
set +e
/usr/bin/arch -arm64 /usr/bin/auval -v aufx Vel1 Veil 2>&1 | tee "$veil_logs/auval.txt"
veil_validation_exit=${PIPESTATUS[0]}
set -e
printf '%s\n' "$veil_validation_exit" > "$veil_logs/auval-exit-code.txt"
printf 'Apple auval exit code: %s\n' "$veil_validation_exit"
exit "$veil_validation_exit"
