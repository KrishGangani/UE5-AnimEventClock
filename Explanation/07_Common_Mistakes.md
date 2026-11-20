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
