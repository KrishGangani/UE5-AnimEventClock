// P06 AnimEventClock - game target (standalone/packaged builds).

using UnrealBuildTool;

public class AnimEventClockTarget : TargetRules
{
	public AnimEventClockTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("AnimEventClock");
	}
}
