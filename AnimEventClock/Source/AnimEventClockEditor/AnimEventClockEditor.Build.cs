// P06 AnimEventClock - editor-only game module: the commandlet that creates this project's demo montages.
// Editor-only, so packaged builds never contain it.

using UnrealBuildTool;

public class AnimEventClockEditor : ModuleRules
{
	public AnimEventClockEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Private, because no other module includes this module's headers.
		PrivateDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine",
			"UnrealEd",                   // UCommandlet hosting, UAnimMontageFactory (the editor's "Create Montage")
			"AnimationBlueprintLibrary",  // the editor's scripting API for notify tracks and notifies
			"AnimEventClockCore",         // the marker notify classes placed in the montages
			"AnimEventClock"              // AECDemoMontages.h: the authored content of the demo montages
		});
	}
}
