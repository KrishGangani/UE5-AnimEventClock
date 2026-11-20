# 01 · Problem Analysis: AnimEventClock

**Phase:** 1 (Problem Analysis), written 2026-11-21 from the Step 0 plan, then corrected by Phase 2 the same day (evidence in [01b_Research_Findings.md](01b_Research_Findings.md); references like `01b #6` point to its §1 table).
**Engine:** UE **5.8.4** (CL 58902052). Paths are relative to `Engine/Source/Runtime/Engine/` unless another root is named.
**Status of every claim:** **[verified]** (read in the engine source or a cited source), **[hypothesis]** (to be confirmed by measurement), or **[target]** (a pass/fail threshold, not a result).

---

## 1. The problem in one paragraph

In Unreal, the moments that matter to gameplay during an attack or an ability (the hit window opens, the projectile spawns, the ability commits, the window closes) are usually authored as **anim notifies** inside an animation montage. That puts gameplay time in the hands of the animation system: a notify fires only in a frame in which its mesh's animation **updates** [verified, `01b #1`]. Large games deliberately update animation less often (update-rate optimisation, the Animation Budget Allocator, "only tick when rendered"), and in multiplayer each machine advances the montage on its own schedule (per client move on the server, from the moment the replicated start arrives on a client) [verified, `01b #2, #4–#6, #8`]. So the same authored hit window opens late on one machine, in a burst on another, early in one rare mode, not at all on a dedicated server with one common setting, and twice on a client after a position correction [hypothesis for each magnitude, §4].

The fix most teams reach for, BranchingPoint notifies, makes a notify precise *within* an update frame but does not create update frames, so it does not help [verified, `01b #1`].

## 2. Where this happens in real games

| Evidence | What it shows | Source |
|---|---|---|
| Epic's own URO code | "Human controlled characters should be ticked always fully to minimize latency w/ game play events triggered by animation." Epic exempts players **because** throttled animation delays gameplay events; NPCs are not exempt | `Private/Components/SkinnedMeshComponent.cpp:287` [verified] |
| Epic's notify docs | Queued: "Select this if you are okay with minor frame inaccuracy"; Branching Point: "maximum frame accuracy" | [raw notes D2](Phase2_Raw_Notes/D_existing_work_and_assets.md) (docs page) |
| Forum, UE 4.27 | URO on non-rendered actors: "anim notify timing seems to be messed up"; "Branching point or queued ticktype doesn't make a difference" | raw notes D2 |
| Forum, UE 5.6.1 (UE-308913) | Notify-state begin/end fire repeatedly on a dedicated server for the whole montage; workaround: client-authoritative notify states + a server RPC | raw notes D2; status on 5.8.4 **[unverified]** (`01b #7`) |
| Epic's GAS code | Simulated-proxy montages are corrected with `SetPosition` beyond 0.1 s; Epic's comment: "Forcing the position here is not a good way to do replication syncing for animations." | `Plugins/Runtime/GameplayAbilities/Source/GameplayAbilities/Private/AbilitySystemComponent_Abilities.cpp:3292, 3442–3446` [verified] |
| Epic's Mover (Experimental) | A whole mechanism to make the network simulation "master of montage position" | `Plugins/Experimental/Mover/Source/Mover/Private/MoverComponent.cpp:56–62` [verified] |
| Hitman: Absolution | Moving agents by animation removed foot sliding but made them "react much slower to steering input": the animation-vs-gameplay authority trade-off | [research/02](Research/02_technical_challenges.md) [T2-A], [T5-F] |
| Job postings, 2026 | Animation appears in 11 of 57 gameplay postings; Respawn Lead Animation Engineer; The Coalition Senior Gameplay Animation Engineer; Hangar 13 GAS + animation | [research/01](Research/01_job_market.md) (EA3, MS1, K2; skill table row 24) |

**What these share:** the engine and studios treat "when does gameplay think the animation reached this moment" as a real problem, and each existing answer covers one slice of it.

## 3. What already exists, and why it doesn't fully solve the problem

| Existing tool | What it does | What it doesn't do | Evidence |
|---|---|---|---|
| BranchingPoint notifies | Fire synchronously at their exact montage position | Fire only in frames where the mesh updates; cost more than Queued | `01b #1` [verified] |
| `a.EnableQueuedAnimEventsOnServer` (+ 5.8's `a.Notifies.DispatchPolicy`) | Fire queued notifies on servers, once per client move | Timing still follows the client's moves on the server | `01b #6, #7` [verified] |
| Notify filters (`bTriggerOnDedicatedServer`, LOD, chance, weight) | Decide *whether* a notify fires | Not *when* | raw notes A3 [verified] |
| `RepRootMotion` | Corrects a simulated proxy's root-motion location | No start time; doesn't drive notifies | `01b #8` [verified] |
| GAS montage replication | Starts montages on simulated proxies, corrects position beyond 0.1 s | No start time; corrections can repeat or skip notifies | `01b #8` [verified; effects hypothesis] |
| Mover sim-driven montages | The simulation owns montage position | Experimental, Mover only; notifies still fire on the next anim update | `01b #11` [verified] |
| Motion Warping | Warps root motion inside notify-state windows | Its windows are notify states, so they inherit the same timing | [research/03](Research/03_existing_solutions_gap_analysis.md) §6 |
| Hand-written timers | Start `FTimerManager` timers at the authored offsets | A second copy of the timing; play rate, sections, interrupts and networking handled per ability by hand | `01b #12` |
| Able Ability System (Fab) | Abilities as tasks on their own timeline; animation is one task | Its clock and network sync are not documented; events are not authored in the montage | `01b #11`; raw notes D3 |

**The gap (medium strength, after Phase 2):** events authored where designers already author them (the montage), fired from the gameplay clock on every network role, each **exactly once**, with a latency-compensated start time on clients, as a drop-in for `ACharacter`/GAS projects, and **measured**. Phase 2 lowered this from the roadmap's "strong" because Mover, Able and timers each cover part of the idea (`01b #11–#12`).

## 4. Root-cause hypotheses

Each hypothesis gets a measurement that can confirm or reject it, and the baseline runs come **before** any event-clock code (§7, [03](03_Implementation_Roadmap.md) S3–S4).

| # | Hypothesis | Why we suspect it | How we test it |
|---|---|---|---|
| H1 | **URO and the budget allocator make notifies late and bunched** (not lost) | Skipped time is added back at the next update (`01b #2, #5`) | Fixed-step runs with `a.URO.ForceAnimRate 2/3/4` and `a.Budget.Debug.Force.Rate 2/4`; error distribution and "events per frame" histogram (raw notes E2, E4) |
| H2 | **BranchingPoint does not help under throttling** | It fires only in update frames (`01b #1`) | The same runs with BranchingPoint markers: same late frames expected |
| H3 | **Look-ahead mode fires notifies early, possibly drifting further ahead** | The animation advances by `DeltaTime + LookAheadAmount` (`01b #3`); the drift is a reading of the bookkeeping only | "Root motion from everything", visible, forced rate ≥ 2; error sign and slope over the montage (E2) |
| H4 | **Visibility-based ticking delays or drops notifies** off-screen and on dedicated servers | `OnlyTickPoseWhenRendered` stops montages off-screen and disables the tick on a dedicated server (`01b #4`) | Off-screen placement in a standalone run, and a dedicated server (E2, E5) |
| H5 | **On a server, a remote player's notifies follow client moves** | `MoveAutonomous` ticks the pose with each move's delta (`01b #6`) | Dedicated and listen server with a possessing client, 0/100/200 ms, with and without jitter (E5) |
| H6 | **Client position corrections cause missed or duplicate notifies** | GAS and root-motion corrections call `SetPosition` (`01b #8`) | Simulated proxies via GAS, autonomous root motion with loss (E6) |
| H7 | **UE-308913: repeated notify-state begin/end on a 5.8.4 server** | Reported in 5.6.1; the code was restructured in 5.8 (`01b #7`) | Begin/end counts per action on a dedicated server (E5) |
| H8 | **A montage's start frame makes "on time" ambiguous by up to ±1 frame** | First-update behaviour depends on tick order (`01b` B-3) | Start from before vs after the mesh tick (E3) |

**Not a cause we accept without checking:** "it's just frame rate". At a fixed 60 fps with defaults, every notify should be within one frame of its time; E1 checks exactly that. If it isn't, the harness is wrong, not the engine.

## 5. The cost of leaving it unsolved

*Arithmetic illustration only, not a measurement:* a hit window authored as 0.30–0.45 s is 150 ms long. With an NPC on a 30 Hz dedicated server at the non-rendered URO rate of 4, the window can open up to 3 server frames = 100 ms late (H1). A player's dodge that lands at 0.32 s should be hit but isn't; one at 0.50 s shouldn't be hit but is. Players experience this as unfair hits, and it can't be fixed by tuning numbers because the error changes with distance, visibility and load.

When this is ignored, teams typically:
- switch NPC meshes to "always tick, every frame" and pay the animation cost the optimisation was meant to save;
- duplicate the timing in hand-written timers and keep two copies in sync by hand;
- move event logic to the owning client and add server RPCs (the UE-308913 workaround), which opens a trust problem.

## 6. What "solved" means: success criteria, defined now

### 6.1 The workload (identical in every run)

- **Map:** the engine's `Template_Default` (no authored map). Characters are spawned by code from a **fixed seed**.
- **Characters:** N NPCs of a demo `ACharacter` (the engine's tutorial mannequin, `01b #13`), split into three bands: near and visible, far and visible (small on screen), and behind the camera (not rendered). Single-process runs: N = 64 (timing), 64 / 256 (overhead). Network runs: 16 NPCs plus one possessed character per client.
- **Actions:** each character loops "wait a seeded 0.5–1.5 s, then play one action". Actions are generated montages (by an editor commandlet, from the tutorial idle sequence):
  - `Attack`: about 2 s, 4 instant events and 1 window (begin + end).
  - `Combo`: 3 sections with a section jump made by the harness, and one looped section.
  - Play rate 1.0 or 1.3 (seeded); 10% of actions are interrupted at a seeded time.
- **Volume:** one action cycle is about 3 s (1 s wait + 2 s action), so a 60 s capture gives about 64 × 20 actions × 6 events ≈ 7,700 events per single-process run, enough for p99 and per-1,000 rates.
- **Control row:** engine defaults (no URO, no budget, `AlwaysTickPose`), which should already be within one frame (E1).

### 6.2 Compared configurations

| ID | Configuration | Why it is included |
|---|---|---|
| **NQ** | Markers as **Queued** notifies (the engine default), fired by the animation | What most projects do |
| **NB** | The same markers as **BranchingPoint** notifies | Epic's "maximum frame accuracy" option. Leaving it out would be unfair |
| **T** | Hand-written timers started at the authored offsets when the action starts (on clients: when the start arrives) | The common workaround that already bypasses animation (`01b #12`) |
| **G** | GAS montage replication (`PlayMontageAndWait`) with Queued markers; network runs only | The standard networked path (`01b #8`) |
| **AEC** | **AnimEventClock**: events from the gameplay clock, montage as visual follower (02) | Our system |

In single-process runs, T and (after S6) AEC record **in the same run** as NQ or NB: they don't change the animation, so the frames are identical and the comparison is exact. NQ and NB need different montages, so they are separate runs.

**Conditions** (each applied to NQ/NB/T/AEC): default; URO forced rate 2 / 4; URO non-rendered (behind camera); look-ahead (root motion from everything, rate 4); budget allocator forced rate 2 / 4; budget off-screen cap (more than 4 off-screen "tick even if not rendered" meshes); `OnlyTickPoseWhenRendered` off-screen; start-order variants (E3). Budget conditions run in single-process and client runs only: budgeted meshes never register on a dedicated server (`01b #5`). Frame rates: fixed step 30 / 60 / 144 fps for the frame-dependent conditions (default, URO 4, budget 4); 60 fps for the rest.

**Network roles:** listen-server host; dedicated server (editor `-server`, 30 Hz); simulated proxy; autonomous proxy; and on the servers, a remote player's character. Latency: added round trip 0 / 100 / 200 ms (`-PktLag=0/50/100` on both processes), plus one jitter run (`PktLagVariance=10`).

### 6.3 Metrics

| Metric | Unit | Tool |
|---|---|---|
| Timing error per event (fire − due): median, p95, p99, min, max | ms | Per-event CSV written by the harness (game time in single-process runs, wall clock in network runs, raw notes E0); `Tools\` stats script |
| Missed events per 1,000 expected | count | Same CSV: expected events come from the timeline the action actually traversed |
| Duplicate events per 1,000 | count | Same CSV: second fire of the same (actor, action, event, pass) on the same role |
| Notify-state begin/end count per action (H7) | count | Same CSV |
| Events per frame (bunching) | histogram | Same CSV |
| Event-clock overhead: game-thread µs per dispatched event, and per active action per frame | µs | In-code cycle scope, published once per frame as a CSV profiler stat; packaged Development |
| Game-thread time, clock on vs off | ms | CSV profiler (`GameThreadTime`) |
| Client server-time estimate error | ms | Clock-sync component vs the server's logged time, on the wall clock |
| Visual follow error (montage position − clock position) | ms | Per-frame sample in the harness |

### 6.4 Pass/fail criteria [target]

**The problem gate (checked on the baselines, before the event clock exists).** A condition counts as "a real problem" only if a baseline (NQ, NB or G) shows **p99 |error| > 1 frame**, or **any** missed or duplicate events. The README and the portfolio claim improvements **only** for conditions that pass this gate. If a condition fails the gate, it is reported as "not a problem on 5.8.4" and dropped from the claims. If no condition passes, the project's claim shrinks to what remains (for example the network roles), and that is stated plainly.

| # | Criterion | Threshold |
|---|---|---|
| SC1 | **On time on the authority** | AEC error in **[0, 1 frame)** for **100%** of events (never early; max below one frame of that process: 33.3 / 16.7 / 6.9 ms at 30 / 60 / 144 fps; 33.3 ms on the 30 Hz dedicated server), in **every** condition of §6.2, single process and server roles |
| SC2 | **Exactly once, everywhere** | AEC **0 missed and 0 duplicates per 1,000** on every role, at 0 / 100 / 200 ms and in the jitter and correction runs. Window begin/end exactly once per pass |
| SC3 | **On time on clients** | For events due after the replicated start arrives: AEC p99 error **≤ 1 client frame + 1 server frame** (50 ms at 60 fps client / 30 Hz server) at 0 / 100 / 200 ms. Events due before it arrives fire in the arrival frame; **0 expired** at ≤ 200 ms |
| SC4 | **Clock sync is good enough** | Client server-time estimate error p99 **≤ 1 server frame** (33 ms) at 0 / 100 / 200 ms, measured on the wall clock |
| SC5 | **Cheap** | ≤ **2 µs** game-thread time per dispatched event (median, one bound handler) and ≤ **0.5 µs** per active action per frame without due events, at N = 256; game-thread median within **+0.10 ms** of "clock off" at N = 64 (packaged Development, pinned to the P-cores) |
| SC6 | **Animation still follows** | Visual follow error ≤ **max(the mesh's current update interval, 0.25 s correction threshold)** in 99% of frames, with no growth over a 60 s run (look-ahead reported separately, since H3 predicts the animation itself runs ahead) |
| SC7 | **No new authoring workflow** | Events are placed as notifies in the montage editor (no second asset). The validator flags **6 of 6** seeded authoring defects and **0** false positives on the demo montages |

Where a baseline already meets a criterion (for example T is on time on the authority), that is reported as the baseline's result, not hidden. **SC1–SC3 are claims about AEC; the improvement claim is "AEC meets them in conditions where the gate shows the baselines don't".**

## 7. Measurement plan (summary; the full protocol goes in `09_Testing_And_Benchmarks.md`)

- **Build:** packaged **Development** for every client, listen server and standalone run; the launcher engine can't build Test or Server targets (`01b #14`). Dedicated servers run on the editor binary with `-server`; that only affects timing if the server falls below its 30 Hz cap, which every run checks from the server's frame log.
- **Single-process runs (timing):** fixed step `-deterministic -FPS=30/60/144`, real GPU at 1280×720 windowed, no VSync, no sound, `-traceautostart=0`. The capture window is opened and closed by the game itself (`BenchStart`/`BenchEnd` CSV events, as in UE5-UpdateBudget). Timing error is measured in game time, which is exact under the fixed step (`01b` B-2).
- **Network runs:** real time with a frame cap (`t.MaxFPS`), because emulated latency is real time (`01b` B-1). Timing error on the shared wall clock (`QueryPerformanceCounter`); due times mapped through the server's per-frame (game time, wall clock) log (raw notes E0).
- **Every timed process is pinned to the performance cores.** This i7-14650HX is hybrid (8 P-cores = logical CPUs 0–15, mask `0xFFFF`; 8 E-cores = 16–23), and UE5-UpdateBudget saw runs about 1.6× slower when Windows put the game thread on an E-core. The runner launches the **real** executable (`Build\Windows\AnimEventClock\Binaries\Win64\AnimEventClock.exe`, not the bootstrap; the editor binary for dedicated servers) with `subprocess.Popen` after pinning **itself** once with `pin_self(performance_core_mask())` from `Tools\cpu_affinity.py`, so every process it starts inherits the mask from its first instruction; it records `applied_mask()` in the environment, waits with a timeout and kills the process tree in a `finally` block. Not `cmd /c start /wait /affinity`, which always returns exit code 0 (found by UE5-TestTrust).
- **Batches:** one discarded priming run per batch; then **runs per combination = number of configurations**, interleaved, with the configuration order rotated each round. Combinations that no criterion needs are skipped. Statistics: per run median, p95, p99 (linear interpolation) plus min/max; then the median across runs with [min–max]. Repeated fixed-step runs must agree exactly; any difference is investigated and reported.
- **Order (baselines first):**
  1. **M1, single-process baselines** (NQ, NB, T × conditions × frame rates), step S3, before any event-clock code. Apply the problem gate.
  2. **M2, network baselines** (NQ, G, T × roles × latency), step S4. Apply the gate.
  3. **M3, after:** the same matrices with AEC (steps S6–S7, full run in Phase 8).
  4. **M4, overhead:** clock off vs on × N = 64 / 256.
- **Rough cost:** M1 ≈ 56 runs, M2 ≈ 72 runs (real time, about 90 s each), about 3 hours together, scripted and unattended through the queue.
- **Environment recorded with every batch:** CPU i7-14650HX, GPU RTX 5070 Laptop (driver recorded per batch), 32 GB RAM, Windows 11 build 26200, AC power and power plan, project on D: (NVMe), engine on C:, UE version and build configuration, resolution, affinity mask, exact command lines.
- **Raw data:** `Benchmarks\Raw\` (manifests in git, captures as release attachments), computed results in `Benchmarks\Summaries\` (naming in [Benchmarks/README.md](../Benchmarks/README.md)).

## 8. Limits we accept up front

- **Events are frame-quantised.** The clock fires in the first frame whose time has passed the due time, so up to one frame late; it never fires early. Handlers receive the exact due time, so code that needs sub-frame accuracy (for example rewinding a hit trace) can use it.
- **On clients, events due before the replicated start arrives fire late.** Physics: the client can't know about an action before it hears of it. They fire once, in the arrival frame, with their lateness reported.
- **Server time on clients, not presentation time.** v1 fires client events on the estimated server clock. A simulated proxy's replicated *movement* is shown about one-way delay behind, so cosmetic effects tied to the pose may prefer a delayed clock. Stated in 02 §12 as a growth path, not built.
- **Server-initiated actions only in v1.** A predicted, client-initiated action (GAS prediction keys) would need the client's start time to be trusted and validated by the server. Out of scope for v1 (02 §11).
- **The animation is not re-timed.** The montage still plays through the engine; the clock only corrects its position when it drifts beyond a threshold (SC6). Root motion and pose timing keep their engine behaviour.
- **Some conditions may pass the gate weakly or not at all.** That is a valid result, and the claims shrink accordingly.
