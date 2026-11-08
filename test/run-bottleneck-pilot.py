#!/usr/bin/env python3
"""Small fixed-butterfly pilot; keep random-graph study separate and unicast."""
import argparse
import csv
import hashlib
import json
import math
import statistics
import subprocess
import tempfile
import tarfile
import platform
from pathlib import Path

project=Path(__file__).resolve().parents[1]
root=project.parent.parent
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path)
args=parser.parse_args()
output=args.output.resolve() if args.output else Path(tempfile.mkdtemp(prefix='nc-rate-pilot-'))
output.mkdir(parents=True,exist_ok=True)
if any(output.iterdir()): raise RuntimeError('Output directory must be empty')
binary=root/'build/src/network-coding/examples/ns3.44-network-coding-butterfly-default'
# One deterministic baseline per condition; independent run numbers for lossy cells.
cells=[(rate,10.0,loss,rep) for rate in ('1Mbps','10Mbps')
       for loss in (0,.01,.05,.1) for rep in (range(1,11) if loss else [1])]
cells += [('10Mbps',1.0,0,1)]
manifest={'study':'fixed two-destination prototype; rate pilot, not main unicast OLSR study',
          'base_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=project,text=True).strip(),
          'source_sha256':{},'runs':[],
          'ns3_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
          'compiler':subprocess.check_output(['g++','--version'],text=True).splitlines()[0],
          'platform':platform.platform(), 'logs_archive':'logs.tar.gz'}
for path in sorted(list((project/'examples').glob('*'))+list((project/'model').glob('*'))):
    if path.is_file(): manifest['source_sha256'][str(path.relative_to(project))]=hashlib.sha256(path.read_bytes()).hexdigest()
for index,(rate,delay,loss,rep) in enumerate(cells):
    command=[str(binary),'--bottleneckDataRate='+rate,'--bottleneckDelay='+str(delay),
             '--normalDataRate=10Mbps','--linkDelay=1','--packetSize=1024','--generationSize=2',
             '--totalPackets=2','--simulationTime=10','--cleanupTime=1','--maxRetransmissions=3',
             '--errorRate='+str(loss),'--RngSeed=1','--RngRun='+str(rep),'--csvFile=metrics.csv']
    result=subprocess.run(command,cwd=output,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
                          text=True,timeout=30)
    name=f'run-{index:03d}.log';(output/name).write_text(result.stdout)
    manifest['runs'].append({'index':index,'rate':rate,'central_delay_ms':delay,'loss':loss,
                             'rng_run':rep,'command':command,'exit_code':result.returncode,'log':name})
    if result.returncode: raise RuntimeError(f'Run failed: {name}')
with tarfile.open(output/'logs.tar.gz','w:gz') as archive:
    for path in sorted(output.glob('run-*.log')): archive.add(path,arcname=path.name)
(output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
with (output/'metrics.csv').open() as stream: rows=list(csv.DictReader(stream))
assert len(rows)==2*len(cells)
# Keep condition metadata including delay, which is recorded in the manifest.
groups={}
for index,cell in enumerate(cells):
    nc,tcp=rows[2*index:2*index+2]
    assert nc['method']=='XOR Network Coding' and tcp['method']=='Traditional TCP/IP'
    assert nc['rng_run']==tcp['rng_run']==str(cell[3])
    groups.setdefault(cell[:3],[]).append((nc,tcp))
report=['# Fixed-butterfly bottleneck pilot','',
        'Two destinations; two 1024-byte originals. Rates changed via CLI. '+
        'These observations combine coding, routing, hop reliability and TCP behavior; they do not isolate coding capacity.', '',
        '| Central rate | Central delay (ms) | Erasure probability | Runs | NC / TCP successes | NC / TCP mean completion (ms), successful runs | NC / TCP mean PPP bytes |',
        '|---|---:|---:|---:|---:|---:|---:|']
summary=[]
for (rate,delay,loss),pairs in groups.items():
    entry={'central_rate':rate,'central_delay_ms':delay,'error_rate':loss,'runs':len(pairs)}
    for label,column in [('nc',0),('tcp',1)]:
        values=[pair[column] for pair in pairs];ok=[v for v in values if v['success']=='1']
        entry[label+'_successes']=len(ok)
        entry[label+'_mean_completion_ms']=1000*statistics.mean(float(v['completion_s']) for v in ok) if ok else None
        entry[label+'_mean_ppp_bytes']=statistics.mean(int(v['transfer_bytes']) for v in values)
        # Wilson score interval for completion, descriptive pilot uncertainty.
        n=len(values);p=len(ok)/n;z=1.959963984540054
        center=(p+z*z/(2*n))/(1+z*z/n)
        half=z*math.sqrt(p*(1-p)/n+z*z/(4*n*n))/(1+z*z/n)
        entry[label+'_completion_wilson95']=[max(0,center-half),min(1,center+half)] if n>1 else None
    paired=[1000*(float(nc['completion_s'])-float(tcp['completion_s']))
            for nc,tcp in pairs if nc['success']=='1' and tcp['success']=='1']
    entry['paired_successes']=len(paired)
    entry['mean_paired_nc_minus_tcp_ms']=statistics.mean(paired) if paired else None
    summary.append(entry)
    report.append(f'| {rate} | {delay:g} | {loss:g} | {len(pairs)} | '+
                  f'{entry["nc_successes"]}/{entry["tcp_successes"]} | '+
                  f'{entry["nc_mean_completion_ms"]:.4f}/{entry["tcp_mean_completion_ms"]:.4f} | '+
                  f'{entry["nc_mean_ppp_bytes"]:.1f}/{entry["tcp_mean_ppp_bytes"]:.1f} |')
report += ['', 'Completion means condition on success; failures remain in the CSV and success counts. '+
           'All PPP traffic started after offer through the fixed simulation horizon is counted, including the drain interval. '+
           'Completion goodput sums useful bytes over both receivers; frame bytes sum over directed links.', '',
           'Ten lossy repetitions per cell are a pilot, not evidence of near-perfect reliability. '+
           'The JSON includes Wilson completion intervals and mean paired time differences among pairs where both methods completed. '+
           'Use repeated scenarios and a precision target to design any full campaign. '+
           'The fixed TCP routes bypass r3->r4, so a slower central link cannot be assumed to disadvantage TCP. '+
           'NC uses a 0.5 s hop-ACK timeout; TCP has different loss recovery. Loss-case differences cannot be attributed solely to coding.', '',
           'The 10 Mbps / 10 ms case removes only the rate restriction. '+
           'The additional 10 Mbps / 1 ms case has homogeneous link rate and propagation delay.']
(output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
(output/'report.md').write_text('\n'.join(report)+'\n')
print('\n'.join(report));print(f'Artifacts: {output}')
