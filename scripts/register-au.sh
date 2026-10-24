#!/usr/bin/env bash
# Install a VEIL.component into ~/Library/Audio/Plug-Ins/Components, restart the
# AU registrar and wait until aufx/Vel1/Veil is listed. Used by CI experiments.
# Usage: scripts/register-au.sh <path/to/VEIL.component>
set -euo pipefail
source_bundle="$1"
components="$HOME/Library/Audio/Plug-Ins/Components"
installed="$components/VEIL.component"
mkdir -p "$components"
rm -rf "$installed"
/usr/bin/ditto "$source_bundle" "$installed"
/usr/bin/killall -9 AudioComponentRegistrar 2>/dev/null || true
for attempt in $(seq 1 30); do
    if /usr/bin/auval -a 2>/dev/null | /usr/bin/grep -Eq '^[[:space:]]*aufx[[:space:]]+Vel1[[:space:]]+Veil'; then
        echo "registered after $attempt attempt(s)"
        exit 0
    fi
    sleep 1
done
echo "aufx/Vel1/Veil not registered after 30 s" >&2
exit 2
