> **Raw Phase 2 notes (A: notify and montage internals), UE 5.8.4, 2026-11-21.** Read by hand from the launcher source. Paths are relative to `Engine/Source/Runtime/Engine/`. The claims the design depends on are restated in [../01b_Research_Findings.md](../01b_Research_Findings.md).

# A. How a montage notify gets from the asset to your code

## A1. The data: `FAnimNotifyEvent`

| Fact | Source |
|---|---|
| Notifies live on every `UAnimSequenceBase` (montages included) as a **runtime** `UPROPERTY() TArray<FAnimNotifyEvent> Notifies`, "sorted by time (earliest notification first)" | `Classes/Animation/AnimSequenceBase.h:41–43` |
| `FAnimNotifyEvent` derives from `FAnimLinkableElement` (its time is a link into the montage timeline) and carries: `TriggerTimeOffset`, `EndTriggerTimeOffset`, `TriggerWeightThreshold`, `NotifyName`, `Notify` (instanced `UAnimNotify`), `NotifyStateClass` (instanced `UAnimNotifyState`), `Duration`, `EndLink`, `MontageTickType`, `NotifyTriggerChance`, `NotifyFilterType`, `NotifyFilterLOD`, `bCanBeFilteredViaRequest`, `bTriggerOnDedicatedServer`, `bTriggerOnFollower`, `TrackIndex` | `Public/Animation/AnimTypes.h:279–355` |
| Defaults: `TriggerWeightThreshold = ZERO_ANIMWEIGHT_THRESH`, `MontageTickType = Queued`, `NotifyTriggerChance = 1`, `NotifyFilterType = NoFiltering`, `bTriggerOnDedicatedServer = true`, `bTriggerOnFollower = false` | `AnimTypes.h:369–390` |
| `EMontageNotifyTickType`: **Queued** "Queue notifies, and trigger them at the end of the evaluation phase (faster). Not suitable for changing sections or montage position." **BranchingPoint** "Trigger notifies as they are encountered (Slower). Suitable for changing sections or montage position." | `AnimTypes.h:84–95` |
| The trigger time is the authored time plus a tiny offset: `GetTriggerTime() = GetTime() + TriggerTimeOffset`; the offset is `±UE_KINDA_SMALL_NUMBER` or 0 (`NOTIFY_TRIGGER_OFFSET`) | `Private/Animation/AnimTypes.cpp:12, 15–37, 81–84` |
| `GetTriggerTime`, `GetEndTriggerTime`, `GetDuration` are **not** inside `WITH_EDITOR`: they work in cooked builds | `AnimTypes.cpp:81–99` (the `WITH_EDITORONLY_DATA` block ends at `:63`) |
| A notify is a branching point if it is linked to a montage and its tick type is BranchingPoint, or its class sets `bIsNativeBranchingPoint` | `AnimTypes.cpp:107–110` |

## A2. How a montage advances

| Fact | Source |
|---|---|
| Montage update runs **on the game thread** inside `UAnimInstance::UpdateAnimation`, before the graph update: "update montage should run in game thread … because branch points need to execute arbitrary code inside this call" | `Private/Animation/AnimInstance.cpp:561–586` (comment `:575–578`), call site `:764–776` (after `PreUpdateAnimation`) |
| `UpdateMontage` = `Montage_UpdateWeight(dt)` then `Montage_Advance(dt)` | `AnimInstance.cpp:572–578` |
| `FAnimMontageInstance::Advance(DeltaTime, …)` adds `DeltaTime` to a sub-stepper and loops **at most 9 sub-steps** (`MaxIterations = 10`, `++NumIterations < MaxIterations`). Remaining time accumulates only while it is below 10 s | `Private/Animation/AnimMontage.cpp:2549, 2575, 2582–2585, 2593` |
| Each sub-step stops **exactly at the next branching-point marker** inside the step (`DeltaMove = Marker->TriggerTime − Position`), so the marker fires at its own position before the montage moves on | `AnimMontage.cpp:2280–2290` |
| After each sub-step `HandleEvents(PreviousSubStepPosition, Position, BranchingPointMarker)` runs | `AnimMontage.cpp:2687` |
| Branching-point markers are built once per montage from `Notifies` (`RefreshBranchingPointMarkers`), sorted by trigger time; notify states add a Begin and an End marker | `AnimMontage.cpp:663–704` |
| `FAnimMontageInstance::Play` sets `bPlaying`, the play rate and the blend; it does **not** advance the position. The first `Advance` happens at the next `UpdateAnimation` | `AnimMontage.cpp:1513–1547` |

**[inferred] Consequence for the start frame.** World time is advanced at the start of a frame (`Private/LevelTick.cpp:1610`), and a montage started earlier in the same frame than its mesh's tick advances by that frame's whole `DeltaTime` on the first update. Its notifies then fire up to one frame **earlier** than "world time at `Montage_Play` + authored time". A montage started after the mesh ticked (for example from a timer, `LevelTick.cpp:1816`, which runs after `TG_PostPhysics`) advances first on the next frame, so its notifies fire up to one frame **later**. The error depends on tick order. **[to measure]** (E3 in [E_measurement.md](E_measurement.md)).

## A3. How notifies are collected and dispatched

| Fact | Source |
|---|---|
| `HandleEvents` returns immediately if the montage was interrupted | `AnimMontage.cpp:2822–2826` |
| Montage-track notifies are gathered for the interval `(Previous, Current]` (`StartTime <= Current && EndTime > Previous`), branching points are **removed**, and the rest go into `AnimInstance->NotifyQueue` with the montage's `NotifyWeight` | `AnimMontage.cpp:2842–2851`; window rule `Private/Animation/AnimSequenceBase.cpp:478` |
| Notifies on the sequences inside slot tracks go into a separate per-slot list (`UnfilteredMontageAnimNotifies`) that is applied only if the slot node is relevant | `AnimMontage.cpp:2854–2871`; `Private/Animation/AnimNotifyQueue.cpp:101–108, 138–147` |
| Branching points fire immediately: `UpdateActiveStateBranchingPoints` (state begin/end) and `BranchingPointEventHandler(marker)` | `AnimMontage.cpp:2876–2886` |
| Queue filters, applied when a notify is added: dedicated-server flag, `TriggerWeightThreshold <= InstanceWeight`, LOD filter (`NotifyFilterLOD > PredictedLODLevel`), random chance (`RandomStream.FRandRange(0,1) < NotifyTriggerChance`, states always pass), and the anim-events filter scope | `AnimNotifyQueue.cpp:28–77` |
| `NotifyWeight = max(PreviousWeight, current blend weight)`, "as notify could have come from any point between now and last tick" | `AnimMontage.cpp:2061–2063` |
| The queue is cleared at the start of every anim update (`PreUpdateAnimation` → `ClearQueuedAnimEvents(true)`) | `AnimInstance.cpp:832–851` |
| Queued notifies are dispatched by `USkeletalMeshComponent::ConditionallyDispatchQueuedAnimEvents` → `UAnimInstance::DispatchQueuedAnimEvents` → `TriggerAnimNotifies`. Call sites: end of `TickComponent` when no parallel evaluation runs (`Private/Components/SkeletalMeshComponent.cpp:2102–2110`), after evaluation completes (`:3369–3377`, `:5186`), and in montage-only ticking (`:1736–1739`) | `SkeletalMeshComponent.cpp:2114–2135`; `AnimInstance.cpp:947–995` |
| `bNeedsQueuedAnimEventsDispatched` is set only when `TickAnimation` runs | `SkeletalMeshComponent.cpp:1726–1727` |

**[inferred] Consequence.** Both tick types fire only in a frame in which the mesh's animation **updates**. BranchingPoint fires earlier inside that frame (during the game-thread montage advance), Queued fires later (after evaluation). Neither fires in a frame whose anim update was skipped. So under URO or the budget allocator both are late by the same number of frames; the forum report that "Branching point or queued ticktype doesn't make a difference" ([D](D_existing_work_and_assets.md) D2) matches the code.

## A4. Jumps in position

| Fact | Source |
|---|---|
| `FAnimMontageInstance::SetPosition` is a plain assignment: `Position = InPosition; MarkerTickRecord.Reset();`. No notify is gathered for the skipped range | `Classes/Animation/AnimMontage.h:558` |
| `SetNextPositionWithEvents(From, To)` instead makes the **next** update fire events for that range | `AnimMontage.h:566–569` |

**[inferred] Consequences.** A forward `SetPosition` skips every notify in the jumped range (a **missed** event). A backward `SetPosition` makes the montage pass the same notifies again (a **duplicate**). Where the engine does this is in [C](C_network_roles.md) (C3, C4).
