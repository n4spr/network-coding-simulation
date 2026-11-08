#!/usr/bin/env python3
"""Verify loss-free pilot latencies against point-to-point serialization/propagation."""
import argparse
import csv
import json
from pathlib import Path

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('directory',type=Path)
args=parser.parse_args()
manifest=json.loads((args.directory/'manifest.json').read_text())
with (args.directory/'metrics.csv').open() as stream:
    rows=list(csv.DictReader(stream))
assert len(rows)==2*len(manifest['runs'])
checks=[]
tcp_times=[]
# The critical NC path traverses three normal links plus r3->r4. With
# zero loss/queuing interference, each contributes L/C serialization + delay.
frame_bytes=1024+19+8+20+2
for index,run in enumerate(manifest['runs']):
    if run['loss']!=0: continue
    nc,tcp=rows[2*index:2*index+2]
    central_bps={'1Mbps':1e6,'10Mbps':10e6}[run['rate']]
    predicted_s=3*(frame_bytes*8/10e6+0.001)+frame_bytes*8/central_bps+run['central_delay_ms']/1000
    actual_s=float(nc['completion_s'])
    assert abs(actual_s-predicted_s)<1e-9, (actual_s,predicted_s)
    assert nc['useful_bytes']==tcp['useful_bytes']=='4096'
    assert nc['data_bytes']==str(9*frame_bytes)
    assert nc['control_bytes']==str(9*(15+8+20+2))
    assert nc['central_frames']=='1' and tcp['central_frames']=='0'
    tcp_times.append(float(tcp['completion_s']))
    checks.append({'central_rate':run['rate'],'central_delay_ms':run['central_delay_ms'],
                   'predicted_nc_completion_ms':1000*predicted_s,
                   'measured_nc_completion_ms':1000*actual_s})
assert len(checks)==3
assert max(tcp_times)-min(tcp_times)<1e-9
print(json.dumps({'analytical_latency_and_byte_oracles':'PASS','cases':checks,
                  'limitation':'Loss-free, one-generation, fixed topology only.'},indent=2))
