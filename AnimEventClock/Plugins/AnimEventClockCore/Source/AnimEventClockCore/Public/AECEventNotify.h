// AnimEventClockCore - authoring marker for an instant gameplay event, placed in a montage like any notify.

#pragma once

#include "Animation/AnimNotifies/AnimNotify.h"
#include "AECEventNotify.generated.h"

#define UE_API ANIMEVENTCLOCKCORE_API

/**
 * Marks the moment of a gameplay event (a hit, a spawn point, an ability commit) in a montage.
 *
 * Designers place it in the montage editor exactly like any other notify. The gameplay clock reads its position from the
 * montage and fires the event on time; when the ANIMATION reaches it, this class only reports to the comparison hook
 * (AnimEventClock::OnMarkerFiredByAnimation), so gameplay never depends on the animation update.
 */
UCLASS(MinimalAPI, meta = (DisplayName = "AEC Event"))
class UAECEventNotify : public UAnimNotify
{
	GENERATED_BODY()

public:
	/** The gameplay name of this event. Unique within a montage: handlers and the validator key on it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AnimEventClock")
	FName EventName;

	//~ Begin UAnimNotify
	UE_API virtual FString GetNotifyName_Implementation() const override;
	UE_API virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	//~ End UAnimNotify
};

#undef UE_API
