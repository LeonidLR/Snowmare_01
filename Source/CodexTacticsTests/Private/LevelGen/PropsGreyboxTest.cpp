// CodexTactics.LevelGen.Props.Greybox.* / .Expanded.* - G003 (designer 2026-11-17: "opened X4 and X6 - no barrels, no loot, nothing"):
// - PropsGreybox: the runtime look of a prop from Content/Data/Balance/props_balance.json (kinds sizes, crate_colors, greybox block);
// - FPropsLayoutView accepts a cut-polygon edge longer than MaxSegmentCm (the expanded maps' trench, 354 / 434 m) - before, the whole
//   layout was refused and every prop spawned at Z 0 without validation;
// - the C++ validator keeps every object of the Training Ground autogen props files of L_Gen_Depot_X4 / L_Gen_Depot_1_X6 / L_Gen_Test_X2
//   (the TG shows them all: same props in the game), each on a walk layer the view knows.
// Pure, no world. UE-only (no Godot reference).

#include "LevelGen/LevelPropsFile.h"
#include "LevelGen/PropsGreybox.h"
#include "LevelGen/PropsLayoutView.h"
#include "LevelGen/PropsValidator.h"
#include "Misc/AutomationTest.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PropsGreyboxTestLocal
{
	TSharedPtr<FJsonObject> LoadGreyboxJson(FAutomationTestBase& Test, const FString& RelativeToContent)
	{
		FString Text;
		TSharedPtr<FJsonObject> Root;
		if (!Test.TestTrue(FString::Printf(TEXT("%s readable"), *RelativeToContent),
				FFileHelper::LoadFileToString(Text, *(FPaths::ProjectContentDir() / RelativeToContent))))
		{
			return nullptr;
		}
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root);
		Test.TestTrue(FString::Printf(TEXT("%s parses"), *RelativeToContent), Root.IsValid());
		return Root;
	}

	TSharedPtr<FJsonObject> Balance(FAutomationTestBase& Test)
	{
		return LoadGreyboxJson(Test, TEXT("Data/Balance/props_balance.json"));
	}

	bool ColorNear(const FLinearColor& A, float R, float G, float B)
	{
		return FMath::IsNearlyEqual(A.R, R, 1e-3f) && FMath::IsNearlyEqual(A.G, G, 1e-3f) && FMath::IsNearlyEqual(A.B, B, 1e-3f);
	}

	/** A minimal layout with a cut rectangle whose long edges are LongCm. */
	FString LongCutLayout(int32 LongCm)
	{
		const int32 H = LongCm / 2;
		return FString::Printf(TEXT("{\"schema\":\"codex.levelgen_layout/1\",\"name\":\"longcut\",\"bounds\":{\"min\":[-%d,-3000],\"max\":[%d,3000]},")
			TEXT("\"ground_z\":0,\"spawn\":{\"pos\":[0,-2900]},\"end\":[0,2900],\"walls\":[],\"buildings\":[],\"covers\":[],\"ladders\":[],\"zones\":[],")
			TEXT("\"cut\":{\"poly\":[[-%d,-500],[%d,-500],[%d,500],[-%d,500]],\"level\":-1,\"floor_z\":-300},")
			TEXT("\"routes\":[{\"id\":\"R\",\"mandatory\":true,\"points\":[[0,-2900],[0,2900]]}]}"), H, H, H, H, H, H);
	}
}

// ---- PropsGreybox ---------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPropsGreyboxCratesTest, "CodexTactics.LevelGen.Props.Greybox.CratesBlueSafeRedMined",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPropsGreyboxCratesTest::RunTest(const FString&)
{
	using namespace PropsGreyboxTestLocal;
	const TSharedPtr<FJsonObject> B = Balance(*this);
	const FPropGreyboxSpec Small = PropsGreybox::Resolve(B.Get(), TEXT("crate_s"));
	TestTrue(TEXT("crate_s is a box"), Small.Shape == EPropGreyboxShape::Box);
	TestEqual(TEXT("crate_s size = kinds.crate_s.size_cm"), Small.SizeCm, FVector(120.0, 120.0, 110.0));
	TestTrue(TEXT("crate_s blocks"), Small.bBlocks);
	TestTrue(TEXT("a safe crate is blue (crate_colors.safe)"), ColorNear(Small.Color, 0.04f, 0.2f, 0.85f));
	const FPropGreyboxSpec Mined = PropsGreybox::Resolve(B.Get(), TEXT("crate_l"), /*bDangerous=*/ true);
	TestEqual(TEXT("crate_l size"), Mined.SizeCm, FVector(160.0, 160.0, 140.0));
	TestTrue(TEXT("a mined crate is red (crate_colors.dangerous)"), ColorNear(Mined.Color, 0.85f, 0.04f, 0.03f));
	TestTrue(TEXT("no muzzle / wire on a crate"), Mined.MuzzleCm.IsZero() && Mined.WireLengthCm == 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPropsGreyboxSpecialsTest, "CodexTactics.LevelGen.Props.Greybox.BarrelTurretMineLoot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPropsGreyboxSpecialsTest::RunTest(const FString&)
{
	using namespace PropsGreyboxTestLocal;
	const TSharedPtr<FJsonObject> B = Balance(*this);

	const FPropGreyboxSpec Barrel = PropsGreybox::Resolve(B.Get(), TEXT("barrel_explosive"));
	TestTrue(TEXT("barrel is a cylinder (designer 6.1 #1)"), Barrel.Shape == EPropGreyboxShape::Cylinder);
	TestEqual(TEXT("barrel 60 x 60 x 90"), Barrel.SizeCm, FVector(60.0, 60.0, 90.0));
	TestTrue(TEXT("barrel blocks"), Barrel.bBlocks);
	TestTrue(TEXT("explosive barrel colour"), ColorNear(Barrel.Color, 0.9f, 0.3f, 0.02f));

	const FPropGreyboxSpec Turret = PropsGreybox::Resolve(B.Get(), TEXT("turret_mg"));
	TestTrue(TEXT("turret is a cylinder (kinds.turret_mg.radius_cm)"), Turret.Shape == EPropGreyboxShape::Cylinder);
	TestEqual(TEXT("turret 100 x 100 x greybox height 110"), Turret.SizeCm, FVector(100.0, 100.0, 110.0));
	TestEqual(TEXT("turret muzzle bar"), Turret.MuzzleCm, FVector(90.0, 12.0, 12.0));
	TestTrue(TEXT("turret blocks"), Turret.bBlocks);

	const FPropGreyboxSpec Wire = PropsGreybox::Resolve(B.Get(), TEXT("mine_tripwire"), false, TEXT("tripwire"), 240.f);
	TestEqual(TEXT("mine_* reads the 'mine' kind"), PropsGreybox::KindKeyFor(B.Get(), TEXT("mine_tripwire")), FString(TEXT("mine")));
	TestTrue(TEXT("mine marker is a disc"), Wire.Shape == EPropGreyboxShape::Cylinder);
	TestEqual(TEXT("mine marker size"), Wire.SizeCm, FVector(36.0, 36.0, 8.0));
	TestFalse(TEXT("a mine never blocks (kinds.mine.nav_affecting false)"), Wire.bBlocks);
	TestEqual(TEXT("tripwire spans the doorway width"), Wire.WireLengthCm, 240.f);
	TestEqual(TEXT("tripwire height"), Wire.WireHeightCm, 15.f);
	TestTrue(TEXT("mine is red"), ColorNear(Wire.Color, 1.f, 0.05f, 0.03f));
	const FPropGreyboxSpec Proximity = PropsGreybox::Resolve(B.Get(), TEXT("mine_proximity"), false, TEXT("proximity"), 180.f);
	TestEqual(TEXT("a proximity mine has no wire"), Proximity.WireLengthCm, 0.f);

	const FPropGreyboxSpec Loot = PropsGreybox::Resolve(B.Get(), TEXT("loot_pile"));
	TestTrue(TEXT("loot pile is a box"), Loot.Shape == EPropGreyboxShape::Box);
	TestEqual(TEXT("loot pile 60 x 60 x 35"), Loot.SizeCm, FVector(60.0, 60.0, 35.0));
	TestFalse(TEXT("loot never blocks"), Loot.bBlocks);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPropsGreyboxFallbackTest, "CodexTactics.LevelGen.Props.Greybox.UnknownAndNoBalanceFallBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPropsGreyboxFallbackTest::RunTest(const FString&)
{
	using namespace PropsGreyboxTestLocal;
	const TSharedPtr<FJsonObject> B = Balance(*this);
	const FPropGreyboxSpec Unknown = PropsGreybox::Resolve(B.Get(), TEXT("folding_barricade"));
	TestTrue(TEXT("unknown variant: a box"), Unknown.Shape == EPropGreyboxShape::Box);
	TestEqual(TEXT("unknown variant: 100 cm"), Unknown.SizeCm, FVector(100.0));
	TestTrue(TEXT("unknown variant: greybox.colors.default"), ColorNear(Unknown.Color, 0.5f, 0.5f, 0.5f));
	const FPropGreyboxSpec NoBalance = PropsGreybox::Resolve(nullptr, TEXT("crate_s"), true, TEXT("tripwire"), 200.f);
	TestEqual(TEXT("no balance: 100 cm box"), NoBalance.SizeCm, FVector(100.0));
	TestEqual(TEXT("no balance: no wire"), NoBalance.WireLengthCm, 0.f);
	return true;
}

// ---- the long cut edge ----------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPropsLayoutLongCutTest, "CodexTactics.LevelGen.Props.LayoutView.LongCutEdgeAccepted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPropsLayoutLongCutTest::RunTest(const FString&)
{
	using namespace PropsGreyboxTestLocal;
	FPropsLayoutView View;
	TArray<FString> Errors;
	const bool bOk = FPropsLayoutView::Build(LongCutLayout(43400), View, Errors);
	for (const FString& E : Errors)
	{
		AddError(E);
	}
	TestTrue(TEXT("a 434 m cut edge builds (was refused: 'poly' edge longer than 30000 cm)"), bOk);
	const FPropsWalkLayer* Cut = View.FindLayer(TEXT("cut"));
	if (!TestNotNull(TEXT("cut layer"), Cut))
	{
		return false;
	}
	TestEqual(TEXT("cut Z = ground_z + floor_z"), Cut->ZCm, -300);
	TestTrue(TEXT("inside the trench is walkable on 'cut'"), View.IsWalkable(FIntPoint(18000, 0), TEXT("cut")));
	TestFalse(TEXT("outside the trench is not 'cut'"), View.IsWalkable(FIntPoint(18000, 900), TEXT("cut")));
	// the split cut walls block along the whole long edge - near both ends, the middle and each split point (21700 - 30000 etc.)
	for (const int32 X : { -21650, -8300, 0, 8300, 21650 })
	{
		TestTrue(FString::Printf(TEXT("cut wall blocks at x %d on the long edge"), X), View.IsBlocked(FIntPoint(X, 500), TEXT("cut"), 0));
		TestFalse(FString::Printf(TEXT("100 cm off the edge is free at x %d"), X), View.IsBlocked(FIntPoint(X, 380), TEXT("cut"), 0));
	}
	return true;
}

// ---- the expanded maps: the C++ keeps everything the Training Ground shows ------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPropsExpandedKeepsAllTest, "CodexTactics.LevelGen.Props.Expanded.ValidatorKeepsTrainingGroundProps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPropsExpandedKeepsAllTest::RunTest(const FString&)
{
	using namespace PropsGreyboxTestLocal;
	const TSharedPtr<FJsonObject> B = Balance(*this);
	FPropsLayoutConfig Config;
	TArray<FString> ConfigErrors;
	if (B.IsValid())
	{
		FPropsLayoutView::ReadConfig(*B, Config, ConfigErrors);
	}
	for (const TCHAR* Map : { TEXT("L_Gen_Depot_1_X6") })
	{
		FString PropsJson;
		if (!TestTrue(FString::Printf(TEXT("%s props readable"), Map),
				FFileHelper::LoadFileToString(PropsJson, *(FPaths::ProjectContentDir() / TEXT("Data/LevelGen/props") / (FString(Map) + TEXT(".props.json"))))))
		{
			continue;
		}
		FLevelPropsFile File;
		TArray<FString> ParseErrors;
		if (!TestTrue(FString::Printf(TEXT("%s props parse"), Map), LevelPropsFile::Parse(PropsJson, File, ParseErrors)))
		{
			continue;
		}
		FString LayoutJson;
		if (!TestTrue(FString::Printf(TEXT("%s layout readable"), Map),
				FFileHelper::LoadFileToString(LayoutJson, *(FPaths::ProjectContentDir() / TEXT("Data/LevelGen/accepted") / (File.Layout + TEXT(".json"))))))
		{
			continue;
		}
		FPropsLayoutView View;
		TArray<FString> LayoutErrors;
		const bool bBuilt = FPropsLayoutView::Build(LayoutJson, View, LayoutErrors, Config);
		for (const FString& E : LayoutErrors)
		{
			AddError(FString::Printf(TEXT("%s layout: %s"), Map, *E));
		}
		TestTrue(FString::Printf(TEXT("%s layout builds (the runtime gets walk-layer Z + validation)"), Map), bBuilt);

		const FLevelPropsResolved Resolved = LevelPropsFile::ApplyOverrides(File);
		FPropsValidationResult Result;
		FPropsValidator::ValidateResolved(View, Resolved, Result, B.Get());
		// G003 found 7 per expanded map the game dropped (Depot crates / a barrel kept from L_Gen_Depot_1.props.json over the Depot
		// covers, mines in a stair keep-out disk / doorway corridor); since G003b the TG autogen runs the same validator (validator.ts)
		// and leaves them out, so nothing is dropped and every kind is kept in full.
		TMap<FString, int32> Want;
		TMap<FString, int32> Kept;
		for (const FLevelPropsResolvedObject& Obj : Resolved.Objects)
		{
			Want.FindOrAdd(Obj.LayerId)++;
		}
		for (const FLevelPropsResolvedObject& Obj : Result.ValidObjects)
		{
			Kept.FindOrAdd(Obj.LayerId)++;
		}
		for (const FPropsValidationIssue& Issue : Result.Issues)
		{
			if (Issue.bDropped)
			{
				AddError(FString::Printf(TEXT("%s drops %s (%s): %s"), Map, *Issue.ObjectId, *Issue.LayerId, *Issue.Reason));
			}
		}
		for (const TPair<FString, int32>& Layer : Want)
		{
			const int32 Got = Kept.FindRef(Layer.Key);
			TestEqual(FString::Printf(TEXT("%s %s: every Training Ground object kept"), Map, *Layer.Key), Got, Layer.Value);
		}
		int32 UnknownLayer = 0;
		for (const FLevelPropsResolvedObject& Obj : Resolved.Objects)
		{
			UnknownLayer += View.FindLayer(Obj.Object.WalkLayer) ? 0 : 1;
		}
		TestEqual(FString::Printf(TEXT("%s: every walk layer known (Z from the layout)"), Map), UnknownLayer, 0);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
