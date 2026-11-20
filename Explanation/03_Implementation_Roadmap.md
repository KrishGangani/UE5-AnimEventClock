# 03 · Implementation Roadmap: AnimEventClock

**Written:** end of Phase 3, 2026-11-21 · **Engine:** UE 5.8.4

**Rules for every step:**
- Each step is small enough to read in one sitting: a few hundred lines at most.
- A step is finished only when its code **compiles with 0 warnings through the queue**, its check **passes**, and its `Explanation/` entries (`05`, `06`, and `07` where relevant; `02` if the design changed) are **written in the same step**.
- After each step that adds C++: run `simplify`, then `code-review`.
- **The baselines are captured before the event clock exists.** Steps S1–S4 build only the assets, the workload, the harness and the baseline paths (NQ, NB, T, G), and end with the **problem gate** (01 §6.4). The event clock starts at S5.
- Every Unreal job (build, commandlet, package, run) goes through `tools\ue_queue.py`.

---

**Progress (2026-11-21):** Phase 4 ✅ (F1–F4). **S1 ✅**, **S2 ✅** (code review and simplify done; results in [09](09_Testing_And_Benchmarks.md)). **S3**: the M1 baseline batch is queued. Next: S4.

## Owner decisions after the Phase 1–4 audit (2026-11-21)

1. **UE-308913 on 5.6.1 is not reproduced now.** The server path is measured on 5.8.4 first (S4). Reproducing on 5.6.1 is an **optional Phase 8 item**, worth doing only if 5.8.4 shows no server-side problem and the evidence is needed to explain the history ([11](11_Optimization_Decisions.md)).
2. **Licence:** referencing the engine's Starter Content mannequin by path, never copying or committing it, is accepted (as the project's reading of the EULA, not legal advice). Engine and template content are never committed.
3. **Scope changes accepted:** gap lowered to medium, the edit-time bake dropped, v1 server-initiated actions only.

## Phase 4: Foundation (no gameplay code yet) ✅

| Step | What gets built | How it is checked |
|---|---|---|
| **F1** | `AnimEventClock\AnimEventClock.uproject` (EngineAssociation 5.8; plugins `AnimEventClockCore`, `AnimationBudgetAllocator`, `GameplayAbilities`), `Source\AnimEventClock.Target.cs`, `AnimEventClockEditor.Target.cs`, the game module `AnimEventClock` and the editor-only game module `AnimEventClockEditor` | Editor and game targets build from the command line, through the queue, with 0 errors and 0 warnings |
| **F2** | The plugin `AnimEventClockCore` with its runtime module `AnimEventClockCore` and editor module `AnimEventClockCoreEditor` | Same builds; all four modules load where they should (editor: four; game: two) |
| **F3** | `Config\DefaultEngine.ini` (default map, no smoothing, `r.VSync=0`, DX12 SM6), `DefaultGame.ini` (always cook the tutorial mannequin folder), `DefaultInput.ini` (Enhanced Input classes); `Tools\build.bat`, `package.bat`, `smoke_game.bat`, `cpu_affinity.py` | A packaged Development build cooks the tutorial mannequin and starts and quits cleanly; the editor binary starts as a dedicated server (`-server`) at 30 Hz; the affinity helper returns `0xFFFF` and pins a test process |
| **F4** | The written foundation docs: 04, 05, 06 Part A, 09 Phase 4 table, stubs 07–12 | `check_links.py` reports 0 problems; no banned words |

## Phase 5: Implementation

| Step | What gets built | How it is checked | Explanation files |
|---|---|---|---|
| **S1. Markers + demo assets** | Plugin: `UAECEventNotify`, `UAECEventWindowNotifyState`, `EAECEventPhase`, the static `OnFiredByAnimation` hook. Game module: `AECDemoMontages.h` (the authored content). Game editor module: `UAECMakeDemoAssetsCommandlet` (montages `AM_Attack_{Q,B}`, `AM_Combo_{Q,B}` from `Tutorial_Idle`; the validator's test montages move to S8) | The commandlet runs through the queue and writes the assets; a packaged build loads them; marker names, times and tick types read back exactly as authored | 04, 05, 06, 07 |
| **S2. Workload + harness (baseline paths only)** | `AAECDemoCharacter` (a plain `UAnimInstance`, no subclass), `UAECBenchSubsystem` (seeded bands, action loop, NQ/NB/T paths, per-event CSV with game time + wall clock + frame), `Tools\event_stats.py` (median/p95/p99/min/max, missed/duplicates per 1,000; self-test on a hand-made CSV), `Tools\run_bench.py` adapted from UE5-UpdateBudget (P-core pinning: `pin_self` once, then the real executable via `Popen` inherits the mask; real exit codes; `applied_mask()` recorded, interleaved rotated order, manifests) | Same seed → same event log hash, twice. Default condition at 60 fps: every NQ event within one frame (E1). Tool self-tests pass | 04, 05, 06, 07, 09 |
| **S3. Single-process baseline capture (M1) + the gate** | Condition switches in the harness (URO force, non-rendered band, look-ahead, budget force/cap, visibility option, start order) | **The M1 batch:** NQ, NB (+ T in the same runs) × conditions × frame rates, fixed step, 1 priming + 2 interleaved runs each, before any event-clock code. H1–H4, H8 answered with data. **Problem gate applied; the claims list in the README is updated, including any condition that is "not a problem"** | 01b (status table), 09, README |
| **S4. Network harness + baseline capture (M2) + the gate** | `AAECDemoGameMode`, `AAECDemoPlayerController`, `UAECDemoAttackAbility` (G), server per-frame time log, remote-player role; `Tools\run_net.py` (server + client processes, `-PktLag` on both, real time, `t.MaxFPS`, both inherit the runner's P-core mask, both killed in `finally`) | **The M2 batch:** NQ, G, T × roles × 0/100/200 ms (+ jitter), real time. H5–H7 answered; UE-308913 status on 5.8.4 **measured**. Gate applied | 05, 06, 07, 09, README |
| **S5. Pure timeline + unit tests** | `FAECMontageTimeline`, `FAECPlayhead`, `Build` from a montage, `Advance`, `JumpToSection` | Automation tests (no world): keys in `(old, new]`; section path; a looped section increments the pass; play rate scaling of due times; zero-length loop guard; key at 0 and at the end; empty timeline; interrupt closes open windows | 04, 05, 06, 07 |
| **S6. Event-clock component (authority)** | `UAECEventClockComponent`: `PlayAction`/`StopAction`/`JumpToSection`, `TG_PrePhysics` tick only while active, ordered dispatch with the stop-during-dispatch rule, `IsWindowOpen`, montage follower, `aec.Enabled`/`aec.Debug`, module `OnEndFrame` CSV stats; AEC path in the harness | A first M1-style run per condition: **SC1** checked on single runs before the full matrix; a handler that destroys its actor during dispatch does not crash | 05, 06, 07, 08 |
| **S7. Replication + clock sync** | `FAECReplicatedAction`, `OnRep` (new action, jumps, stop, late start, expiry), `UAECClockSyncComponent` (burst + 1 Hz, min-RTT of 8), `AECClock::GetServerTime` | One run per role at 0/100/200 ms: **SC2, SC3, SC4** checked on single runs | 05, 06, 07, 08 |
| **S8. Validator** | `UAECMontageValidator` in `AnimEventClockCoreEditor`; the commandlet also makes the six seeded-defect montages | The stock `DataValidation` commandlet over `/Game/AEC/`: **SC7** (6 of 6 seeded defects, 0 false positives on the demo montages) | 05, 06, 07 |

## Phases 6–10 (after the code exists)

| Phase | Work |
|---|---|
| 6 Explanation | Consolidate 04–07 into one consistent line-by-line pass; re-check every API against 5.8.4 |
| 7 Testing | Edge cases: zero events; an event at time 0 and at the end; play rate 0.01 and 3; pause; global time dilation; relevancy join mid-action; actor destroyed mid-window; packet loss; a 30-minute soak |
| 8 Profiling | **M3** (M1 + M2 with AEC) and **M4** (overhead: clock off vs on × N = 64 / 256, one Blueprint handler). Optimisations only with data, in `11_Optimization_Decisions.md` |
| 9 Code review | `code-review` and `simplify`, plus a written self-review: weaknesses, not praise |
| 10 Portfolio | Charts (`dataviz`), architecture page, a side-by-side demo video (notify vs clock on the same NPC under URO and on a client at 200 ms), README results, interview Q&A |

**Rough cost of the matrices:** M1 ≈ 56 fixed-step runs × ~75 s; M2 ≈ 72 real-time runs × ~90 s; M3 the same again; M4 ≈ 10 runs. About 6 hours of unattended queue time in total, spread over S3, S4 and Phase 8.
