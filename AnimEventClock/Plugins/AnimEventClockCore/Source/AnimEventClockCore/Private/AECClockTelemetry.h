// AnimEventClockCore - per-frame counters of every event clock, published once per frame as CSV stats (internal).

#pragma once

#include "CoreTypes.h"

/**
 * Plain counters, incremented by the clock components and published and reset once per frame by the module
 * (AnimEventClockCoreModule.cpp, FCoreDelegates::OnEndFrame). One CSV stat call per frame instead of one per event:
 * UE5-UpdateBudget measured about 50 ns per CSV stat call (02 §4.1). Game thread only.
 */
namespace AnimEventClock::Telemetry
{
	struct FFrameCounters
	{
		int32 EventsFired = 0;         // events broadcast this frame, all clocks
		int32 ActiveActions = 0;       // clocks that ticked an active action this frame
		int32 VisualCorrections = 0;   // driven montages moved back onto their playhead this frame
		uint64 DispatchCycles = 0;     // cycles spent dispatching events this frame (handlers included)
	};

	/** This frame's counts (reset to FFrameCounters() after each publish). */
	extern FFrameCounters Frame;
}
