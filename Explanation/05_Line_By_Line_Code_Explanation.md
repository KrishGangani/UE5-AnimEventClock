# 05 · Line By Line Code Explanation: AnimEventClock

**Rule:** every file that exists in `AnimEventClock\` (and every tool in `Tools\`) is explained here, line by line, in the same step it is written. Sections follow the build order in [03_Implementation_Roadmap.md](03_Implementation_Roadmap.md).

| Part | Step | Files |
|---|---|---|
| A | Phase 4, F1–F4 (foundation) | `.uproject`, two `Target.cs`, four `Build.cs`, four module `.cpp`, `.uplugin`, three config `.ini`, `Tools\*.bat`, `Tools\cpu_affinity.py` |
| B | S1 (markers + demo assets) | `AECEventTypes`, `AECEventNotify`, `AECEventWindowNotifyState` (plugin); `AECDemoMontages` (game); `AECMakeDemoAssetsCommandlet` (game editor); `AECDemoMontageTests`; `Tools\make_assets.bat`, `run_tests.bat` |
| C | S2 (workload + harness, baseline paths only) | `AECDemoCharacter`, `AECBenchSubsystem` (game); `Tools\event_stats.py`, `run_bench.py` |
| D | S3 (the M1 baselines and the gate) | `AECBenchSubsystem` (`PrepareCondition`: `rme`, the budget allocator's second switch); `Tools\event_stats.py` (one shared metric with event-class buckets), `run_bench.py` (the gate, the no-op warning, `--summarize`) |
| E | S4 (network harness, baselines G and the network roles) | `AECDemoCharacter` (replication, RPCs, the player character), `AECDemoGasCharacter`, `AECDemoAttackAbility`, `AECDemoGameMode`, `AECDemoPlayerController`, `AECBenchSubsystem` (server director, client recorder, frame log), `AECNetSetupTests`, `AnimEventClock.Build.cs`; `Tools\event_stats.py` (network analysis), `run_net.py` |
| F | S5 (the pure timeline) | `AECMontageTimeline` (plugin), `AECMontageTimelineTests` (plugin), `AECTimelineBuildTests` (game); the director's key reader |
| G | S6 (the event clock, authority) | `AECEventClockComponent`, `AECClockTelemetry`, the module (plugin); `AECClockTestListener`, `AECEventClockTests` (game); the AEC path in `AECDemoCharacter` / `AECBenchSubsystem`; `run_bench.py --clock`, `event_stats.py` |

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

**`PrepareCondition`.** `def` nothing; `uro` URO on with the engine's own rates; `uro2`/`uro4` URO on plus `a.URO.ForceAnimRate`; `la4` the same at rate 4 plus root motion from everything (look-ahead, `SkinnedMeshComponent.cpp:360`); `rme` root motion from everything **without** URO (added in S3 as the control that separates `la4`'s two switches); `aba2`/`aba4` turn on **both** budget switches, the cvar `a.Budget.Enabled 1` and this world's allocator, `IAnimationBudgetAllocator::Get(GetWorld())->SetEnabled(...)` (it starts disabled, `06` §26; a missing allocator is a setup error), then `a.Budget.Debug.Force 1` and `a.Budget.Debug.Force.Rate` (deterministic, Development builds). `SetEnabled` comes after the cvars, because enabling reads the cvar. S3 found that the cvars alone change nothing (`07` S3); `vis` `OnlyTickPoseWhenRendered`. Unknown → false. `SetConsoleVariable` sets each value at the temporary priority `ECVF_SetByTemp` with a tag, and `Deinitialize` calls `UnsetAllConsoleVariablesWithTag`, which removes exactly those values, because PIE runs share one process (code-review finding). Restoring the old value by hand would have left it stuck at code priority (simplify finding)

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

### C4. `Tools\event_stats.py` (as written in S2; S3 rewrote it around one shared metric, D2)

| Part | What it does, and why |
|---|---|
| `percentile`, `summarize` | Linear-interpolation percentiles, as UE5-UpdateBudget's `csv_stats.py`, plus min and max: "never early" is a pass/fail property |
| `read_csv` | Reads the `# key=value` header line, then the rows |
| `analyse(prefix)` | Over actions inside the capture window only: per path, the **first** fire of each expected event gives its error (fire − due, ms); further fires are duplicates; no fire is a miss; fires of events that weren't expected are "unexpected". Per band (0 near, 1 far, 2 behind) and overall. Window ends closed by an interruption go to their own `interrupted_end` bucket (the animation closes them only after its blend-out: a different question from timing, code-review finding). `bunched_share` = share of first fires sharing a frame with another key of the same action. With a fixed step, errors are also given in frames |
| `collections.Counter` in the bunching count | Counts fires per frame directly (simplify finding) |
| `selftest()` | A hand-made run with known answers: an on-time and a late fire in one frame (bunched), a duplicate, a miss, an action outside the capture, an interrupted window end |

### C5. `Tools\run_bench.py` (S3 added the gate, the no-op warning and `--summarize`, D3)

Adapted from UE5-UpdateBudget's runner (`05` A13 for the pinning): pins itself once with `pin_self`, records the environment (with `applied_mask()`), runs a priming run, then every condition × frame rate × config, `--runs` times (default: the number of configs), interleaved with the config order rotated each round. Each run launches the **real** packaged executable with `Popen` (`-deterministic -FPS=<n>`, 1280×720 windowed, no VSync, no sound, `-traceautostart=0`, the `-AEC…` switches, `-AECExit`), waits with a timeout and kills the tree in a `finally` block. It moves the three run files and the log into `Benchmarks\Raw\` (`refuse_overwrite` raises rather than overwrite raw data), analyses them with `event_stats.analyse`, rewrites the manifest after every run, and writes a summary table per (condition, fps, config, path) with the median across runs [min–max], missed and duplicates per 1,000, the bunched share, and whether the fire hashes of repeated runs agree. A run fails on a non-zero exit, a timeout, missing run files, a missing log or log errors.

---

## Part D · S3: the M1 baselines and the gate

### D1. `AECBenchSubsystem.cpp`, `PrepareCondition`

Two changes, both described in C3 (`PrepareCondition`): the `rme` control (root motion from everything without URO), and the budget conditions turning on the world's allocator as well as the cvar. The order inside the budget branch: fetch the allocator first (a missing one is a setup error, so the run refuses to start); set the three cvars (any failure → `false`); only then `SetEnabled(true)`, because enabling re-reads `a.Budget.Enabled` (`06` §26). An early `return false` on a failed cvar instead of passing the result into `SetEnabled` (simplify finding): `SetEnabled(false)` on a disabled allocator would do nothing, so the old form only looked as if it handled a case.

### D2. `Tools\event_stats.py`, rewritten around one metric

| Part | What it does, and why |
|---|---|
| `summarize` | Adds `abs_p99`, the p99 of the absolute error: the problem gate's number (`01` §6.4). Early and late errors count alike |
| `load_run(prefix)` | Reads a run's three files once into: the header, the actions in capture (by id), the expected events (due time, phase, interrupted) and the fires per (path, action, key). Every analysis below starts from it |
| `measure(due, interrupted, group_of, phase_of, fires, paths, frame_ms)` | **The** metric, written once: per path, the first fire's error, misses, duplicates (and how many of them are window begins/ends), unexpected fires, bunching, into buckets: `all`, one per group, `instant+begin`, `end`, `interrupted_end`. `group_of(action, key)` names the group, so the same code gives bands (`analyse`), per-key labels (`keys`) and, in S4, network roles. It used to live inside `analyse`; the first gate script re-implemented it and had already drifted (reuse and altitude findings), so the gate is now two buckets of this function |
| `paths` argument | `analyse` passes `{config, "T"}`, so a path that fired nothing in a run still gets a row with every event missed, instead of vanishing from the table (code-review finding: the gate exists to catch exactly that run) |
| `analyse(prefix)` | `load_run` + `measure` with the band as the group. Its output for the old buckets is identical to the S2 version on all 73 runs recorded so far (checked by script before the change was kept) |
| `keys(prefix, path)` / `--keys` | The per-key diagnostic used to read the URO and look-ahead results: per (montage, key, phase, band, play rate), expected, missed and min / median / max error. A bare run name is looked up in `Benchmarks\Raw` |
| `selftest()` | The S2 cases, plus `abs_p99`, the `instant+begin` bucket, a path that never fired (T: every event missed), and one `--keys` line |

### D3. `Tools\run_bench.py`

- `RAW_DIR` comes from `event_stats` (one definition of the folder). `CONDITIONS` gains `rme`.
- `gate_lines(groups)`: the gate table in every batch summary, per (condition, fps, config, path) and event class: the median across runs of each run's `abs_p99_frames` [min–max], min and max in frames, missed and duplicates summed over the runs, and the verdict (**problem** if p99 > 1 frame + 0.001 frame, or anything missed or duplicated; the 0.001 frame covers the CSV files' 1 µs resolution). Runs without a fixed step are skipped (no frame length).
- The **no-op warning**: any condition whose fire hash equals the default condition's at the same frame rate and config is flagged "it changed nothing". This is the check that found the budget mistake by hand; now every batch makes it (altitude finding).
- `--summarize <batch>`: re-reads the batch's manifest and raw CSV files, re-analyses every run with the current `event_stats.py` and rewrites only that batch's summary. The raw files and the manifest are never touched (`Benchmarks\README.md`). The S3 summaries were regenerated this way after the gate was added.

---

## Part E · S4: the network harness

The idea in one paragraph: the same director and the same seeded schedule run on the **server** (listen or dedicated). The server waits for its remote player, gives that player a demo character of its own, and multicasts every action start and stop with the action's id. A **client** started with `-AECClient` does not direct anything: it records the fires of every replicated character against the server's ids, and plays NQ/NB montages and T timers when a start arrives (G montages arrive through GAS). Every process of a network run also logs, once per frame, its game time and the wall-clock moment that game time stands for, so the analysis can put the server's due times and every process's fires on one clock (raw notes E0).

### E1. `AECDemoCharacter.h/.cpp`: replication, the two multicasts, the player character

| Part | What it does, and why |
|---|---|
| `bAlwaysRelevant = true` | Relevancy could destroy a character on the client mid-run, and its fires would vanish with it (`06` §27). No effect standalone |
| `UPROPERTY(Replicated) BenchIndex`, `bPlayerCharacter`, `COND_InitialOnly` | Set by the director before `FinishSpawning`, never changed, so the first bunch carries them, and a client has them by its `BeginPlay` (`06` §27). The client needs the index to keep one state array with the server's numbering |
| `MulticastStartAction(ActionId, MontageIndex, PlayRate)`, `MulticastStopAction(ActionId)` | Reliable multicasts (`06` §28). Their bodies return on the server (a multicast runs there too) and otherwise hand the message to the client's director. The montage index is a `uint8`: two demo montages |
| `BeginPlay`, player character | `DefaultLandMovementMode` **and** the current mode become `MOVE_Flying`: the client keeps sending moves, the server ticks the pose from them (the role being measured), and possession's restart cannot reset the mode (`06` §32). NPCs keep `DisableMovement` |
| `BeginPlay`, on a client | `RegisterReplicatedCharacter` puts the character into the recorder's arrays at its bench index, and the actor tick is switched off (it only runs "pre" starts, which only a server makes; efficiency finding) |
| `PlayActionMontage` / `StopActionMontage` (virtual) | How *this machine* plays and stops an action's montage: `Montage_Play` / `Montage_Stop`. Baseline G's character overrides both, so the character class chosen at spawn is the only place the configuration is decided (altitude finding; before, the director tested "G" in three places) |
| `ClientBench` | One helper for "the client's director, or nothing on the server" used by `BeginPlay` and both multicasts |

### E2. `AECDemoGasCharacter.h/.cpp` (baseline G)

A subclass with a replicated `UAbilitySystemComponent`, so the NQ/NB/T runs keep a character without GAS. `BeginPlay` (server) gives it the attack ability. `PossessedBy` (server) and `OnRep_Controller` (owning client) initialise the ability actor info again, so it knows the player controller (it initialised itself with owner = avatar at component start, `06` §33). `PlayActionMontage(Montage, Rate)` (the override): on the server it builds the event data (the montage in `OptionalObject`, the rate in `EventMagnitude`) and calls the public `TriggerAbilityFromGameplayEvent` with the tag `AEC.Demo.Action`, which stamps the tag into the data and activates (reuse finding: it replaced a call to the `Internal…` function); it returns whether the ability activated. On a client it does nothing and returns true: GAS plays the montage there. `StopActionMontage` (server) stops the ability's montage with `CurrentMontageStop`; the ability ends, and GAS stops the montage on clients. The tag is declared in the header (`UE_DECLARE_GAMEPLAY_TAG_EXTERN`) so the automation test checks the same symbol. The tag is not decoration: the owning client runs a server-initiated activation with its event data only if the tag is valid (`06` §34, §35).

### E3. `AECDemoAttackAbility.h/.cpp`

Constructor: `InstancedPerExecution` (the engine default, stated) and `ServerInitiated` (`06` §34). `ActivateAbility` reads the montage and rate from the event data (the montage pointer is `const` there; `const_cast` because the task's API takes a mutable pointer and never changes the asset), ends the ability at once if either is missing or the commit fails, and otherwise starts `PlayMontageAndWait` with three handlers: completed and interrupted end the ability normally, cancelled ends it as cancelled. It does **not** end on blend-out: that would stop the montage early on the owning client, whose copy started later. The task is created with `bAllowInterruptAfterBlendOut = true`, so an interruption that comes after the natural blend-out began still reports `OnInterrupted` and the ability always ends (code-review finding; otherwise neither delegate fires).

### E4. `AECDemoGameMode.h/.cpp`

Player controller class `AAECDemoPlayerController`; no default pawn. `HandleStartingNewPlayer_Implementation` replaces the base version on purpose: a remote player goes to `UAECBenchSubsystem::AddRemotePlayer`; a local player (the listen-server host) gets nothing and watches through the director's camera (`06` §29). It is used only through `?game=` in network runs.

### E5. `AECDemoPlayerController.h/.cpp`

`bAutoManageActiveCameraTarget = false` (possession must not move the view). `ClientBenchFinished` (Client, Reliable): the server's "the run is over"; the body calls the client director's `FinishClientRun`. S7 adds the clock-sync component here.

### E6. `AECBenchSubsystem.h/.cpp`: one director, three roles

| Part | What it does, and why |
|---|---|
| `-AECPlayers`, `-AECClient`, `-AECConfig=G` | New switches (header comment). `G` loads the Queued montages and spawns `AAECDemoGasCharacter` |
| `OnWorldBeginPlay` | Decides the world's role: **recorder** = `-AECClient` and a client world; **director** = not a client, not `-AECClient`, `-AECChars` > 0 (`06` §30). A recorder records with the default condition only (v1); a director with `-AECPlayers` needs the demo game mode. The camera is not spawned on a dedicated server (nobody looks). Network worlds reserve the frame log. Then: recorder → `Recording`; director with players → `WaitingForPlayers` (and the "Waiting for N remote player(s)" line the runner waits for); otherwise `BeginWarmUp` at once, as before |
| `bNetworked`, `bGas` | The net mode and the G config, fixed at begin play and read every frame and every action (efficiency finding) |
| `SpawnCharacter` | One spawn for NPCs and players: class by config, index = array size, the same deferred mesh settings as S2, then a state with its band (`BandOf`, the same rule the client uses) and its own random stream. `SpawnCharacters` loops it for the NPCs |
| `ReserveRecords(MaxCharacters)` | The S2 reservation, now shared: the director reserves for NPCs + players, and so does a recorder, from the same switches (`run_net.py` passes `-AECChars`, `-AECPlayers`, `-AECWarmup`, `-AECCapture` to the client too) |
| `AddRemotePlayer` | Spawns a character in band 3 (in front of the camera, nearer than band 0, one metre apart per player), possesses it, counts the player; plans its first action at once if the warm-up already started (otherwise `BeginWarmUp` plans everyone) |
| `BeginWarmUp` | Plans every character's first action and starts the warm-up clock. Standalone it runs at begin play with the begin-play time, so standalone runs draw exactly the same random numbers as before (S4 regression check: the same hashes) |
| `BeginAction` / `InterruptAction` | What starting and interrupting an action means on **any** machine, written once (simplification finding): the record (id = the server's index, character, montage, **role**, rate, start times, frame, in-capture), the character's `PlayActionMontage`, the timer path; and the stop time, `StopActionMontage`, the timer path's stop. The role is a parameter: on the server it follows from the character (player or NPC), on a client from its net role when the start arrives |
| `StartAction` / `StopAction` (server) | `BeginAction` / `InterruptAction`, plus the multicast with the action id in network runs |
| `StartTimerPath` / `StopTimerPath` | The T path, moved out of `StartAction`/`StopAction` so the client runs the same code when a start or stop arrives |
| `RegisterReplicatedCharacter`, `HandleServerStart`, `HandleServerStop` (client) | Grow the arrays to the bench index; record the start with the role read at that moment (`ROLE_AutonomousProxy` → auto, else sim), play (NQ/NB) and start the timers, then attribute the character's **held fires** (below) to the new action with their own times; a stop is applied only if it names the action the client is playing |
| `FindKey`, `LastFireTime`, held fires (G client) | In G a simulated proxy's montage comes through GAS's replicated property, which can arrive before the start message. A G client's fire that comes after its character's current action is over (its end, or stop + blend-out, plus 0.25 s for late Queued ends) is therefore **held** and given to the next action, with the times it fired at, when that start arrives; fires never claimed count as unattributed. The next action starts at least 0.5 s after the previous one ended, so the grace never takes a fire from it (code-review finding). NQ/NB clients start the montage on the start message, so they have no such race and no hold (altitude finding). A deeper fix, pairing each montage start with its start message, was considered and not taken: GAS's simulated-proxy restarts would be counted as new starts |
| `RecordFire` (two forms) | "Now" (and counted in this frame's CSV stat), or with given times (a held fire): one place builds a fire record |
| `HandleNetworkFailure` (client) | Bound to the engine's network-failure event: a client that loses its server before the run ended logs an error and quits with exit code 5, instead of travelling to the default map and waiting for the runner's timeout (altitude finding) |
| `FinishClientRun` | The server's end message: write the files, finish, quit |
| `Tick` | Done → quit (a server not before `ExitNotBefore`); network runs log (frame, game time, `FrameStartWallSeconds`) every frame (`06` §31); the view target is re-applied only for a **local** player controller (on a dedicated server the first controller is a remote client's); the director part moved to `TickDirector` |
| `TickDirector` | Waiting for players (with a 120 s limit, then exit code 3), then the S2 phases. When the files are written on a server, every remote client is told and the server waits 3 s before quitting, so its clients quit first (a client whose server vanished would travel to the default map instead) |
| `WriteRunFiles` | Header gains `net=` and `players=`; the actions file gains `role`; every `action` column holds the **server's** id; a client writes no expected file (the server's is the truth); network runs write `_frames.csv` |

### E7. `AECNetSetupTests.cpp`: `AnimEventClock.Net.HarnessClassesConfigured`

Checks, on the class defaults, the setup a network run depends on silently: the game mode's controller class and no default pawn; characters replicate and are always relevant; the GAS character's ability system replicates; the ability is server-initiated and instanced per execution; the native tag `AEC.Demo.Action` is registered.

### E8. `AnimEventClock.Build.cs`

`GameplayAbilities` becomes a **public** dependency (two public headers derive from its types); `GameplayTasks` and `GameplayTags` are private (only `.cpp` files call into them).

### E9. `Tools\event_stats.py`: network runs

| Part | What it does, and why |
|---|---|
| `load_run(prefix)` / `load_fires(events, in_capture, clock)` | `load_run` reads a run with an expected file once (header, actions in capture, due times, phases, events); `load_fires` builds the fire table on either clock. The server's events are read once and turned into both its game-time and its wall-clock table; a client's fires are built against the server's actions in capture (it has no expected file and no capture window of its own). Earlier drafts read the server twice (simplification and efficiency findings) |
| `wall_mapper(frames)` | Server game time → wall clock, by linear interpolation in the server's frame log; correct even across a server hitch |
| `frame_intervals(frames)` | Frame-to-frame wall-clock intervals of one process: shows a server below its 30 Hz cap. The server's frame log is read once for this and for the wall mapping |
| `FIXED_BUCKETS`, one group table in `measure` | The bucket names that are not groups, defined once (the network summary picks roles by the roles it knows); each expected event's group is computed once |
| `analyse_net(server, client)` | Server fires per role on its game time **and** on the wall clock; client fires per role on the wall clock, against the server's due times mapped to the wall clock. A character's client role comes from the client's own actions file; an action it never received is still expected ("unreceived") |
| `measure`: unexpected per group | Fires of events that were not expected (a client still firing after the server's stop) are now also counted per band or role |
| `selftest()` | Adds a hand-made network run: server game vs wall error, a remote player's window that begins twice (a window duplicate), a client miss, both client roles, and frame intervals |

### E10. `Tools\run_net.py`

One run = one server process + one client process, both started with `Popen` by a runner pinned to the performance cores (`05` A13), so both inherit the mask. The **server** is the editor binary with `-server` (dedicated, 30 Hz) or the packaged game with `?listen` (rendering, `t.MaxFPS`); both open `Template_Default` with `?game=` set to the demo game mode. The client is started only when the server's own log says it waits for its player; the runner then waits for the client, then the server, each with a timeout, and kills both trees in `finally`. Child output goes nowhere (the editor server would flood the queue log); each process writes its own `-abslog`. Latency is given as the **added round trip**: `-PktLag=<rtt/2>` on both processes, `j` adds `-PktLagVariance=10` (`06` §36). `--chars` must be at least 1 (a server without NPCs would never become the director). Files move to `Benchmarks\Raw\` as `<name>_srv_*` / `_cli_*`, never overwriting; `event_stats.analyse_net` analyses them; the manifest is rewritten after every run. The summary has three tables: wall-clock error per process, role and path (with a gate verdict against one frame of that process), the server's own fires in game time, and both processes' frame intervals. The batch protocol is **shared with `run_bench.py`**, not copied (reuse finding): `split_list`, `check_allowed`, `default_runs`, `rotated`, `batch_name`, `pin_batch` (keep awake, pin to the performance cores or refuse), `reset_game_user_settings`, `wait_or_none`, `kill_tree`, `move_run_files`, `copy_log`, the environment record and the summary cells all live in `run_bench.py`, whose own runs use the same functions (its manifests and summaries come out identical, checked on the S3 batches). The two rendering processes get their window switches from one `window_args`, and both processes their workload switches from one `sizing_args`. `wait_for_text` reads only what the server's log gained since its last look.

---

## Part F · S5: the pure timeline

### F1. `AECMontageTimeline.h/.cpp` (plugin)

Plain C++: no `UCLASS`, no `UObject` pointer, no world, so every rule can be tested alone (F2) and nothing has to be kept alive while an action plays.

| Part | What it does, and why |
|---|---|
| `FAECTimelineKey` | One authored event: name, phase, position (the engine's trigger time), and its window partner |
| `FAECTimelineSection` | One composite section: start, end (where the next section in time starts, or the montage's end), and the section that follows (`INDEX_NONE`: the montage ends) |
| `FAECPlayhead` | The mutable part: section, position, pass, `bIncludeStart`, finished, and the **open windows** (an inline array of 4: no heap allocation for normal montages, efficiency finding). Only goes forward along its path; a loop or a jump back is a new pass |
| `FAECCrossing` | A crossed key: index, pass, offset into the advance, `bForced` for a window end the path never reached, and the playhead's section and position at that moment. An advance ends before anyone reacts to its crossings, so a reaction that needs the playhead (the event clock's jump from a handler, S6) starts from the crossing's record, not from the playhead's end of frame (added while drafting S6, after its code review found the jump started from the wrong place) |
| `Build(Montage)` | Sections exactly as the engine plays them: its own indices, `GetSectionStartAndEndTime` and `GetSectionIndex(NextSectionName)` (`06` §38; reuse finding: an own sort and end computation could disagree with the engine on an out-of-order montage, and a later `JumpToSection` would need an index map); this plugin's markers at `GetTriggerTime` / `GetEndTriggerTime` (`06` §12), so the engine's edge offsets apply to the clock exactly as to the notify. Arrays reserved up front |
| `AddSection` / `AddInstant` / `AddWindow` / `Finalize` | Hand-made timelines. `AddWindow` adds both keys and links them **before** sorting; `Finalize` sorts (stable) and re-points every pair through the new order, so a window's keys always find each other, whatever their names or order (code-review finding: pairing by name and order could leave an inverted window unpaired, and the bench indexes the pair without a check) |
| `Start(Section)` | A playhead at a section's start, including its start position; finished at once if the section does not exist. A start inside a window begins that window on the first advance |
| `Advance(Head, Delta, Out)` | The walk of 02 §6.3. Per step: to the section's end **exactly** (a float `From + (End − From)` can land one ulp short and miss the section change: code-review finding) or as far as the delta reaches; every key in `(From, To]`, or `[From, To]` after a start, loop or jump (`Algo::LowerBoundBy` / `UpperBoundBy`, `06` §38). Right after an entry, a window that spans the entry point **begins** there, as the engine begins a notify state the playback enters partway (`AnimSequenceBase.cpp:478`; altitude finding). At a section end: the montage's end finishes the path and ends any window still open there (its end lies in a section the path never plays; the engine ends a notify state when its montage stops), due at the path's end, not at the end of the delta; a next section that starts exactly where the playhead is, is a continuation; anything else goes through `EnterSection`. The guard counts only steps **in a row** that make no progress: a long advance through a normal loop (a late join catching up) must not end the action (code-review and altitude findings) |
| `EnterSection` | The one rule for every entry other than a continuation (a loop back, a skip, a jump): close the open windows with forced ends, add a pass if the target is montage time already passed (not when a fresh playhead "jumps" onto its own start), include the start (simplification and altitude findings: the loop and the jump each had their own version) |
| `JumpToSection` / `RewindHeadToHandledCrossing` | `CatchUp`, then the timeline's jump (forced ends for the windows it leaves); the path serial moves on and `LastTime` is the jump's time; the montage follows. From a handler, the playhead is already at the end of the frame's advance, so it is first put back where it was at the handled crossing (`FAECCrossing::Section` / `Position` / `Pass`) with the windows handlers saw open: otherwise the windows the jump leaves would close late or not at all, and a forward jump would count as a new pass (both code-review findings). `FinishAction` uses the same rewind |
| `Stop` | Closes the open windows and finishes the playhead: every begin gets its end |
| `DueTime` | `Now − (Delta − Offset) / max(PlayRate, 0.01)`: a play rate of 0 can never give an infinite due time |
| `Cross` / `CloseOpenWindows` | Every crossing goes through `Cross`, which keeps the open windows: a begin opens, an end closes (an end whose window is not open is not reported). `CloseOpenWindows` adds the forced ends and empties the list, in **key** order rather than the order the windows opened (the end keys are copied into a small inline array, 4 without a heap allocation, and sorted): then every crossing of a path is ordered by (pass, position, key), which a client compares with the point where the server cut the path (S7). Every way a path ends closes its windows: a stop, the montage's end, and the empty-step guard (S5 review findings) |

### F2. Tests

`AECMontageTimelineTests.cpp` (plugin, no world): the half-open range (and zero / negative advances); keys at the start and at the end, and the snap onto a section end; the section path (every key once, the boundary key once, a skipped section, a non-contiguous next section's start key); a loop (new pass, the start key fires again; a window cut by the loop point ends every pass); jumps (a forced end when leaving a window, pass rules, a fresh playhead); the guard (a zero-length loop stops, a normal loop survives 200 passes in one advance); empty timelines; due time at two play rates and at 0; a stop closing a window, a reused window name, and an inverted window. a start inside a window begins it. `AnimEventClock.Timeline.BuildFromDemoMontages`, next to the asset test in `AECDemoMontageTests.cpp` (game module, sharing its tolerance and flags: reuse finding): `Build` on all four demo montages matches the spec (sections, ends, links, keys, positions, pairs), and one long advance crosses every key once, in order.

### F3. The director reads its keys through the timeline

`UAECBenchSubsystem::LoadMontages` now takes `FAECMontageTimeline::Build(*Montage).GetKeys()` (its `FKey` is the timeline's key type) instead of its own loop: one reader, so the expected events are exactly the ones the clock will fire. For the demo montages the keys, their order and their pairs are identical to the S2 loop's, so the standalone fire hashes do not change (checked in S5).

---

## Part G · S6: the event clock component (authority)

### G1. `AECEventClockComponent.h` (plugin)

| Part | What it does, and why |
|---|---|
| `FAECEventInfo` (`USTRUCT(BlueprintType)`) | What a handler gets: the owner, the event's name and phase, the action id, the key index and pass (together a stable id), the exact due time, the fire time, the lateness, and whether a window end was forced (a stop, a loop or a jump). The owner is in it so one handler object can serve many actors (the bench does) |
| `FAECClockEventSignature` | A dynamic multicast delegate, so Blueprints can bind (`BlueprintAssignable`, `06` §39) |
| `PlayAction` / `StopAction` / `JumpToSection` | The API (`BlueprintCallable`). Each first fires what was due until now, so nothing is skipped by calling it mid-frame; from inside a handler, "now" is the due time of the event being handled and nothing is advanced (`CatchUp`, 02 §6.1) |
| `MaxPathRestartsPerFrame` (8) | How often one frame re-advances after a handler restarted the path (a jump or a new action); the rest waits a frame, so a handler that jumps on every entry cannot hold the game |
| `IsWindowOpen` / `IsActionActive` / `GetActionId` | The state handlers see: a window is open between the begin and end events that went out; once the action is over, every window counts as closed, also while its last interrupted ends go out (code-review finding: stated, not changed) |
| `bDriveMontage`, `MaxVisualDriftSeconds`, `StopBlendOutSeconds` | The montage is the visual follower (02 §2); off when something else plays it (the bench's NQ/NB path, GAS) |

### G2. `AECEventClockComponent.cpp`

| Part | What it does, and why |
|---|---|
| `aec.Enabled`, `aec.Debug` | `TAutoConsoleVariable`s (`06` §40): 0 turns dispatch off (the "clock off" overhead configuration); 1 logs every event, 2 also draws the open windows |
| Constructor | Tick in `TG_PrePhysics` (the mesh's group, so clock events and notifies happen at the same point of the frame), **off** until an action starts (`06` §41) |
| `FDispatchScope` | Marks "handlers are running" for a scope; only the outermost scope (not the nested dispatch of a handler's stop or jump) reads the cycle counter and adds its cycles to the frame's `DispatchUs` stat, so nothing is counted twice |
| `CatchUp` / `PlayAction` | `CatchUp`: outside a handler, fire what was due until now and return now; inside one, return the handled event's due time (nothing is advanced there). `PlayAction`: a running action is stopped at that moment (interrupted ends) first; if a handler of those events destroyed the actor, no new action starts (`INDEX_NONE`, code-review finding); then the timeline is built from the montage (`FAECMontageTimeline::Build`), the playhead starts, the action id and the path serial move on, `LastTime` is the start time, the tick goes on, and the montage plays if driven |
| `StopAction` | `CatchUp`, then `FinishAction`: a stop never loses what was already due |
| `JumpToSection` | Advance to now, then the timeline's jump (forced ends for the windows it leaves); the path serial moves on and `LastTime` is the jump's time; the montage follows. From a handler, the playhead is already at the end of the frame's advance, so it is first put back where it was at the handled crossing (`FAECCrossing::Section` / `Position` / `Pass`) with the windows handlers saw open: otherwise the windows the jump leaves would close late or not at all, and a forward jump would count as a new pass (both code-review findings) |
| `TickComponent` | `AdvanceTo(now)`, then the montage follower; counts the active action for the CSV stats |
| `AdvanceTo` | Never runs inside a dispatch (`check`; `CatchUp` returns first there), so the crossing buffer is never rewritten while it is dispatched. Delta = clock time since `LastTime` × play rate; `Advance`; `Dispatch`; the action finishes when the path ends. If a handler restarted the path (a jump or a new action, seen by the path serial), the crossings after it were stale and were dropped, and the rest of the frame is advanced again on the new path, from the handled event's due time |
| `Dispatch` | Per crossing (no copy: nothing can change the list while it runs, simplify finding): the window state (an end goes out exactly when its begin did: not for a begin dropped by a handler's jump or crossed with `aec.Enabled 0`, always for one that went out, even if `aec.Enabled` was switched off since: code-review finding), the due time from the advance (`FAECMontageTimeline::DueTime`; a jump's forced ends pass Delta 0 and are due at the jump), `Broadcast`; after each, it stops if the path changed, the action is over, or the component or its owner is going away (02 §8) |
| `MakeEventInfo` / `Broadcast` | One event's `FAECEventInfo`, and its delivery: the due time handlers' calls take effect at, the counter, the `aec.Debug` log line, `OnClockEvent.Broadcast` |
| `FinishAction` | The action goes off **before** its last events, so a handler sees it as over and may start the next one; a driven montage is stopped first for the same reason (a handler may play it again). Then every window a handler saw begin gets its interrupted end, due at the stop: the timeline's own `Stop` (forced ends in key order), from the rewound playhead when a handler stopped or replaced the action (otherwise an end could carry the pass of a loop crossed later in the same frame: simplify finding). The events are built before any goes out, since a handler that starts the next action replaces the timeline |
| `FollowMontage` / `GetDrivenAnimInstance` | The pose follows the clock: the montage instance (`GetActiveInstanceForMontage`, looked up once a frame) is moved onto the playhead only when it drifts beyond `MaxVisualDriftSeconds` (`FAnimMontageInstance::SetPosition`, what `Montage_SetPosition` does: no notify fires), counted as a visual correction. The owner's skeletal mesh is found once and kept as a weak pointer (efficiency finding) |
| `EndPlay` | No events from `EndPlay` (02 §8) |

### G3. Telemetry and the module

`AECClockTelemetry.h` declares one struct of plain counters, `FFrameCounters` (events fired, active actions, visual corrections, dispatch cycles), and one instance of it; `AnimEventClockCoreModule.cpp` defines it and publishes them **once per frame** from `FCoreDelegates::OnEndFrame` as CSV stats in category `AnimEventClock`, then resets it with one assignment (`06` §42): one stat call per frame, not per event.

### G4. The AEC path in the harness (`-AECClock`)

Every demo character now owns an event clock (a default subobject; it does not tick until an action starts, so runs without `-AECClock` are unchanged). With `-AECClock` the director binds `OnClockEvent` to `HandleClockEvent`, turns `bDriveMontage` off (the NQ/NB path plays the montage), and plays and stops every action through the clock too (`PlaysClock()`: `-AECClock` on the authority). The clock's `PlayAction` is called **before** the director moves the character's current action on, so a clock action still running then (only after a hitch) ends inside that call while its events still belong to it (code-review finding; simplify: one call instead of a separate stop). `PathName` writes the path column (NQ / NB / G, T, AEC). `HandleClockEvent` attributes each event to the character's current action, checked against the clock's action id that `PlayAction` returned (stored on the action), so an event of an earlier action is never counted for a later one; the clock's key index is the director's key index, since both come from `FAECMontageTimeline::Build`. The events file names the path `AEC`; the header says `clock=1`, which makes `event_stats.py` measure the AEC path even in a run where it fired nothing. `run_bench.py --clock` passes the switch.

### G5. Tests (`AECEventClockTests.cpp`, game module, a real minimal world)

A game world made, played, ended and destroyed by the engine's `FTestWorldWrapper` (`06` §43), one actor with a clock and no mesh, time moved by hand (`World->TimeSeconds`) and the clock ticked directly, with `UAECClockTestListener` (a test-only `UCLASS` that records events and can react once). One test, `AnimEventClock.Clock.DueTimesAndDestroyDuringDispatch` (the owner's limit for this step): every key of the attack montage once, in order, with its exact due time, never early and under one frame late at play rate 2; then, in a second world, a handler that **destroys** the actor at the first event: dispatch stops there and later frames are safe. The listener can also stop, jump and start another action; those cases are **not tested**.
