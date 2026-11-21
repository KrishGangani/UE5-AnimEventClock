// AnimEventClockCore - the event clock: fires a montage's authored events from the gameplay clock, exactly once.

#pragma once

#include "AECMontageTimeline.h"
#include "Components/ActorComponent.h"
#include "AECEventClockComponent.generated.h"

#define UE_API ANIMEVENTCLOCKCORE_API

class UAnimInstance;
class UAnimMontage;
class USkeletalMeshComponent;

/** What a handler receives for one authored event. */
USTRUCT(BlueprintType)
struct FAECEventInfo
{
	GENERATED_BODY()

	/** The actor whose clock fired the event. */
	UPROPERTY(BlueprintReadOnly, Category = "AnimEventClock")
	TObjectPtr<AActor> Owner;

	UPROPERTY(BlueprintReadOnly, Category = "AnimEventClock")
	FName EventName;

	UPROPERTY(BlueprintReadOnly, Category = "AnimEventClock")
	EAECEventPhase Phase = EAECEventPhase::Instant;

	/** The action this event belongs to (PlayAction's return value). */
	UPROPERTY(BlueprintReadOnly, Category = "AnimEventClock")
	int32 ActionId = INDEX_NONE;

	/** The key's index in the action's timeline; with ActionId and Pass, a stable id of this event. */
	UPROPERTY(BlueprintReadOnly, Category = "AnimEventClock")
	int32 KeyIndex = INDEX_NONE;

	/** 0 on the first time through; +1 after each loop or jump back. */
	UPROPERTY(BlueprintReadOnly, Category = "AnimEventClock")
	int32 Pass = 0;

	/** When the event was due, on the clock (exact, between frames). */
	UPROPERTY(BlueprintReadOnly, Category = "AnimEventClock")
	double DueTime = 0.0;

	/** When it fired: the clock time of this frame. */
	UPROPERTY(BlueprintReadOnly, Category = "AnimEventClock")
	double FireTime = 0.0;

	/** FireTime - DueTime: below one frame on the authority (SC1). */
	UPROPERTY(BlueprintReadOnly, Category = "AnimEventClock")
	float LatenessSeconds = 0.f;

	/** A window end that the action did not reach: closed by a stop, a jump or a loop. */
	UPROPERTY(BlueprintReadOnly, Category = "AnimEventClock")
	bool bInterrupted = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAECClockEventSignature, const FAECEventInfo&, Event);

/**
 * Plays an action's montage and fires the events authored in it (UAECEventNotify, UAECEventWindowNotifyState) from the
 * gameplay clock instead of the animation update: on time whatever URO, the budget allocator, visibility or the network
 * role do to the mesh, and each event exactly once (02 §2, §6.1).
 *
 * Per frame (TG_PrePhysics, the mesh's group, and only while an action is active): the playhead advances by the clock
 * time since the last frame times the play rate; every key it crosses is broadcast in order with its exact due time.
 * The montage, when this component drives it, is only the visual follower: corrected when it drifts further than
 * MaxVisualDriftSeconds from the playhead.
 *
 * Handlers may stop the action, jump, start another or destroy the actor while events are being dispatched. Each call
 * takes effect at the due time of the event being handled: a stop there (later events of that frame do not fire; open
 * windows close there); a jump or a new action restarts the path there, and the rest of the frame is advanced on the
 * new path. Dispatch ends at once if the actor is going away (02 §8).
 *
 * v1 is authority-only (server-initiated actions, 01 §8); S7 adds the replicated action and the client side.
 */
UCLASS(MinimalAPI, ClassGroup = (Animation), meta = (BlueprintSpawnableComponent))
class UAECEventClockComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UE_API UAECEventClockComponent();

	/** Path restarts (a handler that jumps or starts an action) advanced within one frame; the rest waits for the next
	 *  frame, so a handler that jumps on every entry cannot hold the frame. */
	static constexpr int32 MaxPathRestartsPerFrame = 8;

	/** Starts an action now: its events fire from the clock; with bDriveMontage the montage plays too. A running action is
	 *  advanced to now and stopped first. Returns the action id, or INDEX_NONE if Montage is null. */
	UFUNCTION(BlueprintCallable, Category = "AnimEventClock")
	UE_API int32 PlayAction(UAnimMontage* Montage, float PlayRate = 1.f, FName StartSection = NAME_None);

	/** Stops the action now: no later event fires; open windows close (interrupted ends). */
	UFUNCTION(BlueprintCallable, Category = "AnimEventClock")
	UE_API void StopAction();

	/** Moves the action to a section's start now (open windows close; a jump back starts a new pass). */
	UFUNCTION(BlueprintCallable, Category = "AnimEventClock")
	UE_API void JumpToSection(FName Section);

	/** True between a window's begin and end events of the running action; false once the action is over, also while its
	 *  last (interrupted) ends go out. */
	UFUNCTION(BlueprintPure, Category = "AnimEventClock")
	UE_API bool IsWindowOpen(FName Window) const;

	UFUNCTION(BlueprintPure, Category = "AnimEventClock")
	bool IsActionActive() const { return bActive; }

	/** The running action's id, or INDEX_NONE. */
	UFUNCTION(BlueprintPure, Category = "AnimEventClock")
	int32 GetActionId() const { return bActive ? ActionId : INDEX_NONE; }

	/** Every event of the running action, in order. */
	UPROPERTY(BlueprintAssignable, Category = "AnimEventClock")
	FAECClockEventSignature OnClockEvent;

	/** Play the montage on the owner's skeletal mesh and keep it following the playhead. False when something else (GAS,
	 *  a benchmark's own montage path) already plays it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AnimEventClock")
	bool bDriveMontage = true;

	/** A driven montage further than this from the playhead is moved back onto it (02 §7: above any normal throttling). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AnimEventClock", meta = (ClampMin = "0.0"))
	float MaxVisualDriftSeconds = 0.25f;

	/** Blend-out of a driven montage at StopAction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AnimEventClock", meta = (ClampMin = "0.0"))
	float StopBlendOutSeconds = 0.1f;

	//~ Begin UActorComponent
	UE_API virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	UE_API virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

private:
	/** The clock: the world's game time (S7: the estimated server time on clients). */
	double ClockNow() const;

	/** When a call takes effect, after firing what was due until then: now, or from a handler the due time of the event
	 *  being handled, where nothing is advanced (02 §6.1). */
	double CatchUp();

	/** From a handler of this frame's advance the playhead is already at the frame's end: a stop or a jump starts where it
	 *  was at the handled crossing, with the windows handlers saw open. Does nothing otherwise. */
	void RewindHeadToHandledCrossing();

	/** Advances the playhead to Time and dispatches what it crossed, again on the new path after a handler restarted it. */
	void AdvanceTo(double Time);

	/** Broadcasts crossings in order; an end exactly when its begin went out; with aec.Enabled 0 no instant or begin. Due
	 *  times come from the advance (Delta) that ended at Time; forced ends from a jump pass Delta 0 (due at Time). Stops
	 *  when the path changes or the owner goes. */
	void Dispatch(const TArray<FAECCrossing>& ToFire, double Time, float Delta);

	FAECEventInfo MakeEventInfo(const FAECCrossing& Crossing, double DueTime) const;

	/** One event to the handlers (and to the log with aec.Debug). */
	void Broadcast(const FAECEventInfo& Info);

	/** Ends the running action at Time: tick off, a driven montage stopped if interrupted, then the timeline's forced ends
	 *  for the windows handlers saw open. */
	void FinishAction(double Time, bool bInterrupted);

	/** The driven montage follows the playhead (02 §6.1). */
	void FollowMontage();

	/** The anim instance of the owner's skeletal mesh when this component drives the montage (the mesh found once). */
	UAnimInstance* GetDrivenAnimInstance();

	/** The component and its actor are not going away: handlers may still be called. */
	bool IsOwnerAlive() const;

	FAECMontageTimeline Timeline;
	FAECPlayhead Head;
	TArray<FAECCrossing> Crossings;            // reused every frame (no allocation per frame)
	TArray<int32, TInlineAllocator<4>> OpenWindows;   // windows whose begin was broadcast and whose end was not

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> Montage;

	TWeakObjectPtr<USkeletalMeshComponent> DrivenMesh;

	int32 ActionId = INDEX_NONE;               // the last action's id (the next one gets ActionId + 1)
	uint32 PathSerial = 0;                     // +1 at every new action or jump: crossings computed before it are stale
	double LastTime = 0.0;                     // the clock time the playhead has been advanced to
	float PlayRate = 1.f;
	bool bActive = false;
	bool bDispatching = false;                 // handlers are running (calls take effect at DispatchDueTime)
	bool bHeadAhead = false;                   // the playhead is at the end of the advance whose crossings are being dispatched
	double DispatchDueTime = 0.0;              // the due time of the event being handled
	FAECCrossing HandledCrossing;              // the crossing being handled: where a handler's jump starts from
};

#undef UE_API
