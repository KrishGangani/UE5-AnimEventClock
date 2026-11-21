// AnimEventClockCore - UAECEventClockComponent.

#include "AECEventClockComponent.h"

#include "AECClockTelemetry.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AECEventClockComponent)

DEFINE_LOG_CATEGORY_STATIC(LogAnimEventClock, Log, All);

namespace AECClock
{
	TAutoConsoleVariable<int32> CVarEnabled(TEXT("aec.Enabled"), 1,
		TEXT("1: event clocks dispatch their events (default). 0: they don't (montages still play): the 'clock off' configuration."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarDebug(TEXT("aec.Debug"), 0,
		TEXT("0: off. 1: log every event an event clock fires. 2: also draw the open windows above the actor."),
		ECVF_Default);

	bool IsEnabled()
	{
		return CVarEnabled.GetValueOnGameThread() != 0;
	}

	/** Marks handlers as running for its scope; the outermost one adds its cycles to the frame's dispatch cost (nested
	 *  dispatches, from a handler's stop or jump, are inside it). */
	struct FDispatchScope
	{
		explicit FDispatchScope(bool& bInDispatching)
			: bDispatching(bInDispatching), bOutermost(!bInDispatching), StartCycles(bOutermost ? FPlatformTime::Cycles64() : 0)
		{
			bDispatching = true;
		}

		~FDispatchScope()
		{
			if (bOutermost)
			{
				bDispatching = false;   // the component outlives this scope even if its actor was destroyed (freed only by GC)
				AnimEventClock::Telemetry::Frame.DispatchCycles += FPlatformTime::Cycles64() - StartCycles;
			}
		}

		bool& bDispatching;
		const bool bOutermost;
		const uint64 StartCycles;
	};
}

UAECEventClockComponent::UAECEventClockComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;   // ticks only while an action is active
	PrimaryComponentTick.TickGroup = TG_PrePhysics;       // where the mesh would have fired the notify (02 §7)
}

double UAECEventClockComponent::ClockNow() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

UAnimInstance* UAECEventClockComponent::GetDrivenAnimInstance()
{
	if (!bDriveMontage)
	{
		return nullptr;
	}
	if (!DrivenMesh.IsValid())
	{
		const AActor* Owner = GetOwner();
		DrivenMesh = Owner ? Owner->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
	}
	return DrivenMesh.IsValid() ? DrivenMesh->GetAnimInstance() : nullptr;
}

bool UAECEventClockComponent::IsOwnerAlive() const
{
	const AActor* Owner = GetOwner();
	return IsValid(this) && Owner && !Owner->IsPendingKillPending();
}

double UAECEventClockComponent::CatchUp()
{
	if (bDispatching)
	{
		return DispatchDueTime;
	}
	const double Now = ClockNow();
	AdvanceTo(Now);   // nothing unless an action is active
	return Now;
}

void UAECEventClockComponent::RewindHeadToHandledCrossing()
{
	if (bHeadAhead)
	{
		Head = { HandledCrossing.Section, HandledCrossing.Position, HandledCrossing.Pass, /*bIncludeStart=*/ false, /*bFinished=*/ false, OpenWindows };
		bHeadAhead = false;
	}
}

int32 UAECEventClockComponent::PlayAction(UAnimMontage* InMontage, float InPlayRate, FName StartSection)
{
	if (!InMontage)
	{
		return INDEX_NONE;
	}

	// A running action ends where the new one starts: first everything due until then, then interrupted ends (02 §6.2).
	const double Now = CatchUp();
	while (bActive)
	{
		FinishAction(Now, /*bInterrupted=*/ true);   // again if a closing end's handler started one: this call wins
	}
	if (!IsOwnerAlive())
	{
		return INDEX_NONE;   // a handler of the old action's last events destroyed the actor
	}

	Montage = InMontage;
	PlayRate = FMath::Max(InPlayRate, FAECMontageTimeline::MinPlayRate);
	Timeline = FAECMontageTimeline::Build(*InMontage);
	Head = Timeline.Start(Timeline.FindSection(StartSection));
	bHeadAhead = false;   // a fresh playhead, exactly where the new action is (also when started from a handler)
	OpenWindows.Reset();
	++ActionId;
	++PathSerial;
	LastTime = Now;   // from a handler: the rest of the frame is advanced on the new action (AdvanceTo)
	bActive = !Head.bFinished;
	SetComponentTickEnabled(bActive);

	if (UAnimInstance* AnimInstance = GetDrivenAnimInstance())
	{
		AnimInstance->Montage_Play(InMontage, PlayRate);
		if (!StartSection.IsNone())
		{
			AnimInstance->Montage_JumpToSection(StartSection, InMontage);
		}
	}
	return ActionId;
}

void UAECEventClockComponent::StopAction()
{
	// What was due until now still fires; from a handler, the stop happens at the due time of the event being handled and
	// later events of the frame don't fire.
	const double Time = CatchUp();
	if (bActive)
	{
		FinishAction(Time, /*bInterrupted=*/ true);
	}
}

void UAECEventClockComponent::JumpToSection(FName Section)
{
	if (!bActive || Timeline.FindSection(Section) == INDEX_NONE)
	{
		return;
	}
	const double Time = CatchUp();
	// Looked up again: the catch-up may have finished the action, or a handler stopped it or started another.
	const int32 SectionIndex = bActive ? Timeline.FindSection(Section) : INDEX_NONE;
	if (SectionIndex == INDEX_NONE)
	{
		return;
	}
	RewindHeadToHandledCrossing();   // so the right windows close, and the pass rule compares the right positions
	TArray<FAECCrossing> Ends;
	Timeline.JumpToSection(Head, SectionIndex, Ends);
	++PathSerial;      // crossings a running dispatch computed before the jump are stale
	LastTime = Time;   // from a handler: the rest of the frame is advanced on the new path (AdvanceTo)
	if (UAnimInstance* AnimInstance = GetDrivenAnimInstance())
	{
		AnimInstance->Montage_JumpToSection(Section, Montage);
	}
	Dispatch(Ends, Time, 0.f);
}

bool UAECEventClockComponent::IsWindowOpen(FName Window) const
{
	const TArray<FAECTimelineKey>& Keys = Timeline.GetKeys();
	return bActive && OpenWindows.ContainsByPredicate([&Keys, Window](int32 Begin) { return Keys[Begin].Name == Window; });
}

void UAECEventClockComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bActive)
	{
		SetComponentTickEnabled(false);
		return;
	}
	++AnimEventClock::Telemetry::Frame.ActiveActions;
	AdvanceTo(ClockNow());
	if (bActive)
	{
		FollowMontage();
	}
}

void UAECEventClockComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// No events from EndPlay (02 §8): the action simply ends; open windows count as closed.
	bActive = false;
	OpenWindows.Reset();
	Super::EndPlay(EndPlayReason);
}

void UAECEventClockComponent::AdvanceTo(double Time)
{
	// Never from inside a dispatch (CatchUp returns before it there), so the crossing buffer is not rewritten while its
	// crossings are being dispatched.
	check(!bDispatching);

	// A handler that jumps or starts another action restarts the path at the handled event's due time (it sets LastTime
	// there); the rest of the frame is then advanced on the new path, up to MaxPathRestartsPerFrame times.
	for (int32 Restart = 0; Restart <= MaxPathRestartsPerFrame && bActive; ++Restart)
	{
		const float Delta = static_cast<float>((Time - LastTime) * PlayRate);
		if (Delta <= 0.f)
		{
			return;
		}
		LastTime = Time;
		const uint32 Path = PathSerial;
		Crossings.Reset();
		if (!Timeline.Advance(Head, Delta, Crossings))
		{
			UE_LOG(LogAnimEventClock, Warning, TEXT("%s: montage %s has a section cycle that never advances; the action ends."),
				*GetNameSafe(GetOwner()), *GetNameSafe(Montage));
		}
		bHeadAhead = true;   // until the dispatch is over (or a handler's stop or jump rewinds it)
		Dispatch(Crossings, Time, Delta);
		bHeadAhead = false;
		if (PathSerial == Path && bActive && Head.bFinished)
		{
			FinishAction(Time, /*bInterrupted=*/ false);
		}
		if (PathSerial == Path)
		{
			return;   // else a handler (also one of the closing ends) started a new path: advance it through this frame too
		}
	}
}

void UAECEventClockComponent::Dispatch(const TArray<FAECCrossing>& ToFire, double Time, float Delta)
{
	if (ToFire.IsEmpty())
	{
		return;
	}

	// Handlers may stop the action, jump, start another or destroy the actor: each is checked after every event (02 §8).
	// ToFire itself never changes meanwhile (AdvanceTo, the only writer of the crossing buffer, cannot run inside here).
	const AECClock::FDispatchScope Scope(bDispatching);
	const uint32 Path = PathSerial;
	const bool bEnabled = AECClock::IsEnabled();
	for (const FAECCrossing& Crossing : ToFire)
	{
		// The window state handlers see is the state after this event. An end goes out exactly when its begin did: not for
		// a begin cut off by a handler's jump or crossed while aec.Enabled was 0; always for one that went out, even if
		// aec.Enabled has been switched off since (every begin a handler saw gets its end).
		const FAECTimelineKey& Key = Timeline.GetKeys()[Crossing.Key];
		if (Key.Phase == EAECEventPhase::WindowEnd)
		{
			if (OpenWindows.RemoveSingle(Key.PairKey) == 0)
			{
				continue;
			}
		}
		else if (!bEnabled)
		{
			continue;
		}
		else if (Key.Phase == EAECEventPhase::WindowBegin)
		{
			OpenWindows.AddUnique(Crossing.Key);
		}

		HandledCrossing = Crossing;
		Broadcast(MakeEventInfo(Crossing, FAECMontageTimeline::DueTime(Time, Delta, Crossing.Offset, PlayRate)));   // Delta 0: at Time
		if (PathSerial != Path || !bActive || !IsOwnerAlive())
		{
			break;   // stopped, jumped, replaced or destroyed by a handler: these crossings are no longer this action's path
		}
	}
}

FAECEventInfo UAECEventClockComponent::MakeEventInfo(const FAECCrossing& Crossing, double DueTime) const
{
	const FAECTimelineKey& Key = Timeline.GetKeys()[Crossing.Key];
	FAECEventInfo Info;
	Info.Owner = GetOwner();
	Info.EventName = Key.Name;
	Info.Phase = Key.Phase;
	Info.ActionId = ActionId;
	Info.KeyIndex = Crossing.Key;
	Info.Pass = Crossing.Pass;
	Info.DueTime = DueTime;
	Info.FireTime = ClockNow();
	Info.LatenessSeconds = static_cast<float>(Info.FireTime - DueTime);
	Info.bInterrupted = Crossing.bForced;
	return Info;
}

void UAECEventClockComponent::Broadcast(const FAECEventInfo& Info)
{
	DispatchDueTime = Info.DueTime;
	++AnimEventClock::Telemetry::Frame.EventsFired;
	if (AECClock::CVarDebug.GetValueOnGameThread() >= 1)
	{
		UE_LOG(LogAnimEventClock, Display, TEXT("%s: %s %s (action %d, key %d, pass %d) due %.4f fired %.4f late %.1f ms%s"),
			*GetNameSafe(GetOwner()), *Info.EventName.ToString(), *UEnum::GetValueAsString(Info.Phase), Info.ActionId, Info.KeyIndex,
			Info.Pass, Info.DueTime, Info.FireTime, Info.LatenessSeconds * 1000.f, Info.bInterrupted ? TEXT(" (interrupted)") : TEXT(""));
	}
	OnClockEvent.Broadcast(Info);
}

void UAECEventClockComponent::FinishAction(double Time, bool bInterrupted)
{
	// Off before the last events go out, so a handler sees the action as over and may start the next one; the montage
	// is stopped first for the same reason (a handler may play it again).
	bActive = false;
	SetComponentTickEnabled(false);
	if (bInterrupted)
	{
		if (UAnimInstance* AnimInstance = GetDrivenAnimInstance())
		{
			AnimInstance->Montage_Stop(StopBlendOutSeconds, Montage);
		}
	}

	// Every window a handler saw begin gets its end, due at Time: the timeline's own forced ends (key order), from where
	// the playhead was at the handled crossing when a handler stops or replaces the action. Built before any goes out: a
	// handler that starts the next action replaces the timeline and the open windows. Once the action is over every
	// window counts as closed (IsWindowOpen is false), also while these ends go out.
	RewindHeadToHandledCrossing();
	Head.OpenWindows = OpenWindows;
	OpenWindows.Reset();
	TArray<FAECCrossing> Forced;
	Timeline.Stop(Head, Forced);
	if (Forced.IsEmpty())
	{
		return;
	}
	TArray<FAECEventInfo, TInlineAllocator<4>> Ends;
	for (const FAECCrossing& End : Forced)
	{
		Ends.Add(MakeEventInfo(End, Time));
	}
	const AECClock::FDispatchScope Scope(bDispatching);
	for (const FAECEventInfo& End : Ends)
	{
		Broadcast(End);
		if (!IsOwnerAlive())
		{
			break;
		}
	}
}

void UAECEventClockComponent::FollowMontage()
{
	if (AECClock::CVarDebug.GetValueOnGameThread() >= 2 && !OpenWindows.IsEmpty())
	{
		FString Text;
		for (const int32 Begin : OpenWindows)
		{
			Text += Timeline.GetKeys()[Begin].Name.ToString() + TEXT(" ");
		}
		DrawDebugString(GetWorld(), GetOwner()->GetActorLocation() + FVector(0.0, 0.0, 120.0), Text, nullptr, FColor::Yellow, 0.f);
	}

	UAnimInstance* AnimInstance = GetDrivenAnimInstance();
	FAnimMontageInstance* Instance = AnimInstance ? AnimInstance->GetActiveInstanceForMontage(Montage) : nullptr;
	if (!Instance || !Instance->IsPlaying())
	{
		return;
	}
	// The pose follows the clock: corrected only beyond the threshold, so normal throttling never triggers it (02 §7).
	// Moving the instance fires no notify (as Montage_SetPosition, which does the same).
	if (FMath::Abs(Instance->GetPosition() - Head.Position) / PlayRate > MaxVisualDriftSeconds)
	{
		Instance->SetPosition(Head.Position);
		++AnimEventClock::Telemetry::Frame.VisualCorrections;
	}
}
