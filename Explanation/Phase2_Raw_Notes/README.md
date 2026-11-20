# Phase 2 raw notes

These are the working notes behind [01b_Research_Findings.md](../01b_Research_Findings.md). They are kept so every statement in the findings can be traced back to the exact engine line or web page it came from.

| File | Scope |
|---|---|
| [A_notify_and_montage_internals.md](A_notify_and_montage_internals.md) | `FAnimNotifyEvent`, Queued vs BranchingPoint, how a montage advances and collects notifies, when queued notifies are dispatched, filters, `SetPosition` |
| [B_update_rate_visibility_budget.md](B_update_rate_visibility_budget.md) | URO (`FAnimUpdateRateParameters`, trail and look-ahead modes), `VisibilityBasedAnimTickOption`, the Animation Budget Allocator |
| [C_network_roles.md](C_network_roles.md) | Server moves and `a.EnableQueuedAnimEventsOnServer` (5.6.1, 5.7.4 and 5.8.4 compared), `RepRootMotion`, GAS and Mover montage replication, server time on clients, net emulation, dedicated servers on a launcher engine |
| [D_existing_work_and_assets.md](D_existing_work_and_assets.md) | Existing solutions and what they leave open; which skeletal mesh and animation this project can use, and the licence terms |
| [E_measurement.md](E_measurement.md) | How each failure mode can be reproduced deterministically, and the clocks used to measure timing error |

**Conventions.**
- Engine paths are relative to `Engine/Source/Runtime/Engine/` unless another root is named. Line numbers are from the UE **5.8.4** launcher source (CL 58902052) unless a version is named.
- **[inferred]** marks a conclusion drawn from following code flow, not from a direct statement in the source. **[unverified]** marks a claim that could not be checked. **[to measure]** marks a hypothesis that needs a run.
- **The findings file is authoritative.** These notes are the audit trail.
