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
- `<Condition>` (as implemented in S2–S3, `-AECCond`): `def` defaults · `uro` URO at the engine's own rates · `uro2` / `uro4` forced URO rate · `la4` look-ahead (root motion from everything + URO rate 4) · `rme` root motion from everything without URO (the S3 control) · `aba2` / `aba4` forced budget rate · `vis` `OnlyTickPoseWhenRendered`. The behind-camera band is the "not rendered" case inside every run. The start order is not part of the name: a batch with `--start pre` says so in its label (`m1_pre`) and its manifest. The off-screen budget cap is a Phase 8 item.
- `<Rate>`: `f30` / `f60` / `f144` (fixed step) for single-process runs; network runs name the added round trip instead (below).
- Network runs (`run_net.py`): `<batch>_<server>_<Config>_<cond>_rtt<round trip ms>[j]_r<run>_srv_*.csv` / `_cli_*.csv` / `.log`, where `<server>` is `dedicated` or `listen` and `j` marks the jitter variant; the server writes `actions`, `expected`, `events`, `frames`, the client `actions`, `events`, `frames`.
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
| `2026-11-21_034755_s2check` | NQ, NB | def, 60 fps, 20 s capture | 4 + 1 | S2 harness check | [summary](Summaries/2026-11-21_034755_s2check_summary.md) |
| `2026-11-21_041947_s2recheck` | NQ, NB | def, 60 fps, 20 s capture | 4 + 1 | S2 re-check after review and simplify | [summary](Summaries/2026-11-21_041947_s2recheck_summary.md) |
| `2026-11-21_042059_m1_f60` | NQ, NB (+T) | def, uro, uro2, uro4, la4, aba2*, aba4*, vis at 60 fps | 32 + 1 | M1 baselines (S3) | [summary](Summaries/2026-11-21_042059_m1_f60_summary.md) |
| `2026-11-21_043638_m1_fps` | NQ, NB (+T) | def, uro4, aba4* at 30 and 144 fps | 24 + 1 | M1 baselines (S3) | [summary](Summaries/2026-11-21_043638_m1_fps_summary.md) |
| `2026-11-21_045238_m1_pre` | NQ, NB (+T) | def at 60 fps, start order "pre" | 4 + 1 | M1, H8 (S3) | [summary](Summaries/2026-11-21_045238_m1_pre_summary.md) |
| `2026-11-21_051003_m1_fix` | NQ, NB (+T) | def, aba2, aba4, rme at 60 fps | 16 + 1 | M1 after the budget fix, plus the `rme` control (S3-f) | [summary](Summaries/2026-11-21_051003_m1_fix_summary.md) |
| `2026-11-21_051748_m1_fixfps` | NQ, NB (+T) | aba4 at 30 and 144 fps | 8 + 1 | M1 after the budget fix (S3-f) | [summary](Summaries/2026-11-21_051748_m1_fixfps_summary.md) |
| `2026-11-21_062044_s4regress` | NQ, NB (+T) | def, 60 fps, 20 s | 4 + 1 | S4: standalone behaviour unchanged (same hashes as S2) | [summary](Summaries/2026-11-21_062044_s4regress_summary.md) |
| `2026-11-21_062153_s4smoke` | NQ, G (+T) | dedicated and listen server, 0 and 100 ms round trip, 20 s | 8 | S4 network smoke | [summary](Summaries/2026-11-21_062153_s4smoke_summary.md) |
| `2026-11-21_071356_m2` | NQ, G (+T) | dedicated and listen server, 0 / 100 / 200 / 100+jitter ms round trip, 60 s | 32 + 1 | M2 network baselines (S4) | [summary](Summaries/2026-11-21_071356_m2_summary.md) |
| `2026-11-21_080140_m2_cond` | NQ, G (+T) | dedicated server, `uro` and `vis`, 0 ms, 60 s | 8 + 1 | M2 conditions on the dedicated server (S4) | [summary](Summaries/2026-11-21_080140_m2_cond_summary.md) |

\* Invalid: the budget allocator was never enabled (`09` S3-b); these runs equal `def` and are kept only as the record of the mistake. Re-measured in `m1_fix` / `m1_fixfps`.
