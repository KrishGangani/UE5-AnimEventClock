// P06 AnimEventClock - primary game module: demo characters and the benchmark harness only.
// The reusable system lives in the AnimEventClockCore plugin; this module will depend on it, never the reverse.

using UnrealBuildTool;

public class AnimEventClock : ModuleRules
{
	public AnimEventClock(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Phase 4: only the three modules every gameplay module needs. Each later step adds the modules its code uses.
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
	}
}
