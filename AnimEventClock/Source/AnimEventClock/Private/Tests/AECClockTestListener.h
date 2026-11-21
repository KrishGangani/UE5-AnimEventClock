// P06 AnimEventClock (demo) - a test-only listener for the event clock's dispatch tests (AECEventClockTests.cpp).

#pragma once

#include "AECEventClockComponent.h"
#include "UObject/Object.h"
#include "AECClockTestListener.generated.h"

class UAnimMontage;

/** Records every event it receives and, once, reacts to a chosen one the way a gameplay handler might. */
UCLASS(Transient)
class UAECClockTestListener : public UObject
{
	GENERATED_BODY()

public:
	enum class EReaction : uint8
	{
		None,
		DestroyOwner,   // the handler destroys the actor that owns the clock
		StopAction,     // the handler stops the action
		PlayAnother,    // the handler starts another action on the same clock
		JumpTo          // the handler jumps to another section of the action
	};

	/** What to do, and on which event (by name and phase). */
	EReaction Reaction = EReaction::None;
	FName ReactTo;
	EAECEventPhase ReactToPhase = EAECEventPhase::Instant;

	void ReactOn(EReaction InReaction, FName Name, EAECEventPhase Phase)
	{
		Reaction = InReaction;
		ReactTo = Name;
		ReactToPhase = Phase;
	}

	UPROPERTY()
	TObjectPtr<UAnimMontage> Another;

	FName JumpSection;

	TArray<FAECEventInfo> Received;

	UFUNCTION()
	void HandleEvent(const FAECEventInfo& Event);

private:
	bool bReacted = false;
};
