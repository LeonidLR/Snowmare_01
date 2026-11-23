using UnrealBuildTool;

// Developer-only module (audit 2026-10-11 #3): the dev console commands of Private/Debug (smokes, probes, shot commands,
// the event bus probe). Type DeveloperTool in CodexTactics.uproject, like CodexTacticsTests: built for the editor and the
// Debug / Development game targets, never for Test / Shipping, so a shipped game carries no smoke code.
public class CodexTacticsDev : ModuleRules
{
	public CodexTacticsDev(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"GameplayTags",
			"AIModule",
			"NavigationSystem",
			"UMG",
			"Slate",
			"SlateCore",
			"CommonUI",
			"Json",
			"RHI",        // EnvironmentShots snowcover perf A/B: GNumDrawCallsRHI / RHIGetGPUFrameCycles
			"RenderCore", // ... GRenderThreadTime / GGameThreadTime
			// Learning Agents probe P0 (2026-11-23): the PMC arena's Policy=train|infer (Debug/PmcPolicyTraining) - the PPO trainer,
			// the training environment and the Python trainer process (LearningTraining: FSharedMemoryTrainerServerProcess).
			"LearningAgents",
			"LearningAgentsTraining",
			"Learning",
			"LearningTraining",
			"CodexTactics"
		});

		if (Target.bBuildEditor)
		{
			// StartFlickerShots: the PIE variant (CodexTactics.PIEFlickerShots loads a map and starts PIE in the level viewport).
			PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "LevelEditor" });
		}
	}
}
