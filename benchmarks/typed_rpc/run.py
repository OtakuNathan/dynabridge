#!/usr/bin/env python3
"""Serial, randomized matched process trials; no compilation during timing."""
import argparse,csv,hashlib,json,os,pathlib,platform,random,statistics,subprocess,time
p=argparse.ArgumentParser()
p.add_argument('--binary',required=True,type=pathlib.Path)
p.add_argument('--allocations',required=True,type=pathlib.Path)
p.add_argument('--out',required=True,type=pathlib.Path)
p.add_argument('--repetitions',type=int,default=7)
p.add_argument('--seed',type=int,default=20261114)
p.add_argument('--cpu',type=int)
a=p.parse_args()
if a.repetitions<1:p.error('repetitions must be positive')
for f in (a.binary,a.allocations):
    if not f.is_file():p.error(f'missing binary: {f}')
a.binary=a.binary.resolve();a.allocations=a.allocations.resolve()
a.out.mkdir(parents=True,exist_ok=True)
allowed=sorted(os.sched_getaffinity(0)) if hasattr(os,'sched_getaffinity') else []
if allowed:
    cpu=a.cpu if a.cpu is not None else allowed[0]
    if cpu not in allowed:p.error('requested CPU is outside allowed affinity')
    os.sched_setaffinity(0,{cpu})
else:cpu=None
meta={'platform':platform.platform(),'machine':platform.machine(),'libc':platform.libc_ver(),
      'allowed_cpus':allowed,'selected_cpu':cpu,'seed':a.seed,'repetitions':a.repetitions,
      'allocator_env':{k:v for k,v in os.environ.items() if k.startswith(('MALLOC_','GLIBC_TUNABLES'))},
      'binaries':{str(x):hashlib.sha256(x.read_bytes()).hexdigest() for x in (a.binary,a.allocations)}}
if pathlib.Path('/proc/cpuinfo').exists():
    meta['cpu_model']=next((s.split(':',1)[1].strip() for s in pathlib.Path('/proc/cpuinfo').read_text().splitlines() if s.startswith('model name')),'unknown')
(a.out/'environment.json').write_text(json.dumps(meta,indent=2)+'\n')
lanes=['dynamic_fresh','dynamic_reuse','legacy_static','typed_reuse','typed_fresh','hand_reference']
cases={0:2_000_000,16:500_000,1024:200_000,65536:5_000}
rng=random.Random(a.seed);rows=[];raw=[];checks={}
def run(binary,lane,size,n,mode,rep):
    cmd=[str(binary),lane,str(size),str(n),mode];start=time.time()
    r=subprocess.run(cmd,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=120)
    record={'command':cmd,'round':rep,'elapsed_s':time.time()-start,'returncode':r.returncode,'stdout':r.stdout,'stderr':r.stderr}
    raw.append(record)
    if r.returncode:raise RuntimeError(record)
    data=json.loads(r.stdout);key=(size,n)
    if key in checks and data['checksum']!=checks[key]:raise RuntimeError('checksum mismatch')
    checks[key]=data['checksum'];data['round']=rep;data['mode']=mode
    return data
for rep in range(a.repetitions):
    sizes=list(cases);rng.shuffle(sizes)
    for size in sizes:
        order=lanes.copy();rng.shuffle(order)
        for lane in order:rows.append(run(a.binary,lane,size,cases[size],'timing',rep))
    # Save incrementally but do not silently drop failed or slower trials.
    (a.out/'processes.json').write_text(json.dumps(raw,indent=2)+'\n')
    print(f'completed paired round {rep+1}/{a.repetitions}',flush=True)
for size in cases:
    for lane in lanes:rows.append(run(a.allocations,lane,size,1000 if size<65536 else 100,'allocation',-1))
(a.out/'processes.json').write_text(json.dumps(raw,indent=2)+'\n')
with (a.out/'samples.csv').open('w',newline='') as f:
    w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
summary=[]
for size in cases:
    base={r['round']:r['ns_per_call'] for r in rows if r['mode']=='timing' and r['payload']==size and r['lane']=='dynamic_reuse'}
    baseline=statistics.median(base.values())
    for lane in lanes:
        obs=[r for r in rows if r['mode']=='timing' and r['payload']==size and r['lane']==lane]
        values=[r['ns_per_call'] for r in obs];alloc=next(r for r in rows if r['mode']=='allocation' and r['payload']==size and r['lane']==lane)
        summary.append({'payload':size,'lane':lane,'median_ns':statistics.median(values),'min_ns':min(values),'max_ns':max(values),
                        'change_vs_dynamic_reuse_pct':100*(statistics.median(values)/baseline-1),
                        'faster_pairs':sum(r['ns_per_call']<base[r['round']] for r in obs),'pairs':len(obs),
                        'allocations_per_call':alloc['allocations']/alloc['iterations'],
                        'requested_bytes_per_call':alloc['requested_bytes']/alloc['iterations']})
with (a.out/'summary.csv').open('w',newline='') as f:
    w=csv.DictWriter(f,fieldnames=list(summary[0]));w.writeheader();w.writerows(summary)
print(json.dumps(summary,indent=2))
