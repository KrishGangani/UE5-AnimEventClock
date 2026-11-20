# P06 · AnimEventClock: gameplay events on the gameplay clock

**Domain:** Animation–gameplay sync · multiplayer roles · **Difficulty:** ★★★★☆ · **Engine:** UE 5.8.4

> **Status: planned, not started.** The projects in this portfolio are built one at a time, and this one starts after the projects before it are complete. Everything below is the plan. The success criteria are **targets fixed before any code exists, not results**. Real numbers will only come from controlled before/after runs, each traceable to a raw benchmark batch.

## The problem

Hit windows and ability commit points live in anim notifies. Those fire late or bunched under update-rate optimisation and budgets, can fire early in look-ahead mode, depend on visibility, and have an open server-side bug in 5.6.1 (UE-308913)

## Proposed solution

Bake authored montage events into data at edit time. Fire them from a **gameplay-time event track** that is independent of animation update rate, visibility and network role. Animation stays the visual layer

## Main Unreal Engine technologies

AnimMontage, notifies (Queued/BranchingPoint), URO, Animation Budget Allocator, `RepRootMotion`, GAS montage replication (baseline), editor module for baking, net emulation

## Success criteria (targets, defined before implementation)

Timing error per event (median/p99 ms) vs authored time; missed/duplicate events per 1,000 across URO rates, budgets, 30/60/144 fps, server/client at 0/100/200 ms; overhead µs per event

## Existing work it must be compared against

Branching-point notifies, `a.EnableQueuedAnimEventsOnServer`, `RepRootMotion`, GAS and Mover montage replication, Motion Warping

**The gap it targets:** A gameplay-clock event track independent of anim rate, visibility and network role (gap strength: Strong).

**Main risk:** Must be measured first. The URO late/early behaviour is from code reading so far

## Why studios care

Animation appears in 11/57 postings; Respawn Lead Animation Engineer; The Coalition gameplay animation; Hangar 13 GAS + animation; Hitman's authority trade-off

## Method

Every project in the portfolio follows the same ten phases: problem analysis → research → architecture → foundation → implementation → explanation → testing → profiling → review → portfolio. Each phase is documented in `Explanation/` (files 00–12, including a line-by-line code walkthrough), and every claimed improvement must be measured against the existing solutions above on the same workload. Engine: Unreal Engine 5.8.4, C++.

## Folder layout

```text
P06_AnimEventClock\
├── README.md        ← this page (becomes the project's portfolio page)
├── Explanation\     ← 00–12, created when the project starts
├── Benchmarks\      ← Raw\ + Summaries\
├── Media\           ← diagrams, screenshots, demo video
├── Tools\           ← analysis and run scripts
└── AnimEventClock\ ← the Unreal Engine project (created in its Phase 4)
```
