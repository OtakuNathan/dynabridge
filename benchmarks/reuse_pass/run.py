#!/usr/bin/env python3
"""Serial Portal reuse-pass experiments. No downloads, pushes, or source edits.

Use a separate baseline checkout. The allocator experiment is applied only to
an out-of-tree copy. Allocation instrumentation runs in separate binaries.
"""
from __future__ import annotations
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import statistics
import subprocess
import sys
from prepare_allocator import prepare

ROOT = Path(__file__).resolve().parents[2]


def write_csv(path: Path, rows: list[dict]) -> None:
    with path.open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]), lineterminator='\n')
        writer.writeheader()
        writer.writerows(rows)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--baseline', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--cxx', default='g++')
    p.add_argument('--cpu', type=int)
    p.add_argument('--repeats', type=int, default=7)
    p.add_argument('--scale', type=float, default=1.0, help='iteration multiplier; warmup remains 10000')
    p.add_argument('--isolated', action='store_true', help='explicit reduced codec/router coverage')
    action = p.add_mutually_exclusive_group()
    action.add_argument('--build-only', action='store_true')
    action.add_argument('--run-only', action='store_true')
    a = p.parse_args()
    if a.repeats < 1 or not (0 < a.scale <= 100):
        p.error('repeats must be positive and scale in (0,100]')
    baseline, out = a.baseline.resolve(), a.out.resolve()
    if not (baseline / 'portal/backends/rpc.h').is_file():
        p.error('baseline must be an existing Portal source checkout')
    if a.cpu is not None:
        if not shutil.which('taskset'):
            p.error('--cpu requires Linux taskset; omit it on other hosts')
        if hasattr(os, 'sched_getaffinity') and a.cpu not in os.sched_getaffinity(0):
            p.error('--cpu is outside the process affinity mask')
    out.mkdir(parents=True, exist_ok=True)
    bindir = out / 'bin'; bindir.mkdir(exist_ok=True)
    affinity = ['taskset', '-c', str(a.cpu)] if a.cpu is not None else []
    build_commands = []
    common = [a.cxx, '-std=c++14', '-O3', '-DNDEBUG', '-Wall', '-Wextra', '-Werror', '-pthread']
    if not a.run_only:
        overlay = prepare(ROOT, out / 'allocator-overlay')
        if a.isolated:
            for label, source in [('before', baseline), ('after', ROOT)]:
                subprocess.run([sys.executable, str(ROOT/'benchmarks/reuse_pass/extract_wire.py'),
                                str(source), str(out/label/'isolated_rpc.h')], check=True)

        def build(name: str, source: str, headers: Path, *, before=False, wire=False, instrument=False) -> None:
            command = common + ['-I'+str(headers), '-I'+str(ROOT)]
            if before: command += ['-DPORTAL_REUSE_BASELINE']
            if wire and a.isolated:
                label = 'before' if headers == baseline else 'after'
                command += ['-DPORTAL_REUSE_ISOLATED', '-I'+str(out/label)]
            if instrument: command += ['-DPORTAL_COUNT_ALLOCATIONS']
            command += [str(ROOT/'benchmarks/reuse_pass'/source), '-o', str(bindir/name)]
            print('build', name, flush=True)
            subprocess.run(command, check=True)
            build_commands.append(command)

        build('rpc-before', 'rpc_bench.cpp', baseline, before=True, wire=True)
        build('rpc-after', 'rpc_bench.cpp', ROOT, wire=True)
        build('rpc-ordinary-control', 'rpc_bench.cpp', ROOT, before=True, wire=True)
        build('rpc-count', 'rpc_bench.cpp', ROOT, wire=True, instrument=True)
        build('async-before', 'async_bench.cpp', baseline, before=True)
        build('async-experiment', 'async_bench.cpp', overlay)
        build('async-count', 'async_bench.cpp', overlay, instrument=True)
        build('borrowed', 'borrowed_bench.cpp', ROOT, wire=True)
        build('borrowed-count', 'borrowed_bench.cpp', ROOT, wire=True, instrument=True)
        sources = {}
        for label, source in [('baseline', baseline), ('candidate', ROOT), ('allocator-experiment', overlay)]:
            for file in ['portal/backends/rpc.h','portal/backends/rpc_reuse.h','portal/async.h','portal/optional.h']:
                path = source / file
                if path.is_file(): sources[label+'/'+file] = digest(path)
        meta = dict(compiler=subprocess.check_output([a.cxx,'--version'],text=True),
                    platform=platform.platform(), python=sys.version, cpu=a.cpu,
                    affinity=sorted(os.sched_getaffinity(0)) if hasattr(os,'sched_getaffinity') else None,
                    isolated=a.isolated, builds=build_commands, sources=sources,
                    binaries={x.name:digest(x) for x in bindir.iterdir() if x.is_file()})
        (out/'provenance.json').write_text(json.dumps(meta,indent=2))
    else:
        meta = json.loads((out/'provenance.json').read_text())
        if meta['isolated'] != a.isolated:
            p.error('run-only must use the same --isolated setting as build-only')
        for name, expected in meta['binaries'].items():
            if digest(bindir/name) != expected:
                raise RuntimeError('binary changed since build: '+name)
    if a.build_only: return

    cases = []
    for length, n in [(-1,1000000),(16,1000000),(1024,300000),(65536,10000)]:
        cases.append(('rpc',str(length), [('before','rpc-before','ordinary'),
                     ('ordinary','rpc-after','ordinary'),('reuse','rpc-after','reuse')],n))
    for length,n in [(16,1000000),(1024,500000),(65536,40000)]:
        cases.append(('decode',str(length),[(m,'borrowed',m) for m in
                     ('owning','borrow-owning','borrow-readonly')],n))
    cases.append(('async','inline',[('before','async-before','system'),
                  ('system','async-experiment','system'),('pool','async-experiment','pool')],1000000))
    rows, observations, sums = [], [], {}
    for repeat in range(a.repeats):
        for family,case,variants,count in cases:
            n = max(1,int(count*a.scale))
            shift = repeat % len(variants)
            for variant,exe,mode in variants[shift:]+variants[:shift]:
                arguments = [mode,str(n)] if family=='async' else [mode,case,str(n)]
                command = affinity+[str(bindir/exe)]+arguments
                result = subprocess.run(command,text=True,capture_output=True,timeout=120,check=True)
                fields = result.stdout.strip().split(',')
                elapsed = float(fields[2] if family=='async' else fields[3])
                checksum = int(fields[-2]); key = (family,case,n)
                if key in sums and sums[key] != checksum:
                    raise RuntimeError('checksum mismatch: '+str(key))
                sums[key] = checksum
                rows.append(dict(family=family,case=case,variant=variant,repeat=repeat,
                                 operations=n,ns_per_op=elapsed))
                observations.append(dict(command=command,stdout=result.stdout,stderr=result.stderr))
        print('repeat',repeat+1,flush=True)
    write_csv(out/'timings.csv',rows)
    (out/'processes.json').write_text(json.dumps(observations,indent=2))
    summary=[]
    for family,case,variants,_ in cases:
        for variant,_,_ in variants:
            values=[r['ns_per_op'] for r in rows if (r['family'],r['case'],r['variant'])==(family,case,variant)]
            summary.append(dict(family=family,case=case,variant=variant,
                                median=statistics.median(values),min=min(values),max=max(values)))
    write_csv(out/'summary.csv',summary)
    # Instrumentation AFTER timing; the clock values of these binaries are ignored.
    counts=[]
    for family,case,variants,_ in cases:
        for variant,_,mode in variants:
            if variant=='before': continue
            exe={'rpc':'rpc-count','decode':'borrowed-count','async':'async-count'}[family]
            arguments=[mode,'10000'] if family=='async' else [mode,case,'10000']
            result=subprocess.run(affinity+[str(bindir/exe)]+arguments,text=True,capture_output=True,check=True,timeout=120)
            fields=result.stdout.strip().split(',')
            counts.append(dict(family=family,case=case,variant=variant,operations=10000,
                               operator_new_calls=int(fields[-1]),checksum=int(fields[-2])))
    write_csv(out/'allocation-observations.csv',counts)
    print(out/'summary.csv')


if __name__ == '__main__':
    main()
