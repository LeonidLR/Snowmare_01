using UnrealBuildTool;

public class CodexTacticsEditor : ModuleRules
{
	public CodexTacticsEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UnrealEd",
			"BlueprintGraph",
			"Kismet",
			"AssetRegistry",
			"ToolsetRegistry",
			"AnimGraph",
			"AnimGraphRuntime",
			"UMG",
			"UMGEditor",
			"CommonUI",
			"CodexTactics"
		});
	}
}
