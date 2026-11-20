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

### Phase 4 re-run (F-11), after two configuration fixes

Two fixes were made after the first job, and both need a full rebuild (a plugin change alters what the monolithic game links):
1. `ServerDefaultMap=/Engine/Maps/Templates/Template_Default` in `DefaultEngine.ini` (F-7a).
2. **`AndroidFileServer` disabled** in the `.uproject`, and its engine-written `DefaultEngine.ini` block (with a random `SecurityToken`) deleted, so no token can reach the public repository (portfolio rule; `05` A1). The token was also removed from this repository's local, unpushed history.

| # | Check | Command | Result |
|---|---|---|---|
| F-11 | Editor + game builds, package, packaged smoke, dedicated-server smoke, all again; `DefaultEngine.ini` unchanged afterwards; the server opens `Template_Default` | The same chain as the first job, queued as one job | *(pending: queue job running at the time of writing)* |

**What F-7a taught us:** a project that will run dedicated servers must set **all three** default maps. Without the fix, the first networked run would have connected clients to an empty `Entry` map, and the failure would have looked like a harness bug. Found in Phase 4, before any network code exists, and fixed with one config line.

**Also observed (no action needed):**
- The first job's editor run made the engine's `AndroidFileServerEditor` plugin append its settings block to `DefaultEngine.ini`. Handled by fix 2 above.
