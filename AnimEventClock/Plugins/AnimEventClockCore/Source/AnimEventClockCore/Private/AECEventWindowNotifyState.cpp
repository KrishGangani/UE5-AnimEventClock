// AnimEventClockCore - UAECEventWindowNotifyState.

#include "AECEventWindowNotifyState.h"

#include "AECEventTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AECEventWindowNotifyState)

FString UAECEventWindowNotifyState::GetNotifyName_Implementation() const
{
	return EventName.IsNone() ? FString(TEXT("AEC Window")) : FString::Printf(TEXT("AEC %s"), *EventName.ToString());
}

void UAECEventWindowNotifyState::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	AnimEventClock::OnMarkerFiredByAnimation().Broadcast(MeshComp, Animation, EventName, EAECEventPhase::WindowBegin);
}

void UAECEventWindowNotifyState::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);
	AnimEventClock::OnMarkerFiredByAnimation().Broadcast(MeshComp, Animation, EventName, EAECEventPhase::WindowEnd);
}
