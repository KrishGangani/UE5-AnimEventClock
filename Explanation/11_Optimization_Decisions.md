# 11 · Optimization Decisions: AnimEventClock

**Status:** stub. Filled in **Phase 8**. No system code exists yet.

**Will contain:** every optimisation considered (for example a per-montage timeline cache, a native delegate beside the Blueprint one, ticking all clocks from one place instead of per component), the measurement that motivated it, the change, the before/after numbers from the M4 overhead batch, and what was rejected. Phase 3 deliberately deferred all three until data exists ([02](02_System_Architecture.md) §2, §4.1, §11).

**Optional Phase 8 item (owner decision, 2026-11-21):** reproduce UE-308913 on the installed 5.6.1 engine **only if** the 5.8.4 server measurements (S4) show no server-side notify problem and the 5.6.1 evidence is needed to explain the history. It needs a 5.6 build of the harness and extra queue time, so it is not planned by default.
