# 11 · Optimization Decisions: AnimEventClock

**Status:** stub. Filled in **Phase 8**. No system code exists yet.

**Will contain:** every optimisation considered (for example a per-montage timeline cache, a native delegate beside the Blueprint one, ticking all clocks from one place instead of per component), the measurement that motivated it, the change, the before/after numbers from the M4 overhead batch, and what was rejected. Phase 3 deliberately deferred all three until data exists ([02](02_System_Architecture.md) §2, §4.1, §11).

**Optional Phase 8 item (owner decision, 2026-11-21; S4: condition not met, so not needed):** reproduce UE-308913 on the installed 5.6.1 engine **only if** the 5.8.4 server measurements (S4) show no server-side notify problem and the 5.6.1 evidence is needed to explain the history. It needs a 5.6 build of the harness and extra queue time, so it is not planned by default.

**Phase 8 measurement items added by S3** (baselines that S3 did not run, with the reason):
- **Budget allocator, off-screen cap and real budget.** The off-screen cap (more than `a.Budget.MaxTickedOffsreen` meshes that "tick even if not rendered") only applies to meshes the game marks that way through `SetComponentSignificance` (`AnimationBudgetAllocator.cpp:150–163`): a game policy, not an engine default, so the harness would first need a significance policy. The real (unforced) budget depends on measured cost and is not deterministic; it is reported with its run-to-run range when it runs.
- **Root motion from everything with an Anim Blueprint, and on a moving character.** The demo characters use a graph-less `UAnimInstance` and `MOVE_None`. The lost Queued instant notifies of rendered meshes (09 S3-f) follow a code path that does not depend on either (the root-motion update is immediate for every anim instance, `AnimInstance.cpp:1028–1029`), but one run with a compiled Anim Blueprint and a walking character would confirm it; the open question of why non-rendered meshes keep their notifies is re-read in Phase 6.

**Phase 8 candidates found in code review** (not changed until measured):
- **The timeline's entry scan.** Each section entry scans the keys before the entry point for windows that span it (`FAECMontageTimeline::Advance`), so a long catch-up through a short loop costs O(keys before the loop start) per pass. A per-section list of spanning begins, built in `Finalize`, would make it O(spanning windows). Measured first with the late-join runs of S7 / M3 (S5 code review).
