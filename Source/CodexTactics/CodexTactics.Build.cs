using UnrealBuildTool;

public class CodexTactics : ModuleRules
{
	public CodexTactics(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"GameplayTags",
			"AIModule",
			"GameplayTasks",
			"NavigationSystem",
			"Niagara",
			"UMG",
			"Slate",
			"SlateCore",
			"CommonUI",
			"CommonInput",
			"DeveloperSettings",
			"ProceduralMeshComponent",
			"MotionWarping" // the mantle climb (docs/design/MANTLING.md 11.3): root-motion clips warped onto the real ledge top
		});

		PrivateDependencyModuleNames.AddRange(new string[] { "Json", "RenderCore", "AnimationCore", "Navmesh" }); // RenderCore: GWhiteTexture for the minimap canvas triangles; AnimationCore: the ladder rung IK (TwoBoneIK); Navmesh: Detour's nav link directions (FDropLinkReach)
	}
}
