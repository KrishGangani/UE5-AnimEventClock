// AnimEventClockCore - UAECEventNotify.

#include "AECEventNotify.h"

#include "AECEventTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AECEventNotify)

FString UAECEventNotify::GetNotifyName_Implementation() const
{
	// The label shown on the notify track, so the montage editor shows the gameplay name, not the class name.
	return EventName.IsNone() ? FString(TEXT("AEC Event")) : FString::Printf(TEXT("AEC %s"), *EventName.ToString());
}

void UAECEventNotify::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);
	AnimEventClock::OnMarkerFiredByAnimation().Broadcast(MeshComp, Animation, EventName, EAECEventPhase::Instant);
}
