// CodexTactics.LevelGen.Props.C4.* - level authoring stage 1, task C4 (docs/design/levelgen/PROPS_LAYERS.md 2.3, 5 C4, 6.1, 7): turrets
// (coverage >= min_covered_m, the sight rules), mines (doorways by N and by ceil(p x eligible), mandatory-route doors forbidden unless the
// layer opts in, crate mines by % of the unlocked crates and dropped / reported when the crate is gone, the field), loot within the
// budget / tables, same seed -> identical, on fixture_small and the accepted Depot layout, plus the C4 golden outputs the TS port must
// reproduce byte for byte. Pure, no world. UE-only (no Godot reference).
// Regenerate the golden files: set CODEX_PROPS_GOLDEN_WRITE=1, run CodexTactics.LevelGen.Props.C4.Golden, commit them.

#include "LevelGen/PropsPlacementRules.h"
#include "Misc/AutomationTest.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/PlatformMisc.h"
#include "LevelGen/LevelPropsFile.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

#define PROPS_C4_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.LevelGen.Props.C4." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace PropsC4TestLocal
{
	const TCHAR* const GC4Fixtures = TEXT("Data/LevelGen/props/fixtures");
	const TCHAR* const GC4SmallLayout = TEXT("fixture_small.layout.json");
	const TCHAR* const GC4SmallInput = TEXT("golden_in_small_c4.props.json");
	const TCHAR* const GC4DepotLayout = TEXT("../../accepted/gen_ref_01_02_depot_1.json");
	/** The C3 Depot output with crates (3 locked, 1 moved, 1 hand) + barrels; the C4 layers come from golden_in_depot_c4_layers. */
	const TCHAR* const GC4DepotBase = TEXT("golden_depot_barrels_s1.json");
	const TCHAR* const GC4DepotLayers = TEXT("golden_in_depot_c4_layers.props.json");

	FString C4Path(const FString& Relative)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / GC4Fixtures / Relative);
	}

	bool C4LoadText(FAutomationTestBase& Test, const FString& Relative, FString& Out)
	{
		return Test.TestTrue(FString::Printf(TEXT("%s readable"), *Relative), FFileHelper::LoadFileToString(Out, *C4Path(Relative)));
	}

	struct FC4World
	{
		TSharedPtr<FJsonObject> Balance;
		FPropsLayoutView View;
		FLevelPropsFile File;
	};

	bool C4ParseProps(FAutomationTestBase& Test, const FString& Relative, FLevelPropsFile& Out)
	{
		FString Json;
		if (!C4LoadText(Test, Relative, Json))
		{
			return false;
		}
		TArray<FString> Errors;
		const bool bOk = LevelPropsFile::Parse(Json, Out, Errors);
		for (const FString& E : Errors)
		{
			Test.AddError(FString::Printf(TEXT("%s: %s"), *Relative, *E));
		}
		return Test.TestTrue(FString::Printf(TEXT("%s parses"), *Relative), bOk);
	}

	bool C4LoadBase(FAutomationTestBase& Test, const FString& Layout, FC4World& Out)
	{
		FString Json;
		if (!C4LoadText(Test, TEXT("../../../Balance/props_balance.json"), Json)
			|| !Test.TestTrue(TEXT("props_balance.json parses"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Out.Balance) && Out.Balance.IsValid()))
		{
			return false;
		}
		FString LayoutJson;
		if (!C4LoadText(Test, Layout, LayoutJson))
		{
			return false;
		}
		TArray<FString> Errors;
		FPropsLayoutConfig Config;
		Test.TestTrue(TEXT("layout_view config reads"), FPropsLayoutView::ReadConfig(*Out.Balance, Config, Errors));
		const bool bOk = FPropsLayoutView::Build(LayoutJson, Out.View, Errors, Config);
		for (const FString& E : Errors)
		{
			Test.AddError(FString::Printf(TEXT("%s: %s"), *Layout, *E));
		}
		return Test.TestTrue(FString::Printf(TEXT("%s builds"), *Layout), bOk);
	}

	/** The small C4 world: fixture_small + golden_in_small_c4. */
	bool LoadSmall(FAutomationTestBase& Test, FC4World& Out)
	{
		return C4LoadBase(Test, GC4SmallLayout, Out) && C4ParseProps(Test, GC4SmallInput, Out.File);
	}

	/** The Depot C4 world: the C3 crates + barrels output with the three C4 layers appended (the golden recipe). */
	bool LoadDepot(FAutomationTestBase& Test, FC4World& Out)
	{
		FLevelPropsFile Layers;
		if (!C4LoadBase(Test, GC4DepotLayout, Out) || !C4ParseProps(Test, GC4DepotBase, Out.File) || !C4ParseProps(Test, GC4DepotLayers, Layers))
		{
			return false;
		}
		Out.File.Layers.Append(Layers.Layers);
		return true;
	}

	FLevelPropsLayer* C4FindLayer(FLevelPropsFile& File, const TCHAR* Id)
	{
		return File.Layers.FindByPredicate([Id](const FLevelPropsLayer& L) { return L.Id == Id; });
	}

	/** Re-rolls Layer of World.File in place; returns the written file (empty on refusal). */
	FString RerollInPlace(FAutomationTestBase& Test, FC4World& World, const TCHAR* Layer, uint32 Seed, FPropsPlacementResult* OutResult = nullptr)
	{
		TArray<FString> Errors;
		const bool bOk = PropsPlacementRules::RerollLayer(World.File, *World.Balance, World.View, Layer, Seed, Errors, OutResult);
		for (const FString& E : Errors)
		{
			Test.AddError(E);
		}
		return Test.TestTrue(FString::Printf(TEXT("re-roll %s seed %u"), Layer, Seed), bOk) ? LevelPropsFile::Write(World.File) : FString();
	}

	FString RerollCopy(FAutomationTestBase& Test, const FC4World& World, const TCHAR* Layer, uint32 Seed, FPropsPlacementResult* OutResult = nullptr)
	{
		FC4World Copy;
		Copy.Balance = World.Balance;
		Copy.View = World.View;
		Copy.File = World.File;
		return RerollInPlace(Test, Copy, Layer, Seed, OutResult);
	}

	bool C4MakeInput(FAutomationTestBase& Test, const FC4World& World, const TCHAR* Layer, FPropsPlacementInput& Out)
	{
		TArray<FString> Errors;
		const bool bOk = PropsPlacementRules::MakeInput(World.File, *World.Balance, World.View, Layer, Out, Errors);
		for (const FString& E : Errors)
		{
			Test.AddError(E);
		}
		return Test.TestTrue(FString::Printf(TEXT("input for %s"), Layer), bOk && Errors.IsEmpty());
	}

	int64 C4DistSq(FIntPoint A, FIntPoint B)
	{
		const int64 DX = static_cast<int64>(A.X) - B.X;
		const int64 DY = static_cast<int64>(A.Y) - B.Y;
		return DX * DX + DY * DY;
	}

	/** Validate (no violations) + ids unique and in format + report arithmetic. */
	void C4CheckInvariants(FAutomationTestBase& Test, const FString& Ctx, const FPropsPlacementInput& In, const FPropsPlacementResult& Result)
	{
		for (const FPropsViolation& V : PropsPlacementRules::Validate(In, Result.Objects))
		{
			Test.AddError(FString::Printf(TEXT("%s: %s: %s"), *Ctx, *V.Id, *V.Reason));
		}
		TSet<FString> Seen;
		for (const FLevelPropsObject& O : Result.Objects)
		{
			Test.TestFalse(FString::Printf(TEXT("%s: id %s unique"), *Ctx, *O.Id), Seen.Contains(O.Id));
			Seen.Add(O.Id);
			const bool bKept = In.Kept.ContainsByPredicate([&O](const FLevelPropsObject& K) { return K.Id == O.Id; });
			if (!bKept)
			{
				Test.TestFalse(FString::Printf(TEXT("%s: new id %s not reserved"), *Ctx, *O.Id), In.ReservedIds.Contains(O.Id));
				Test.TestTrue(FString::Printf(TEXT("%s: id format %s"), *Ctx, *O.Id), O.Id.StartsWith(FString::Printf(TEXT("%s_%08x_"), *In.LayerId, In.Seed)));
			}
		}
		Test.TestEqual(FString::Printf(TEXT("%s: objects = kept + placed"), *Ctx), Result.Objects.Num(), Result.Report.Kept + Result.Report.Placed);
	}

	TArray<const FLevelPropsObject*> WithDoor(const FPropsPlacementResult& R)
	{
		TArray<const FLevelPropsObject*> Out;
		for (const FLevelPropsObject& O : R.Objects)
		{
			if (!O.Door.IsEmpty())
			{
				Out.Add(&O);
			}
		}
		return Out;
	}

	/** A mines layer with only the doorway sub-rule (the crates / field sub-rules switched off with null). */
	void DoorwaysOnly(FLevelPropsFile& File, const FString& Mode, int32 Value, const FString& Mandatory)
	{
		FLevelPropsLayer* Mines = C4FindLayer(File, TEXT("mines"));
		TSharedRef<FJsonObject> Rules = MakeShared<FJsonObject>();
		TSharedRef<FJsonObject> Doors = MakeShared<FJsonObject>();
		Doors->SetStringField(TEXT("mode"), Mode);
		Doors->SetNumberField(TEXT("value"), Value);
		Doors->SetStringField(TEXT("mandatory_route"), Mandatory);
		Rules->SetObjectField(TEXT("doorways"), Doors);
		Rules->SetField(TEXT("crates"), MakeShared<FJsonValueNull>());
		Rules->SetField(TEXT("field"), MakeShared<FJsonValueNull>());
		Mines->Rules = Rules;
		Mines->Objects.Reset();
	}

	FString C4StripCr(const FString& S)
	{
		return S.Replace(TEXT("\r"), TEXT(""));
	}

	FString C4FirstDiff(const FString& A, const FString& B)
	{
		TArray<FString> LA;
		TArray<FString> LB;
		A.ParseIntoArray(LA, TEXT("\n"), false);
		B.ParseIntoArray(LB, TEXT("\n"), false);
		for (int32 I = 0; I < FMath::Max(LA.Num(), LB.Num()); ++I)
		{
			const FString X = LA.IsValidIndex(I) ? LA[I] : TEXT("<eof>");
			const FString Y = LB.IsValidIndex(I) ? LB[I] : TEXT("<eof>");
			if (X != Y)
			{
				return FString::Printf(TEXT("line %d: expected '%s' got '%s'"), I + 1, *X, *Y);
			}
		}
		return TEXT("(equal)");
	}

	TArray<int32> C4IntArray(const FJsonObject& J, const TCHAR* Key)
	{
		TArray<int32> Out;
		const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
		if (J.TryGetArrayField(Key, Items))
		{
			for (const TSharedPtr<FJsonValue>& V : *Items)
			{
				Out.Add(static_cast<int32>(V->AsNumber()));
			}
		}
		return Out;
	}
}

// ---- rules ---------------------------------------------------------------------------------------------------------------------------

PROPS_C4_TEST(FPropsC4BuildRulesTest, "BuildRules")
bool FPropsC4BuildRulesTest::RunTest(const FString&)
{
	using namespace PropsC4TestLocal;
	FC4World W;
	if (!LoadDepot(*this, W))
	{
		return false;
	}
	TArray<FString> Errors;
	FPropsRules T;
	TestTrue(TEXT("turrets rules"), PropsPlacementRules::BuildRules(*W.Balance, *C4FindLayer(W.File, TEXT("turrets")), T, Errors));
	TestEqual(TEXT("turrets: 2"), T.CountValue, 2);
	TestEqual(TEXT("range"), FIntPoint(T.Turret.RangeMinCm, T.Turret.RangeMaxCm), FIntPoint(1000, 4000));
	TestEqual(TEXT("arc"), T.Turret.ArcDeg, 90);
	TestEqual(TEXT("min covered"), T.Turret.MinCoveredM, 25);
	TestEqual(TEXT("anchors"), T.Anchors, TArray<FString>{ TEXT("ambush_points"), TEXT("roof_edges"), TEXT("behind_tall_cover") });
	TestEqual(TEXT("turret spawn exclusion 40 m over the standard preset"), T.Exclude.SpawnCm, 4000);
	TestEqual(TEXT("turret ladder exclusion from standard"), T.Exclude.LadderCm, 300);
	TestEqual(TEXT("turret half"), T.HalfSizeCm.FindRef(TEXT("turret_mg")), 50);

	FPropsRules M;
	TestTrue(TEXT("mines rules"), PropsPlacementRules::BuildRules(*W.Balance, *C4FindLayer(W.File, TEXT("mines")), M, Errors));
	TestTrue(TEXT("doorways on"), M.MineDoors.bEnabled);
	TestEqual(TEXT("doorways percent"), M.MineDoors.Mode, FString(TEXT("percent")));
	TestEqual(TEXT("doorways 25"), M.MineDoors.Value, 25);
	TestFalse(TEXT("designer 6.1 #3: mandatory doors forbidden by default"), M.MineDoors.bAllowMandatory);
	TestEqual(TEXT("inside 80"), M.MineDoors.InsideCm, 80);
	TestTrue(TEXT("crates on"), M.MineCrates.bEnabled);
	TestEqual(TEXT("crates 10 %"), M.MineCrates.Percent, 10);
	TestTrue(TEXT("field on"), M.MineField.bEnabled);
	TestEqual(TEXT("field in the cut"), M.MineField.Common.WalkLayers, TArray<FString>{ TEXT("cut") });
	TestEqual(TEXT("field: no variants corridor"), M.MineField.Common.Exclude.RouteVariantsCm, 0);
	TestEqual(TEXT("field: mandatory corridor"), M.MineField.Common.Exclude.RouteMandatoryCm, 260);
	TestEqual(TEXT("field: ladder from $preset standard"), M.MineField.Common.Exclude.LadderCm, 300);
	TestEqual(TEXT("mines budget"), M.BudgetCap, 40);

	FPropsRules L;
	TestTrue(TEXT("loot rules"), PropsPlacementRules::BuildRules(*W.Balance, *C4FindLayer(W.File, TEXT("loot")), L, Errors));
	TestEqual(TEXT("budget 600"), L.Loot.BudgetValue, 600);
	TestEqual(TEXT("3 tables"), L.Loot.Tables.Num(), 3);
	TestEqual(TEXT("table values"), L.Loot.TableValues, TArray<int32>{ 40, 60, 150 });
	TestEqual(TEXT("roof table"), L.Loot.RoofTable, FString(TEXT("rare_roof")));
	TestEqual(TEXT("crates share 0.3 -> 300"), L.Loot.ContainersShareMilli, 300);
	TestEqual(TEXT("containers spend at most half the budget"), L.Loot.ContainersBudgetMilli, 500);
	TestEqual(TEXT("loose anchors from loose.where"), L.Anchors.Num(), 4);
	TestTrue(TEXT("no errors"), Errors.IsEmpty());

	// a bad doorway rule is reported, the default kept
	FLevelPropsLayer Bad = *C4FindLayer(W.File, TEXT("mines"));
	TSharedRef<FJsonObject> Rules = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> Doors = MakeShared<FJsonObject>();
	Doors->SetStringField(TEXT("mandatory_route"), TEXT("sometimes"));
	Rules->SetObjectField(TEXT("doorways"), Doors);
	Bad.Rules = Rules;
	FPropsRules BadRules;
	TArray<FString> BadErrors;
	TestFalse(TEXT("bad mandatory_route refused"), PropsPlacementRules::BuildRules(*W.Balance, Bad, BadRules, BadErrors));
	TestFalse(TEXT("forbid kept"), BadRules.MineDoors.bAllowMandatory);
	return true;
}

// ---- sight ---------------------------------------------------------------------------------------------------------------------------

PROPS_C4_TEST(FPropsC4SightTest, "SightRules")
bool FPropsC4SightTest::RunTest(const FString&)
{
	// a 2.5 m wall, a 1.5 m crate cover, a 1.1 m low cover and a 2-storey roof: the evaluator's ground and elevated rules
	const FString Json = TEXT("{\"schema\":\"codex.levelgen_layout/1\",\"name\":\"sight\",\"bounds\":{\"min\":[-3000,-3000],\"max\":[3000,3000]},")
		TEXT("\"spawn\":{\"pos\":[-2900,-2900]},\"end\":[2900,2900],")
		TEXT("\"walls\":[{\"id\":\"W_a\",\"a\":[0,-500],\"b\":[0,500],\"height\":250,\"thickness\":20,\"kind\":\"wall\"}],")
		TEXT("\"buildings\":[{\"id\":\"B_t\",\"footprint\":[[-2000,-500],[-1000,500]],\"floors\":2,\"enterable\":false,\"roof_walkable\":true,\"doors\":[]}],")
		TEXT("\"covers\":[{\"id\":\"C_tall\",\"pos\":[0,1500],\"yaw\":45,\"size\":[200,120,160]},{\"id\":\"C_low\",\"pos\":[0,-1500],\"yaw\":0,\"size\":[120,120,110]}],")
		TEXT("\"ladders\":[],\"zones\":[],\"routes\":[]}");
	FPropsLayoutView View;
	TArray<FString> Errors;
	if (!TestTrue(TEXT("sight layout builds"), FPropsLayoutView::Build(Json, View, Errors)))
	{
		return false;
	}
	TArray<FPropsSightSegment> Ground;
	View.GetSightBlockers(TEXT("ground"), Ground);
	// ground: the wall + the tall cover's 4 edges + the building's 4 synthetic walls? no: layerWalls(ground) = layout walls only
	TestEqual(TEXT("ground blockers: 1 wall + 4 tall cover edges"), Ground.Num(), 5);
	TestTrue(TEXT("the wall blocks a ground line across it"), FPropsLayoutView::IsSightBlocked(FIntPoint(-500, 0), FIntPoint(500, 0), Ground));
	TestFalse(TEXT("a ground line past the wall's end is free"), FPropsLayoutView::IsSightBlocked(FIntPoint(-500, 700), FIntPoint(500, 700), Ground));
	TestTrue(TEXT("touching the wall's end counts as blocked"), FPropsLayoutView::IsSightBlocked(FIntPoint(-500, 500), FIntPoint(500, 500), Ground));
	TestTrue(TEXT("the tall (160 cm) cover blocks"), FPropsLayoutView::IsSightBlocked(FIntPoint(-500, 1500), FIntPoint(500, 1500), Ground));
	TestFalse(TEXT("the low (110 cm) cover does not"), FPropsLayoutView::IsSightBlocked(FIntPoint(-500, -1500), FIntPoint(500, -1500), Ground));
	TArray<FPropsSightSegment> Roof;
	View.GetSightBlockers(TEXT("roof:B_t"), Roof);
	TestEqual(TEXT("roof +2 (eye 760 cm): nothing reaches it"), Roof.Num(), 0);
	TestFalse(TEXT("from the roof the line passes over the wall"), FPropsLayoutView::IsSightBlocked(FIntPoint(-1500, 0), FIntPoint(500, 0), Roof));
	// integer helpers
	TestEqual(TEXT("isqrt(99)"), PropsGeometry::ISqrt(99), static_cast<int64>(9));
	TestEqual(TEXT("isqrt(100)"), PropsGeometry::ISqrt(100), static_cast<int64>(10));
	TestEqual(TEXT("isqrt(2^62)"), PropsGeometry::ISqrt(static_cast<int64>(1) << 62), static_cast<int64>(1) << 31);
	TestEqual(TEXT("floor(-7 / 2)"), PropsGeometry::FloorDiv(-7, 2), static_cast<int64>(-4));
	TestEqual(TEXT("floor(7 / -2)"), PropsGeometry::FloorDiv(7, -2), static_cast<int64>(-4));
	TestEqual(TEXT("floor(-8 / 2)"), PropsGeometry::FloorDiv(-8, 2), static_cast<int64>(-4));
	TestEqual(TEXT("sin 30"), PropsGeometry::SinQ(30), static_cast<int64>(8192));
	TestEqual(TEXT("cos 180"), PropsGeometry::CosQ(180), static_cast<int64>(-16384));
	return true;
}

// ---- turrets -------------------------------------------------------------------------------------------------------------------------

PROPS_C4_TEST(FPropsC4TurretCoverageTest, "TurretCoverage")
bool FPropsC4TurretCoverageTest::RunTest(const FString&)
{
	using namespace PropsC4TestLocal;
	FC4World Depot;
	FC4World Small;
	if (!LoadDepot(*this, Depot) || !LoadSmall(*this, Small))
	{
		return false;
	}
	int32 Placed = 0;
	for (FC4World* W : { &Small, &Depot })
	{
		FPropsPlacementInput In;
		if (!C4MakeInput(*this, *W, TEXT("turrets"), In))
		{
			continue;
		}
		const int32 MinM = In.Rules.Turret.MinCoveredM;
		for (uint32 Seed = 1; Seed <= 10; ++Seed)
		{
			In.Seed = Seed;
			const FPropsPlacementResult R = PropsPlacementRules::Place(In);
			const FString Ctx = FString::Printf(TEXT("%s turrets seed %u"), W == &Small ? TEXT("small") : TEXT("depot"), Seed);
			C4CheckInvariants(*this, Ctx, In, R);
			TestTrue(FString::Printf(TEXT("%s: turrets placed"), *Ctx), R.Report.Placed > 0);
			for (const FLevelPropsObject& O : R.Objects)
			{
				int32 Yaw = 0;
				const int32 Score = PropsPlacementRules::TurretCoverage(W->View, In.Rules, O.Pos, O.WalkLayer, Yaw);
				TestTrue(FString::Printf(TEXT("%s: %s covers %d m >= min_covered_m %d"), *Ctx, *O.Id, Score, MinM), Score >= MinM && Score > 0);
				TestEqual(FString::Printf(TEXT("%s: %s yaw = the best arc"), *Ctx, *O.Id), O.Yaw, Yaw);
				TestEqual(FString::Printf(TEXT("%s: %s yaw on 15 deg"), *Ctx, *O.Id), O.Yaw % 15, 0);
				TestTrue(FString::Printf(TEXT("%s: %s arc stored"), *Ctx, *O.Id), O.Arc.IsSet() && O.Arc.GetValue() == In.Rules.Turret.ArcDeg);
				for (const FLevelPropsObject& P : R.Objects)
				{
					TestTrue(FString::Printf(TEXT("%s: turrets >= min_spacing"), *Ctx), &P == &O || C4DistSq(O.Pos, P.Pos) >= static_cast<int64>(In.Rules.MinSpacingCm) * In.Rules.MinSpacingCm);
				}
				++Placed;
			}
			if (W == &Depot)
			{
				TestTrue(FString::Printf(TEXT("%s: low coverage rejected"), *Ctx), R.Report.Rejected.Coverage > 0);
			}
		}
	}
	TestTrue(TEXT("turrets placed overall"), Placed > 0);

	// a higher bar removes candidates; an impossible bar places nothing and reports short
	FPropsPlacementInput In;
	if (C4MakeInput(*this, Depot, TEXT("turrets"), In))
	{
		In.Rules.Turret.MinCoveredM = 100000;
		const FPropsPlacementResult R = PropsPlacementRules::Place(In);
		TestEqual(TEXT("impossible coverage: nothing placed"), R.Report.Placed, 0);
		TestTrue(TEXT("impossible coverage: short"), R.Report.IsShort());
		TestTrue(TEXT("impossible coverage: no_candidates"), R.Report.Warnings.Contains(TEXT("no_candidates")));
		// a narrow arc covers less than a full circle from the same point
		FPropsPlacementInput Full = In;
		Full.Rules.Turret.MinCoveredM = 0;
		Full.Rules.Turret.ArcDeg = 360;
		FPropsPlacementInput Narrow = Full;
		Narrow.Rules.Turret.ArcDeg = 30;
		const FPropsPlacementResult FR = PropsPlacementRules::Place(Full);
		if (TestTrue(TEXT("full-circle turret placed"), FR.Objects.Num() > 0))
		{
			const FLevelPropsObject& O = FR.Objects.Last();
			int32 YawFull = 0;
			int32 YawNarrow = 0;
			const int32 SFull = PropsPlacementRules::TurretCoverage(Depot.View, Full.Rules, O.Pos, O.WalkLayer, YawFull);
			const int32 SNarrow = PropsPlacementRules::TurretCoverage(Depot.View, Narrow.Rules, O.Pos, O.WalkLayer, YawNarrow);
			TestTrue(FString::Printf(TEXT("narrow arc %d <= full circle %d"), SNarrow, SFull), SNarrow <= SFull && SNarrow > 0);
			TestEqual(TEXT("a full circle has yaw 0"), YawFull, 0);
		}
		// no route -> nothing, reported
		FPropsPlacementInput NoRoute = In;
		NoRoute.Rules.Turret.MinCoveredM = 0;
		NoRoute.Rules.Turret.RouteId = TEXT("R_nope");
		TestTrue(TEXT("no_cover_route"), PropsPlacementRules::Place(NoRoute).Report.Warnings.Contains(TEXT("no_cover_route")));
	}
	return true;
}

// ---- mines ---------------------------------------------------------------------------------------------------------------------------

PROPS_C4_TEST(FPropsC4DoorwayPercentTest, "DoorwayPercentCeil")
bool FPropsC4DoorwayPercentTest::RunTest(const FString&)
{
	using namespace PropsC4TestLocal;
	FC4World W;
	if (!LoadDepot(*this, W))
	{
		return false;
	}
	// the eligible doors computed independently: not on the mandatory route, mine position outside the mines' spawn / end disks
	FPropsPlacementInput Probe;
	DoorwaysOnly(W.File, TEXT("percent"), 25, TEXT("forbid"));
	if (!C4MakeInput(*this, W, TEXT("mines"), Probe))
	{
		return false;
	}
	int32 Eligible = 0;
	int32 Mandatory = 0;
	for (const FPropsDoorway& D : W.View.GetDoorways())
	{
		if (D.bOnMandatoryRoute)
		{
			++Mandatory;
			continue;
		}
		const FIntPoint P = D.Pos - D.Outward * Probe.Rules.MineDoors.InsideCm;
		const int64 Spawn = Probe.Rules.Exclude.SpawnCm;
		const int64 End = Probe.Rules.Exclude.EndCm;
		Eligible += (C4DistSq(P, W.View.GetSpawnPos()) > Spawn * Spawn && C4DistSq(P, W.View.GetEndPos()) > End * End) ? 1 : 0;
	}
	TestEqual(TEXT("Depot: 3 doorways on the mandatory route (6.1 #7)"), Mandatory, 3);
	TestEqual(TEXT("Depot: 23 doorways (14 doors + 9 gates)"), W.View.GetDoorways().Num(), 23);
	for (const int32 Percent : { 1, 10, 25, 30, 33, 50, 99, 100 })
	{
		DoorwaysOnly(W.File, TEXT("percent"), Percent, TEXT("forbid"));
		FPropsPlacementInput In;
		if (!C4MakeInput(*this, W, TEXT("mines"), In))
		{
			continue;
		}
		const FPropsPlacementResult R = PropsPlacementRules::Place(In);
		const int32 Ceil = (Percent * Eligible + 99) / 100;
		TestEqual(FString::Printf(TEXT("%d %%: eligible"), Percent), R.Report.SubA.Eligible, Eligible);
		TestEqual(FString::Printf(TEXT("%d %%: requested = ceil(%d x %d / 100) = %d"), Percent, Percent, Eligible, Ceil), R.Report.SubA.Requested, Ceil);
		TestEqual(FString::Printf(TEXT("%d %%: placed"), Percent), WithDoor(R).Num(), Ceil);
		TestEqual(FString::Printf(TEXT("%d %%: forbidden = the mandatory doors"), Percent), R.Report.SubA.Forbidden, Mandatory);
		TestFalse(FString::Printf(TEXT("%d %%: not short"), Percent), R.Report.IsShort());
		C4CheckInvariants(*this, FString::Printf(TEXT("doorways %d %%"), Percent), In, R);
	}
	// count mode: exactly N; more than the free doors -> short, every free door mined once
	for (const int32 N : { 0, 1, 5, 40 })
	{
		DoorwaysOnly(W.File, TEXT("count"), N, TEXT("forbid"));
		FPropsPlacementInput In;
		if (!C4MakeInput(*this, W, TEXT("mines"), In))
		{
			continue;
		}
		const FPropsPlacementResult R = PropsPlacementRules::Place(In);
		const int32 Expected = FMath::Min(N, Eligible);
		TestEqual(FString::Printf(TEXT("count %d: placed"), N), WithDoor(R).Num(), Expected);
		TestEqual(FString::Printf(TEXT("count %d: short_by"), N), R.Report.ShortBy, FMath::Min(N, In.Rules.BudgetCap) - Expected);
		C4CheckInvariants(*this, FString::Printf(TEXT("doorways count %d"), N), In, R);
	}
	// the small fixture: 2 shed doors eligible, the yard gate is on the route: 60 % -> ceil(1.2) = 2
	FC4World Small;
	if (LoadSmall(*this, Small))
	{
		FPropsPlacementInput In;
		if (C4MakeInput(*this, Small, TEXT("mines"), In))
		{
			const FPropsPlacementResult R = PropsPlacementRules::Place(In);
			TestEqual(TEXT("small: 2 eligible doors"), R.Report.SubA.Eligible, 2);
			TestEqual(TEXT("small: ceil(60 % x 2) = 2"), R.Report.SubA.Requested, 2);
			TestEqual(TEXT("small: the gate forbidden"), R.Report.SubA.Forbidden, 1);
			C4CheckInvariants(*this, TEXT("small mines"), In, R);
		}
	}
	return true;
}

PROPS_C4_TEST(FPropsC4ForbidMandatoryTest, "ForbidMandatoryDoors")
bool FPropsC4ForbidMandatoryTest::RunTest(const FString&)
{
	using namespace PropsC4TestLocal;
	FC4World W;
	if (!LoadDepot(*this, W))
	{
		return false;
	}
	TSet<FString> MandatoryIds;
	for (const FPropsDoorway& D : W.View.GetDoorways())
	{
		if (D.bOnMandatoryRoute)
		{
			MandatoryIds.Add(D.Id);
		}
	}
	TestTrue(TEXT("the compound gate is on the route"), MandatoryIds.Contains(TEXT("F_compound_N#g0")));
	TestTrue(TEXT("the shed door is on the route"), MandatoryIds.Contains(TEXT("B_shed#0")));
	TestFalse(TEXT("B_pump#0 is not"), MandatoryIds.Contains(TEXT("B_pump#0")));

	// forbid (default): never a mandatory door, on 50 seeds and at 100 %
	DoorwaysOnly(W.File, TEXT("percent"), 100, TEXT("forbid"));
	FPropsPlacementInput In;
	if (!C4MakeInput(*this, W, TEXT("mines"), In))
	{
		return false;
	}
	for (uint32 Seed = 1; Seed <= 50; ++Seed)
	{
		In.Seed = Seed;
		In.Rules.MineDoors.Value = 20 + static_cast<int32>(Seed) % 81;
		const FPropsPlacementResult R = PropsPlacementRules::Place(In);
		for (const FLevelPropsObject* O : WithDoor(R))
		{
			TestFalse(FString::Printf(TEXT("seed %u: %s not on the mandatory door %s"), Seed, *O->Id, *O->Door), MandatoryIds.Contains(O->Door));
		}
		C4CheckInvariants(*this, FString::Printf(TEXT("forbid seed %u"), Seed), In, R);
	}

	// allow (opt-in per layer, 6.1 #3): at 100 % every mandatory door is mined
	DoorwaysOnly(W.File, TEXT("percent"), 100, TEXT("allow"));
	FPropsPlacementInput Allow;
	if (C4MakeInput(*this, W, TEXT("mines"), Allow))
	{
		TestTrue(TEXT("allow parsed"), Allow.Rules.MineDoors.bAllowMandatory);
		const FPropsPlacementResult R = PropsPlacementRules::Place(Allow);
		TSet<FString> Mined;
		for (const FLevelPropsObject* O : WithDoor(R))
		{
			Mined.Add(O->Door);
		}
		for (const FString& Id : MandatoryIds)
		{
			TestTrue(FString::Printf(TEXT("allow: %s mined"), *Id), Mined.Contains(Id));
		}
		TestEqual(TEXT("allow: nothing forbidden"), R.Report.SubA.Forbidden, 0);
		C4CheckInvariants(*this, TEXT("allow"), Allow, R);

		// the validator catches a mandatory-door mine under forbid
		FPropsPlacementInput Forbid = Allow;
		Forbid.Rules.MineDoors.bAllowMandatory = false;
		const TArray<FPropsViolation> V = PropsPlacementRules::Validate(Forbid, R.Objects);
		TestEqual(TEXT("validator: one violation per mandatory door"), V.Num(), MandatoryIds.Num());
	}
	// position and yaw: 80 cm inside, across the opening; a gate keeps its centre
	FPropsPlacementInput Any = Allow;
	const FPropsPlacementResult R = PropsPlacementRules::Place(Any);
	for (const FLevelPropsObject* O : WithDoor(R))
	{
		const FPropsDoorway* D = W.View.FindDoorway(O->Door);
		if (!TestNotNull(TEXT("doorway exists"), D))
		{
			continue;
		}
		const FIntPoint Expected = D->Kind == EPropsDoorwayKind::Gate ? D->Pos : D->Pos - D->Outward * 80;
		TestEqual(FString::Printf(TEXT("%s position"), *O->Door), O->Pos, Expected);
		TestEqual(FString::Printf(TEXT("%s yaw across the opening"), *O->Door), O->Yaw, D->Along.X != 0 ? 90 : 0);
		TestEqual(FString::Printf(TEXT("%s trigger"), *O->Door), O->Trigger, FString(TEXT("tripwire")));
		TestTrue(FString::Printf(TEXT("%s width"), *O->Door), O->Extra.Contains(PropsPlacementRules::WidthField));
	}
	return true;
}

PROPS_C4_TEST(FPropsC4CrateMinesTest, "CrateMinesFollowUnlockedCrates")
bool FPropsC4CrateMinesTest::RunTest(const FString&)
{
	using namespace PropsC4TestLocal;
	FC4World W;
	if (!LoadDepot(*this, W))
	{
		return false;
	}
	const auto Unlocked = [](const FC4World& World)
	{
		int32 N = 0;
		for (const FLevelPropsResolvedObject& R : LevelPropsFile::ApplyOverrides(World.File).Objects)
		{
			N += R.LayerId == TEXT("crates") && !R.Object.bLocked ? 1 : 0;
		}
		return N;
	};
	const auto CrateMines = [](const FC4World& World)
	{
		TArray<FLevelPropsObject> Out;
		for (const FLevelPropsObject& O : C4FindLayer(const_cast<FLevelPropsFile&>(World.File), TEXT("mines"))->Objects)
		{
			if (!O.Attach.IsEmpty())
			{
				Out.Add(O);
			}
		}
		return Out;
	};
	const int32 UnlockedBefore = Unlocked(W);
	TestTrue(TEXT("Depot has unlocked crates"), UnlockedBefore > 10);
	FPropsPlacementResult R1;
	RerollInPlace(*this, W, TEXT("mines"), 1, &R1);
	const int32 Percent = 10;
	TestEqual(TEXT("eligible = the unlocked crates"), R1.Report.SubB.Eligible, UnlockedBefore);
	TestEqual(TEXT("crate mines = ceil(10 % x unlocked)"), CrateMines(W).Num(), (Percent * UnlockedBefore + 99) / 100);
	TSet<FString> LockedCrates;
	for (const FLevelPropsObject& O : C4FindLayer(W.File, TEXT("crates"))->Objects)
	{
		if (O.bLocked)
		{
			LockedCrates.Add(O.Id);
		}
	}
	for (const FLevelPropsObject& M : CrateMines(W))
	{
		TestFalse(FString::Printf(TEXT("%s not on a locked crate"), *M.Id), LockedCrates.Contains(M.Attach));
	}
	// a mined crate is red, the others blue (derived, 6.1 #1)
	const TMap<FString, FString> Colors = PropsPlacementRules::ResolveCrateColors(W.File, *W.Balance);
	int32 Red = 0;
	for (const TPair<FString, FString>& C : Colors)
	{
		Red += C.Value == TEXT("red") ? 1 : 0;
	}
	TestEqual(TEXT("one red crate per crate mine"), Red, CrateMines(W).Num());
	for (const FLevelPropsObject& M : CrateMines(W))
	{
		TestEqual(FString::Printf(TEXT("%s red"), *M.Attach), Colors.FindRef(M.Attach), FString(TEXT("red")));
	}

	// lock every crate mine and one locked crate gets a locked mine too (a hand edit); then re-roll the crates away
	const TArray<FLevelPropsObject> Mined = CrateMines(W);
	for (FLevelPropsObject& O : C4FindLayer(W.File, TEXT("mines"))->Objects)
	{
		O.bLocked = !O.Attach.IsEmpty();
	}
	FLevelPropsObject OnLocked;
	OnLocked.Id = TEXT("mines_hand_locked_crate");
	OnLocked.Variant = TEXT("mine_crate");
	OnLocked.Attach = *LockedCrates.CreateConstIterator();
	OnLocked.Trigger = TEXT("crate_open");
	OnLocked.bLocked = true;
	C4FindLayer(W.File, TEXT("mines"))->Objects.Add(OnLocked);

	FPropsPlacementResult RC;
	RerollInPlace(*this, W, TEXT("crates"), 7, &RC);
	// the crates that survive a re-roll: locked, moved (pinned) and hand ones; every other crate is re-rolled away
	TSet<FString> Surviving;
	for (const FLevelPropsResolvedObject& R : LevelPropsFile::ApplyOverrides(W.File).Objects)
	{
		if (R.LayerId == TEXT("crates"))
		{
			Surviving.Add(R.Object.Id);
		}
	}
	int32 Gone = 0;
	for (const FLevelPropsObject& M : Mined)
	{
		Gone += Surviving.Contains(M.Attach) ? 0 : 1;
	}
	TestTrue(TEXT("some mined crates were re-rolled away"), Gone > 0);
	int32 Orphaned = 0;
	for (const FString& Warning : RC.Report.Warnings)
	{
		Orphaned += Warning.StartsWith(TEXT("attach_orphaned ")) ? 1 : 0;
	}
	TestEqual(TEXT("the crates re-roll reports every crate mine whose crate it removed"), Orphaned, Gone);
	TestEqual(TEXT("the crates re-roll leaves the mines layer alone"), CrateMines(W).Num(), Mined.Num() + 1);

	FPropsPlacementResult R2;
	RerollInPlace(*this, W, TEXT("mines"), 1, &R2);
	TestEqual(TEXT("locked mines on re-rolled crates dropped"), R2.Report.SubB.Dropped, Gone);
	int32 DroppedWarnings = 0;
	for (const FString& Warning : R2.Report.Warnings)
	{
		DroppedWarnings += Warning.StartsWith(TEXT("attach_dropped ")) ? 1 : 0;
	}
	TestEqual(TEXT("each drop reported"), DroppedWarnings, Gone);
	const int32 KeptCrateMines = Mined.Num() - Gone + 1;
	TestEqual(TEXT("kept crate mines counted"), R2.Report.SubB.Kept, KeptCrateMines);
	TestTrue(TEXT("the locked mine on the locked crate is kept"), C4FindLayer(W.File, TEXT("mines"))->Objects.ContainsByPredicate([](const FLevelPropsObject& O)
	{
		return O.Id == TEXT("mines_hand_locked_crate");
	}));
	const int32 UnlockedAfter = Unlocked(W);
	TestEqual(TEXT("eligible follows the new unlocked crates"), R2.Report.SubB.Eligible, UnlockedAfter);
	TestEqual(TEXT("the count follows the new unlocked crates (kept ones included)"), CrateMines(W).Num(), FMath::Max((Percent * UnlockedAfter + 99) / 100, KeptCrateMines));
	FPropsPlacementInput In;
	if (C4MakeInput(*this, W, TEXT("mines"), In))
	{
		C4CheckInvariants(*this, TEXT("mines after the crates re-roll"), In, R2);
	}
	return true;
}

// ---- loot ----------------------------------------------------------------------------------------------------------------------------

PROPS_C4_TEST(FPropsC4LootBudgetTest, "LootBudget")
bool FPropsC4LootBudgetTest::RunTest(const FString&)
{
	using namespace PropsC4TestLocal;
	FC4World W;
	if (!LoadDepot(*this, W))
	{
		return false;
	}
	RerollInPlace(*this, W, TEXT("mines"), 1); // red crates exist
	FPropsPlacementInput In;
	if (!C4MakeInput(*this, W, TEXT("loot"), In))
	{
		return false;
	}
	const TMap<FString, FString> Colors = PropsPlacementRules::ResolveCrateColors(W.File, *W.Balance);
	const auto Value = [&In](const FString& Table)
	{
		const int32 I = In.Rules.Loot.Tables.IndexOfByKey(Table);
		return I == INDEX_NONE ? 0 : In.Rules.Loot.TableValues[I];
	};
	int32 Roof = 0;
	int32 Containers = 0;
	for (uint32 Seed = 1; Seed <= 20; ++Seed)
	{
		In.Seed = Seed;
		const FPropsPlacementResult R = PropsPlacementRules::Place(In);
		const FString Ctx = FString::Printf(TEXT("loot seed %u"), Seed);
		C4CheckInvariants(*this, Ctx, In, R);
		int64 Total = 0;
		for (const FLevelPropsObject& O : R.Objects)
		{
			Total += Value(O.LootTable);
			TestTrue(FString::Printf(TEXT("%s: %s has a known table"), *Ctx, *O.Id), Value(O.LootTable) > 0);
			const bool bRoof = O.WalkLayer.StartsWith(TEXT("roof:"));
			if (O.Attach.IsEmpty())
			{
				TestEqual(FString::Printf(TEXT("%s: %s roof table iff on a roof"), *Ctx, *O.Id), O.LootTable == In.Rules.Loot.RoofTable, bRoof);
				Roof += bRoof ? 1 : 0;
			}
			else
			{
				TestEqual(FString::Printf(TEXT("%s: %s only in a blue crate"), *Ctx, *O.Id), Colors.FindRef(O.Attach), FString(TEXT("blue")));
				TestNotEqual(FString::Printf(TEXT("%s: %s crates never get the roof table"), *Ctx, *O.Id), O.LootTable, In.Rules.Loot.RoofTable);
				++Containers;
			}
		}
		TestTrue(FString::Printf(TEXT("%s: value %lld <= budget %d"), *Ctx, Total, In.Rules.Loot.BudgetValue), Total <= In.Rules.Loot.BudgetValue);
		TestEqual(FString::Printf(TEXT("%s: report spent"), *Ctx), static_cast<int64>(R.Report.BudgetSpent), Total);
		// spent until no table fits, or reported short
		const int64 Left = In.Rules.Loot.BudgetValue - Total;
		TestTrue(FString::Printf(TEXT("%s: budget spent or short reported (left %lld)"), *Ctx, Left), Left < 40 || R.Report.bBudgetShort);
		TestTrue(FString::Printf(TEXT("%s: containers = round(share x eligible) at most"), *Ctx), R.Report.SubA.Placed <= R.Report.SubA.Requested);
	}
	TestTrue(TEXT("roof piles occur (rare_roof)"), Roof > 0);
	TestTrue(TEXT("containers occur"), Containers > 0);

	// a tiny budget: only what fits; nothing fits -> nothing, no short
	FPropsPlacementInput Tiny = In;
	Tiny.Rules.Loot.BudgetValue = 50;
	const FPropsPlacementResult T = PropsPlacementRules::Place(Tiny);
	TestEqual(TEXT("budget 50: one 40-value roll"), T.Objects.Num(), 1);
	FPropsPlacementInput None = In;
	None.Rules.Loot.BudgetValue = 30;
	const FPropsPlacementResult N = PropsPlacementRules::Place(None);
	TestEqual(TEXT("budget 30: nothing affordable"), N.Objects.Num(), 0);
	TestFalse(TEXT("budget 30: not short"), N.Report.IsShort());
	return true;
}

// ---- determinism ---------------------------------------------------------------------------------------------------------------------

PROPS_C4_TEST(FPropsC4SameSeedTest, "SameSeedIdentical")
bool FPropsC4SameSeedTest::RunTest(const FString&)
{
	using namespace PropsC4TestLocal;
	FC4World Small;
	FC4World Depot;
	if (!LoadSmall(*this, Small) || !LoadDepot(*this, Depot))
	{
		return false;
	}
	const auto Positions = [](const FPropsPlacementResult& R)
	{
		TArray<FIntPoint> P;
		for (const FLevelPropsObject& O : R.Objects)
		{
			P.Add(O.Pos);
		}
		return P;
	};
	for (FC4World* W : { &Small, &Depot })
	{
		const TCHAR* Name = W == &Small ? TEXT("small") : TEXT("depot");
		for (const TCHAR* Layer : { TEXT("turrets"), TEXT("mines"), TEXT("loot") })
		{
			const FString A = RerollCopy(*this, *W, Layer, 9);
			const FString B = RerollCopy(*this, *W, Layer, 9);
			TestTrue(FString::Printf(TEXT("%s %s: same seed -> byte-identical file"), Name, Layer), !A.IsEmpty() && A == B);
		}
		// a stored roll re-rolled with its own seed reproduces itself
		FC4World Once;
		Once.Balance = W->Balance;
		Once.View = W->View;
		Once.File = W->File;
		RerollInPlace(*this, Once, TEXT("mines"), 4);
		FC4World Twice;
		Twice.Balance = Once.Balance;
		Twice.View = Once.View;
		Twice.File = Once.File;
		RerollInPlace(*this, Twice, TEXT("mines"), 4);
		// the objects repeat; the report may not (the first roll reported the locked mine whose crate is gone, the second has none)
		C4FindLayer(Once.File, TEXT("mines"))->Report.Reset();
		C4FindLayer(Twice.File, TEXT("mines"))->Report.Reset();
		TestEqual(FString::Printf(TEXT("%s mines: re-roll of a stored roll with its seed is idempotent"), Name), LevelPropsFile::Write(Twice.File), LevelPropsFile::Write(Once.File));
	}
	for (const TCHAR* Layer : { TEXT("turrets"), TEXT("mines"), TEXT("loot") })
	{
		FPropsPlacementResult A;
		FPropsPlacementResult B;
		RerollCopy(*this, Depot, Layer, 1, &A);
		RerollCopy(*this, Depot, Layer, 2, &B);
		TestFalse(FString::Printf(TEXT("depot %s: seeds 1 / 2 place differently"), Layer), Positions(A) == Positions(B));
	}
	// "Generate all" in the fixed order places every layer and the stored file validates
	FC4World All;
	All.Balance = Depot.Balance;
	All.View = Depot.View;
	All.File = Depot.File;
	TArray<FString> Errors;
	TestTrue(TEXT("generate all"), PropsPlacementRules::RerollAll(All.File, *All.Balance, All.View, Errors));
	for (const TCHAR* Layer : { TEXT("crates"), TEXT("barrels"), TEXT("turrets"), TEXT("mines"), TEXT("loot") })
	{
		TestTrue(FString::Printf(TEXT("generate all: %s has objects"), Layer), C4FindLayer(All.File, Layer)->Objects.Num() > 0);
		TestTrue(FString::Printf(TEXT("generate all: %s report"), Layer), C4FindLayer(All.File, Layer)->Report.IsValid());
	}
	FC4World AllAgain;
	AllAgain.Balance = Depot.Balance;
	AllAgain.View = Depot.View;
	AllAgain.File = Depot.File;
	TArray<FString> Errors2;
	PropsPlacementRules::RerollAll(AllAgain.File, *AllAgain.Balance, AllAgain.View, Errors2);
	TestEqual(TEXT("generate all from the same file twice = identical"), LevelPropsFile::Write(AllAgain.File), LevelPropsFile::Write(All.File));
	return true;
}

// ---- golden outputs (the TS port's parity contract) ----------------------------------------------------------------------------------

PROPS_C4_TEST(FPropsC4GoldenTest, "Golden")
bool FPropsC4GoldenTest::RunTest(const FString&)
{
	using namespace PropsC4TestLocal;
	const bool bWrite = FPlatformMisc::GetEnvironmentVariable(TEXT("CODEX_PROPS_GOLDEN_WRITE")) == TEXT("1");
	FString ManifestJson;
	TSharedPtr<FJsonObject> Manifest;
	if (!C4LoadText(*this, TEXT("golden_cases.json"), ManifestJson)
		|| !TestTrue(TEXT("manifest parses"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ManifestJson), Manifest) && Manifest.IsValid()))
	{
		return false;
	}
	TMap<FString, FString> OutputOf;
	int32 Ran = 0;
	for (const TSharedPtr<FJsonValue>& CaseValue : Manifest->GetArrayField(TEXT("cases")))
	{
		const TSharedPtr<FJsonObject> Case = CaseValue->AsObject();
		const FString Name = Case->GetStringField(TEXT("case"));
		OutputOf.Add(Name, Case->GetStringField(TEXT("output")));
		FString Step;
		if (!Case->TryGetStringField(TEXT("step"), Step) || Step != TEXT("C4"))
		{
			continue;
		}
		const FString Input = Case->GetStringField(TEXT("input"));
		const FString Layer = Case->GetStringField(TEXT("layer"));
		const uint32 Seed = static_cast<uint32>(Case->GetNumberField(TEXT("seed")));

		// a derived input = an earlier case's output file + add_layers + the lock / delete / move / hand recipe on this case's layer
		FString DerivedFrom;
		if (Case->TryGetStringField(TEXT("derived_from"), DerivedFrom))
		{
			FLevelPropsFile Derived;
			const FString* Source = OutputOf.Find(DerivedFrom);
			if (TestNotNull(FString::Printf(TEXT("%s: source case %s listed first"), *Name, *DerivedFrom), Source) && C4ParseProps(*this, *Source, Derived))
			{
				FString AddLayers;
				FLevelPropsFile Extra;
				if (Case->TryGetStringField(TEXT("add_layers"), AddLayers) && C4ParseProps(*this, AddLayers, Extra))
				{
					Derived.Layers.Append(Extra.Layers);
				}
				if (FLevelPropsLayer* L = Derived.Layers.FindByPredicate([&Layer](const FLevelPropsLayer& X) { return X.Id == Layer; }))
				{
					for (const int32 I : C4IntArray(*Case, TEXT("lock")))
					{
						if (L->Objects.IsValidIndex(I))
						{
							L->Objects[I].bLocked = true;
						}
					}
				}
				const FString DerivedText = LevelPropsFile::Write(Derived);
				if (bWrite)
				{
					TestTrue(FString::Printf(TEXT("%s: write %s"), *Name, *Input), FFileHelper::SaveStringToFile(DerivedText, *C4Path(Input), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
				}
				else
				{
					FString Stored;
					if (C4LoadText(*this, Input, Stored))
					{
						TestTrue(FString::Printf(TEXT("%s: derived input is current (%s)"), *Name, *C4FirstDiff(C4StripCr(Stored), DerivedText)), C4StripCr(Stored) == DerivedText);
					}
				}
			}
		}

		FC4World W;
		if (!C4LoadBase(*this, Case->GetStringField(TEXT("layout")), W) || !C4ParseProps(*this, Input, W.File))
		{
			continue;
		}
		FPropsPlacementInput In;
		C4MakeInput(*this, W, *Layer, In);
		In.Seed = Seed;
		FPropsPlacementResult Result;
		const FString Text = RerollInPlace(*this, W, *Layer, Seed, &Result);
		++Ran;
		TestTrue(FString::Printf(TEXT("%s: places something"), *Name), Result.Report.Placed > 0);
		C4CheckInvariants(*this, Name, In, Result);
		const FString Output = Case->GetStringField(TEXT("output"));
		if (bWrite)
		{
			TestTrue(FString::Printf(TEXT("%s: write %s"), *Name, *Output), FFileHelper::SaveStringToFile(Text, *C4Path(Output), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
			AddInfo(FString::Printf(TEXT("golden %s written: %d placed, %d kept, short_by %d, warnings %s"), *Name, Result.Report.Placed, Result.Report.Kept,
				Result.Report.ShortBy, *FString::Join(Result.Report.Warnings, TEXT(", "))));
			continue;
		}
		FString Golden;
		if (C4LoadText(*this, Output, Golden))
		{
			TestTrue(FString::Printf(TEXT("%s matches %s (%s)"), *Name, *Output, *C4FirstDiff(C4StripCr(Golden), Text)), C4StripCr(Golden) == Text);
		}
	}
	TestTrue(TEXT("6+ C4 golden cases"), Ran >= 6);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
