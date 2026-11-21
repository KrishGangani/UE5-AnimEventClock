// P06 AnimEventClock (demo) - the benchmark director: spawns the seeded workload, plays the actions, records every event.

#pragma once

#include "AECEventTypes.h"
#include "Engine/TimerHandle.h"
#include "Math/RandomStream.h"
#include "Net/Core/Connection/NetEnums.h"
#include "Subsystems/WorldSubsystem.h"
#include "AECBenchSubsystem.generated.h"

class AAECDemoCharacter;
class ACameraActor;
class APlayerController;
class UAnimMontage;
class UAnimSequenceBase;
class UNetDriver;
class USkeletalMeshComponent;

/**
 * Runs one timing benchmark in a game world (01 §6, §7). A world subsystem because the project has no authored map: it has
 * exactly the world's lifetime and needs nothing placed. Tickable: its Tick runs after TG_PostPhysics (the "post" start order).
 *
 * The workload (01 §6.1): N demo characters in three bands in front of / far in front of / behind a fixed camera. Each one
 * loops "wait a seeded 0.5-1.5 s, then play an action": one of the demo montages at play rate 1.0 or 1.3, 10% of them
 * interrupted at a seeded point. Every character has its own random stream, so its schedule never depends on the others.
 *
 * Two paths report each authored event, and both are recorded with game time, wall clock and frame number:
 *   - the ANIMATION path (configuration NQ, NB or G: which montage variant plays, and whether GAS plays it), through the
 *     markers' comparison hook;
 *   - the TIMER path (configuration T): FTimerManager timers set at the action's start from the authored positions.
 * The expected events (with their due times: start + position / play rate) are written alongside, so a script can measure
 * timing error and missed / duplicate events per path without trusting either path.
 *
 * Network runs (S4; the world's net mode decides, nothing changes in a standalone run). The server (listen or dedicated)
 * is the director: it waits for -AECPlayers remote players, gives each a demo character of its own (the demo game mode
 * calls AddRemotePlayer), then runs the same schedule for NPCs and players, and multicasts each start and stop with the
 * action's id. A client started with -AECClient is a recorder: it records the fires of every replicated character
 * (simulated proxies and its own autonomous proxy) against the server's action ids, starts NQ/NB montages and T timers
 * when a start arrives (G montages come from GAS), and writes its files when the server says the run is over. Every
 * process of a network run also logs (frame, game time, wall clock) once per frame, so due times in server game time can
 * be put on the shared wall clock (raw notes E0).
 *
 * Timeline (world time; "start" = begin play, or the moment the last expected remote player joined):
 *   [start, start + Warmup)     warm-up: actions already running, not counted
 *   start + Warmup              CSV_EVENT "BenchStart"; actions started from now on are "in capture"
 *   + Capture                   CSV_EVENT "BenchEnd"; no new actions start
 *   after the last action ends  + 1 s grace for late events, then the files are written; a server tells its clients, and
 *                               quits 3 s later (so its clients quit first); with -AECExit the process quits
 *
 * Command line (all optional; -AECChars=0, the default, means "no workload" unless -AECClient is given):
 *   -AECChars=N          NPC characters to spawn (split evenly across the three bands)
 *   -AECSeed=S           seed (default 1)
 *   -AECWarmup=SEC       warm-up seconds (default 5)
 *   -AECCapture=SEC      capture seconds (default 60)
 *   -AECConfig=NQ|NB|G   NQ / NB: Queued or BranchingPoint markers played with Montage_Play; G: Queued markers played by
 *                        a GAS ability (default NQ). A client must be given the server's value
 *   -AECCond=NAME        def | uro | uro2 | uro4 | la4 | rme | aba2 | aba4 | vis   (default def; 01 §6.2; rme added in S3)
 *   -AECStart=post|pre   where actions start: this Tick, after the meshes ticked (post, default), or the character's tick
 *                        before its mesh (pre: the montage then advances a whole frame on its first update; 01 H8)
 *   -AECPlayers=N        server: wait for N remote players before the warm-up starts (default 0)
 *   -AECClient           this process is a network client that records (its world must be a client world). A client
 *                        uses -AECChars, -AECPlayers, -AECWarmup and -AECCapture only to size its buffers
 *   -AECRunTag=NAME      output file prefix (default "run"); files go to Saved/AECRuns/
 *   -AECExit             quit when done (exit code 0; 3 if the run could not be set up; 4 if its files could not be written;
 *                        5 if a client lost its server before the run ended)
 *
 * Frame numbers in the files are counted from the frame of OnWorldBeginPlay, so engine start-up and loading frames can
 * never change them (the determinism hash compares them across runs).
 */
UCLASS()
class UAECBenchSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin UWorldSubsystem
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	//~ End UWorldSubsystem

	//~ Begin FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	//~ End FTickableGameObject

	/** Starts the action the director planned for this character. Called from the character's tick ("pre" start order). */
	void ExecuteRequestedStart(AAECDemoCharacter& Character);

	/** Server: gives a remote player a demo character of its own and possesses it (called by the demo game mode). */
	void AddRemotePlayer(APlayerController& Player);

	/** Client: a replicated demo character began play. */
	void RegisterReplicatedCharacter(AAECDemoCharacter& Character);

	/** Client: the server started the action with this id on this character. */
	void HandleServerStart(AAECDemoCharacter& Character, int32 ActionId, uint8 MontageIndex, float PlayRate);

	/** Client: the server interrupted the action with this id. */
	void HandleServerStop(AAECDemoCharacter& Character, int32 ActionId);

	/** Client: the server's run is over; write this client's files and finish. */
	void FinishClientRun();

protected:
	//~ Begin UWorldSubsystem
	/** Game and PIE worlds only. */
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	//~ End UWorldSubsystem

private:
	/** Which path reported a fire. */
	enum class EPath : uint8
	{
		Animation,
		Timer
	};

	enum class ERunPhase : uint8
	{
		Idle,
		WaitingForPlayers,   // server of a network run: NPCs spawned, no action yet
		WarmUp,
		Capture,
		Draining,
		Recording,           // client of a network run: everything comes from the server
		Done
	};

	/** Who drives a character's animation in this process (01 §6.2, network roles). Fixed per action, when it starts. */
	enum class ERole : uint8
	{
		Npc,                 // standalone, or a server's NPC
		RemotePlayer,        // server: a character a remote player possesses (its pose follows the client's moves)
		SimulatedProxy,      // client: someone else's character
		AutonomousProxy      // client: this client's own character
	};

	/** Values read once from the command line. */
	struct FRunSettings
	{
		int32 NumCharacters = 0;
		int32 Seed = 1;
		double WarmupSeconds = 5.0;
		double CaptureSeconds = 60.0;
		FString Config = TEXT("NQ");
		FString Condition = TEXT("def");
		bool bStartPre = false;
		int32 Players = 0;
		bool bClient = false;
		FString RunTag = TEXT("run");
		bool bExitWhenDone = false;
	};

	/** One authored event of a montage, read from the montage's own notifies (a window gives two keys). */
	struct FKey
	{
		FName Name;
		EAECEventPhase Phase = EAECEventPhase::Instant;
		float Position = 0.f;
		int32 PairKey = INDEX_NONE;   // a window's begin <-> end key; INDEX_NONE for an instant event
	};

	/** A loaded demo montage and its keys, sorted by position. */
	struct FMontageInfo
	{
		FString Name;
		float Length = 0.f;
		TArray<FKey> Keys;
	};

	/** One played action. On a client, "start" and "stop" are when the server's messages arrived. */
	struct FAction
	{
		int32 Id = INDEX_NONE;           // the server's action index (the same as this array's index on the server)
		int32 Character = INDEX_NONE;
		int32 Montage = INDEX_NONE;
		ERole Role = ERole::Npc;
		float PlayRate = 1.f;
		double StartGameTime = 0.0;
		double StartWallTime = 0.0;
		uint64 StartFrame = 0;
		double StopGameTime = -1.0;      // < 0: not interrupted
		bool bInCapture = false;         // always false on a client (the server's file says)
	};

	/** One reported event. */
	struct FFire
	{
		int32 Action = INDEX_NONE;       // index into Actions (not the id)
		int32 Key = INDEX_NONE;
		EPath Path = EPath::Animation;
		double GameTime = 0.0;
		double WallTime = 0.0;
		uint64 Frame = 0;
	};

	/** Client: an animation fire that came after its character's current action was over, waiting for the next start. */
	struct FHeldFire
	{
		const UAnimSequenceBase* Animation = nullptr;   // a demo montage (kept alive by Montages)
		FName Name;
		EAECEventPhase Phase = EAECEventPhase::Instant;
		double GameTime = 0.0;
		double WallTime = 0.0;
		uint64 Frame = 0;
	};

	/** One frame of a network run: its game time and the wall-clock moment that game time stands for. */
	struct FFrameStamp
	{
		uint64 Frame = 0;
		double GameTime = 0.0;
		double WallTime = 0.0;
	};

	/** Per-character schedule and the timer path's state. */
	struct FCharacterState
	{
		int32 Band = 0;                     // 0 near, 1 far, 2 behind the camera, 3 a player's character
		double NextStartTime = 0.0;
		int32 CurrentAction = INDEX_NONE;   // kept after the action ends, so late animation fires are still attributed
		bool bPlaying = false;
		bool bWaitingForStart = false;
		double PlannedEndTime = 0.0;
		double PlannedStopTime = -1.0;
		int32 PlannedMontage = 0;
		float PlannedPlayRate = 1.f;
		double PlannedStopFraction = -1.0;
		TArray<FTimerHandle> Timers;
		TArray<bool> TimerWindowOpen;       // per key of the current action: a timer-path window begun but not ended
		FRandomStream Random;               // this character's own schedule stream
		TArray<FHeldFire> HeldFires;        // client: fires that belong to an action whose start has not arrived yet
	};

	static FRunSettings ReadRunSettings();
	static double WallSeconds();

	/** The wall-clock moment this frame's game time stands for: the engine's frame start time, on WallSeconds()'s base. */
	static double FrameStartWallSeconds();

	/** "standalone", "listen", "dedicated" or "client" (the file header's net= value). */
	const TCHAR* NetModeName() const;

	/** Applies the -AECCond settings that must be in place before any character exists. Returns false if unknown. */
	bool PrepareCondition();

	/** Sets an int console variable for this run only (temporary priority, tagged; undone in Deinitialize). */
	static bool SetConsoleVariable(const TCHAR* Name, int32 Value);

	/** Cancels a character's pending timer-path timers. */
	void ClearTimers(FCharacterState& State) const;

	/** Loads the run variant's two demo montages and reads their keys. Returns false if any is missing or has no keys. */
	bool LoadMontages();

	/** Spawns one demo character of the run's class (deferred, so per-condition mesh settings are applied before BeginPlay)
	 *  and adds its state. Returns null if the spawn failed. */
	AAECDemoCharacter* SpawnCharacter(UWorld& World, const FTransform& Transform, bool bPlayer);

	/** Reserves the action and fire records for the longest possible run of this many characters, so the arrays never
	 *  reallocate (and copy megabytes) inside a measured frame. */
	void ReserveRecords(int32 MaxCharacters);

	/** Spawns the NPCs into their bands. Returns false if any spawn failed: the run is then not started, so indices always match. */
	bool SpawnCharacters(UWorld& World);

	/** Plans every character's first action and starts the warm-up. */
	void BeginWarmUp(double Now);

	/** The director's part of a frame (server or standalone): phases, starts, stops, and the files at the end. */
	void TickDirector(double Now);

	/** Plans the next action of one character (montage, play rate, interruption) from its own random stream. */
	void PlanNextAction(int32 CharacterIndex, double Now);

	/** An action starts now on this process (server or client): its record, its montage (the character decides how, G
	 *  included), its timer path. Returns its index in Actions. */
	int32 BeginAction(int32 CharacterIndex, int32 Id, int32 MontageIndex, float PlayRate, ERole Role);

	/** An action is interrupted now on this process: its stop time, the montage stop, the timer path's stop. */
	void InterruptAction(int32 CharacterIndex, int32 ActionIndex);

	/** Server: plays the planned action now: montage, timers, records, and (network runs) tells the clients. */
	void StartAction(int32 CharacterIndex);

	/** Server: interrupts the current action now. */
	void StopAction(int32 CharacterIndex);

	/** The timer path, started with an action: one timer per key, at the authored position divided by the play rate. */
	void StartTimerPath(int32 CharacterIndex, int32 ActionIndex);

	/** The timer path at an interruption: cancel what is pending, close what is open (as hand-written code would). */
	void StopTimerPath(int32 CharacterIndex, int32 ActionIndex);

	/** The key of this montage that a fire names, or INDEX_NONE (another montage, or no such key). */
	int32 FindKey(int32 MontageIndex, const UAnimSequenceBase* Animation, FName EventName, EAECEventPhase EventPhase) const;

	/** Client: the game time after which no more fires of this action can come (its end or stop, plus a grace). */
	double LastFireTime(const FAction& Action) const;

	/** The markers' comparison hook: the animation path. */
	void HandleMarkerFiredByAnimation(const USkeletalMeshComponent* MeshComp, const UAnimSequenceBase* Animation, FName EventName, EAECEventPhase EventPhase);

	/** A timer-path key came due. */
	void HandleTimerKey(int32 CharacterIndex, int32 ActionIndex, int32 KeyIndex);

	/** Records a fire now (and counts it for this frame's CSV stat). */
	void RecordFire(int32 ActionIndex, int32 KeyIndex, EPath Path);

	/** Records a fire with the times it happened at (a client's held fires). */
	void RecordFire(int32 ActionIndex, int32 KeyIndex, EPath Path, double GameTime, double WallTime, uint64 Frame);

	/** Client: the server connection failed before the run ended; quit with exit code 5 (with -AECExit). */
	void HandleNetworkFailure(UWorld* FailedWorld, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString);

	/** Server of a network run: tells every remote player's client that the run is over. */
	void NotifyClientsFinished() const;

	/** Writes <RunTag>_actions.csv, _events.csv, (not on a client) _expected.csv and (network runs) _frames.csv to
	 *  Saved/AECRuns/. Returns false if any write failed. */
	bool WriteRunFiles() const;

	/** Frames since OnWorldBeginPlay (start-up and loading frames excluded). */
	uint64 RunFrame() const { return GFrameCounter - BeginPlayFrame; }

	/** A hash of every fire (action, key, path, run frame), order-independent within a frame, for the determinism check. */
	uint32 HashFires() const;

	FRunSettings Settings;
	ERunPhase Phase = ERunPhase::Idle;
	double BeginPlayTime = 0.0;
	uint64 BeginPlayFrame = 0;
	double WarmupStartTime = 0.0;
	double BenchStartTime = 0.0;
	double CaptureEndTime = 0.0;
	double DrainEndTime = 0.0;
	double ExitNotBefore = 0.0;
	int32 FramesSinceDone = 0;
	uint8 ExitCode = 0;
	int32 FiresThisFrame = 0;
	int32 UnattributedFires = 0;
	int32 RemotePlayers = 0;
	bool bNetworked = false;   // the world's net mode is not standalone (fixed at begin play)
	bool bGas = false;         // -AECConfig=G
	FDelegateHandle MarkerHookHandle;
	FDelegateHandle NetworkFailureHandle;

	bool bUseUpdateRateOptimizations = false;
	bool bRootMotionFromEverything = false;
	bool bRegisterWithBudget = false;
	bool bOnlyTickWhenRendered = false;

	TArray<FMontageInfo> MontageInfos;
	TArray<FCharacterState> States;
	TArray<FAction> Actions;
	TArray<FFire> Fires;
	TArray<FFrameStamp> FrameStamps;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UAnimMontage>> Montages;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AAECDemoCharacter>> Characters;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> Camera;
};
