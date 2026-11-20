// P06 AnimEventClock - editor-only game module: tools that create this project's demo assets (a commandlet, later).
// Editor-only, so packaged builds never contain it.

using UnrealBuildTool;

public class AnimEventClockEditor : ModuleRules
{
	public AnimEventClockEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Phase 4: no code yet. Private, because no other module includes this module's headers.
		PrivateDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
	}
}
