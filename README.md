# Bachelor Thesis project: developing a Network Coding Simulation 

The retained examples are the fixed seven-node butterfly prototype (two destinations)
and the OLSR random-graph experiment (one destination). Work is on `agent/nct`.

From the ns-3.44 root, in WSL:

```bash
python3 src/network-coding/test/validate-correctness.py
./ns3 run "network-coding-butterfly --bottleneckDataRate=1Mbps --csvFile=/tmp/butterfly-1-v2.csv"
./ns3 run "network-coding-butterfly --bottleneckDataRate=10Mbps --csvFile=/tmp/butterfly-10-v2.csv"
python3 src/network-coding/test/run-bottleneck-pilot.py
```

Use a new CSV filename: schema 2 changes the measurement definitions and rejects
old headers. The fixed prototype supports exactly one two-packet generation.
The rate comparison retains a 10 ms central propagation delay; add
`--bottleneckDelay=1` at 10 Mbps for homogeneous link delays.

See [measurement definitions](docs/measurement-contract.md),
[validation plan](docs/validation-plan.md), and
[thesis handoff](docs/thesis-agent-note.md). Build/run success and mechanism checks
are distinct from a statistical claim that coding improves performance.
