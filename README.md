# P06 · AnimEventClock: gameplay events on the gameplay clock

**Author:** Krish Gangani · [github.com/KrishGangani](https://github.com/KrishGangani)

**Domain:** Animation–gameplay sync · multiplayer roles · **Difficulty:** ★★★★☆ · **Engine:** UE 5.8.4

> **Status: in progress. There are no results yet.** Phases 1–4 are done: the problem analysis, the research verified in the 5.8.4 source, the final architecture, and an empty UE project and plugin that build with 0 warnings and run (packaged, and as a dedicated server). The success criteria are **targets fixed before any code exists, not results**. Every number will come from controlled before/after runs, each traceable to a raw benchmark batch. Sections marked *(pending)* are filled in only from real measurements, in the phase named.

| Phase | 1 Problem | 2 Research | 3 Architecture | 4 Foundation | 5 Implementation | 6 Explanation | 7 Testing | 8 Profiling | 9 Review | 10 Portfolio |
|---|---|---|---|---|---|---|---|---|---|---|
| State | ✓ | ✓ | ✓ final | ✓ | next (S1 of 8) | ongoing | — | — | — | — |

---

## Problem statement

Hit windows, spawn points and ability commit points usually live in **anim notifies**, and a notify fires only in a frame in which its mesh's animation updates. Update-rate optimisation and the Animation Budget Allocator make such events late and bunched, "only tick when rendered" stops them off-screen and on dedicated servers, a server advances a remote player's montage once per client move, and client-side position corrections can skip or repeat them. BranchingPoint notifies don't help: they are precise inside an update frame but don't create update frames. All of this was read in the 5.8.4 source; **how large each effect is will be measured before anything is built**. Details: [Explanation/01_Problem_Analysis.md](Explanation/01_Problem_Analysis.md) and [01b_Research_Findings.md](Explanation/01b_Research_Findings.md).

**What Phase 2 changed in this statement:**
- The effects are **conditional**: with engine defaults a montage notify is within one frame of its time. The project measures them under the settings large projects enable, and claims nothing for a condition the baselines don't fail (the "problem gate", `01` §6.4).
- The 5.6.1 server bug (UE-308913, repeated notify-state begin/end on a dedicated server) **cannot be confirmed open in 5.8.4**: the code path was restructured in 5.8. It is measured here, not assumed.
- "Early in look-ahead mode" holds only for "root motion from everything" meshes.

## Engineering approach

Keep notifies as the **authoring** tool, so designers place events in the montage editor exactly as today. At run time, fire them from a **playhead driven by the gameplay clock** (a latency-compensated server time on clients), built from the montage's own notify data. Replicate each action's **start time** and an action id, and give every event a stable id, so each role fires it **exactly once**, whatever the animation does. The montage keeps playing as the visual layer and is only corrected when it drifts far. Compared against Queued and BranchingPoint notifies, hand-written timers and GAS montage replication. Details: [Explanation/02_System_Architecture.md](Explanation/02_System_Architecture.md).

## Main Unreal Engine technologies

`UAnimMontage`, notifies (Queued / BranchingPoint), URO (`FAnimUpdateRateParameters`), the Animation Budget Allocator, `VisibilityBasedAnimTickOption`, component replication and RPCs, character server moves, `RepRootMotion`, GAS montage replication (baseline), an editor validator and a commandlet, net emulation (`-PktLag`), the CSV profiler.

## Success criteria (targets, defined before implementation)

The full table is in [01_Problem_Analysis.md §6.4](Explanation/01_Problem_Analysis.md). In short:
- **SC1** on the authority, every event fires in [0, 1 frame) of its due time, in every condition;
- **SC2** 0 missed and 0 duplicate events per 1,000 on every role, at 0 / 100 / 200 ms;
- **SC3–SC4** on clients, p99 ≤ 1 client frame + 1 server frame for events due after the action is known; clock sync within one server frame;
- **SC5** ≤ 2 µs per dispatched event; **SC6** the animation still follows; **SC7** no new authoring workflow.

## Existing work it is compared against

BranchingPoint notifies, `a.EnableQueuedAnimEventsOnServer` (and 5.8's new `a.Notifies.DispatchPolicy`), `RepRootMotion`, GAS montage replication, Mover's sim-driven montages (Experimental), Motion Warping windows, hand-written timers, and the Able Ability System.

**The gap it targets (medium, lowered from "strong" in Phase 2):** events authored in the montage, fired on the gameplay clock on every network role, each exactly once, with a latency-compensated start on clients, as a drop-in for `ACharacter`/GAS projects, **with published measurements** of the failure modes. Mover, Able and timers each already cover part of this idea; see [01b §5](Explanation/01b_Research_Findings.md).

## Before / after comparison

*(pending, Phase 8)*. Configurations NQ (Queued), NB (BranchingPoint), T (timers), G (GAS) and AEC on the same seeded workload, across URO, budget and visibility conditions at 30 / 60 / 144 fps, and across server and client roles at 0 / 100 / 200 ms.

## Benchmark results

*(pending, Phases 5 and 8)*. The baselines are captured first (steps S3–S4), before the event clock exists. Raw data in [Benchmarks/](Benchmarks/README.md).

## Recorded demonstration

*(pending, Phase 10)*. The same NPCs with two hit-window bars each (notify path vs clock), under URO and on a client at 200 ms. Saved in `Media/`.

## Why studios care

Animation appears in 11 of 57 reviewed gameplay postings: Respawn (Lead Animation Engineer), The Coalition (Senior Gameplay Animation Engineer, UE5), Hangar 13 (GAS + animation), Ubisoft Montréal (animation programmer on a UE multiplayer team). Hitman: Absolution's animation-vs-steering authority trade-off is the same tension. Sources: [Explanation/Research/01_job_market.md](Explanation/Research/01_job_market.md), [02_technical_challenges.md](Explanation/Research/02_technical_challenges.md).

## Source code

The UE project is in [AnimEventClock/](AnimEventClock/) (UE 5.8.4, C++):
- the reusable system is the plugin `AnimEventClockCore` (runtime module + editor module for validation);
- the demo and benchmark code is in the game module `AnimEventClock` (+ an editor module for the demo-asset commandlet).

**Assets:** the demo character is the engine's tutorial mannequin, **referenced by path and never copied** into this repository (it is Starter Content under the Unreal Engine EULA). The project's own montages are generated by a commandlet. See [01b #13](Explanation/01b_Research_Findings.md) and [the licence notes](Explanation/Phase2_Raw_Notes/D_existing_work_and_assets.md).

## Documentation

| File | Contents |
|---|---|
| [00_Project_Overview](Explanation/00_Project_Overview.md) | Identity, demonstration, presentation, industry relevance |
| [01_Problem_Analysis](Explanation/01_Problem_Analysis.md) | Problem, existing work, root-cause hypotheses, workload, configurations, metrics, success criteria, measurement plan |
| [01b_Research_Findings](Explanation/01b_Research_Findings.md) | Phase 2: verified engine behaviour (UE 5.8.4) and what it changed in the design |
| [02_System_Architecture](Explanation/02_System_Architecture.md) | Final v1.0: modules, classes, the playhead, replication and clock sync, per-frame flows, safety rules, rejected alternatives |
| [03_Implementation_Roadmap](Explanation/03_Implementation_Roadmap.md) | The small, ordered build steps; baselines captured before the event clock |
| [04](Explanation/04_CPP_Fundamentals.md) · [05](Explanation/05_Line_By_Line_Code_Explanation.md) · [06](Explanation/06_Unreal_APIs_And_Macros.md) | Language fundamentals, line-by-line code, APIs and macros. Written step by step with the code (current to Phase 4) |
| [09](Explanation/09_Testing_And_Benchmarks.md) | Every check and benchmark batch so far, with its job or batch name |
| [07](Explanation/07_Common_Mistakes.md) · [08](Explanation/08_Debugging_And_Profiling.md) · [10](Explanation/10_Interview_Questions.md) · [11](Explanation/11_Optimization_Decisions.md) · [12](Explanation/12_Future_Scalability.md) | Common mistakes, debugging and profiling, interview questions, optimisation decisions, future scalability. Filled in during Phases 5–10 |
| [Phase2_Raw_Notes/](Explanation/Phase2_Raw_Notes/README.md) | The Phase 2 audit trail, with every quote |
| [Research/](Explanation/Research/) | The portfolio-wide research this project draws on: job market, technical challenges, existing solutions, local engine verification |

## Folder layout

```text
P06_AnimEventClock\
├── README.md        ← this page (the project's portfolio page)
├── Explanation\     ← 00–12, raw research notes, portfolio research
├── Benchmarks\      ← Raw\ + Summaries\
├── Media\           ← diagrams, screenshots, demo video
├── Tools\           ← build, package, smoke, affinity (and later run and stats) scripts
└── AnimEventClock\  ← the Unreal Engine project
```
