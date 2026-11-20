> **Raw Phase 2 notes (C: network roles), UE 5.8.4 unless a version is named, 2026-11-21.** Read by hand from the launcher sources of 5.8.4, 5.7.4 (CL 51494982) and 5.6.1 (CL 44394996), all installed on this machine. Paths are relative to `Engine/Source/Runtime/Engine/` unless another root is named. The claims the design depends on are restated in [../01b_Research_Findings.md](../01b_Research_Findings.md).

# C. Who advances the montage, per network role

## C1. Root-motion montages and the character movement component

| Fact | Source |
|---|---|
| When a character plays root motion, the movement component ticks the pose itself (`TickCharacterPose`) before moving: in `PerformMovement` and in `SimulatedTick` for simulated proxies playing a networked root-motion montage | `Private/Components/CharacterMovementComponent.cpp:2772, 2796–2799, 2920–2923, 1880–1894` |
| `TickCharacterPose` sets `bIsAutonomousTickPose = true`, calls `TickPose(DeltaTime, true)` (or `TickMontagesOnly`), consumes root motion, and resets the flag | `CharacterMovementComponent.cpp:12027–12118` |
| URO forces update rate 1 for a networked root-motion montage (`bNeedsEveryFrame`) | `Private/Components/SkinnedMeshComponent.cpp:290, 295, 300–303, 394` |

## C2. The server ticks a remote player's pose per client move

| Fact | Source |
|---|---|
| On the server, a remotely controlled character's mesh may tick its pose **only** through the movement code: `bOnlyAllowAutonomousTickPose = IsReplicatingMovement && CMC active && RemoteRole == AutonomousProxy && has a connection`. Comment: "set animation timing to be driven by client's network updates. So timing and events remain in sync" | `Private/Character.cpp:2443–2453`; enforced in `SkeletalMeshComponent.cpp:1910` |
| `MoveAutonomous` (one call per client move received) runs `PerformMovement`, then, when not playing root motion, `TickCharacterPose(DeltaTime)` with the **client move's** delta | `CharacterMovementComponent.cpp:10664–10722` (pose tick `:10705`) |
| Clients send moves at most every `ClientNetSendMoveDeltaTime = 0.0166` s, also when stationary (`ClientNetSendMoveDeltaTimeStationary = 0.0166`) | `Engine/Config/BaseGame.ini:33, 35`; `Private/GameNetworkManager.cpp:38–40` |

**[inferred] Consequence.** On a server, the animation time of a player-controlled character is the sum of the client move deltas processed so far, at the moments the moves arrive. Its notifies follow the client's frame cadence plus network delay and jitter, and bunch when several moves arrive in one server frame. **[to measure]** (E5).

## C3. Queued events on the server: 5.6.1 and 5.7.4 vs 5.8.4

`a.EnableQueuedAnimEventsOnServer` (default 1 in all three): "Whether to enable queued anim events on servers. In most cases, when the server is doing a full anim graph update, queued notifies aren't triggered by the server, but this will enable them. Enabling this is recommended in projects using Listen Servers." (5.8.4 `CharacterMovementComponent.cpp:151–157`; 5.6.1 `:144–150`; 5.7.4 `:142–148`.)

| Version | Where the server dispatches and clears the queue after a move | Source |
|---|---|---|
| **5.6.1** | In `MoveAutonomous`, after `TickCharacterPose`: if the cvar is on and the graph update is pending (`VisibilityBasedAnimTickOption <= AlwaysTickPose && AnimInstance->NeedsUpdate()`), `ConditionallyDispatchQueuedAnimEvents()`, then `ClearQueuedAnimEvents(false)`, then `AllowQueuedAnimEventsNextDispatch()` | 5.6.1 `CharacterMovementComponent.cpp:10459–10489` |
| **5.7.4** | Identical code | 5.7.4 `CharacterMovementComponent.cpp:10532–10561` |
| **5.8.4** | Moved **into `TickCharacterPose`**, so it runs for every pose tick the movement code makes (root motion included), for the main and the post-process anim instance. `MoveAutonomous` keeps only the "cvar off" fallback | 5.8.4 `CharacterMovementComponent.cpp:12084–12115`, `10712–10720` |
| **5.8.4 only** | Two new cvars in `AnimInstance.cpp`: `a.Notifies.ApplyUnfilteredOnAutonomousTickPose` (default 1) and `a.Notifies.DispatchPolicy` (default 1 = `UpdatedSourcesOnly`: "Only dispatch from sources that have updated this tick … active notify states from skipped sources remain valid. Useful for multi-tick scenarios such as MoveAutonomous") | 5.8.4 `Private/Animation/AnimInstance.cpp:169–189`; used at `:952–957, 1868–1872`; per-tick flags `:989–994`, reset `:842–846` |

**The 5.6.1 report (UE-308913).** The forum poster (Aug 2025) saw `NotifyBegin`/`NotifyEnd` of a notify state fire repeatedly on a dedicated server for the whole montage, traced it to `ServerMovePacked → MoveAutonomous → TriggerAnimNotifies` (begin), then `MoveAutonomous → ClearQueuedAnimEvents`, then `FinalizeBoneTransform → TriggerAnimNotifies` (end, because the queue was cleared), said it was not present in 5.5.4, and gave tracker id UE-308913 ([D](D_existing_work_and_assets.md) D2).

**[inferred] Status on 5.8.4.** The code path the poster named was restructured in 5.8 and gained a dispatch policy whose help text names `MoveAutonomous`. That is consistent with a fix, but **whether UE-308913 is fixed in 5.8.4 is [unverified]**: the public tracker page returned HTTP 403 to this research, and code reading cannot prove the absence of a repeat. The project therefore **measures** it (E5: notify-state begin/end counts per action on a dedicated server for a remote player) and claims nothing about 5.8.4 until then.

## C4. Simulated proxies and position corrections

| Mechanism | Fact | Source |
|---|---|---|
| `RepRootMotion` (`FRepRootMotionMontage`) | Replicates, while a root-motion montage plays: the animation, `bIsActive`, the **track position**, location, rotation, movement base, authoritative root-motion sources, acceleration, velocity. It carries no start time | `Classes/GameFramework/Character.h:47–131, 1159–1160` |
| `OnRep_RootMotion` on a simulated proxy | Queues the move with the local receive time; it does **not** start a montage | `Private/Character.cpp:1786–1819` |
| `SimulatedRootMotionPositionFixup` | Moves the actor back to the server position of a buffered move and re-simulates root motion for `ClientPosition − ServerPosition`; it changes the actor's location, not the montage position | `Character.cpp:1821–1872` |
| Autonomous correction | `ClientAdjustRootMotionPosition` on a disagreement calls `RootMotionMontageInstance->SetPosition(ServerMontageTrackPosition)` ("Not much we can do there unfortunately, just jump to server's track position") | `CharacterMovementComponent.cpp:11509–11521` |
| GAS montage replication (`FGameplayAbilityRepAnimMontage`) | Replicates the animation, play rate, the server's **current position** (custom serializer, when `bRepPosition`), section ids, `PlayInstanceId`, stop flag, prediction key. No start time | `Engine/Plugins/Runtime/GameplayAbilities/Source/GameplayAbilities/Public/Abilities/GameplayAbilityRepAnimMontage.h:22–104` |
| GAS on a simulated proxy | A new `PlayInstanceId` → `PlayMontageSimulated` (local start at position 0). Then, if the local position differs from the replicated one by more than **0.1 s** (`MONTAGE_REP_POS_ERR_THRESH`) in the same section: forward → `HandleEvents(Current, Rep, nullptr)` + `TriggerAnimNotifies(…, ForceMontageOnly)`; backward → no events. In both cases `Montage_SetPosition(Rep)`. Epic's own comment: "Forcing the position here is not a good way to do replication syncing for animations." | `Engine/Plugins/Runtime/GameplayAbilities/Source/GameplayAbilities/Private/AbilitySystemComponent_Abilities.cpp:3279–3450` (threshold `:3292`, forward `:3427–3441`, comment `:3442–3445`, set `:3446`) |

**[inferred] Consequences.**
- A simulated proxy's montage runs on its **own** local clock, started when the replicated start arrives, so its notifies are late by the one-way delay plus frame quantisation relative to server time. For cosmetics that is consistent with what the client sees of that actor, which is also delayed.
- Below 0.1 s of error, GAS never corrects; above it, a **backward** correction makes the montage pass the same notifies again (**duplicates**). A **forward** correction fires queued notifies for the jumped range, but `HandleEvents(…, nullptr)` passes no branching-point marker, so instant BranchingPoint notifies in that range are **skipped**. Notify states are still begun/ended by `UpdateActiveStateBranchingPoints` (notes A3).
- The autonomous root-motion correction (`SetPosition`) skips (forward) or repeats (backward) notifies (notes A4).
- All of these are **[to measure]** (E6).

## C5. Mover (Experimental) owns montage position in its simulation

| Fact | Source |
|---|---|
| Mover is marked `IsExperimentalVersion: true` in 5.8.4 | `Engine/Plugins/Experimental/Mover/Mover.uplugin:15` |
| A per-substep montage state is produced by the Mover simulation (`FMoverSimDrivenMontageEntry`: position at the end of the substep, play rate, simulation start/blend-out/finish times) | `Engine/Plugins/Experimental/Mover/Source/Mover/Public/DefaultMovementSet/MoverMontageSimulationTypes.h:14–66` |
| `mover.anim.AsyncMontageSimMaster` (default true): "simulation is master of montage position, GT advances to interpolated simulation position in post sim" | `Engine/Plugins/Experimental/Mover/Source/Mover/Private/MoverComponent.cpp:56–62` |
| In the async backend the game thread moves the montage with `SetNextPositionWithEvents(Previous, Current)`, so notifies fire for the traversed range; on the first encounter it uses `SetPosition` on purpose, so notifies before the current position are **not** fired | `MoverComponent.cpp:3085–3147` (comment `:3124–3131`) |

**[inferred] What Mover leaves open.** Mover is the closest existing work: position is authoritative and network-predicted. But (a) only for montages driven by Mover layered moves on the async backend, (b) Experimental, and (c) the notifies are still fired by the anim instance when it next updates (`SetNextPositionWithEvents` acts on the **next** update, notes A4), so URO, the budget allocator and visibility still decide *when* they fire.

## C6. Server time on a client

| Fact | Source |
|---|---|
| `AGameStateBase::GetServerWorldTimeSeconds()` = local world time + `ServerWorldTimeSecondsDelta` (a `float`) | `Private/GameStateBase.cpp:144–150`; `Classes/GameFramework/GameStateBase.h:155` |
| The server replicates its world time every 0.1 s (`ServerWorldTimeSecondsUpdateFrequency`); the client sets the delta to `ReplicatedTime − LocalTime` **on receipt**, averages it (reset after 250 samples) and moves halfway towards the average each update. There is **no** round-trip or one-way-delay compensation | `GameStateBase.cpp:36, 65–67, 155–161, 164–193` |

**[inferred] Consequence.** On a client, `GetServerWorldTimeSeconds()` lags true server time by about the one-way delay. A client that schedules "server start + authored offset" with it fires events late by that delay, the same as starting the montage on receipt. A latency-compensated estimate needs a round-trip measurement, for example Cristian's method: the client sends its time `t0`, the server replies with its time `ts`, the client receives at `t1` and estimates `ServerNow ≈ ts + (t1 − t0)/2` at `t1` (F. Cristian, "Probabilistic clock synchronization", *Distributed Computing* 3, 1989, https://doi.org/10.1007/BF01784024). Keeping the sample with the smallest round trip bounds the error by half that round trip.

## C7. Network emulation and dedicated servers on this engine

| Fact | Source |
|---|---|
| Packet emulation is compiled into every non-Shipping build (`DO_ENABLE_NET_TEST !(UE_BUILD_SHIPPING)`) | `Classes/Engine/NetDriver.h:450` |
| Command-line form `-PktLag=<ms>` (also `PktLagVariance`, `PktLoss`, …), parsed from the command line when a net driver initialises | `Private/Net/NetEmulationHelper.cpp:534–560, 628–645`; `Private/NetDriver.cpp:788` |
| Lag is applied to **outgoing** packets of the process that sets it, using `FPlatformTime::Seconds()` (real time, not game time): `SendTime = Now + PktLag/1000 ± variance` | `Private/NetConnection.cpp:2457, 2504, 2666–2673` |
| A dedicated server is capped at `NetServerMaxTickRate = 30` Hz by default (listen servers only if `bClampListenServerTickRate`) | `Engine/Config/BaseEngine.ini:1867`; `Private/GameEngine.cpp:1742–1745` |
| The launcher engine lists only Editor (Development, DebugGame) and Game (DebugGame, Development, Shipping) configurations for Win64 x64; there is no Server or Client entry. UBT refuses target types that are not listed ("{0} targets are not currently supported from this engine distribution.") | `Engine/Config/BaseEngine.ini:4001–4008` (no `PlatformType="Server"` line in the file); `Engine/Source/Programs/UnrealBuildTool/Configuration/UEBuildTarget.cs:1396–1417` |
| A packaged Game build can never be a dedicated server (`IsGameOnly`); "An editor build can be launched with -server" | `Engine/Source/Runtime/Core/Public/Misc/CoreMisc.h:146–166` |

**[inferred] Consequences for this project.**
- Dedicated-server roles run on the uncooked editor binary with `-server` (Phase 4 check F-7), at 30 Hz unless configured otherwise. Clients and listen servers can be packaged Development builds.
- Emulated latency is real time, so **network runs cannot use the fixed step** (`-deterministic` makes game time run faster or slower than real time). They run in real time with a frame cap (`t.MaxFPS`), and every process is pinned to the performance cores.
- "0/100/200 ms latency" is defined in this project as the added **round trip**: `-PktLag=0/50/100` on **both** the client and the server, so each direction carries half (01 §6.2).
