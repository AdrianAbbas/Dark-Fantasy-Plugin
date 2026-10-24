#!/usr/bin/env bash
# Remove VEIL for the current user.
#   bash uninstall-mac.sh           remove the plug-ins, keep your saved presets
#   bash uninstall-mac.sh --purge   also remove ~/Library/Audio/Presets/Veil Audio/VEIL
set -euo pipefail
purge=0
[[ "${1:-}" == --purge ]] && purge=1

removed=0
for bundle in "$HOME/Library/Audio/Plug-Ins/Components/VEIL.component" "$HOME/Library/Audio/Plug-Ins/VST3/VEIL.vst3"; do
    if [[ -e "$bundle" ]]; then
        rm -rf "$bundle"
        echo "Removed $bundle"
        removed=1
    fi
done
[[ $removed == 1 ]] || echo "No VEIL plug-ins found in ~/Library/Audio/Plug-Ins."

presets="$HOME/Library/Audio/Presets/Veil Audio/VEIL"
if [[ $purge == 1 && -d "$presets" ]]; then
    rm -rf "$presets"
    echo "Removed $presets"
elif [[ -d "$presets" ]]; then
    echo "Kept your presets in $presets (use --purge to remove them)."
fi

for system_copy in "/Library/Audio/Plug-Ins/Components/VEIL.component" "/Library/Audio/Plug-Ins/VST3/VEIL.vst3"; do
    [[ -e "$system_copy" ]] && echo "A system-wide copy remains: sudo rm -rf \"$system_copy\""
done

/usr/bin/killall -9 AudioComponentRegistrar 2>/dev/null || true
echo "Audio Unit registry reset. Restart GarageBand or Logic."
