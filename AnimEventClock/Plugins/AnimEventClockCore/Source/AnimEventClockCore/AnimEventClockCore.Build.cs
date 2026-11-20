// AnimEventClockCore - the reusable runtime module (montage timeline, event clock component, clock sync, telemetry).
// It must not depend on any game module.

using UnrealBuildTool;

public class AnimEventClockCore : ModuleRules
{
	public AnimEventClockCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
	}
}
