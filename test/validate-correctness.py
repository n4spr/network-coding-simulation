#!/usr/bin/env python3
"""Run deterministic coding checks; this is not a performance benchmark."""
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
known = run('known-olsr', [olsr, '--topology=butterfly', '--RngRun=1'])
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
fixed = run('fixed', [binary('network-coding-butterfly'), '--csvFile=fixed.csv', '--RngRun=1'])
assert fixed.count('[VALIDATION] Fixed NC original payload bytes verified at destination') == 2
assert re.search(r'Total Transmissions\s+9\s',fixed), 'Fixed NC data count differs from oracle'
assert re.search(r'Bottleneck Usage\s+1\s',fixed), 'Fixed NC central-link data count differs from oracle'
summary = {'unit_suite': 'PASS', 'detector_fixtures': 'PASS', 'known_olsr_xor_and_payload': 'PASS',
           'olsr_role_mappings_checked': len(mappings), 'fixed_nc_payload_and_data_counts': 'PASS',
           'limitation': 'No performance metrics or statistical gain validated.'}
(output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary,indent=2))
print(f'Logs: {output}')
