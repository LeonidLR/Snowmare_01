using UnrealBuildTool;

public class CodexTacticsTests : ModuleRules
{
	public CodexTacticsTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"CodexTactics"
		});
	}
}
