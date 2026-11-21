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

## Step S3 · Single-process baselines (M1) and the problem gate (2026-11-21)

Queue job `20261121_035758_6ced61` (the S2 re-check above, then three batches): **exit 0 in 2,221 s**. Packaged Development, fixed step, real GPU 1280×720 windowed, pinned (`applied_mask` = `0xFFFF`), 64 characters, warm-up 5 s, capture 60 s, seed 1, NQ and NB (T recorded in the same runs), 1 priming run + 2 interleaved runs per combination.

| Batch | Conditions × frame rates | Runs | Summary |
|---|---|---|---|
| `2026-11-21_042059_m1_f60` | def, uro, uro2, uro4, la4, aba2, aba4, vis at 60 fps | 32 + 1 | [summary](../Benchmarks/Summaries/2026-11-21_042059_m1_f60_summary.md) |
| `2026-11-21_043638_m1_fps` | def, uro4, aba4 at 30 and 144 fps | 24 + 1 | [summary](../Benchmarks/Summaries/2026-11-21_043638_m1_fps_summary.md) |
| `2026-11-21_045238_m1_pre` | def at 60 fps, "pre" start order (H8) | 4 + 1 | [summary](../Benchmarks/Summaries/2026-11-21_045238_m1_pre_summary.md) |

**Health of the batches:** all 63 runs exit 0, 0 log errors, 0 log warnings, 0 unattributed fires, 3,507 expected events per run (3,501 with "pre"). **Every combination gave the same fire hash in both runs** (and in its priming run), so each number below is exact for this workload, not a sample. The summaries were regenerated with `run_bench.py --summarize` after the gate table was added (`05` D3); the raw files and manifests are the originals.

### S3-a · The gate, per condition (p99 |error| in frames; worst of 30 / 60 / 144 fps where measured)

The gate (01 §6.4): **a real problem** if p99 |error| > 1 frame, or any missed or duplicate event. Judged separately on window **ends** and on the rest (instant events and window begins), because Queued window ends are late at defaults (S2-d).

| Condition | NQ instants + begins | NQ window ends | NB instants + begins | NB window ends | T | Verdict |
|---|---|---|---|---|---|---|
| `def` (control) | 1.00, min 0.02 | **1.44–1.99** (always 1–2 frames late) | 1.00 | 0.44–0.99 | ≤ 0.88 | **NQ window ends only**; everything else passes |
| `def`, "pre" start | 0.97, **min −0.97** | 0.88 | 0.97, **min −0.97** | 0.92, min −0.92 | 0.88 | passes, but every animation event moves up to one frame **early** (H8) |
| `uro` (engine rates) | **3.20**, min −2.88 | **6.88** | **3.20**, min −2.88 | **2.88** | ≤ 0.88 | **problem** (both notify types) |
| `uro2` | **3.20**, min −2.88 | **6.88** | **3.20**, min −2.88 | **2.88** | ≤ 0.88 | **problem** |
| `uro4` | **3.75–3.94**, min −2.94 | **5.63–7.44** | **3.75–3.94**, min −2.94 | **2.54–3.44** | ≤ 0.88 | **problem** at 30, 60 and 144 fps |
| `la4` (look-ahead) | **14.8**, min **−15.8**, **2,758 missed** (59% of the instant events; no begin missed) | **32.1**, min −32.1 | **40.8**, min **−41.9** | **32.1** | ≤ 0.88 | **problem**, the largest |
| `vis` (`OnlyTickPoseWhenRendered`) | 1.00, **2,382 missed** (41%) | 1.88, **472 missed** | 1.00, **2,382 missed** | 0.88, **472 missed** | ≤ 0.88, 0 missed | **problem** (lost events) |
| `aba2` (budget, forced rate 2; S3-f) | **2.00**, min −0.97 | **2.88** | **2.00**, min −0.97 | 0.92 | ≤ 0.88 | **problem** |
| `aba4` (budget, forced rate 4; S3-f) | **3.75–3.94**, min −2.94 | **5.62–7.44** | **3.75–3.94**, min −2.94 | **2.54–3.44** | ≤ 0.88 | **problem** at 30, 60 and 144 fps; the same numbers as `uro4` |
| `rme` (root motion from everything, no URO; S3-f) | 1.00, **2,758 missed** (59% of the instant events) | 1.88 | 1.00 | 0.88 | ≤ 0.88 | **problem** (Queued instants lost); BranchingPoint unaffected |

"1.00" is not "more than one frame": it is a single key (`ComboWindow` begin at play rate 1.0, due at exactly 94 frames at 60 fps and 47 at 30) that fires one frame after a due time lying exactly on a frame boundary (07 S3). No duplicate event appeared in any single-process run.

### S3-b · The budget runs measured nothing (a harness mistake, found by the hash)

Every `aba2` and `aba4` run gave **exactly the default run's fire hash** (NQ 60 fps `0x3858EDC1`, NB `0xF8854965`, the same at 30 and 144 fps): the budget allocator never ran. The harness set `a.Budget.Enabled`, `a.Budget.Debug.Force` and `a.Budget.Debug.Force.Rate`, but each world's allocator is created disabled and also needs `SetEnabled(true)` (`06` §26, `01b #5`, `07` S3). Fixed in `PrepareCondition`; every batch summary now warns when a condition gives the default's hash (`05` D3). The re-run is S3-f.

### S3-c · What the data says about each hypothesis

| # | Hypothesis | Result |
|---|---|---|
| H1 | URO and the budget allocator make notifies late and bunched (not lost) | **Late, and also early; never lost.** The forced budget rate gives exactly URO's numbers at the same rate (S3-f). Instant events and window begins land between **2.9 frames early and 3.9 frames late** at rate 4, at 30, 60 and 144 fps alike (so in ms the error shrinks with frame rate: −98…+131 ms at 30 fps, −20…+26 ms at 144). The early side is new: the time a mesh skipped is added to every montage at the next update, including time skipped before the montage started (`SkinnedMeshComponent.cpp:6103–6116`), so a montage started mid-interval runs up to (rate − 1) frames ahead for its whole life (`event_stats.py --keys`: bounded by 2.9 frames in every band). Queued window ends add their own 1–2 frames (up to 7.4). **Bunching** could not be observed with this metric: keys of one action are too far apart (07 S3); it is not claimed |
| H2 | BranchingPoint does not help under throttling | **Confirmed.** NB has the same instant/begin range as NQ under every URO condition (−2.9…+3.9 frames). It only avoids the Queued end lag |
| H3 | Look-ahead fires early, possibly drifting | **Confirmed, drifting.** The lead grows through the montage (about −0.2 s at the window begin, −0.47 s at its end, up to −0.7 s): the animation runs ~12.5% fast at 60 fps, which the source explains (one frame of uncredited lead per look-ahead update, `01b #3`). **Also: 59% of the Queued instant events (2,758 of 4,656 in two runs) were never fired**, all in the visible bands; no window begin was lost, and BranchingPoint lost nothing. **The `rme` control (S3-f) separates the two:** the drift comes from look-ahead (`rme` has none), the lost events from root motion from everything (`rme` loses **the same 2,758 events**, with URO off) |
| H4 | Visibility-based ticking delays or drops notifies off-screen | **Drops, confirmed.** With `OnlyTickPoseWhenRendered`, the behind-camera band fires **nothing** (all 1,148 events per run missed, both notify types), and 24% of the far band's events are lost too, although that band is in front of the camera (**[inferred]** small far meshes that are culled or occluded count as not rendered). Visible meshes are on time. The timer path loses nothing |
| H8 | The start frame makes "on time" ambiguous by ±1 frame | **Confirmed.** Started before the mesh's tick ("pre"), every animation event moves between 0 and 0.97 frames **early**; started after it ("post", the default), between 0 and 1 frame late. The same montage, the same frame, a one-frame difference in what "on time" means. The timer path is unaffected |

### S3-d · The gate's verdict (what the project may claim)

- **Real problems on 5.8.4, single process:** URO in every form (rate by screen size, forced 2 and 4; both notify types; early and late), the budget allocator's throttling (forced 2 and 4: the same as URO), look-ahead (drift), root motion from everything (**every Queued instant event of a rendered mesh lost**), `OnlyTickPoseWhenRendered` (lost events off-screen and on small far meshes), and, at defaults, **Queued window ends** (1–2 frames late, always).
- **Not a problem at defaults:** instant events and window begins, Queued or BranchingPoint, at 30, 60 and 144 fps; and the timer path (T) in every condition (0–0.88 frames, never early, nothing lost). T is therefore the baseline to beat on the authority, and AEC's single-process claim is about matching T's timing while keeping authoring in the montage, play rate, sections and replication (01 §6.4: "where a baseline already meets a criterion, that is reported").
- **Not run in S3, and why:** the budget allocator's off-screen cap needs game-side significance (`SetComponentSignificance` with "tick even if not rendered"), a policy of the game rather than an engine default; it moves to Phase 8 with the real-budget run ([11](11_Optimization_Decisions.md)).

### S3-e · Tools and audit

- `Tools\event_stats.py` rewritten around one shared metric with `instant+begin` / `end` buckets and `abs_p99` (`05` D2); its old buckets give identical numbers on all 73 recorded runs (checked by script). Self-test extended (a path that never fired, the new buckets, `--keys`).
- `Tools\run_bench.py`: the gate table, the "changed nothing" warning, `--summarize` (`05` D3).
- `code-review` (medium), 8 findings: the doc claimed a re-measurement that had not happened yet, and the fix was not built yet (both: this section and S3-f); the first gate script dropped a path that fired nothing (fixed by seeding the paths, now in `event_stats`); 05/06 not updated (done: `05` D, `06` §26); a delay bound in 07 that the data contradicted, and a range in 01b that held only for instants (both corrected); unescaped batch globs and a crash on runs without a fixed step (gone with the script).
- `simplify`, applied: the gate became two buckets of the shared metric instead of a second copy of it (reuse, simplification and altitude agreed); one `RAW_DIR`; `summarize` for the per-key spread; the budget branch returns early on a failed cvar and then calls `SetEnabled(true)`; the "changed nothing" warning in every summary. Skipped: a separate `-AECRootMotion` axis instead of the named `rme` condition (named conditions are the documented design, 01 §6.2); a faster CSV reader (the whole gate takes 1.5 s); generating `CONDITIONS` from the C++ list (it buys a check before the engine starts).

### S3-f · Re-run after the budget fix, with the `rme` control

Queue job `20261121_045524_9c2717` (build editor && game && `run_tests.bat AnimEventClock` && package && two batches): editor and game builds **0 errors, 0 warnings**; `DemoMontagesMatchSpec` **Success**; cook "Success - 0 error(s), 0 warning(s)". Batches `2026-11-21_051003_m1_fix` (def, aba2, aba4, rme at 60 fps; [summary](../Benchmarks/Summaries/2026-11-21_051003_m1_fix_summary.md)) and `2026-11-21_051748_m1_fixfps` (aba4 at 30 and 144 fps; [summary](../Benchmarks/Summaries/2026-11-21_051748_m1_fixfps_summary.md)): every run exit 0, 0 log errors, 0 unattributed, the same hash in both runs of every combination.

| # | Check | Result |
|---|---|---|
| S3-f1 | The rebuilt binary changes nothing at defaults | ✅ `def` gives **the M1 hashes** again (NQ `0x3858EDC1`, NB `0xF8854965`) |
| S3-f2 | The budget allocator now runs | ✅ New hashes for every `aba` combination, and the "changed nothing" warning is gone for them. Forced rate 2: instant events and window begins −0.97…+2.00 frames; forced rate 4: −2.94…+3.94 frames at 30, 60 and 144 fps, Queued window ends up to 7.4 frames late; nothing lost. **Per key, `aba4` gives exactly `uro4`'s errors** (for example `Hit1` at rate 1.0: −39.3 / −6.0 / +10.7 ms in both): the allocator's forced rate adds back skipped time the same way as URO's trail mode (`01b #5`), early side included |
| S3-f3 | Does look-ahead or root motion lose the Queued instants? | **Root motion from everything.** `rme` (no URO) loses **exactly the same 2,758 instant events as `la4`**, and nothing else: its timing otherwise equals `def` (no early errors, so the drift is look-ahead's), and its BranchingPoint runs have **the default's hash** (the summary's "changed nothing" warning for `rme` / NB is correct: root-motion mode alone does not move BranchingPoint notifies or window states) |
| S3-f4 | Which meshes lose them? | **Every rendered mesh, and no other.** All 807 instant events of the near band; 572 of the far band's 757; none of the behind-camera band's 764. The 185 far-band events that survive are **exactly** the 185 that the `vis` run lost (its non-rendered far meshes): the two sets are disjoint and together cover the band. So with root motion from everything, a mesh that renders never fires a Queued instant notify; window states and BranchingPoint notifies still work |

**Why, as far as the source shows** (**[inferred]**; the measured facts are the rows above). With root motion from everything, the movement component ticks the pose itself every frame, even with movement disabled (`CharacterMovementComponent.cpp:2791–2799`), and that update runs immediately on the game thread (`AnimInstance.cpp:1028–1029`). Montage notifies queued by such an autonomous tick are applied only by a dispatch made during an autonomous tick (`AnimInstance.cpp:953–957`); `TickCharacterPose`'s own dispatch requires a pending graph update (`CharacterMovementComponent.cpp:12085–12111`, the condition at `:12091–12097`), which the immediate update has already finished (`AnimInstance.cpp:892`), so the queued instants reach a later dispatch that skips them. Why a non-rendered mesh keeps them was not traced; it is recorded as open (Phase 6 re-reads it, `11` lists an Anim Blueprint run).

**Effect on the claims:** root motion from everything is a real, large failure mode for Queued notifies (H3's "lost events" belong to it, not to look-ahead), and look-ahead's drift is separate. Both enter the claims list. The budget allocator's forced throttling is a problem exactly like URO's.

## Step S4 · Network harness, smoke and the M2 baselines (2026-11-21)

### S4-a · Build, tests and a standalone regression

Queue job `20261121_052421_922f53` (build editor && game && `run_tests.bat AnimEventClock` && package && `run_bench.py … --label s4regress` && `run_net.py … --label s4smoke`): **exit 0 in 562 s.**

| # | Check | Result |
|---|---|---|
| S4-a1 | Editor and game targets build | ✅ 19.0 s and 21.9 s, **0 errors, 0 warnings** (the S4 code compiled on its first build, after code review and simplify) |
| S4-a2 | Automation tests | ✅ `AnimEventClock.Assets.DemoMontagesMatchSpec` and the new **`AnimEventClock.Net.HarnessClassesConfigured`**: Success |
| S4-a3 | Package | ✅ BUILD SUCCESSFUL, cook "Success - 0 error(s), 0 warning(s)" |
| S4-a4 | **Standalone behaviour unchanged** | ✅ Batch `2026-11-21_062044_s4regress` (NQ, NB, def, 60 fps, 20 s): NQ hash **`0x01067BAE`** in all three runs, NB **`0x415A67C0`**: the S2 re-check's hashes. The new character class split, the shared `BeginAction` and the net code change nothing in a single-process run |

### S4-b · Network smoke (8 runs, one per combination, 20 s capture)

Batch `2026-11-21_062153_s4smoke` ([summary](../Benchmarks/Summaries/2026-11-21_062153_s4smoke_summary.md)): dedicated (editor `-server`) and listen (packaged) server × NQ and G × 0 and 100 ms added round trip; 16 NPCs + 1 remote player; pinned.

| # | Check | Result |
|---|---|---|
| S4-b1 | Every run starts, connects, finishes and writes both processes' files | ✅ 8 of 8: server and client exit 0 in 40–47 s; the dedicated server is ready in 8.5–9.1 s, the listen server in 2.0 s; **53 of 53 actions received** by the client in every run; 0 log errors; **0 unattributed fires** on either side |
| S4-b2 | The dedicated server holds 30 Hz | ✅ Frame interval median 33.3 ms, p99 34.3–34.4 ms in every run. Listen server and clients: 16.67 ms (the `t.MaxFPS 60` cap), with start-up hitches up to 2 s before the capture |
| S4-b3 | Latency shows up where it should | ✅ Client error median about one-way delay + 20 ms: 2–6 ms at 0 ms, 58–75 ms at 100 ms round trip; the server's own fires do not move with latency |
| S4-b4 | Log warnings | The editor server logs 21 engine warnings (`LogEditorDataStorageUI`, as in Phase 4). GAS runs add one per process: "No GameplayCueNotifyPaths were specified". Harmless (no cues exist); `DefaultGame.ini` sets the path after the M2 batch, so all M2 runs share one configuration (`07` S4) |

**First readings (one run each, to be confirmed by M2):**
- **G duplicates on simulated proxies, even at 0 ms.** In 3 of 4 G runs, 10–14 duplicate fires: every event of one attack fires a second time, in one frame, at 6.017 s, the moment the montage begins its natural blend-out. Read in the source: once the simulated proxy's local montage is blending out it counts as stopped, while the server's replicated info still says "playing"; GAS then **restarts** the montage (`AbilitySystem.AllowSimulatedMontageRestart`, default on, `AbilitySystemComponent_Abilities.cpp:55, 3327–3335`) and, finding the restarted copy more than 0.1 s behind the replicated position, **jumps it forward firing every notify in between** (`:3418–3446`). This is H6's "corrections cause duplicates", in a form the Phase 2 reading did not predict.
- **The remote player's events fire early on the server** (median −9 to −11 ms, as early as −35 ms, in server game time), while NPCs on the same server are 0–2 frames late: the server advances that character's pose by the client's moves (H5).

### S4-c · The M2 batch: dedicated and listen server × NQ, G × 0 / 100 / 200 / 100+jitter ms

Batch `2026-11-21_071356_m2` ([summary](../Benchmarks/Summaries/2026-11-21_071356_m2_summary.md)), queue job `20261121_062439_3c67ef` (exit 0, 3,670 s for both M2 batches): 16 NPCs + 1 remote player, 60 s capture after a 5 s warm-up, real time, `-PktLag` = half the round trip on both processes (jitter runs: ±10 ms), 2 runs per combination after one discarded priming run, pinned to the P-cores. T is recorded in every run beside the configured path. **32 of 32 runs: both processes exit 0, 0 log errors, every action received by the client, 0 missed events on any path or role.** Per run: 928 expected events (777 instants and window begins, 151 window ends), 874 of NPCs and 54 of the remote player; per server type, 2,544 windows in the batch (2,400 of NPCs, 144 of the remote player).

Error = fire − due on the wall clock (due: the server's game time, mapped through its frame log); per phase bucket, median over the two runs of each run's p99 (range in brackets in the summary):

| Where | Path | Instants and window begins, p99 | Window ends, p99 | Earliest | Missed / duplicates |
|---|---|---|---|---|---|
| Dedicated server (30 Hz), game time | NQ, G | **33.1–33.2 ms** (0.99–1.0 frame) | **58–66 ms (1.7–2.0 frames)** | **−47 to −62 ms: the remote player's events** | 0 / 0 |
| Dedicated server, game time | T | 32.7–33.2 ms | 24–33 ms | ≥ 0 | 0 / 0 |
| Listen server (60 fps), game time | NQ, G | 14.7–14.8 ms (0.88 frame) | **31.5–31.8 ms (1.9 frames)** | −16 to −33 ms: the remote player's | 0 / 0 |
| Listen server, game time | T | 14.7–14.8 ms | 14.8–15.1 ms | ≥ 0 | 0 / 0 |
| Client (60 fps), wall clock, 0 ms | NQ | 10–16 ms | 27–33 ms | −15 ms | 0 / 0 |
| Client, 0 ms | G | 15–16 ms | 32–33 ms | −14 ms | **0 / 7–18 per 1,000 (simulated proxies)** |
| Client, 100 ms (± 10 ms) | NQ / G | 77–83 ms | 94–100 ms | +35 to +52 ms | NQ 0 / 0; **G 0 / 20–140 per 1,000** |
| Client, 200 ms | NQ / G | 121–149 ms | 138–166 ms | +85 to +96 ms | NQ 0 / 0; **G 0 / 67–117 per 1,000** |
| Client, any | T (started on the start message) | ≈ the notifies' + up to 1 frame | the same | ≥ the one-way delay | 0 / 0 |

Frames: the dedicated server holds 33.2–33.3 ms (p99 34.3–34.4 ms, worst 47 ms); listen server and clients 16.67 ms (p99 16.7 ms), with single hitches of up to 130 ms.

**What it shows.**
- **On a server, notifies are on time only for NPC instants.** NPC instants and window begins are within one server frame (the engine's (A, B] rule at the server's tick); **Queued window ends are 2 server frames late (66 ms at 30 Hz)**, the S3 end lag at the server's rate; and the remote player's events fire **early**, by up to 1.9 server frames (−62 ms) on the dedicated server and 2 frames (−33 ms) on the listen server, because that character's pose advances by the client's moves (**H5**). The timer path on the same characters is never early and never more than a frame late.
- **On a client, every path is late by the one-way delay plus up to two client frames** (NQ instants: median 72 ms, p99 83 ms at 100 ms round trip; 111–124 / 121–149 ms at 200 ms). Timers started on the start message are no better: the start itself is late. This is the latency S7 compensates.
- **GAS repeats events on simulated proxies at every latency** (**H6**): 7–140 duplicates per 1,000 events, 2–40 repeated window begins/ends per run, never a missed one; the mechanism is the one read in S4-b (the proxy restarts its blended-out montage and jumps it forward, firing everything in between). Direct montage playback (NQ), which replicates no position, has **0** duplicates.
- **Nothing is ever lost** in the default condition, on any role, path or latency.
- **UE-308913 is not reproduced on 5.8.4 (H7).** 0 repeated notify-state begins or ends on either server, in 2,400 NPC windows and 144 windows of the remote player's character (whose montage the server advances in `TickCharacterPose`, the path the report is about) per server type, for both NQ and G, at 0–200 ms.

### S4-d · Conditions on the dedicated server (URO, visibility), 0 ms

Batch `2026-11-21_080140_m2_cond` ([summary](../Benchmarks/Summaries/2026-11-21_080140_m2_cond_summary.md)): dedicated server × NQ, G × `uro` (URO on every NPC mesh) and `vis` (`OnlyTickPoseWhenRendered`), 2 runs each; 8 of 8 runs exit 0.

| Condition | Server, NPCs (NQ, G) | Server, remote player | Client (simulated proxies) | Timers on the server |
|---|---|---|---|---|
| `uro` | instants and begins **3 frames early to 4 late** (−100 ms earliest, +131 ms p99), ends up to **7.7 frames late** (258 ms); nothing lost | unchanged (a player's pawn is exempt from URO) | NQ unchanged; **G 100 ms early** (−115 ms; the proxy is corrected towards the server's montage, which URO runs ahead) and **75 duplicates per 1,000** | 0–1 frame |
| `vis` | **every notify lost** (1,000 per 1,000) | **every notify lost** | unchanged (the client renders) | 0–1 frame, nothing lost |

On a dedicated server nothing is rendered, so URO always uses its non-rendered rate (4) and `OnlyTickPoseWhenRendered` stops every mesh (01b #2, #4): the S3 single-process results, but for **every** character on the server, including the remote player under `vis`.

### S4-e · Hypotheses H4–H7 and the gate (01 §6.4)

| # | Result |
|---|---|
| H4 | **Confirmed on dedicated servers:** `OnlyTickPoseWhenRendered` loses every server-side notify, for NPCs and players alike |
| H5 | **Confirmed:** a remote player's notifies fire up to 1.9 server frames early on a dedicated server, 2 frames on a listen server, and are up to 1.7 frames late (45–56 ms p99 on the dedicated server) |
| H6 | **Confirmed for GAS's montage replication, as duplicates:** 7–140 per 1,000 on simulated proxies at every latency, up to 75 per 1,000 with 100 ms early events under server URO; no missed events. Not seen with direct montage playback (no position replication) |
| H7 | **Not reproduced on 5.8.4:** 0 repeated begin/end in 2 × 2,544 server windows (+ 636 in the URO runs) |

**Gate (M2): problem on every network role for the notify paths.** On servers: window ends 2 frames late at defaults; remote-player events up to 2 frames early; under URO 3 frames early to 4 late; under `OnlyTickPoseWhenRendered` nothing at all. On clients: one-way delay + up to 2 frames on every path, and GAS duplicates. **Pass:** nothing missed at defaults; no repeated notify-state begin/end on servers (H7); timers on the authority (0–1 frame, the T rows; the dedicated server's wall-clock p99 is 34 ms, 1.02 frames, from frame-to-frame jitter, while its game-time p99 is within the frame). With the S3 gate, this fixes what the event clock is measured against in Phase 8: **on servers**, timer-grade timing for every character in every condition (SC1); **on clients**, the delay compensated (SC3, SC4) and no duplicate (SC2), where the notify paths are late by the one-way delay and GAS repeats.

**Owner decision 1, checked:** the 5.6.1 reproduction of UE-308913 was worth doing only if 5.8.4 showed no server-side notify problem. It shows several (late ends, early remote-player events, URO, visibility), so it stays an optional Phase 8 item that is not needed for the claims ([11](11_Optimization_Decisions.md)).

**Audit trail (S4 part 2).** No code changed: the measurements, these documents, and `DefaultGame.ini`'s `GameplayCueNotifyPaths` (S4-b4), set after the batch so every M2 run shares one configuration. The per-phase split above (instants and begins vs window ends, server game time) comes from the batch manifests through `event_stats.analyse_net` (the same buckets as S3's gate).

**Audit trail for S4 (before its first build):**
- `code-review` (medium), 5 findings, all fixed: in G, a simulated proxy's fires could go to the previous action when GAS's montage property arrives before the start message (now held and given to the next action; G clients only); the recorder did not reserve its arrays (it now sizes them from the same switches as the server); the summary described the due moment wrongly (it is interpolated between server frames); a server without NPCs would never become the director (`run_net.py` refuses `--chars 0`); an interruption after the natural blend-out would never end the G ability (`bAllowInterruptAfterBlendOut`).
- `simplify`, applied: the montage play / stop of an action became virtual on the character (G overrides it), so the configuration is decided once, by the class spawned; `BeginAction` / `InterruptAction` shared by server and client; the band derived, the per-character role removed (it is a parameter of each start); one `RecordFire` with an explicit-time form for held fires; the public `TriggerAbilityFromGameplayEvent` instead of the internal activation; the event tag declared once and used by the test; the client's actor tick switched off; net mode and config cached at begin play; the frame log built with one reserved string; a lost server ends a client with exit code 5; in the tools, the batch protocol (list parsing, run rotation, pinning, process kill, raw-file moves, log copy) moved into `run_bench.py` and is shared by `run_net.py`; one events pass for both clocks; one place for the window and workload switches; the server's log read incrementally while waiting. `run_bench`'s output is identical (checked on the S3 batches).
- `simplify`, skipped on purpose: pairing each montage start with its start message instead of a time window (GAS's own simulated-proxy restarts would be counted as starts); replicating the server's config to clients (the runner always passes the same one, and a mismatch shows as unattributed fires); waiting for every client to disconnect instead of a fixed 3 s (the client now quits on its own if the server goes first); `K2_EndAbility` for the normal end (the explicit call states what is replicated).

## Step S5 · The pure timeline (2026-11-21)

Queue job `20261121_082648_0f6dba` (build editor && game && `run_tests.bat AnimEventClock` && package && `run_bench.py --configs NQ,NB --conds def --fps 60 --runs 2 --capture 20 --label s5regress`): **exit 0 in 176 s.** The first attempt, job `20261121_065609_730c5f`, failed to compile (exit 6): one test body used the test helpers without opening their namespace (07 S5); fixed and re-queued.

| # | Check | Result |
|---|---|---|
| S5-1 | Editor and game targets build | ✅ **0 errors, 0 warnings**; package BUILD SUCCESSFUL, cook "Success - 0 error(s), 0 warning(s)" |
| S5-2 | Automation tests | ✅ **12 of 12**: the two earlier ones (`Assets.DemoMontagesMatchSpec`, `Net.HarnessClassesConfigured`) and ten `AnimEventClock.Timeline.*`: the half-open range, keys at the start and end (and the snap onto a section end), the section path (every key once, the boundary key once, a skipped section; each crossing's recorded section and position), loops (a new pass; a window cut by the loop point ends every pass; forced ends in key order), jumps (a forced end, pass rules, a fresh playhead, a start inside a window; the recorded playhead state), the zero-length-loop guard (and a window open when it trips), an empty timeline, due time and play rate, stops and the montage's end closing open windows (two windows in key order; an inverted window), a (pass, position, key) path-order check over loop / jump / entry / stop cases, and `BuildFromDemoMontages` (all four demo montages: sections, links, keys, positions, pairs; one long advance crosses every key once) |
| S5-3 | **The director's keys now come from the timeline** (`FAECMontageTimeline::Build`), with nothing else changed | ✅ Batch `2026-11-21_091624_s5regress` ([summary](../Benchmarks/Summaries/2026-11-21_091624_s5regress_summary.md)): NQ hash **`0x01067BAE`** in all three runs, NB **`0x415A67C0`** in both: the S2 and S4 hashes. The timeline reads the same positions the hand-written reading did |

**Audit trail for S5.**
- `code-review` (medium) on the first draft, 9 findings, all fixed, among them: a step cap that would end a normal loop on a long advance (the guard now counts only steps in a row without progress); window keys paired by name and order (an inverted window lost its partner: they are linked when added and re-pointed after sorting); windows left without an end by a loop, a jump or a skip (forced ends); a float step stopping one ulp short of a section end (snapped onto it); a test tolerance tighter than float inputs allow; the pass rules for a jump onto the current position; three names shadowing members (`Start`/`End` → `SectionStart`/`SectionEnd`, `StartPosition`/`EndPosition`).
- `simplify`, applied: `CloseOpenWindows` adds the forced ends directly (no copy of the list, no search); a dead arm of a ternary removed; one `Describe` helper in the tests; the open-window list on an inline allocator; **windows entered partway now begin at the entry** (the engine's rule for notify states, `AnimSequenceBase.cpp:478`; an altitude finding: a start, loop or jump into a window gave neither begin nor end); one `EnterSection` for every entry that is not a continuation; `Build` uses the montage's own section lookups; the director builds its keys with `FAECMontageTimeline::Build` and the build test joined the asset test file, sharing its tolerance and flags (reuse findings). Skipped on purpose: inlining `FirstKeyFrom` (its name documents the bound choice); one shared section layout for every test (each test reads on its own); the director keeping the whole timeline instead of its keys (the keys are all it needs).
- **Three changes after that review**, found while drafting S6 and S7: a window still open at the montage's end now ends there (it would otherwise have been closed by the component at the frame's time, not the path's); every crossing records the playhead's section and position (a handler's jump starts from there, S6); forced ends come in key order, so every crossing of a path is ordered by (pass, position, key), which S7 compares against the server's cuts. A second `code-review` (medium) on these three found the empty-step guard and the no-section exit still finishing a path with windows open (fixed: every way a path ends closes its windows) and six test or comment gaps (fixed: the tests above assert the recorded state and the path order; header comments; the 05 wording). One pre-existing cost was left for Phase 8: each section entry scans the keys before it for spanning windows (11).

## Step S6 · The event clock (authority) and the final job (2026-11-21)

Queue job `20261121_105610_a0ed8e` (build editor && game && `run_tests.bat AnimEventClock` && package && `run_bench.py --configs NQ --clock --conds def,uro4 --fps 60 --runs 2 --capture 20 --label final`): **exit 0 in 186 s.** Owner direction for this last step: one job, no new campaigns, no further code review; S7 (replication, clock sync) and S8 (validator) were dropped. Earlier S6 jobs were cancelled before they ran.

| # | Check | Result |
|---|---|---|
| S6-1 | Editor and game targets build, package | ✅ 0 errors, 0 warnings; cook "Success - 0 error(s), 0 warning(s)" |
| S6-2 | Automation tests | ✅ **13 of 13**: the 12 of S5 and `AnimEventClock.Clock.DueTimesAndDestroyDuringDispatch` (every key once with its exact due time, never early, under one frame late at play rate 2; a handler that destroys the actor mid-dispatch stops dispatch, and later frames are safe). The other handler cases drafted for S6 (stop, jump, new action during dispatch) were cut to one test by the owner's limit and are **not tested** |
| S6-3 | The event clock beside NQ in one batch | ✅ Batch `2026-11-21_105804_final` ([summary](../Benchmarks/Summaries/2026-11-21_105804_final_summary.md)): see the table below; both runs of each condition have the same hash |

**Audit trail for S6.** `code-review` (medium) on the drafts, 6 findings, all fixed (a handler's jump started from the end-of-frame playhead and counted as a new pass; `PlayAction` after the owner was destroyed; `aec.Enabled` toggled mid-window; window state during closing ends; the bench's attribution after a hitch). `simplify` on the tree diff: applied (no copy of the crossings, `CatchUp`, the timeline's own `Stop` for closing ends from the rewound playhead, a cached mesh, one counters struct, the engine's test world, the bench's single clock call); skipped with reasons in `11`.

## Success criteria: what was measured (01 §6.4)

| # | Criterion | Result | Source |
|---|---|---|---|
| SC1 | AEC on time on the authority, [0, 1 frame), every condition | **Partly measured.** Defaults and URO rate 4 at 60 fps: p99 1.00 frame, never early (earliest +0.03), latest exactly 1.00 frame (on the bound, not below it), 0 missed, 0 duplicates, unchanged by URO. Other conditions, 30 / 144 fps, dedicated and listen servers: **not measured** | `final` batch |
| SC2 | Exactly once on every role, 0–200 ms | Authority: 0 missed, 0 duplicates (2,384 events per condition). Clients: **not measured** (no replicated clock: S7 not built). Baselines: NQ 0 / 0 on clients; G 7–140 duplicates per 1,000 | `final`, `m2` |
| SC3 | On time on clients | **Not measured** (S7 not built). Baselines: every path late by the one-way delay + up to 2 frames | `m2` |
| SC4 | Clock sync within one server frame | **Not measured** (S7 not built) | — |
| SC5 | Cost per event and per action | **Not measured** (the CSV stats exist, `08` §1; no overhead batch was run) | — |
| SC6 | The animation still follows | **Not measured** (the bench drove the montage through NQ; the follower was not exercised) | — |
| SC7 | Validator: 6 of 6 seeded defects, 0 false positives | **Not measured** (S8 not built; drafts parked outside the repository) | — |

