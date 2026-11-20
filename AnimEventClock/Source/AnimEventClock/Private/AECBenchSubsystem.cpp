// P06 AnimEventClock (demo) - UAECBenchSubsystem.

#include "AECBenchSubsystem.h"

#include "AECDemoCharacter.h"
#include "AECDemoMontages.h"
#include "AECEventNotify.h"
#include "AECEventWindowNotifyState.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Camera/CameraActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
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

	/** Exit codes: the run could not be set up (missing montage, unknown condition, a failed spawn); its files could not be written. */
	constexpr uint8 SetupFailedExitCode = 3;
	constexpr uint8 WriteFailedExitCode = 4;

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
	FParse::Value(CommandLine, TEXT("AECRunTag="), Read.RunTag);
	Read.bExitWhenDone = FParse::Param(CommandLine, TEXT("AECExit"));
	return Read;
}

double UAECBenchSubsystem::WallSeconds()
{
	// The raw, system-wide performance counter: comparable across processes on one machine (raw notes E0).
	return FPlatformTime::ToSeconds64(FPlatformTime::Cycles64());
}

void UAECBenchSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	Settings = ReadRunSettings();
	if (Settings.NumCharacters <= 0)
	{
		return; // No workload requested: the world behaves like a normal empty map.
	}

	BeginPlayTime = InWorld.GetTimeSeconds();
	BeginPlayFrame = GFrameCounter;

	const bool bConfigKnown = (Settings.Config == TEXT("NQ") || Settings.Config == TEXT("NB"));
	if (!bConfigKnown || !PrepareCondition() || !LoadMontages() || Settings.CaptureSeconds <= 0.0 || Settings.WarmupSeconds < 0.0
		|| !SpawnCharacters(InWorld))
	{
		UE_LOG(LogAECBench, Error, TEXT("Run not started: config %s, condition %s, warm-up %.2f s, capture %.2f s (see errors above)."),
			*Settings.Config, *Settings.Condition, Settings.WarmupSeconds, Settings.CaptureSeconds);
		ExitCode = AECBench::SetupFailedExitCode;
		Phase = ERunPhase::Done;
		return;
	}

	MarkerHookHandle = AnimEventClock::OnMarkerFiredByAnimation().AddUObject(this, &UAECBenchSubsystem::HandleMarkerFiredByAnimation);
	Camera = InWorld.SpawnActor<ACameraActor>(AECBench::CameraLocation, FRotator::ZeroRotator);
	Phase = ERunPhase::WarmUp;

	UE_LOG(LogAECBench, Display, TEXT("Run %s: %d characters, seed %d, config %s, condition %s, start %s, warm-up %.1f s, capture %.1f s, fixed step %s"),
		*Settings.RunTag, Characters.Num(), Settings.Seed, *Settings.Config, *Settings.Condition, AECBench::StartOrderName(Settings.bStartPre),
		Settings.WarmupSeconds, Settings.CaptureSeconds, *AECBench::FixedStepText());
	CSV_METADATA(TEXT("AECRunTag"), *Settings.RunTag);
	CSV_METADATA(TEXT("AECConfig"), *Settings.Config);
	CSV_METADATA(TEXT("AECCond"), *Settings.Condition);
}

void UAECBenchSubsystem::Deinitialize()
{
	AnimEventClock::OnMarkerFiredByAnimation().Remove(MarkerHookHandle);
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
	if (Condition == TEXT("aba2") || Condition == TEXT("aba4"))
	{
		// Enabled before any budgeted mesh exists, as the cvar's help asks; then a forced rate for exact, repeatable runs.
		bRegisterWithBudget = true;
		return SetConsoleVariable(TEXT("a.Budget.Enabled"), 1)
			&& SetConsoleVariable(TEXT("a.Budget.Debug.Force"), 1)
			&& SetConsoleVariable(TEXT("a.Budget.Debug.Force.Rate"), Condition == TEXT("aba2") ? 2 : 4);
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
	const bool bBranchingPoint = (Settings.Config == TEXT("NB"));
	for (const AECDemo::FMontageSpec& Spec : AECDemo::GetMontageSpecs())
	{
		const FString Path = AECDemo::GetMontageObjectPath(Spec, bBranchingPoint);
		UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *Path);
		if (!Montage)
		{
			UE_LOG(LogAECBench, Error, TEXT("Montage not found: %s (run Tools\\make_assets.bat)."), *Path);
			return false;
		}

		// The keys come from the montage's own notifies, exactly as the event clock will read them; a window gives two.
		FMontageInfo& Info = MontageInfos.AddDefaulted_GetRef();
		Info.Name = Montage->GetName();
		Info.Length = Montage->GetPlayLength();
		for (const FAnimNotifyEvent& Event : Montage->Notifies)
		{
			if (const UAECEventNotify* Instant = Cast<UAECEventNotify>(Event.Notify))
			{
				Info.Keys.Add({ Instant->EventName, EAECEventPhase::Instant, Event.GetTriggerTime() });
			}
			else if (const UAECEventWindowNotifyState* Window = Cast<UAECEventWindowNotifyState>(Event.NotifyStateClass))
			{
				Info.Keys.Add({ Window->EventName, EAECEventPhase::WindowBegin, Event.GetTriggerTime() });
				Info.Keys.Add({ Window->EventName, EAECEventPhase::WindowEnd, Event.GetEndTriggerTime() });
			}
		}
		Info.Keys.StableSort([](const FKey& A, const FKey& B) { return A.Position < B.Position; });
		for (int32 Key = 0; Key < Info.Keys.Num(); ++Key)
		{
			// Pair each window's begin and end once, so nothing searches by name later (window names are unique per montage).
			if (Info.Keys[Key].Phase == EAECEventPhase::WindowBegin)
			{
				const FName WindowName = Info.Keys[Key].Name;
				const int32 End = Info.Keys.IndexOfByPredicate([WindowName](const FKey& Candidate)
					{ return Candidate.Name == WindowName && Candidate.Phase == EAECEventPhase::WindowEnd; });
				Info.Keys[Key].PairKey = End;
				Info.Keys[End].PairKey = Key;
			}
		}
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

bool UAECBenchSubsystem::SpawnCharacters(UWorld& World)
{
	Characters.Reserve(Settings.NumCharacters);
	States.SetNum(Settings.NumCharacters);

	for (int32 Index = 0; Index < Settings.NumCharacters; ++Index)
	{
		// Placement and schedule each get their own stream per character, so changing N or the schedule of one
		// character never moves another (and the same seed always gives the same run).
		FRandomStream Placement(Settings.Seed * 7919 + Index);
		States[Index].Random.Initialize(Settings.Seed * 104729 + Index);

		const int32 Band = Index % 3;
		const AECBench::FBandBox& Box = AECBench::Bands[Band];
		const FVector Location(Placement.FRandRange(Box.MinX, Box.MaxX), Placement.FRandRange(-Box.HalfY, Box.HalfY), AECBench::CharacterHeight);
		const FTransform Transform(FRotator(0.0, 180.0, 0.0), Location);   // facing the camera

		// Deferred spawn: the mesh settings of the condition are in place before BeginPlay registers anything.
		AAECDemoCharacter* Character = World.SpawnActorDeferred<AAECDemoCharacter>(AAECDemoCharacter::StaticClass(), Transform,
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Character)
		{
			UE_LOG(LogAECBench, Error, TEXT("Spawning character %d failed."), Index);
			return false;
		}
		Character->SetBenchIndex(Index);
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
		Character->FinishSpawning(Transform);

		if (bRootMotionFromEverything)
		{
			if (UAnimInstance* AnimInstance = Mesh->GetAnimInstance())
			{
				AnimInstance->RootMotionMode = ERootMotionMode::RootMotionFromEverything;
			}
		}

		Characters.Add(Character);
		States[Index].Band = Band;
		PlanNextAction(Index, BeginPlayTime);
	}

	// Reserved up front, so the arrays never reallocate (and copy megabytes) inside a measured frame. An action takes at
	// least the shortest montage at the fastest rate plus the shortest wait; each action records at most 2 fires per key.
	const float ShortestLength = FMath::Min(MontageInfos[0].Length, MontageInfos.Last().Length);
	const double ShortestCycle = AECBench::MinWaitSeconds + ShortestLength / AECBench::FastPlayRate;
	const int32 MaxKeys = FMath::Max(MontageInfos[0].Keys.Num(), MontageInfos.Last().Keys.Num());
	const int32 ActionsPerCharacter = FMath::CeilToInt32((Settings.WarmupSeconds + Settings.CaptureSeconds) / ShortestCycle) + 2;
	Actions.Reserve(Settings.NumCharacters * ActionsPerCharacter);
	Fires.Reserve(Settings.NumCharacters * ActionsPerCharacter * MaxKeys * 2);
	return true;
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

void UAECBenchSubsystem::StartAction(int32 CharacterIndex)
{
	AAECDemoCharacter* Character = Characters.IsValidIndex(CharacterIndex) ? Characters[CharacterIndex].Get() : nullptr;
	UAnimInstance* AnimInstance = IsValid(Character) ? Character->GetMesh()->GetAnimInstance() : nullptr;
	if (!AnimInstance)
	{
		return;
	}

	UWorld* World = GetWorld();
	FCharacterState& State = States[CharacterIndex];
	const FMontageInfo& Info = MontageInfos[State.PlannedMontage];

	FAction& Action = Actions.AddDefaulted_GetRef();
	const int32 ActionIndex = Actions.Num() - 1;
	Action.Character = CharacterIndex;
	Action.Montage = State.PlannedMontage;
	Action.PlayRate = State.PlannedPlayRate;
	Action.StartGameTime = World->GetTimeSeconds();
	Action.StartWallTime = WallSeconds();
	Action.StartFrame = RunFrame();
	Action.bInCapture = (Phase == ERunPhase::Capture);

	AnimInstance->Montage_Play(Montages[State.PlannedMontage], Action.PlayRate);

	// The timer path: one timer per key, at the authored position divided by the play rate.
	ClearTimers(State);
	FTimerManager& Timers = World->GetTimerManager();
	State.Timers.SetNum(Info.Keys.Num());
	State.TimerWindowOpen.Init(false, Info.Keys.Num());
	for (int32 Key = 0; Key < Info.Keys.Num(); ++Key)
	{
		const float Delay = Info.Keys[Key].Position / Action.PlayRate;
		Timers.SetTimer(State.Timers[Key], FTimerDelegate::CreateUObject(this, &UAECBenchSubsystem::HandleTimerKey, CharacterIndex, ActionIndex, Key),
			FMath::Max(Delay, UE_KINDA_SMALL_NUMBER), false);
	}

	const double Duration = Info.Length / Action.PlayRate;
	State.CurrentAction = ActionIndex;
	State.bPlaying = true;
	State.PlannedEndTime = Action.StartGameTime + Duration;
	State.PlannedStopTime = (State.PlannedStopFraction >= 0.0) ? Action.StartGameTime + State.PlannedStopFraction * Duration : -1.0;
}

void UAECBenchSubsystem::StopAction(int32 CharacterIndex)
{
	FCharacterState& State = States[CharacterIndex];
	FAction& Action = Actions[State.CurrentAction];
	Action.StopGameTime = GetWorld()->GetTimeSeconds();

	AAECDemoCharacter* Character = Characters[CharacterIndex].Get();
	if (UAnimInstance* AnimInstance = IsValid(Character) ? Character->GetMesh()->GetAnimInstance() : nullptr)
	{
		AnimInstance->Montage_Stop(AECBench::StopBlendOutSeconds, Montages[Action.Montage]);
	}

	// The timer path does what a hand-written implementation would: cancel what is pending, close what is open.
	ClearTimers(State);
	const TArray<FKey>& Keys = MontageInfos[Action.Montage].Keys;
	for (int32 Key = 0; Key < Keys.Num(); ++Key)
	{
		if (State.TimerWindowOpen[Key])
		{
			State.TimerWindowOpen[Key] = false;
			RecordFire(State.CurrentAction, Keys[Key].PairKey, EPath::Timer);
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
	const int32 ActionIndex = States[Character->GetBenchIndex()].CurrentAction;
	const int32 MontageIndex = Actions.IsValidIndex(ActionIndex) ? Actions[ActionIndex].Montage : INDEX_NONE;
	const int32 Key = (MontageIndex != INDEX_NONE && Animation == Montages[MontageIndex].Get())
		? MontageInfos[MontageIndex].Keys.IndexOfByPredicate([EventName, EventPhase](const FKey& Candidate) { return Candidate.Name == EventName && Candidate.Phase == EventPhase; })
		: INDEX_NONE;
	if (Key == INDEX_NONE)
	{
		++UnattributedFires;
		return;
	}
	RecordFire(ActionIndex, Key, EPath::Animation);
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
	FFire& Fire = Fires.AddDefaulted_GetRef();
	Fire.Action = ActionIndex;
	Fire.Key = KeyIndex;
	Fire.Path = Path;
	Fire.GameTime = GetWorld()->GetTimeSeconds();
	Fire.WallTime = WallSeconds();
	Fire.Frame = RunFrame();
	++FiresThisFrame;
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
		if (Settings.bExitWhenDone && FramesSinceDone >= AECBench::ExitDelayFrames)
		{
			Settings.bExitWhenDone = false; // request once
			FPlatformMisc::RequestExitWithStatus(false, ExitCode, TEXT("UAECBenchSubsystem"));
		}
		return;
	}

	// Re-applied every frame: anything that changes the view target would otherwise move the bands on screen.
	APlayerController* PlayerController = World->GetFirstPlayerController();
	if (Camera && PlayerController && PlayerController->GetViewTarget() != Camera)
	{
		PlayerController->SetViewTarget(Camera);
	}

	if (Phase == ERunPhase::WarmUp && Now - BeginPlayTime >= Settings.WarmupSeconds)
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
		}
	}

	CSV_CUSTOM_STAT(AECBench, Fires, FiresThisFrame, ECsvCustomStatOp::Set);
	FiresThisFrame = 0;
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
	const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AECRuns"));
	const uint32 Hash = HashFires();
	const FString Header = FString::Printf(
		TEXT("# run=%s config=%s cond=%s start=%s chars=%d seed=%d warmup=%.3f capture=%.3f benchstart=%.6f captureend=%.6f fixedstep=%s unattributed=%d hash=0x%08X\n"),
		*Settings.RunTag, *Settings.Config, *Settings.Condition, AECBench::StartOrderName(Settings.bStartPre), Characters.Num(), Settings.Seed,
		Settings.WarmupSeconds, Settings.CaptureSeconds, BenchStartTime, CaptureEndTime, *AECBench::FixedStepText(), UnattributedFires, Hash);

	FString ActionsCsv = Header + TEXT("action,char,band,montage,rate,start_game,start_wall,start_frame,stop_game,in_capture\n");
	FString ExpectedCsv = Header + TEXT("action,key,name,phase,position,due_game,interrupted\n");
	for (int32 ActionIndex = 0; ActionIndex < Actions.Num(); ++ActionIndex)
	{
		const FAction& Action = Actions[ActionIndex];
		const FMontageInfo& Info = MontageInfos[Action.Montage];
		ActionsCsv += FString::Printf(TEXT("%d,%d,%d,%s,%.3f,%.6f,%.6f,%llu,%.6f,%d\n"), ActionIndex, Action.Character, States[Action.Character].Band,
			*Info.Name, Action.PlayRate, Action.StartGameTime, Action.StartWallTime, Action.StartFrame, Action.StopGameTime, Action.bInCapture ? 1 : 0);

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
				ExpectedCsv += FString::Printf(TEXT("%d,%d,%s,%s,%.6f,%.6f,%d\n"), ActionIndex, Key, *K.Name.ToString(), AECBench::PhaseName(K.Phase),
					K.Position, Due, bInterrupted ? 1 : 0);
			}
		}
	}

	FString EventsCsv = Header + TEXT("action,key,name,phase,path,game,wall,frame\n");
	for (const FFire& Fire : Fires)
	{
		const FKey& K = MontageInfos[Actions[Fire.Action].Montage].Keys[Fire.Key];
		EventsCsv += FString::Printf(TEXT("%d,%d,%s,%s,%s,%.6f,%.6f,%llu\n"), Fire.Action, Fire.Key, *K.Name.ToString(), AECBench::PhaseName(K.Phase),
			Fire.Path == EPath::Timer ? TEXT("T") : *Settings.Config, Fire.GameTime, Fire.WallTime, Fire.Frame);
	}

	const bool bActionsWritten = FFileHelper::SaveStringToFile(ActionsCsv, *FPaths::Combine(Directory, Settings.RunTag + TEXT("_actions.csv")));
	const bool bExpectedWritten = FFileHelper::SaveStringToFile(ExpectedCsv, *FPaths::Combine(Directory, Settings.RunTag + TEXT("_expected.csv")));
	const bool bEventsWritten = FFileHelper::SaveStringToFile(EventsCsv, *FPaths::Combine(Directory, Settings.RunTag + TEXT("_events.csv")));
	const bool bWritten = bActionsWritten && bExpectedWritten && bEventsWritten;
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
