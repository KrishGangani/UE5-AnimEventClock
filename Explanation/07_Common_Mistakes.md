# 07 · Common Mistakes: AnimEventClock

**Status:** stub. Filled in **Phase 5, step by step** (every step that adds C++ adds its rows here), then consolidated in Phase 9. No system code exists yet.

**Will contain:** for every module, the mistakes that are easy to make here, how to spot them, how to prevent them and how to debug them. Mistakes a code review actually caught in this project will be marked as such.

**Already known from Phase 4** (foundation, no C++ yet):

| Mistake | How to spot it | How to prevent it | How to debug it |
|---|---|---|---|
| Setting only `GameDefaultMap` in a project that runs dedicated servers (made here, check F-7a) | The server log says `Bringing World /Engine/Maps/Entry.Entry up for play`; clients land in an empty world | Set `ServerDefaultMap` too (`06` §7) | Grep the server log for `LoadMap:` |
| Typing an editor-only module `Runtime`, or adding it to the game target | The game target fails to build once the module uses `UnrealEd` | `"Type": "Editor"` and only the editor target's `ExtraModuleNames` (`06` §3–4) | The UBT error names the editor-only module |
| Leaving a default-on plugin that writes secrets into project config (`AndroidFileServer`'s random `SecurityToken` in `DefaultEngine.ini`; made here, first Phase 4 job) | A `[/Script/AndroidFileServerEditor.AndroidFileServerRuntimeSettings]` block appears in `git diff` after an editor run | Disable the plugin in the `.uproject` and delete the block (`05` A1) | `git diff Config/` after every editor or cook job |
| Copying a smoke-script flag without checking it exists (`-ExitAfterSeconds`) | Nothing: an unknown flag is silently ignored | Grep the engine source for the parameter before relying on it | `grep -r "ExitAfterSeconds" Engine/Source` finds nothing in 5.8.4 |

**Planned topics** (from 02 §8 and 01b): relying on notifies for gameplay timing under URO, the budget allocator or visibility-based ticking; using BranchingPoint as a timing fix; using `GetServerWorldTimeSeconds()` as if it were latency-compensated; correcting montages with `SetPosition` and expecting notifies to fire; firing events again after a client resync; destroying the owner inside an event handler; calling `Montage_JumpToSection` on the anim instance and bypassing the clock; placing markers on slot-track sequences.
