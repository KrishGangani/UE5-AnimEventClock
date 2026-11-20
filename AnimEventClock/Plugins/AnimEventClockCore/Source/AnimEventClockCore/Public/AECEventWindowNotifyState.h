// AnimEventClockCore - authoring marker for a gameplay window (a hit window, a combo window), placed like a notify state.

#pragma once

#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AECEventWindowNotifyState.generated.h"

#define UE_API ANIMEVENTCLOCKCORE_API

/**
 * Marks a window of gameplay time in a montage: it opens at the start of the notify state and closes at its end.
 *
 * Like UAECEventNotify, it is an authoring marker. The gameplay clock reads its begin and end positions from the montage;
 * when the ANIMATION begins or ends it, this class only reports to the comparison hook.
 */
UCLASS(MinimalAPI, meta = (DisplayName = "AEC Window"))
class UAECEventWindowNotifyState : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	/** The gameplay name of this window. Unique within a montage. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AnimEventClock")
	FName EventName;

	//~ Begin UAnimNotifyState
	UE_API virtual FString GetNotifyName_Implementation() const override;
	UE_API virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	UE_API virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	//~ End UAnimNotifyState
};

#undef UE_API
