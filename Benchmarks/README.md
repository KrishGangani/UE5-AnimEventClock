# P06 · Benchmarks

Every number in this project's write-up must trace back to a file in this folder. The method is in [../Explanation/01_Problem_Analysis.md §7](../Explanation/01_Problem_Analysis.md). The results, batch by batch, are in [../Explanation/09_Testing_And_Benchmarks.md](../Explanation/09_Testing_And_Benchmarks.md).

## Measurement rules

1. **Baselines first.** NQ (Queued), NB (BranchingPoint), T (timers) and G (GAS) are captured before the event clock exists (steps S3–S4), on the same workload, build and machine as every later run. The **problem gate** (`01` §6.4) is applied to them before any claim is made.
2. **Packaged Development build** for clients, listen servers and standalone runs (never PIE, never the editor UI). The launcher engine can't build Test or a Server target, so **dedicated servers run on the editor binary with `-server`** (`01b #14`).
3. **Single-process runs use the fixed step** (`-deterministic -FPS=30/60/144`); timing error is measured in **game time**, which the fixed step makes exact. **Network runs are real time** with a frame cap (`t.MaxFPS`), because emulated latency runs on real time (`01b` B-1); timing error is measured on the shared **wall clock** (`QueryPerformanceCounter`). Latency L = `-PktLag=L/2` on both processes.
4. The capture window is opened and closed by the game itself (`BenchStart` / `BenchEnd` CSV events), never by a timer.
5. **Every timed process is pinned to the performance cores.** The runner pins **itself** once with `pin_self(performance_core_mask())` (`Tools\cpu_affinity.py`; mask `0xFFFF` on this machine) before recording the environment or launching anything, records `applied_mask()`, then launches the **real** executable with `subprocess.Popen`, which inherits the mask from its first instruction. It waits with a timeout and kills the process tree in a `finally` block. Never through `cmd /c start /wait`, which always returns 0.
6. **One discarded priming run per batch**, then **runs per combination = number of configurations**, interleaved, with the configuration order rotated each round. Combinations that no success criterion needs are skipped.
7. **Statistics:** per run, median, p95 and p99 (linear-interpolation percentiles) plus min and max of the timing error, and missed / duplicate events per 1,000; then the median across runs with the [min–max] across runs. Repeated fixed-step runs must agree exactly; any difference is reported. FPS is never used as evidence.
8. **The environment is recorded with every batch** (below), automatically, by the runner.

## Folders

| Folder | What goes in it | Rule |
|---|---|---|
| `Raw\` | Per-event CSVs (`_events.csv`), expected-event lists (`_expected.csv`), per-frame server time logs (`_frames.csv`), CSV profiler captures (`.csv`), game logs (`.log`), one `_manifest.json` per batch | **Never edited.** Only the manifests are in git (exact command lines, environment, per-run statistics). Captures cited in the docs are attached as `<batch>_raw.zip` to this repository's **Releases** page |
| `Summaries\` | Computed statistics, SC1–SC7 and problem-gate tables, charts | Each summary names the batch it was computed from |

## File naming

`<batch>_<Config>_<Condition>_<Rate>_r<run>[_<role>].<ext>`, with `<batch>` = `YYYY-MM-DD_HHMMSS[_label]`; the runner refuses to overwrite.

- `<Config>`: `NQ` Queued notifies · `NB` BranchingPoint notifies · `T` timers · `G` GAS · `AEC` AnimEventClock (in single-process runs T and AEC are recorded inside the NQ/NB runs, `01` §6.2).
- `<Condition>`: `def` defaults · `uro2` / `uro4` forced URO rate · `urooff` not rendered · `la4` look-ahead · `aba2` / `aba4` forced budget rate · `abacap` off-screen cap · `vis` `OnlyTickPoseWhenRendered` · `startpre` / `startpost` start order.
- `<Rate>`: `f30` / `f60` / `f144` (fixed step) for single-process runs; `l0` / `l100` / `l200` (added round trip in ms, `j` suffix for jitter) for network runs.
- `<role>` (network runs): `srv`, `cli1`, …
- Example: `2026-12-01_101500_m1_NQ_uro4_f60_r1_events.csv`, with `2026-12-01_101500_m1_manifest.json` in `Raw\` and `2026-12-01_101500_m1_summary.md` in `Summaries\`.
- `_r0_prime` files are the discarded priming run. They are kept for transparency and excluded from every summary.

## Environment record (written into every manifest and summary)

```text
Engine:            UE 5.8.4 (CL 58902052), launcher build
Build:             Development, packaged (clients, listen servers, standalone); editor binary -server (dedicated servers)
Compiler:          MSVC 14.44.35229 (VS 2022), Windows SDK 10.0.22621.0
CPU / GPU / RAM:   i7-14650HX (8P + 8E) / RTX 5070 Laptop (driver recorded per batch) / 32 GB
Affinity:          performance cores only, mask 0xFFFF (logical CPUs 0-15): applied_mask() read back and recorded in every manifest
Storage:           project on D: (Kingston NVMe), engine on C: (WD NVMe)
OS:                Windows 11 build 26200
Power:             AC, power plan recorded per batch
Resolution:        1280x720 windowed, default scalability, no VSync, no sound
Timing:            fixed step -deterministic -FPS=<n> (single process) | real time, t.MaxFPS=<n>, -PktLag=<L/2> both sides (network)
Command line:      exact, per run, in the manifest
```

## Run log

| Batch | Configs | Conditions | Runs | Purpose | Summary |
|---|---|---|---|---|---|
| — | — | — | — | No batch has run yet. The first batch is the M1 single-process baseline in step S3 ([03](../Explanation/03_Implementation_Roadmap.md)) | — |
