// AnimEventClockCore - shared types: what kind of authored event fired, and the comparison hook for the animation path.

#pragma once

#include "Delegates/Delegate.h"
#include "UObject/NameTypes.h"
#include "UObject/ObjectMacros.h"
#include "AECEventTypes.generated.h"

#define UE_API ANIMEVENTCLOCKCORE_API

class UAnimSequenceBase;
class USkeletalMeshComponent;

/** What happened at an authored event: a single moment, or the start or end of a window. */
UENUM(BlueprintType)
enum class EAECEventPhase : uint8
{
	Instant,
	WindowBegin,
	WindowEnd
};

/**
 * Broadcast when the ANIMATION fires one of this plugin's marker notifies (the engine's normal notify path).
 * Gameplay should not listen to it: it exists so a benchmark or a debug view can record the animation path next to the
 * gameplay-clock path for the very same montage. Game thread only. Costs nothing when nothing is bound.
 */
DECLARE_MULTICAST_DELEGATE_FourParams(FAECMarkerFiredByAnimation,
	const USkeletalMeshComponent* /*MeshComp*/, const UAnimSequenceBase* /*Animation*/, FName /*EventName*/, EAECEventPhase /*Phase*/);

namespace AnimEventClock
{
	/** The comparison hook. One delegate for the whole process, shared by both marker classes. */
	UE_API FAECMarkerFiredByAnimation& OnMarkerFiredByAnimation();
}

#undef UE_API
