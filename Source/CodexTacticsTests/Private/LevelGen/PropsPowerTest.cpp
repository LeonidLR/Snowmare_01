// CodexTactics.LevelGen.Props.Power.* - turret power phase 1 in the props pipeline (docs/design/TURRET_POWER.md sections 6, 12, 13):
// the generator / turret fields of a props object, the power-link validator (FPropsValidator::ValidatePowerLinks, the Training Ground's
// validatePowerLinks: the same reasons), and the generated maps' power layers (links in reach, ~75 % of the turrets dark, the jerrycans).
// Pure, no world. UE-only (no Godot reference).

#include "LevelGen/PropsGameplayRules.h"
#include "Misc/AutomationTest.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "LevelGen/LevelPropsFile.h"
#include "LevelGen/LevelPropsTypes.h"
#include "LevelGen/PropsValidator.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PropsPowerTestLocal
{
	TSharedPtr<FJsonObject> LoadPowerBalance(FAutomationTestBase& Test)
	{
		FString Text;
		TSharedPtr<FJsonObject> Root;
		Test.TestTrue(TEXT("props_balance.json readable"),
			FFileHelper::LoadFileToString(Text, *(FPaths::ProjectContentDir() / TEXT("Data/Balance/props_balance.json"))));
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root);
		return Root;
	}

	FLevelPropsResolvedObject Make(const TCHAR* Layer, const TCHAR* Id, const TCHAR* Variant, FIntPoint Pos)
	{
		FLevelPropsResolvedObject R;
		R.LayerId = Layer;
		R.Object.Id = Id;
		R.Object.Variant = Variant;
		R.Object.Pos = Pos;
		return R;
	}

	void SetString(FLevelPropsResolvedObject& R, const TCHAR* Key, const TCHAR* Value)
	{
		R.Object.Extra.Add(Key, MakeShared<FJsonValueString>(Value));
	}

	void SetLinks(FLevelPropsResolvedObject& R, const TArray<FString>& Ids)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FString& Id : Ids)
		{
			Values.Add(MakeShared<FJsonValueString>(Id));
		}
		R.Object.Extra.Add(TEXT("links"), MakeShared<FJsonValueArray>(Values));
	}

	TMap<FString, FString> Kinds()
	{
		return { { TEXT("turrets"), TEXT("turrets") }, { TEXT("power"), TEXT("power") }, { TEXT("loot"), TEXT("loot") } };
	}

	bool HasReason(const TArray<FPropsValidationIssue>& Issues, const FString& Id, const FString& Part)
	{
		return Issues.ContainsByPredicate([&](const FPropsValidationIssue& I) { return I.ObjectId == Id && I.Reason.Contains(Part) && !I.bDropped; });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPropsPowerFieldsTest, "CodexTactics.LevelGen.Props.Power.Fields",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPropsPowerFieldsTest::RunTest(const FString&)
{
	using namespace PropsPowerTestLocal;
	FLevelPropsResolvedObject G = Make(TEXT("power"), TEXT("power_00000001_1"), TEXT("generator_diesel"), FIntPoint(0, 0));
	SetString(G, TEXT("start"), TEXT("running_hostile"));
	G.Object.Extra.Add(TEXT("fuel_l"), MakeShared<FJsonValueNumber>(14.0));
	SetLinks(G, { TEXT("t1"), TEXT("t2") });
	const PropsGameplayRules::FGeneratorInput Input = PropsGameplayRules::GeneratorInputOf(G.Object);
	TestEqual(TEXT("start"), Input.Start, EGeneratorStart::RunningHostile);
	TestTrue(TEXT("fuel 14 L"), Input.FuelL.IsSet() && Input.FuelL.GetValue() == 14.f);
	TestTrue(TEXT("links in file order"), Input.Links == TArray<FString>({ TEXT("t1"), TEXT("t2") }));
	const PropsGameplayRules::FGeneratorInput Bare = PropsGameplayRules::GeneratorInputOf(Make(TEXT("power"), TEXT("g"), TEXT("generator_diesel"), FIntPoint(0, 0)).Object);
	TestTrue(TEXT("no fields: dry, the default fuel, no links"), Bare.Start == EGeneratorStart::Dry && !Bare.FuelL.IsSet() && Bare.Links.IsEmpty());
	FLevelPropsResolvedObject T = Make(TEXT("turrets"), TEXT("t1"), TEXT("turret_mg"), FIntPoint(1000, 0));
	SetString(T, TEXT("generator"), TEXT("power_00000001_1"));
	TestEqual(TEXT("a turret's generator"), PropsGameplayRules::TurretGeneratorOf(T.Object), FString(TEXT("power_00000001_1")));
	TestTrue(TEXT("no generator field"), PropsGameplayRules::TurretGeneratorOf(Make(TEXT("turrets"), TEXT("t9"), TEXT("turret_mg"), FIntPoint(0, 0)).Object).IsEmpty());
	TestTrue(TEXT("a power layer object is a generator"), PropsGameplayRules::IsGeneratorObject(TEXT("power"), Make(TEXT("x"), TEXT("x"), TEXT("crate_s"), FIntPoint(0, 0)).Object));
	TestTrue(TEXT("a generator variant anywhere is one"), PropsGameplayRules::IsGeneratorObject(TEXT("turrets"), G.Object));
	TestFalse(TEXT("a turret is not"), PropsGameplayRules::IsGeneratorObject(TEXT("turrets"), T.Object));
	TestEqual(TEXT("class key = the variant"), PropsGameplayRules::ClassKeyFor(TEXT("power"), G.Object, false), FString(TEXT("generator_diesel")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPropsPowerValidatorTest, "CodexTactics.LevelGen.Props.Power.LinkValidator",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPropsPowerValidatorTest::RunTest(const FString&)
{
	using namespace PropsPowerTestLocal;
	const TSharedPtr<FJsonObject> Balance = LoadPowerBalance(*this);
	// A clean network: one generator, two turrets within 40 m, both pointing back.
	TArray<FLevelPropsResolvedObject> Clean;
	FLevelPropsResolvedObject G = Make(TEXT("power"), TEXT("g1"), TEXT("generator_diesel"), FIntPoint(0, 0));
	SetString(G, TEXT("start"), TEXT("dry"));
	SetLinks(G, { TEXT("t1"), TEXT("t2") });
	FLevelPropsResolvedObject T1 = Make(TEXT("turrets"), TEXT("t1"), TEXT("turret_mg"), FIntPoint(1000, 0));
	SetString(T1, TEXT("generator"), TEXT("g1"));
	FLevelPropsResolvedObject T2 = Make(TEXT("turrets"), TEXT("t2"), TEXT("turret_mg"), FIntPoint(0, 3999));
	SetString(T2, TEXT("generator"), TEXT("g1"));
	Clean = { G, T1, T2 };
	TestEqual(TEXT("a clean network has no warning"), FPropsValidator::ValidatePowerLinks(Clean, Kinds(), Balance.Get()).Num(), 0);
	TestEqual(TEXT("no generator on the map (a hand-made props file): no power checks"),
		FPropsValidator::ValidatePowerLinks({ T1 }, Kinds(), Balance.Get()).Num(), 0);

	// Every broken rule, one warning each (the TG's validator.test / power.test use the same cases and reasons).
	FLevelPropsResolvedObject Bad = Make(TEXT("power"), TEXT("g2"), TEXT("generator_diesel"), FIntPoint(0, 0));
	SetString(Bad, TEXT("start"), TEXT("humming"));
	SetLinks(Bad, { TEXT("t1"), TEXT("far"), TEXT("crate"), TEXT("t3"), TEXT("t4") });
	FLevelPropsResolvedObject Far = Make(TEXT("turrets"), TEXT("far"), TEXT("turret_mg"), FIntPoint(4500, 0));
	FLevelPropsResolvedObject Crate = Make(TEXT("loot"), TEXT("crate"), TEXT("loot_pile"), FIntPoint(100, 0));
	FLevelPropsResolvedObject T3 = Make(TEXT("turrets"), TEXT("t3"), TEXT("turret_mg"), FIntPoint(0, 500));
	FLevelPropsResolvedObject T4 = Make(TEXT("turrets"), TEXT("t4"), TEXT("turret_mg"), FIntPoint(0, -500));
	FLevelPropsResolvedObject Lonely = Make(TEXT("turrets"), TEXT("t5"), TEXT("turret_mg"), FIntPoint(9000, 9000));
	FLevelPropsResolvedObject Wrong = Make(TEXT("turrets"), TEXT("t6"), TEXT("turret_mg"), FIntPoint(9000, 0));
	SetString(Wrong, TEXT("generator"), TEXT("g1"));
	const TArray<FLevelPropsResolvedObject> Objects = { G, T1, T2, Bad, Far, Crate, T3, T4, Lonely, Wrong };
	const TArray<FPropsValidationIssue> Issues = FPropsValidator::ValidatePowerLinks(Objects, Kinds(), Balance.Get());
	TestTrue(TEXT("unknown start"), HasReason(Issues, TEXT("g2"), TEXT("unknown generator start 'humming'")));
	TestTrue(TEXT("more than 4 turrets"), HasReason(Issues, TEXT("g2"), TEXT("feeds 5 turrets (max 4)")));
	TestTrue(TEXT("a cable beyond 40 m"), HasReason(Issues, TEXT("g2"), TEXT("cable to 'far' is 4500 cm (max 4000)")));
	TestTrue(TEXT("a link that is no turret"), HasReason(Issues, TEXT("g2"), TEXT("link 'crate' is not a turret of the map")));
	TestTrue(TEXT("a turret on two generators"), HasReason(Issues, TEXT("t1"), TEXT("turret linked by 'g1' and 'g2'")));
	TestTrue(TEXT("a turret its generator does not list"), HasReason(Issues, TEXT("t6"), TEXT("generator 'g1' does not list this turret")));
	TestTrue(TEXT("a turret without a power line"), HasReason(Issues, TEXT("t5"), TEXT("no power line")));
	TestFalse(TEXT("a linked turret without its own field is fine"), HasReason(Issues, TEXT("t3"), TEXT("")));
	TestFalse(TEXT("nothing is ever dropped"), Issues.ContainsByPredicate([](const FPropsValidationIssue& I) { return I.bDropped; }));

	// The fairness rule with a spawn point: a running hostile network keeps 60 m away.
	FLevelPropsResolvedObject Hostile = Make(TEXT("power"), TEXT("g9"), TEXT("generator_diesel"), FIntPoint(5000, 0));
	SetString(Hostile, TEXT("start"), TEXT("running_hostile"));
	const FIntPoint Spawn(0, 0);
	TestTrue(TEXT("hostile within 60 m of the spawn"),
		HasReason(FPropsValidator::ValidatePowerLinks({ Hostile }, Kinds(), Balance.Get(), &Spawn), TEXT("g9"), TEXT("running hostile network within 6000 cm")));
	const FIntPoint FarSpawn(20000, 20000);
	TestEqual(TEXT("far from the spawn: fine"), FPropsValidator::ValidatePowerLinks({ Hostile }, Kinds(), Balance.Get(), &FarSpawn).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPropsPowerMapsTest, "CodexTactics.LevelGen.Props.Power.GeneratedMaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPropsPowerMapsTest::RunTest(const FString&)
{
	using namespace PropsPowerTestLocal;
	const TSharedPtr<FJsonObject> Balance = LoadPowerBalance(*this);
	for (const TCHAR* Map : { TEXT("L_Gen_Depot_X4"), TEXT("L_Gen_Depot_1_X6"), TEXT("L_Gen_Test_X2") })
	{
		FString Json;
		FLevelPropsFile File;
		TArray<FString> Errors;
		const FString Path = FPaths::ProjectContentDir() / TEXT("Data/LevelGen/props") / (FString(Map) + TEXT(".props.json"));
		if (!TestTrue(FString::Printf(TEXT("%s parses"), Map), FFileHelper::LoadFileToString(Json, *Path) && LevelPropsFile::Parse(Json, File, Errors)))
		{
			continue;
		}
		TMap<FString, FString> KindOfLayer;
		for (const FLevelPropsLayer& Layer : File.Layers)
		{
			KindOfLayer.Add(Layer.Id, Layer.Kind);
		}
		const TArray<FLevelPropsResolvedObject> Objects = LevelPropsFile::ApplyOverrides(File).Objects;
		const TArray<FPropsValidationIssue> Issues = FPropsValidator::ValidatePowerLinks(Objects, KindOfLayer, Balance.Get());
		for (const FPropsValidationIssue& Issue : Issues)
		{
			AddError(FString::Printf(TEXT("%s: %s: %s"), Map, *Issue.ObjectId, *Issue.Reason));
		}
		int32 Generators = 0;
		int32 Running = 0;
		int32 Turrets = 0;
		int32 LiveTurrets = 0;
		int32 FuelPiles = 0;
		for (const FLevelPropsResolvedObject& R : Objects)
		{
			const FString Kind = KindOfLayer.FindRef(R.LayerId);
			if (PropsGameplayRules::IsGeneratorObject(Kind, R.Object))
			{
				++Generators;
				const PropsGameplayRules::FGeneratorInput Input = PropsGameplayRules::GeneratorInputOf(R.Object);
				const bool bRunning = Input.Start == EGeneratorStart::RunningHostile || Input.Start == EGeneratorStart::RunningNeutral;
				Running += bRunning ? 1 : 0;
				LiveTurrets += bRunning ? Input.Links.Num() : 0;
				TestTrue(FString::Printf(TEXT("%s %s feeds 1-4 turrets"), Map, *R.Object.Id), Input.Links.Num() >= 1 && Input.Links.Num() <= 4);
			}
			else if (Kind == TEXT("turrets"))
			{
				++Turrets;
			}
			else if (Kind == TEXT("loot") && R.Object.LootTable == TEXT("fuel_rare"))
			{
				++FuelPiles;
				TestTrue(FString::Printf(TEXT("%s %s: a loose pile"), Map, *R.Object.Id), R.Object.Attach.IsEmpty());
			}
		}
		TestTrue(FString::Printf(TEXT("%s has generators"), Map), Generators >= 1);
		TestTrue(FString::Printf(TEXT("%s: about three quarters of the turrets dark (%d of %d live)"), Map, LiveTurrets, Turrets),
			Turrets > 0 && LiveTurrets * 2 <= Turrets);
		TestTrue(FString::Printf(TEXT("%s: a running generator at most every other one (%d of %d)"), Map, Running, Generators), Running * 2 <= Generators);
		TestEqual(FString::Printf(TEXT("%s: jerrycans for ~40 %% of the generators"), Map), FuelPiles, FMath::Max(1, FMath::RoundToInt(0.4 * Generators)));
	}
	// A fuel pile holds exactly one jerrycan; a crate of the table one jerrycan per roll.
	const TMap<FString, int32> Pile = PropsGameplayRules::RollLoot(Balance.Get(), TEXT("fuel_rare"), TEXT("loot_00000001_3"), true);
	TestTrue(TEXT("fuel pile = one Diesel"), Pile.Num() == 1 && Pile.FindRef(TEXT("Diesel")) == 1);
	const TMap<FString, int32> Crate = PropsGameplayRules::RollLoot(Balance.Get(), TEXT("fuel_rare"), TEXT("crate_1"), false);
	TestTrue(TEXT("fuel crate = DieselCans"), Crate.FindRef(TEXT("DieselCans")) >= 1);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
