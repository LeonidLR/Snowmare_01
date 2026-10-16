using UnrealBuildTool;
using System.Collections.Generic;

public class CodexTacticsTarget : TargetRules
{
	public CodexTacticsTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("CodexTactics");
		// Dev console commands (smokes / probes) only in Debug / Development games, never in Test / Shipping.
		if (Target.Configuration != UnrealTargetConfiguration.Test && Target.Configuration != UnrealTargetConfiguration.Shipping)
		{
			ExtraModuleNames.Add("CodexTacticsDev");
		}
	}
}
