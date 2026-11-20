// AnimEventClockCore - shared types.

#include "AECEventTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AECEventTypes)

FAECMarkerFiredByAnimation& AnimEventClock::OnMarkerFiredByAnimation()
{
	// A function-local static: built on first use, so it exists before any notify can fire, in every module.
	static FAECMarkerFiredByAnimation Delegate;
	return Delegate;
}
