# 05 · Line By Line Code Explanation: AnimEventClock

**Rule:** every file that exists in `AnimEventClock\` (and every tool in `Tools\`) is explained here, line by line, in the same step it is written. Sections follow the build order in [03_Implementation_Roadmap.md](03_Implementation_Roadmap.md).

| Part | Step | Files |
|---|---|---|
| A | Phase 4, F1–F4 (foundation) | `.uproject`, two `Target.cs`, four `Build.cs`, four module `.cpp`, `.uplugin`, three config `.ini`, `Tools\*.bat`, `Tools\cpu_affinity.py` |

---

## Part A · Foundation (Phase 4)

### A1. `AnimEventClock.uproject`: the project descriptor

```json
{
	"FileVersion": 3,
	"EngineAssociation": "5.8",
	"Category": "",
	"Description": "P06 AnimEventClock: gameplay events on the gameplay clock (UE 5.8.4 portfolio project).",
	"Modules": [
		{ "Name": "AnimEventClock",       "Type": "Runtime", "LoadingPhase": "Default" },
		{ "Name": "AnimEventClockEditor", "Type": "Editor",  "LoadingPhase": "Default" }
	],
	"Plugins": [
		{ "Name": "AnimEventClockCore",       "Enabled": true },
		{ "Name": "AnimationBudgetAllocator", "Enabled": true },
		{ "Name": "GameplayAbilities",        "Enabled": true },
		{ "Name": "AndroidFileServer",        "Enabled": false }
	]
}
```

| Line | What it does, and why |
|---|---|
| `"FileVersion": 3` | The descriptor schema the engine expects. 3 is what Epic's 5.8 templates use. A wrong value makes the editor refuse or upgrade the file |
| `"EngineAssociation": "5.8"` | Which installed engine opens the project. A launcher build is identified by its version string; blank would make the launcher ask |
| `"Category"`, `"Description"` | Shown in the project browser. Cosmetic |
| `"Modules"` | Every C++ module the **project** owns (plugin modules are listed in their `.uplugin`) |
| `AnimEventClock` / `"Runtime"` | The game module, built into game **and** editor targets. Its name must match `Source\AnimEventClock\` and the class in `AnimEventClock.Build.cs` |
| `AnimEventClockEditor` / `"Editor"` | The editor-only game module (the demo-asset commandlet, S1). `Editor` = "Loads only when the editor is starting up" (`Engine/Source/Runtime/Projects/Public/ModuleDescriptor.h:109–110`), so a packaged game never contains it. *Alternative:* `UncookedOnly`, which would also load it in `-game`/`-server` editor runs where it is useless |
| `"LoadingPhase": "Default"` | Loaded at the normal point, after engine start-up and before the first world exists: early enough for UClasses (and, later, subsystems) to register before the map loads (`06` §4) |
| `AnimEventClockCore` | The project's own plugin. It is `EnabledByDefault`, but listing it makes the dependency visible in one place |
| `AnimationBudgetAllocator` | Engine plugin, **off by default** (`Engine/Plugins/Runtime/AnimationBudgetAllocator/AnimationBudgetAllocator.uplugin`: `"EnabledByDefault": false`). Needed for the budget conditions (`01` §6.2). Enabled in Phase 4 so the foundation build proves the launcher engine ships it as a usable binary plugin |
| `GameplayAbilities` | Engine plugin, off by default. Needed for baseline G (S4). Enabled now for the same reason. It pulls in its own plugin dependencies (for example `GameplayTagsEditor`, `DataRegistry`), which the engine enables automatically |
| `AndroidFileServer` / `"Enabled": false` | Engine plugin, **on** by default (`Engine/Plugins/Runtime/AndroidFileServer/AndroidFileServer.uplugin:15`). Its editor half writes a `[/Script/AndroidFileServerEditor.AndroidFileServerRuntimeSettings]` block with a **random `SecurityToken`** into `DefaultEngine.ini` the first time the editor runs; it did so in this project's first Phase 4 job. A token must not reach a public repository, and the project has no Android target, so the plugin is switched off and the block was deleted (portfolio rule, found while auditing UE5-SpawnPacer). *Alternative rejected:* deleting the block alone, which the editor would write again on its next run |

*Not listed:* `EnhancedInput` and `DataValidation` are enabled by default in 5.8 (`EnhancedInput.uplugin:13`, `DataValidation.uplugin`: `"EnabledByDefault": true`); the validator (S8) will declare `DataValidation` in the plugin's own `.uplugin` when it starts to depend on it.

### A2. `Source\AnimEventClock.Target.cs` and `AnimEventClockEditor.Target.cs`: build targets

```csharp
using UnrealBuildTool;

public class AnimEventClockTarget : TargetRules
{
	public AnimEventClockTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("AnimEventClock");
	}
}
```

| Line | What it does, and why |
|---|---|
| `using UnrealBuildTool;` | Target files are **C#**, compiled by UBT (`04` §1). Brings `TargetRules`, `TargetType` and friends into scope |
| `public class AnimEventClockTarget : TargetRules` | UBT finds targets by class name `<Name>Target`, so this defines the target `AnimEventClock` (that is why `build.bat AnimEventClock` works) |
| `(TargetInfo Target) : base(Target)` | UBT passes the platform, configuration and architecture; the base constructor stores them |
| `Type = TargetType.Game` | A standalone game executable: no editor code; the target that gets packaged. The editor file sets `TargetType.Editor`, which builds DLLs loaded by `UnrealEditor.exe`. **No `Server` target:** the launcher engine refuses it (`01b #14`), so dedicated servers run on the editor binary with `-server` |
| `DefaultBuildSettings = BuildSettingsVersion.V7` | The newest versioned set of default build options; the value Epic's 5.8 template uses |
| `IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8` | Pins 5.8's engine header include order, so a future engine upgrade can't silently change what our code includes |
| `ExtraModuleNames.Add("AnimEventClock")` | Which of the project's modules this executable builds. Plugin modules come in through the plugin system |

**The editor target differs in one line:** `ExtraModuleNames.AddRange(new string[] { "AnimEventClock", "AnimEventClockEditor" })`. The editor executable needs the editor-only game module too; the game target must not have it (it would fail to link against editor-only engine modules once S1 adds them).

### A3. `Source\AnimEventClock\AnimEventClock.Build.cs`: the game module's build rules

```csharp
public class AnimEventClock : ModuleRules
{
	public AnimEventClock(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
	}
}
```

| Line | What it does, and why |
|---|---|
| `class AnimEventClock : ModuleRules` | The class name **must equal the module name**; UBT maps `<Module>.Build.cs` to the module |
| `ReadOnlyTargetRules Target` | A module may read target settings (platform, configuration) but not change them, so module rules can't fight each other |
| `PCHUsage = UseExplicitOrSharedPCHs` | Use a shared precompiled header provided by the engine for a common dependency set, unless the module names its own. Faster compiles for small modules (`06` §2) |
| `PublicDependencyModuleNames … Core, CoreUObject, Engine` | The three modules every gameplay module needs: containers/strings/logging, UObject/reflection/GC, actors/components/worlds. **Public**, because the game's public headers (from S2: the demo character, the director) will expose engine types |
| *Not yet:* `AnimEventClockCore`, `GameplayAbilities`, `AnimationBudgetAllocator`, … | Each is added in the step whose code first uses it (02 §3). An unused dependency costs build time and hides the real coupling |

### A4. `Source\AnimEventClock\Private\AnimEventClockModule.cpp`

```cpp
#include "Modules/ModuleManager.h"
IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, AnimEventClock, "AnimEventClock");
```

| Line | What it does, and why |
|---|---|
| `#include "Modules/ModuleManager.h"` | Declares `IMPLEMENT_PRIMARY_GAME_MODULE` and `FDefaultGameModuleImpl`. The specific header, not a catch-all one (`04` §3) |
| `IMPLEMENT_PRIMARY_GAME_MODULE(…)` | Generates the module's entry point (`Engine/Source/Runtime/Core/Public/Modules/ModuleManager.h:1094–1136`). In a monolithic (packaged) build it also defines the project-name global that the packaged game starts from. **Primary** = the project's main game module; a project needs exactly one. *Without it:* the module never registers and the packaged game has no project name |
| `FDefaultGameModuleImpl` | The engine's module class with empty `StartupModule`/`ShutdownModule`. We have no start-up logic, so our own class would be a class with no reason |
| `AnimEventClock` | The module name; must match the `.uproject` and `Build.cs` |
| `"AnimEventClock"` | The old "game name" argument. In 5.8 it is named `DEPRECATED_GameName` and Epic's comment says specifying it "is no longer necessary" (`ModuleManager.h:1094–1100`). The macro still takes three arguments, so we pass the project name |

**Why there is no `AnimEventClock.h`:** an empty module header that nothing includes adds nothing. The `Public\` folder appears in S2 with the first real header.

### A5. `Source\AnimEventClockEditor\AnimEventClockEditor.Build.cs` and `Private\AnimEventClockEditorModule.cpp`

- **`Build.cs`:** same pattern as A3, but `Core`, `CoreUObject`, `Engine` are **private** dependencies: no other module will ever include this module's headers (it is a leaf: a commandlet). `UnrealEd` and `AssetTools` are added in S1 with the commandlet.
- **`IMPLEMENT_MODULE(FDefaultModuleImpl, AnimEventClockEditor);`** is the ordinary (non-primary) entry point (`ModuleManager.h:946–955`). A project has one primary module; every other module uses this macro.

### A6. `Plugins\AnimEventClockCore\AnimEventClockCore.uplugin`: the plugin descriptor

| Field | Value | Why |
|---|---|---|
| `FileVersion`, `Version`, `VersionName` | 3, 1, "1.0" | The 5.8 schema, as Epic's own runtime plugins use |
| `FriendlyName`, `Description`, `Category` | "Anim Event Clock Core", one sentence, "Animation" | Shown in the Plugins window. The description says what it does, not how |
| `EnabledByDefault` | `true` | A project that copies the plugin gets it without editing its `.uproject` |
| `CanContainContent` | `false` | Code only; avoids an empty content mount point. The demo montages belong to the game (`/Game/AEC/`), not the plugin |
| `IsBetaVersion`, `IsExperimentalVersion` | `false`, `false` | No warning badge. That says "stable code", not "production-ready" |
| `Installed` | `false` | Part of the project, not installed into the engine |
| `Modules[0]` | `AnimEventClockCore`, `Runtime`, `Default` | The reusable runtime system |
| `Modules[1]` | `AnimEventClockCoreEditor`, `Editor`, `Default` | The reusable editor tooling (the validator, S8). A separate module because a runtime module must not depend on editor-only modules (02 §3) |

### A7. `AnimEventClockCore.Build.cs` and `Private\AnimEventClockCoreModule.cpp`

- **`Build.cs`:** `Core`, `CoreUObject`, `Engine` as **public** dependencies: the plugin's public headers (S1 onwards: notify classes, the component) expose engine types to whoever includes them. `NetCore` is added in S7 only if the replication code uses the push model.
- **`IMPLEMENT_MODULE(FDefaultModuleImpl, AnimEventClockCore);`**: an ordinary module entry point. In S6 it is replaced by a small `FAnimEventClockCoreModule` class that registers the once-per-frame CSV stats (02 §4.1); until then the default implementation is enough.

### A8. `AnimEventClockCoreEditor.Build.cs` and `Private\AnimEventClockCoreEditorModule.cpp`

Same as A5: private `Core`, `CoreUObject`, `Engine`; `IMPLEMENT_MODULE(FDefaultModuleImpl, AnimEventClockCoreEditor);`. `AnimEventClockCore` and `DataValidation` are added in S8 with the validator.

### A9. `Config\DefaultEngine.ini`

| Section / key | Why |
|---|---|
| `GameMapsSettings: GameDefaultMap / EditorStartupMap = /Engine/Maps/Templates/Template_Default` | A shipped engine map with a floor and lights; no map has to be authored, so the project stays buildable from text files. Characters are spawned by code (S2) |
| `[/Script/Engine.Engine] bSmoothFrameRate=False, bUseFixedFrameRate=False` | Already the engine defaults; **restated** so nobody can turn on smoothing or a frame cap without it showing in a diff. Both would change the frame deltas the timing runs depend on |
| `[SystemSettings] r.VSync=0` | Already the default; restated for the same reason |
| `RendererSettings`, `WindowsTargetSettings` | As Epic's 5.8 template: DX12 with SM6, virtual shadow maps, no static lighting. Standard modern defaults, not benchmark choices |
| *(no `AndroidFileServerEditor` block)* | During the first Phase 4 job the engine's `AndroidFileServerEditor` plugin appended a settings block with a random `SecurityToken`. It was **deleted**, and the plugin is disabled in the `.uproject` (A1) so it is not written again; check F-11 confirms the file stays clean after a full editor build, cook and editor `-server` run |

### A10. `Config\DefaultGame.ini`

| Section / key | Why |
|---|---|
| `GeneralProjectSettings: ProjectName, Description` | Shown in window titles and logs |
| `[/Script/UnrealEd.ProjectPackagingSettings] +DirectoriesToAlwaysCook=(Path="/Engine/Tutorial/SubEditors/TutorialAssets/Character")` | The demo character uses the engine's tutorial mannequin, referenced **by path** and never copied into this repository (EULA, `01b #13`). Nothing in a map references it, so the cooker would leave it out; this line cooks the folder ("Directories containing .uasset files that should always be cooked regardless of whether they're referenced", `Engine/Source/Developer/DeveloperToolSettings/Classes/Settings/ProjectPackagingSettings.h:564–568`). The section name is `UnrealEd.` because the class overrides its config section to that name (`:190`; `Engine/Config/BaseGame.ini:87`). Phase 4 check F-5 confirms the folder cooks |

### A11. `Config\DefaultInput.ini`

| Key | Why |
|---|---|
| `DefaultPlayerInputClass=/Script/EnhancedInput.EnhancedPlayerInput`, `DefaultInputComponentClass=/Script/EnhancedInput.EnhancedInputComponent` | The two keys Epic's 5.8 `TP_Blank` template sets (`Templates/TP_Blank/Config/DefaultInput.ini:79–80`; declared in `Engine/Source/Runtime/Engine/Classes/GameFramework/InputSettings.h:219, 223`). Without them the engine creates the classic `UPlayerInput`, and the default-enabled Enhanced Input plugin logs a warning at start-up ("does not have a valid PlayerInput object"), seen in UE5-SpawnPacer's Phase 4 smoke run. Fixing it before the first run keeps the smoke log at 0 warnings. The project reads no input |

### A12. `Tools\build.bat`, `package.bat`, `smoke_game.bat`

All three are adapted from UE5-UpdateBudget, start with `setlocal` and `%~dp0`-relative paths (`04` §7), and must be run **through the queue** (`tools\ue_queue.py`), never directly.

| Script | Key lines | Why |
|---|---|---|
| `build.bat [Target] [Config]` | `call "%UE_ROOT%\Engine\Build\BatchFiles\Build.bat" %TARGET% Win64 %CONFIG% -Project="%PROJECT%" -WaitMutex -NoHotReloadFromIDE` | A reproducible command-line build. Defaults: `AnimEventClockEditor Development`. `-WaitMutex` waits instead of failing if another UBT instance holds the build mutex. The header comment lists the valid configurations, and why Test is not one |
| `package.bat [Config]` | `call "%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project=… -noP4 -utf8output -unattended -platform=Win64 -clientconfig=%CONFIG% -build -cook -stage -pak -archive -archivedirectory="%ARCHIVE%"` | Build, cook, stage, pak and archive a standalone game into `..\Build\` (git-ignored). Default **Development**: every measurement uses it (`01` §7). Shipping has no CSV profiler and no net emulation (`DO_ENABLE_NET_TEST !(UE_BUILD_SHIPPING)`, `01b` §4) |
| `smoke_game.bat [packaged\|editor\|server] [extra…]` | `packaged`: `"%PACKAGED%" -nullrhi -nosound -unattended -nosplash -traceautostart=0 -ExecCmds="quit" -log -abslog=…`; `editor`: `UnrealEditor-Cmd.exe <uproject> -game -nullrhi …`; `server`: `UnrealEditor-Cmd.exe <uproject> -server …` | One script, three ways to start the project and quit after the map loads. `-nullrhi` = no GPU needed; `-ExecCmds="quit"` queues `quit` as a deferred command run after start-up (`Private/UnrealEngine.cpp:2552`); `-traceautostart=0` stops an open Unreal Insights from tracing the run; `-abslog` writes the log to `AnimEventClock\Saved\Logs\Smoke_<mode>.log`. **`server` is new compared with UE5-UpdateBudget**: it is how this project gets a dedicated server from a launcher engine (`01b #14`). The `:collect` loop (`shift` until no arguments are left) appends any extra arguments. Unknown mode → exit 2; packaged build missing → exit 3 (both tested without starting Unreal) |

*Not copied:* UE5-UpdateBudget's `-ExitAfterSeconds=0`. A search of the 5.8.4 runtime source finds no such parameter, so it would be a no-op flag dressed up as a setting.

### A13. `Tools\cpu_affinity.py`

Copied (not imported, each repository stands alone) from UE5-UpdateBudget's `Tools\run_bench.py`, for the later batch runners (`run_bench.py`, `run_net.py`).

| Part | What it does, and why |
|---|---|
| `KERNEL32 = ctypes.WinDLL("kernel32", use_last_error=True)` + `restype`/`argtypes` | Loads the Windows API once, on Windows only (`KERNEL32` stays `None` elsewhere). Declaring `GetCurrentProcess` as returning `c_void_p` and the mask arguments as `c_size_t` keeps 64-bit handles and masks intact (`04` §8) |
| `performance_core_mask()` | Calls `GetLogicalProcessorInformationEx(RelationProcessorCore)` twice (first to get the buffer size), walks the variable-length records with `struct.unpack_from`, ORs the core masks together per **efficiency class**, and returns the mask of the highest class (the P-cores). `None` on a non-hybrid CPU. On this i7-14650HX: `0xFFFF` (check F-8) |
| `pin_self(mask)` | `SetProcessAffinityMask(GetCurrentProcess(), mask)` on the **runner itself**. Every process it starts afterwards inherits the mask from its first instruction, so the game is pinned for its whole life, engine start-up included. Simpler than pinning each child after `Popen`, which leaves a short window in which the child runs unpinned |
| `applied_mask()` | Reads the mask back from Windows (`GetProcessAffinityMask`), so the environment record states what was **applied**, not what was requested |
| Why pin at all | This CPU is hybrid; UE5-UpdateBudget saw runs about 1.6× slower when Windows placed the game thread on an E-core. Timed runs must all get the same cores |
| Why `Popen`, not `cmd /c start /affinity` | `start /wait` always returns exit code 0, so a crashed run would look like a success (found by UE5-TestTrust). The runners launch the real executable with `Popen`, wait with a timeout and kill the tree in a `finally` block |
| `__main__` | Prints the performance-core mask and this process's current mask |

**Phase 4 results** are recorded in [09_Testing_And_Benchmarks.md](09_Testing_And_Benchmarks.md) (Phase 4 · Foundation checks).
