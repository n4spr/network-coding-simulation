# Fixed-butterfly bottleneck pilot

Two destinations; two 1024-byte originals. Rates changed via CLI. These observations combine coding, routing, hop reliability and TCP behavior; they do not isolate coding capacity.

| Central rate | Central delay (ms) | Erasure probability | Runs | NC / TCP successes | NC / TCP mean completion (ms), successful runs | NC / TCP mean PPP bytes |
|---|---:|---:|---:|---:|---:|---:|
| 1Mbps | 10 | 0 | 1 | 1/1 | 24.1592/8.5120 | 10062.0/11032.0 |
| 1Mbps | 10 | 0.01 | 10 | 10/10 | 124.1592/409.4645 | 10276.6/11392.2 |
| 1Mbps | 10 | 0.05 | 10 | 10/10 | 374.1592/1111.0565 | 11148.5/12471.2 |
| 1Mbps | 10 | 0.1 | 10 | 10/10 | 474.1592/2211.6781 | 11698.5/13575.0 |
| 10Mbps | 10 | 0 | 1 | 1/1 | 16.4336/8.5120 | 10062.0/11032.0 |
| 10Mbps | 10 | 0.01 | 10 | 10/10 | 116.4336/409.4645 | 10276.6/11392.2 |
| 10Mbps | 10 | 0.05 | 10 | 10/10 | 366.4336/1111.0565 | 11148.5/12471.2 |
| 10Mbps | 10 | 0.1 | 10 | 10/10 | 466.4336/2211.6781 | 11698.5/13575.0 |
| 10Mbps | 1 | 0 | 1 | 1/1 | 7.4336/8.5120 | 10062.0/11032.0 |

Completion means condition on success; failures remain in the CSV and success counts. All PPP traffic started after offer through the fixed simulation horizon is counted, including the drain interval. Completion goodput sums useful bytes over both receivers; frame bytes sum over directed links.

Ten lossy repetitions per cell are a pilot, not evidence of near-perfect reliability. The JSON includes Wilson completion intervals and mean paired time differences among pairs where both methods completed. Use repeated scenarios and a precision target to design any full campaign. The fixed TCP routes bypass r3->r4, so a slower central link cannot be assumed to disadvantage TCP. NC uses a 0.5 s hop-ACK timeout; TCP has different loss recovery. Loss-case differences cannot be attributed solely to coding.

The 10 Mbps / 10 ms case removes only the rate restriction. The additional 10 Mbps / 1 ms case has homogeneous link rate and propagation delay.

Independent analytical check: the critical NC path has three normal links and
one central link. For a modeled PPP data frame of 1073 bytes,
`T = 3 × (1073 × 8 / 10^7 + 0.001) + 1073 × 8 / central_bps + central_delay_s`.
This predicts all three loss-free NC completion times to within 1 ns. See
`analytic-check.json` and `test/verify-bottleneck-pilot.py`.

No-loss data/control breakdown: NC sends 9657 data bytes and 405 control bytes;
TCP sends 9056 data bytes and 1976 control bytes (modeled PPP bytes summed over
links). Thus the smaller NC total in this fixture is not a reduction in data-only
traffic. TCP segmentation, connection setup, acknowledgments and shutdown matter.
