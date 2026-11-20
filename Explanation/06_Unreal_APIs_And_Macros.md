# 06 · Unreal APIs And Macros: AnimEventClock

**Rule:**
- Only APIs this project **actually uses** appear here, each in the same step it enters the code.
- Each entry follows the 8-point format: what it is · why it exists · how it works · when to use it · when not to · common mistakes · a small example · how we use it.
- Verified against UE **5.8.4** source; file references are relative to `Engine/` unless another root is named.

| # | API | First used in |
|---|---|---|
| 1 | `IMPLEMENT_PRIMARY_GAME_MODULE` / `IMPLEMENT_MODULE`, `FDefaultGameModuleImpl` / `FDefaultModuleImpl` | Phase 4 |
| 2 | `ModuleRules`: dependency lists and `PCHUsage` | Phase 4 |
| 3 | `TargetRules`: `Type`, `DefaultBuildSettings`, `IncludeOrderVersion`, `ExtraModuleNames` | Phase 4 |
| 4 | Descriptor module `Type` and `LoadingPhase` (`.uproject` / `.uplugin`) | Phase 4 |
| 5 | Descriptor `Plugins` list (enabling engine plugins) | Phase 4 |
| 6 | `ProjectPackagingSettings` `DirectoriesToAlwaysCook` | Phase 4 |
| 7 | `GameMapsSettings` (`GameDefaultMap`, `EditorStartupMap`, `ServerDefaultMap`) | Phase 4 |
| 8 | Command-line launch modes: `-game`, `-server`, `-nullrhi`, `-ExecCmds`, `-abslog`, `-traceautostart=0` | Phase 4 |
| 9 | `UAnimNotify` / `UAnimNotifyState` subclasses | S1 |
| 10 | `UCLASS(MinimalAPI)` + `UE_API`, `UENUM`, `UPROPERTY`, `GENERATED_BODY` | S1 |
| 11 | Native multicast delegates | S1 |
| 12 | Montage data: `FAnimNotifyEvent`, `CompositeSections`, `RefreshCacheData` | S1 |
| 13 | `UCommandlet` | S1 |
| 14 | `UAnimMontageFactory`, `UAnimationBlueprintLibrary` (editor) | S1 |
| 15 | Automation tests | S1 |
| 16 | `CreatePackage`, `SavePackage`, `FPackageName`, `LoadObject` | S1 |
| 17 | `SetDefaultSubobjectClass`, `ConstructorHelpers::FObjectFinder` | S2 |
| 18 | Tick group and tick prerequisites | S2 |
| 19 | `UTickableWorldSubsystem` | S2 |
| 20 | `SpawnActorDeferred` / `FinishSpawning` | S2 |
| 21 | `Montage_Play` / `Montage_Stop`, `RootMotionMode` | S2 |
| 22 | `FTimerManager::SetTimer` with a payload delegate | S2 |
| 23 | Console variables from code | S2 |
| 24 | Mesh update settings, `DisableMovement` | S2 |
| 25 | CSV profiler macros and small utilities | S2 |
| 26 | `IAnimationBudgetAllocator::Get` / `SetEnabled` | S3 |

---

## 1. `IMPLEMENT_PRIMARY_GAME_MODULE` / `IMPLEMENT_MODULE`

1. **What it is.** Macros that generate a module's entry point (`Source/Runtime/Core/Public/Modules/ModuleManager.h:946–955` for `IMPLEMENT_MODULE`, `:984` for the game variant, `:1094–1136` for the primary game variant).
2. **Why it exists.** The engine loads code as **modules**: DLLs in editor builds, statically linked code in packaged builds. Each module needs a standard way to be found and started, so the module manager can call its `StartupModule()`/`ShutdownModule()`.
3. **How it works.**
   - In a modular (editor) build it exports an `InitializeModule` function that the module manager calls after loading the DLL; it returns an instance of the class you name.
   - In a monolithic (packaged) build it registers a static initialiser instead.
   - The **primary** variant also sets up project-wide globals for packaged builds (the project name passed in by UBT from the `.uproject`). Its third argument is named `DEPRECATED_GameName` and is ignored: "manually specifying a name is no longer necessary" (`ModuleManager.h:1094–1096`).
4. **When to use it.** Exactly one per module; exactly one *primary* game module per project.
5. **When not to.** Never twice in one module (duplicate-symbol link errors). Plugin modules and secondary game modules use `IMPLEMENT_MODULE`.
6. **Common mistakes.**
   - The module-name argument doesn't match `Build.cs` and the descriptor ("module not found" at start-up).
   - Writing an empty custom `IModuleInterface` class when `FDefaultModuleImpl` would do.
   - Putting the primary macro in an editor-only module: the packaged game then has no primary module.
7. **Example.**
   ```cpp
   #include "Modules/ModuleManager.h"
   IMPLEMENT_MODULE(FDefaultModuleImpl, MyPluginModule);
   ```
8. **In this project.** `AnimEventClockModule.cpp` uses the primary variant with `FDefaultGameModuleImpl`. `AnimEventClockEditor`, `AnimEventClockCore` and `AnimEventClockCoreEditor` use `IMPLEMENT_MODULE(FDefaultModuleImpl, …)`. None needs start-up logic yet; `AnimEventClockCore` gets a small module class in S6 (02 §4.1).

## 2. `ModuleRules`: dependencies and `PCHUsage`

1. **What it is.** The C# class in `<Module>.Build.cs` that tells UnrealBuildTool how to compile one module.
2. **Why it exists.** Unreal is hundreds of modules. Each declares what it depends on, so UBT can work out include paths, link order and what to rebuild.
3. **How it works.**
   - `PublicDependencyModuleNames`: their include paths are given to us *and* to every module that depends on us.
   - `PrivateDependencyModuleNames`: only to us.
   - Both link the dependency.
   - `PCHUsage = UseExplicitOrSharedPCHs` uses a shared precompiled header built for a common dependency set (`SharedPCH.UnrealEd.Project.ValApi.ValExpApi.Cpp20` in our editor build log), unless a module names its own.
4. **When to use which.** Public **only** if one of *your public headers* includes that module's headers. Everything else private.
5. **When not to.** Don't add modules "just in case": every dependency costs build time and couples modules.
6. **Common mistakes.**
   - Making everything public, which widens include paths and rebuilds.
   - A missing dependency: `unresolved external symbol` or `cannot open include file`.
   - A runtime module depending on an editor-only module (`UnrealEd`, `DataValidation`): the game target then fails to build. That is why this project has two editor modules (02 §3).
7. **Example.**
   ```csharp
   PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
   PrivateDependencyModuleNames.Add("AnimEventClockCore");
   ```
8. **In this project.** The game module and the runtime plugin module depend publicly on `Core`, `CoreUObject`, `Engine` (their public headers will expose engine types). The two editor modules depend privately (they are leaves). Every other dependency (`AnimEventClockCore`, `GameplayAbilities`, `AnimationBudgetAllocator`, possibly `NetCore`, `UnrealEd`, `AssetTools`, `DataValidation`) is added in the step whose code first uses it.

## 3. `TargetRules`: `Type`, `DefaultBuildSettings`, `IncludeOrderVersion`, `ExtraModuleNames`

1. **What it is.** The C# class in `<Name>.Target.cs` that describes one executable: game, editor, client, server or program.
2. **Why it exists.** The same modules build into different executables, each with different settings and module sets.
3. **How it works.** UBT finds the class `<Name>Target`, builds its `ExtraModuleNames` plus the engine and enabled plugins. `DefaultBuildSettings` selects a versioned set of default build options; `IncludeOrderVersion` selects which engine version's include order to use. On an installed (launcher) engine, UBT also checks the target type and configuration against the engine's `[InstalledPlatforms]` list and refuses anything not listed (`Source/Programs/UnrealBuildTool/Configuration/UEBuildTarget.cs:1396–1417`).
4. **When to use it.** One file per executable type. Here: `AnimEventClock` (game) and `AnimEventClockEditor` (editor).
5. **When not to.** No `Server` target on this engine: the launcher build lists only Editor and Game configurations for Win64 (`Config/BaseEngine.ini:4001–4008`), and UBT would refuse it ("{0} targets are not currently supported from this engine distribution."). Dedicated servers run on the editor binary instead (§8).
6. **Common mistakes.**
   - The class name doesn't follow `<Name>Target`.
   - Adding an editor-only module to the **game** target's `ExtraModuleNames`.
   - Expecting `Test` or `Server` to build on a launcher engine.
7. **Example.** `Source\AnimEventClock.Target.cs` (`05` A2).
8. **In this project.** `V7` and `Unreal5_8`, as Epic's 5.8 template. The editor target adds `AnimEventClockEditor` to `ExtraModuleNames`; the game target doesn't.

## 4. Descriptor module `Type` and `LoadingPhase`

1. **What it is.** Two fields on each module entry in `.uproject`/`.uplugin`: which kinds of executable load it (`Type`, the `EHostType` enum) and when during start-up (`LoadingPhase`).
2. **Why it exists.** Editor tools must not ship in games; some modules must exist before others start.
3. **How it works.** `EHostType` (`Source/Runtime/Projects/Public/ModuleDescriptor.h:83–118`): `Runtime` "Loads on all targets, except programs"; `UncookedOnly` "Only loads in uncooked games"; `Editor` "Loads only when the editor is starting up"; `EditorNoCommandlet` the same but not in commandlets. Phases run in order (`PostConfigInit`, `PreDefault`, `Default`, `PostDefault`, …); a module another loaded module depends on loads first regardless.
4. **When to use which.** `Runtime` for gameplay code; `Editor` for editor tools that commandlets also need; `Default` phase for ordinary code.
5. **When not to.** Don't pick an early phase "to be safe": early modules can't use systems that aren't up yet. Don't use `EditorNoCommandlet` for a module that contains a commandlet.
6. **Common mistakes.** An editor module typed `Runtime` (the game target then tries to build it against editor-only engine modules); UClasses a map needs registered too late (`PostDefault` or later).
7. **Example.** `{ "Name": "AnimEventClockEditor", "Type": "Editor", "LoadingPhase": "Default" }`
8. **In this project.** Both runtime modules are `Runtime`/`Default`; both editor modules are `Editor`/`Default` (`Editor`, not `EditorNoCommandlet`, because S1's commandlet lives in one of them). Check F-7 confirms the split: the editor binary in `-server` mode loads `AnimEventClock` and `AnimEventClockCore` but neither editor module.

## 5. Descriptor `Plugins` list

1. **What it is.** The `"Plugins": [ { "Name": …, "Enabled": true } ]` array in the `.uproject`.
2. **Why it exists.** Engine plugins that are off by default must be switched on per project; listing a default-on plugin documents the dependency.
3. **How it works.** At start-up the plugin manager mounts every enabled plugin (log: `LogPluginManager: Mounting Engine plugin AnimationBudgetAllocator`), loads its modules by their own `Type`/`LoadingPhase`, and enables the plugins *it* depends on.
4. **When to use it.** For every engine plugin the project's code or content needs.
5. **When not to.** Don't enable plugins nothing uses: each one adds load time, cooked content and possible warnings.
6. **Common mistakes.** Enabling a plugin but forgetting the module dependency in `Build.cs` (link errors), or the reverse (the module isn't loaded at run time). Forgetting that default-on plugins can write project config: `AndroidFileServer` adds a random token to `DefaultEngine.ini` unless disabled.
7. **Example.** `{ "Name": "GameplayAbilities", "Enabled": true }`
8. **In this project.** `AnimationBudgetAllocator` (off by default; the budget conditions) and `GameplayAbilities` (off by default; baseline G) are enabled; `AndroidFileServer` (on by default) is **disabled**, because its editor half writes a random `SecurityToken` into `DefaultEngine.ini` and the project has no Android target (`05` A1). The two plugins were enabled in Phase 4 so the foundation build proves the launcher engine ships both as usable precompiled plugins (check F-7 log: `UnrealEditor-AnimationBudgetAllocator.dll`, `UnrealEditor-GameplayAbilities.dll` loaded).

## 6. `ProjectPackagingSettings` `DirectoriesToAlwaysCook`

1. **What it is.** A config array on `UProjectPackagingSettings`: "Directories containing .uasset files that should always be cooked regardless of whether they're referenced by anything in your project" (`Source/Developer/DeveloperToolSettings/Classes/Settings/ProjectPackagingSettings.h:564–568`).
2. **Why it exists.** The cooker includes only what maps and other cooked assets reference. Assets loaded only from C++ by path would be missing from the packaged game.
3. **How it works.** Listed folders are added to the cook's root set. The config section is `[/Script/UnrealEd.ProjectPackagingSettings]` even though the class now lives in `DeveloperToolSettings`: it overrides its section name (`:190`; `Config/BaseGame.ini:87`).
4. **When to use it.** For content referenced only from code or loaded dynamically by path.
5. **When not to.** Not for content a cooked asset already references (it cooks anyway), and not for huge folders "just in case" (cook time, package size).
6. **Common mistakes.** A path without the mount point (`/Game/…`, `/Engine/…`), or the wrong section name (silently ignored).
7. **Example.** `+DirectoriesToAlwaysCook=(Path="/Engine/BasicShapes")`
8. **In this project.** `/Engine/Tutorial/SubEditors/TutorialAssets/Character` (the demo character's mesh, skeleton and sequences, referenced by path, never copied: `01b #13`). Check F-5: all nine packages of the folder appear in the cook's `ReferencedSet.txt`, and the cook reports 0 errors and 0 warnings.

## 7. `GameMapsSettings`: `GameDefaultMap`, `EditorStartupMap`, `ServerDefaultMap`

1. **What it is.** The project's default maps (`Source/Runtime/EngineSettings/Classes/GameMapsSettings.h`; `ServerDefaultMap` at `:213`).
2. **Why it exists.** A game, an editor and a dedicated server each need a map to open when none is given on the command line.
3. **How it works.** `UGameMapsSettings::GetGameDefaultMap` returns `IsRunningDedicatedServer() ? ServerDefaultMap : GameDefaultMap` (`Source/Runtime/EngineSettings/Private/EngineSettingsModule.cpp:92–108`); the editor opens `EditorStartupMap`. So **a dedicated server opens `ServerDefaultMap`**, whose engine default is `/Engine/Maps/Entry` (`Config/BaseEngine.ini:16`), an empty map.
4. **When to use it.** Always set all three in a project that runs dedicated servers.
5. **When not to.** A map given on the command line (`UnrealEditor-Cmd.exe Project.uproject /Game/Map -server`) overrides them.
6. **Common mistakes.** Setting only `GameDefaultMap`: the server then sits in `Entry` and clients that connect are sent to an empty world. **This project made exactly this mistake** in its first Phase 4 job (F-7a) and fixed it (F-11).
7. **Example.** `ServerDefaultMap=/Engine/Maps/Templates/Template_Default`
8. **In this project.** All three point at `/Engine/Maps/Templates/Template_Default`, a shipped map with a floor and lights.

## 8. Command-line launch modes

1. **What it is.** The switches that decide how an Unreal executable starts.
2. **Why it exists.** One editor binary can run the editor, a game, a dedicated server or a commandlet; one packaged binary can run with or without a GPU.
3. **How it works.**
   - `-game` runs the editor binary as a game; `-server` as a **dedicated server**: "An editor build can be launched with -server" (`Source/Runtime/Core/Public/Misc/CoreMisc.h:146–166`). A packaged Game build can never be a dedicated server (`IsGameOnly`).
   - `-nullrhi` creates no GPU device (no window, no rendering).
   - `-ExecCmds="a, b"` queues console commands as deferred commands run once start-up has finished (`Source/Runtime/Engine/Private/UnrealEngine.cpp:2552`).
   - `-abslog=<path>` writes the log to an absolute path; `-log` also opens a log console.
   - `-traceautostart=0` stops an open Unreal Insights from tracing the run (UE5-UpdateBudget, its 01b §4).
4. **When to use which.** Smoke tests: `-nullrhi` + `-ExecCmds="quit"`. Timing runs: a real RHI (the game-thread time stat needs it, UE5-UpdateBudget S1). Dedicated server: the editor binary + `-server`.
5. **When not to.** Not `-nullrhi` for any run that measures frame times.
6. **Common mistakes.** Expecting a packaged build to accept `-server`; passing a flag that doesn't exist (a search of the 5.8.4 source finds no `-ExitAfterSeconds`, which UE5-UpdateBudget's smoke script passes: it is ignored).
7. **Example.** `UnrealEditor-Cmd.exe AnimEventClock.uproject -server -nosound -unattended -ExecCmds="quit" -abslog=Smoke_server.log`
8. **In this project.** `Tools\smoke_game.bat` modes `packaged`, `editor`, `server` (`05` A12). The server mode logs `Bringing World … up for play (max tick rate 30)` (`Source/Runtime/Engine/Private/World.cpp:6008`), the 30 Hz dedicated-server cap (`01b #14`).

---

## APIs added in steps S1 and S2

Paths in this section are relative to `Engine/Source/Runtime/Engine/` unless they start with `Engine/`. Format, as above: **1** what it is · **2** why it exists · **3** how it works · **4** when to use · **5** when not to · **6** common mistakes · **7** example · **8** in this project.

## 9. `UAnimNotify` / `UAnimNotifyState` subclasses

1. Base classes for objects placed on a sequence's or montage's notify track (`Classes/Animation/AnimNotifies/AnimNotify.h:50–125`, `AnimNotifyState.h`). 2. They let animation trigger code at a moment (notify) or over a range (state). 3. The engine stores each placement as an `FAnimNotifyEvent` that owns an instanced notify object; when the montage passes it, the engine calls `Notify` (or `NotifyBegin`/`NotifyTick`/`NotifyEnd`), from the notify queue or, for BranchingPoint, through `BranchingPointNotify*`, whose defaults forward to the same functions (`Private/Animation/AnimNotify.cpp:37–41`, `AnimNotifyState.cpp:44–65`). 4. For effects tied to the visible pose: sounds, particles, footsteps. 5. Not as the *authority* for gameplay timing: they fire only when the mesh's animation updates (`01b #1`). 6. Forgetting `Super::Notify` (the Blueprint event stops firing); assuming BranchingPoint passes a full `FAnimNotifyEventReference` (it passes an empty one); keeping per-instance state in the notify object (one object is shared by every mesh that plays the asset). 7. `void UMyNotify::Notify(USkeletalMeshComponent* Mesh, UAnimSequenceBase* Anim, const FAnimNotifyEventReference& Ref) { Super::Notify(Mesh, Anim, Ref); /* … */ }` 8. `UAECEventNotify` and `UAECEventWindowNotifyState` are **authoring markers**: they hold only `EventName` and report to a comparison hook; the gameplay clock (S6) reads their positions instead.

## 10. `UCLASS(MinimalAPI)` + `UE_API`, `UENUM(BlueprintType)`, `UPROPERTY`, `GENERATED_BODY`

1. Reflection macros read by UnrealHeaderTool. 2. They give the engine type information for serialisation, the editor, Blueprint and garbage collection. 3. UHT generates code into `<File>.generated.h`/`.gen.cpp`; `GENERATED_BODY()` pastes the generated members into the class. `MinimalAPI` exports only what other modules need to *use* the type (`StaticClass`, casting); individual members are exported with the module's `_API` macro (here via 5.8's `UE_API` alias). 4. Any type the engine must know about. 5. Not for plain data that never leaves C++ (our `FKey`, `FAction`: plain structs). 6. A missing `.generated.h` or not last in the include list; exporting a whole class with `<MODULE>_API` when only two functions are called from outside. 7. `UCLASS(MinimalAPI) class UFoo : public UObject { GENERATED_BODY() public: UE_API void Bar(); };` 8. The marker classes are `MinimalAPI` with `UE_API` on the overrides; `EAECEventPhase` is a `BlueprintType` `uint8` enum.

## 11. Native multicast delegates

1. `DECLARE_MULTICAST_DELEGATE_*Params` declares a type that many C++ functions can bind to and that `Broadcast` calls in turn. 2. Decoupling: the sender doesn't know who listens. 3. A small array of bound callables; `AddUObject(Object, &Class::Func)` returns an `FDelegateHandle`; bindings to a destroyed UObject are skipped (weak); `Remove(Handle)` unbinds. 4. C++-only notifications, many listeners, low cost. 5. When Blueprint must bind (use a dynamic multicast delegate, S6's `OnClockEvent`). 6. Never removing a binding (it leaks a dead entry until GC); broadcasting from a worker thread to game-thread listeners. 7. `MarkerHookHandle = AnimEventClock::OnMarkerFiredByAnimation().AddUObject(this, &UAECBenchSubsystem::HandleMarkerFiredByAnimation);` 8. The comparison hook: the director binds in `OnWorldBeginPlay` and removes in `Deinitialize`.

## 12. Montage data: `FAnimNotifyEvent`, `CompositeSections`, `RefreshCacheData`

1. A montage's authored data: `Notifies` (an array of `FAnimNotifyEvent`, sorted by time, `Classes/Animation/AnimSequenceBase.h:41–43`) and `CompositeSections` (`AnimMontage.h:697`). 2. The runtime reads them to fire notifies and play sections. 3. `GetTriggerTime()` = link time + a tiny edge offset; `GetEndTriggerTime()` for states; `MontageTickType` chooses Queued or BranchingPoint; `IsBranchingPoint()` also requires a linked montage (`Private/Animation/AnimTypes.cpp:81–110`). `RefreshCacheData()` (runtime, `AnimMontage.cpp:706–711`) rebuilds the branching-point marker list. 4. Reading authored timing at run time (01b #10). 5. Editing them in a running game: other code caches the marker list. 6. Changing `MontageTickType` without `RefreshCacheData()` (the marker list is stale); reading the raw link time instead of `GetTriggerTime()` (off by the edge offset). 7. `for (const FAnimNotifyEvent& E : Montage->Notifies) { const float T = E.GetTriggerTime(); }` 8. The commandlet sets the tick type and refreshes; the director reads keys with `GetTriggerTime`/`GetEndTriggerTime`, as the clock will.

## 13. `UCommandlet`

1. A class whose `Main(Params)` runs instead of the editor UI when the editor is started with `-run=<Name>` (`Classes/Commandlets/Commandlet.h`). 2. Batch jobs: cooking, asset generation, validation. 3. The engine loads the editor without a viewport, constructs the commandlet and returns `Main`'s value as the exit code. 4. Reproducible asset work that must go through a queue. 5. Anything a designer must judge visually. 6. Putting it in a runtime module (it then ships in games); forgetting that it runs with the editor's full module set. 7. `UCLASS() class UMyCommandlet : public UCommandlet { GENERATED_BODY() virtual int32 Main(const FString& Params) override; };` 8. `UAECMakeDemoAssetsCommandlet`, in the editor-only game module, run by `Tools\make_assets.bat` through the queue.

## 14. `UAnimMontageFactory` and `UAnimationBlueprintLibrary` (editor)

1. The factory behind the editor's "Create Montage" (`Engine/Source/Editor/UnrealEd/Classes/Factories/AnimMontageFactory.h:18–40`) and Epic's scripting library for animation assets (`Engine/Source/Editor/AnimationBlueprintLibrary/Public/AnimationBlueprintLibrary.h:240–299`). 2. So tools build assets exactly as the editor does. 3. The factory makes a montage from a sequence (slot track, length, first section); the library adds tracks and notifies, computing the editor's trigger offsets and refreshing cached data. 4. Generating or batch-editing animation assets. 5. In runtime code (editor-only modules). 6. Calling an un-exported `MinimalAPI` override directly (link error): call through the exported base, `UFactory*`. Adding a notify outside `[0, length]` (the library refuses and logs). 7. `UAnimationBlueprintLibrary::AddAnimationNotifyEvent(Montage, TEXT("AEC"), 1.2f, UAECEventNotify::StaticClass());` 8. Both, in the commandlet (`05` B5).

## 15. Automation tests

1. `IMPLEMENT_SIMPLE_AUTOMATION_TEST(Class, "Path.Name", Flags)` defines a test class whose `RunTest` the automation framework calls (`Engine/Source/Runtime/Core/Public/Misc/AutomationTest.h`). 2. Repeatable checks runnable from the command line. 3. `TestEqual`, `TestNearlyEqual`, `TestNotNull`… record failures with a message and return whether they passed; `Automation RunTests <Filter>` runs every test whose name starts with the filter and the editor exits with the result (`Tools\run_tests.bat`). 4. Asset and logic checks that need no player. 5. For timing measurements (use the benchmark). 6. Wrong context flags (the test never runs); comparing types the framework can't print (we cast enums to `int32`). 7. `TestNearlyEqual(TEXT("start"), Actual, Expected, 0.001f);` 8. `AnimEventClock.Assets.DemoMontagesMatchSpec` (S1); the S5 timeline tests follow.

## 16. `CreatePackage`, `UPackage::SavePackage`, `FPackageName`, `LoadObject`

1. The API that creates, saves and loads assets as packages. 2. Every asset lives in a `.uasset` package with a long name like `/Game/AEC/Montages/AM_Attack_Q`. 3. `CreatePackage` finds or creates the in-memory package; `FPackageName::LongPackageNameToFilename` maps it to the content folder; `SavePackage(Package, Asset, Filename, FSavePackageArgs)` writes it (`Engine/Source/Runtime/CoreUObject/Public/UObject/SavePackage.h`); `LoadObject<T>(nullptr, TEXT("/Path/Pkg.Obj"))` loads by object path. 4. Tools and loading by path. 5. `LoadObject` on the game thread in a hot path (it blocks: load once at start-up, as the director does). 6. Saving without `RF_Standalone` on the asset; passing the package path to `LoadObject` instead of the object path. 7. `UAnimMontage* M = LoadObject<UAnimMontage>(nullptr, TEXT("/Game/AEC/Montages/AM_Attack_Q.AM_Attack_Q"));` 8. The commandlet saves; the test and the director load.

## 17. `ACharacter` subclass with a different mesh class: `FObjectInitializer::SetDefaultSubobjectClass`, `ConstructorHelpers::FObjectFinder`

1. `SetDefaultSubobjectClass<T>(Name)` replaces the class of a default subobject a parent class creates (`Engine/Source/Runtime/CoreUObject/Public/UObject/UObjectGlobals.h:1459`); `FObjectFinder` loads an asset in a constructor. 2. Keep `ACharacter`'s mesh but as a subclass; reference engine assets from C++. 3. The initializer is passed up to `ACharacter`'s constructor, which then creates `ACharacter::MeshComponentName` with the new class; `FObjectFinder` (in a `static`, so once per class) finds the asset while the class default object is built. 4. Swapping engine components for compatible subclasses. 5. To *add* components (use `CreateDefaultSubobject`). 6. A `FObjectFinder` that isn't `static` (it runs per construction); assets referenced only from C++ not being cooked (`DirectoriesToAlwaysCook`). 7. `Super(OI.SetDefaultSubobjectClass<USkeletalMeshComponentBudgeted>(ACharacter::MeshComponentName))` 8. `AAECDemoCharacter`'s mesh is a `USkeletalMeshComponentBudgeted` (budget conditions); its mesh is the tutorial mannequin.

## 18. Tick group and tick prerequisites

1. `PrimaryActorTick.TickGroup` places a tick in the frame; `FTickFunction::AddPrerequisite(Object, TickFunction)` orders two ticks of the same frame. 2. Ordering without a manager. 3. The tick task manager runs a function only after its prerequisites in the same frame. 4. When one tick must see another's result. 5. Long chains across many actors (they serialise the frame). 6. Adding a prerequisite in the constructor (do it in `BeginPlay`, on the instance); assuming same-group ticks run in a fixed order without one. 7. `GetMesh()->AddTickPrerequisiteActor(this);` (`Private/Components/ActorComponent.cpp:2613–2619`: `AddPrerequisite` on the actor's tick, only if both can tick) 8. The character's tick (where "pre" starts run) always precedes its mesh's tick.

## 19. `UTickableWorldSubsystem`

1. A world subsystem that also ticks once per frame (`Public/Subsystems/WorldSubsystem.h`). 2. Per-world services with no actor to place. 3. Created with the world; `OnWorldBeginPlay` runs before actors' `BeginPlay` (UE5-UpdateBudget 01b §3); its `Tick` runs in `FTickableGameObject::TickObjects`, after `TG_PostPhysics` and after timers (`Private/LevelTick.cpp:1816–1821`). `DoesSupportWorldType` limits which worlds create it. 4. A director or a registry with world lifetime. 5. When the tick must be in a specific tick group (use a tick function). 6. Forgetting `GetStatId`; ticking in editor worlds. 7. `virtual bool DoesSupportWorldType(const EWorldType::Type T) const override { return T == EWorldType::Game || T == EWorldType::PIE; }` 8. `UAECBenchSubsystem`; its `Tick` is the "post" start order.

## 20. `SpawnActorDeferred` / `FinishSpawning`

1. Two-step spawn: construct, configure, then run construction scripts and `BeginPlay`. 2. Settings that `BeginPlay` reads must be in place first. 3. The actor exists but hasn't begun play until `FinishSpawning(Transform)`. 4. Configuring components before they register with systems. 5. When nothing reads the settings at `BeginPlay`. 6. Forgetting `FinishSpawning` (a half-made actor). 7. `A* X = World.SpawnActorDeferred<A>(A::StaticClass(), T, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn); /*…*/ X->FinishSpawning(T);` 8. URO, visibility and budget-registration flags are set before `BeginPlay` registers the mesh with URO trackers or the budget allocator.

## 21. `UAnimInstance::Montage_Play` / `Montage_Stop`, `RootMotionMode`

1. Play or stop a montage on an anim instance (`Classes/Animation/AnimInstance.h:626`). 2. The standard way to play one-shot animations. 3. `Montage_Play(Montage, PlayRate)` creates an `FAnimMontageInstance` (the position advances at the next anim update, `01b` B-3); `Montage_Stop(BlendOut, Montage)` interrupts it, after which its notifies no longer fire (`AnimMontage.cpp:2822–2826`). `RootMotionMode` decides where root motion is taken from. 4. Any montage playback. 5. For gameplay timing (`01b #1`). 6. Assuming the montage advanced in the frame it was played; expecting notifies after an interrupt. 7. `AnimInstance->Montage_Play(Montage, 1.3f);` 8. The director's NQ/NB paths. A plain `UAnimInstance` (no graph) is enough to advance montages and fire notifies; the pose isn't needed to measure timing.

## 22. `FTimerManager::SetTimer` with a payload delegate

1. Engine timers (`Public/TimerManager.h`). 2. Delayed calls without a tick. 3. `SetTimer(Handle, FTimerDelegate::CreateUObject(this, &C::F, A, B), Delay, false)` expires at `InternalTime + Delay`; the manager ticks once per frame after `TG_PostPhysics` and fires every expired timer (`LevelTick.cpp:1816`). A rate ≤ 0 only clears the handle's old timer and sets nothing (`Private/TimerManager.cpp:665–673`). 4. The hand-written workaround this project compares against (baseline T, `01b #12`). 5. Thousands of short timers per frame (each has bookkeeping). 6. A zero delay (silently cleared); not clearing timers on interrupt. 7. As above. 8. One timer per key per action; cleared on stop; an open timer window is closed at the stop.

## 23. Console variables from code: `IConsoleManager`, `IConsoleVariable::Set(…, ECVF_SetByCode)`

1. Find and set a cvar by name (`Engine/Source/Runtime/Core/Public/HAL/IConsoleManager.h`). 2. Conditions that engine systems read from cvars. 3. `FindConsoleVariable` returns null if no module registered it. Each value is stored per priority (`ECVF_SetByConstructor` … `ECVF_SetByConsole`, `Engine/Source/Runtime/Core/Public/HAL/IConsoleManager.h:155–187`); the highest set priority wins. `ECVF_SetByTemp` ("for temporarily replacing a variable") with a tag can be removed again with `UnsetAllConsoleVariablesWithTag(Tag, ECVF_SetByTemp)` (`:1378`), which restores whatever lower priority was there. 4. Test harnesses. 5. As gameplay state. 6. Not undoing values (they outlive the world in the editor); undoing by setting the old value at a high priority (it then stays stuck above config and device profiles). 7. `Var->Set(4, ECVF_SetByTemp, Tag); … IConsoleManager::Get().UnsetAllConsoleVariablesWithTag(Tag, ECVF_SetByTemp);` 8. `a.URO.ForceAnimRate` and the `a.Budget.*` cvars, tagged `AECBench`, undone in `Deinitialize`.

## 24. Mesh update settings: `bEnableUpdateRateOptimizations`, `VisibilityBasedAnimTickOption`, `SetCastShadow`, `SetAutoRegisterWithBudgetAllocator`, `DisableMovement`

1. Per-mesh switches for URO and visibility-based ticking (`01b #2, #4`), shadow casting, budget-allocator registration (`Engine/Plugins/Runtime/AnimationBudgetAllocator/Source/AnimationBudgetAllocator/Public/SkeletalMeshComponentBudgeted.h:40`), and `UCharacterMovementComponent::DisableMovement` (movement mode `MOVE_None`). 2. They decide when the animation updates, which is what the baselines measure. 3. Read at registration or each tick; registration with the budget allocator happens in `BeginPlay` and turns URO off (the plugin's `Private/AnimationBudgetAllocator.cpp:1191–1197`). 4. Per condition. 5. Mixing URO and the budget on the same mesh (the allocator switches URO off). 6. Setting them after `BeginPlay`; forgetting that shadows count as "rendered". 7. `Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;` 8. Set by the director per `-AECCond` before `FinishSpawning`; shadows off and movement disabled for every character, in every condition.

## 25. CSV profiler macros and small utilities

`CSV_DEFINE_CATEGORY`, `CSV_EVENT` (BenchStart/BenchEnd), `CSV_CUSTOM_STAT` (fires per frame, published once per frame), `CSV_METADATA` (run description inside the capture), as in UE5-UpdateBudget. `FParse::Value`/`Param` on `FCommandLine::Get()` for `-AEC…` values; `FFileHelper::SaveStringToFile` + `FPaths::ProjectSavedDir()` for the run files; `FCrc::MemCrc32` chained over fire records for the determinism hash; `FPlatformTime::ToSeconds64(FPlatformTime::Cycles64())` for the wall clock (raw notes E0); `FApp::UseFixedTimeStep()` / `GetFixedDeltaTime()` to record the fixed step; `GFrameCounter` minus the frame of `OnWorldBeginPlay` for frame numbers; `FPlatformMisc::RequestExitWithStatus(false, Code)` to end a run with a meaningful exit code.

## 26. `IAnimationBudgetAllocator::Get` / `SetEnabled`

1. `IAnimationBudgetAllocator::Get(UWorld*)` returns the world's budget allocator (`Engine/Plugins/Runtime/AnimationBudgetAllocator/Source/AnimationBudgetAllocator/Public/IAnimationBudgetAllocator.h:18`; one per world, created at world initialisation and deleted at world cleanup, `Private/AnimationBudgetAllocatorModule.cpp:42–58`); `SetEnabled(bool)` switches that allocator on or off (`IAnimationBudgetAllocator.h:73`; `Private/AnimationBudgetAllocator.cpp:1328–1332`). 2. The allocator is created **disabled** (`AnimationBudgetAllocator.cpp:71`) and works only when both `a.Budget.Enabled` (default 0, `AnimationBudgetAllocatorCVars.cpp:7`) and `SetEnabled(true)` are on (`:1191, 1366`); the cvar alone does nothing. 3. `SetEnabled` calls `CheckAndPropagateUpdateStatusChange`, which re-reads the cvar and hands every registered mesh to the allocator's tick-rate control (`:1363–1380`); meshes registered later (deferred to world begin play, `:1414–1425`) are taken over at registration. 4. Budget conditions only. 5. Enabling before the cvar is set (the status check reads the cvar). 6. Setting only the cvar, as the first M1 batch did. 7. `IAnimationBudgetAllocator::Get(GetWorld())->SetEnabled(true);` 8. `UAECBenchSubsystem::PrepareCondition`, before any character exists; nothing to undo, since the allocator lives and dies with the world.
