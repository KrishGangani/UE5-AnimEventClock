// P06 AnimEventClock (demo) - UAECBenchSubsystem.

#include "AECBenchSubsystem.h"

#include "AECDemoCharacter.h"
#include "AECDemoGameMode.h"
#include "AECDemoGasCharacter.h"
#include "AECDemoMontages.h"
#include "AECDemoPlayerController.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Camera/CameraActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "IAnimationBudgetAllocator.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "SkeletalMeshComponentBudgeted.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AECBenchSubsystem)

DEFINE_LOG_CATEGORY_STATIC(LogAECBench, Log, All);

// CSV category for the run's events (BenchStart / BenchEnd) and the per-frame fire count.
CSV_DEFINE_CATEGORY(AECBench, true);

namespace AECBench
{
	/** The camera: 1.2 m up at the origin, looking along +X, so the three bands are in front, far in front and behind. */
	const FVector CameraLocation(0.0, 0.0, 120.0);

	/** Character centres stand at the capsule's half height above the floor (z = 0). */
	constexpr double CharacterHeight = 90.0;

	/** Bands (cm): near and visible, far and visible (small on screen), behind the camera (not rendered). */
	struct FBandBox { double MinX, MaxX, HalfY; };
	constexpr FBandBox Bands[3] = { { 400.0, 900.0, 300.0 }, { 4000.0, 7000.0, 1500.0 }, { -1200.0, -400.0, 300.0 } };

	/** Remote players' characters: band 3, in front of the camera and nearer than band 0, one metre apart. */
	constexpr int32 PlayerBand = 3;
	const FVector PlayerLocation(300.0, 0.0, CharacterHeight);
	constexpr double PlayerSpacing = 100.0;

	/** The band of character Index: NPCs take turns across the three bands; players have their own. */
	int32 BandOf(int32 Index, bool bPlayer) { return bPlayer ? PlayerBand : Index % 3; }

	/** The schedule (01 §6.1): wait between actions, the montage mix, play rates, how often and where an action is cut. */
	constexpr float MinWaitSeconds = 0.5f;
	constexpr float MaxWaitSeconds = 1.5f;
	constexpr float AttackShare = 0.7f;
	constexpr float FastPlayRate = 1.3f;
	constexpr float InterruptShare = 0.1f;
	constexpr float MinInterruptFraction = 0.2f;
	constexpr float MaxInterruptFraction = 0.8f;
	constexpr float StopBlendOutSeconds = 0.1f;

	/** After the last action ends: time for late events to arrive, then frames for the CSV profiler to finish its file. */
	constexpr double DrainGraceSeconds = 1.0;
	constexpr int32 ExitDelayFrames = 3;

	/** Network runs: how long a server waits for its players; how long it outlives its clients at the end (so they quit
	 *  first: a client whose server vanished would travel to the default map instead of quitting); frames reserved per
	 *  second of run for the per-frame log (above any frame cap the runs use). */
	constexpr double PlayerWaitSeconds = 120.0;
	constexpr double ServerExitDelaySeconds = 3.0;
	constexpr double FrameStampsPerSecond = 250.0;

	/** Client: how long after its end (or stop + blend-out) an action may still fire (late Queued ends). */
	constexpr double LateFireGraceSeconds = 0.25;

	/** Exit codes: the run could not be set up (missing montage, unknown condition, a failed spawn, no player); its files
	 *  could not be written; a client lost its server before the run ended. */
	constexpr uint8 SetupFailedExitCode = 3;
	constexpr uint8 WriteFailedExitCode = 4;
	constexpr uint8 LostServerExitCode = 5;

	/** The tag on every console variable a run sets, so Deinitialize can undo exactly those (PIE runs share a process). */
	const FName ConsoleVariableTag(TEXT("AECBench"));

	const TCHAR* StartOrderName(bool bStartPre) { return bStartPre ? TEXT("pre") : TEXT("post"); }

	FString FixedStepText() { return FApp::UseFixedTimeStep() ? FString::SanitizeFloat(FApp::GetFixedDeltaTime()) : FString(TEXT("off")); }

	const TCHAR* PhaseName(EAECEventPhase Phase)
	{
		switch (Phase)
		{
		case EAECEventPhase::WindowBegin: return TEXT("begin");
		case EAECEventPhase::WindowEnd:   return TEXT("end");
		default:                          return TEXT("instant");
		}
	}
}

bool UAECBenchSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

UAECBenchSubsystem::FRunSettings UAECBenchSubsystem::ReadRunSettings()
{
	const TCHAR* CommandLine = FCommandLine::Get();
	FRunSettings Read;
	FParse::Value(CommandLine, TEXT("AECChars="), Read.NumCharacters);
	FParse::Value(CommandLine, TEXT("AECSeed="), Read.Seed);
	FParse::Value(CommandLine, TEXT("AECWarmup="), Read.WarmupSeconds);
	FParse::Value(CommandLine, TEXT("AECCapture="), Read.CaptureSeconds);
	FParse::Value(CommandLine, TEXT("AECConfig="), Read.Config);
	FParse::Value(CommandLine, TEXT("AECCond="), Read.Condition);
	FString Start;
	if (FParse::Value(CommandLine, TEXT("AECStart="), Start))
	{
		Read.bStartPre = (Start == TEXT("pre"));
	}
	FParse::Value(CommandLine, TEXT("AECPlayers="), Read.Players);
	Read.bClient = FParse::Param(CommandLine, TEXT("AECClient"));
	Read.bClock = FParse::Param(CommandLine, TEXT("AECClock"));
	FParse::Value(CommandLine, TEXT("AECRunTag="), Read.RunTag);
	Read.bExitWhenDone = FParse::Param(CommandLine, TEXT("AECExit"));
	return Read;
}

double UAECBenchSubsystem::WallSeconds()
{
	// The raw, system-wide performance counter: comparable across processes on one machine (raw notes E0).
	return FPlatformTime::ToSeconds64(FPlatformTime::Cycles64());
}

double UAECBenchSubsystem::FrameStartWallSeconds()
{
	// FApp::GetCurrentTime() is the FPlatformTime::Seconds() reading this frame's delta time was measured from
	// (UnrealEngine.cpp:3127-3138, real time when no fixed step is used), so it is the moment this frame's game time
	// stands for. FPlatformTime::Seconds() adds a constant to the raw counter (WindowsPlatformTime.h:29, 46-49; 0 on
	// other platforms); taking it off puts the value on WallSeconds()'s base.
	return FApp::GetCurrentTime() - FPlatformTime::GetSecondsTimeOffset();
}

const TCHAR* UAECBenchSubsystem::NetModeName() const
{
	switch (GetWorld()->GetNetMode())
	{
	case NM_ListenServer:    return TEXT("listen");
	case NM_DedicatedServer: return TEXT("dedicated");
	case NM_Client:          return TEXT("client");
	default:                 return TEXT("standalone");
	}
}

void UAECBenchSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	Settings = ReadRunSettings();
	const bool bClientWorld = (InWorld.GetNetMode() == NM_Client);
	const bool bRecorder = Settings.bClient && bClientWorld;
	const bool bDirector = !Settings.bClient && !bClientWorld && Settings.NumCharacters > 0;
	if (!bRecorder && !bDirector)
	{
		return; // No workload requested for this world (or a client's world before it connected): a normal empty map.
	}

	BeginPlayTime = InWorld.GetTimeSeconds();
	BeginPlayFrame = GFrameCounter;
	bNetworked = (InWorld.GetNetMode() != NM_Standalone);
	bGas = (Settings.Config == TEXT("G"));

	const bool bConfigKnown = (Settings.Config == TEXT("NQ") || Settings.Config == TEXT("NB") || Settings.Config == TEXT("G"));
	bool bReady = bConfigKnown && PrepareCondition() && LoadMontages();
	if (bRecorder && Settings.Condition != TEXT("def"))
	{
		UE_LOG(LogAECBench, Error, TEXT("A client records with the default condition only (v1); got -AECCond=%s."), *Settings.Condition);
		bReady = false;
	}
	if (bDirector && Settings.Players > 0 && !Cast<AAECDemoGameMode>(InWorld.GetAuthGameMode()))
	{
		UE_LOG(LogAECBench, Error, TEXT("-AECPlayers needs the demo game mode: open the map with ?game=/Script/AnimEventClock.AECDemoGameMode."));
		bReady = false;
	}
	if (bDirector)
	{
		bReady = bReady && Settings.CaptureSeconds > 0.0 && Settings.WarmupSeconds >= 0.0 && SpawnCharacters(InWorld);
	}
	if (!bReady)
	{
		UE_LOG(LogAECBench, Error, TEXT("Run not started: config %s, condition %s, warm-up %.2f s, capture %.2f s (see errors above)."),
			*Settings.Config, *Settings.Condition, Settings.WarmupSeconds, Settings.CaptureSeconds);
		ExitCode = AECBench::SetupFailedExitCode;
		Phase = ERunPhase::Done;
		return;
	}

	MarkerHookHandle = AnimEventClock::OnMarkerFiredByAnimation().AddUObject(this, &UAECBenchSubsystem::HandleMarkerFiredByAnimation);
	if (InWorld.GetNetMode() != NM_DedicatedServer)
	{
		Camera = InWorld.SpawnActor<ACameraActor>(AECBench::CameraLocation, FRotator::ZeroRotator);
	}
	if (bNetworked)
	{
		FrameStamps.Reserve(FMath::CeilToInt32((AECBench::PlayerWaitSeconds + Settings.WarmupSeconds + Settings.CaptureSeconds + 30.0) * AECBench::FrameStampsPerSecond));
	}

	if (bRecorder)
	{
		ReserveRecords(Settings.NumCharacters + Settings.Players);
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &UAECBenchSubsystem::HandleNetworkFailure);
		Phase = ERunPhase::Recording;
		UE_LOG(LogAECBench, Display, TEXT("Client recorder %s: config %s"), *Settings.RunTag, *Settings.Config);
	}
	else if (Settings.Players > 0)
	{
		Phase = ERunPhase::WaitingForPlayers;
		UE_LOG(LogAECBench, Display, TEXT("Waiting for %d remote player(s) (%s server)"), Settings.Players, NetModeName());
	}
	else
	{
		BeginWarmUp(BeginPlayTime);
	}

	UE_LOG(LogAECBench, Display, TEXT("Run %s: %d characters, seed %d, config %s, condition %s, start %s, warm-up %.1f s, capture %.1f s, fixed step %s, net %s"),
		*Settings.RunTag, Characters.Num(), Settings.Seed, *Settings.Config, *Settings.Condition, AECBench::StartOrderName(Settings.bStartPre),
		Settings.WarmupSeconds, Settings.CaptureSeconds, *AECBench::FixedStepText(), NetModeName());
	CSV_METADATA(TEXT("AECRunTag"), *Settings.RunTag);
	CSV_METADATA(TEXT("AECConfig"), *Settings.Config);
	CSV_METADATA(TEXT("AECCond"), *Settings.Condition);
}

void UAECBenchSubsystem::Deinitialize()
{
	AnimEventClock::OnMarkerFiredByAnimation().Remove(MarkerHookHandle);
	if (GEngine)
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
	}
	// Removes exactly the values this run set; whatever was there before (config, device profile) applies again.
	IConsoleManager::Get().UnsetAllConsoleVariablesWithTag(AECBench::ConsoleVariableTag, ECVF_SetByTemp);
	Super::Deinitialize();
}

bool UAECBenchSubsystem::SetConsoleVariable(const TCHAR* Name, int32 Value)
{
	IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
	if (!Variable)
	{
		UE_LOG(LogAECBench, Error, TEXT("Console variable %s not found."), Name);
		return false;
	}
	Variable->Set(Value, ECVF_SetByTemp, AECBench::ConsoleVariableTag);
	return true;
}

void UAECBenchSubsystem::ClearTimers(FCharacterState& State) const
{
	FTimerManager& Timers = GetWorld()->GetTimerManager();
	for (FTimerHandle& Handle : State.Timers)
	{
		Timers.ClearTimer(Handle);
	}
}

bool UAECBenchSubsystem::PrepareCondition()
{
	const FString& Condition = Settings.Condition;
	if (Condition == TEXT("def"))
	{
		return true;
	}
	if (Condition == TEXT("uro"))
	{
		bUseUpdateRateOptimizations = true;   // the engine's own rates: by screen size, 4 when not rendered
		return true;
	}
	if (Condition == TEXT("uro2") || Condition == TEXT("uro4") || Condition == TEXT("la4"))
	{
		bUseUpdateRateOptimizations = true;
		bRootMotionFromEverything = (Condition == TEXT("la4"));   // look-ahead mode needs it (SkinnedMeshComponent.cpp:360)
		return SetConsoleVariable(TEXT("a.URO.ForceAnimRate"), Condition == TEXT("uro2") ? 2 : 4);
	}
	if (Condition == TEXT("rme"))
	{
		bRootMotionFromEverything = true;   // la4's root-motion mode without URO, to tell the two causes apart (S3)
		return true;
	}
	if (Condition == TEXT("aba2") || Condition == TEXT("aba4"))
	{
		// The allocator works only with both switches on: the global cvar (default 0, AnimationBudgetAllocatorCVars.cpp:7)
		// and this world's allocator, which starts disabled (AnimationBudgetAllocator.cpp:71; both tested at :1191, :1366). The
		// cvars alone change nothing: S3's first batch gave the aba runs the same fire hash as def. Both are switched on
		// before any budgeted mesh exists, as the cvar's help asks; then a forced rate for exact, repeatable runs.
		IAnimationBudgetAllocator* Allocator = IAnimationBudgetAllocator::Get(GetWorld());
		if (!Allocator)
		{
			UE_LOG(LogAECBench, Error, TEXT("No animation budget allocator for this world."));
			return false;
		}
		bRegisterWithBudget = true;
		if (!SetConsoleVariable(TEXT("a.Budget.Enabled"), 1)
			|| !SetConsoleVariable(TEXT("a.Budget.Debug.Force"), 1)
			|| !SetConsoleVariable(TEXT("a.Budget.Debug.Force.Rate"), Condition == TEXT("aba2") ? 2 : 4))
		{
			return false;
		}
		Allocator->SetEnabled(true);   // after the cvar: enabling reads it (AnimationBudgetAllocator.cpp:1328-1332, 1363-1366)
		return true;
	}
	if (Condition == TEXT("vis"))
	{
		bOnlyTickWhenRendered = true;
		return true;
	}
	UE_LOG(LogAECBench, Error, TEXT("Unknown condition -AECCond=%s."), *Condition);
	return false;
}

bool UAECBenchSubsystem::LoadMontages()
{
	const bool bBranchingPoint = (Settings.Config == TEXT("NB"));   // G plays the Queued variant, like NQ
	for (const AECDemo::FMontageSpec& Spec : AECDemo::GetMontageSpecs())
	{
		const FString Path = AECDemo::GetMontageObjectPath(Spec, bBranchingPoint);
		UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *Path);
		if (!Montage)
		{
			UE_LOG(LogAECBench, Error, TEXT("Montage not found: %s (run Tools\\make_assets.bat)."), *Path);
			return false;
		}

		// The keys come from the event clock's own timeline (S5): the montage's notifies at their trigger times, sorted,
		// windows paired. One reader for both, so the expected events are exactly the ones the clock will fire.
		FMontageInfo& Info = MontageInfos.AddDefaulted_GetRef();
		Info.Name = Montage->GetName();
		Info.Length = Montage->GetPlayLength();
		Info.Keys = FAECMontageTimeline::Build(*Montage).GetKeys();
		if (Info.Keys.IsEmpty() || Info.Length <= 0.f)
		{
			UE_LOG(LogAECBench, Error, TEXT("Montage %s has no AEC markers or no length."), *Path);
			return false;
		}
		Montages.Add(Montage);
		UE_LOG(LogAECBench, Display, TEXT("Loaded %s: %.4f s, %d keys"), *Info.Name, Info.Length, Info.Keys.Num());
	}
	return true;
}

AAECDemoCharacter* UAECBenchSubsystem::SpawnCharacter(UWorld& World, const FTransform& Transform, bool bPlayer)
{
	const int32 Index = Characters.Num();
	const TSubclassOf<AAECDemoCharacter> Class = bGas ? AAECDemoGasCharacter::StaticClass() : AAECDemoCharacter::StaticClass();

	// Deferred spawn: the mesh settings of the condition are in place before BeginPlay registers anything.
	AAECDemoCharacter* Character = World.SpawnActorDeferred<AAECDemoCharacter>(Class, Transform, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Character)
	{
		UE_LOG(LogAECBench, Error, TEXT("Spawning character %d failed."), Index);
		return nullptr;
	}
	Character->SetBenchIndex(Index);
	Character->SetPlayerCharacter(bPlayer);
	Character->PrimaryActorTick.bStartWithTickEnabled = Settings.bStartPre;   // its tick only runs "pre" starts
	USkeletalMeshComponent* Mesh = Character->GetMesh();
	Mesh->bEnableUpdateRateOptimizations = bUseUpdateRateOptimizations;
	if (bOnlyTickWhenRendered)
	{
		Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	}
	if (bRegisterWithBudget)
	{
		CastChecked<USkeletalMeshComponentBudgeted>(Mesh)->SetAutoRegisterWithBudgetAllocator(true);
	}
	if (Settings.bClock)
	{
		Character->GetClock()->bDriveMontage = false;   // the NQ/NB path plays the montage; the clock only fires events
		Character->GetClock()->OnClockEvent.AddDynamic(this, &UAECBenchSubsystem::HandleClockEvent);
	}
	Character->FinishSpawning(Transform);

	if (bRootMotionFromEverything)
	{
		if (UAnimInstance* AnimInstance = Mesh->GetAnimInstance())
		{
			AnimInstance->RootMotionMode = ERootMotionMode::RootMotionFromEverything;
		}
	}

	// Placement and schedule each get their own stream per character, so changing N or the schedule of one character
	// never moves another (and the same seed always gives the same run).
	Characters.Add(Character);
	FCharacterState& State = States.AddDefaulted_GetRef();
	State.Band = AECBench::BandOf(Index, bPlayer);
	State.Random.Initialize(Settings.Seed * 104729 + Index);
	return Character;
}

bool UAECBenchSubsystem::SpawnCharacters(UWorld& World)
{
	const int32 MaxCharacters = Settings.NumCharacters + Settings.Players;
	Characters.Reserve(MaxCharacters);
	States.Reserve(MaxCharacters);

	for (int32 Index = 0; Index < Settings.NumCharacters; ++Index)
	{
		FRandomStream Placement(Settings.Seed * 7919 + Index);
		const int32 Band = AECBench::BandOf(Index, false);
		const AECBench::FBandBox& Box = AECBench::Bands[Band];
		const FVector Location(Placement.FRandRange(Box.MinX, Box.MaxX), Placement.FRandRange(-Box.HalfY, Box.HalfY), AECBench::CharacterHeight);
		const FTransform Transform(FRotator(0.0, 180.0, 0.0), Location);   // facing the camera
		if (!SpawnCharacter(World, Transform, false))
		{
			return false;
		}
	}
	ReserveRecords(MaxCharacters);
	return true;
}

void UAECBenchSubsystem::ReserveRecords(int32 MaxCharacters)
{
	// An action takes at least the shortest montage at the fastest rate plus the shortest wait; each action records at
	// most 2 fires per key.
	const float ShortestLength = FMath::Min(MontageInfos[0].Length, MontageInfos.Last().Length);
	const double ShortestCycle = AECBench::MinWaitSeconds + ShortestLength / AECBench::FastPlayRate;
	const int32 MaxKeys = FMath::Max(MontageInfos[0].Keys.Num(), MontageInfos.Last().Keys.Num());
	const int32 ActionsPerCharacter = FMath::CeilToInt32((Settings.WarmupSeconds + Settings.CaptureSeconds) / ShortestCycle) + 2;
	Actions.Reserve(MaxCharacters * ActionsPerCharacter);
	Fires.Reserve(MaxCharacters * ActionsPerCharacter * MaxKeys * 2);
}

void UAECBenchSubsystem::AddRemotePlayer(APlayerController& Player)
{
	if (Phase == ERunPhase::Idle || Phase == ERunPhase::Recording || Phase == ERunPhase::Done)
	{
		return;   // no workload in this world, or its setup failed
	}

	UWorld* World = GetWorld();
	const FVector Location = AECBench::PlayerLocation + FVector(0.0, AECBench::PlayerSpacing * RemotePlayers, 0.0);
	const FTransform Transform(FRotator(0.0, 180.0, 0.0), Location);
	AAECDemoCharacter* Character = SpawnCharacter(*World, Transform, true);
	if (!Character)
	{
		return;   // logged; the server keeps waiting and fails at its player timeout
	}
	Player.Possess(Character);
	++RemotePlayers;

	// A player who joins after the warm-up started gets its schedule now; before that, BeginWarmUp plans everyone.
	const int32 Index = Characters.Num() - 1;
	if (Phase == ERunPhase::WarmUp || Phase == ERunPhase::Capture)
	{
		PlanNextAction(Index, World->GetTimeSeconds());
	}
	UE_LOG(LogAECBench, Display, TEXT("Remote player %d joined at %.3f s: character %d"), RemotePlayers, World->GetTimeSeconds(), Index);
}

void UAECBenchSubsystem::BeginWarmUp(double Now)
{
	WarmupStartTime = Now;
	for (int32 Index = 0; Index < States.Num(); ++Index)
	{
		PlanNextAction(Index, Now);
	}
	Phase = ERunPhase::WarmUp;
}

void UAECBenchSubsystem::PlanNextAction(int32 CharacterIndex, double Now)
{
	// Always the same number of draws per plan, so one character's stream advances identically in every run.
	FCharacterState& State = States[CharacterIndex];
	FRandomStream& Random = State.Random;
	const float Wait = Random.FRandRange(AECBench::MinWaitSeconds, AECBench::MaxWaitSeconds);
	const float MontageDraw = Random.FRand();
	const float RateDraw = Random.FRand();
	const float InterruptDraw = Random.FRand();
	const float InterruptFraction = Random.FRandRange(AECBench::MinInterruptFraction, AECBench::MaxInterruptFraction);

	State.NextStartTime = Now + Wait;
	State.PlannedMontage = (MontageDraw < AECBench::AttackShare) ? 0 : 1;
	State.PlannedPlayRate = (RateDraw < 0.5f) ? 1.f : AECBench::FastPlayRate;
	State.PlannedStopFraction = (InterruptDraw < AECBench::InterruptShare) ? InterruptFraction : -1.0;
}

void UAECBenchSubsystem::ExecuteRequestedStart(AAECDemoCharacter& Character)
{
	const int32 Index = Character.GetBenchIndex();
	if (!States.IsValidIndex(Index) || !States[Index].bWaitingForStart)
	{
		return;
	}
	States[Index].bWaitingForStart = false;
	if (Phase == ERunPhase::WarmUp || Phase == ERunPhase::Capture)
	{
		StartAction(Index);
	}
}

int32 UAECBenchSubsystem::BeginAction(int32 CharacterIndex, int32 Id, int32 MontageIndex, float PlayRate, ERole Role)
{
	FAction& Action = Actions.AddDefaulted_GetRef();
	const int32 ActionIndex = Actions.Num() - 1;
	Action.Id = Id;
	Action.Character = CharacterIndex;
	Action.Montage = MontageIndex;
	Action.Role = Role;
	Action.PlayRate = PlayRate;
	Action.StartGameTime = GetWorld()->GetTimeSeconds();
	Action.StartWallTime = WallSeconds();
	Action.StartFrame = RunFrame();
	Action.bInCapture = (Phase == ERunPhase::Capture);

	AAECDemoCharacter* Character = Characters[CharacterIndex].Get();
	if (PlaysClock() && IsValid(Character))
	{
		// Before the current action changes: a clock action still running (after a hitch) ends inside this call, and its
		// last events are still attributed to it.
		Action.ClockActionId = Character->GetClock()->PlayAction(Montages[MontageIndex], PlayRate);
	}
	States[CharacterIndex].CurrentAction = ActionIndex;
	if (!IsValid(Character) || !Character->PlayActionMontage(*Montages[MontageIndex], PlayRate))
	{
		UE_LOG(LogAECBench, Error, TEXT("Action %d: the montage did not start on character %d."), Id, CharacterIndex);
	}
	StartTimerPath(CharacterIndex, ActionIndex);
	return ActionIndex;
}

void UAECBenchSubsystem::InterruptAction(int32 CharacterIndex, int32 ActionIndex)
{
	FAction& Action = Actions[ActionIndex];
	Action.StopGameTime = GetWorld()->GetTimeSeconds();
	if (AAECDemoCharacter* Character = Characters[CharacterIndex].Get(); IsValid(Character))
	{
		Character->StopActionMontage(*Montages[Action.Montage], AECBench::StopBlendOutSeconds);
		if (PlaysClock())
		{
			Character->GetClock()->StopAction();   // closes an open window at the stop, as the expected events say
		}
	}
	StopTimerPath(CharacterIndex, ActionIndex);
}

void UAECBenchSubsystem::StartAction(int32 CharacterIndex)
{
	AAECDemoCharacter* Character = Characters.IsValidIndex(CharacterIndex) ? Characters[CharacterIndex].Get() : nullptr;
	if (!IsValid(Character) || !Character->GetMesh()->GetAnimInstance())
	{
		return;
	}

	FCharacterState& State = States[CharacterIndex];
	const ERole Role = Character->IsPlayerCharacter() ? ERole::RemotePlayer : ERole::Npc;
	const int32 ActionIndex = BeginAction(CharacterIndex, Actions.Num(), State.PlannedMontage, State.PlannedPlayRate, Role);
	const FAction& Action = Actions[ActionIndex];
	if (bNetworked)
	{
		Character->MulticastStartAction(Action.Id, static_cast<uint8>(Action.Montage), Action.PlayRate);
	}

	const double Duration = MontageInfos[Action.Montage].Length / Action.PlayRate;
	State.bPlaying = true;
	State.PlannedEndTime = Action.StartGameTime + Duration;
	State.PlannedStopTime = (State.PlannedStopFraction >= 0.0) ? Action.StartGameTime + State.PlannedStopFraction * Duration : -1.0;
}

void UAECBenchSubsystem::StopAction(int32 CharacterIndex)
{
	const int32 ActionIndex = States[CharacterIndex].CurrentAction;
	InterruptAction(CharacterIndex, ActionIndex);
	if (bNetworked && IsValid(Characters[CharacterIndex]))
	{
		Characters[CharacterIndex]->MulticastStopAction(Actions[ActionIndex].Id);
	}
}

void UAECBenchSubsystem::StartTimerPath(int32 CharacterIndex, int32 ActionIndex)
{
	FCharacterState& State = States[CharacterIndex];
	const FAction& Action = Actions[ActionIndex];
	const TArray<FKey>& Keys = MontageInfos[Action.Montage].Keys;

	ClearTimers(State);
	FTimerManager& Timers = GetWorld()->GetTimerManager();
	State.Timers.SetNum(Keys.Num());
	State.TimerWindowOpen.Init(false, Keys.Num());
	for (int32 Key = 0; Key < Keys.Num(); ++Key)
	{
		const float Delay = Keys[Key].Position / Action.PlayRate;
		Timers.SetTimer(State.Timers[Key], FTimerDelegate::CreateUObject(this, &UAECBenchSubsystem::HandleTimerKey, CharacterIndex, ActionIndex, Key),
			FMath::Max(Delay, UE_KINDA_SMALL_NUMBER), false);
	}
}

void UAECBenchSubsystem::StopTimerPath(int32 CharacterIndex, int32 ActionIndex)
{
	FCharacterState& State = States[CharacterIndex];
	ClearTimers(State);
	const TArray<FKey>& Keys = MontageInfos[Actions[ActionIndex].Montage].Keys;
	for (int32 Key = 0; Key < Keys.Num(); ++Key)
	{
		if (State.TimerWindowOpen[Key])
		{
			State.TimerWindowOpen[Key] = false;
			RecordFire(ActionIndex, Keys[Key].PairKey, EPath::Timer);
		}
	}
}

void UAECBenchSubsystem::RegisterReplicatedCharacter(AAECDemoCharacter& Character)
{
	const int32 Index = Character.GetBenchIndex();
	if (Phase != ERunPhase::Recording || Index < 0)
	{
		return;
	}
	if (Index >= Characters.Num())
	{
		Characters.SetNum(Index + 1);
		States.SetNum(Index + 1);
	}
	Characters[Index] = &Character;
	States[Index].Band = AECBench::BandOf(Index, Character.IsPlayerCharacter());
}

void UAECBenchSubsystem::HandleServerStart(AAECDemoCharacter& Character, int32 ActionId, uint8 MontageIndex, float PlayRate)
{
	const int32 Index = Character.GetBenchIndex();
	if (Phase != ERunPhase::Recording || !Characters.IsValidIndex(Index) || Characters[Index] != &Character || !MontageInfos.IsValidIndex(MontageIndex))
	{
		return;
	}

	// The role is read when the action starts: by then a player's character has been possessed for the whole warm-up.
	const ERole Role = (Character.GetLocalRole() == ROLE_AutonomousProxy) ? ERole::AutonomousProxy : ERole::SimulatedProxy;
	const int32 ActionIndex = BeginAction(Index, ActionId, MontageIndex, PlayRate, Role);

	// Fires that arrived before this start (a G montage can reach a simulated proxy before the start message) belong here,
	// with the times they really fired at.
	for (const FHeldFire& Held : States[Index].HeldFires)
	{
		const int32 Key = FindKey(MontageIndex, Held.Animation, Held.Name, Held.Phase);
		if (Key == INDEX_NONE)
		{
			++UnattributedFires;
			continue;
		}
		RecordFire(ActionIndex, Key, EPath::Animation, Held.GameTime, Held.WallTime, Held.Frame);
	}
	States[Index].HeldFires.Reset();
}

void UAECBenchSubsystem::HandleServerStop(AAECDemoCharacter& Character, int32 ActionId)
{
	const int32 Index = Character.GetBenchIndex();
	const int32 ActionIndex = States.IsValidIndex(Index) ? States[Index].CurrentAction : INDEX_NONE;
	if (Phase != ERunPhase::Recording || !Actions.IsValidIndex(ActionIndex) || Actions[ActionIndex].Id != ActionId)
	{
		return;   // a stop for an action whose start this client never saw
	}
	InterruptAction(Index, ActionIndex);
}

void UAECBenchSubsystem::FinishClientRun()
{
	if (Phase != ERunPhase::Recording)
	{
		return;
	}
	for (const FCharacterState& State : States)
	{
		UnattributedFires += State.HeldFires.Num();   // no start ever came for them
	}
	if (!WriteRunFiles())
	{
		ExitCode = AECBench::WriteFailedExitCode;
	}
	Phase = ERunPhase::Done;
}

void UAECBenchSubsystem::NotifyClientsFinished() const
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		AAECDemoPlayerController* Player = Cast<AAECDemoPlayerController>(It->Get());
		if (Player && !Player->IsLocalController())
		{
			Player->ClientBenchFinished();
		}
	}
}

void UAECBenchSubsystem::HandleMarkerFiredByAnimation(const USkeletalMeshComponent* MeshComp, const UAnimSequenceBase* Animation,
	FName EventName, EAECEventPhase EventPhase)
{
	const AAECDemoCharacter* Character = MeshComp ? Cast<AAECDemoCharacter>(MeshComp->GetOwner()) : nullptr;
	if (!Character || Character->GetWorld() != GetWorld() || !States.IsValidIndex(Character->GetBenchIndex()))
	{
		return;   // not one of this run's characters (another world, or no workload)
	}

	// Attributed to the character's current (or just finished) action, and only if it is that action's montage.
	FCharacterState& State = States[Character->GetBenchIndex()];
	const int32 ActionIndex = State.CurrentAction;

	// A G client learns of an action from its start message, but a simulated proxy's montage comes through GAS's own
	// replicated property, which can arrive first. A fire after the current action is over belongs to the next action:
	// it is held until that start arrives (HandleServerStart). NQ/NB clients start the montage on that message: no race.
	if (Settings.bClient && bGas)
	{
		const double Now = GetWorld()->GetTimeSeconds();
		if (!Actions.IsValidIndex(ActionIndex) || Now > LastFireTime(Actions[ActionIndex]))
		{
			State.HeldFires.Add({ Animation, EventName, EventPhase, Now, WallSeconds(), RunFrame() });
			++FiresThisFrame;
			return;
		}
	}

	const int32 Key = Actions.IsValidIndex(ActionIndex) ? FindKey(Actions[ActionIndex].Montage, Animation, EventName, EventPhase) : INDEX_NONE;
	if (Key == INDEX_NONE)
	{
		++UnattributedFires;
		return;
	}
	RecordFire(ActionIndex, Key, EPath::Animation);
}

int32 UAECBenchSubsystem::FindKey(int32 MontageIndex, const UAnimSequenceBase* Animation, FName EventName, EAECEventPhase EventPhase) const
{
	if (!Montages.IsValidIndex(MontageIndex) || Animation != Montages[MontageIndex].Get())
	{
		return INDEX_NONE;
	}
	return MontageInfos[MontageIndex].Keys.IndexOfByPredicate([EventName, EventPhase](const FKey& Candidate)
		{ return Candidate.Name == EventName && Candidate.Phase == EventPhase; });
}

double UAECBenchSubsystem::LastFireTime(const FAction& Action) const
{
	// The next action starts at least MinWaitSeconds after this one ended or was stopped, and its first key comes later
	// still, so this grace can never take a fire away from the next action.
	const double End = (Action.StopGameTime >= 0.0) ? Action.StopGameTime + AECBench::StopBlendOutSeconds
		: Action.StartGameTime + MontageInfos[Action.Montage].Length / Action.PlayRate;
	return End + AECBench::LateFireGraceSeconds;
}

void UAECBenchSubsystem::HandleClockEvent(const FAECEventInfo& Event)
{
	// The clock's keys are the director's keys (both from FAECMontageTimeline::Build), so its key index is the director's.
	// Attributed by the clock's own action id, not by timing: an event of an earlier action is never counted for a later one.
	const AAECDemoCharacter* Character = Cast<AAECDemoCharacter>(Event.Owner);
	const int32 Index = Character ? Character->GetBenchIndex() : INDEX_NONE;
	const int32 ActionIndex = States.IsValidIndex(Index) ? States[Index].CurrentAction : INDEX_NONE;
	if (!Actions.IsValidIndex(ActionIndex) || Actions[ActionIndex].ClockActionId != Event.ActionId
		|| !MontageInfos[Actions[ActionIndex].Montage].Keys.IsValidIndex(Event.KeyIndex))
	{
		++UnattributedFires;
		return;
	}
	RecordFire(ActionIndex, Event.KeyIndex, EPath::Clock);
}

void UAECBenchSubsystem::HandleTimerKey(int32 CharacterIndex, int32 ActionIndex, int32 KeyIndex)
{
	FCharacterState& State = States[CharacterIndex];
	if (State.CurrentAction != ActionIndex)
	{
		return;
	}
	const FKey& Key = MontageInfos[Actions[ActionIndex].Montage].Keys[KeyIndex];
	if (Key.Phase == EAECEventPhase::WindowBegin)
	{
		State.TimerWindowOpen[KeyIndex] = true;
	}
	else if (Key.Phase == EAECEventPhase::WindowEnd)
	{
		State.TimerWindowOpen[Key.PairKey] = false;   // close the window's begin key
	}
	RecordFire(ActionIndex, KeyIndex, EPath::Timer);
}

void UAECBenchSubsystem::RecordFire(int32 ActionIndex, int32 KeyIndex, EPath Path)
{
	RecordFire(ActionIndex, KeyIndex, Path, GetWorld()->GetTimeSeconds(), WallSeconds(), RunFrame());
	++FiresThisFrame;
}

void UAECBenchSubsystem::RecordFire(int32 ActionIndex, int32 KeyIndex, EPath Path, double GameTime, double WallTime, uint64 Frame)
{
	FFire& Fire = Fires.AddDefaulted_GetRef();
	Fire.Action = ActionIndex;
	Fire.Key = KeyIndex;
	Fire.Path = Path;
	Fire.GameTime = GameTime;
	Fire.WallTime = WallTime;
	Fire.Frame = Frame;
}

void UAECBenchSubsystem::HandleNetworkFailure(UWorld* FailedWorld, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString)
{
	// Without this, a client whose server vanished would travel to the default map and never quit (S4).
	if (FailedWorld != GetWorld() || Phase != ERunPhase::Recording)
	{
		return;
	}
	UE_LOG(LogAECBench, Error, TEXT("Lost the server before the run ended (%s: %s); no files written."), ENetworkFailure::ToString(FailureType), *ErrorString);
	Phase = ERunPhase::Done;
	if (Settings.bExitWhenDone)
	{
		FPlatformMisc::RequestExitWithStatus(false, AECBench::LostServerExitCode, TEXT("UAECBenchSubsystem"));
	}
}

void UAECBenchSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (Phase == ERunPhase::Idle)
	{
		return;
	}

	UWorld* World = GetWorld();
	const double Now = World->GetTimeSeconds();

	if (Phase == ERunPhase::Done)
	{
		++FramesSinceDone;
		if (Settings.bExitWhenDone && FramesSinceDone >= AECBench::ExitDelayFrames && Now >= ExitNotBefore)
		{
			Settings.bExitWhenDone = false; // request once
			FPlatformMisc::RequestExitWithStatus(false, ExitCode, TEXT("UAECBenchSubsystem"));
		}
		return;
	}

	if (bNetworked)
	{
		FrameStamps.Add({ RunFrame(), Now, FrameStartWallSeconds() });
	}

	// Re-applied every frame: anything that changes the view target would otherwise move the bands on screen. Only a
	// local player's view (a dedicated server's player controllers all belong to remote clients).
	APlayerController* PlayerController = World->GetFirstPlayerController();
	if (Camera && PlayerController && PlayerController->IsLocalController() && PlayerController->GetViewTarget() != Camera)
	{
		PlayerController->SetViewTarget(Camera);
	}

	if (Phase != ERunPhase::Recording)   // a client only records: its actions come from the server's messages
	{
		TickDirector(Now);
	}

	CSV_CUSTOM_STAT(AECBench, Fires, FiresThisFrame, ECsvCustomStatOp::Set);
	FiresThisFrame = 0;
}

void UAECBenchSubsystem::TickDirector(double Now)
{
	if (Phase == ERunPhase::WaitingForPlayers)
	{
		if (RemotePlayers >= Settings.Players)
		{
			UE_LOG(LogAECBench, Display, TEXT("All %d remote player(s) joined at %.3f s; warm-up"), RemotePlayers, Now);
			BeginWarmUp(Now);
		}
		else if (Now - BeginPlayTime > AECBench::PlayerWaitSeconds)
		{
			UE_LOG(LogAECBench, Error, TEXT("Only %d of %d remote player(s) joined within %.0f s."), RemotePlayers, Settings.Players, AECBench::PlayerWaitSeconds);
			ExitCode = AECBench::SetupFailedExitCode;
			Phase = ERunPhase::Done;
		}
		return;
	}

	if (Phase == ERunPhase::WarmUp && Now - WarmupStartTime >= Settings.WarmupSeconds)
	{
		BenchStartTime = Now;
		CaptureEndTime = Now + Settings.CaptureSeconds;
		Phase = ERunPhase::Capture;
		CSV_EVENT(AECBench, TEXT("BenchStart"));
		UE_LOG(LogAECBench, Display, TEXT("BenchStart at %.3f s"), Now);
	}
	else if (Phase == ERunPhase::Capture && Now >= CaptureEndTime)
	{
		Phase = ERunPhase::Draining;
		CSV_EVENT(AECBench, TEXT("BenchEnd"));
		UE_LOG(LogAECBench, Display, TEXT("BenchEnd at %.3f s; draining"), Now);
	}

	bool bAnyActive = false;
	for (int32 Index = 0; Index < States.Num(); ++Index)
	{
		FCharacterState& State = States[Index];
		if (State.bPlaying)
		{
			if (State.PlannedStopTime >= 0.0 && Now >= State.PlannedStopTime)
			{
				StopAction(Index);
				State.bPlaying = false;
				PlanNextAction(Index, Now);
			}
			else if (Now >= State.PlannedEndTime)
			{
				State.bPlaying = false;
				PlanNextAction(Index, Now);
			}
		}
		else if (!State.bWaitingForStart && Phase != ERunPhase::Draining && Now >= State.NextStartTime)
		{
			if (Settings.bStartPre && IsValid(Characters[Index]))
			{
				Characters[Index]->RequestStartInNextTick();
				State.bWaitingForStart = true;
			}
			else
			{
				StartAction(Index);
			}
		}
		bAnyActive |= State.bPlaying || State.bWaitingForStart;
	}

	if (Phase == ERunPhase::Draining && !bAnyActive)
	{
		if (DrainEndTime <= 0.0)
		{
			DrainEndTime = Now + AECBench::DrainGraceSeconds;
		}
		else if (Now >= DrainEndTime)
		{
			if (!WriteRunFiles())
			{
				ExitCode = AECBench::WriteFailedExitCode;
			}
			Phase = ERunPhase::Done;
			if (bNetworked)
			{
				NotifyClientsFinished();
				ExitNotBefore = Now + AECBench::ServerExitDelaySeconds;
			}
		}
	}
}

const TCHAR* UAECBenchSubsystem::PathName(EPath Path) const
{
	switch (Path)
	{
	case EPath::Timer: return TEXT("T");
	case EPath::Clock: return TEXT("AEC");
	default:           return *Settings.Config;   // the animation path: NQ, NB or G
	}
}

uint32 UAECBenchSubsystem::HashFires() const
{
	// Sorted first: inside one frame, the order in which meshes dispatch their queued notifies depends on which parallel
	// evaluation task finishes first, so the recording order can differ between identical runs (S2 check).
	TArray<FFire> Sorted = Fires;
	Sorted.Sort([](const FFire& A, const FFire& B)
	{
		if (A.Frame != B.Frame) { return A.Frame < B.Frame; }
		if (A.Action != B.Action) { return A.Action < B.Action; }
		if (A.Key != B.Key) { return A.Key < B.Key; }
		return A.Path < B.Path;
	});
	uint32 Hash = 0;
	for (const FFire& Fire : Sorted)
	{
		const int64 Values[4] = { Fire.Action, Fire.Key, static_cast<int64>(Fire.Path), static_cast<int64>(Fire.Frame) };
		Hash = FCrc::MemCrc32(Values, sizeof(Values), Hash);
	}
	return Hash;
}

bool UAECBenchSubsystem::WriteRunFiles() const
{
	auto RoleName = [](ERole Role) -> const TCHAR*
	{
		switch (Role)
		{
		case ERole::RemotePlayer:    return TEXT("remote");
		case ERole::SimulatedProxy:  return TEXT("sim");
		case ERole::AutonomousProxy: return TEXT("auto");
		default:                     return TEXT("npc");
		}
	};

	const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AECRuns"));
	const uint32 Hash = HashFires();
	const FString Header = FString::Printf(
		TEXT("# run=%s config=%s cond=%s start=%s chars=%d seed=%d warmup=%.3f capture=%.3f benchstart=%.6f captureend=%.6f fixedstep=%s net=%s players=%d clock=%d unattributed=%d hash=0x%08X\n"),
		*Settings.RunTag, *Settings.Config, *Settings.Condition, AECBench::StartOrderName(Settings.bStartPre), Characters.Num(), Settings.Seed,
		Settings.WarmupSeconds, Settings.CaptureSeconds, BenchStartTime, CaptureEndTime, *AECBench::FixedStepText(), NetModeName(), RemotePlayers, Settings.bClock ? 1 : 0,
		UnattributedFires, Hash);

	// Every "action" column holds the server's action id, so a client's files join the server's on it.
	FString ActionsCsv = Header + TEXT("action,char,band,montage,rate,start_game,start_wall,start_frame,stop_game,in_capture,role\n");
	FString ExpectedCsv = Header + TEXT("action,key,name,phase,position,due_game,interrupted\n");
	for (const FAction& Action : Actions)
	{
		const FMontageInfo& Info = MontageInfos[Action.Montage];
		ActionsCsv += FString::Printf(TEXT("%d,%d,%d,%s,%.3f,%.6f,%.6f,%llu,%.6f,%d,%s\n"), Action.Id, Action.Character, States[Action.Character].Band,
			*Info.Name, Action.PlayRate, Action.StartGameTime, Action.StartWallTime, Action.StartFrame, Action.StopGameTime, Action.bInCapture ? 1 : 0,
			RoleName(Action.Role));

		// Expected = every key the action reached, due at start + position / play rate. An interrupted action expects
		// nothing after its stop, except the end of a window that was open: that closes at the stop.
		const double StopPosition = (Action.StopGameTime >= 0.0) ? (Action.StopGameTime - Action.StartGameTime) * Action.PlayRate : TNumericLimits<double>::Max();
		for (int32 Key = 0; Key < Info.Keys.Num(); ++Key)
		{
			const FKey& K = Info.Keys[Key];
			double Due = -1.0;
			bool bInterrupted = false;
			if (K.Position <= StopPosition)
			{
				Due = Action.StartGameTime + K.Position / Action.PlayRate;
			}
			else if (K.Phase == EAECEventPhase::WindowEnd)
			{
				if (Info.Keys[K.PairKey].Position <= StopPosition)   // the window had begun before the stop
				{
					Due = Action.StopGameTime;
					bInterrupted = true;
				}
			}
			if (Due >= 0.0)
			{
				ExpectedCsv += FString::Printf(TEXT("%d,%d,%s,%s,%.6f,%.6f,%d\n"), Action.Id, Key, *K.Name.ToString(), AECBench::PhaseName(K.Phase),
					K.Position, Due, bInterrupted ? 1 : 0);
			}
		}
	}

	FString EventsCsv = Header + TEXT("action,key,name,phase,path,game,wall,frame\n");
	for (const FFire& Fire : Fires)
	{
		const FAction& Action = Actions[Fire.Action];
		const FKey& K = MontageInfos[Action.Montage].Keys[Fire.Key];
		EventsCsv += FString::Printf(TEXT("%d,%d,%s,%s,%s,%.6f,%.6f,%llu\n"), Action.Id, Fire.Key, *K.Name.ToString(), AECBench::PhaseName(K.Phase),
			PathName(Fire.Path), Fire.GameTime, Fire.WallTime, Fire.Frame);
	}

	auto Save = [&Directory, this](const FString& Text, const TCHAR* Kind)
	{
		return FFileHelper::SaveStringToFile(Text, *FPaths::Combine(Directory, Settings.RunTag + Kind));
	};
	bool bWritten = Save(ActionsCsv, TEXT("_actions.csv"));
	bWritten &= Save(EventsCsv, TEXT("_events.csv"));
	if (!Settings.bClient)
	{
		bWritten &= Save(ExpectedCsv, TEXT("_expected.csv"));   // a client's expected events are the server's
	}
	if (bNetworked)
	{
		FString FramesCsv = Header + TEXT("frame,game,wall\n");
		FramesCsv.Reserve(FramesCsv.Len() + FrameStamps.Num() * 40);   // one line is under 40 characters
		for (const FFrameStamp& Stamp : FrameStamps)
		{
			FramesCsv.Appendf(TEXT("%llu,%.6f,%.6f\n"), Stamp.Frame, Stamp.GameTime, Stamp.WallTime);
		}
		bWritten &= Save(FramesCsv, TEXT("_frames.csv"));
	}

	UE_LOG(LogAECBench, Display, TEXT("Run files %s in %s: %d actions, %d fires, %d unattributed, fire hash 0x%08X"),
		bWritten ? TEXT("written") : TEXT("NOT written"), *FPaths::ConvertRelativePathToFull(Directory), Actions.Num(), Fires.Num(), UnattributedFires, Hash);
	if (!bWritten)
	{
		UE_LOG(LogAECBench, Error, TEXT("Could not write the run files to %s."), *Directory);
	}
	return bWritten;
}

TStatId UAECBenchSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UAECBenchSubsystem, STATGROUP_Tickables);
}
