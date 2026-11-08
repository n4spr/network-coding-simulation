# Near-term validation plan

Baseline: commit 4a0b62e on agent/nct, ns-3.44 in WSL. This is a validation plan, not a statement that existing result tables are valid. Work remains on agent/nct. The full parameter sweep follows review of a pilot.

## 1. Establish coding and topology correctness

- Replace the unit test's random-device erasures with repeatable, explicit packet delivery schedules. Check independent equations, repeated equations, insufficient rank, out-of-order delivery, and exact decoded payload bytes. Insufficient rank must produce an incomplete generation, not a test expecting guaranteed recovery.
- Add an independent polynomial-arithmetic oracle to the field regression tests. Keep the implemented 0x11d representation; compare all 65,536 multiplication inputs and the 255 nonzero inverses. Do not switch field representations to satisfy an AES test vector.
- Validate exactly one generation of two distinct 1024-byte packets in the fixed seven-node butterfly. With zero loss, the intended NC path requires 9 data-link transmissions: S=2, r1=2, r2=2, r3=1, r4=2. Expect 1 coded data transmission on r3->r4 and 2048 useful bytes at each destination. These are an oracle for the intended forwarding schedule, to reconcile against traces; they exclude ACK/control traffic.
- Validate payload identity, duplicate suppression, and bounded retry behavior under prescribed data and ACK losses. Treat exhausted retries and deadlines as explicit failure outcomes.
- Add an injectable known topology for the OLSR app: a positive butterfly, a graph lacking the required edges, and a disconnected pair. Verify discovered roles/edges against a separate graph description after route convergence. Require a trace showing XOR activation, correct side information, and correct final payload; a completed unicast with zero XOR is only a smoke test.

Gate: deterministic correctness tests pass, fixed topology data counts reconcile, and a known OLSR scenario exercises the coding path. Keep the prototype's packet/generation settings bounded to supported behavior until multi-generation correctness is established.

## 2. Make the measurement contract consistent

- Compare the same payload workload, topology, directed link resources, error assumptions, and observation window. Keep TCP as a practical transport comparison; add uncoded forwarding with matched hop reliability to isolate the effect of coding from routing and retransmission policy.
- Count unique original payload bytes at each destination. For TCP, count application stream bytes, independent of segment/callback boundaries. For NC, count successfully reconstructed originals once. Record delivered fraction and destination completion separately.
- Record first offered data time and each destination completion time. Fixed-butterfly completion requires both destinations. Stop traffic at its deadline and allow queues/control traffic to drain. Do not substitute the entire simulation duration, including OLSR convergence, for transfer completion time. Failed completion remains missing/censored, not zero or an apparently fast transfer.
- Use the same NetDevice transmission trace for both methods to count per-hop frames and actual bytes, including control and retransmissions. Measure r3->r4 directly when that link exists; coding activity is not a bottleneck-usage counter. Label total transmitted bytes across links as aggregate network traffic, not end-to-end throughput or capacity.
- Use FlowMonitor's actual rxBytes and delaySum/rxPackets for explicitly selected IP flows. Label IP-hop/application-forwarding flows separately from application end-to-end latency. Collect OLSR broadcast overhead through device traces. Hand-check a small trace or PCAP against counters before trusting CSV aggregation.

Gate: publish a metric dictionary (layer, flow filter, numerator, denominator, units, duplicate policy) and reconcile independently counted totals. Version CSV output because old metric meanings change.

## 3. Run a reproducible pilot, then agree the full sweep

- First rerun deterministic zero-loss cases, then exercise fixed-butterfly packet erasure probabilities 0, 0.01, 0.05, 0.1. Proposed random-graph pilot: 7, 10, 15 nodes with edge probability 0.2, 0.4, 0.6. These are engineering pilot ranges, not literature-validated realism claims.
- Use 10 independent scenario replications per stochastic pilot cell, with the same saved topology/workload for paired method comparisons. Manage topology randomness separately from ns-3 seed/run/stream assignments. Replaying a manifest must reproduce outcomes.
- Save the commit, all parameters, edge list, source/destinations, RNG identities and seeds/run numbers, timestamps, exit status, measured outcomes, and failure reason. Count rejected graphs and destination reselections; the current reachable-pair reselection changes the population sampled.
- Report detection frequency, coding activation conditional on detection, payload correctness/completion, and traffic cost separately. Preserve disconnected, timeout, and failed runs. Estimate variability across independent scenarios, not across packets of one run. For method effects report paired differences; use a 95% interval across scenario pairs, with a Wilson interval for completion proportions. Report completion time conditional on success alongside failure rate, so failures cannot improve the mean silently.
- Use pilot runtime, failure frequency, and interval widths to propose the final ranges and repetition count for user review. Ten pilot repetitions are not a precision guarantee. Do not start the full campaign yet.

Grounding and caveats are recorded in thesis-agent-note.md. Add permanent tests and metric instrumentation before claiming either protocol's performance advantage.
