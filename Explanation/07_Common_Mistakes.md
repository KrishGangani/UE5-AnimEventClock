# 07 · Common Mistakes: AnimEventClock

**Status:** filled in **step by step** in Phase 5 (current to S2), consolidated in Phase 9. Mistakes a code review **actually caught in this project** are marked ⚠ caught.

**Will contain:** for every module, the mistakes that are easy to make here, how to spot them, how to prevent them and how to debug them. Mistakes a code review actually caught in this project will be marked as such.

## Phase 4 (foundation)

| Mistake | How to spot it | How to prevent it | How to debug it |
|---|---|---|---|
| Setting only `GameDefaultMap` in a project that runs dedicated servers (made here, check F-7a) | The server log says `Bringing World /Engine/Maps/Entry.Entry up for play`; clients land in an empty world | Set `ServerDefaultMap` too (`06` §7) | Grep the server log for `LoadMap:` |
| Typing an editor-only module `Runtime`, or adding it to the game target | The game target fails to build once the module uses `UnrealEd` | `"Type": "Editor"` and only the editor target's `ExtraModuleNames` (`06` §3–4) | The UBT error names the editor-only module |
| Leaving a default-on plugin that writes secrets into project config (`AndroidFileServer`'s random `SecurityToken` in `DefaultEngine.ini`; made here, first Phase 4 job) | A `[/Script/AndroidFileServerEditor.AndroidFileServerRuntimeSettings]` block appears in `git diff` after an editor run | Disable the plugin in the `.uproject` and delete the block (`05` A1) | `git diff Config/` after every editor or cook job |
| Copying a smoke-script flag without checking it exists (`-ExitAfterSeconds`) | Nothing: an unknown flag is silently ignored | Grep the engine source for the parameter before relying on it | `grep -r "ExitAfterSeconds" Engine/Source` finds nothing in 5.8.4 |

**Planned topics** (from 02 §8 and 01b): relying on notifies for gameplay timing under URO, the budget allocator or visibility-based ticking; using BranchingPoint as a timing fix; using `GetServerWorldTimeSeconds()` as if it were latency-compensated; correcting montages with `SetPosition` and expecting notifies to fire; firing events again after a client resync; destroying the owner inside an event handler; calling `Montage_JumpToSection` on the anim instance and bypassing the clock; placing markers on slot-track sequences.

## S1: marker notifies and the asset commandlet

| Mistake | How to spot it | How to prevent it | How to debug it |
|---|---|---|---|
| Doing gameplay inside a marker's `Notify` | Gameplay timing depends on the mesh updating again (`01b #1`) | Markers only report to the comparison hook; the clock fires gameplay (S6) | Search the marker classes for anything but the broadcast |
| Assuming a BranchingPoint `Notify` gets a full `FAnimNotifyEventReference` | `EventReference.GetNotify()` is null for BranchingPoint | Identify the event by its own data (`EventName`), not by the reference | Log `GetNotify()` in both montage variants |
| Changing `MontageTickType` without `RefreshCacheData()` | `_B` montages fire their markers as Queued, or not at all as branching points | Refresh after editing notifies (`06` §12) | The asset test checks `IsBranchingPoint()` per marker |
| Placing a marker exactly on a segment or section edge | It fires a frame early or late depending on the ±0.1 ms edge offset | Keep markers off edges (`AECDemoMontages.h`) | Compare `GetTime()` with `GetTriggerTime()` |
| Copying engine content into `/Game` to edit it | Starter Content ends up in a public repository (EULA, `01b #13`) | Reference engine assets by path; generate your own montages | `git status` shows engine asset names under `Content/` |
| A commandlet in a runtime module | Shipping builds contain editor code, or the game target fails to link | Editor-only module, `"Type": "Editor"` | The game build's UBT log |

## S2: the benchmark director

| Mistake | How to spot it | How to prevent it | How to debug it |
|---|---|---|---|
| ⚠ caught: **a failed spawn shifts every later index** (states sized to N, characters appended only on success) | Out-of-bounds access in `Tick`, or a character driven by another's schedule | Abort the run on a failed spawn, so all arrays share one index | Force a spawn failure; the run must refuse to start (exit 3) |
| ⚠ caught: **hashing absolute frame numbers** | The determinism check fails between identical runs that needed a different number of loading frames | Count frames from `OnWorldBeginPlay` | Compare the first fire's frame in two runs |
| ⚠ caught: **mixing interrupted window ends into the timing error** | p99 and max of the animation path inflated by the 0.1 s blend-out | A separate `interrupted_end` bucket | The bucket's errors cluster at the blend-out time |
| ⚠ caught: **characters that move in one condition only** | In look-ahead (root motion from everything) the movement component simulates controller-less characters, and off-floor ones fall | `DisableMovement()` for every character | Log positions at the end of a `la4` run |
| ⚠ caught: **cvars that outlive the run**, and then **restoring them at the wrong priority** | A second PIE run inherits the previous condition; after a hand-written restore the value is stuck at code priority, above config and device profiles | Set at `ECVF_SetByTemp` with a tag; `UnsetAllConsoleVariablesWithTag` removes exactly that layer | Read the cvar and its priority back after `Deinitialize` |
| Found by the S2 check: **an order-dependent determinism hash** | Identical runs disagree although the same fires happened in the same frames | Sort fires by (frame, action, key, path) before hashing: queued notifies from different meshes are dispatched in parallel-task order | Diff the two event files as sorted sets |
| ⚠ caught: **exit code 0 when the results were not written** | A batch script trusts a run that produced nothing | Exit code 4 on a failed write | The runner also checks that the files exist |
| Starting actions in a tick whose order relative to the mesh is not fixed | Notify errors move by one frame between runs | A tick prerequisite (pre) or a tick known to run after the meshes (post) | `-AECStart=pre` vs `post` (H8) |
| Letting shadows mark off-screen meshes as rendered | The behind-camera band never reaches URO's non-rendered rate | `SetCastShadow(false)` on the demo meshes | `a.URO.Draw 1` in a windowed run |
| Setting mesh flags after `BeginPlay` | URO or the budget allocator ignores them | Deferred spawn: set, then `FinishSpawning` | Read the flags back in the log |
| A timer with a zero delay | It silently never fires (`SetTimer` with rate ≤ 0 sets nothing) | `FMath::Max(Delay, UE_KINDA_SMALL_NUMBER)` | Missed timer-path events at position 0 |

## S3: the single-process baselines (M1)

| Mistake | How to spot it | How to prevent it | How to debug it |
|---|---|---|---|
| Found by the M1 batch: **taking the budget allocator's cvar for its on switch** | The budget runs (`aba2`, `aba4`) gave exactly the default runs' numbers **and the same fire hash** | Enable both switches: `a.Budget.Enabled 1` and the world's allocator, `IAnimationBudgetAllocator::Get(World)->SetEnabled(true)` (it starts disabled, `AnimationBudgetAllocator.cpp:71`; both are tested at `:1191, 1366`) | Compare a condition's fire hash with the default's: a condition that changes nothing gives the same hash. Cheap, and it caught this; every batch summary now warns about it (`run_bench.py`, simplify finding) |
| **A metric that is (almost) zero by construction** | `bunched_share` (two keys of one action fired in one frame) was 0.000 in every run but one combination | Check that a metric *can* be non-zero on the workload before reading a zero as "not bunched". Keys of one action are at least 0.19 s apart (Combo: `ComboWindow` ends at 0.42 and `B1` is at 0.45 of 6.27 s); instant events move between 2.9 frames early and 3.9 late, so they never meet. Only a Queued window end, up to 7.4 frames late at 30 fps under URO rate 4, can reach the next key's frame: `bunched_share` 0.043 there, 0 elsewhere. "Bunched" across characters is a different question, not measured by this metric | Compare, per condition, the smallest gap between keys of one action (in frames) with the largest measured delay |
| **Reading "within one frame" too literally at a frame boundary** | p99 = max = exactly 1.000 frame for instant events at 30 and 60 fps | Know the inputs: `ComboWindow` begins at 25% of 6.2667 s = 1.566667 s, exactly 94 frames at 60 fps (47 at 30), and fires one frame later; every exact-1-frame error in the batch is that key at play rate 1.0 | List the events whose error equals one frame and look for a common key |
| **Explaining a symptom with the first plausible cause** | Look-ahead (`la4`) both drifts early and loses Queued instant events; URO alone does neither | Separate the switches: `la4` = root motion from everything + URO rate 4, so S3 added `rme` (root motion from everything, no URO). It lost exactly the same events with URO off: the loss is root-motion mode's, the drift is look-ahead's (09 S3-f). Naming look-ahead as the cause would have been wrong | Run the control; compare per key (`Tools\event_stats.py --keys <run> <path>`) |
| **Assuming URO can only be late** | Negative errors (up to 2.9 frames early) in the URO trail-mode runs, which have no look-ahead | The skipped time belongs to the mesh, not the montage (`SkinnedMeshComponent.cpp:6103–6116`): a montage started mid-interval inherits time from before its start | `event_stats.py --keys` by play rate: the early errors are bounded by (rate − 1) frames in every band |

## S4: the network harness

| Mistake | How to spot it | How to prevent it | How to debug it |
|---|---|---|---|
| Found by reading before the first run: **a server-initiated GAS activation without an event tag** | The owning client's ability activates with no event data, so no montage plays on the autonomous proxy | Give the event a tag: the client passes its event data only if the tag is valid (`AbilitySystemComponent_Abilities.cpp:2453`); a native tag needs no ini (`06` §35). The automation test checks the tag is registered | G runs: the auto role misses every event while sim does not |
| **Treating a multicast as client-only** | Every server action handled twice | `NetMulticast` runs on the server too: return on authority | Count server-side calls of the handler |
| **Setting the view target on a dedicated server** | A remote client's view jumps to an actor it does not have | On a dedicated server the first player controller is a remote client's: only touch a local controller's view | Log `IsLocalController()` where the view is set |
| **A player character with movement disabled, or flying only until possessed** | The server never ticks the remote player's pose (no client moves), or the character falls after possession | The client must keep sending moves; `Restart` re-applies the default movement mode (`Character.cpp:1375–1390`), so set `DefaultLandMovementMode` too (`06` §32) | Read the movement mode on both sides after possession |
| **Letting the server quit first** | The client never writes its files: it loses its server, travels to the default map and keeps running until the runner kills it | The server tells its clients and waits 3 s before quitting; a client that loses its server anyway quits with exit code 5 (`06` §37, simplify finding) | The client log ends with "Lost the server" instead of "Run files written" |
| **Deciding a world's role from the command line alone** | A client process's first world (before it connects) would start a recorder with nothing to record | Decide the role in `OnWorldBeginPlay` from the world's net mode, which is final by then (`06` §30) | Log the net mode at begin play |
| **Letting the editor server write to the runner's console** | The queue log fills with the dedicated server's log lines | Child output to nowhere; each process writes its own `-abslog` | — |
| ⚠ caught: **attributing a client fire to the action whose start message came last** | In G with latency, a simulated proxy's montage (GAS's replicated property) can start before the start RPC arrives, so its fires would go to the previous action | Hold a G client's fire that comes after the character's current action is over, and give it to the next action when its start arrives | `unattributed` in the client's file header; per-action duplicates on the client only |
| Found by simplify: **one configuration decided in several places** | The director tested "G" at spawn, at play and at stop | A virtual play / stop on the character class chosen at spawn | — |
| ⚠ caught: **an ability that never ends** | With `bAllowInterruptAfterBlendOut = false`, an interruption after the natural blend-out began fires no delegate | Create the task with `bAllowInterruptAfterBlendOut = true` | The ability's active count grows through a run |
| ⚠ caught: **a recorder that reallocates its arrays mid-run** | Client frame-time spikes that look like network jitter | Reserve on the recorder path too | Client frame-interval p99 against the server's |

## S5: the pure timeline

| Mistake | How to spot it | How to prevent it | How to debug it |
|---|---|---|---|
| ⚠ caught: **a step cap that ends normal actions** | A looping action stops firing after a long frame or a late join | Guard only against steps that make no progress | A 200-loop advance in the tests |
| ⚠ caught: **pairing a window's keys by name and order** | An inverted (zero-length) window keeps no partner; code that indexes the partner crashes | Link the two keys when they are added, re-point them after sorting | The inverted-window test |
| ⚠ caught: **a window left without an end** | A loop or a jump out of a window: begin, begin, begin with no end; "is the window open" stays true | Close open windows with forced ends at every discontinuity and at a stop | The cut-window and jump tests |
| ⚠ caught: **a float step that stops one ulp short of a section end** | A key at the end and the section change slip to the next frame | Snap the step onto the end when the delta reaches it | The snap test (0.1 + 0.6 vs 0.7) |
| ⚠ caught: **a test tolerance tighter than float inputs allow** | `0.5f − 0.2f` is not 0.3 in double | Use binary-exact inputs (0.25, 0.5) or a tolerance that fits float | — |
| A key exactly at a section's start | It fires only the first time through a loop | Include the start position after a start, a loop back or a jump (but not on a continuation, where the previous section already crossed it) | The loop test: the boundary key fires on pass 1 again |
| Found by simplify: **entering a window partway without beginning it** | A start, a loop or a jump into the middle of a window: no begin, no end, "is the window open" false, while the notify state does begin | Begin every window that spans the entry point (`AnimSequenceBase.cpp:478` is the engine's rule) | The start-inside-a-window test |
| A test body that uses a namespace's helpers without opening it | `error C3861: 'Describe': identifier not found` (the S5 first build) | `using namespace AECTimelineTests;` at the top of every test body that uses them, or qualify them | The compiler names the line |
| Found by simplify: **two versions of "is this a loop back"** | A loop and a jump disagreed on zero-length sections | One `EnterSection` for every entry that is not a continuation | — |

## S6: the event clock component

| Mistake | How to spot it | How to prevent it | How to debug it |
|---|---|---|---|
| Rewriting the crossing buffer while handlers run | A handler's call advances the clock and refills the list being dispatched | Calls from a handler never advance (`CatchUp` returns the handled due time); `AdvanceTo` checks it is not inside a dispatch; re-check after every call (02 §8) | The `check` in `AdvanceTo` |
| Closing windows from the end-of-frame playhead after a handler's stop | An interrupted end carries the pass of a loop crossed later in the same frame, not its begin's | Rewind the playhead to the handled crossing before the timeline's `Stop` (simplify finding) | — |
| Applying a stop at "now" when it comes from a handler | Events later in the same frame still fire after a "cancel on hit" | From a handler, a stop takes effect at the due time of the event being handled | `aec.Debug 1` (not covered by the shipped test) |
| Finishing the frame's crossings after a handler jumped | Keys of the section the jump left fire anyway, or the target section's keys come a frame late | A path serial: dispatch stops when it changes; the rest of the frame is advanced again on the new path | `aec.Debug 1` (not covered by the shipped test) |
| Jumping from the playhead's end-of-frame state inside a handler | The windows the jump leaves close late (at the action's end) or not at all; a forward jump reports a new pass | Each crossing records where the playhead was; a handler's jump starts from there | `aec.Debug 1`: the pass of the target section's events (not covered by the shipped test) |
| Sending a window end whose begin never went out | A handler sees an end without a begin (after a jump cut the frame, or with `aec.Enabled` toggled) | The component keeps its own open-window list; an end goes out only if its begin did | — |
| Firing the last window ends while the action still counts as active | A handler that starts the next action from a window end re-enters the old action | Mark the action finished first, then send its closing ends | — |
| Driving the montage from the clock in a comparison run | The NQ/NB path would play the montage twice | `bDriveMontage = false` when another path plays it | — |
| A component that ticks when idle | Cost per character even without actions (SC5) | Tick only while an action is active | `ActiveActions` CSV stat vs characters |
