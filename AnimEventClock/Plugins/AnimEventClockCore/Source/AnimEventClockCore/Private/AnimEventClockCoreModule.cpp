// AnimEventClockCore - module: publishes the event clocks' per-frame counters as CSV stats.

#include "AECClockTelemetry.h"
#include "HAL/PlatformTime.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"
#include "ProfilingDebugging/CsvProfiler.h"

CSV_DEFINE_CATEGORY(AnimEventClock, true);

namespace AnimEventClock::Telemetry
{
	FFrameCounters Frame;

	/** Once per frame (OnEndFrame): one stat call per counter, then a fresh frame. */
	static void PublishAndReset()
	{
		CSV_CUSTOM_STAT(AnimEventClock, EventsFired, Frame.EventsFired, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(AnimEventClock, ActiveActions, Frame.ActiveActions, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(AnimEventClock, VisualCorrections, Frame.VisualCorrections, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(AnimEventClock, DispatchUs, static_cast<float>(FPlatformTime::ToSeconds64(Frame.DispatchCycles) * 1e6), ECsvCustomStatOp::Set);
		Frame = FFrameCounters();
	}
}

class FAnimEventClockCoreModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		EndFrameHandle = FCoreDelegates::OnEndFrame.AddStatic(&AnimEventClock::Telemetry::PublishAndReset);
	}

	virtual void ShutdownModule() override
	{
		FCoreDelegates::OnEndFrame.Remove(EndFrameHandle);
	}

private:
	FDelegateHandle EndFrameHandle;
};

IMPLEMENT_MODULE(FAnimEventClockCoreModule, AnimEventClockCore);
