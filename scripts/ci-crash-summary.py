#!/usr/bin/env python3
"""Summarise recent macOS crash reports (.ips) so CI logs show the crashing frames.

Usage: ci-crash-summary.py <minutes> <name-substring>... (reads ~/Library/Logs/DiagnosticReports)
"""
import json
import os
import sys
import time

minutes = float(sys.argv[1])
needles = [n.lower() for n in sys.argv[2:]]
folder = os.path.expanduser('~/Library/Logs/DiagnosticReports')
cutoff = time.time() - minutes * 60
reports = []
for root in (folder, '/Library/Logs/DiagnosticReports'):
    if os.path.isdir(root):
        for name in os.listdir(root):
            path = os.path.join(root, name)
            if name.endswith('.ips') and os.path.getmtime(path) >= cutoff and any(n in name.lower() for n in needles):
                reports.append(path)
if not reports:
    print('no matching crash reports in the last %g minutes' % minutes)
for path in sorted(reports, key=os.path.getmtime):
    print('=== %s' % os.path.basename(path))
    try:
        header, body = open(path, encoding='utf-8', errors='replace').read().split('\n', 1)
        report = json.loads(body)
    except (ValueError, OSError) as error:
        print('  unreadable: %s' % error)
        continue
    print('  process: %s (%s)' % (report.get('procName'), report.get('cpuType')))
    print('  exception: %s' % json.dumps(report.get('exception')))
    if report.get('termination'):
        print('  termination: %s' % json.dumps(report['termination'])[:600])
    for key in ('asi', 'ktriageinfo'):
        if report.get(key):
            print('  %s: %s' % (key, json.dumps(report[key])[:1200]))
    images = report.get('usedImages', [])
    threads = report.get('threads', [])
    crashed = next((t for t in threads if t.get('triggered')), threads[0] if threads else None)
    if crashed:
        print('  crashed thread: %s' % (crashed.get('name') or crashed.get('queue') or ''))
        for frame in crashed.get('frames', [])[:24]:
            image = images[frame['imageIndex']].get('name', '?') if frame.get('imageIndex', -1) < len(images) else '?'
            symbol = frame.get('symbol', '+0x%x' % frame.get('imageOffset', 0))
            print('    %-28s %s' % (image, symbol))
