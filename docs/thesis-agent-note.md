# Thesis handoff: scientific validation status

Baseline: agent/nct at 4a0b62e. The local main.md is a draft, not validated evidence. Two retained experiments exist: the fixed seven-node/two-destination butterfly and a static random point-to-point graph with OLSR and one selected destination. Build/run smoke tests passed previously, but the random smoke case used no XOR. It does not validate the coding path. No performance superiority has been established.

## Source-grounded interpretation

1. Ahlswede, Cai, Li and Yeung, *Network Information Flow*, IEEE TIT 46(4), 1204–1216 (2000), DOI [10.1109/18.850663](https://doi.org/10.1109/18.850663); [paper](https://web.stanford.edu/class/ee388/REFS/ahlswede2000network.pdf). Its single-source multicast result is a theoretical reference under a directed, capacity-constrained network model. It does not predict the performance of this TCP comparison. Check edge directions, allowed paths, capacities, and receiver requirements before invoking the butterfly capacity argument.
2. Fragouli, Le Boudec and Widmer, *Network Coding: An Instant Primer*, ACM SIGCOMM CCR 36(1), 63–68 (2006), DOI [10.1145/1111322.1111337](https://doi.org/10.1145/1111322.1111337). Local copy: `bib/tesi/fragouli nc instant primer.pdf`, especially the finite-field discussion and Figure 2. Addition is XOR; multiplication depends on the irreducible polynomial representation. The illustrative network differs from our single-source graph, so its numerical rate claims must not be copied into our results.
3. [ns-3 FlowMonitor documentation](https://www2.nsnam.org/docs/models/html/flow-monitor.html), checked against this installation's `src/flow-monitor/doc/flow-monitor.rst`. It measures unicast TCP/UDP at IP level, includes IP headers but excludes L2 headers, and sees TCP retransmissions as new IP packets. It does not account for all broadcast routing traffic. Allow traffic to drain before simulation termination. Citation for the framework: Carneiro, Fortuna and Ricardo (2009), DOI [10.4108/ICST.VALUETOOLS2009.7493](https://doi.org/10.4108/ICST.VALUETOOLS2009.7493).
4. [ns-3.44 random-variable manual](https://www2.nsnam.org/docs/release/3.44/manual/html/random-variables.html): document RNG, seed, run numbers and independent replications. The application also uses std::mt19937 for graph generation; an ns-3 run number alone does not describe all randomness.

The documentation web page in item 3 is the current model guide; the installed 3.44 source is the version-specific reference used here. Some local PDFs named `it-ahlsw*.pdf` are edited books, not necessarily the original 2000 article: verify title pages rather than assigning citations by filename.

## Initial validation evidence

- An independent shift/XOR polynomial oracle modulo 0x11d agreed with the current field implementation for all 65,536 products and 255 nonzero inverses. This checks those operations, not the entire encoder/decoder or protocol. Temporary harness/log: `/tmp/nct-field-oracle.cc` and `/tmp/nct-field-oracle.log`; promote it to permanent regression coverage next.
- The original 0x53 * 0xca test expected 0x01 (0x11b representation). Under the implemented 0x11d representation the product is 0x8f. The corrected field test passes.
- The packet-loss unit test does not justify guaranteed decoding: for K=8, p=0.1 it sends only 8 coded packets. With independent erasures, P(at least 8 arrive)=0.9^8=0.430467, even before checking equation independence. Its assertion received<sent also rejects the valid no-erasure outcome. This explains invalid test expectations; it does not establish that the decoder is bug-free.
- The draft says 10 Mbps on the bottleneck, but the fixed example defaults to 1 Mbps. Reconcile recorded configurations with actual execution before regenerating tables.
- Current random-graph results mix application send counts for NC with IP packet counts for TCP, derive throughput from nominal payload size, and use IP received bytes for goodput. OLSR convergence is included in the total simulation duration. Those definitions cannot support a fair coding-gain claim.

## Suggested wording and measurement policy

“Application instrumentation measured uniquely reconstructed payload, while device traces measured per-link traffic cost. IP flow statistics provided a separate network-layer view. Each metric states its observation point and time interval, and small deterministic cases cross-check the collectors.” This is intended methodology; write it in past tense only once implemented and checked. Manual counters are defensible when their semantics and validation are explicit; a helper or built-in collector does not automatically make a metric correct.

Use goodput = 8 × unique useful payload bytes / stated active observation duration. For two-receiver multicast, state whether bytes are summed across destinations (receiver-summed goodput) or counted once as a common delivered object. Report full completion separately. Call summed per-link transmitted bytes “aggregate network traffic”; use measured link byte totals for traffic cost and utilization with the correct link capacity/time denominator. Do not present IP-hop delay as source-to-decoded-original latency.

A TCP-versus-NC comparison combines coding, reliability, routing and transport choices. A matched uncoded-forwarding baseline is needed to attribute differences to coding. OLSR on a random static wired graph is broader than one hand-built topology, but does not establish Internet/VANET realism: there is no wireless interference or mobility model here. Theoretical multicast capacity, finite-file completion time, and practical protocol overhead are distinct claims.

Next steps and acceptance gates: validation-plan.md. Existing thesis tables remain provisional until those gates pass. Do not invent completed runs, confidence intervals, optimality, or real-world validation from this note.
