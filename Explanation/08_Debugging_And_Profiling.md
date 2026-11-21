# 08 · Debugging And Profiling: AnimEventClock

**Status:** final for the built system (S6). The network view (S7) and a profiling campaign were not done.

**Will contain:** how to see the system working and measure it, with exact commands for UE 5.8.4: `aec.Debug` (event log, window bars), the per-event CSV and its stats script, the CSV profiler category `AnimEventClock`, Unreal Insights scopes for `Advance` and `Dispatch`, `a.URO.Draw` and `a.Budget.Debug.Enabled` for seeing what the animation system is doing, `stat net` and `-PktLag` for network runs, and how to read a run's per-frame server time log.

## 1. Seeing the event clock work (S6)

| What | How (UE 5.8.4) | What it shows |
|---|---|---|
| Every event as it fires | Console `aec.Debug 1` (or `-ini:Engine:[ConsoleVariables]:aec.Debug=1` on the command line) | One `LogAnimEventClock` line per event: actor, name, phase, action, key, pass, due time, fire time, lateness in ms, `(interrupted)` for a forced end. Lateness is always ≥ 0 and under one frame on the authority (SC1); a negative value is a bug |
| Which windows are open | `aec.Debug 2` | The open windows' names above each actor with an active action (`DrawDebugString`, one frame) |
| The clock off | `aec.Enabled 0` | Montages still play; no clock event fires: the "clock off" configuration of the overhead runs |
| Per-frame cost and counts | CSV profiler: console `CsvProfile Start` / `CsvProfile Stop`, or `-csvCaptureFrames=<n>` on the command line; files in `Saved/Profiling/CSV/` | Category `AnimEventClock`: `EventsFired`, `ActiveActions`, `VisualCorrections`, `DispatchUs` (time in dispatch, handlers included), published once per frame (`CsvProfiler.cpp:1059–1066, 5049`) |
| The clock against the notify path, per event | A bench run with `-AECClock` (`Tools\run_bench.py --clock`): the events file has an `AEC` path beside `NQ`/`NB` | `Tools\event_stats.py` gives both error distributions from the same run |

**Reading a clock event that looks wrong.** Compare its `KeyIndex` with `FAECMontageTimeline::Build(*Montage).GetKeys()` (the same keys the director's expected events come from). A wrong position means the montage's markers (the S8 validator); a right position but a wrong due time means the play rate or a jump; an event that fires twice in one pass would be a timeline bug (the tests in `AnimEventClock.Timeline.*` cover the crossing rules).
