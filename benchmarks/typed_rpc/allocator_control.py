#!/usr/bin/env python3
"""Isolate allocator-environment sensitivity of the large-payload result.

This never changes the caller's process environment or recommends deployment
settings. Each measured child runs with one explicitly recorded setting.
"""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import random
import statistics
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--binary', type=Path, required=True)
p.add_argument('--out', type=Path, required=True)
p.add_argument('--repetitions', type=int, default=5)
p.add_argument('--cpu', type=int)
a = p.parse_args()
if not a.binary.is_file() or a.repetitions < 1:
    p.error('existing binary and positive repetitions required')
a.binary = a.binary.resolve()
a.out.mkdir(parents=True, exist_ok=True)
cpus = sorted(os.sched_getaffinity(0)) if hasattr(os, 'sched_getaffinity') else []
cpu = a.cpu if a.cpu is not None else (cpus[0] if cpus else None)
if cpus:
    if cpu not in cpus:
        p.error('CPU is outside allowed affinity')
    os.sched_setaffinity(0, {cpu})
base_env = os.environ.copy()
# Do not label a control 'default' when inherited malloc tunables are active.
for key in list(base_env):
    if key.startswith('MALLOC_') or key == 'GLIBC_TUNABLES':
        del base_env[key]
rng = random.Random(20261114)
raw, rows = [], []
lanes = ('dynamic_fresh', 'typed_fresh', 'dynamic_reuse', 'typed_reuse')
checksum = None
for rep in range(a.repetitions):
    jobs = [(mode, lane) for mode in ('default', 'trim_disabled') for lane in lanes]
    rng.shuffle(jobs)
    for mode, lane in jobs:
        env = base_env.copy()
        changes = {} if mode == 'default' else {'MALLOC_TRIM_THRESHOLD_': '-1'}
        env.update(changes)
        command = [str(a.binary), lane, '65536', '10000', 'timing']
        r = subprocess.run(command, env=env, text=True, capture_output=True, timeout=120)
        raw.append({'round': rep, 'environment': changes, 'command': command,
                    'returncode': r.returncode, 'stdout': r.stdout, 'stderr': r.stderr})
        (a.out / 'processes.json').write_text(json.dumps(raw, indent=2) + '\n')
        if r.returncode:
            raise RuntimeError(raw[-1])
        data = json.loads(r.stdout)
        if checksum is not None and checksum != data['checksum']:
            raise RuntimeError('checksum mismatch')
        checksum = data['checksum']
        rows.append(dict(data, round=rep, allocator=mode))
(a.out / 'environment.json').write_text(json.dumps({
    'binary': str(a.binary), 'sha256': hashlib.sha256(a.binary.read_bytes()).hexdigest(),
    'selected_cpu': cpu, 'repetitions': a.repetitions, 'seed': 20261114,
    'inherited_allocator_settings_removed': True,
    'diagnostic_only': True,
}, indent=2) + '\n')
with (a.out / 'samples.csv').open('w', newline='') as f:
    writer = csv.DictWriter(f, fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
summary = []
for mode in ('default', 'trim_disabled'):
    for lane in lanes:
        obs = [r['ns_per_call'] for r in rows if r['allocator'] == mode and r['lane'] == lane]
        summary.append({'allocator': mode, 'lane': lane, 'median_ns': statistics.median(obs),
                        'min_ns': min(obs), 'max_ns': max(obs)})
with (a.out / 'summary.csv').open('w', newline='') as f:
    writer = csv.DictWriter(f, fieldnames=list(summary[0])); writer.writeheader(); writer.writerows(summary)
print(json.dumps(summary, indent=2))
