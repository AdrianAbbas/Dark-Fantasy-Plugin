#!/usr/bin/env bash
# Read-only diagnostics for "VEIL is silent / shows generic sliders in GarageBand".
#
# It changes nothing on your Mac. It only reads files, asks Apple's tools
# questions and writes one report file, by default ~/Desktop/VEIL-diagnostics.txt.
#
# For the most useful report:
#   1. Open GarageBand, insert VEIL on a software-instrument track and play a note,
#      so the failing state is live.
#   2. Leave GarageBand open and run in Terminal:
#        bash ~/Downloads/diagnose-mac.sh
#   3. Send the report file back.
# (No `set -u`: macOS bash 3.2 treats an empty array as unbound.)

report="${1:-$HOME/Desktop/VEIL-diagnostics.txt}"
: > "$report" || { echo "Cannot write $report" >&2; exit 1; }
exec > >(tee -a "$report") 2>&1

section() { printf '\n===== %s\n' "$*"; }
run() { printf '$ %s\n' "$*"; "$@" 2>&1; printf '(exit %s)\n' "$?"; }

section "System"
run sw_vers
run uname -m
sysctl -n machdep.cpu.brand_string 2>/dev/null
printf 'Shell running under Rosetta (1 = yes): %s\n' "$(sysctl -n sysctl.proc_translated 2>/dev/null || echo n/a)"
run /usr/sbin/spctl --status
if /usr/bin/arch -x86_64 /usr/bin/true 2>/dev/null; then echo "Rosetta 2: installed"; else echo "Rosetta 2: not installed (or Intel Mac)"; fi

section "Hosts"
for app in "/Applications/GarageBand.app" "/Applications/Logic Pro.app" "/Applications/Logic Pro X.app"; do
    [[ -d "$app" ]] || continue
    exe="$app/Contents/MacOS/$(/usr/bin/defaults read "$app/Contents/Info" CFBundleExecutable 2>/dev/null)"
    # `file`, not `lipo`: lipo is an Xcode shim that fails until the Xcode licence is accepted.
    printf '%s  version %s\n  %s\n' "$app" \
        "$(/usr/bin/defaults read "$app/Contents/Info" CFBundleShortVersionString 2>/dev/null)" \
        "$(/usr/bin/file "$exe" 2>/dev/null)"
done
echo "Running host / plug-in hosting processes (pid, name):"
/usr/bin/pgrep -l -i -f 'GarageBand|Logic Pro|AUHostingService' 2>/dev/null || echo "  none running"

section "Where is VEIL loaded right now?"
# If GarageBand is open with VEIL inserted, this shows which process has the VEIL
# binary mapped: GarageBand itself (in-process) or an AUHostingService (out-of-process).
found_loaded=0
for pid in $(/usr/bin/pgrep -i -f 'GarageBand|Logic Pro|AUHostingService' 2>/dev/null); do
    name=$(/bin/ps -o comm= -p "$pid" 2>/dev/null)
    hits=$(/usr/sbin/lsof -p "$pid" 2>/dev/null | /usr/bin/grep -i 'VEIL.component' | /usr/bin/awk '{print $NF}' | /usr/bin/sort -u)
    if [[ -n "$hits" ]]; then
        found_loaded=1
        printf 'pid %s (%s) has VEIL loaded from:\n%s\n' "$pid" "$name" "$hits"
    fi
done
[[ $found_loaded == 1 ]] || echo "VEIL is not mapped into any running GarageBand/Logic/AUHostingService process."

section "Installed VEIL bundles"
components=()
for dir in "$HOME/Library/Audio/Plug-Ins/Components" "/Library/Audio/Plug-Ins/Components" \
           "$HOME/Library/Audio/Plug-Ins/VST3" "/Library/Audio/Plug-Ins/VST3"; do
    [[ -d "$dir" ]] || continue
    while IFS= read -r -d '' bundle; do
        printf '%s  (modified %s)\n' "$bundle" "$(/usr/bin/stat -f '%Sm' "$bundle")"
        [[ "$bundle" == *.component ]] && components+=("$bundle")
    done < <(/usr/bin/find "$dir" -maxdepth 1 -iname 'veil*' -print0 2>/dev/null)
done
[[ ${#components[@]} -gt 0 ]] || echo "No VEIL.component found in either Components folder."
echo "Other copies Spotlight knows about (e.g. in Downloads):"
/usr/bin/mdfind "kMDItemFSName == 'VEIL.component'" 2>/dev/null | /usr/bin/head -20

for bundle in "${components[@]}"; do
    section "Bundle: $bundle"
    binary="$bundle/Contents/MacOS/VEIL"
    run /usr/bin/file "$binary"
    run /usr/bin/shasum -a 256 "$binary"
    run /usr/libexec/PlistBuddy -c 'Print :AudioComponents' "$bundle/Contents/Info.plist"
    echo "Extended attributes (com.apple.quarantine means macOS treats it as downloaded):"
    /usr/bin/xattr -lr "$bundle" 2>&1 | /usr/bin/head -20
    run /usr/bin/codesign -dv --verbose=4 "$bundle"
    run /usr/bin/codesign --verify --strict --verbose=4 "$bundle"
    run /usr/sbin/spctl --assess --type install -vv "$bundle"
done

section "Audio Unit registry"
echo "Native ($(uname -m)) registry entries for VEIL:"
/usr/bin/auval -a 2>&1 | /usr/bin/grep -i -E 'veil|vel1' || echo "  not listed"
if /usr/bin/arch -x86_64 /usr/bin/true 2>/dev/null; then
    echo "x86_64 (Rosetta) registry entries for VEIL:"
    /usr/bin/arch -x86_64 /usr/bin/auval -a 2>&1 | /usr/bin/grep -i -E 'veil|vel1' || echo "  not listed"
fi

section "auval (native)"
/usr/bin/auval -v aufx Vel1 Veil > /tmp/veil-auval-native.txt 2>&1
echo "exit $?"
/usr/bin/grep -E 'Cocoa|FATAL|ERROR|WARNING|Channel Capabilities|\[[0-9-]+, [0-9-]+\]|VALIDATION|Time to open' /tmp/veil-auval-native.txt | /usr/bin/head -40
if /usr/bin/arch -x86_64 /usr/bin/true 2>/dev/null; then
    section "auval (x86_64 under Rosetta)"
    /usr/bin/arch -x86_64 /usr/bin/auval -v aufx Vel1 Veil > /tmp/veil-auval-x86.txt 2>&1
    echo "exit $?"
    /usr/bin/grep -E 'FATAL|ERROR|VALIDATION|architecture|Time to open' /tmp/veil-auval-x86.txt | /usr/bin/head -15
fi

section "Audio Unit caches"
/bin/ls -la "$HOME/Library/Caches/AudioUnitCache" 2>&1 | /usr/bin/head -20
/bin/ls -la "$HOME/Library/Preferences/com.apple.audio.InfoHelper.plist" 2>&1

section "Crash reports from the last 7 days"
crashes=$(/usr/bin/find "$HOME/Library/Logs/DiagnosticReports" /Library/Logs/DiagnosticReports -mtime -7 -type f \
    \( -iname '*AUHosting*' -o -iname '*GarageBand*' -o -iname '*Logic*' -o -iname '*VEIL*' -o -iname '*auval*' -o -iname '*AudioComponent*' \) 2>/dev/null \
    | /usr/bin/xargs -I{} /usr/bin/stat -f '%m %N' {} 2>/dev/null | /usr/bin/sort -rn | /usr/bin/cut -d' ' -f2- | /usr/bin/head -6)
if [[ -z "$crashes" ]]; then
    echo "none"
else
    while IFS= read -r crash; do
        printf '\n--- %s\n' "$crash"
        /usr/bin/grep -n -i -E '"procName"|"exception"|"type"|"signal"|termination|Exception Type|Crashed Thread|VEIL|juce' "$crash" | /usr/bin/head -40
    done <<< "$crashes"
fi

section "System log, last 2 hours (VEIL, Audio Unit hosting, Gatekeeper)"
/usr/bin/log show --last 2h --style compact --predicate \
    '(eventMessage CONTAINS[c] "veil") OR (eventMessage CONTAINS[c] "Vel1") OR (process BEGINSWITH "AUHostingService" AND messageType >= 16) OR (process == "AudioComponentRegistrar" AND messageType >= 16)' \
    2>&1 | /usr/bin/tail -250

section "Questions only you can answer (please add the answers when you send this)"
cat <<'QUESTIONS'
1. In Finder > Applications, select GarageBand, File > Get Info: is "Open using Rosetta" ticked?
2. Which file did you download (artifact name and date), and how did you install VEIL.component?
3. In the VEIL plug-in window, is there a "Controls / Editor" view menu at the top? Which one is selected?
4. Does the instrument become audible again when you switch VEIL off with its power button in Smart Controls?
5. Did macOS show any warning such as "cannot be opened because the developer cannot be verified"?
QUESTIONS

printf '\nReport written to %s\n' "$report"
