#!/usr/bin/env bash
# Publish log files as GitHub Actions notice annotations. Annotations can be read
# through the REST API (check-run annotations) where artifact downloads are not
# reachable. Long files keep their end, where the summaries are.
# Usage: scripts/ci-annotate.sh <file>...
set -euo pipefail
for file in "$@"; do
    [[ -f "$file" ]] || continue
    python3 - "$file" <<'PY'
import sys
path = sys.argv[1]
text = open(path, encoding='utf-8', errors='replace').read()
limit = 60000
if len(text) > limit:
    text = '[first %d characters omitted]\n' % (len(text) - limit) + text[-limit:]
def escape(s, prop=False):
    s = s.replace('%', '%25').replace('\r', '%0D').replace('\n', '%0A')
    return s.replace(':', '%3A').replace(',', '%2C') if prop else s
print('::notice title=%s::%s' % (escape(path, True), escape(text)))
PY
done
