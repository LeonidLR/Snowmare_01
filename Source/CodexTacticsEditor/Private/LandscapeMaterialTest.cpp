#include "Misc/AutomationTest.h"

#include "LandscapeAuthoringLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionParameter.h"
#include "Materials/MaterialExpressionPixelNormalWS.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSmoothStep.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialInstanceConstant.h"

#if WITH_DEV_AUTOMATION_TESTS

// Environment §13 (docs/systems/environment-weather.md): the fixed landscape material and its snow functions, checked on the graph (headless
// tests have no RHI; the SM6 compile is proven by Scripts/Editor/landscape_snow_patch.py stats and the terrain stills).

namespace LandscapeMaterialTestLocal
{
	const TCHAR* MasterPath = TEXT("/Game/VFX/Materials/M_Landscape_Master.M_Landscape_Master");
	const TCHAR* InstancePath = TEXT("/Game/VFX/Materials/MI_Landscape_Master.MI_Landscape_Master");
	const TCHAR* FunctionPaths[] = {
		TEXT("/Game/Environment/Weather/Snow/Materials/MF_SnowPowder.MF_SnowPowder"),
		TEXT("/Game/Environment/Weather/Snow/Materials/MF_SnowSurface.MF_SnowSurface"),
		TEXT("/Game/Environment/Weather/Snow/Materials/MF_SnowInteraction.MF_SnowInteraction"),
	};

	struct FScalarSpec { const TCHAR* Name; float Default; const TCHAR* Group; };
	struct FVectorSpec { const TCHAR* Name; FLinearColor Default; const TCHAR* Group; };

	// The user's parameters as found before the fix (Saved/Logs/landscape_snow_dump.txt); an empty group = not checked.
	const FScalarSpec Scalars[] = {
		{ TEXT("RockSlopeMinAngle"), 35.f, TEXT("01_AutoSlope") }, { TEXT("RockSlopeMaxAngle"), 50.f, TEXT("01_AutoSlope") },
		{ TEXT("IceMaxHeight"), -100.f, TEXT("02_AutoHeight") }, { TEXT("IceMinHeight"), -250.f, TEXT("02_AutoHeight") },
		{ TEXT("Rock_Tiling"), 1.f, TEXT("") }, { TEXT("Snow_Tiling"), 1.f, TEXT("") }, { TEXT("Ice_Tiling"), 1.f, TEXT("") },
		{ TEXT("TrackAreaSize"), 4000.f, TEXT("04_SnowTracks") }, { TEXT("TrackRoughnessDrop"), 0.45f, TEXT("04_SnowTracks") },
		{ TEXT("TrackMaxWPODepth"), 4.f, TEXT("04_SnowTracks") }, { TEXT("RockRoughness"), 0.85f, TEXT("06_LayerRoughness") },
		{ TEXT("SnowRoughness"), 0.65f, TEXT("06_LayerRoughness") }, { TEXT("IceRoughness"), 0.08f, TEXT("06_LayerRoughness") },
	};
	const FVectorSpec Vectors[] = {
		{ TEXT("TrackCenterLocation"), FLinearColor(0.f, 0.f, 0.f, 0.f), TEXT("04_SnowTracks") },
		{ TEXT("TrackIndentColor"), FLinearColor(0.48f, 0.65f, 0.77f, 1.f), TEXT("04_SnowTracks") },
		{ TEXT("RockBaseColor"), FLinearColor(0.22f, 0.23f, 0.25f, 1.f), TEXT("05_LayerColors") },
		{ TEXT("SnowBaseColor"), FLinearColor(0.85f, 0.92f, 0.98f, 1.f), TEXT("05_LayerColors") },
		{ TEXT("IceBaseColor"), FLinearColor(0.45f, 0.72f, 0.85f, 1.f), TEXT("05_LayerColors") },
	};

	UMaterialExpressionParameter* FindParameter(UMaterial* Material, FName Name)
	{
		for (UMaterialExpression* Expression : Material->GetExpressions())
		{
			UMaterialExpressionParameter* Parameter = Cast<UMaterialExpressionParameter>(Expression);
			if (Parameter && Parameter->ParameterName == Name)
			{
				return Parameter;
			}
		}
		return nullptr;
	}

	FString MaskText(const UMaterialExpressionComponentMask& Mask)
	{
		return FString::Printf(TEXT("%s%s%s%s"), Mask.R ? TEXT("r") : TEXT(""), Mask.G ? TEXT("g") : TEXT(""), Mask.B ? TEXT("b") : TEXT(""), Mask.A ? TEXT("a") : TEXT(""));
	}

	const UMaterialFunctionInterface* CalledFunction(const FExpressionInput& Input)
	{
		const UMaterialExpressionMaterialFunctionCall* Call = Cast<UMaterialExpressionMaterialFunctionCall>(Input.Expression);
		return Call ? Call->MaterialFunction.Get() : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandscapeMaterialGraphTest, "CodexTactics.Environment.LandscapeMaterial.Graph",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLandscapeMaterialGraphTest::RunTest(const FString&)
{
	using namespace LandscapeMaterialTestLocal;
	UMaterial* Material = LoadObject<UMaterial>(nullptr, MasterPath);
	if (!TestNotNull(TEXT("M_Landscape_Master loads"), Material))
	{
		return false;
	}

	// Every existing parameter kept: name, default, group.
	for (const FScalarSpec& Spec : Scalars)
	{
		const UMaterialExpressionScalarParameter* Parameter = Cast<UMaterialExpressionScalarParameter>(FindParameter(Material, Spec.Name));
		if (TestNotNull(*FString::Printf(TEXT("scalar %s kept"), Spec.Name), Parameter))
		{
			TestEqual(*FString::Printf(TEXT("scalar %s default"), Spec.Name), Parameter->DefaultValue, Spec.Default, 1e-4f);
			if (*Spec.Group)
			{
				TestEqual(*FString::Printf(TEXT("scalar %s group"), Spec.Name), Parameter->Group.ToString(), FString(Spec.Group));
			}
		}
	}
	for (const FVectorSpec& Spec : Vectors)
	{
		const UMaterialExpressionVectorParameter* Parameter = Cast<UMaterialExpressionVectorParameter>(FindParameter(Material, Spec.Name));
		if (TestNotNull(*FString::Printf(TEXT("vector %s kept"), Spec.Name), Parameter))
		{
			TestTrue(*FString::Printf(TEXT("vector %s default"), Spec.Name), Parameter->DefaultValue.Equals(Spec.Default, 1e-4f));
			TestEqual(*FString::Printf(TEXT("vector %s group"), Spec.Name), Parameter->Group.ToString(), FString(Spec.Group));
		}
	}
	const UMaterialExpressionTextureSampleParameter2D* OldTracks = nullptr;
	for (UMaterialExpression* Expression : Material->GetExpressions())
	{
		const UMaterialExpressionTextureSampleParameter2D* Texture = Cast<UMaterialExpressionTextureSampleParameter2D>(Expression);
		OldTracks = Texture && Texture->ParameterName == TEXT("RT_SnowTracks") ? Texture : OldTracks;
	}
	if (TestNotNull(TEXT("texture RT_SnowTracks kept (unused)"), OldTracks))
	{
		TestEqual(TEXT("RT_SnowTracks group"), OldTracks->Group.ToString(), FString(TEXT("04_SnowTracks")));
	}

	// The fix: slope from PixelNormalWS.z, ice from WorldPosition.z (scalars), no WPO.
	int32 SlopeMasks = 0;
	int32 IceMasks = 0;
	for (UMaterialExpression* Expression : Material->GetExpressions())
	{
		const UMaterialExpressionComponentMask* Mask = Cast<UMaterialExpressionComponentMask>(Expression);
		if (!Mask)
		{
			continue;
		}
		if (Cast<UMaterialExpressionPixelNormalWS>(Mask->Input.Expression))
		{
			++SlopeMasks;
			TestEqual(TEXT("slope mask reads PixelNormalWS.z only"), MaskText(*Mask), FString(TEXT("b")));
		}
		if (Cast<UMaterialExpressionWorldPosition>(Mask->Input.Expression) && Mask->B && !Mask->Desc.StartsWith(TEXT("landsnow:")))
		{
			// The ice mask feeds the SmoothStep IceMaxHeight -> IceMinHeight.
			for (UMaterialExpression* Other : Material->GetExpressions())
			{
				const UMaterialExpressionSmoothStep* Step = Cast<UMaterialExpressionSmoothStep>(Other);
				if (Step && Step->Value.Expression == Mask)
				{
					++IceMasks;
					TestEqual(TEXT("ice mask reads WorldPosition.z only"), MaskText(*Mask), FString(TEXT("b")));
				}
			}
		}
	}
	TestEqual(TEXT("one slope mask"), SlopeMasks, 1);
	TestEqual(TEXT("one ice mask"), IceMasks, 1);

	const UMaterialEditorOnlyData* Data = Material->GetEditorOnlyData();
	if (!TestNotNull(TEXT("editor-only data"), Data))
	{
		return false;
	}
	TestFalse(TEXT("World Position Offset disconnected (no landscape WPO track depth)"), Data->WorldPositionOffset.IsConnected());

	// The snow functions drive the outputs.
	const UMaterialFunctionInterface* Interaction = LoadObject<UMaterialFunctionInterface>(nullptr, FunctionPaths[2]);
	const UMaterialFunctionInterface* Surface = LoadObject<UMaterialFunctionInterface>(nullptr, FunctionPaths[1]);
	for (const TCHAR* Path : FunctionPaths)
	{
		TestNotNull(*FString::Printf(TEXT("%s exists"), Path), LoadObject<UMaterialFunctionInterface>(nullptr, Path));
	}
	TestTrue(TEXT("BaseColor <- MF_SnowInteraction"), Interaction && CalledFunction(Data->BaseColor) == Interaction);
	TestTrue(TEXT("Roughness <- MF_SnowInteraction"), Interaction && CalledFunction(Data->Roughness) == Interaction);
	TestTrue(TEXT("Normal <- MF_SnowInteraction"), Interaction && CalledFunction(Data->Normal) == Interaction);
	TestTrue(TEXT("AO <- MF_SnowInteraction"), Interaction && CalledFunction(Data->AmbientOcclusion) == Interaction);
	TestTrue(TEXT("Specular <- MF_SnowSurface"), Surface && CalledFunction(Data->Specular) == Surface);
	int32 PowderCalls = 0;
	for (UMaterialExpression* Expression : Material->GetExpressions())
	{
		const UMaterialExpressionMaterialFunctionCall* Call = Cast<UMaterialExpressionMaterialFunctionCall>(Expression);
		PowderCalls += Call && Call->MaterialFunction && Call->MaterialFunction->GetPathName() == FunctionPaths[0] ? 1 : 0;
	}
	TestEqual(TEXT("one MF_SnowPowder call"), PowderCalls, 1);

	// The instance no longer loads the old RT_SnowTracks render target.
	const UMaterialInstanceConstant* Instance = LoadObject<UMaterialInstanceConstant>(nullptr, InstancePath);
	if (TestNotNull(TEXT("MI_Landscape_Master loads"), Instance))
	{
		TestTrue(TEXT("MI parent is M_Landscape_Master"), Instance->Parent == Material);
		for (const FTextureParameterValue& Value : Instance->TextureParameterValues)
		{
			if (Value.ParameterInfo.Name == TEXT("RT_SnowTracks"))
			{
				TestFalse(TEXT("MI RT_SnowTracks override is not the old RT_SnowTracks"),
					Value.ParameterValue && Value.ParameterValue->GetPathName().StartsWith(TEXT("/Game/VFX/SnowTracks/RT_SnowTracks")));
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandscapeEncodeHeightTest, "CodexTactics.Environment.LandscapeMaterial.EncodeHeight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLandscapeEncodeHeightTest::RunTest(const FString&)
{
	// The test terrain heights (create_snow_terrain_test_map.py): 32768 = 0; 128 steps per Z-scale unit (1 cm at Z scale 128, 0.78 cm at 100).
	TestEqual(TEXT("0 cm"), static_cast<int32>(ULandscapeAuthoringLibrary::EncodeHeight(0.f, 100.f)), 32768);
	TestEqual(TEXT("+100 cm at Z scale 100"), static_cast<int32>(ULandscapeAuthoringLibrary::EncodeHeight(100.f, 100.f)), 32768 + 128);
	TestEqual(TEXT("-420 cm at Z scale 100 (the bowl floor)"), static_cast<int32>(ULandscapeAuthoringLibrary::EncodeHeight(-420.f, 100.f)), 32768 - 538);
	TestEqual(TEXT("clamped high"), static_cast<int32>(ULandscapeAuthoringLibrary::EncodeHeight(1.e7f, 100.f)), 65535);
	TestEqual(TEXT("clamped low"), static_cast<int32>(ULandscapeAuthoringLibrary::EncodeHeight(-1.e7f, 100.f)), 0);
	TestEqual(TEXT("a zero Z scale does not divide by zero"), static_cast<int32>(ULandscapeAuthoringLibrary::EncodeHeight(0.f, 0.f)), 32768);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
