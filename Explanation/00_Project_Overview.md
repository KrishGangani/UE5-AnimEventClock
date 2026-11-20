# 00 · Project Overview: AnimEventClock

> *"This was the problem. This was my investigation. This was my solution. These were my technical decisions. This was how I measured the results."*

**Status (2026-11-21):** Phases 1–4 are done (problem analysis, verified research in `01b`, final architecture v1.0 in `02`, and the foundation: an empty UE project and plugin that build and run). **No system code exists yet, and no results exist yet.**

---

## A. Identity

| | |
|---|---|
| **Name** | AnimEventClock: gameplay events on the gameplay clock |
| **Technical category** | Animation–gameplay synchronisation across network roles |
| **Main objective** | Fire the gameplay events authored in an animation montage (hit windows, spawn points, commit points) at their authored gameplay time, **exactly once on every network role**, no matter how often, how late or whether the mesh's animation updates |
| **Difficulty** | ★★★★☆ Advanced |
| **Core UE technologies** | `UAnimMontage`, `UAnimNotify` / `UAnimNotifyState` (Queued and BranchingPoint), update-rate optimisation (`FAnimUpdateRateParameters`), the Animation Budget Allocator, `VisibilityBasedAnimTickOption`, `UActorComponent` replication and RPCs, `ACharacter` / `UCharacterMovementComponent` server moves, GAS montage replication (baseline), `UEditorValidatorBase`, commandlets, net emulation (`-PktLag`), the CSV profiler |
| **Relation to existing work** | BranchingPoint notifies, `a.EnableQueuedAnimEventsOnServer`, `RepRootMotion`, GAS montage replication, Mover's sim-driven montages (Experimental), hand-written timers and the Able Ability System each cover part of the problem. The gap is **medium**, not strong: see `01` §3 and `01b #11` |

## B–D. Problem, solution, architecture

- Problem, root-cause hypotheses, success criteria and the measurement plan: **[01_Problem_Analysis.md](01_Problem_Analysis.md)**
- What the engine actually does, verified in the 5.8.4 source, and what it changed: **[01b_Research_Findings.md](01b_Research_Findings.md)**
- Solution and architecture (final v1.0): **[02_System_Architecture.md](02_System_Architecture.md)**
- Build order, with baselines first: **[03_Implementation_Roadmap.md](03_Implementation_Roadmap.md)**

**In one paragraph.** A montage notify fires only in a frame in which its mesh's animation updates, so update-rate optimisation, the budget allocator, "tick only when rendered", the server's per-client-move pose ticks and client-side position corrections all move, bunch, drop or repeat gameplay events. AnimEventClock keeps the notifies as the **authoring** tool, but fires the events from a playhead driven by the **gameplay clock** (server time on clients), replicates each action's start time and gives every event a stable id, so it fires once, on time, everywhere. The montage keeps playing as the visual layer.

## E. Technical demonstration

**What the viewer sees**
- A row of identical NPCs swinging the same attack montage. Each has a hit-window bar above its head: grey when closed, red when the gameplay system thinks the window is open.
- Two bars per NPC: **top** = the notify path (what most games do), **bottom** = AnimEventClock.
- An overlay with the active condition (URO rate, budget, visibility option, network role, latency) and a live count of late, early, missed and duplicate events per path.

**Before** (notify path)
- With URO forced to rate 4, the far NPCs' top bars open late and in jerks; some open and close in the same frame.
- With the camera turned away and "only tick when rendered", the top bars stop entirely.
- On a client at 200 ms, the top bars open late, and after a GAS position correction a window opens twice.

**After** (AnimEventClock)
- The bottom bars open and close at the same moment on every NPC, on screen or not, on the server and on the client.

**Recorded material:**
- a side-by-side video of the notify path vs the clock on the same NPCs, under URO and on a client at 200 ms;
- timing-error distributions (median, p95, p99, min, max) per condition and role;
- missed/duplicate counts per 1,000 events;
- the overhead per event, from packaged Development runs.

## G. Portfolio presentation (produced in Phase 10)

1. Problem statement, one paragraph (from `01` §1).
2. The investigation: hypotheses H1–H8, what the source said, and what the baseline captures showed for each, **including conditions that turned out not to be a problem on 5.8.4** (the problem gate, `01` §6.4).
3. The architecture diagram (`02` §5) and the per-frame flows (`02` §6).
4. The key decisions and rejected alternatives: no edit-time bake, the clock-sync component, the replicated start time, "playhead only moves forward".
5. Before/after results against **all** baselines (Queued, BranchingPoint, timers, GAS), with the full environment and method.
6. What didn't work or didn't improve, stated plainly.
7. Video, source repository and the complete `Explanation/` folder.

## H. Industry relevance

**Skills demonstrated:**
- engine-level understanding of the animation update path: montage advance, notify queues, URO, the budget allocator, visibility-based ticking;
- multiplayer engineering: server moves, simulated vs autonomous proxies, replication of intent instead of state, clock synchronisation, emulated latency;
- correctness metrics (missed/duplicate per 1,000, timing error distributions), not just frame time;
- editor tooling (commandlet, data validation);
- honest scoping against existing engine features.

**Studios and roles that ask for this:**
- Respawn, Lead Animation Engineer (Apex): deep C++, blending, procedural animation, performance-sensitive systems;
- The Coalition, Senior Gameplay Animation Engineer (Gears, UE5): gameplay and animation systems, "Analyze performance and memory issues in core gameplay systems", multiplayer;
- Hangar 13, Senior Gameplay & Animation Engineer: GAS and UE animation;
- Ubisoft Montréal, Animation Programmer on a UE multiplayer team.

Sources: [research/01](Research/01_job_market.md) (EA3, MS1, K2, U6); animation appears in 11 of 57 reviewed postings.

**Interview questions it prepares you for:**
- "What's the difference between Queued and BranchingPoint notifies? When does each one actually fire?"
- "Your NPC's hit window opens late on the dedicated server but not in PIE. Why?"
- "What does URO do to anim notifies? What does the Animation Budget Allocator do to them?"
- "How does the server advance a player character's animation? Why can that make server-side notifies jitter?"
- "Why is `GetServerWorldTimeSeconds()` on a client not the server's current time? How would you estimate it better?"
- "How do you guarantee an event fires exactly once on every machine when animation positions get corrected?"
- "When would you *not* drive gameplay from animation? When would you?"
- "How did you prove the problem existed before you fixed it?"
