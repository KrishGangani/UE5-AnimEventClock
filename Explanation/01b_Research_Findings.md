# 01b · Research Findings (Phase 2): AnimEventClock

**Phase:** 2 (Research), completed 2026-11-21 · **Engine:** UE 5.8.4 (CL 58902052), read from `C:\Program Files\Epic Games\UE_5.8\Engine`; for the server bug, also the installed 5.6.1 (CL 44394996) and 5.7.4 (CL 51494982)

**Method:**
- Every engine claim was read by hand in the local source and cites a path plus line numbers. Paths are relative to `Engine/Source/Runtime/Engine/` unless another root is named.
- Claims drawn from following code flow rather than a direct statement are marked **[inferred]**; claims that could not be checked are **[unverified]**; hypotheses that need a run are **[to measure]**.
- The raw notes, with every quote, are in [Phase2_Raw_Notes/](Phase2_Raw_Notes/README.md) (A: notify internals, B: update rate and visibility, C: network roles, D: existing work and assets, E: measurement). **This file is authoritative.**

**Why this phase matters here.** The roadmap's problem statement came from code reading and forum posts. Phase 2 checked each part against the 5.8.4 source. Four parts held, two turned out conditional or narrower, one could not be confirmed, and the prior-art picture is weaker for us than the roadmap said. All of that is stated below, and the scope was adjusted to match.

---

## 1. What Phase 2 changed

| # | Finding | Consequence for AnimEventClock |
|---|---|---|
| 1 | **Both notify tick types fire only in a frame in which the mesh's animation updates.** A montage advances only inside `UAnimInstance::UpdateAnimation` → `UpdateMontage` (`Private/Animation/AnimInstance.cpp:561–586, 764–776`). BranchingPoint fires during that advance at its exact position (`Private/Animation/AnimMontage.cpp:2280–2290, 2885`); Queued fires after evaluation (`Private/Components/SkeletalMeshComponent.cpp:2102–2135`). A frame without an anim update fires neither | **Switching to BranchingPoint is not a fix** for update-rate, budget or visibility problems, which matches the forum report (raw notes D2). The event source must live **outside** the anim update. This is the core design decision |
| 2 | **URO is opt-in and, in its normal ("trail") mode, late and bunched but lossless.** `bEnableUpdateRateOptimizations = false` by default (`SkeletalMeshComponent.cpp:493`). Skipped time is added back at the next update (`Private/Components/SkinnedMeshComponent.cpp:6066–6117`, `SkeletalMeshComponent.cpp:1952–1955`). Off-screen and dedicated-server meshes use `BaseNonRenderedUpdateRate = 4` (`Classes/Engine/EngineTypes.h:2820`; `SkinnedMeshComponent.cpp:292–297`). Player-controlled pawns are exempt "to minimize latency w/ game play events triggered by animation" (`SkinnedMeshComponent.cpp:287, 205–210`). **S3 measured: lossless, but early as well as late.** The skipped time is the mesh's, not the montage's (`SkinnedMeshComponent.cpp:6103–6116`), so a montage started in the middle of a skip interval inherits the time skipped *before* it started and runs up to (rate − 1) frames ahead for its whole life: at rate 4, instant events and window begins fire from 2.9 frames early to 3.9 frames late at 30, 60 and 144 fps (Queued window ends, which start 1–2 frames late, up to 7.4 frames late), none lost (09 S3) | **The problem is conditional, not universal.** With engine defaults a montage notify is within one frame of its time (S3: true for instant events and window begins; Queued window **ends** are 1–2 frames late even at defaults, 09 S2-d). The project claims only what it measures under the settings that large projects enable: URO, the budget allocator, visibility-based ticking. The "default" row of every matrix is the control |
| 3 | **Look-ahead mode exists only for "root motion from everything"** (`SkinnedMeshComponent.cpp:360–364`), advances the animation ahead of world time (`:6119–6152`), and, by a hand reading of its bookkeeping, may drift further ahead each update cycle **[inferred]**. **S3 measured: the drift is real.** At 60 fps and rate 4 the animation runs about 12.5% fast, so events fire up to 0.7 s early by the end of a 6.3 s montage. Read again in the source: the look-ahead amount is sized for a 30 fps target (`TargetFrameTimeForUpdateRate = 1/30`, `SkinnedMeshComponent.cpp:155, 363`), every update ticks the pose by DeltaTime + look-ahead (`SkeletalMeshComponent.cpp:1952–1955`), but the bookkeeping credits only the look-ahead (`SkinnedMeshComponent.cpp:6131–6137`): one frame of extra lead per update, and at 60 fps an update comes every 8 frames (1/8 = 12.5%). In the same runs **59% of the Queued instant events were never fired**; the `rme` control (root motion from everything, URO off) lost **exactly the same events**, so that loss belongs to root-motion mode, not to look-ahead: with root motion from everything, **no rendered mesh ever fires a Queued instant notify** (window states and BranchingPoint notifies are unaffected; 09 S3-f) | "Can fire early in look-ahead mode" holds, but narrowly. The drift reading is a hypothesis (H3), measured before any claim |
| 4 | **`VisibilityBasedAnimTickOption` decides whether off-screen montages advance at all.** `OnlyTickPoseWhenRendered` stops them off-screen and **disables the component's tick at registration on a dedicated server** (`SkeletalMeshComponent.cpp:1068–1071, 1883–1916`). The defaults (`AlwaysTickPoseAndRefreshBones`; `AlwaysTickPose` for `ACharacter`, `Private/Character.cpp:125`) are unaffected | "Depends on visibility" holds **for that setting**. Server-side gameplay events must not depend on whether a mesh ticks: the event clock needs no mesh |
| 5 | **The Animation Budget Allocator throttles like URO and can also disable a mesh, losing its time.** Throttled meshes get the accumulated delta (`Plugins/Runtime/AnimationBudgetAllocator/Source/AnimationBudgetAllocator/Private/AnimationBudgetAllocator.cpp:183–186, 466–480`); disabled ones have `AccumulatedDeltaTime = 0` (`:232–241`), e.g. off-screen meshes beyond `a.Budget.MaxTickedOffsreen = 4` (`:344–389`). `a.Budget.Debug.Force.Rate` forces rates deterministically in Development builds (`AnimationBudgetAllocatorCVars.cpp:130–160`) **[inferred: disabled meshes fall behind permanently]** | Budget tests use the forced rate for exact, repeatable runs, plus one real-budget run reported with its run-to-run range (the real budget depends on measured cost, so it is not deterministic). **Scope limit:** budgeted meshes never register on a dedicated server (`Plugins/Runtime/AnimationBudgetAllocator/Source/AnimationBudgetAllocator/Private/SkeletalMeshComponentBudgeted.cpp:29–36`), so budget conditions are standalone, listen-server and client only, and `a.Budget.Enabled` (default 0) is set at start-up. **S3 correction:** the cvar is not enough. Each world's allocator is created disabled (`AnimationBudgetAllocator.cpp:71`) and runs only when both the cvar and `SetEnabled(true)` are on (`:1191, 1328–1332, 1363–1366`). The first M1 batch set only the cvars, and its budget runs gave the **same fire hash as the default runs**, which is how the mistake was found (07 S3); the harness now enables the world's allocator too. **Measured again (09 S3-f):** the forced rate behaves exactly like URO's trail mode at the same rate, early side included (per key, `aba4` = `uro4`), and loses nothing |
| 6 | **On a server, a remote player's animation advances once per client move, with the client's delta**, not on the server's clock: `bOnlyAllowAutonomousTickPose` (`Character.cpp:2443–2453`) and `MoveAutonomous → TickCharacterPose` (`Private/Components/CharacterMovementComponent.cpp:10664–10722`) **[inferred: server notify timing follows client cadence and network jitter]** | A role the roadmap did not list. "Server, remote player character" becomes its own test role (raw notes E5) |
| 7 | **The 5.6.1 server bug (UE-308913) cannot be confirmed open in 5.8.4.** The poster's path (`MoveAutonomous` dispatch + `ClearQueuedAnimEvents`) is identical in 5.6.1 and 5.7.4 (`CharacterMovementComponent.cpp:10459–10489` / `10532–10561`), but 5.8.4 moved it into `TickCharacterPose` (`:12084–12115`) and added `a.Notifies.DispatchPolicy` ("Useful for multi-tick scenarios such as MoveAutonomous", `AnimInstance.cpp:169–189`). The tracker page returned HTTP 403 **[unverified]** | **No claim that 5.8.4 has the bug.** The README now says "reported in 5.6.1; restructured in 5.8; measured here". It is measured as notify-state begin/end counts on a dedicated server (SC2, E5). Reproducing it on 5.6.1 is an owner decision (it needs a 5.6 build of the harness) |
| 8 | **No engine path replicates a montage *start time*.** `RepRootMotion` carries the track position and corrects actor location (`Classes/GameFramework/Character.h:47–131`; `Character.cpp:1786–1872`). GAS replicates the current position and corrects only beyond 0.1 s, with `SetPosition` (`Plugins/Runtime/GameplayAbilities/Source/GameplayAbilities/Private/AbilitySystemComponent_Abilities.cpp:3292, 3427–3446`). `SetPosition` gathers no notifies (`Classes/Animation/AnimMontage.h:558`) **[inferred: backward corrections repeat notifies; forward ones skip instant BranchingPoint notifies]** | The replicated action carries the **server start time**, and every event has a stable id (action, key, loop pass), so each role fires it **exactly once** no matter how the animation is corrected |
| 9 | **`GetServerWorldTimeSeconds()` on a client is not latency-compensated**: the delta is set on receipt, averaged, with no round-trip term (the function `Private/GameStateBase.cpp:144–152`; the delta `:164–193`) **[inferred: it lags true server time by about the one-way delay]** | A small clock-sync component (Cristian's method, minimum-round-trip sample) gives clients a usable server-time estimate. Without it, "scheduling on server time" would be just as late as starting the montage on receipt |
| 10 | **Notifies are runtime data.** `UAnimSequenceBase::Notifies` is a runtime `UPROPERTY`, sorted by time (`Classes/Animation/AnimSequenceBase.h:41–43`); `GetTriggerTime`/`GetEndTriggerTime` are not editor-only (`Private/Animation/AnimTypes.cpp:81–99`); sections are runtime too (`Classes/Animation/AnimMontage.h:697, 837`) | **The edit-time "bake" from the roadmap is dropped.** A baked copy would be a second source of truth that can go stale. The runtime builds a small timeline from the montage when an action starts. The editor module **validates** authored events instead of baking them |
| 11 | **Prior art is closer than the roadmap said.** Mover (Experimental) makes its simulation "master of montage position" (`Plugins/Experimental/Mover/Source/Mover/Private/MoverComponent.cpp:56–62, 3085–3147`), but still fires notifies through the next anim update. The Able Ability System puts animation on an ability timeline as one task among others (vendor docs; clock details undocumented) | **Gap strength: medium, not strong.** Positioned as: montage-authored events, fired from the gameplay clock on every role, exactly once, with a latency-compensated start on clients, drop-in for `ACharacter`/GAS projects, **with published measurements**. Not "nobody has done this" (§5) |
| 12 | **Hand-written timers are the common workaround.** `FTimerManager` ticks once per frame after `TG_PostPhysics` (`Private/LevelTick.cpp:1816`) and does not depend on the mesh | Timers are a **baseline configuration** (T), not a straw man. Our claim against them is about play rate, sections, interruption, replication and having one source of truth, not about the clock itself |
| 13 | **Assets: the engine's tutorial mannequin is Starter Content; template content is Examples.** EULA: Examples (Samples and Templates folders) may be distributed "to any third party"; Starter Content may not be shared outside a Product (raw notes D5). The tutorial mannequin, its skeleton and two sequences ship in `Engine/Content/Tutorial/…` (raw notes D4); the template mannequins are 126 MB | Reference the tutorial mannequin **by path**, never copy it. This project's own montages are generated by an editor commandlet through the queue. Phase 4 check F-5 confirms the folder cooks. **This is the project's reading of the EULA, not legal advice**; the owner accepted it on 2026-11-21. No engine or template content is ever committed |
| 14 | **The launcher engine has no Server target**, and a packaged Game build can never be a dedicated server (`Engine/Config/BaseEngine.ini:4001–4008`; `Engine/Source/Programs/UnrealBuildTool/Configuration/UEBuildTarget.cs:1396–1417`; `Engine/Source/Runtime/Core/Public/Misc/CoreMisc.h:146–166`). Dedicated servers tick at `NetServerMaxTickRate = 30` (`Engine/Config/BaseEngine.ini:1867`; `Private/GameEngine.cpp:1742–1745`) | Dedicated-server roles run on the **editor binary with `-server`** (check F-7). Server timing error is judged against a 33 ms frame |

**Benchmark-plan changes:**

| # | Finding | Consequence |
|---|---|---|
| B-1 | **Net emulation runs on real time**: `SendTime = FPlatformTime::Seconds() + PktLag` (`Private/NetConnection.cpp:2457, 2666–2673`), set per process with `-PktLag=` (`Private/Net/NetEmulationHelper.cpp:534–645`; `Private/NetDriver.cpp:788`) | **Network runs cannot use the fixed step.** They run in real time with a frame cap, every process pinned to the P-cores, and timing error is measured on the shared wall clock (`QueryPerformanceCounter`). Latency L means `-PktLag=L/2` on both sides |
| B-2 | **Under the fixed step, game time is exact and identical run to run** (UE5-UpdateBudget, its 01b B-1) | Single-process timing error is measured in game time. Repeated runs must agree exactly except where real time leaks in (the real ABA budget); any difference is reported |
| B-3 | **The start frame is ambiguous for notifies [inferred]**: a montage started before its mesh ticks advances a whole frame on its first update (`AnimMontage.cpp:1513–1547`; `LevelTick.cpp:1610`) | The reference time is defined as *the authority's game time in the frame the action starts*; notifies are measured against it, and tick order is one test condition (E3) |

---

## 2. Notify internals (evidence; raw notes A)

| Fact | Evidence (5.8.4) |
|---|---|
| `FAnimNotifyEvent` fields and defaults: `MontageTickType = Queued`, `TriggerWeightThreshold = ZERO_ANIMWEIGHT_THRESH`, `NotifyTriggerChance = 1`, `bTriggerOnDedicatedServer = true` | `Public/Animation/AnimTypes.h:279–390` |
| Tick types: Queued "trigger them at the end of the evaluation phase (faster)"; BranchingPoint "as they are encountered (Slower)" | `AnimTypes.h:84–95` |
| Montage advance: at most 9 sub-steps per update, each stopping at the next branching point; remaining time accumulates only below 10 s | `AnimMontage.cpp:2575, 2582–2593, 2280–2290` |
| Notifies are gathered for `(Previous, Current]`; interrupted montages fire nothing | `AnimSequenceBase.cpp:478`; `AnimMontage.cpp:2822–2826` |
| Queue filters: dedicated server, weight threshold, LOD, chance, filter scope | `Private/Animation/AnimNotifyQueue.cpp:53–77` |
| The queue is cleared at every anim update | `AnimInstance.cpp:832–851` |
| `SetPosition` is a plain assignment; `SetNextPositionWithEvents` fires the range on the next update | `AnimMontage.h:558, 566–569` |

## 3. Update rate, visibility and budget (evidence; raw notes B)

| Fact | Evidence (5.8.4) |
|---|---|
| Mesh components tick in `TG_PrePhysics` | `SkeletalMeshComponent.cpp:441`; `SkinnedMeshComponent.cpp:472`; `Character.cpp:128` |
| "Recently rendered" = rendered within the last **1 s** | `SkinnedMeshComponent.cpp:1864` |
| `TickPose` delta: external (budget) delta, or `DeltaTime + AdditionalTime` with URO; skipped frames call an empty `OnUROSkipTickAnimation` | `SkeletalMeshComponent.cpp:1938–1985`; `Classes/Animation/AnimInstance.h:1353` |
| Rate selection, the force cvar `a.URO.ForceAnimRate` (visible branch only) | `SkinnedMeshComponent.cpp:283–370` (`:354–358`) |
| Visibility options and `ShouldTickPose` | `Classes/Components/SkinnedMeshComponent.h:96–116`; `SkeletalMeshComponent.cpp:1883–1916` |

## 4. Network roles (evidence; raw notes C)

| Fact | Evidence |
|---|---|
| Root-motion montages: the movement component ticks the pose (`TickCharacterPose`) | `CharacterMovementComponent.cpp:2796–2799, 2920–2923, 12027–12118` |
| Clients send moves at most every 0.0166 s, also when stationary | `Engine/Config/BaseGame.ini:33, 35` |
| `a.EnableQueuedAnimEventsOnServer` default 1 in 5.6.1, 5.7.4, 5.8.4 | `CharacterMovementComponent.cpp:151–157` (5.8.4) |
| Autonomous root-motion correction calls `SetPosition` | `CharacterMovementComponent.cpp:11509–11521` |
| GAS replicated montage fields (position, not start time) | `Plugins/Runtime/GameplayAbilities/Source/GameplayAbilities/Public/Abilities/GameplayAbilityRepAnimMontage.h:22–104` |
| Server world time replicated every 0.1 s, client delta without latency term | `GameStateBase.cpp:36` (update rate), `:144–152` (the function), `:164–193` (the delta) |
| Packet emulation compiled in non-Shipping builds | `Classes/Engine/NetDriver.h:450` |

## 5. Existing work, updated (raw notes D)

| Solution | Fires gameplay events independent of the anim update? | Exactly once on every role? | Network start time? | Authored in the montage? | Status |
|---|---|---|---|---|---|
| Queued notifies (default) | No | No (corrections, 5.6.1 report) | No | Yes | Engine |
| BranchingPoint notifies | No (exact position, but only in update frames) | No | No | Yes | Engine |
| `a.EnableQueuedAnimEventsOnServer`, `a.Notifies.DispatchPolicy` | No | Aims at once per move on servers | No | Yes | Engine (DispatchPolicy new in 5.8) |
| `RepRootMotion` | — (location correction) | — | No (position) | — | Engine |
| GAS montage replication | No | No (0.1 s `SetPosition` corrections) | No (position) | Yes | Plugin, stable |
| Mover sim-driven montage | Position yes, notifies no | Not documented | Via the simulation | Yes | **Experimental** |
| Hand-written timers | Yes | Per ability, by hand | By hand | **No** (copied offsets) | Common practice |
| Able Ability System | Not documented | Not documented | Not documented | No (own timeline editor) | Marketplace/Fab product |

**The gap, stated conservatively:** among the public work reviewed, none combines events authored in the montage, fired on the gameplay clock independently of the anim update and visibility, exactly once per role with a latency-compensated start on clients, and published measurements of the failure modes. A web search cannot prove absence, and studio-internal systems are not public. **Gap strength: medium.**

## 6. Hypothesis status after Phase 2

| # | Hypothesis (01 §4) | Status |
|---|---|---|
| H1 | URO/budget throttling makes notifies late and bunched | **Measured (S3): late and also early (up to rate − 1 frames each way), lossless**, URO and forced budget alike. Bunching not observable with this workload's metric |
| H2 | BranchingPoint does not help under throttling | **Measured (S3): confirmed**, the same range as Queued; it only avoids the Queued end lag |
| H3 | Look-ahead makes notifies early, possibly drifting | **Measured (S3): early and drifting** (~12.5% fast at 60 fps, up to 0.7 s early). New, separate: root motion from everything loses every Queued instant notify of a rendered mesh |
| H4 | Visibility-based ticking delays or drops notifies off-screen and on dedicated servers | **Measured (S3, single process): drops** (behind-camera band: all; far band: 24%). **Measured (S4): on a dedicated server every notify is lost**, NPCs and players alike |
| H5 | On servers, remote players' notify timing follows client moves | **Measured (S4): confirmed**: up to 1.9 server frames early on a dedicated server (−62 ms), 2 frames on a listen server, and up to 1.7 frames late |
| H6 | Position corrections on clients cause missed or duplicate notifies | **Measured (S4): confirmed for GAS's montage replication, as duplicates** (7–140 per 1,000 on simulated proxies at every latency, never a miss), through its blend-out restart and forward correction (`09` S4-b); direct montage playback has none |
| H7 | UE-308913 repeats notify-state begin/end on servers | **Measured (S4): not reproduced on 5.8.4** (0 repeats in 2 × 2,544 server windows, 288 of them the remote player's, at 0–200 ms). Reported in 5.6.1; reproducing it there is not needed for the claims (`11`) |
| H8 | A montage's start frame makes "on time" ambiguous by up to ±1 frame (01 §4) | **Measured (S3): confirmed**: started before the mesh's tick, every animation event is 0–0.97 frames early; after it, 0–1 frame late |

## 7. Carried into Phase 3

- The event source is a component that advances its own playhead from the gameplay clock (server time on clients), built from the montage's own notify data; the montage is only the visual follower.
- Two marker classes (an instant event and a window) are what designers place in the montage editor; when the animation fires them they do nothing except report to an optional comparison hook.
- Replicate the start time and an action id; give every event a stable id; never re-fire.
- A clock-sync component on the player controller (Cristian, minimum round trip).
- An editor validator, not a baker.
- Single-process runs: fixed step, game time. Network runs: real time, wall clock, `-PktLag` on both sides, editor `-server` for dedicated servers. Every timed run pinned to the P-cores. **Phase 4 found** that a dedicated server opens `ServerDefaultMap` (engine default: the empty `Entry` map), not `GameDefaultMap`, so all three default maps are set in `DefaultEngine.ini` ([09](09_Testing_And_Benchmarks.md) F-7a; `06` §7).
- Baselines (Queued, BranchingPoint, timers, GAS) are captured **before** the event clock exists, and a "problem gate" decides which conditions the project may claim at all (01 §6.4).
