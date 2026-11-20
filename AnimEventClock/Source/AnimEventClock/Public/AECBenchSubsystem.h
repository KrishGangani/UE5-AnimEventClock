// P06 AnimEventClock (demo) - the benchmark director: spawns the seeded workload, plays the actions, records every event.

#pragma once

#include "AECEventTypes.h"
#include "Engine/TimerHandle.h"
#include "Math/RandomStream.h"
#include "Subsystems/WorldSubsystem.h"
#include "AECBenchSubsystem.generated.h"

class AAECDemoCharacter;
class ACameraActor;
class UAnimMontage;
class UAnimSequenceBase;
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
 *   - the ANIMATION path (configuration NQ or NB: which montage variant plays), through the markers' comparison hook;
 *   - the TIMER path (configuration T): FTimerManager timers set at the action's start from the authored positions.
 * The expected events (with their due times: start + position / play rate) are written alongside, so a script can measure
 * timing error and missed / duplicate events per path without trusting either path.
 *
 * Timeline (world time since begin play):
 *   [0, Warmup)                 warm-up: actions already running, not counted
 *   Warmup                      CSV_EVENT "BenchStart"; actions started from now on are "in capture"
 *   Warmup + Capture            CSV_EVENT "BenchEnd"; no new actions start
 *   after the last action ends  + 1 s grace for late events, then the files are written; with -AECExit the game quits
 *
 * Command line (all optional; -AECChars=0, the default, means "no workload"):
 *   -AECChars=N          characters to spawn (split evenly across the three bands)
 *   -AECSeed=S           seed (default 1)
 *   -AECWarmup=SEC       warm-up seconds (default 5)
 *   -AECCapture=SEC      capture seconds (default 60)
 *   -AECConfig=NQ|NB     montage variant: Queued or BranchingPoint markers (default NQ)
 *   -AECCond=NAME        def | uro | uro2 | uro4 | la4 | rme | aba2 | aba4 | vis   (default def; 01 §6.2; rme added in S3)
 *   -AECStart=post|pre   where actions start: this Tick, after the meshes ticked (post, default), or the character's tick
 *                        before its mesh (pre: the montage then advances a whole frame on its first update; 01 H8)
 *   -AECRunTag=NAME      output file prefix (default "run"); files go to Saved/AECRuns/
 *   -AECExit             quit when done (exit code 0; 3 if the run could not be set up; 4 if its files could not be written)
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
		WarmUp,
		Capture,
		Draining,
		Done
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

	/** One played action. */
	struct FAction
	{
		int32 Character = INDEX_NONE;
		int32 Montage = INDEX_NONE;
		float PlayRate = 1.f;
		double StartGameTime = 0.0;
		double StartWallTime = 0.0;
		uint64 StartFrame = 0;
		double StopGameTime = -1.0;      // < 0: not interrupted
		bool bInCapture = false;
	};

	/** One reported event. */
	struct FFire
	{
		int32 Action = INDEX_NONE;
		int32 Key = INDEX_NONE;
		EPath Path = EPath::Animation;
		double GameTime = 0.0;
		double WallTime = 0.0;
		uint64 Frame = 0;
	};

	/** Per-character schedule and the timer path's state. */
	struct FCharacterState
	{
		int32 Band = 0;
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
	};

	static FRunSettings ReadRunSettings();
	static double WallSeconds();

	/** Applies the -AECCond settings that must be in place before any character exists. Returns false if unknown. */
	bool PrepareCondition();

	/** Sets an int console variable for this run only (temporary priority, tagged; undone in Deinitialize). */
	static bool SetConsoleVariable(const TCHAR* Name, int32 Value);

	/** Cancels a character's pending timer-path timers. */
	void ClearTimers(FCharacterState& State) const;

	/** Loads the run variant's two demo montages and reads their keys. Returns false if any is missing or has no keys. */
	bool LoadMontages();

	/** Spawns the characters into their bands (deferred, so per-condition mesh settings are applied before BeginPlay).
	 *  Returns false if any spawn failed: the run is then not started, so indices always match. */
	bool SpawnCharacters(UWorld& World);

	/** Plans the next action of one character (montage, play rate, interruption) from its own random stream. */
	void PlanNextAction(int32 CharacterIndex, double Now);

	/** Plays the planned action now: montage, timers, records. */
	void StartAction(int32 CharacterIndex);

	/** Interrupts the current action now. */
	void StopAction(int32 CharacterIndex);

	/** The markers' comparison hook: the animation path. */
	void HandleMarkerFiredByAnimation(const USkeletalMeshComponent* MeshComp, const UAnimSequenceBase* Animation, FName EventName, EAECEventPhase EventPhase);

	/** A timer-path key came due. */
	void HandleTimerKey(int32 CharacterIndex, int32 ActionIndex, int32 KeyIndex);

	void RecordFire(int32 ActionIndex, int32 KeyIndex, EPath Path);

	/** Writes <RunTag>_actions.csv, _expected.csv and _events.csv to Saved/AECRuns/. Returns false if any write failed. */
	bool WriteRunFiles() const;

	/** Frames since OnWorldBeginPlay (start-up and loading frames excluded). */
	uint64 RunFrame() const { return GFrameCounter - BeginPlayFrame; }

	/** A hash of every fire (action, key, path, run frame), order-independent within a frame, for the determinism check. */
	uint32 HashFires() const;

	FRunSettings Settings;
	ERunPhase Phase = ERunPhase::Idle;
	double BeginPlayTime = 0.0;
	uint64 BeginPlayFrame = 0;
	double BenchStartTime = 0.0;
	double CaptureEndTime = 0.0;
	double DrainEndTime = 0.0;
	int32 FramesSinceDone = 0;
	uint8 ExitCode = 0;
	int32 FiresThisFrame = 0;
	int32 UnattributedFires = 0;
	FDelegateHandle MarkerHookHandle;

	bool bUseUpdateRateOptimizations = false;
	bool bRootMotionFromEverything = false;
	bool bRegisterWithBudget = false;
	bool bOnlyTickWhenRendered = false;

	TArray<FMontageInfo> MontageInfos;
	TArray<FCharacterState> States;
	TArray<FAction> Actions;
	TArray<FFire> Fires;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UAnimMontage>> Montages;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AAECDemoCharacter>> Characters;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> Camera;
};
