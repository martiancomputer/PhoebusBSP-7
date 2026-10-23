#!/usr/bin/env python3
"""Summarize JSONL benchmark data without embedding machine identifiers."""
import collections
import json
import statistics
import sys
from pathlib import Path

rows = [json.loads(line) for line in Path(sys.argv[1]).read_text().splitlines() if line.strip()]
groups = collections.defaultdict(list)
for row in rows:
    groups[tuple(row[k] for k in ('algorithm', 'work', 'memory_kib', 'lanes', 'jobs'))].append(row)
print('| Algorithm / parameters | Trials | Median batch seconds | Range seconds | Peak RSS MiB | Process CPU, one VPE=100% | Machine busy % |')
print('|---|---:|---:|---:|---:|---:|---:|')
for (algorithm, work, memory, lanes, jobs), values in groups.items():
    label = f'{algorithm} work={work}'
    if algorithm == 'argon2id':
        label += f', m={memory/1024:g} MiB, p={lanes}'
    label += f', jobs={jobs}'
    wall = [v['wall_s'] for v in values]
    cpu = statistics.median(v['process_cpu_pct_one_vpe'] for v in values)
    busy = statistics.median(v['machine_busy_pct'] for v in values)
    rss = max(v['peak_rss_kib'] for v in values)/1024
    print(f'| {label} | {len(values)} | {statistics.median(wall):.3f} | {min(wall):.3f}–{max(wall):.3f} | {rss:.2f} | {cpu:.1f} | {busy:.1f} |')
print()
print('For jobs > 1, batch time is NOT per-login time. CPU percentages include sampler overhead.')
print('Peak temperature, C:', max(v['max_temp_mC'] for v in rows)/1000)
print('Minimum observed MemAvailable, MiB:', min(v['min_available_kib'] for v in rows)/1024)
print('Major page faults:', sum(v['major_faults'] for v in rows))
