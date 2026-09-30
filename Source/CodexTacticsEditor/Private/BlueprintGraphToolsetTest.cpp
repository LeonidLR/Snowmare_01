#include "Misc/AutomationTest.h"
#include "BlueprintGraphToolset.h"

#if WITH_DEV_AUTOMATION_TESTS

// The Blueprint graph tools read the project's own Blueprints: BP_Operative and ABP_Operative (its AnimGraph).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBlueprintGraphToolsetTest, "CodexTactics.Editor.BlueprintTools.ReadProjectBlueprints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintGraphToolsetTest::RunTest(const FString&)
{
	const FString Found = UBlueprintGraphToolset::FindBlueprints(TEXT("/Game/Characters/Operatives"), TEXT("Operative"));
	TestTrue(TEXT("FindBlueprints lists BP_Operative and ABP_Operative"),
		Found.Contains(TEXT("/Game/Characters/Operatives/BP_Operative.BP_Operative")) && Found.Contains(TEXT("ABP_Operative")));

	const FString Actor = UBlueprintGraphToolset::DescribeBlueprint(TEXT("/Game/Characters/Operatives/BP_Operative"));
	TestTrue(TEXT("BP_Operative: parent OperativeCharacter, graphs listed"), Actor.Contains(TEXT("parent OperativeCharacter")) && Actor.Contains(TEXT("Graphs:")));

	const FString Anim = UBlueprintGraphToolset::DescribeBlueprint(TEXT("/Game/Characters/Operatives/ABP_Operative"));
	TestTrue(TEXT("ABP_Operative: skeleton and AnimGraph"), Anim.Contains(TEXT("Target skeleton: /Game")) && Anim.Contains(TEXT("AnimGraph")));

	const FString Graph = UBlueprintGraphToolset::DumpBlueprintGraph(TEXT("/Game/Characters/Operatives/ABP_Operative"), TEXT("AnimGraph"));
	// The pose is blended natively (UOperativeAnimInstance): the AnimGraph holds only the Output Pose node for now.
	TestTrue(TEXT("AnimGraph dump: nodes with their pins"), Graph.Contains(TEXT("=== Graph 'AnimGraph'")) && Graph.Contains(TEXT("#0 AnimGraphNode_Root"))
		&& Graph.Contains(TEXT("in  Result : Pose Link")));

	// The root node cannot be duplicated; the event graph's node can.
	const FString Text = UBlueprintGraphToolset::ExportGraphNodesText(TEXT("/Game/Characters/Operatives/ABP_Operative"), TEXT("EventGraph"));
	TestTrue(TEXT("EventGraph export in copy-paste format"), Text.Contains(TEXT("Begin Object")));

	TestTrue(TEXT("Missing asset reports an error"), UBlueprintGraphToolset::DescribeBlueprint(TEXT("/Game/Nope/BP_Missing")).StartsWith(TEXT("ERROR")));
	TestTrue(TEXT("Missing graph reports an error"),
		UBlueprintGraphToolset::DumpBlueprintGraph(TEXT("/Game/Characters/Operatives/ABP_Operative"), TEXT("NoSuchGraph")).StartsWith(TEXT("ERROR")));

	const FString Compiled = UBlueprintGraphToolset::CompileBlueprint(TEXT("/Game/Characters/Operatives/ABP_Operative"));
	TestTrue(TEXT("ABP_Operative compiles"), Compiled.StartsWith(TEXT("Compile: OK")) || Compiled.StartsWith(TEXT("Compile: WARNINGS")));
	AddInfo(Compiled);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
