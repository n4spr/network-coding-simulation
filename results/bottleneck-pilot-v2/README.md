# Bottleneck pilot artifacts

Generated using implementation commit `bc856fb`; `manifest.json` pins the ns-3
commit, compiler, platform, model/example source hashes, CLI parameters and RNG
run numbers. These are fixed two-destination prototype results, separate from the
single-destination OLSR study. Read `report.md` and `docs/measurement-contract.md`.

- `metrics.csv`: all 126 method-level rows, including explicit completion status.
- `manifest.json`: 63 paired cases; exact commands and source provenance.
- `summary.json`: descriptive aggregates, paired successful completion differences,
  and Wilson intervals for the ten-repetition loss cells.
- `logs.tar.gz`: raw simulator output referenced by the manifest.
- `analytic-check.json`: independent no-loss latency and byte-oracle check.
- `validation-summary.json`: unit, known-topology, accounting and retry checks.
- `validation-fixtures.tar.gz`: the fixture's event traces, PCAPs, XML diagnostics
  and logs. This is a validation run, not another pilot replicate.

To regenerate after building in WSL, run `python3 test/run-bottleneck-pilot.py`.
It creates a new output directory; it never mixes new rows into these artifacts.
Verify the supplied data with:

```bash
python3 test/verify-bottleneck-pilot.py results/bottleneck-pilot-v2
```

Simulation paths in the manifest describe the original workspace. Logs named
there are available inside `logs.tar.gz`. The pilot script locates the current
workspace automatically. The report's analytical explanation is a write-up
addition after raw result generation; raw metric rows are unchanged.
