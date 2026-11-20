# 04 · C++ Fundamentals: AnimEventClock

**Rule:** only the language ideas this project's code actually relies on, each with a tiny example first and then where it appears in our code. This file grows with every step.

**As of Phase 4** the project has no gameplay C++ yet. What exists is the build description (C#), four one-line module files (C++ preprocessor macros), descriptors (JSON), configuration (INI), and tools (batch, Python). Each is explained here once, so `05` can go line by line without repeating basics.

---

## 1. C# classes, inheritance and constructor chaining (Build and Target files)

```csharp
public class Animal            { public Animal(string Name) { /* store Name */ } }
public class Dog : Animal      { public Dog(string Name) : base(Name) { Legs = 4; } public int Legs; }
```
- `: Animal` means *Dog is an Animal*: it gets every field and method of `Animal`.
- `: base(Name)` runs the parent constructor **first**, with that argument, before the body of `Dog`'s constructor.
- **In our code:** `public class AnimEventClockTarget : TargetRules` and `public AnimEventClockTarget(TargetInfo Target) : base(Target)`. UnrealBuildTool (UBT, itself a C# program) compiles these files, creates the object, and reads the fields our constructor set (`Type`, `ExtraModuleNames`, …). The same pattern with `ModuleRules` and `ReadOnlyTargetRules` appears in all four `*.Build.cs` files.

## 2. C# arrays and lists

```csharp
var Names = new string[] { "Core", "Engine" };   // a fixed array literal
List<string> All = new List<string>();  All.Add("Core");  All.AddRange(Names);
```
- **In our code:** `PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" })` and `ExtraModuleNames.Add("AnimEventClock")`. The rule fields are lists; `AddRange` appends many, `Add` appends one.

## 3. The C++ preprocessor: `#include` and macros

```cpp
#include "Modules/ModuleManager.h"      // paste that header's text here before compiling
#define SQUARE(X) ((X) * (X))           // text substitution, done before the compiler sees the code
int Nine = SQUARE(3);                   // becomes ((3) * (3))
```
- `#include` copies a header in, so its declarations are known. Including only what you use keeps builds fast (UE calls this IWYU, "include what you use").
- A **macro** is text substitution. Unreal uses macros to generate repetitive boilerplate that must look the same in every module.
- **In our code:** each module's `.cpp` is one `#include` plus one macro, `IMPLEMENT_MODULE(...)` or `IMPLEMENT_PRIMARY_GAME_MODULE(...)`. The macro expands into the functions the module manager looks for when it loads the module (`06` §1).

## 4. Comments as documentation

```cpp
// A line comment: the compiler ignores it. Say *why*, not *what*.
```
- **In our code:** every file starts with a one-line comment naming the project and the file's role, and the non-obvious lines say why they exist (for example *why* the editor module is a private dependency). That is part of the "docs match code" rule.

## 5. JSON (the `.uproject` and `.uplugin` descriptors)

```json
{ "Name": "AnimEventClock", "Modules": [ { "Name": "AnimEventClock", "Type": "Runtime" } ] }
```
- `{}` is an object (named fields), `[]` is an array (a list), strings use double quotes, and there are **no comments and no trailing commas**. A trailing comma makes the editor refuse to open the project.
- **In our code:** `AnimEventClock.uproject` lists two modules and three plugins; `AnimEventClockCore.uplugin` lists two modules.

## 6. INI files and Unreal's array operators

```ini
[/Script/EngineSettings.GameMapsSettings]   ; section = the config class that reads these keys
GameDefaultMap=/Engine/Maps/Templates/Template_Default
+DirectoriesToAlwaysCook=(Path="/Engine/Foo") ; '+' appends to an array inherited from the engine's Base*.ini
-D3D12TargetedShaderFormats=PCD3D_SM5          ; '-' removes a value the engine's defaults added
```
- A section names the **class** that loads the keys (`/Script/<Module>.<Class>`), or a special section such as `[SystemSettings]` (console variables).
- Our `Default*.ini` files are layered **on top of** the engine's `Base*.ini`; `+` and `-` edit inherited arrays instead of replacing them.
- `;` starts a comment.
- **In our code:** all three files in `AnimEventClock\Config\` (`05` A9–A11).

## 7. Batch files (`Tools\*.bat`)

```bat
setlocal                         & rem variables set below vanish when the script ends
set "ROOT=%~dp0"                 & rem %~dp0 = this script's own folder, with a trailing backslash
if "%~1"=="" set "MODE=packaged" & rem %~1 = first argument without its quotes
call other.bat                   & rem run another batch file and come back
exit /b %ERRORLEVEL%             & rem return the last program's exit code to the caller
```
- `set "NAME=value"` (quotes around the whole assignment) keeps trailing spaces and special characters out of the value.
- `goto label` / `:label` jump; `shift` moves `%2` into `%1`, which is how `smoke_game.bat` collects its extra arguments.
- Batch files must have **CRLF** line endings: `cmd.exe` can mis-read labels in LF-only files. `.gitattributes` enforces `*.bat eol=crlf`.
- **In our code:** `build.bat`, `package.bat`, `smoke_game.bat` (`05` A12).

## 8. Python `ctypes` and `struct` (`Tools\cpu_affinity.py`)

```python
import ctypes, struct
kernel32 = ctypes.windll.kernel32                     # call Windows API functions from Python
size, = struct.unpack_from("<I", raw_bytes, offset)  # read a little-endian 32-bit integer at offset
```
- `ctypes` calls C functions in Windows DLLs. Declaring `argtypes`/`restype` tells Python the C types, so 64-bit handles are not truncated.
- `struct.unpack_from` reads binary records (here, the `SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX` records Windows returns).
- **In our code:** `performance_core_mask()`, `pin_self()` and `applied_mask()`, copied from UE5-UpdateBudget (`05` A13).

---

## Added in steps S1 and S2

## 9. `enum class` with an underlying type

```cpp
enum class EAECEventPhase : uint8 { Instant, WindowBegin, WindowEnd };   // scoped: EAECEventPhase::Instant
```
- Scoped names, no silent conversion to `int`, one byte of storage.
- **In our code:** `EAECEventPhase` (reflected with `UENUM`), and the director's private `EPath` and `ERunPhase` (not reflected: nothing outside the class needs them).

## 10. Function-local `static`

```cpp
FAECMarkerFiredByAnimation& OnMarkerFiredByAnimation() { static FAECMarkerFiredByAnimation Delegate; return Delegate; }
```
- Built the first time the function runs, then reused; C++ guarantees the construction happens once, even with threads.
- It avoids the "static initialisation order" problem: a global in one DLL may be used by another before it is constructed.
- **In our code:** the comparison hook, the demo-montage spec table (`GetMontageSpecs`), and the mannequin `FObjectFinder` in the character's constructor.

## 11. `inline` variables and namespaces

```cpp
namespace AECDemo { inline const TCHAR* const MontageFolder = TEXT("/Game/AEC/Montages"); }
```
- A namespace groups names without a class. `inline` (C++17) lets a variable be defined in a header that many `.cpp` files include, with one shared definition.
- `const TCHAR* const`: a constant pointer to constant characters.
- **In our code:** `AECDemo::` (the demo content), `AECBench::` (the director's tuning constants), `AECDemoCharacter::` (the mesh path).

## 12. Aggregates and brace initialisation

```cpp
struct FMarkerSpec { const TCHAR* Name; float StartFraction; float DurationFraction; };
const FMarkerSpec Hit = { TEXT("Hit1"), 0.36f, 0.f };          // members in declaration order
Info.Keys.Add({ Name, EAECEventPhase::Instant, Time });         // a temporary FKey, built in place
```
- A struct with public members and no constructors is an *aggregate*: braces fill its members in order. Default member values (`float Position = 0.f;`) keep it an aggregate.
- **In our code:** the spec tables and the director's `FKey`, `FAction`, `FFire`, `FCharacterState`.

## 13. Range-for, also over a brace list

```cpp
for (const bool bBranchingPoint : { false, true }) { /* both variants */ }
```
- **In our code:** the commandlet and the asset test build and check both montage variants this way.

## 14. Lambdas as predicates

```cpp
const int32 Key = Keys.IndexOfByPredicate([EventName, EventPhase](const FKey& K) { return K.Name == EventName && K.Phase == EventPhase; });
```
- The capture list copies `EventName` and `EventPhase` into the lambda; the lambda is called once per element.
- **In our code:** finding a key by name and phase, sorting keys by position (`StableSort`), closing a window by name.

## 15. Overriding engine virtuals and calling `Super`

```cpp
void UAECEventNotify::Notify(USkeletalMeshComponent* Mesh, UAnimSequenceBase* Anim, const FAnimNotifyEventReference& Ref)
{
	Super::Notify(Mesh, Anim, Ref);   // keep the base behaviour, then add ours
	/* … */
}
```
- `Super` is Unreal's alias for the parent class (generated by `GENERATED_BODY`). Forgetting it silently drops what the base did.
- **In our code:** every engine override (`Notify`, `NotifyBegin`/`End`, `BeginPlay`, `Tick`, `OnWorldBeginPlay`, `Deinitialize`).

## 16. Avoiding name shadowing

```cpp
void Handle(EAECEventPhase EventPhase);   // not "Phase": the class already has a member called Phase
```
- A parameter or local with the same name as a member hides the member. Unreal builds treat that as an error, and it is a classic source of bugs.
- **In our code:** the hook handler's parameter is `EventPhase` because the director has a `Phase` (its run phase).
