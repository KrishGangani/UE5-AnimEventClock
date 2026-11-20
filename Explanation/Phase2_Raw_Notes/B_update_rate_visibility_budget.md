> **Raw Phase 2 notes (B: update rate, visibility, budget), UE 5.8.4, 2026-11-21.** Read by hand from the launcher source. Paths are relative to `Engine/Source/Runtime/Engine/` unless another root is named. The claims the design depends on are restated in [../01b_Research_Findings.md](../01b_Research_Findings.md).

# B. What decides whether a mesh's animation updates this frame

## B1. The path from component tick to montage advance

| Fact | Source |
|---|---|
| Skeletal and skinned mesh components tick in `TG_PrePhysics`; `ACharacter` sets its mesh to `TG_PrePhysics` too | `Private/Components/SkeletalMeshComponent.cpp:441`; `Private/Components/SkinnedMeshComponent.cpp:472`; `Private/Character.cpp:128` |
| `USkinnedMeshComponent::TickComponent` first computes `bRecentlyRendered = LastRenderTime > TimeSeconds − 1.0`, so a mesh counts as "rendered" for **1 s** after it was last on screen | `SkinnedMeshComponent.cpp:1864` |
| It then calls `TickPose(DeltaTime, false)` only if `ShouldTickPose()` | `SkinnedMeshComponent.cpp:1874–1878` |
| `USkinnedMeshComponent::TickPose` broadcasts `OnTickPose` and calls `TickUpdateRate` (URO bookkeeping, once per owner per frame) | `SkinnedMeshComponent.cpp:1815–1830` |
| `USkeletalMeshComponent::TickPose` calls `TickAnimation(DeltaTimeForTick)` only if `ShouldTickAnimation()`. `DeltaTimeForTick` is `ExternalDeltaTime` when the budget allocator controls the mesh, `DeltaTime + AnimUpdateRateParams->GetTimeAdjustment()` with URO, otherwise `DeltaTime`. On a skipped frame it calls `OnUROSkipTickAnimation()` instead | `SkeletalMeshComponent.cpp:1938–1985` |
| `ShouldTickAnimation()`: `bExternalUpdate` under external (budget) control, otherwise `!ShouldUseUpdateRateOptimizations() \|\| !ShouldSkipUpdate()` | `SkeletalMeshComponent.cpp:1918–1928` |
| `TickAnimation` → `TickAnimInstances` → `UAnimInstance::UpdateAnimation` (which advances montages, notes A2) | `SkeletalMeshComponent.cpp:1696–1741, 1755–1786` |

## B2. URO (update-rate optimisation)

| Fact | Source |
|---|---|
| URO is **opt-in per mesh**: `bEnableUpdateRateOptimizations = false` by default; globally gated by `a.URO.Enable` (default 1) | `SkeletalMeshComponent.cpp:493`; `SkinnedMeshComponent.cpp:95–98, 1810–1813` |
| `FAnimUpdateRateParameters`: `OptimizeMode` (`TrailMode` / `LookAheadMode`), `UpdateRate`, `EvaluationRate`, `TickedPoseOffestTime` ("Track time we have lost via skipping"), `AdditionalTime` ("Total time of the last series of skipped updates"), `BaseNonRenderedUpdateRate` (default **4**), `BaseVisibleDistanceFactorThesholds` (defaults 0.24, 0.12), `LODToFrameSkipMap` | `Classes/Engine/EngineTypes.h:2707–2827` |
| `GetTimeAdjustment()` returns `AdditionalTime` | `EngineTypes.h:2876–2880` |
| Rate selection (`AnimUpdateRateSetParams`): **not rendered** (including dedicated servers): `UpdateRate = 1` if human-controlled or playing networked root motion, else `BaseNonRenderedUpdateRate`; **rendered and** human-controlled or root motion: 1; **otherwise** by screen size (or the LOD map), overridable by `a.URO.ForceAnimRate` | `SkinnedMeshComponent.cpp:283–370` (comments `:287`, `:292`, force `:354–358`) |
| The engine's own comment: "Human controlled characters should be ticked always fully to minimize latency w/ game play events triggered by animation." "Human controlled" means the owner's instigator controller is a `APlayerController` | `SkinnedMeshComponent.cpp:287, 205–210` |
| Look-ahead mode is used only when every visible component of the owner plays "root motion from everything" and the rate is above 1 | `SkinnedMeshComponent.cpp:360–364`; flag gathered at `:386–392` |
| **Trail mode:** on a skipped frame `TickedPoseOffestTime −= DeltaTime`; on the next update frame `AdditionalTime = −TickedPoseOffestTime` and it is reset to 0. The update then advances by `DeltaTime + AdditionalTime` | `SkinnedMeshComponent.cpp:6066–6117` |
| **Look-ahead mode:** every frame `TickedPoseOffestTime −= DeltaTime`; when it goes negative, `AdditionalTime = max(−TickedPoseOffestTime, LookAheadAmount)` and `TickedPoseOffestTime += LookAheadAmount`; otherwise the frame is skipped. `LookAheadAmount = (1/30 s) × DesiredEvaluationRate` | `SkinnedMeshComponent.cpp:6119–6152`; `:155`, `:363` |

**[inferred] Trail mode is late and bunched, never lost.** The skipped time is added back at the next update, so the montage position catches up exactly, and every notify crossed during the skipped frames fires together in the update frame. Lateness is up to `(UpdateRate − 1)` frames. With `BaseNonRenderedUpdateRate = 4` an off-screen NPC's notifies can be up to 3 frames late (50 ms at 60 fps, 100 ms on a 30 Hz server).

**[inferred, to measure] Look-ahead mode runs ahead.** On an update frame the animation advances by `DeltaTime + LookAheadAmount`, which puts it ahead of world time, so notifies fire **early**. Following the bookkeeping by hand: the update advances the animation by `DeltaTime + AdditionalTime`, but `TickedPoseOffestTime` is credited only with `LookAheadAmount`. If that reading is right, each update cycle adds one extra `DeltaTime` and the animation drifts further ahead over a long montage. This is a code reading only; it is the first thing E2 in [E_measurement.md](E_measurement.md) checks, and no claim is made until it is measured.

*Worked arithmetic of that reading (a hand simulation of the two functions above, not a measurement):* at rate 3 (`LookAheadAmount` = 0.1 s), after 2.0 s of world time the animation would have advanced 2.246 / 2.333 / 2.667 s at 144 / 60 / 30 fps, a lead of 246 / 333 / 667 ms. A lead that large would be plainly visible on any affected mesh, which is a reason to **doubt** the reading until E2 runs: something outside these two functions may compensate. If E2 shows no drift, this note is corrected and the drift claim is dropped.

## B3. `VisibilityBasedAnimTickOption`

| Value | Meaning (enum comments) | Source |
|---|---|---|
| `AlwaysTickPoseAndRefreshBones` | Always tick and refresh bones. **Default for `USkeletalMeshComponent`** | `Classes/Components/SkinnedMeshComponent.h:96–116`; default `SkeletalMeshComponent.cpp:446` |
| `AlwaysTickPose` | Always tick, refresh bones only when rendered. **`ACharacter`'s default** | `Character.cpp:125` |
| `OnlyTickMontagesAndRefreshBonesWhenPlayingMontages` | Not rendered: only montages and bones while a montage plays | enum |
| `OnlyTickMontagesWhenNotRendered` | Not rendered: only montages, graph skipped | enum |
| `OnlyTickPoseWhenRendered` | Tick only when rendered | enum |

| Fact | Source |
|---|---|
| `ShouldTickPose` passes visibility if the option is `<= AlwaysTickPose`, or the mesh was recently rendered, or a montage-only option applies while a montage plays, or a networked root-motion montage plays | `SkeletalMeshComponent.cpp:1883–1916` (visibility term `:1913`) |
| With `OnlyTickPoseWhenRendered` in a process that can never render (a dedicated server), the component's tick is **disabled at registration** | `SkeletalMeshComponent.cpp:1068–1071` |
| In montage-only mode, `UpdateAnimation` clears the queue, updates only montages and returns; the queued notifies are dispatched from `TickAnimation` | `Private/Animation/AnimInstance.cpp:703–748`; `SkeletalMeshComponent.cpp:1736–1739` |

**[inferred] Consequence.** With `OnlyTickPoseWhenRendered`, an off-screen mesh's montage does not advance at all: its notifies fire late by the whole off-screen time, after it becomes visible again (if the montage is still playing). On a dedicated server they never fire. The two montage-only options keep montage notifies alive when off-screen. The defaults (`AlwaysTickPoseAndRefreshBones`, and `AlwaysTickPose` for characters) are not affected.

## B4. The Animation Budget Allocator (plugin)

Root: `Engine/Plugins/Runtime/AnimationBudgetAllocator/Source/AnimationBudgetAllocator/`.

| Fact | Source |
|---|---|
| Only `USkeletalMeshComponentBudgeted` meshes take part; the allocator sets `EnableExternalTickRateControl(true)` on registration | `Public/SkeletalMeshComponentBudgeted.h:40, 103`; `Private/AnimationBudgetAllocator.cpp:1196` |
| Each frame a queued component adds the frame delta to `AccumulatedDeltaTime` | `AnimationBudgetAllocator.cpp:183–186` |
| A component ticks this frame if `(GFrameCounter + FrameOffset) % TickRate == 0`; it receives `SetExternalDeltaTime(AccumulatedDeltaTime × CustomTimeDilation)` and `EnableExternalUpdate(bTickThisFrame)`; the accumulator resets only when it ticks | `AnimationBudgetAllocator.cpp:440, 466–480` |
| A component that the allocator decides not to tick at all goes through `DisableComponentTick`, which sets `AccumulatedDeltaTime = 0` | `AnimationBudgetAllocator.cpp:232–241, 294–296, 385–389` |
| Off-screen components with `bTickEvenIfNotRendered` are capped at `a.Budget.MaxTickedOffsreen` (default **4**, most significant first); the rest are disabled | `AnimationBudgetAllocator.cpp:280–288, 344–389`; `Private/AnimationBudgetAllocatorCVars.cpp:319–330`; `Public/AnimationBudgetAllocatorParameters.h:94` |
| Deterministic test controls exist in Development builds: `a.Budget.Debug.Force`, `a.Budget.Debug.Force.Rate` (default 4), `.Interp`, `.Reduced` (compiled when `!(UE_BUILD_SHIPPING \|\| UE_BUILD_TEST)`) | `AnimationBudgetAllocatorCVars.cpp:130–185` |
| Budget cvars: `a.Budget.Enabled` (default **0**; "Should be set when there are no running skeletal meshes"), `a.Budget.BudgetMs`, `a.Budget.MaxTickRate`, … | `AnimationBudgetAllocatorCVars.cpp:7–14, 195, 221` |
| A budgeted mesh registers itself in `BeginPlay` (`bAutoRegisterWithBudgetAllocator` defaults to true), **except on a dedicated server** (`!UKismetSystemLibrary::IsDedicatedServer(this)`) | `Private/SkeletalMeshComponentBudgeted.cpp:23, 29–36` |

**[inferred] Consequences.** A throttled component behaves like URO trail mode: notifies late by up to `TickRate − 1` frames and bunched, with no time lost. A component that is **disabled** (over the off-screen cap) loses the time it was disabled: its montage falls behind world time permanently, so its notifies are late by the whole disabled duration, and a montage that should have ended keeps playing. **[to measure]** (E4).

**Verified scope limit.** The budget allocator never manages meshes on a dedicated server, so budget conditions apply to standalone, listen-server and client runs only, and `a.Budget.Enabled` has to be set at start-up (command line or config), before any budgeted mesh runs.

## B5. What none of these paths does

No path above advances a montage from the gameplay clock when the mesh's own update is skipped. `OnUROSkipTickAnimation()` is a hook on the anim instance, called on skipped frames (`SkeletalMeshComponent.cpp:1969–1985`); its base implementation is empty (`Classes/Animation/AnimInstance.h:1353`). A project can use it, but the montage still advances only inside `UpdateAnimation`.
