// P06 AnimEventClock - editor target. It also builds the editor-only game module AnimEventClockEditor.

using UnrealBuildTool;

public class AnimEventClockEditorTarget : TargetRules
{
	public AnimEventClockEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.AddRange(new string[] { "AnimEventClock", "AnimEventClockEditor" });
	}
}
