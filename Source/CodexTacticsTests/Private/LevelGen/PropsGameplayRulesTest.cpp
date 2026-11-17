// CodexTactics.LevelGen.Props.Gameplay.* - G003b (designer 2026-11-17: the generator is tested with all assets): which real game object a
// levelgen prop becomes on a live map (props_balance.json "gameplay"), the class paths resolve to the game's actor classes, the mine reveal
// range, and the deterministic loot of a crate / a pile. Pure, no world. UE-only (no Godot reference).

#include "LevelGen/PropsGameplayRules.h"
#include "Misc/AutomationTest.h"

#include "Dom/JsonObject.h"
#include "Interactables/BarrelActor.h"
#include "Interactables/DroppedItemActor.h"
#include "Interactables/LootCrateActor.h"
#include "Interactables/LootRules.h"
#include "Interactables/ProximityMineActor.h"
#include "Interactables/TripwireActor.h"
#include "Interactables/TurretActor.h"
#include "LevelGen/LevelPropsTypes.h"
#include "Misc/FileHelper.h"
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

#endif // WITH_DEV_AUTOMATION_TESTS
