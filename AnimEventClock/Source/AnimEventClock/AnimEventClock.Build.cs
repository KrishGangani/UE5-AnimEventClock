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
		// GameplayAbilities is public: AECDemoGasCharacter.h and AECDemoAttackAbility.h (public headers) derive from its types (S4).
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "AnimEventClockCore", "GameplayAbilities" });

		// AnimationBudgetAllocator: only .cpp files use USkeletalMeshComponentBudgeted (budget conditions).
		// GameplayTasks and GameplayTags: the .cpp files call into them through GAS (the montage task's activation, the event data).
		PrivateDependencyModuleNames.AddRange(new string[] { "AnimationBudgetAllocator", "GameplayTasks", "GameplayTags" });
	}
}
