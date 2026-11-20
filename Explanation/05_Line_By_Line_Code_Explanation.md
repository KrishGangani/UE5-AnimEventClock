# 05 · Line By Line Code Explanation: AnimEventClock

**Rule:** every file that exists in `AnimEventClock\` (and every tool in `Tools\`) is explained here, line by line, in the same step it is written. Sections follow the build order in [03_Implementation_Roadmap.md](03_Implementation_Roadmap.md).

| Part | Step | Files |
|---|---|---|
| A | Phase 4, F1–F4 (foundation) | `.uproject`, two `Target.cs`, four `Build.cs`, four module `.cpp`, `.uplugin`, three config `.ini`, `Tools\*.bat`, `Tools\cpu_affinity.py` |
| B | S1 (markers + demo assets) | `AECEventTypes`, `AECEventNotify`, `AECEventWindowNotifyState` (plugin); `AECDemoMontages` (game); `AECMakeDemoAssetsCommandlet` (game editor); `AECDemoMontageTests`; `Tools\make_assets.bat`, `run_tests.bat` |
| C | S2 (workload + harness, baseline paths only) | `AECDemoCharacter`, `AECBenchSubsystem` (game); `Tools\event_stats.py`, `run_bench.py` |

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

- **`Build.cs`:** same pattern as A3, but `Core`, `CoreUObject`, `Engine` are **private** dependencies: no other module will ever include this module's headers (it is a leaf: a commandlet). S1 added the commandlet's dependencies (B5).
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

---

## Part B · Step S1: markers and demo assets

### B1. `Plugins\AnimEventClockCore\Source\AnimEventClockCore\Public\AECEventTypes.h` / `Private\AECEventTypes.cpp`

```cpp
UENUM(BlueprintType)
enum class EAECEventPhase : uint8 { Instant, WindowBegin, WindowEnd };

DECLARE_MULTICAST_DELEGATE_FourParams(FAECMarkerFiredByAnimation,
	const USkeletalMeshComponent*, const UAnimSequenceBase*, FName /*EventName*/, EAECEventPhase);

namespace AnimEventClock { UE_API FAECMarkerFiredByAnimation& OnMarkerFiredByAnimation(); }
```

| Line | What it does, and why |
|---|---|
| `#include "Delegates/Delegate.h"`, `"UObject/NameTypes.h"`, `"UObject/ObjectMacros.h"` | Exactly the three things this header uses: the delegate macros, `FName`, and `UENUM`. Not `CoreMinimal.h`: including only what you use keeps every file that includes this header cheap to compile |
| `#include "AECEventTypes.generated.h"` | The header UnrealHeaderTool writes for the `UENUM`. It must be the **last** include |
| `#define UE_API ANIMEVENTCLOCKCORE_API` … `#undef UE_API` | 5.8's export convention: `ANIMEVENTCLOCKCORE_API` expands to `dllexport` while building this module and `dllimport` for modules that use it, so the function can be called from the game module in editor (DLL) builds. The `#undef` keeps the short name from leaking into other headers |
| `class UAnimSequenceBase; class USkeletalMeshComponent;` | Forward declarations: the delegate only passes pointers, so the full class definitions aren't needed here |
| `UENUM(BlueprintType) enum class EAECEventPhase : uint8` | What happened at an authored event. `enum class` (scoped, no silent int conversion, `04` §9), one byte, reflected so Blueprint and `FAECEventInfo` (S6) can carry it. *Alternative:* two booleans (`bIsWindow`, `bIsEnd`), which allow a meaningless fourth state |
| `DECLARE_MULTICAST_DELEGATE_FourParams(…)` | A **native** multicast delegate type: many C++ listeners, no reflection, cheap when unbound (`06` §11). The comparison hook is for the harness and debug views only, so a Blueprint-visible dynamic delegate would add cost and imply that gameplay should use it |
| `namespace AnimEventClock { … OnMarkerFiredByAnimation(); }` | One hook for the whole process, reached through a function. A namespace instead of a static member, because both marker classes share it |
| `.cpp`: `static FAECMarkerFiredByAnimation Delegate; return Delegate;` | A function-local static (`04` §10): constructed on first use, so it exists before any notify can fire, in every module, with no initialisation-order problem between DLLs |
| `#include UE_INLINE_GENERATED_CPP_BY_NAME(AECEventTypes)` | Compiles the UHT-generated code for the enum inside this `.cpp` |

### B2. `Public\AECEventNotify.h` / `Private\AECEventNotify.cpp`: the instant-event marker

| Line | What it does, and why |
|---|---|
| `UCLASS(MinimalAPI, meta = (DisplayName = "AEC Event"))` | A reflected class. `MinimalAPI` exports only the class's type information (enough for `Cast`, `StaticClass`, `NewObject` from other modules); members other modules call are marked `UE_API` one by one. `DisplayName` is the name in the montage editor's "Add Notify" menu |
| `class UAECEventNotify : public UAnimNotify` | Derives from the engine's notify base, so designers place it like any notify, on any notify track, and it is saved inside the montage as an instanced subobject (`FAnimNotifyEvent::Notify`, `Public/Animation/AnimTypes.h:279–355`) |
| `UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AnimEventClock") FName EventName;` | The gameplay name. `EditAnywhere`: editable in the notify's details panel. `BlueprintReadOnly`: Blueprint may read, never write, the authored name. `FName`, not `FString`: compared by index, cheap in the hot path. *Alternative:* gameplay tags (`02` §11) |
| `GetNotifyName_Implementation() const override` | `GetNotifyName` is a `BlueprintNativeEvent` on `UAnimNotify` (`Classes/Animation/AnimNotifies/AnimNotify.h:58–59`); C++ overrides its `_Implementation`. Returns `"AEC Hit1"`, so the track shows the gameplay name |
| `Notify(MeshComp, Animation, EventReference) override` | Called by the engine when the **animation** reaches the marker: from the notify queue (Queued), or from `BranchingPointNotify`, whose default implementation calls `Notify` with an empty reference (`Private/Animation/AnimNotify.cpp:37–41`). So one override covers both tick types |
| `Super::Notify(…)` | Keeps the base behaviour: it sets the mesh context and calls the Blueprint event (`AnimNotify.cpp:29–35`) |
| `…Broadcast(MeshComp, Animation, EventName, EAECEventPhase::Instant)` | Reports to the comparison hook. Nothing gameplay-related happens here: that is the point of the design (`02` §2) |

### B3. `Public\AECEventWindowNotifyState.h` / `Private\AECEventWindowNotifyState.cpp`: the window marker

The same pattern on `UAnimNotifyState`: `NotifyBegin` (`AnimNotifyState.h:74`) broadcasts `WindowBegin` and `NotifyEnd` (`:76`) broadcasts `WindowEnd`, each after `Super`. BranchingPoint states reach them through `BranchingPointNotifyBegin`/`End` (`Private/Animation/AnimNotifyState.cpp:44–65`). `NotifyTick` is not overridden: a window has no meaning between its edges, and per-tick calls would be a per-frame cost the clock doesn't need.

### B4. `Source\AnimEventClock\Public\AECDemoMontages.h` / `Private\AECDemoMontages.cpp`: the authored content

| Part | What it does, and why |
|---|---|
| `inline const TCHAR* const SourceSequencePath = TEXT("/Engine/Tutorial/…/Tutorial_Idle.Tutorial_Idle")` | An `inline` variable (C++17, `04` §11): one definition across every `.cpp` that includes the header. The `Package.Object` form is what `LoadObject` expects. The sequence is **6.2667 s** long (commandlet log, 09 S1-b) |
| `FMarkerSpec { Name, StartFraction, DurationFraction }` | A marker: instant when the duration is 0. **Fractions of the montage length**, so the data fits whatever length Epic's sequence has. Positions avoid section edges and the very start and end (a notify on a segment edge gets a ±0.1 ms trigger offset, `AnimTypes.cpp:12–37`) |
| `FSectionSpec`, `FMontageSpec` | Sections in play order; each montage exists as `_Q` and `_B` |
| `GetMontageSpecs()` | A function-local static array (built once), the **single source of truth**: the commandlet builds from it, the test checks against it, the director loads by its paths. `AM_Attack`: one section; `Wind` 0.12, window `HitWindow` 0.30–0.62, `Hit1` 0.36, `Hit2` 0.56, `Recover` 0.88. `AM_Combo`: sections A 0, B ⅓, C ⅔; `A1` 0.10, window `ComboWindow` 0.25–0.42 (it crosses the A→B boundary on purpose), `B1` 0.45, `C1` 0.78, `End` 0.94 |
| `ANIMEVENTCLOCK_API` on the functions | Exported from the game module, so the editor module can call them in editor (DLL) builds |
| `GetMontageAssetName / PackageName / ObjectPath` | The three name forms Unreal uses: asset `AM_Attack_Q`, package `/Game/AEC/Montages/AM_Attack_Q`, object `…/AM_Attack_Q.AM_Attack_Q` |

### B5. `Source\AnimEventClockEditor\Private\AECMakeDemoAssetsCommandlet.h` / `.cpp`

| Part | What it does, and why |
|---|---|
| `UCLASS() class UAECMakeDemoAssetsCommandlet : public UCommandlet` | A commandlet: a headless editor program run with `-run=AECMakeDemoAssets` (the engine strips the `Commandlet` suffix). It lives in the **editor-only game module**, so no game build contains it |
| `AECMakeDemoAssets::MakeMontage` (a free function in the `.cpp`) | It uses no commandlet state, so it is not a member: the header keeps only `Main` and needs neither the spec header nor a forward declaration (simplify finding) |
| `virtual int32 Main(const FString& Params) override` | The commandlet's entry point; its return value is the process exit code (0 = every asset saved) |
| `LoadObject<UAnimSequence>(nullptr, AECDemo::SourceSequencePath)` | Loads the engine's sequence by path. Nothing is copied: the saved montages only **reference** it |
| `UAnimMontageFactory` + `SourceAnimation`, `FactoryCreateNew` called through `UFactory*` | The editor's own "Create Montage" path (`Editor/UnrealEd/Private/Factories/AnimMontageFactory.cpp`): one slot track with the sequence, the montage length set from it, a first section "Default" at 0. The factory class is `MinimalAPI` and its override isn't exported, so it is called through the base pointer: an ordinary virtual call, resolved at run time |
| `CreatePackage(*PackageName)` | The in-memory package the asset is created in and saved from |
| `CompositeSections[0].SectionName = …`; `AddAnimCompositeSection(Name, Fraction × Length)` | Sections. `AddAnimCompositeSection` links the previous section to the new one when the previous has no link yet, so A→B→C is linked in array order; the test checks the chain |
| `UAnimationBlueprintLibrary::AddAnimationNotifyTrack / AddAnimationNotifyEvent / AddAnimationNotifyStateEvent` | The editor's scripting API: it links each event to the montage, sets the trigger offset the editor would (`CalculateOffsetForNotify`), creates the notify object inside the montage and refreshes cached data (`Editor/AnimationBlueprintLibrary/Private/AnimationBlueprintLibrary.cpp:651–690`). Our assets are authored exactly as a designer's tool would author them |
| `Event->EventName = FName(Marker.Name)` | The returned notify object is ours to configure |
| `Event.MontageTickType = …; Event.NotifyName = …GetNotifyName()` | The tick type belongs to each **notify event**, not to the class. The display name was taken before `EventName` was set, so it is refreshed |
| `Montage->RefreshCacheData()` | Rebuilds the branching-point markers from the new tick types (`Private/Animation/AnimMontage.cpp:706–711`). Without it, `_B` montages would fire nothing as branching points |
| `Notifies.Num() != Spec.Markers.Num()` → error | A cheap self-check before saving |
| `FSavePackageArgs` (`TopLevelFlags = RF_Public \| RF_Standalone`, `Error = GWarn`) + `UPackage::SavePackage` | Saves to `Content/AEC/Montages/<Name>.uasset`. `RF_Standalone` keeps the asset alive without references; `RF_Public` lets other packages reference it |
| The summary log line | Every marker's name and trigger time, so a run of the commandlet can be reviewed from its log alone |

**The generated assets** (`AnimEventClock\Content\AEC\Montages\*.uasset`, 13–15 KB each) are committed: they are this project's own content, hold only references to the engine sequence, and `Tools\make_assets.bat` regenerates them at any time.

### B6. `Source\AnimEventClock\Private\Tests\AECDemoMontageTests.cpp`

| Part | What it does, and why |
|---|---|
| `#if WITH_DEV_AUTOMATION_TESTS` | Compiled only where automation tests exist (not Shipping) |
| `IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAECDemoMontagesMatchSpecTest, "AnimEventClock.Assets.DemoMontagesMatchSpec", Flags)` | Registers a test under that name (`06` §15). `EditorContext \| ProductFilter`: runs in the editor, in the project's own test group |
| Per montage: loads, has a length, section names and starts (1 ms tolerance), the `NextSectionName` chain | Checks the asset against `AECDemoMontages.h`, not against itself |
| Per marker: found by `EventName`, a window if and only if the spec says so, start (and end) within 1 ms, tick type, `IsBranchingPoint()` | `IsBranchingPoint` also proves the montage link is valid (it needs `GetLinkedMontage`, `AnimTypes.cpp:107–110`). Tick types are compared as `int32` because the test framework prints only basic types |

### B7. Build and project files for S1

- `AnimEventClockEditor.Build.cs` (private): `UnrealEd` (commandlet hosting, the montage factory), `AnimationBlueprintLibrary` (the notify API), `AnimEventClockCore` (the marker classes), `AnimEventClock` (the spec).
- `AnimEventClock.Build.cs`: `AnimEventClockCore` (made public in S2, C3).
- `DefaultGame.ini`: `+DirectoriesToAlwaysCook=(Path="/Game/AEC/Montages")`, because the director loads the montages by path and no map references them.
- `Tools\make_assets.bat`: `UnrealEditor-Cmd.exe <uproject> -run=AECMakeDemoAssets -unattended -nullrhi -nosound -nosplash -traceautostart=0 -log -abslog=…\MakeAssets.log`. Making assets needs no GPU.
- `Tools\run_tests.bat`: as UE5-UpdateBudget's: `-ExecCmds="Automation RunTests <Filter>;Quit"`, default filter `AnimEventClock`, exit code = the test result.

---

## Part C · Step S2: the workload and the harness (baseline paths only)

No event-clock code exists in this step: S2 builds only what the baselines (NQ, NB, T) need, so they can be captured before the system exists (S3).

### C1. `Source\AnimEventClock\Public\AECDemoCharacter.h` / `Private\AECDemoCharacter.cpp`

| Line | What it does, and why |
|---|---|
| `UCLASS() class AAECDemoCharacter : public ACharacter` | A plain character: capsule, movement component, skeletal mesh. Not exported (no `_API`): only this module uses it |
| `AAECDemoCharacter(const FObjectInitializer&) : Super(ObjectInitializer.SetDefaultSubobjectClass<USkeletalMeshComponentBudgeted>(ACharacter::MeshComponentName))` | `ACharacter` creates its mesh under `MeshComponentName`; this makes that subobject a `USkeletalMeshComponentBudgeted` (`06` §17), so the budget conditions need no second character class |
| `PrimaryActorTick.bCanEverTick = true; TickGroup = TG_PrePhysics` | The actor ticks (that is where "pre" starts run). `TG_PrePhysics` is `ACharacter`'s default, stated because the start order depends on it |
| `AutoPossessAI = EAutoPossessAI::Disabled` | No controller at all: an NPC. URO only exempts pawns whose instigator controller is a player controller (`SkinnedMeshComponent.cpp:205–210`) |
| `static ConstructorHelpers::FObjectFinder<USkeletalMesh> MannequinMesh(…)` | Loads the tutorial mannequin once per class (`static`, `04` §10). Cooked by `DefaultGame.ini` |
| `SetAnimInstanceClass(UAnimInstance::StaticClass())` | A **plain** `UAnimInstance` with no anim graph. Montages still advance in `UpdateAnimation` and fire notifies (`01b #1`); the pose isn't needed to measure timing. *Changed from 02's plan:* no `UAECDemoAnimInstance` class, because it would have been empty (02 §14) |
| `SetRelativeLocationAndRotation(MeshOffset, MeshRotation)` | Puts the mannequin's feet at the capsule's base and turns it to face +X. Cosmetic |
| `SetCastShadow(false)` | So "off screen" means "not rendered". A mesh behind the camera that casts a visible shadow could count as rendered, because "recently rendered" uses the last render time of any pass (`SkinnedMeshComponent.cpp:1864`). Same for every condition |
| `CastChecked<USkeletalMeshComponentBudgeted>(…)->SetAutoRegisterWithBudgetAllocator(false)` | The budget allocator manages a mesh only if the director turns this back on (budget conditions). Otherwise a registered mesh could be affected by the allocator in conditions that are not about it |
| `BeginPlay`: `GetMesh()->AddTickPrerequisiteActor(this)` | Every frame, this actor's tick runs before its mesh's tick (`06` §18): the engine helper adds the actor's tick as the mesh tick's prerequisite, with safety checks. Done on the instance in `BeginPlay`, not in the constructor. The director enables the actor's tick only in "pre" mode |
| `BeginPlay`: `GetCharacterMovement()->DisableMovement()` | The characters never move. Without it, the look-ahead condition (root motion from everything) would make the movement component simulate them, and characters off the map's floor would fall (code-review finding, 09 S2) |
| `Tick`: `if (bStartRequested) { …; Bench->ExecuteRequestedStart(*this); }` | The "pre" start order: a start the director requested runs here, before the mesh's animation update of the same frame |
| `GetBenchIndex` / `SetBenchIndex` | Which state in the director this character is. `INDEX_NONE` for a character the director didn't spawn |
| `RequestStartInNextTick` / `bStartRequested` | The one-frame request. Kept on the character (not looked up in the director every tick) so a tick with nothing to do costs one branch |

### C2. `Source\AnimEventClock\Public\AECBenchSubsystem.h` / `Private\AECBenchSubsystem.cpp`: the director

**Header.** A `UTickableWorldSubsystem` (`06` §19): no map to place a director actor in, exactly the world's lifetime, and its `Tick` runs after `TG_PostPhysics`, which is the "post" start order. The class comment lists the timeline and every `-AEC…` switch. Private plain structs hold the data:

| Type | Fields, and why |
|---|---|
| `FRunSettings` | The command line, read once. `bStartPre` defaults to **false**: actions start from this Tick (after the meshes ticked), so the montage first advances on the next frame and a notify's error starts at 0 (see C2 "Start order") |
| `FKey { Name, Phase, Position, PairKey }` | One authored event, read from the montage's own notifies (a window gives a begin and an end key). `PairKey` links a window's begin and end, filled once when the montage loads, so nothing searches by name later (simplify finding) |
| `FMontageInfo { Name, Length, Keys }` | A loaded montage's keys, sorted by position |
| `FAction` | One played action: character, montage, play rate, start (game time, wall clock, frame), stop (< 0 = not interrupted), `bInCapture` |
| `FFire` | One reported event: action, key, path (animation or timer), game time, wall clock, frame |
| `FCharacterState` | One character's schedule and the timer path's state: band, next start, current action (kept after it ends, so late animation fires are still attributed), planned end/stop, the planned montage/rate/interruption, its timers, which timer-path windows are open, and its own `FRandomStream` (in the state, not a parallel array: simplify finding) |
| `UPROPERTY(Transient) TArray<TObjectPtr<…>> Montages / Characters; TObjectPtr<ACameraActor> Camera` | The UObjects the director holds: `UPROPERTY` so garbage collection sees the references and never frees a montage or character in use |

**`OnWorldBeginPlay`.** Reads the settings; with no characters requested, does nothing (an empty map). Records the begin-play time and **frame** (frame numbers in the files count from here, so loading frames can't change them). Then, in order: the config must be `NQ` or `NB`; `PrepareCondition` sets the condition's console variables **before any character exists** (the budget allocator asks for this); `LoadMontages`; timings must be sane; `SpawnCharacters`. If any step fails, the run doesn't start and the exit code is 3. Then the comparison hook is bound, the camera spawned, and the run enters warm-up.

**`PrepareCondition`.** `def` nothing; `uro` URO on with the engine's own rates; `uro2`/`uro4` URO on plus `a.URO.ForceAnimRate`; `la4` the same at rate 4 plus root motion from everything (look-ahead, `SkinnedMeshComponent.cpp:360`); `aba2`/`aba4` `a.Budget.Enabled 1`, `a.Budget.Debug.Force 1`, `a.Budget.Debug.Force.Rate` (deterministic, Development builds); `vis` `OnlyTickPoseWhenRendered`. Unknown → false. `SetConsoleVariable` sets each value at the temporary priority `ECVF_SetByTemp` with a tag, and `Deinitialize` calls `UnsetAllConsoleVariablesWithTag`, which removes exactly those values, because PIE runs share one process (code-review finding). Restoring the old value by hand would have left it stuck at code priority (simplify finding)

**`LoadMontages`.** Loads the two montages of the run's variant by path and reads their keys from `Notifies` with `GetTriggerTime`/`GetEndTriggerTime` (`06` §12), exactly as the event clock will. A montage without markers is a setup error.

**`SpawnCharacters`.** Per character, two seeded streams: one for placement, one for the schedule, each seeded from (seed, index), so changing one character never moves another. Band = index mod 3; position inside the band's box (near 4–9 m, far 40–70 m, behind −12 to −4 m); facing the camera. Deferred spawn (`06` §20): URO, visibility and budget-registration flags go on before `BeginPlay`; root motion from everything after `FinishSpawning` (the anim instance exists only then). Each character's actor tick is enabled only in "pre" mode, where it runs the requested starts (efficiency finding). A failed spawn aborts the run, so the state and character arrays always share one index (code-review finding). At the end, `Actions` and `Fires` are **reserved** for the longest possible run (the shortest montage at the fastest rate plus the shortest wait, two fires per key), so they never reallocate and copy megabytes inside a measured frame (efficiency finding).

**`PlanNextAction`.** Always five draws in the same order: the wait (0.5–1.5 s), the montage (70% Attack), the play rate (1.0 or 1.3), whether to interrupt (10%) and where (20–80% of the duration). Same number of draws every time, so a stream advances identically in every run.

**`StartAction`.** Records the action (game time, wall clock, run frame, in-capture flag), `Montage_Play`s it (the animation path), then clears any old timers (`ClearTimers`) and sets one timer per key at `position / play rate` (the timer path, `06` §22), and the planned end and stop times. `FMath::Max(Delay, UE_KINDA_SMALL_NUMBER)` because a zero rate would set nothing.

**`StopAction`.** Records the stop, `Montage_Stop` with a 0.1 s blend-out (the montage's notifies stop at once, `AnimMontage.cpp:2822–2826`), clears the timers, and closes any timer-path window that was open, recording its end key (`PairKey`) now: what a hand-written implementation would do.

**`HandleMarkerFiredByAnimation`.** The animation path. Finds the character from the mesh's owner (ignores other worlds), takes its current action, and accepts the fire only if the `Animation` is that action's montage and a key with that name and phase exists. Anything else is counted as *unattributed* (reported in the files and the log, never silently dropped).

**`HandleTimerKey`.** The timer path. Ignores a timer of an action that is no longer current (a cheap guard: stopping clears timers, so it should never trigger); tracks open windows through `PairKey`; records the fire.

**`Tick`.** Done: count frames and quit with the exit code after 3 frames (`-AECExit`). Otherwise: keep the view on the camera; move from warm-up to capture (`BenchStart`) and to draining (`BenchEnd`); per character, stop or end a playing action at its planned time and plan the next, or start the next when due (here for "post", via the character's next tick for "pre"). When draining and nothing is playing or waiting: 1 s of grace for late events, then write the files.

**`WriteRunFiles`.** The start-order and fixed-step texts come from two small helpers shared with the start-up log line. Three CSV files with one `# key=value` header line (run tag, config, condition, start order, characters, seed, timings, capture window, the fixed step, unattributed fires, the fire hash):
- `_actions.csv`: one line per action;
- `_expected.csv`: every key the action reached, due at start + position / play rate. An interrupted action expects nothing after its stop except the end of a window that was open, due at the stop and flagged `interrupted=1`;
- `_events.csv`: every fire, with its path (`NQ`/`NB` for the animation, `T` for timers).
A failed write gives exit code 4 (code-review finding).

**`HashFires`.** CRC32 over (action, key, path, run frame) of every fire, **sorted** first: inside one frame the order in which meshes dispatch their queued notifies depends on which parallel evaluation task finishes first, so two identical runs recorded the same fires in a different order (S2 check, 09 S2-c). Computed once per write. Two runs with the same seed and settings must give the same hash.

**Start order.** With "post" (default), a montage started after the meshes ticked first advances in the next frame, so a notify fires 0 to 1 frame after "start + position / rate". With "pre", the montage advances a whole frame on its first update, so notifies fire up to one frame *before* it. Both are kept: the difference is hypothesis H8, measured in S3.

### C3. Build file changes for S2

`AnimEventClock.Build.cs`: `AnimEventClockCore` becomes **public** (`AECBenchSubsystem.h`, a public header, uses `EAECEventPhase`); `AnimationBudgetAllocator` is **private** (only `.cpp` files use `USkeletalMeshComponentBudgeted`).

### C4. `Tools\event_stats.py`

| Part | What it does, and why |
|---|---|
| `percentile`, `summarize` | Linear-interpolation percentiles, as UE5-UpdateBudget's `csv_stats.py`, plus min and max: "never early" is a pass/fail property |
| `read_csv` | Reads the `# key=value` header line, then the rows |
| `analyse(prefix)` | Over actions inside the capture window only: per path, the **first** fire of each expected event gives its error (fire − due, ms); further fires are duplicates; no fire is a miss; fires of events that weren't expected are "unexpected". Per band (0 near, 1 far, 2 behind) and overall. Window ends closed by an interruption go to their own `interrupted_end` bucket (the animation closes them only after its blend-out: a different question from timing, code-review finding). `bunched_share` = share of first fires sharing a frame with another key of the same action. With a fixed step, errors are also given in frames |
| `collections.Counter` in the bunching count | Counts fires per frame directly (simplify finding) |
| `selftest()` | A hand-made run with known answers: an on-time and a late fire in one frame (bunched), a duplicate, a miss, an action outside the capture, an interrupted window end |

### C5. `Tools\run_bench.py`

Adapted from UE5-UpdateBudget's runner (`05` A13 for the pinning): pins itself once with `pin_self`, records the environment (with `applied_mask()`), runs a priming run, then every condition × frame rate × config, `--runs` times (default: the number of configs), interleaved with the config order rotated each round. Each run launches the **real** packaged executable with `Popen` (`-deterministic -FPS=<n>`, 1280×720 windowed, no VSync, no sound, `-traceautostart=0`, the `-AEC…` switches, `-AECExit`), waits with a timeout and kills the tree in a `finally` block. It moves the three run files and the log into `Benchmarks\Raw\` (`refuse_overwrite` raises rather than overwrite raw data), analyses them with `event_stats.analyse`, rewrites the manifest after every run, and writes a summary table per (condition, fps, config, path) with the median across runs [min–max], missed and duplicates per 1,000, the bunched share, and whether the fire hashes of repeated runs agree. A run fails on a non-zero exit, a timeout, missing run files, a missing log or log errors.
