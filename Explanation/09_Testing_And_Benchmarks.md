# 09 · Testing And Benchmarks: AnimEventClock

**Rule:** every check is recorded with its date, its exact command and its result, **including failures**. The full functional and benchmark sections are filled in during Phases 5–8. Results from earlier phases are recorded as they happen. Every Unreal job ran through the machine-wide queue (`tools\ue_queue.py`); the job id is given so the log can be found.

---

## Phase 4 · Foundation checks (2026-11-21)

Queue job `20261121_011055_94541a` ran F-1 to F-7a in one chain (`build.bat AnimEventClockEditor` && `build.bat AnimEventClock` && `package.bat` && `smoke_game.bat packaged` && `smoke_game.bat server`): **exit 0 in 252.5 s**.

| # | Check | Command (from `P06_AnimEventClock\`) | Result |
|---|---|---|---|
| F-1 | Editor target builds | `Tools\build.bat` (`AnimEventClockEditor Win64 Development`) | ✅ Succeeded in 65.3 s; 22 actions (4 module DLLs); **0 errors, 0 warnings**. Toolchain: MSVC 14.44.35229, Windows SDK 10.0.22621.0; shared PCH `SharedPCH.UnrealEd.Project.ValApi.ValExpApi.Cpp20` |
| F-2 | Game target builds | `Tools\build.bat AnimEventClock Development` | ✅ Succeeded in 40.3 s; 24 actions; `AnimEventClock.exe` linked; **0 errors, 0 warnings** |
| F-3 | Package a Development build | `Tools\package.bat` | ✅ BUILD SUCCESSFUL; BuildCookRun 130.4 s; 535 packages cooked, cook result **"Success - 0 error(s), 0 warning(s)"**; staged and archived to `Build\Windows\` (≈ 990 MB, git-ignored) |
| F-4 | Packaged build starts and quits (no GPU) | `Tools\smoke_game.bat packaged` (`Build\Windows\AnimEventClock.exe -nullrhi -nosound -unattended -traceautostart=0 -ExecCmds="quit"`) | ✅ Exit 0. Plugin `AnimEventClockCore` mounted; `Template_Default` loaded in 0.022 s (`max tick rate 0`: uncapped standalone); **0 errors, 0 warnings** in `Smoke_packaged.log` (770 lines). The Enhanced Input warning seen in UE5-SpawnPacer's first smoke run does not appear, thanks to `DefaultInput.ini` (`05` A11) |
| F-5 | The tutorial mannequin is cookable (asset decision, `01b #13`) | Same package job; cook metadata `AnimEventClock\Saved\Cooked\Windows\AnimEventClock\Metadata\ReferencedSet.txt` | ✅ All **9** packages of `/Engine/Tutorial/SubEditors/TutorialAssets/Character` are in the cooked set (`TutorialTPP`, `TutorialTPP_Skeleton`, `TutorialTPP_PhysicsAsset`, `TutorialTPP_Mat`, `TutorialTPP_AnimBlueprint`, `TutorialCharacter`, `Tutorial_Idle`, `Tutorial_Walk_Fwd`, `NewBlendSpace1D`), with 0 cook warnings. This 5.8 cook writes to the Zen store, so there are no loose cooked `.uasset` files to list; the cook's own referenced-set file is the evidence |
| F-6 | The two engine plugins are usable on the launcher engine | Server smoke log (F-7a) | ✅ `Mounting Engine plugin AnimationBudgetAllocator`, `… GameplayAbilities`; `UnrealEditor-AnimationBudgetAllocator.dll`, `UnrealEditor-GameplayAbilities.dll` and `UnrealEditor-GameplayAbilitiesEditor.dll` loaded from the engine's precompiled binaries |
| F-7a | The editor binary runs as a **dedicated server** (the launcher engine has no Server target, `01b #14`) | `Tools\smoke_game.bat server` (`UnrealEditor-Cmd.exe AnimEventClock.uproject -server …`) | ✅ Exit 0. `IpNetDriver listening on port 7777`; `Bringing World … up for play (max tick rate 30)`: the 30 Hz dedicated-server cap, as read in the source. `AnimEventClock` and `AnimEventClockCore` loaded; **neither editor module loaded** (correct for `"Type": "Editor"`). 0 errors; 21 warnings, **all** `LogEditorDataStorageUI: Unable to register widget factory …` from the editor binary itself (engine-side, none from this project). **❌ Finding: the server opened `/Engine/Maps/Entry`, not `Template_Default`.** A dedicated server opens `ServerDefaultMap` (engine default `Entry`), not `GameDefaultMap` (`06` §7) |
| F-7b | The same after setting `ServerDefaultMap` | A server-only rerun was queued, then **withdrawn** before it started, because the plugin change below needs a full rebuild and repackage anyway; folded into F-11 | — |
| F-8 | The P-core affinity helper works (not a UE job) | `python -I Tools\cpu_affinity.py`, then a test that calls `pin_self(performance_core_mask())`, starts a child Python process with `Popen` that reads its own mask and exits with code 7 | ✅ Prints `performance cores: 0xFFFF`. After `pin_self`, `applied_mask()` reads back `0xFFFF (performance cores only; hybrid CPU)`; the child reports mask `0xffff` (system mask `0xffffff`: 24 logical CPUs), so it **inherited** the mask, and its real exit code 7 passed through `Popen` |
| F-9 | `smoke_game.bat` argument handling (no UE started) | `smoke_game.bat bogus -foo`; `smoke_game.bat packaged` before any package existed | ✅ Unknown mode → exit 2 with a message; missing packaged build → exit 3 with a message |
| F-10 | Documentation audit before the Phase 1–4 commit | `python -I tools\check_links.py P06_AnimEventClock`; a grep of the folder for the banned words and personal paths | ✅ 72 relative links checked, **0 problems**; **0** banned-word matches; every `File:line` citation in 01b, 02 and the raw notes was opened in the 5.8.4 source while writing |

### Phase 4 re-run (F-11), after two configuration fixes ✅

Two fixes were made after the first job, and both need a full rebuild (a plugin change alters what the monolithic game links):
1. `ServerDefaultMap=/Engine/Maps/Templates/Template_Default` in `DefaultEngine.ini` (F-7a).
2. **`AndroidFileServer` disabled** in the `.uproject`, and its engine-written `DefaultEngine.ini` block (with a random `SecurityToken`) deleted, so no token can reach the public repository (portfolio rule; `05` A1). The token was also removed from this repository's local, unpushed history.

| # | Check | Command | Result |
|---|---|---|---|
| F-11 | Editor + game builds, package, packaged smoke, dedicated-server smoke, all again; `DefaultEngine.ini` unchanged afterwards; the server opens `Template_Default` | The same chain as the first job, queue job `20261121_015010_b62c2f` | ✅ **Exit 0 in 105.3 s.** Editor target succeeded in 11.8 s and game target in 21.1 s (incremental: the plugin change re-links), both **0 errors, 0 warnings**. Package: BUILD SUCCESSFUL, BuildCookRun 58.9 s, 533 packages cooked (2 fewer than before, the disabled plugin's), cook **"Success - 0 error(s), 0 warning(s)"**; all 9 tutorial packages still in `ReferencedSet.txt`. Packaged smoke: exit 0, `Template_Default` loaded, **0 errors, 0 warnings**. Dedicated server: exit 0, **`LoadMap: /Engine/Maps/Templates/Template_Default`**, `IpNetDriver listening on port 7777`, `max tick rate 30`, game and plugin runtime modules loaded, editor modules not loaded, 0 errors, 21 warnings all `LogEditorDataStorageUI` (engine-side, as in F-7a). No `AndroidFileServer` plugin mounted. `git status` after the job: **no file changed**, so `DefaultEngine.ini` stayed free of the token block |

**What F-7a taught us:** a project that will run dedicated servers must set **all three** default maps. Without the fix, the first networked run would have connected clients to an empty `Entry` map, and the failure would have looked like a harness bug. Found in Phase 4, before any network code exists, and fixed with one config line.

**Also observed (no action needed):**
- The first job's editor run made the engine's `AndroidFileServerEditor` plugin append its settings block to `DefaultEngine.ini`. Handled by fix 2 above.

---

## Step S1 · Markers and demo assets (2026-11-21)

Queue job `20261121_025909_07dd51` (`build.bat` editor && game && `make_assets.bat` && `run_tests.bat AnimEventClock.Assets`): **exit 0 in 51.3 s**. The tree already held the S2 sources, so this job compiled both steps.

| # | Check | Result |
|---|---|---|
| S1-a | Editor and game targets build | ✅ Editor 13.0 s, game 14.3 s; **0 errors, 0 warnings** |
| S1-b | The commandlet makes the four montages | ✅ Exit 0, "Success - 0 error(s), 0 warning(s)". Source `Tutorial_Idle`: **6.2667 s**. Saved `AM_Attack_{Q,B}` (1 section) and `AM_Combo_{Q,B}` (3 sections), 5 markers each; the log lists every marker's trigger time (e.g. `AM_Attack`: Wind 0.7520, HitWindow 1.8800, Hit1 2.2560, Hit2 3.5093, Recover 5.5147 s) |
| S1-c | The saved assets match the spec | ✅ `AnimEventClock.Assets.DemoMontagesMatchSpec`: **Success** (sections, links, every marker's class, start, end, tick type and branching-point flag, in all four montages) |
| S1-d | The assets are small and contain no local paths | ✅ 13–15 KB each; a `strings` scan finds no drive or user path |

**Finding that changes later steps:** the engine sequence is 6.27 s long, so actions are longer than planned (01 §6.1 updated: about 3,500 events per 64-character run instead of 7,700; two runs per combination give about 7,000).

## Step S2 · Workload and harness, baseline paths only (2026-11-21)

Queue job `20261121_032110_2ef0a1` (build editor && game && package && `run_bench.py --configs NQ,NB --conds def --fps 60 --runs 2 --capture 20 --label s2check`): **exit 0**. Packaged Development, fixed step 60 fps, real GPU, 1280×720, pinned (`applied_mask` = `0xFFFF (performance cores only; hybrid CPU)`), 64 characters, warm-up 5 s. Batch `2026-11-21_034755_s2check` ([summary](../Benchmarks/Summaries/2026-11-21_034755_s2check_summary.md)).

| # | Check | Result |
|---|---|---|
| S2-a | Build and package | ✅ Both targets 0 errors / 0 warnings; cook "Success - 0 error(s), 0 warning(s)" |
| S2-b | Every run starts, finishes and writes its files | ✅ 5 runs (1 priming), each exit 0 in 11–35 s, 0 log errors, 0 warnings, 0 unattributed fires, 1,192 expected events per run (20 s capture), 0 missed, 0 duplicates on every path |
| S2-c | **Determinism** (same seed, two runs) | ⚠ NB and T: identical hash. **NQ: the hash differed.** The fires were the same set (same action, key, path and frame for all 3,174), only their *order* inside a frame differed: queued notifies from different meshes are dispatched in the order their parallel evaluation tasks finish. **Fix:** hash a copy sorted by (frame, action, key, path). Re-checked in S2-g |
| S2-d | Default condition, 60 fps: within one frame? | **Instant events and window begins: yes**, error 0.5–16.7 ms on both paths (NQ instants 0.51–14.67 ms). **Queued window ENDS: no**, 18.0–31.3 ms (1 to 2 frames late) in every band. BranchingPoint window ends stay within one frame. The timer path is 0–14.7 ms everywhere. A new observation, explained by the source: in the frame a Queued notify state's end is crossed, the state is still gathered (`EndTime > PreviousPosition`, `AnimSequenceBase.cpp:478`), so `NotifyEnd` fires only at the next dispatch, when the state is no longer in the queue. Recorded for the S3 gate |
| S2-e | Interrupted window ends | 9 per run; animation path 16.7 ms after the stop (the next anim update), timer path at the stop. Kept in their own bucket |
| S2-f | Tool self-tests | ✅ `event_stats.py --selftest` (percentiles; miss, duplicate, bunching, outside-capture and interrupted-end cases); `run_bench.py --dry-run` prints the rotated order |
| S2-g | Re-check after the code review and simplify changes | ✅ Queue job `20261121_035758_6ced61` (the first part of the S3 job): editor 12.1 s and game 18.3 s, **0 errors, 0 warnings**; `AnimEventClock.Assets.DemoMontagesMatchSpec` **Success**; package BUILD SUCCESSFUL, cook "Success - 0 error(s), 0 warning(s)". Batch `2026-11-21_041947_s2recheck` ([summary](../Benchmarks/Summaries/2026-11-21_041947_s2recheck_summary.md)): 5 runs, exit 0, 0 log errors, 0 warnings, 0 unattributed. **Same hash in every run of a config, the priming run included** (NQ `0x01067BAE` ×3, NB `0x415A67C0` ×2). Every statistic equals the S2 check to the last digit, so the review and simplify changes did not change what is measured |

**Audit trail for S1 and S2** (built, reviewed and checked together, one commit):
- `code-review` (medium), 8 findings, all fixed: a failed spawn shifting every index (now aborts the run, exit 3); interrupted window ends mixed into timing error (own bucket); absolute frame numbers in the hash (now counted from `OnWorldBeginPlay`); characters that could move or fall under look-ahead (`DisableMovement` for all); "pre" start order making notifies look a frame early (the default is now "post"; "pre" stays as the H8 variant); cvars leaking into later PIE runs; exit code 0 on a failed write (now 4); docs out of date (05, 06, 07, 02, 03 updated).
- `simplify` (reuse, simplification, efficiency, altitude), applied: `PairKey` replaces three name searches for a window's other key; the schedule stream moved into the character state; one `ClearTimers` helper; start-order and fixed-step text helpers; `HashFires` once per write; dead guards removed; cvars set at `ECVF_SetByTemp` with a tag and undone with `UnsetAllConsoleVariablesWithTag` (the hand-written restore would have left them stuck at code priority); `FPlatformTime::ToSeconds64`; `AddTickPrerequisiteActor`; the character's actor tick enabled only in "pre" mode; `Actions`/`Fires` reserved up front so no measured frame pays for a reallocation; the commandlet's `MakeMontage` a free function; `collections.Counter`, `refuse_overwrite`, a shorter `is_failure` in the tools.
- `simplify`, skipped on purpose: a shared key-reading function in the plugin (it belongs to the S5 timeline; the director will switch to it then, with the same output); passing the notify event reference through the hook (BranchingPoint passes an empty one, so it cannot identify the montage instance); a condition table instead of the if-ladder (eight conditions, one place); clearing `bAutoManageActiveCameraTarget` instead of re-applying the view target (the network steps want the bench camera regardless of possession); one PowerShell call for the environment (once per batch).
