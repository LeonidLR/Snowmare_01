#include "Misc/AutomationTest.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"

#if WITH_DEV_AUTOMATION_TESTS

// Every material the editor scripts generate (/Game/VFX/Materials) must have its required inputs connected: a broken one silently renders as the
// default material in game (M_Silhouette / M_TacticalStasis once had an unconnected Clamp input).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVfxMaterialsCompileTest, "CodexTactics.Editor.Materials.VfxGraphsConnected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVfxMaterialsCompileTest::RunTest(const FString&)
{
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	TArray<FAssetData> Assets;
	Registry.GetAssetsByPath(FName(TEXT("/Game/VFX/Materials")), Assets, true);
	int32 Checked = 0;
	for (const FAssetData& Asset : Assets)
	{
		UMaterial* Material = Cast<UMaterial>(Asset.GetAsset());
		if (!Material)
		{
			continue;
		}
		// Headless tests have no RHI (no shader maps): check the graph instead — every required input connected.
		int32 Missing = 0;
		for (UMaterialExpression* Expression : Material->GetExpressions())
		{
			if (!Expression)
			{
				continue;
			}
			for (int32 Index = 0; Expression->GetInput(Index); ++Index)
			{
				if (!Expression->GetInput(Index)->Expression && Expression->IsInputConnectionRequired(Index))
				{
					AddError(FString::Printf(TEXT("%s: %s input '%s' is not connected"), *Material->GetName(),
						*Expression->GetClass()->GetName(), *Expression->GetInputName(Index).ToString()));
					++Missing;
				}
			}
		}
		TestEqual(*FString::Printf(TEXT("%s: required inputs connected"), *Material->GetName()), Missing, 0);
		++Checked;
	}
	TestTrue(TEXT("Materials found under /Game/VFX/Materials"), Checked >= 4);
	AddInfo(FString::Printf(TEXT("%d materials checked"), Checked));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
