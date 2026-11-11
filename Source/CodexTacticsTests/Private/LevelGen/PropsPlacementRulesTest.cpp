// CodexTactics.LevelGen.Props.Rng.* / .Placement.* - level authoring stage 1, task C3 (docs/design/levelgen/PROPS_LAYERS.md sections 0,
// 2.2-2.5 and 5): the mulberry32 / fnv1a reference vectors of the TS port, the common placement algorithm with crates and barrels
// (same seed -> identical, different seed -> different, locked kept and counted, spacing / min_gap_any / exclusions / keep-outs on 50
// seeds, the explosive gap, the short report) and the golden outputs Content/Data/LevelGen/props/fixtures/golden_*.json that the TS port
// (T2) must reproduce byte for byte. Pure, no world. UE-only (no Godot reference).
// Regenerate the golden files: set CODEX_PROPS_GOLDEN_WRITE=1, run CodexTactics.LevelGen.Props.Placement.Golden, commit them.

#include "LevelGen/PropsPlacementRules.h"
#include "Misc/AutomationTest.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/PlatformMisc.h"
#include "LevelGen/LevelPropsFile.h"
#include "LevelGen/PropsRng.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

#define PROPS_RNG_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.LevelGen.Props.Rng." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
#define PROPS_PLACEMENT_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.LevelGen.Props.Placement." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace PropsPlacementTestLocal
{
	const TCHAR* const GFixtures = TEXT("Data/LevelGen/props/fixtures");
	const TCHAR* const GSmallLayout = TEXT("fixture_small.layout.json");
	const TCHAR* const GSmallInput = TEXT("golden_in_small.props.json");
	const TCHAR* const GDepotLayout = TEXT("../../accepted/gen_ref_01_02_depot_1.json");
	const TCHAR* const GDepotInput = TEXT("golden_in_depot.props.json");

	FString FixturePath(const FString& Relative)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / GFixtures / Relative);
	}

	bool LoadText(FAutomationTestBase& Test, const FString& Relative, FString& Out)
	{
		return Test.TestTrue(FString::Printf(TEXT("%s readable"), *Relative), FFileHelper::LoadFileToString(Out, *FixturePath(Relative)));
	}

	TSharedPtr<FJsonObject> LoadBalance(FAutomationTestBase& Test)
	{
		FString Json;
		TSharedPtr<FJsonObject> Root;
		if (LoadText(Test, TEXT("../../../Balance/props_balance.json"), Json))
		{
			Test.TestTrue(TEXT("props_balance.json parses"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) && Root.IsValid());
		}
		return Root;
	}

	bool LoadLayoutForPlacement(FAutomationTestBase& Test, const TSharedPtr<FJsonObject>& Balance, const FString& Relative, FPropsLayoutView& Out)
	{
		FString Json;
		if (!LoadText(Test, Relative, Json) || !Balance.IsValid())
		{
			return false;
		}
		TArray<FString> Errors;
		FPropsLayoutConfig Config;
		Test.TestTrue(TEXT("layout_view config reads"), FPropsLayoutView::ReadConfig(*Balance, Config, Errors));
		const bool bOk = FPropsLayoutView::Build(Json, Out, Errors, Config);
		for (const FString& E : Errors)
		{
			Test.AddError(FString::Printf(TEXT("%s: %s"), *Relative, *E));
		}
		return Test.TestTrue(FString::Printf(TEXT("%s builds"), *Relative), bOk);
	}

	bool LoadProps(FAutomationTestBase& Test, const FString& Relative, FLevelPropsFile& Out)
	{
		FString Json;
		if (!LoadText(Test, Relative, Json))
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

	/** Everything a placement case needs. */
	struct FWorld
	{
		TSharedPtr<FJsonObject> Balance;
		FPropsLayoutView View;
		FLevelPropsFile File;
	};

	bool LoadWorld(FAutomationTestBase& Test, const FString& Layout, const FString& Input, FWorld& Out)
	{
		Out.Balance = LoadBalance(Test);
		return Out.Balance.IsValid() && LoadLayoutForPlacement(Test, Out.Balance, Layout, Out.View) && LoadProps(Test, Input, Out.File);
	}

	FLevelPropsLayer* FindLayer(FLevelPropsFile& File, const TCHAR* Id)
	{
		return File.Layers.FindByPredicate([Id](const FLevelPropsLayer& L) { return L.Id == Id; });
	}

	/** Re-rolls a copy of World.File; returns the written file (empty on refusal). */
	FString Reroll(FAutomationTestBase& Test, const FWorld& World, const TCHAR* Layer, uint32 Seed, FPropsPlacementResult* OutResult = nullptr,
		FLevelPropsFile* OutFile = nullptr)
	{
		FLevelPropsFile Copy = World.File;
		TArray<FString> Errors;
		if (!Test.TestTrue(FString::Printf(TEXT("re-roll %s seed %u"), Layer, Seed), PropsPlacementRules::RerollLayer(Copy, *World.Balance, World.View, Layer, Seed, Errors, OutResult)))
		{
			return FString();
		}
		if (OutFile)
		{
			*OutFile = Copy;
		}
		return LevelPropsFile::Write(Copy);
	}

	bool MakeInput(FAutomationTestBase& Test, const FWorld& World, const TCHAR* Layer, uint32 Seed, FPropsPlacementInput& Out)
	{
		TArray<FString> Errors;
		const bool bOk = PropsPlacementRules::MakeInput(World.File, *World.Balance, World.View, Layer, Out, Errors);
		for (const FString& E : Errors)
		{
			Test.AddError(E);
		}
		Out.Seed = Seed;
		return Test.TestTrue(FString::Printf(TEXT("input for %s"), Layer), bOk && Errors.IsEmpty());
	}

	int64 DistSq(FIntPoint A, FIntPoint B)
	{
		const int64 DX = static_cast<int64>(A.X) - B.X;
		const int64 DY = static_cast<int64>(A.Y) - B.Y;
		return DX * DX + DY * DY;
	}

	TArray<FLevelPropsObject> NewObjects(const FPropsPlacementInput& In, const FPropsPlacementResult& Result)
	{
		TArray<FLevelPropsObject> Out;
		for (int32 I = In.Kept.Num(); I < Result.Objects.Num(); ++I)
		{
			Out.Add(Result.Objects[I]);
		}
		return Out;
	}

	/** Validate + independent checks of the hard filters on the new objects (not the code under test's own helpers). */
	void CheckInvariants(FAutomationTestBase& Test, const FString& Ctx, const FPropsPlacementInput& In, const FPropsPlacementResult& Result)
	{
		for (const FPropsViolation& V : PropsPlacementRules::Validate(In, Result.Objects))
		{
			Test.AddError(FString::Printf(TEXT("%s: %s: %s"), *Ctx, *V.Id, *V.Reason));
		}
		const FPropsRules& R = In.Rules;
		const TArray<FPropsExclusion> Shapes = In.View->BuildExclusions(R.Exclude);
		const TArray<FLevelPropsObject> New = NewObjects(In, Result);
		const auto Half = [&R](const FString& Variant) { const int32* H = R.HalfSizeCm.Find(Variant); return H ? *H : 0; };
		for (const FLevelPropsObject& O : New)
		{
			if (FPropsLayoutView::IsExcluded(Shapes, O.Pos, O.WalkLayer))
			{
				Test.AddError(FString::Printf(TEXT("%s: %s inside an exclusion"), *Ctx, *O.Id));
			}
			if (O.Pos.X % PropsPlacementRules::LatticeCm != 0 || O.Pos.Y % PropsPlacementRules::LatticeCm != 0)
			{
				Test.AddError(FString::Printf(TEXT("%s: %s off the 50 cm lattice"), *Ctx, *O.Id));
			}
			for (const FLevelPropsObject& P : In.OtherObjects)
			{
				const int64 Gap = static_cast<int64>(Half(O.Variant)) + Half(P.Variant) + R.MinGapAnyCm;
				if (DistSq(O.Pos, P.Pos) < Gap * Gap)
				{
					Test.AddError(FString::Printf(TEXT("%s: %s within min_gap_any of %s"), *Ctx, *O.Id, *P.Id));
				}
			}
			for (const FLevelPropsKeepOut& K : In.KeepOuts)
			{
				const int64 Radius = K.RadiusCm > 0 ? K.RadiusCm : R.MinSpacingCm;
				if (DistSq(O.Pos, K.Pos) < Radius * Radius)
				{
					Test.AddError(FString::Printf(TEXT("%s: %s inside the keep-out of %s"), *Ctx, *O.Id, *K.Id));
				}
			}
			if (R.Kind == PropsPlacementRules::KindCrates)
			{
				for (const FLevelPropsObject& P : Result.Objects)
				{
					if (P.Id != O.Id && DistSq(O.Pos, P.Pos) < static_cast<int64>(R.MinSpacingCm) * R.MinSpacingCm)
					{
						Test.AddError(FString::Printf(TEXT("%s: %s closer than min_spacing to %s"), *Ctx, *O.Id, *P.Id));
					}
				}
			}
		}
		// kept objects are kept unchanged and counted
		for (int32 I = 0; I < In.Kept.Num(); ++I)
		{
			if (!Test.TestTrue(FString::Printf(TEXT("%s: kept object %d present"), *Ctx, I), Result.Objects.IsValidIndex(I)))
			{
				break;
			}
			Test.TestEqual(FString::Printf(TEXT("%s: kept id"), *Ctx), Result.Objects[I].Id, In.Kept[I].Id);
			Test.TestEqual(FString::Printf(TEXT("%s: kept pos"), *Ctx), Result.Objects[I].Pos, In.Kept[I].Pos);
		}
		const FPropsPlacementReport& Rep = Result.Report;
		Test.TestEqual(FString::Printf(TEXT("%s: report kept"), *Ctx), Rep.Kept, In.Kept.Num());
		Test.TestEqual(FString::Printf(TEXT("%s: report placed"), *Ctx), Rep.Placed, New.Num());
		Test.TestEqual(FString::Printf(TEXT("%s: short_by"), *Ctx), Rep.ShortBy, FMath::Max(0, Rep.Requested - Rep.Kept - Rep.Placed));
		// ids unique, none taken from the reserved set
		TSet<FString> Seen;
		for (const FLevelPropsObject& O : Result.Objects)
		{
			Test.TestFalse(FString::Printf(TEXT("%s: id %s unique"), *Ctx, *O.Id), Seen.Contains(O.Id));
			Seen.Add(O.Id);
		}
		for (const FLevelPropsObject& O : New)
		{
			Test.TestFalse(FString::Printf(TEXT("%s: new id %s not reserved"), *Ctx, *O.Id), In.ReservedIds.Contains(O.Id));
			Test.TestTrue(FString::Printf(TEXT("%s: id format %s"), *Ctx, *O.Id), O.Id.StartsWith(FString::Printf(TEXT("%s_%08x_"), *In.LayerId, In.Seed)));
		}
	}

	FString StripCr(const FString& S)
	{
		return S.Replace(TEXT("\r"), TEXT(""));
	}

	/** First differing line, for a readable golden mismatch. */
	FString FirstDiff(const FString& A, const FString& B)
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

	TArray<int32> IntArray(const FJsonObject& J, const TCHAR* Key)
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

	/** The "derived_from" recipe of golden_cases.json applied to a parsed props file. */
	void Derive(FLevelPropsFile& File, const FString& LayerId, const FJsonObject& Case)
	{
		FLevelPropsLayer* Layer = File.Layers.FindByPredicate([&LayerId](const FLevelPropsLayer& L) { return L.Id == LayerId; });
		if (!Layer)
		{
			return;
		}
		const TArray<FLevelPropsObject> Original = Layer->Objects;
		for (const int32 I : IntArray(Case, TEXT("lock")))
		{
			if (Original.IsValidIndex(I)) { Layer->Objects[I].bLocked = true; }
		}
		const TArray<int32> MoveBy = IntArray(Case, TEXT("move_by_cm"));
		for (const int32 I : IntArray(Case, TEXT("move")))
		{
			if (Original.IsValidIndex(I) && MoveBy.Num() == 2)
			{
				FLevelPropsOverride& Op = File.Overrides.AddDefaulted_GetRef();
				Op.Op = ELevelPropsOp::Move;
				Op.Id = Original[I].Id;
				Op.Pos = Original[I].Pos + FIntPoint(MoveBy[0], MoveBy[1]);
				Op.WalkLayer = Original[I].WalkLayer;
				Op.Yaw = Original[I].Yaw;
			}
		}
		for (const int32 I : IntArray(Case, TEXT("delete")))
		{
			if (Original.IsValidIndex(I))
			{
				FLevelPropsOverride& Op = File.Overrides.AddDefaulted_GetRef();
				Op.Op = ELevelPropsOp::Delete;
				Op.Id = Original[I].Id;
				Op.Pos = Original[I].Pos;
			}
		}
		int32 HandIndex = 0;
		TArray<FString> HandIds;
		for (const int32 I : IntArray(Case, TEXT("hand")))
		{
			if (Original.IsValidIndex(I))
			{
				FLevelPropsOverride& Op = File.Overrides.AddDefaulted_GetRef();
				Op.Op = ELevelPropsOp::Add;
				Op.Object = Original[I];
				Op.Object.Id = FString::Printf(TEXT("hand_%s_%d"), *LayerId, HandIndex++);
				Op.Object.Origin = TEXT("hand");
				Op.Object.bLocked = false;
				Op.Object.Extra.Reset();
				Op.Id = Op.Object.Id;
				HandIds.Add(Original[I].Id);
			}
		}
		Layer->Objects.RemoveAll([&HandIds](const FLevelPropsObject& O) { return HandIds.Contains(O.Id); });
		Layer->Report.Reset();
	}
}

// ---- RNG: the JS reference vectors -------------------------------------------------------------------------------------------------

PROPS_RNG_TEST(FPropsRngMulberryTest, "Mulberry32Vectors")
bool FPropsRngMulberryTest::RunTest(const FString&)
{
	struct FVector
	{
		uint32 Seed;
		uint32 Out[5];
	};
	const FVector Vectors[] = {
		{ 0u, { 0x4434b462u, 0x00159c37u, 0x39285b08u, 0x256d8104u, 0x77a2cbd4u } },
		{ 1u, { 0xa087eaf3u, 0x00b349c9u, 0x8706c4ebu, 0xfb2627fdu, 0xf7e79d2bu } },
		{ 7u, { 0x02ff152cu, 0x0fdc7f12u, 0xfa169e5eu, 0xb2f38b96u, 0x857d6fe7u } },
		{ 0xffffffffu, { 0xe57bf3d3u, 0x3081a5a4u, 0xb7350390u, 0xf1ade904u, 0xd8616a2fu } },
	};
	for (const FVector& V : Vectors)
	{
		FPropsRng Rng(V.Seed);
		for (int32 I = 0; I < 5; ++I)
		{
			TestEqual(FString::Printf(TEXT("mulberry32(0x%08x) #%d"), V.Seed, I), Rng.NextU32(), V.Out[I]);
		}
	}
	// the layer stream of crates / seed 7 = mulberry32(7 ^ fnv1a("crates"))
	TestEqual(TEXT("stream seed"), PropsRng::StreamSeed(7u, TEXT("crates")), 0xfcecbe4eu);
	FPropsRng Stream(PropsRng::StreamSeed(7u, TEXT("crates")));
	TestEqual(TEXT("stream #0"), Stream.NextU32(), 0x9bb9e7c5u);
	TestEqual(TEXT("stream #1"), Stream.NextU32(), 0x694c7fa9u);
	TestEqual(TEXT("stream #2"), Stream.NextU32(), 0x82a1553fu);
	return true;
}

PROPS_RNG_TEST(FPropsRngFnvTest, "Fnv1aVectors")
bool FPropsRngFnvTest::RunTest(const FString&)
{
	TestEqual(TEXT("fnv1a('')"), PropsRng::Fnv1a(FString()), 0x811c9dc5u);
	TestEqual(TEXT("fnv1a('a')"), PropsRng::Fnv1a(TEXT("a")), 0xe40c292cu);
	TestEqual(TEXT("fnv1a('foobar')"), PropsRng::Fnv1a(TEXT("foobar")), 0xbf9cf968u);
	TestEqual(TEXT("fnv1a('crates')"), PropsRng::Fnv1a(TEXT("crates")), 0xfcecbe49u);
	TestEqual(TEXT("fnv1a('barrels')"), PropsRng::Fnv1a(TEXT("barrels")), 0x575f8e06u);
	const TCHAR Cyrillic[] = { 0x044F, 0x0449, 0x0438, 0x043A, 0 }; // hashed as UTF-8 bytes like TextEncoder
	TestEqual(TEXT("fnv1a(UTF-8 Cyrillic)"), PropsRng::Fnv1a(Cyrillic), 0xdb9bb9b9u);
	return true;
}

PROPS_RNG_TEST(FPropsRngBoundedTest, "BoundedShuffleModulo")
bool FPropsRngBoundedTest::RunTest(const FString&)
{
	{
		FPropsRng Rng(1u);
		const uint32 Expected[] = { 6, 0, 5, 9, 9, 2, 6, 7 };
		for (int32 I = 0; I < 8; ++I)
		{
			TestEqual(FString::Printf(TEXT("nextBelow(10) #%d"), I), Rng.NextBelow(10u), Expected[I]);
		}
	}
	{
		FPropsRng Rng(42u);
		TArray<int32> Items = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
		Rng.Shuffle(Items);
		TestEqual(TEXT("shuffle seed 42"), Items, TArray<int32>{ 0, 7, 3, 5, 2, 1, 8, 9, 4, 6 });
	}
	{
		FPropsRng Rng(5u);
		const uint32 Expected[] = { 0, 3, 2, 1, 3, 0 };
		for (int32 I = 0; I < 6; ++I)
		{
			TestEqual(FString::Printf(TEXT("next() %% 4 #%d"), I), Rng.NextU32() % 4u, Expected[I]);
		}
	}
	{
		// NextBelow(0) draws nothing; a 1-item shuffle draws nothing
		FPropsRng A(9u);
		FPropsRng B(9u);
		TestEqual(TEXT("NextBelow(0) = 0"), A.NextBelow(0u), 0u);
		TArray<int32> One = { 3 };
		A.Shuffle(One);
		TestEqual(TEXT("streams still aligned"), A.NextU32(), B.NextU32());
	}
	return true;
}

// ---- rules ---------------------------------------------------------------------------------------------------------------------------

PROPS_PLACEMENT_TEST(FPropsPlacementBuildRulesTest, "BuildRules")
bool FPropsPlacementBuildRulesTest::RunTest(const FString&)
{
	using namespace PropsPlacementTestLocal;
	FWorld W;
	if (!LoadWorld(*this, GSmallLayout, GSmallInput, W))
	{
		return false;
	}
	TArray<FString> Errors;
	FPropsRules Crates;
	TestTrue(TEXT("crates rules"), PropsPlacementRules::BuildRules(*W.Balance, *FindLayer(W.File, TEXT("crates")), Crates, Errors));
	TestEqual(TEXT("count mode from the layer"), Crates.CountMode, FString(TEXT("count")));
	TestEqual(TEXT("count value"), Crates.CountValue, 8);
	TestEqual(TEXT("count max from the preset"), Crates.CountMax, 80);
	TestEqual(TEXT("density kept from the preset (0.25 -> 250)"), Crates.DensityMilli, 250);
	TestEqual(TEXT("budget"), Crates.BudgetCap, 120);
	TestEqual(TEXT("variants sorted by key"), Crates.Variants.Num(), 2);
	if (Crates.Variants.Num() == 2)
	{
		TestEqual(TEXT("crate_l first"), Crates.Variants[0].Key, FString(TEXT("crate_l")));
		TestEqual(TEXT("crate_s weight"), Crates.Variants[1].Value, 3);
	}
	TestEqual(TEXT("walk layers"), Crates.WalkLayers, TArray<FString>{ TEXT("ground"), TEXT("roof:*") });
	TestEqual(TEXT("near"), Crates.Near.Num(), 1);
	TestEqual(TEXT("spacing"), Crates.MinSpacingCm, 250);
	TestEqual(TEXT("gap"), Crates.MinGapAnyCm, 120);
	TestEqual(TEXT("exclude spawn overridden key by key"), Crates.Exclude.SpawnCm, 800);
	TestEqual(TEXT("exclude end from $preset standard"), Crates.Exclude.EndCm, 500);
	TestEqual(TEXT("route mandatory"), Crates.Exclude.RouteMandatoryCm, 260);
	TestEqual(TEXT("route variants"), Crates.Exclude.RouteVariantsCm, 200);
	TestTrue(TEXT("crossing"), Crates.Exclude.bCrossing);
	TestEqual(TEXT("hole rim"), Crates.Exclude.HoleRimCm, 200);
	TestEqual(TEXT("crate_s half"), Crates.HalfSizeCm.FindRef(TEXT("crate_s")), 60);
	TestEqual(TEXT("crate_l half"), Crates.HalfSizeCm.FindRef(TEXT("crate_l")), 80);
	TestEqual(TEXT("barrel half"), Crates.HalfSizeCm.FindRef(TEXT("barrel")), 30);

	FPropsRules Barrels;
	TestTrue(TEXT("barrels rules"), PropsPlacementRules::BuildRules(*W.Balance, *FindLayer(W.File, TEXT("barrels")), Barrels, Errors));
	TestEqual(TEXT("share 0.5 -> 500"), Barrels.ExplosiveShareMilli, 500);
	TestEqual(TEXT("explosive gap"), Barrels.ExplosiveMinGapCm, 400);
	TestEqual(TEXT("never within spawn"), Barrels.ExplosiveNeverSpawnCm, 1000);
	TestEqual(TEXT("cluster min"), Barrels.ClusterMin, 2);
	TestEqual(TEXT("cluster max"), Barrels.ClusterMax, 4);
	TestEqual(TEXT("ring"), FIntPoint(Barrels.RingMinCm, Barrels.RingMaxCm), FIntPoint(70, 130));
	TestEqual(TEXT("spacing from the layer"), Barrels.MinSpacingCm, 500);
	TestEqual(TEXT("gap from the preset"), Barrels.MinGapAnyCm, 100);
	TestEqual(TEXT("exclude spawn over $preset"), Barrels.Exclude.SpawnCm, 800);
	TestEqual(TEXT("exclude ladder from $preset"), Barrels.Exclude.LadderCm, 300);
	TestTrue(TEXT("no errors"), Errors.IsEmpty());

	// a named preset that does not exist is an error, the rules alone are still read
	FLevelPropsLayer Bad = *FindLayer(W.File, TEXT("crates"));
	Bad.Preset = TEXT("nope");
	FPropsRules BadRules;
	TArray<FString> BadErrors;
	TestFalse(TEXT("missing preset refused"), PropsPlacementRules::BuildRules(*W.Balance, Bad, BadRules, BadErrors));
	TestEqual(TEXT("one error"), BadErrors.Num(), 1);
	TestEqual(TEXT("rules still read"), BadRules.CountValue, 8);
	TestEqual(TEXT("no preset spacing"), BadRules.MinSpacingCm, 0);
	return true;
}

// ---- determinism -----------------------------------------------------------------------------------------------------------------

PROPS_PLACEMENT_TEST(FPropsPlacementSameSeedTest, "SameSeedIdentical")
bool FPropsPlacementSameSeedTest::RunTest(const FString&)
{
	using namespace PropsPlacementTestLocal;
	FWorld Small;
	FWorld Depot;
	if (!LoadWorld(*this, GSmallLayout, GSmallInput, Small) || !LoadWorld(*this, GDepotLayout, GDepotInput, Depot))
	{
		return false;
	}
	for (const TCHAR* Layer : { TEXT("crates"), TEXT("barrels") })
	{
		const FString A = Reroll(*this, Small, Layer, 77);
		const FString B = Reroll(*this, Small, Layer, 77);
		TestTrue(FString::Printf(TEXT("small %s: same seed -> byte-identical file"), Layer), !A.IsEmpty() && A == B);
		const FString C = Reroll(*this, Depot, Layer, 3);
		const FString D = Reroll(*this, Depot, Layer, 3);
		TestTrue(FString::Printf(TEXT("depot %s: same seed -> byte-identical file"), Layer), !C.IsEmpty() && C == D);
	}
	// re-rolling a stored roll with its own seed reproduces it (ids too: the discarded objects' ids are not reserved)
	FLevelPropsFile Once;
	Reroll(*this, Depot, TEXT("crates"), 9, nullptr, &Once);
	FWorld Again = Depot;
	Again.File = Once;
	TestEqual(TEXT("re-roll of a stored roll with its seed is idempotent"), Reroll(*this, Again, TEXT("crates"), 9), LevelPropsFile::Write(Once));
	return true;
}

PROPS_PLACEMENT_TEST(FPropsPlacementDifferentSeedTest, "DifferentSeedDiffers")
bool FPropsPlacementDifferentSeedTest::RunTest(const FString&)
{
	using namespace PropsPlacementTestLocal;
	FWorld Small;
	FWorld Depot;
	if (!LoadWorld(*this, GSmallLayout, GSmallInput, Small) || !LoadWorld(*this, GDepotLayout, GDepotInput, Depot))
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
	for (const TCHAR* Layer : { TEXT("crates"), TEXT("barrels") })
	{
		FPropsPlacementResult A;
		FPropsPlacementResult B;
		Reroll(*this, Small, Layer, 21, &A);
		Reroll(*this, Small, Layer, 22, &B);
		TestFalse(FString::Printf(TEXT("small %s: seeds 21 / 22 place differently"), Layer), Positions(A) == Positions(B));
		FPropsPlacementResult C;
		FPropsPlacementResult D;
		Reroll(*this, Depot, Layer, 1, &C);
		Reroll(*this, Depot, Layer, 2, &D);
		TestFalse(FString::Printf(TEXT("depot %s: seeds 1 / 2 place differently"), Layer), Positions(C) == Positions(D));
		TestEqual(FString::Printf(TEXT("depot %s: the target does not depend on the seed"), Layer), C.Report.Requested, D.Report.Requested);
	}
	return true;
}

// ---- locked / kept -------------------------------------------------------------------------------------------------------------------

PROPS_PLACEMENT_TEST(FPropsPlacementLockedTest, "LockedKeptAndCounted")
bool FPropsPlacementLockedTest::RunTest(const FString&)
{
	using namespace PropsPlacementTestLocal;
	FWorld W;
	if (!LoadWorld(*this, GSmallLayout, GSmallInput, W))
	{
		return false;
	}
	FPropsPlacementInput In;
	if (!MakeInput(*this, W, TEXT("crates"), 21, In))
	{
		return false;
	}
	// kept = the locked crate + the hand crate; the unlocked one is discarded; the deleted one leaves a keep-out disk
	TArray<FString> KeptIds;
	for (const FLevelPropsObject& K : In.Kept)
	{
		KeptIds.Add(K.Id);
	}
	TestEqual(TEXT("kept ids"), KeptIds, TArray<FString>{ TEXT("crates_00000015_0"), TEXT("hand_crates_0") });
	TestEqual(TEXT("one keep-out"), In.KeepOuts.Num(), 1);
	TestEqual(TEXT("other layers' objects (barrel + mine)"), In.OtherObjects.Num(), 2);
	TestTrue(TEXT("override ids reserved"), In.ReservedIds.Contains(TEXT("crates_00000015_2")));

	const FPropsPlacementResult Result = PropsPlacementRules::Place(In);
	CheckInvariants(*this, TEXT("small crates"), In, Result);
	TestEqual(TEXT("requested 8"), Result.Report.Requested, 8);
	TestEqual(TEXT("kept 2 counted"), Result.Report.Kept, 2);
	TestEqual(TEXT("6 new"), Result.Report.Placed, 6);
	TestFalse(TEXT("not short"), Result.Report.IsShort());

	// the file after the re-roll: the locked record stays as stored, the hand crate stays in the overrides only, the unlocked one is gone
	FLevelPropsFile After;
	Reroll(*this, W, TEXT("crates"), 21, nullptr, &After);
	const FLevelPropsLayer* Layer = FindLayer(After, TEXT("crates"));
	if (TestNotNull(TEXT("crates layer"), Layer))
	{
		TestEqual(TEXT("stored objects = locked + 6 new"), Layer->Objects.Num(), 7);
		TestEqual(TEXT("locked first"), Layer->Objects[0].Id, FString(TEXT("crates_00000015_0")));
		TestEqual(TEXT("locked pos unchanged"), Layer->Objects[0].Pos, FIntPoint(-1000, 1500));
		// the unlocked record is gone; its id is free again (the same seed re-uses it for a new object - re-rolls stay idempotent)
		TestFalse(TEXT("the unlocked crate was re-rolled away"), Layer->Objects.ContainsByPredicate([](const FLevelPropsObject& O)
		{
			return O.Id == TEXT("crates_00000015_1") && O.Pos == FIntPoint(-1500, -500) && O.Yaw == 90;
		}));
		TestFalse(TEXT("hand crate not copied into the layer"), Layer->Objects.ContainsByPredicate([](const FLevelPropsObject& O) { return O.Id == TEXT("hand_crates_0"); }));
		TestEqual(TEXT("seed stored"), Layer->Seed, 21u);
		TestTrue(TEXT("report stored"), Layer->Report.IsValid());
	}
	TestEqual(TEXT("overrides untouched"), After.Overrides.Num(), W.File.Overrides.Num());
	TArray<FString> Warnings;
	const FLevelPropsResolved Resolved = LevelPropsFile::ApplyOverrides(After, &Warnings);
	TestTrue(TEXT("no override warnings after the re-roll"), Warnings.IsEmpty());
	int32 Crates = 0;
	for (const FLevelPropsResolvedObject& R : Resolved.Objects)
	{
		Crates += R.LayerId == TEXT("crates") ? 1 : 0;
	}
	TestEqual(TEXT("resolved crates = requested"), Crates, 8);

	// a locked barrel of another roll (explosive) is kept and the new explosives keep the gap to it
	FPropsPlacementInput BarrelsIn;
	if (MakeInput(*this, W, TEXT("barrels"), 5, BarrelsIn))
	{
		TestEqual(TEXT("locked barrel kept"), BarrelsIn.Kept.Num(), 1);
		CheckInvariants(*this, TEXT("small barrels"), BarrelsIn, PropsPlacementRules::Place(BarrelsIn));
	}
	return true;
}

// ---- invariants on 50 seeds ----------------------------------------------------------------------------------------------------------

PROPS_PLACEMENT_TEST(FPropsPlacementInvariantsSmallTest, "Invariants.Small50Seeds")
bool FPropsPlacementInvariantsSmallTest::RunTest(const FString&)
{
	using namespace PropsPlacementTestLocal;
	FWorld W;
	if (!LoadWorld(*this, GSmallLayout, GSmallInput, W))
	{
		return false;
	}
	int32 Explosives = 0;
	for (const TCHAR* Layer : { TEXT("crates"), TEXT("barrels") })
	{
		FPropsPlacementInput In;
		if (!MakeInput(*this, W, Layer, 0, In))
		{
			continue;
		}
		for (uint32 Seed = 1; Seed <= 50; ++Seed)
		{
			In.Seed = Seed;
			const FPropsPlacementResult Result = PropsPlacementRules::Place(In);
			CheckInvariants(*this, FString::Printf(TEXT("small %s seed %u"), Layer, Seed), In, Result);
			TestTrue(FString::Printf(TEXT("small %s seed %u places something"), Layer, Seed), Result.Report.Placed > 0);
			for (const FLevelPropsObject& O : Result.Objects)
			{
				Explosives += O.Variant == PropsPlacementRules::VariantBarrelExplosive && !O.bLocked ? 1 : 0;
			}
		}
	}
	TestTrue(TEXT("explosive clusters occur"), Explosives > 0);
	return true;
}

PROPS_PLACEMENT_TEST(FPropsPlacementInvariantsDepotTest, "Invariants.Depot50Seeds")
bool FPropsPlacementInvariantsDepotTest::RunTest(const FString&)
{
	using namespace PropsPlacementTestLocal;
	FWorld W;
	if (!LoadWorld(*this, GDepotLayout, GDepotInput, W))
	{
		return false;
	}
	// barrels with a crates roll in place, so min_gap_any to another layer is exercised on the big map
	FLevelPropsFile WithCrates;
	Reroll(*this, W, TEXT("crates"), 1, nullptr, &WithCrates);
	W.File = WithCrates;
	for (const TCHAR* Layer : { TEXT("crates"), TEXT("barrels") })
	{
		FPropsPlacementInput In;
		if (!MakeInput(*this, W, Layer, 0, In))
		{
			continue;
		}
		for (uint32 Seed = 1; Seed <= 50; ++Seed)
		{
			In.Seed = Seed;
			const FPropsPlacementResult Result = PropsPlacementRules::Place(In);
			CheckInvariants(*this, FString::Printf(TEXT("depot %s seed %u"), Layer, Seed), In, Result);
			TestFalse(FString::Printf(TEXT("depot %s seed %u not short"), Layer, Seed), Result.Report.IsShort());
		}
	}
	return true;
}

// ---- explosives --------------------------------------------------------------------------------------------------------------------

PROPS_PLACEMENT_TEST(FPropsPlacementExplosiveGapTest, "ExplosiveGap")
bool FPropsPlacementExplosiveGapTest::RunTest(const FString&)
{
	using namespace PropsPlacementTestLocal;
	FWorld W;
	if (!LoadWorld(*this, GDepotLayout, GDepotInput, W))
	{
		return false;
	}
	FPropsPlacementInput In;
	if (!MakeInput(*this, W, TEXT("barrels"), 0, In))
	{
		return false;
	}
	In.Rules.ExplosiveShareMilli = 1000; // every cluster wants to be explosive: only the gap / spawn rules decide
	const int64 Gap = In.Rules.ExplosiveMinGapCm;
	const int64 Never = In.Rules.ExplosiveNeverSpawnCm;
	TestEqual(TEXT("designer: explosives >= 400 cm apart"), Gap, static_cast<int64>(400));
	int32 Total = 0;
	int32 ShortWarnings = 0;
	for (uint32 Seed = 1; Seed <= 20; ++Seed)
	{
		In.Seed = Seed;
		const FPropsPlacementResult Result = PropsPlacementRules::Place(In);
		CheckInvariants(*this, FString::Printf(TEXT("explosive seed %u"), Seed), In, Result);
		TArray<const FLevelPropsObject*> Ex;
		for (const FLevelPropsObject& O : Result.Objects)
		{
			if (O.Variant == PropsPlacementRules::VariantBarrelExplosive)
			{
				Ex.Add(&O);
				TestTrue(FString::Printf(TEXT("seed %u: %s outside never_within spawn"), Seed, *O.Id), DistSq(O.Pos, W.View.GetSpawnPos()) > Never * Never);
			}
		}
		for (int32 I = 0; I < Ex.Num(); ++I)
		{
			for (int32 J = I + 1; J < Ex.Num(); ++J)
			{
				const TSharedPtr<FJsonValue>* CI = Ex[I]->Extra.Find(PropsPlacementRules::ClusterField);
				const TSharedPtr<FJsonValue>* CJ = Ex[J]->Extra.Find(PropsPlacementRules::ClusterField);
				const bool bSameCluster = CI && CJ && (*CI)->AsString() == (*CJ)->AsString();
				if (!bSameCluster)
				{
					TestTrue(FString::Printf(TEXT("seed %u: %s / %s >= 400 cm"), Seed, *Ex[I]->Id, *Ex[J]->Id), DistSq(Ex[I]->Pos, Ex[J]->Pos) >= Gap * Gap);
				}
			}
		}
		Total += Ex.Num();
		ShortWarnings += Result.Report.Warnings.ContainsByPredicate([](const FString& S) { return S.StartsWith(TEXT("explosive_short ")); }) ? 1 : 0;
	}
	TestTrue(TEXT("explosives placed"), Total > 0);
	// share 1.0 with a 25 m spawn ban on a 124 x 177 m map: some clusters must be refused (reported, not an error)
	TestTrue(TEXT("explosive_short reported when the rules refuse clusters"), ShortWarnings > 0);
	return true;
}

// ---- failure handling ----------------------------------------------------------------------------------------------------------------

PROPS_PLACEMENT_TEST(FPropsPlacementShortTest, "ShortReport")
bool FPropsPlacementShortTest::RunTest(const FString&)
{
	using namespace PropsPlacementTestLocal;
	FWorld W;
	if (!LoadWorld(*this, GSmallLayout, GSmallInput, W))
	{
		return false;
	}
	FPropsPlacementInput In;
	if (!MakeInput(*this, W, TEXT("crates"), 3, In))
	{
		return false;
	}
	{
		FPropsPlacementInput Capped = In;
		Capped.Rules.CountValue = 500;
		TestEqual(TEXT("requested capped by the preset max"), PropsPlacementRules::Place(Capped).Report.Requested, 80);
		Capped.Rules.CountMax = 1000;
		TestEqual(TEXT("requested capped by the balance budget"), PropsPlacementRules::Place(Capped).Report.Requested, 120);

		FPropsPlacementInput Big = In;
		Big.Rules.CountValue = 500; // far more than a 40 x 40 m yard holds at 250 cm spacing
		Big.Rules.CountMax = MAX_int32;
		Big.Rules.BudgetCap = MAX_int32;
		const FPropsPlacementResult R = PropsPlacementRules::Place(Big);
		TestEqual(TEXT("requested 500"), R.Report.Requested, 500);
		TestTrue(TEXT("short"), R.Report.IsShort());
		TestTrue(TEXT("placed some"), R.Report.Placed > 0);
		TestEqual(TEXT("short_by"), R.Report.ShortBy, 500 - R.Report.Kept - R.Report.Placed);
		CheckInvariants(*this, TEXT("short"), Big, R);
		const TSharedRef<FJsonObject> J = PropsPlacementRules::ReportToJson(R.Report);
		TestEqual(TEXT("report status"), J->GetStringField(TEXT("status")), FString(TEXT("short")));
		TestEqual(TEXT("report gate n/a (TS fast gate)"), J->GetStringField(TEXT("gate")), FString(TEXT("n/a")));
	}
	{
		FPropsPlacementInput Nowhere = In;
		Nowhere.Rules.WalkLayers = { TEXT("floor:nope:*") };
		const FPropsPlacementResult R = PropsPlacementRules::Place(Nowhere);
		TestEqual(TEXT("no eligible area"), R.Report.Eligible, 0);
		TestEqual(TEXT("nothing placed"), R.Report.Placed, 0);
		TestTrue(TEXT("no_candidates warning"), R.Report.Warnings.Contains(TEXT("no_candidates")));
		TestEqual(TEXT("kept still returned"), R.Objects.Num(), 2);
	}
	{
		FPropsPlacementInput NoView = In;
		NoView.View = nullptr;
		const FPropsPlacementResult R = PropsPlacementRules::Place(NoView);
		TestTrue(TEXT("no_layout warning"), R.Report.Warnings.Contains(TEXT("no_layout")));
	}
	{
		FPropsPlacementInput Statues = In;
		Statues.Rules.Kind = TEXT("statues");
		TestTrue(TEXT("kind_unsupported"), PropsPlacementRules::Place(Statues).Report.Warnings.Contains(TEXT("kind_unsupported statues")));
	}
	{
		// a 3 x 3 m layout: a density roll finds no room at all and reports it
		const FString Tiny = TEXT("{\"schema\":\"codex.levelgen_layout/1\",\"name\":\"tiny\",\"bounds\":{\"min\":[0,0],\"max\":[300,300]},")
			TEXT("\"spawn\":{\"pos\":[0,0]},\"end\":[300,300],\"walls\":[],\"buildings\":[],\"covers\":[],\"ladders\":[],\"zones\":[],\"routes\":[]}");
		FPropsLayoutView TinyView;
		TArray<FString> Errors;
		TestTrue(TEXT("tiny layout builds"), FPropsLayoutView::Build(Tiny, TinyView, Errors));
		FPropsPlacementInput Small = In;
		Small.View = &TinyView;
		Small.Kept.Reset();
		Small.KeepOuts.Reset();
		Small.OtherObjects.Reset();
		Small.Rules.CountValue = 5;
		const FPropsPlacementResult R = PropsPlacementRules::Place(Small);
		TestTrue(TEXT("tiny: short"), R.Report.IsShort());
		TestEqual(TEXT("tiny: short by 5"), R.Report.ShortBy, 5);
		TestTrue(TEXT("tiny: every lattice point rejected"), R.Report.Rejected.Spawn > 0);
	}
	return true;
}

PROPS_PLACEMENT_TEST(FPropsPlacementRefusalsTest, "RerollRefusals")
bool FPropsPlacementRefusalsTest::RunTest(const FString&)
{
	using namespace PropsPlacementTestLocal;
	FWorld W;
	if (!LoadWorld(*this, GSmallLayout, GSmallInput, W))
	{
		return false;
	}
	const auto Refused = [this, &W](FLevelPropsFile File, const TCHAR* Layer, const TCHAR* Why)
	{
		const FString Before = LevelPropsFile::Write(File);
		TArray<FString> Errors;
		TestFalse(FString::Printf(TEXT("%s: refused"), Why), PropsPlacementRules::RerollLayer(File, *W.Balance, W.View, Layer, 99, Errors));
		TestEqual(FString::Printf(TEXT("%s: one error"), Why), Errors.Num(), 1);
		TestEqual(FString::Printf(TEXT("%s: file unchanged"), Why), LevelPropsFile::Write(File), Before);
	};
	FLevelPropsFile Frozen = W.File;
	Frozen.bFrozen = true;
	Refused(Frozen, TEXT("crates"), TEXT("frozen file"));
	FLevelPropsFile Locked = W.File;
	FindLayer(Locked, TEXT("crates"))->bLocked = true;
	Refused(Locked, TEXT("crates"), TEXT("locked layer"));
	Refused(W.File, TEXT("nope"), TEXT("missing layer"));
	FLevelPropsFile Unknown = W.File;
	FindLayer(Unknown, TEXT("mines"))->Kind = TEXT("statues");
	Refused(Unknown, TEXT("mines"), TEXT("unknown kind"));
	return true;
}

// ---- golden outputs (the TS port's parity contract) ---------------------------------------------------------------------------------

PROPS_PLACEMENT_TEST(FPropsPlacementGoldenTest, "Golden")
bool FPropsPlacementGoldenTest::RunTest(const FString&)
{
	using namespace PropsPlacementTestLocal;
	const bool bWrite = FPlatformMisc::GetEnvironmentVariable(TEXT("CODEX_PROPS_GOLDEN_WRITE")) == TEXT("1");
	FString ManifestJson;
	TSharedPtr<FJsonObject> Manifest;
	if (!LoadText(*this, TEXT("golden_cases.json"), ManifestJson)
		|| !TestTrue(TEXT("manifest parses"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ManifestJson), Manifest) && Manifest.IsValid()))
	{
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>& Cases = Manifest->GetArrayField(TEXT("cases"));
	TestTrue(TEXT("3+ golden cases"), Cases.Num() >= 3);
	TMap<FString, FString> Outputs;
	for (const TSharedPtr<FJsonValue>& CaseValue : Cases)
	{
		const TSharedPtr<FJsonObject> Case = CaseValue->AsObject();
		FString Step;
		if (Case->TryGetStringField(TEXT("step"), Step) && Step != TEXT("C3"))
		{
			continue; // C4 cases: CodexTactics.LevelGen.Props.C4.Golden
		}
		const FString Name = Case->GetStringField(TEXT("case"));
		const FString Input = Case->GetStringField(TEXT("input"));
		const FString Layer = Case->GetStringField(TEXT("layer"));

		// a derived input = an earlier case's output edited by the recipe; written in write mode, checked for staleness otherwise
		FString DerivedFrom;
		if (Case->TryGetStringField(TEXT("derived_from"), DerivedFrom))
		{
			const FString* Source = Outputs.Find(DerivedFrom);
			FLevelPropsFile Derived;
			TArray<FString> Errors;
			if (TestNotNull(FString::Printf(TEXT("%s: source case %s ran first"), *Name, *DerivedFrom), Source)
				&& TestTrue(TEXT("source parses"), LevelPropsFile::Parse(*Source, Derived, Errors)))
			{
				Derive(Derived, Layer, *Case);
				const FString DerivedText = LevelPropsFile::Write(Derived);
				if (bWrite)
				{
					TestTrue(FString::Printf(TEXT("%s: write %s"), *Name, *Input), FFileHelper::SaveStringToFile(DerivedText, *FixturePath(Input), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
				}
				else
				{
					FString Stored;
					if (LoadText(*this, Input, Stored))
					{
						TestTrue(FString::Printf(TEXT("%s: derived input is current (%s)"), *Name, *FirstDiff(StripCr(Stored), DerivedText)), StripCr(Stored) == DerivedText);
					}
				}
			}
		}

		FWorld W;
		if (!LoadWorld(*this, Case->GetStringField(TEXT("layout")), Input, W))
		{
			continue;
		}
		FPropsPlacementResult Result;
		const FString Text = Reroll(*this, W, *Layer, static_cast<uint32>(Case->GetNumberField(TEXT("seed"))), &Result);
		Outputs.Add(Name, Text);
		TestTrue(FString::Printf(TEXT("%s: places something"), *Name), Result.Report.Placed > 0);
		FPropsPlacementInput In;
		if (MakeInput(*this, W, *Layer, static_cast<uint32>(Case->GetNumberField(TEXT("seed"))), In))
		{
			CheckInvariants(*this, Name, In, Result);
		}
		const FString Output = Case->GetStringField(TEXT("output"));
		if (bWrite)
		{
			TestTrue(FString::Printf(TEXT("%s: write %s"), *Name, *Output), FFileHelper::SaveStringToFile(Text, *FixturePath(Output), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
			AddInfo(FString::Printf(TEXT("golden %s written: %d placed, %d kept, short_by %d"), *Name, Result.Report.Placed, Result.Report.Kept, Result.Report.ShortBy));
			continue;
		}
		FString Golden;
		if (LoadText(*this, Output, Golden))
		{
			TestTrue(FString::Printf(TEXT("%s matches %s (%s)"), *Name, *Output, *FirstDiff(StripCr(Golden), Text)), StripCr(Golden) == Text);
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
