#include "Misc/AutomationTest.h"
#include "Combat/EnemyRegistryRules.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

// Audit 2026-10-11 #2 / P2-P3: the per-frame enemy queries read UEnemyRegistrySubsystem instead of scanning the world.
// UE-only (Godot iterated the "enemies" group); these nets pin the registry's bookkeeping and keep the world scans out
// of the converted hot paths.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyRegistryOrderTest, "CodexTactics.Combat.EnemyRegistry.OrderedRegistry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyRegistryOrderTest::RunTest(const FString&)
{
	EnemyRegistryRules::TOrderedRegistry<int32> Registry;
	TestTrue(TEXT("first add"), Registry.Add(3));
	TestTrue(TEXT("second add"), Registry.Add(1));
	TestTrue(TEXT("third add"), Registry.Add(2));
	TestFalse(TEXT("a second registration is refused"), Registry.Add(3));
	TestEqual(TEXT("three registered"), Registry.Num(), 3);
	TestTrue(TEXT("registration order kept"), Registry.GetItems() == TArray<int32>({ 3, 1, 2 }));

	TestTrue(TEXT("remove the middle one"), Registry.Remove(1));
	TestFalse(TEXT("removing twice does nothing"), Registry.Remove(1));
	TestTrue(TEXT("the rest keep their order"), Registry.GetItems() == TArray<int32>({ 3, 2 }));
	TestTrue(TEXT("re-registered after leaving: appended"), Registry.Add(1));
	TestTrue(TEXT("appended at the end"), Registry.GetItems() == TArray<int32>({ 3, 2, 1 }));

	TestEqual(TEXT("prune drops the rejected"), Registry.RemoveAllBut([](int32 Item) { return Item != 2; }), 1);
	TestTrue(TEXT("prune keeps the order"), Registry.GetItems() == TArray<int32>({ 3, 1 }));
	TestTrue(TEXT("contains a kept one"), Registry.Contains(3));
	TestFalse(TEXT("not the pruned one"), Registry.Contains(2));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyRegistryNoWorldTest, "CodexTactics.Combat.EnemyRegistry.NoWorld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyRegistryNoWorldTest::RunTest(const FString&)
{
	TestFalse(TEXT("no world: not serving"), UEnemyRegistrySubsystem::IsServing(nullptr));
	TestEqual(TEXT("no world: no tagged enemies"), UEnemyRegistrySubsystem::GetTaggedEnemies(nullptr).Num(), 0);
	TestEqual(TEXT("no world: no enemy characters"), UEnemyRegistrySubsystem::GetEnemyCharacters(nullptr).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyRegistryHotPathsTest, "CodexTactics.Combat.EnemyRegistry.HotPathsUseRegistry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyRegistryHotPathsTest::RunTest(const FString&)
{
	const FString Private = FPaths::ConvertRelativePathToFull(FPaths::GameSourceDir() / TEXT("CodexTactics/Private"));
	struct FRule
	{
		const TCHAR* File;
		const TCHAR* Forbidden;
	};
	// Per-frame / per-tick enemy queries: no scan of every actor (TActorIterator<AActor>) or every enemy class instance,
	// no FName built per call for the tag (CombatQueries::EnemyTag).
	const FRule Rules[] = {
		{ TEXT("Characters/OperativeCharacterCombat.cpp"), TEXT("TActorIterator<AActor>") },
		{ TEXT("Characters/OperativeCharacterCombat.cpp"), TEXT("TActorIterator<AOperativeCharacter>") },
		{ TEXT("Characters/OperativeCharacterWeapons.cpp"), TEXT("TActorIterator<AActor>") },
		{ TEXT("Characters/PanicComponent.cpp"), TEXT("TActorIterator<AActor>") },
		{ TEXT("Characters/RageComponent.cpp"), TEXT("TActorIterator<AActor>") },
		{ TEXT("Combat/FirePuddleActor.cpp"), TEXT("TActorIterator<AActor>") },
		{ TEXT("Combat/EncounterQueries.cpp"), TEXT("TActorIterator<AActor>") },
		{ TEXT("Interactables/BarricadeActor.cpp"), TEXT("TActorIterator<AActor>") },
		{ TEXT("Interactables/LootCrateActor.cpp"), TEXT("TActorIterator<AActor>") },
		{ TEXT("Interactables/ProximityMineActor.cpp"), TEXT("TActorIterator<AActor>") },
		{ TEXT("Interactables/TurretActor.cpp"), TEXT("TActorIterator<AActor>") },
		{ TEXT("Interactables/InteractableActor.cpp"), TEXT("TActorIterator<AActor>") },
		{ TEXT("UI/CodexTacticsHUD.cpp"), TEXT("TActorIterator<AEnemyCharacter>") },
		{ TEXT("Combat/TacticalSightSubsystem.cpp"), TEXT("TActorIterator<AEnemyCharacter>") },
		{ TEXT("Characters/EnemyTacticsSubsystem.cpp"), TEXT("TActorIterator<AEnemyCharacter>") },
		{ TEXT("Characters/SquadAutonomySubsystem.cpp"), TEXT("TActorIterator<AEnemyCharacter>") },
		{ TEXT("Characters/EnemyCharacter.cpp"), TEXT("TActorIterator<AEnemyCharacter>") },
		{ TEXT("Characters/MarksmanEnemyCharacter.cpp"), TEXT("TActorIterator<AEnemyCharacter>") },
		{ TEXT("Characters/OperativeCharacterOverlays.cpp"), TEXT("TActorIterator<AEnemyCharacter>") },
		{ TEXT("Characters/OperativeCharacterCornerAim.cpp"), TEXT("TActorIterator<AEnemyCharacter>") },
		{ TEXT("Characters/OperativeCharacterCover.cpp"), TEXT("TActorIterator<AEnemyCharacter>") },
		{ TEXT("Vehicles/VehicleTurretComponent.cpp"), TEXT("TActorIterator<AEnemyCharacter>") },
		{ TEXT("Vehicles/VehiclePawn.cpp"), TEXT("TActorIterator<AEnemyCharacter>") },
		{ TEXT("Vehicles/VehicleSubsystem.cpp"), TEXT("TActorIterator<AEnemyCharacter>") },
		{ TEXT("Combat/HordeIntroSubsystem.cpp"), TEXT("TActorIterator<AEnemyCharacter>") },
		{ TEXT("Interactables/BarricadeActor.cpp"), TEXT("FName(TEXT(\"Enemy\"))") },
		{ TEXT("Interactables/LootCrateActor.cpp"), TEXT("FName(TEXT(\"Enemy\"))") },
		{ TEXT("Interactables/ProximityMineActor.cpp"), TEXT("FName(TEXT(\"Enemy\"))") },
		{ TEXT("Interactables/TurretActor.cpp"), TEXT("FName(TEXT(\"Enemy\"))") },
		{ TEXT("Characters/OperativeCharacterInternal.h"), TEXT("FName(TEXT(\"Enemy\"))") },
	};
	for (const FRule& Rule : Rules)
	{
		FString Text;
		const FString Path = Private / Rule.File;
		if (!TestTrue(FString::Printf(TEXT("%s readable"), Rule.File), FFileHelper::LoadFileToString(Text, *Path)))
		{
			continue;
		}
		TestFalse(FString::Printf(TEXT("%s has no %s"), Rule.File, Rule.Forbidden), Text.Contains(Rule.Forbidden, ESearchCase::CaseSensitive));
	}
	// The enemy character registers / leaves.
	FString Enemy;
	if (TestTrue(TEXT("EnemyCharacter.cpp readable"), FFileHelper::LoadFileToString(Enemy, *(Private / TEXT("Characters/EnemyCharacter.cpp")))))
	{
		TestTrue(TEXT("BeginPlay registers"), Enemy.Contains(TEXT("RegisterEnemy(this)")));
		TestTrue(TEXT("EndPlay leaves"), Enemy.Contains(TEXT("UnregisterEnemy(this)")));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
