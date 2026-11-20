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
