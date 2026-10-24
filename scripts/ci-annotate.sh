#!/usr/bin/env bash
# Publish log files as GitHub Actions notice annotations. Annotations can be read
# through the REST API (check-run annotations) where artifact downloads are not
# reachable. GitHub cuts each annotation at about 4 KB, so files are split into
# numbered parts; with --condense, runs of PASS lines become a count (FAIL, INFO,
# SKIP and SUMMARY lines are kept verbatim). GitHub keeps at most 10 notices per
# step and 50 per job, so keep the input small.
# Usage: scripts/ci-annotate.sh [--condense] <file>...
set -euo pipefail
condense=0
if [[ "${1:-}" == --condense ]]; then condense=1; shift; fi
for file in "$@"; do
    [[ -f "$file" ]] || continue
    python3 - "$file" "$condense" <<'PY'
import sys
path, condense = sys.argv[1], sys.argv[2] == '1'
lines = open(path, encoding='utf-8', errors='replace').read().splitlines()
if condense:
    out, passes = [], 0
    for line in lines:
        if line.startswith('PASS '):
            passes += 1
            continue
        if passes:
            out.append('(%d PASS)' % passes)
            passes = 0
        out.append(line)
    if passes:
        out.append('(%d PASS)' % passes)
    lines = out
def escape(s, prop=False):
    s = s.replace('%', '%25').replace('\r', '%0D').replace('\n', '%0A')
    return s.replace(':', '%3A').replace(',', '%2C') if prop else s
chunks, current = [], ''
for line in lines:
    line = line[:1500]
    if current and len(escape(current + line + '\n')) > 3800:
        chunks.append(current)
        current = ''
    current += line + '\n'
chunks.append(current)
for i, chunk in enumerate(chunks, 1):
    print('::notice title=%s::%s' % (escape('%s [%d/%d]' % (path, i, len(chunks)), True), escape(chunk)))
PY
done
