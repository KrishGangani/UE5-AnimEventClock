> **Raw Phase 2 notes (D: existing work, assets and licence), 2026-11-21.** Web sources were opened on this date unless marked "snippet only". Engine facts cite the 5.8.4 launcher source. The claims the design depends on are restated in [../01b_Research_Findings.md](../01b_Research_Findings.md).

# D. What already exists, and what this project may use

## D1. Engine features (each read in the 5.8.4 source; details in notes A–C)

| Feature | What it does | What it leaves open |
|---|---|---|
| BranchingPoint notifies | Fire synchronously at their exact montage position, inside the game-thread montage advance (A2) | Still fire only in a frame in which the mesh's animation updates; URO, the budget allocator and visibility settings decide *which* frame (B1–B4) |
| `bTriggerOnDedicatedServer`, `NotifyFilterType`/`NotifyFilterLOD`, `NotifyTriggerChance`, `TriggerWeightThreshold` | Per-notify filters (A3) | Filters decide *whether*, not *when* |
| `a.EnableQueuedAnimEventsOnServer`, 5.8's `a.Notifies.DispatchPolicy` | Make queued notifies fire on servers, and fire once per move for remote players (C3) | Timing still follows the client's moves (C2) |
| URO "human controlled" exemption | Player-controlled characters always update at rate 1 to keep gameplay events on time (B2) | NPCs on a server and remote players on a client are not exempt |
| `RepRootMotion` | Corrects a simulated proxy's root-motion **location** (C4) | Carries no start time; does not drive notifies |
| GAS montage replication | Starts the montage on simulated proxies and corrects position beyond 0.1 s (C4) | Corrections can skip BranchingPoint notifies or repeat notifies; no start time is replicated |
| Mover sim-driven montages (Experimental) | The network simulation owns montage position (C5) | Experimental; Mover users only; notifies still fire on the next anim update |
| `FTimerManager` timers | A common hand-written workaround: start timers at the authored offsets when the montage starts. Timers tick once per frame after `TG_PostPhysics` (`Private/LevelTick.cpp:1816`) and do not depend on the mesh | The offsets are copied by hand from the montage (two sources of truth); play rate, section jumps, interruption and network start time must all be handled by hand per ability |

## D2. Community reports

| Source | What it says | Status |
|---|---|---|
| "URO and AnimNotifies", UE 4.27, 2022-07-19: https://forums.unrealengine.com/t/uro-and-animnotifies/608584 | With URO for non-rendered actors "anim notify timing seems to be messed up" (footsteps). "Branching point or queued ticktype doesn't make a difference." No replies | Opened |
| "UE 5.6.1 Why is NotifyBegin/NotifyEnd constantly being called in my anim notify state on the server?", 2025-08-28: https://forums.unrealengine.com/t/ue-5-6-1-why-is-notifybegin-notifyend-constantly-being-called-in-my-anim-notify-state-on-the-server/2651617 | Dedicated server + 1 client; begin/end "continually called only on the dedicated server instance for the duration of the montage"; "not an issue in UE 5.5.4"; call path in C3; "Already reported … (UE-308913)". Workaround (2025-10-25): client-authoritative notify states plus a server RPC | Opened |
| Epic issue tracker, UE-308913: https://issues.unrealengine.com/issue/UE-308913 | — | **HTTP 403** to this research: status **[unverified]** |
| Epic docs, "Animation Notifies" (page labelled UE 5.9 when opened): https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-notifies-in-unreal-engine | Queued: "less precise, asynchronous, but more performant"; "Select this if you are okay with minor frame inaccuracy". Branching Point: "more precise, synchronous, and higher-cost"; "ensure your Notify executes with maximum frame accuracy". Trigger Weight Threshold default 0.00001 | Opened |
| Portfolio research [research/03](../Research/03_existing_solutions_gap_analysis.md) §6 | Further forum snippets: queued notify states firing twice, frame-rate-dependent notify-state ticks, high play rate stepping past windows | Snippet only; not relied on |

## D3. Plugins and frameworks

| Work | What it does | Source and status |
|---|---|---|
| **Able Ability System** (Extra Life Studios, Marketplace/Fab since 2017) | Abilities are tasks laid out on a timeline in an Ability Editor; "Play Animation" is one task among others (sound, collision query, damage); "Fully Network Supported"; "the Server is always authoritative and uses its own copy of the Ability for all validation" | https://extralifestudios.atlassian.net/wiki/spaces/ABLE/pages/2195469/Overview (opened). How its clock relates to animation update rate, and how it syncs time over the network, is **not documented there [unverified]** |
| GAS `UAbilityTask_PlayMontageAndWait` + notify-sent gameplay events | The standard GAS pattern: gameplay events are sent from anim notifies | Inherits every notify behaviour above |
| The portfolio research search for a plugin that fires montage events independent of anim update rate | "No plugin found"; only forum workarounds (socket colliders, branching points, always-tick meshes) | [research/03](../Research/03_existing_solutions_gap_analysis.md) §6, snippet-level search |

**Honest reading.** The idea "drive gameplay from an ability timeline, not from animation" already exists (Able, hand-written timers, and Mover's sim-driven montage position). What the reviewed public work does **not** show is: events authored in the montage itself (one source of truth) but fired from the gameplay clock on every role, each exactly once, with a latency-compensated start time on clients, as a drop-in for `ACharacter`/GAS projects, **with published measurements** of the failure modes it fixes. Studio-internal systems are not public, and a web search cannot prove absence. Gap strength: **medium**, down from the roadmap's "strong" (01b §1).

## D4. Assets: what ships with the engine install

| Asset | Path | Kind (checked by the asset's own references) |
|---|---|---|
| Tutorial mannequin | `/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP` (689 KB) | `SkeletalMesh` on `TutorialTPP_Skeleton` |
| Idle, walk | `…/Character/Tutorial_Idle` (551 KB), `…/Tutorial_Walk_Fwd` (189 KB) | `AnimSequence` on `TutorialTPP_Skeleton` |
| Tutorial anim blueprint | `…/Character/TutorialTPP_AnimBlueprint` | `AnimBlueprint`; contains no "Slot" name, so it would not show montage poses |
| Skeletal cube | `/Engine/EngineMeshes/SkeletalCube` (+ `_Skeleton`) | `SkeletalMesh`, 16 KB, no animation |
| Template mannequins | `Templates/TemplateResources/High/Characters/Content/Mannequins/` | Manny/Quinn meshes, `MM_*` sequences, one montage (`MM_Pistol_Fire_Montage`); **126 MB** in total (31 MB of meshes) |

Paths are under `C:\Program Files\Epic Games\UE_5.8\` (`Engine/Content/…` for `/Engine/…`). Sizes from the file system; kinds from the package's own import names (a `strings` scan, no editor needed).

## D5. Licence: what may go into a public repository

Source: the Unreal Engine EULA, https://cdn2.unrealengine.com/unreal-engine-end-user-license-agreement-d2812e10c642.pdf (PDF metadata date 2022-03-24; the HTML page `unrealengine.com/eula/unreal` returned HTTP 403 to this research, so whether a newer revision exists is **[unverified]**). Quotes:

- §1: the Licensed Technology "includes Engine Code, Examples, and Starter Content."
- "'Examples' means the code, artwork, or other content made available by us in the **Samples and Templates folders** in the install directory."
- "'Starter Content' means the code, artwork, or other content made available by us and installed to your computer when you install Unreal Engine, **excluding Examples**."
- §3: "Any Product that you Distribute that incorporates Licensed Technology must incorporate the Licensed Technology only in object code and only as an inseparable part of the Product."
- §3(a)(i): Non-Engine Products must "not include Starter Content in source format".
- §4: "You may only Distribute Licensed Technology (including as modified by you) outside of a Product as expressly permitted by this Section 4." §4(b): "You may Distribute Examples (including as modified by you) in Source Code or object code to any third party."

**Reading (not legal advice):**
- `Engine/Content/…` (the tutorial mannequin, the skeletal cube) is **Starter Content**. Its `.uasset` files must **not** be committed to a public repository. Referencing it by path is fine: anyone who clones the repository needs UE 5.8 installed, which ships the same files. A packaged build contains it cooked, as part of the Product; packaged builds are never committed here.
- `Templates/…` content (Manny, Quinn) is **Examples** and could be redistributed, but it is 126 MB of binaries.
- Assets this project creates itself (montages, data) are ours, even when they *reference* engine animations by path.

**Decision (01b §1, #13):** reference the tutorial mannequin and its two sequences by path; create this project's own montages (with the authored events) by an editor commandlet, through the queue. Nothing from the engine install is copied into the repository.
