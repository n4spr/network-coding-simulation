#!/usr/bin/env python3
"""Run deterministic coding checks; this is not a performance benchmark."""
import csv
import struct
import argparse
import json
import re
import subprocess
import tempfile
from pathlib import Path

project = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path)
args = parser.parse_args()
output = args.output.resolve() if args.output else Path(tempfile.mkdtemp(prefix='nc-validation-'))
output.mkdir(parents=True, exist_ok=True)
if any(output.iterdir()):
    raise RuntimeError(f"Use an empty output directory to preserve run artifacts: {output}")
root = project.parent.parent

def run(name, command, cwd=output):
    result = subprocess.run(command, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, errors='replace', timeout=180)
    (output / f'{name}.log').write_text(result.stdout)
    if result.returncode:
        raise RuntimeError(f'{name} exited {result.returncode}; see {output / (name + ".log")}')
    return result.stdout

def binary(target, folder='src/network-coding/examples'):
    matches = list((root/'build'/folder).glob(f'ns3.*-{target}-default'))
    if len(matches) != 1:
        raise RuntimeError(f'Expected one default-profile binary for {target}: {matches}')
    return str(matches[0])

run('build', [str(root/'ns3'), 'build', 'network-coding-test', 'network-coding-butterfly',
              'network-coding-butterfly-oslr', 'test-runner'], cwd=root)
suite = run('unit', [binary('test-runner', 'utils'), '--suite=network-coding', '--verbose'])
assert 'PASS network-coding' in suite, 'Unit suite did not pass'
olsr = binary('network-coding-butterfly-oslr')
detector = run('detector', [olsr, '--validateDetector=1'])
assert '[VALIDATION] Detector fixtures: PASS' in detector
known = run('known-olsr', [olsr, '--topology=butterfly', '--RngRun=1', '--tracePrefix=known', '--metricsFile=olsr.csv'])
assert re.search(r'XOR activation count \(node 0\): 1\b', known), 'Source did not activate coding'
assert 'BUTTERFLY R3->R4' in known, 'Coded packet did not use central path'
assert '[VALIDATION] NC original payload bytes verified' in known, 'NC payload not verified'
assert known.count('[VALIDATION] TCP original payload bytes verified:') == 2
assert known.count('*** SUCCESS: Destination node 5 decoded successfully! ***') == 2
# Check discovered role assignments against the fixture edge list, independently
# of the Boost matching callback and its internal adjacency representation.
edges = {frozenset(edge) for edge in [(0,1),(0,2),(1,3),(1,5),(2,3),(2,6),(3,4),(4,5),(4,6)]}
mappings = re.findall(r'Found REAL butterfly.*s=(\d+) r1=(\d+) r2=(\d+) r3=(\d+) r4=(\d+) d1=(\d+) d2=(\d+)', known)
assert mappings, 'No OLSR-derived motif mapping found'
for mapping in mappings:
    s,r1,r2,r3,r4,d1,d2 = map(int,mapping)
    assert len({s,r1,r2,r3,r4,d1,d2}) == 7
    for edge in [(s,r1),(s,r2),(r1,r3),(r2,r3),(r1,d1),(r2,d2),(r3,r4),(r4,d1),(r4,d2)]:
        assert frozenset(edge) in edges, f'Nonexistent edge in mapping: {edge}'
fixed = run('fixed', [binary('network-coding-butterfly'), '--csvFile=fixed.csv', '--RngRun=1', '--tracePrefix=fixed', '--enablePcap=1'])
assert fixed.count('[VALIDATION] Fixed NC original payload bytes verified at destination') == 2
assert re.search(r'NC app data transmissions: 9\b',fixed), 'Fixed NC data count differs from oracle'
assert re.search(r'NC app r3 data transmissions: 1\b',fixed), 'Fixed NC central-link data count differs from oracle'
def rows(name):
    with (output/name).open() as stream:
        return list(csv.DictReader(stream))

def check_trace(row, filename):
    events = rows(filename)
    active = [event for event in events if event['phase']=='transfer']
    before = [event for event in events if event['phase']=='pre']
    assert len(active)==int(row['transfer_frames'])
    assert sum(int(event['bytes']) for event in active)==int(row['transfer_bytes'])
    assert len(before)==int(row['pre_frames'])
    assert sum(int(event['bytes']) for event in before)==int(row['pre_bytes'])
    for kind in ('data','control','routing'):
        selected=[event for event in active if event['kind']==kind]
        assert len(selected)==int(row[kind+'_frames'])
        assert sum(int(event['bytes']) for event in selected)==int(row[kind+'_bytes'])
    if row['central_frames']:
        selected=[event for event in active if event['source']=='3' and event['destination']=='4']
        assert len(selected)==int(row['central_frames'])
        assert sum(int(event['bytes']) for event in selected)==int(row['central_bytes'])

for row in rows('olsr.csv'):
    method='nc' if row['method']=='NC' else 'tcp'
    check_trace(row, f'known-{method}-{row["graph_seed"]}.csv')
    assert row['required']=='1' and row['useful_bytes']=='2048' and row['success']=='1'
    assert float(row['offer_s'])==55
    assert row['xor_operations']==('1' if method=='nc' else '0')

# Independent PCAP parser: in this loss-free drained fixture every transmitted
# PPP frame is captured once at Tx and once at Rx across all device captures.
def pcap_totals(prefix):
    count=size=0
    for path in output.glob(prefix+'-*.pcap'):
        data=path.read_bytes()
        endian = '<' if data[:4]==b'\xd4\xc3\xb2\xa1' else '>'
        assert data[:4] in (b'\xd4\xc3\xb2\xa1', b'\xa1\xb2\xc3\xd4')
        assert struct.unpack(endian+'I',data[20:24])[0]==9, 'Expected PPP link type'
        offset=24
        while offset<len(data):
            _,_,captured,original=struct.unpack(endian+'IIII',data[offset:offset+16])
            assert captured==original, 'Unexpected truncated capture'
            count+=1;size+=original;offset+=16+captured
        assert offset==len(data)
    assert count>0, 'No captures found'
    return count,size

for row in rows('fixed.csv'):
    method='nc' if row['method']=='XOR Network Coding' else 'tcp'
    check_trace(row, 'fixed-'+method+'.csv')
    assert row['useful_bytes']=='4096' and row['success']=='1' and float(row['offer_s'])==1
    captured_count,captured_bytes=pcap_totals('butterfly-xor' if method=='nc' else 'butterfly-tcp')
    assert captured_count==2*int(row['transfer_frames'])
    assert captured_bytes==2*int(row['transfer_bytes'])
    assert row['xor_operations']==('1' if method=='nc' else '0')
    if method=='nc':
        assert row['data_frames']=='9' and row['control_frames']=='9'
        assert int(row['data_bytes'])==9*(1024+19+8+20+2)
        assert int(row['control_bytes'])==9*(15+8+20+2)

for loss in ('data','ack'):
    run('loss-'+loss,[binary('network-coding-butterfly'),'--validationDrop='+loss,
                     '--csvFile=loss-'+loss+'.csv','--RngRun=1'])
    nc=rows('loss-'+loss+'.csv')[0]
    assert nc['success']=='1' and nc['useful_bytes']=='4096'
    assert nc['injected_app_drops']=='1' and nc['data_frames']=='10', 'Expected exactly one retransmission'

run('all-lost',[binary('network-coding-butterfly'),'--errorRate=1','--maxRetransmissions=2',
                '--csvFile=all-lost.csv','--simulationTime=4','--RngRun=1'])
failed=rows('all-lost.csv')
for row in failed:
    assert row['success']=='0' and row['useful_bytes']=='0' and row['completion_s']==''
assert failed[0]['data_frames']=='6', 'Two originals with initial send and two retries each'
summary = {'unit_suite': 'PASS', 'detector_fixtures': 'PASS', 'known_olsr_xor_and_payload': 'PASS',
           'olsr_role_mappings_checked': len(mappings), 'fixed_nc_payload_and_data_counts': 'PASS',
           'trace_and_independent_pcap_accounting': 'PASS', 'data_and_ack_loss_recovery': 'PASS',
           'bounded_retry_exhaustion': 'PASS',
           'limitation': 'Fixture-level validation; no statistical coding-gain conclusion.'}
(output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary,indent=2))
print(f'Logs: {output}')
