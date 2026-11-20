> **Raw Phase 2 notes (E: measurement), UE 5.8.4, 2026-11-21.** How each failure mode from notes A–C can be reproduced on purpose, and which clock each timing error is measured on. The plan that uses this is in [../01_Problem_Analysis.md](../01_Problem_Analysis.md) §7; the facts are restated in [../01b_Research_Findings.md](../01b_Research_Findings.md).

# E. Reproducing the failure modes, and measuring them

## E0. The two clocks

| Run type | Reference clock | Why |
|---|---|---|
| **Single process** (standalone, or listen-server host view) | **Game time**, `UWorld::GetTimeSeconds()`, under the fixed step `-deterministic -FPS=<n>` | The fixed step gives every frame exactly `1/n` s of game time (UE5-UpdateBudget, 01b B-1), so the same run produces the same frames. Timing error is a property of game time, not of CPU speed, so it is measured in game time exactly |
| **Several processes** (dedicated or listen server + clients) | **Wall clock**, `FPlatformTime::Cycles64()` (raw `QueryPerformanceCounter`) × `FPlatformTime::GetSecondsPerCycle64()` | Emulated latency is applied in real time (`NetConnection.cpp:2457, 2666–2673`), so these runs must run in real time, not under the fixed step. `QueryPerformanceCounter` is one system-wide counter, so stamps from two processes on this machine are comparable (Microsoft, "Acquiring high-resolution time stamps": https://learn.microsoft.com/en-us/windows/win32/sysinfo/acquiring-high-resolution-time-stamps) **[inferred for cross-process use from that document]**. `FPlatformTime::Seconds()` adds `GetSecondsTimeOffset()`, which is 0 in the generic implementation (`Engine/Source/Runtime/Core/Public/GenericPlatform/GenericPlatformTime.h:219–222`; Windows: `Public/Windows/WindowsPlatformTime.h:23–44`), but the harness uses the raw counter so nothing depends on that |

**Due time of an authored event.** `Due = StartTime + (position of the event along the section path − start position) / PlayRate`, where `StartTime` is the authority's game time in the frame the action starts. In multi-process runs the server also writes, every frame, the pair (server game time, wall clock), so a due time in server game time is mapped to the wall clock by interpolating between the two frames around it. That keeps the mapping correct even if the server hitches.

**Timing error** = fire time − due time, in ms. Negative is early. Per run: median, p95, p99 (linear interpolation, as in UE5-UpdateBudget's `csv_stats.py`), plus min and max because "never early" is a pass/fail property.

**Missed** = an authored event whose position the action actually reached (the action was not interrupted before it) and that a role never fired, within 1 s after the action ended. **Duplicate** = a second fire of the same (actor, action instance, event index, loop pass) on the same role. Both per 1,000 expected events.

## E1. Baseline sanity (defaults, one process)

- Standalone, fixed step at 30, 60 and 144 fps, montage on a character with default mesh settings.
- Expected from the code: every notify fires within one frame of its due time. Its sign depends on start order (E3).
- **Purpose:** the reference row. If defaults already fail, the harness is wrong.

## E2. URO

| Variant | How to force it | Expected (from notes B2) |
|---|---|---|
| Visible, rate 2/3/4 | `bEnableUpdateRateOptimizations = true` on the demo NPC mesh, `a.URO.ForceAnimRate 2/3/4` (applies in the "visible, not human-controlled, no root motion" branch, `SkinnedMeshComponent.cpp:354–358`) | Late by 0…(rate−1) frames, bunched, none missed |
| Not rendered | NPC behind the camera for more than 1 s (`bRecentlyRendered` window, `:1864`) | Rate `BaseNonRenderedUpdateRate` = 4 |
| Look-ahead | Anim instance with `RootMotionMode = RootMotionFromEverything`, visible, forced rate ≥ 2 (`:360–364`) | Early; and possibly a growing lead (the drift reading in notes B2) |
| Human-controlled control | The same with a `APlayerController` possessing the pawn (`:205–210, 287`) | Rate 1: on time |

## E3. Tick order at the start frame

- Start the montage once from a tick that runs **before** the mesh's tick in the same frame (a `TG_PrePhysics` tick that the mesh tick lists as a prerequisite) and once from a timer (timers run after `TG_PostPhysics`, `LevelTick.cpp:1816`, so after the mesh).
- Expected (notes A2, inferred): about −1 frame vs +0…1 frame relative to "world time at start + authored time".
- **Purpose:** shows whether "frame-accurate" notifies are even well defined relative to gameplay time.

## E4. Animation Budget Allocator

| Variant | How | Expected (notes B4) |
|---|---|---|
| Forced rate 2/4 | `USkeletalMeshComponentBudgeted` mesh, `a.Budget.Enabled 1`, `a.Budget.Debug.Force 1`, `a.Budget.Debug.Force.Rate 2/4` (Development builds, `AnimationBudgetAllocatorCVars.cpp:130–160`) | Like URO trail: late, bunched, none missed |
| Real budget | N budgeted meshes and a small `a.Budget.BudgetMs` so the budget binds | Rates vary with measured cost: **not deterministic** run to run; reported with the run-to-run range |
| Off-screen cap | More than `a.Budget.MaxTickedOffsreen` (4) off-screen meshes with "tick even if not rendered" | The extra meshes are disabled and lose time: very late events, montages that overrun |

## E5. Server roles

| Variant | How | Expected (notes C2, C3, B3) |
|---|---|---|
| Listen server, NPC | Packaged build as `?listen` host, AI-controlled demo character plays the montage | Like standalone (visible to the host) |
| Dedicated server, NPC | Editor binary with `-server` (Phase 4 check F-7), 30 Hz | Not rendered: with URO on, rate 4 at 30 Hz = up to 100 ms late. With `OnlyTickPoseWhenRendered`: never fires |
| Server, remote player character | A client possesses the demo character; the server starts the montage | Pose ticked per client move (C2): notify timing follows client cadence and latency |
| UE-308913 check | The same, with a notify **state** in the montage; count `NotifyBegin`/`NotifyEnd` per action on the server | 5.6.1 reported repeats; 5.8.4 restructured the code (C3). Measured, not assumed |

Latency: `-PktLag=0/50/100` on both processes (0/100/200 ms added round trip). One run with `PktLagVariance` adds jitter.

## E6. Client roles

| Variant | How | Expected (notes C4) |
|---|---|---|
| Simulated proxy, plain start | The server multicasts the start; the client plays on receipt | Late by one-way delay + quantisation; none missed |
| Simulated proxy, GAS | The montage is played by a GAS ability (`PlayMontageAndWait`) | As above until an error > 0.1 s triggers a correction: backward → duplicates; forward → skipped BranchingPoint notifies |
| Autonomous proxy, root motion | A root-motion montage on the client's own character, with emulated loss so the server corrects | `SetPosition` on correction: skips or repeats |

## E7. Overhead

- Packaged Development, fixed step 60 fps, real GPU 1280×720, **pinned to the performance cores** (mask `0xFFFF` on this machine; the runner pins itself with `pin_self` from `Tools\cpu_affinity.py`, and the real executable it starts with `Popen` inherits the mask), N = 64 / 256 characters each running the attack loop.
- Compare game-thread time with the event clock **off** (montages and notifies only) vs **on**, plus an in-code cycle scope around the clock's dispatch, published once per frame as a CSV stat (one stat per frame, not per event: UE5-UpdateBudget measured about 50 ns per `CSV_CUSTOM_STAT`).
- The P01 protocol: one priming run, runs per combination = number of configs, interleaved with rotated order, median across runs with [min–max].
