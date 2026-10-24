#!/usr/bin/env bash
# Reproduce what a user does, then validate like a host.
#   1. unzip the release package and mark every file as a browser download
#      (com.apple.quarantine), which is what broke VEIL in GarageBand
#   2. record whether macOS on this runner refuses the quarantined copy
#   3. install with install-mac.sh --ci
#   4. auval and the AU host harness (--strict), arm64 and x86_64 (Rosetta)
# Usage: scripts/ci-user-install-test.sh <VEIL-macOS-Universal.zip> <harness binary> <log dir>
set -euo pipefail
package="$1"
harness="$(cd "$(dirname "$2")" && pwd)/$(basename "$2")"
logs="$3"
mkdir -p "$logs"
work="$(mktemp -d)"
status=0

# Run a command with a time limit (a Gatekeeper prompt would otherwise block forever).
limit() { local seconds="$1"; shift; /usr/bin/perl -e 'alarm shift @ARGV; exec @ARGV or die "exec failed: $!\n"' "$seconds" "$@"; }

/usr/bin/ditto -x -k "$package" "$work"
folder="$work/VEIL-macOS-Universal"
flag="0081;$(printf '%x' "$(date +%s)");Chrome;$(/usr/bin/uuidgen)"
/usr/bin/find "$folder" -exec /usr/bin/xattr -w com.apple.quarantine "$flag" {} +
{
    echo "Gatekeeper: $(/usr/sbin/spctl --status 2>&1)"
    /usr/bin/xattr -l "$folder/VEIL.component/Contents/MacOS/VEIL"
} | tee "$logs/quarantine.txt"

# 2. Quarantined copy installed by hand, without the script (informational).
components="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$components"
rm -rf "$components/VEIL.component"
/usr/bin/ditto "$folder/VEIL.component" "$components/VEIL.component"
/usr/bin/killall -9 AudioComponentRegistrar 2>/dev/null || true
sleep 3
set +e
limit 120 /usr/bin/auval -v aufx Vel1 Veil > "$logs/auval-quarantined-without-script.txt" 2>&1
quarantined_exit=$?
set -e
echo "auval on the quarantined copy (no install script): exit $quarantined_exit" | tee -a "$logs/quarantine.txt"
rm -rf "$components/VEIL.component"

# 3. The user's path: run the installer from the quarantined download.
set +e
limit 300 /bin/bash "$folder/install-mac.sh" --ci 2>&1 | tee "$logs/install-mac.txt"
install_exit=${PIPESTATUS[0]}
set -e
echo "install-mac.sh exit $install_exit"
if [[ $install_exit != 0 ]]; then echo "::error::install-mac.sh failed (exit $install_exit)"; status=1; fi
if /usr/bin/xattr -lr "$components/VEIL.component" | /usr/bin/grep -q com.apple.quarantine; then
    echo "::error::installed VEIL.component is still quarantined"; status=1
fi

# 4. Validators on the installed copy.
run_check() {
    local name="$1"; shift
    set +e
    limit 600 "$@" > "$logs/$name.txt" 2>&1
    local code=$?
    set -e
    echo "$name: exit $code"
    if [[ $code != 0 ]]; then echo "::error::$name failed (exit $code)"; status=1; fi
}
strict=""  # becomes -strict when this auval supports it
/usr/bin/auval -h 2>&1 | /usr/bin/grep -qE -- '(^|[[:space:]])-strict([[:space:]]|$)' && strict="-strict"
# shellcheck disable=SC2086  # $strict is empty or one flag
run_check auval-arm64 /usr/bin/arch -arm64 /usr/bin/auval $strict -v aufx Vel1 Veil
# One process per hosting route, so a crash in one cannot hide the others.
suites="v2-oop auaudiounit v2-inproc"
for suite in $suites; do run_check "harness-arm64-$suite" /usr/bin/arch -arm64 "$harness" --strict --only "$suite"; done
if /usr/bin/arch -x86_64 /usr/bin/true 2>/dev/null; then
    # shellcheck disable=SC2086
    run_check auval-x86_64 /usr/bin/arch -x86_64 /usr/bin/auval $strict -v aufx Vel1 Veil
    for suite in $suites; do run_check "harness-x86_64-$suite" /usr/bin/arch -x86_64 "$harness" --strict --only "$suite"; done
else
    echo "::error::Rosetta unavailable: the x86_64 slice was not validated"; status=1
fi
# Control (diagnostic, not a gate): Apple's own AULowpass through the AUAudioUnit route,
# to tell a harness problem on that route from a VEIL problem.
set +e
limit 300 "$harness" --only auaudiounit --no-window --component aufx:lpas:appl > "$logs/control-apple-lowpass-auaudiounit.txt" 2>&1
echo "control (Apple AULowpass, AUAudioUnit route): exit $?"
set -e
grep -h -E "^(FAIL|SUMMARY)" "$logs"/harness-*.txt || true
python3 "$(dirname "${BASH_SOURCE[0]}")/ci-crash-summary.py" 60 VeilAUHarness AUHostingService auval VEIL > "$logs/crashes.txt" 2>&1 || true
cat "$logs/crashes.txt"
exit $status
