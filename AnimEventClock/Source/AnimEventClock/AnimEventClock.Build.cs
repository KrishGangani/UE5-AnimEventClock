// P06 AnimEventClock - primary game module: demo characters and the benchmark harness only.
// The reusable system lives in the AnimEventClockCore plugin; this module will depend on it, never the reverse.

using UnrealBuildTool;

public class AnimEventClock : ModuleRules
{
	public AnimEventClock(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Each step adds the modules its code uses, no earlier.
		// AnimEventClockCore is public: AECBenchSubsystem.h (a public header) uses its EAECEventPhase.
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "AnimEventClockCore" });

		// AnimationBudgetAllocator is private: only .cpp files use USkeletalMeshComponentBudgeted (budget conditions).
		PrivateDependencyModuleNames.AddRange(new string[] { "AnimationBudgetAllocator" });
	}
}
