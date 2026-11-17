// CodexTactics.LevelGen.Props.Gameplay.* - G003b (designer 2026-11-17: the generator is tested with all assets): which real game object a
// levelgen prop becomes on a live map (props_balance.json "gameplay"), the class paths resolve to the game's actor classes, the mine reveal
// range, and the deterministic loot of a crate / a pile. Pure, no world. UE-only (no Godot reference).

#include "LevelGen/PropsGameplayRules.h"
#include "Misc/AutomationTest.h"

#include "Algo/Reverse.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Interactables/BarrelActor.h"
#include "Interactables/DroppedItemActor.h"
#include "Interactables/LootCrateActor.h"
#include "Interactables/LootRules.h"
#include "Interactables/ProximityMineActor.h"
#include "Interactables/TripwireActor.h"
#include "Interactables/TurretActor.h"
#include "LevelGen/LevelPropsTypes.h"
#include "Misc/FileHelper.h"
#include "Power/PowerGeneratorActor.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PropsGameplayRulesTestLocal
{
	TSharedPtr<FJsonObject> LoadGameplayBalance(FAutomationTestBase& Test)
	{
		FString Text;
		TSharedPtr<FJsonObject> Root;
		Test.TestTrue(TEXT("props_balance.json readable"),
			FFileHelper::LoadFileToString(Text, *(FPaths::ProjectContentDir() / TEXT("Data/Balance/props_balance.json"))));
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root);
		Test.TestTrue(TEXT("props_balance.json parses"), Root.IsValid());
		return Root;
	}

	FLevelPropsObject MakeObject(const TCHAR* Variant, const TCHAR* Trigger = TEXT(""))
	{
		FLevelPropsObject Obj;
		Obj.Id = TEXT("test_0");
		Obj.Variant = Variant;
		Obj.Trigger = Trigger;
		return Obj;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPropsGameplayClassKeyTest, "CodexTactics.LevelGen.Props.Gameplay.ClassKeys",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPropsGameplayClassKeyTest::RunTest(const FString&)
{
	using namespace PropsGameplayRulesTestLocal;
	TestEqual(TEXT("a plain crate keeps its variant"), PropsGameplayRules::ClassKeyFor(TEXT("crates"), MakeObject(TEXT("crate_s")), false), FString(TEXT("crate_s")));
	TestEqual(TEXT("a crate with loot / a crate-mine = crate_container"), PropsGameplayRules::ClassKeyFor(TEXT("crates"), MakeObject(TEXT("crate_l")), true),
		FString(TEXT("crate_container")));
	TestEqual(TEXT("a doorway mine by its trigger"), PropsGameplayRules::ClassKeyFor(TEXT("mines"), MakeObject(TEXT("mine_tripwire"), TEXT("tripwire")), false),
		FString(TEXT("mine_tripwire")));
	TestEqual(TEXT("a hand mine (variant 'mine') with a tripwire trigger"), PropsGameplayRules::ClassKeyFor(TEXT("mines"), MakeObject(TEXT("mine"), TEXT("tripwire")), false),
		FString(TEXT("mine_tripwire")));
	TestEqual(TEXT("a field mine by its trigger"), PropsGameplayRules::ClassKeyFor(TEXT("mines"), MakeObject(TEXT("mine_proximity"), TEXT("proximity")), false),
		FString(TEXT("mine_proximity")));
	TestEqual(TEXT("a barrel keeps its variant"), PropsGameplayRules::ClassKeyFor(TEXT("barrels"), MakeObject(TEXT("barrel_explosive")), false),
		FString(TEXT("barrel_explosive")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPropsGameplayClassesTest, "CodexTactics.LevelGen.Props.Gameplay.ClassesAreTheGameActors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPropsGameplayClassesTest::RunTest(const FString&)
{
	using namespace PropsGameplayRulesTestLocal;
	const TSharedPtr<FJsonObject> B = LoadGameplayBalance(*this);
	struct FCase
	{
		const TCHAR* Key;
		UClass* Expected;
	};
	const FCase Cases[] = {
		{ TEXT("barrel_explosive"), ABarrelActor::StaticClass() },
		{ TEXT("barrel"), ABarrelActor::StaticClass() },
		{ TEXT("crate_container"), ALootCrateActor::StaticClass() },
		{ TEXT("turret_mg"), ATurretActor::StaticClass() },
		{ TEXT("mine_proximity"), AProximityMineActor::StaticClass() },
		{ TEXT("mine_tripwire"), APMCTripwireActor::StaticClass() },
		{ TEXT("loot_pile"), ADroppedItemActor::StaticClass() },
		{ TEXT("generator_diesel"), APowerGeneratorActor::StaticClass() },
	};
	for (const FCase& Case : Cases)
	{
		const FString Path = PropsGameplayRules::ClassPathFor(B.Get(), Case.Key);
		UClass* Loaded = FSoftClassPath(Path).TryLoadClass<AActor>();
		TestTrue(FString::Printf(TEXT("%s -> %s loads as %s"), Case.Key, *Path, *Case.Expected->GetName()), Loaded == Case.Expected);
	}
	TestTrue(TEXT("plain crates have no gameplay class (greybox cover)"), PropsGameplayRules::ClassPathFor(B.Get(), TEXT("crate_s")).IsEmpty());
	TestTrue(TEXT("comment keys are not classes"), PropsGameplayRules::ClassPathFor(B.Get(), TEXT("barrel_comment")).IsEmpty());
	TestEqual(TEXT("reveal range 3 m (designer #4)"), PropsGameplayRules::RevealCm(B.Get()), 300.f);
	TestEqual(TEXT("spawn frame budget"), PropsGameplayRules::SpawnFrameBudgetMs(B.Get()), 20.0);
	TestEqual(TEXT("no balance: reveal default"), PropsGameplayRules::RevealCm(nullptr), 300.f);
	TestTrue(TEXT("no balance: no classes"), PropsGameplayRules::ClassPathFor(nullptr, TEXT("barrel")).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPropsGameplayLootTest, "CodexTactics.LevelGen.Props.Gameplay.LootRollsDeterministic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPropsGameplayLootTest::RunTest(const FString&)
{
	using namespace PropsGameplayRulesTestLocal;
	const TSharedPtr<FJsonObject> B = LoadGameplayBalance(*this);
	const TMap<FString, int32> A = PropsGameplayRules::RollLoot(B.Get(), TEXT("ammo_common"), TEXT("loot_00000001_3"), false);
	const TMap<FString, int32> A2 = PropsGameplayRules::RollLoot(B.Get(), TEXT("ammo_common"), TEXT("loot_00000001_3"), false);
	TestTrue(TEXT("same object id -> same loot"), A.OrderIndependentCompareEqual(A2));
	// ammo_common draws ammo_pack (RifleAmmo 30) or ammo_box (RifleAmmo 60 + ...): two crate rolls give 60..120 rifle rounds
	const int32 Rifle = A.FindRef(TEXT("RifleAmmo"));
	TestTrue(FString::Printf(TEXT("two ammo rolls: %d rifle rounds"), Rifle), Rifle >= 60 && Rifle <= 120);

	bool bAnyGrenadeOnPile = false;
	bool bAnyPileLoot = true;
	for (int32 Index = 0; Index < 64; ++Index)
	{
		const TMap<FString, int32> Pile = PropsGameplayRules::RollLoot(B.Get(), TEXT("rare_roof"), FString::Printf(TEXT("loot_00000001_%d"), Index), true);
		bAnyGrenadeOnPile |= Pile.Contains(TEXT("Grenades")) || Pile.Contains(TEXT("Grenade"));
		bAnyPileLoot &= !Pile.IsEmpty();
	}
	TestFalse(TEXT("a pile never draws an item it cannot hold (grenade)"), bAnyGrenadeOnPile);
	TestTrue(TEXT("every rare_roof pile holds something"), bAnyPileLoot);
	TestTrue(TEXT("unknown table: nothing"), PropsGameplayRules::RollLoot(B.Get(), TEXT("no_such_table"), TEXT("x"), false).IsEmpty());

	FLootContents Contents;
	TMap<FString, int32> Fields;
	Fields.Add(TEXT("RifleAmmo"), 30);
	Fields.Add(TEXT("Medkits"), 2);
	Fields.Add(TEXT("Bandages"), 1);
	const TArray<FString> Unknown = PropsGameplayRules::ApplyToContents(Fields, Contents);
	TestEqual(TEXT("rifle rounds set"), Contents.RifleAmmo, 30);
	TestEqual(TEXT("medkits set"), Contents.Medkits, 2);
	TestEqual(TEXT("the Godot defaults are cleared (pistol ammo 24 -> 0)"), Contents.PistolAmmo, 0);
	TestEqual(TEXT("the default food is cleared"), Contents.CannedFood, 0);
	TestEqual(TEXT("an unknown field is reported"), Unknown, TArray<FString>{ TEXT("Bandages") });
	return true;
}

namespace PropsGameplayRulesTestLocal
{
	TSharedPtr<FJsonObject> ParseJson(const TCHAR* Text)
	{
		TSharedPtr<FJsonObject> Root;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(Text)), Root);
		return Root;
	}

	TArray<PropsGameplayRules::FTurretInput> Turrets(int32 Count)
	{
		TArray<PropsGameplayRules::FTurretInput> Out;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Out.AddDefaulted_GetRef().Id = FString::Printf(TEXT("turrets_0000abcd_%d"), Index);
		}
		return Out;
	}

	int32 CountPowered(const TMap<FString, PropsGameplayRules::FTurretSetup>& Setups)
	{
		int32 Count = 0;
		for (const TPair<FString, PropsGameplayRules::FTurretSetup>& Pair : Setups)
		{
			Count += Pair.Value.bPowered ? 1 : 0;
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPropsGameplayTurretFieldsTest, "CodexTactics.LevelGen.Props.Gameplay.TurretFields",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPropsGameplayTurretFieldsTest::RunTest(const FString&)
{
	using namespace PropsGameplayRulesTestLocal;
	using PropsGameplayRules::ETurretSide;
	ETurretSide Side = ETurretSide::Neutral;
	TestTrue(TEXT("'Hostile' parses (case-insensitive)"), PropsGameplayRules::ParseTurretSide(TEXT("Hostile"), Side) && Side == ETurretSide::Hostile);
	TestTrue(TEXT("'friendly' parses"), PropsGameplayRules::ParseTurretSide(TEXT("friendly"), Side) && Side == ETurretSide::Friendly);
	TestTrue(TEXT("'neutral' parses"), PropsGameplayRules::ParseTurretSide(TEXT("neutral"), Side) && Side == ETurretSide::Neutral);
	TestFalse(TEXT("'pmc' is not a side (Out unchanged)"), PropsGameplayRules::ParseTurretSide(TEXT("pmc"), Side));
	TestTrue(TEXT("Out unchanged after a bad value"), Side == ETurretSide::Neutral);

	FLevelPropsObject Obj = MakeObject(TEXT("turret_mg"));
	PropsGameplayRules::FTurretInput Input = PropsGameplayRules::TurretInputOf(Obj);
	TestEqual(TEXT("the input keeps the id"), Input.Id, Obj.Id);
	TestFalse(TEXT("no fields: faction unset"), Input.Side.IsSet());
	TestFalse(TEXT("no fields: powered unset"), Input.bPowered.IsSet());

	Obj.Extra.Add(TEXT("faction"), MakeShared<FJsonValueString>(TEXT("friendly")));
	Obj.Extra.Add(TEXT("powered"), MakeShared<FJsonValueBoolean>(true));
	Input = PropsGameplayRules::TurretInputOf(Obj);
	TestTrue(TEXT("faction read"), Input.Side.IsSet() && Input.Side.GetValue() == ETurretSide::Friendly);
	TestTrue(TEXT("powered read"), Input.bPowered.IsSet() && Input.bPowered.GetValue());

	Obj.Extra.Add(TEXT("faction"), MakeShared<FJsonValueString>(TEXT("pmc")));
	Obj.Extra.Add(TEXT("powered"), MakeShared<FJsonValueNumber>(1.0));
	Input = PropsGameplayRules::TurretInputOf(Obj);
	TestFalse(TEXT("a bad faction counts as unset"), Input.Side.IsSet());
	TestFalse(TEXT("a non-bool powered counts as unset"), Input.bPowered.IsSet());

	const TSharedPtr<FJsonObject> B = LoadGameplayBalance(*this);
	TestEqual(TEXT("props_balance.json: a quarter of the turrets powered (designer 2026-11-17)"),
		PropsGameplayRules::TurretPoweredShare(B.Get(), nullptr), 0.25);
	TestEqual(TEXT("no balance: default share 0.25"), PropsGameplayRules::TurretPoweredShare(nullptr, nullptr), 0.25);
	TMap<FString, TSharedPtr<FJsonValue>> FileExtra;
	FileExtra.Add(TEXT("turret_powered_share"), MakeShared<FJsonValueNumber>(0.5));
	TestEqual(TEXT("a props file's turret_powered_share wins"), PropsGameplayRules::TurretPoweredShare(B.Get(), &FileExtra), 0.5);
	FileExtra.Add(TEXT("turret_powered_share"), MakeShared<FJsonValueNumber>(3.0));
	TestEqual(TEXT("the share is clamped to 1"), PropsGameplayRules::TurretPoweredShare(B.Get(), &FileExtra), 1.0);
	FileExtra.Add(TEXT("turret_powered_share"), MakeShared<FJsonValueString>(TEXT("half")));
	TestEqual(TEXT("a non-number share is ignored"), PropsGameplayRules::TurretPoweredShare(B.Get(), &FileExtra), 0.25);

	TestTrue(TEXT("props_balance.json: level mines / wires are hostile"), PropsGameplayRules::AreTrapsHostile(B.Get()));
	TestTrue(TEXT("no balance: hostile"), PropsGameplayRules::AreTrapsHostile(nullptr));
	TestFalse(TEXT("traps_faction squad"), PropsGameplayRules::AreTrapsHostile(ParseJson(TEXT("{\"gameplay\":{\"traps_faction\":\"squad\"}}")).Get()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPropsGameplayTurretResolveTest, "CodexTactics.LevelGen.Props.Gameplay.TurretPowerShare",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPropsGameplayTurretResolveTest::RunTest(const FString&)
{
	using namespace PropsGameplayRulesTestLocal;
	using PropsGameplayRules::ETurretSide;
	const TSharedPtr<FJsonObject> B = LoadGameplayBalance(*this);

	// 8 turrets at 0.25: 2 powered + hostile, 6 inactive (hostile by default).
	const TArray<PropsGameplayRules::FTurretInput> Eight = Turrets(8);
	const TMap<FString, PropsGameplayRules::FTurretSetup> Setups = PropsGameplayRules::ResolveTurrets(B.Get(), 0.25, Eight);
	TestEqual(TEXT("every turret has a setup"), Setups.Num(), 8);
	TestEqual(TEXT("a quarter powered"), CountPowered(Setups), 2);
	for (const TPair<FString, PropsGameplayRules::FTurretSetup>& Pair : Setups)
	{
		TestTrue(FString::Printf(TEXT("%s hostile (powered %d)"), *Pair.Key, Pair.Value.bPowered), Pair.Value.Side == ETurretSide::Hostile);
	}

	// The pick depends on the ids only, not on the order.
	TArray<PropsGameplayRules::FTurretInput> Reversed = Eight;
	Algo::Reverse(Reversed);
	const TMap<FString, PropsGameplayRules::FTurretSetup> Again = PropsGameplayRules::ResolveTurrets(B.Get(), 0.25, Reversed);
	bool bSame = Again.Num() == Setups.Num();
	for (const TPair<FString, PropsGameplayRules::FTurretSetup>& Pair : Setups)
	{
		const PropsGameplayRules::FTurretSetup* Other = Again.Find(Pair.Key);
		bSame &= Other && Other->bPowered == Pair.Value.bPowered && Other->Side == Pair.Value.Side;
	}
	TestTrue(TEXT("the same turrets powered whatever the input order"), bSame);

	TestEqual(TEXT("share 0: none powered"), CountPowered(PropsGameplayRules::ResolveTurrets(B.Get(), 0.0, Eight)), 0);
	TestEqual(TEXT("share 1: all powered"), CountPowered(PropsGameplayRules::ResolveTurrets(B.Get(), 1.0, Eight)), 8);
	TestEqual(TEXT("1 turret at 0.25 rounds to none"), CountPowered(PropsGameplayRules::ResolveTurrets(B.Get(), 0.25, Turrets(1))), 0);
	TestEqual(TEXT("no turrets: empty"), PropsGameplayRules::ResolveTurrets(B.Get(), 0.25, {}).Num(), 0);

	// Explicit fields win and count toward the share.
	TArray<PropsGameplayRules::FTurretInput> Mixed = Turrets(8);
	Mixed[0].bPowered = true;
	Mixed[0].Side = ETurretSide::Friendly;
	Mixed[1].bPowered = false;
	Mixed[2].Side = ETurretSide::Neutral;
	const TMap<FString, PropsGameplayRules::FTurretSetup> MixedSetups = PropsGameplayRules::ResolveTurrets(B.Get(), 0.25, Mixed);
	TestTrue(TEXT("an explicitly powered friendly turret stays so"), MixedSetups[Mixed[0].Id].bPowered && MixedSetups[Mixed[0].Id].Side == ETurretSide::Friendly);
	TestFalse(TEXT("an explicitly unpowered turret stays off"), MixedSetups[Mixed[1].Id].bPowered);
	TestTrue(TEXT("an explicit faction wins"), MixedSetups[Mixed[2].Id].Side == ETurretSide::Neutral);
	TestEqual(TEXT("the explicit powered turret counts toward the 2 of 8"), CountPowered(MixedSetups), 2);

	// Config sides.
	const TSharedPtr<FJsonObject> Custom = ParseJson(TEXT("{\"gameplay\":{\"turrets\":{\"powered_faction\":\"neutral\",\"default_faction\":\"friendly\"}}}"));
	const TMap<FString, PropsGameplayRules::FTurretSetup> CustomSetups = PropsGameplayRules::ResolveTurrets(Custom.Get(), 0.5, Turrets(4));
	int32 PoweredNeutral = 0;
	int32 OffFriendly = 0;
	for (const TPair<FString, PropsGameplayRules::FTurretSetup>& Pair : CustomSetups)
	{
		PoweredNeutral += Pair.Value.bPowered && Pair.Value.Side == ETurretSide::Neutral ? 1 : 0;
		OffFriendly += !Pair.Value.bPowered && Pair.Value.Side == ETurretSide::Friendly ? 1 : 0;
	}
	TestEqual(TEXT("powered_faction from the config"), PoweredNeutral, 2);
	TestEqual(TEXT("default_faction from the config"), OffFriendly, 2);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
