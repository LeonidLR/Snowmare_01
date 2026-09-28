using UnrealBuildTool;
using System.Collections.Generic;

public class CodexTacticsEditorTarget : TargetRules
{
	public CodexTacticsEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.AddRange(new string[] { "CodexTactics", "CodexTacticsTests" });
	}
}
