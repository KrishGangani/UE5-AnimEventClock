// AnimEventClockCoreEditor - the reusable editor-only module (data validation of authored montage events, later).
// A separate module because runtime modules must not depend on editor-only modules such as DataValidation.

using UnrealBuildTool;

public class AnimEventClockCoreEditor : ModuleRules
{
	public AnimEventClockCoreEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Phase 4: no code yet. Private, because no other module includes this module's headers.
		PrivateDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
	}
}
