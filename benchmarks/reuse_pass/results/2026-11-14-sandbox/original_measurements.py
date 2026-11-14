from pathlib import Path
import subprocess,csv,json,os,statistics,platform,hashlib
out=Path('/mnt/data/portal-pass2/work/benchmarks/reuse_pass/results/2026-11-14-sandbox');out.mkdir(parents=True,exist_ok=True)
root=Path('/mnt/data/portal-pass2')
cases=[]
for length,n in [(-1,1000000),(16,1000000),(1024,300000),(65536,10000)]:
 cases.append(('rpc',str(length), [('before','rpc-base','ordinary'),('ordinary','rpc-new','ordinary'),('reuse','rpc-new','reuse')],n))
for length,n in [(16,1000000),(1024,500000),(65536,40000)]:
 cases.append(('decode',str(length),[(x,'borrowed-bench',x) for x in ('owning','borrow-owning','borrow-readonly')],n))
cases.append(('async','inline',[('before','async-base','system'),('system','async-new','system'),('pool','async-new','pool')],1000000))
rows=[];raw=[]
# Each case gets seven independent processes per variant; rotate which goes first.
for repeat in range(7):
 for family,case,variants,n in cases:
  vs=variants[repeat%len(variants):]+variants[:repeat%len(variants)]
  for name,exe,mode in vs:
   args=[mode,str(n)] if family=='async' else [mode,case,str(n)]
   cmd=['taskset','-c','0',str(root/exe),*args]
   p=subprocess.run(cmd,text=True,capture_output=True,timeout=30,check=True)
   fields=p.stdout.strip().split(',')
   val=float(fields[2] if family=='async' else fields[3])
   rows.append(dict(family=family,case=case,variant=name,repeat=repeat,ns_per_op=val))
   raw.append(dict(command=cmd,stdout=p.stdout,stderr=p.stderr))
 print('repeat',repeat+1,flush=True)
with (out/'timings.csv').open('w') as f:
 w=csv.DictWriter(f,fieldnames=rows[0].keys());w.writeheader();w.writerows(rows)
(out/'processes.json').write_text(json.dumps(raw,indent=2))
summary=[]
for family,case,variants,n in cases:
 for name,exe,mode in variants:
  vals=[r['ns_per_op'] for r in rows if (r['family'],r['case'],r['variant'])==(family,case,name)]
  summary.append(dict(family=family,case=case,variant=name,median=statistics.median(vals),min=min(vals),max=max(vals)))
with (out/'summary.csv').open('w') as f:
 w=csv.DictWriter(f,fieldnames=summary[0].keys());w.writeheader();w.writerows(summary)
print(json.dumps(summary,indent=2))
