// CodexTactics.LevelGen.Props.Subsystem.* - level authoring stage 1, task C6 (docs/design/levelgen/PROPS_LAYERS.md 3.1, 3.2, 5 C6):
// UCodexLevelPropsSubsystem + actor classes / class map + tags + nav modifiers + zero tickers.
// UE-only (no Godot reference).

#include "LevelGen/CodexLevelPropsSubsystem.h"
#include "Misc/AutomationTest.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "LevelGen/CodexPropActors.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NavAreas/NavArea_Null.h"
#include "NavModifierComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

#define PROPS_SUBSYSTEM_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.LevelGen.Props.Subsystem." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace LevelPropsSubsystemTestLocal
{
	const TCHAR* const GSubsystemTestFixtures = TEXT("Data/LevelGen/props/fixtures");
	const TCHAR* const GSubsystemSmallCrates = TEXT("golden_small_crates.json");
	const TCHAR* const GSubsystemSmallMines = TEXT("golden_small_mines.json");
	const TCHAR* const GSubsystemSmallLayout = TEXT("fixture_small.layout.json");

	FString SubsystemPropsFixturePath(const FString& Relative)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / GSubsystemTestFixtures / Relative);
	}

	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FWorldContext* Context = nullptr;

		FScopedTestWorld(const FString& WorldName)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, *WorldName);
			if (World && GEngine)
			{
				Context = &GEngine->CreateNewWorldContext(EWorldType::Game);
				Context->SetCurrentWorld(World);
				World->InitializeActorsForPlay(FURL());
			}
		}

		~FScopedTestWorld()
		{
			if (World && GEngine)
			{
				if (UCodexLevelPropsSubsystem* Sub = World->GetSubsystem<UCodexLevelPropsSubsystem>())
				{
					Sub->ClearSpawnedProps();
				}
				if (Context)
				{
					GEngine->DestroyWorldContext(World);
				}
				World->DestroyWorld(false);
			}
		}

		bool IsValid() const { return World != nullptr; }
	};
}

// 1. Spawning crates from golden_small_crates: verify counts, tags, 0 tickers, nav modifiers
PROPS_SUBSYSTEM_TEST(FPropsSubsystemSpawnCratesTest, "SpawnCrates")
bool FPropsSubsystemSpawnCratesTest::RunTest(const FString& Parameters)
{
	using namespace LevelPropsSubsystemTestLocal;

	FScopedTestWorld TestWorld(TEXT("PropsTestCratesWorld"));
	if (!TestTrue(TEXT("Test world created"), TestWorld.IsValid()))
	{
		return false;
	}

	UCodexLevelPropsSubsystem* Sub = TestWorld.World->GetSubsystem<UCodexLevelPropsSubsystem>();
	if (!TestNotNull(TEXT("UCodexLevelPropsSubsystem exists"), Sub))
	{
		return false;
	}

	const FString PropsPath = SubsystemPropsFixturePath(GSubsystemSmallCrates);
	const FString LayoutPath = SubsystemPropsFixturePath(GSubsystemSmallLayout);

	const int32 Spawned = Sub->SpawnPropsFromFile(PropsPath, LayoutPath);
	TestTrue(TEXT("Spawned props > 0"), Spawned > 0);
	TestEqual(TEXT("Subsystem count matches return value"), Sub->GetSpawnedCount(), Spawned);
	TestTrue(TEXT("Subsystem marked has spawned"), Sub->HasSpawned());

	const TArray<TWeakObjectPtr<ACodexPropActor>>& Props = Sub->GetSpawnedProps();
	for (const TWeakObjectPtr<ACodexPropActor>& PropPtr : Props)
	{
		ACodexPropActor* Prop = PropPtr.Get();
		if (!TestNotNull(TEXT("Prop actor valid"), Prop))
		{
			continue;
		}

		// Verify zero tickers (PROPS_LAYERS.md 3.1 & 3.3)
		TestFalse(TEXT("Prop does not tick"), Prop->PrimaryActorTick.bCanEverTick);

		// Verify tags (PROPS_LAYERS.md 3.2): CodexProp, prop_layer=<layer>, prop_id=<id>
		TestTrue(TEXT("Has CodexProp tag"), Prop->ActorHasTag(TEXT("CodexProp")));
		TestTrue(TEXT("Has prop_layer tag"), Prop->ActorHasTag(FName(*FString::Printf(TEXT("prop_layer=%s"), *Prop->LayerId))));
		TestTrue(TEXT("Has prop_id tag"), Prop->ActorHasTag(FName(*FString::Printf(TEXT("prop_id=%s"), *Prop->PropId))));

		// Crates must have nav modifiers (PROPS_LAYERS.md 2.4, 3.1)
		if (ACodexPropCrate* Crate = Cast<ACodexPropCrate>(Prop))
		{
			TestTrue(TEXT("Crate is nav affecting"), Crate->bNavAffecting);
			TestNotNull(TEXT("Crate has NavModifierComponent"), Crate->NavModifierComponent.Get());
			if (Crate->NavModifierComponent)
			{
				TestTrue(TEXT("Crate nav area is UNavArea_Null"),
					Crate->NavModifierComponent->AreaClass == UNavArea_Null::StaticClass());
			}
		}
	}

	// Verify layer counts (golden_small_crates has 8 crates, 1 barrel, 1 mine)
	TestEqual(TEXT("GetSpawnedCountForLayer('crates') matches 8"),
		Sub->GetSpawnedCountForLayer(TEXT("crates")), 8);
	TestEqual(TEXT("GetSpawnedCountForLayer('barrels') matches 1"),
		Sub->GetSpawnedCountForLayer(TEXT("barrels")), 1);
	TestEqual(TEXT("GetSpawnedCountForLayer('mines') matches 1"),
		Sub->GetSpawnedCountForLayer(TEXT("mines")), 1);
	TestEqual(TEXT("Total spawned props matches 10"), Sub->GetSpawnedCount(), 10);

	return true;
}

// 2. Spawning mines, turrets, and trapped crates: verify trap attachment, no separate mine_crate actor, turrets
PROPS_SUBSYSTEM_TEST(FPropsSubsystemSpawnMinesAndTrapsTest, "SpawnMinesAndTraps")
bool FPropsSubsystemSpawnMinesAndTrapsTest::RunTest(const FString& Parameters)
{
	using namespace LevelPropsSubsystemTestLocal;

	FScopedTestWorld TestWorld(TEXT("PropsTestMinesWorld"));
	if (!TestTrue(TEXT("Test world created"), TestWorld.IsValid()))
	{
		return false;
	}

	UCodexLevelPropsSubsystem* Sub = TestWorld.World->GetSubsystem<UCodexLevelPropsSubsystem>();
	if (!TestNotNull(TEXT("UCodexLevelPropsSubsystem exists"), Sub))
	{
		return false;
	}

	const FString PropsPath = SubsystemPropsFixturePath(GSubsystemSmallMines);
	const FString LayoutPath = SubsystemPropsFixturePath(GSubsystemSmallLayout);

	const int32 Spawned = Sub->SpawnPropsFromFile(PropsPath, LayoutPath);
	TestTrue(TEXT("Spawned props > 0"), Spawned > 0);

	bool bFoundTurret = false;
	bool bFoundTripwireMine = false;
	bool bFoundTrappedCrate = false;
	int32 SeparateMineCrateActors = 0;

	for (const TWeakObjectPtr<ACodexPropActor>& PropPtr : Sub->GetSpawnedProps())
	{
		ACodexPropActor* Prop = PropPtr.Get();
		if (!Prop)
		{
			continue;
		}

		// Zero tickers
		TestFalse(TEXT("No ticking props"), Prop->PrimaryActorTick.bCanEverTick);

		// mine_crate should NEVER spawn a separate actor (PROPS_LAYERS.md 3.2)
		if (Prop->Variant.Equals(TEXT("mine_crate"), ESearchCase::IgnoreCase))
		{
			SeparateMineCrateActors++;
		}

		if (ACodexPropCrate* Crate = Cast<ACodexPropCrate>(Prop))
		{
			if (Crate->TrapComponent != nullptr)
			{
				bFoundTrappedCrate = true;
				TestTrue(TEXT("Trap component is armed"), Crate->TrapComponent->bArmed);
				TestFalse(TEXT("Trap component is not inert by default"), Crate->TrapComponent->bInert);
				TestTrue(TEXT("Crate marked dangerous"), Crate->bDangerous);
			}
		}
		else if (ACodexPropTurret* Turret = Cast<ACodexPropTurret>(Prop))
		{
			bFoundTurret = true;
			TestTrue(TEXT("Turret has arc set"), Turret->ArcDeg > 0.f);
		}
		else if (ACodexPropMine* Mine = Cast<ACodexPropMine>(Prop))
		{
			// Mines never block navigation (PROPS_LAYERS.md 2.3)
			TestFalse(TEXT("Mine is not nav affecting"), Mine->bNavAffecting);
			TestNull(TEXT("Mine has no NavModifierComponent"), Mine->NavModifierComponent.Get());

			if (Mine->TriggerType.Equals(TEXT("tripwire"), ESearchCase::IgnoreCase))
			{
				bFoundTripwireMine = true;
				TestNotNull(TEXT("Tripwire mine has trigger shape"), Mine->TriggerShape.Get());
			}
		}
	}

	TestEqual(TEXT("Zero separate mine_crate actors spawned"), SeparateMineCrateActors, 0);
	TestTrue(TEXT("Found at least one trapped crate"), bFoundTrappedCrate);
	TestTrue(TEXT("Found at least one turret"), bFoundTurret);
	TestTrue(TEXT("Found at least one tripwire mine"), bFoundTripwireMine);

	return true;
}

// 3. Codex.Props.Enabled 0 spawns nothing (PROPS_LAYERS.md 3.1 & 5 C6)
PROPS_SUBSYSTEM_TEST(FPropsSubsystemCVarDisabledTest, "CVarDisabled")
bool FPropsSubsystemCVarDisabledTest::RunTest(const FString& Parameters)
{
	using namespace LevelPropsSubsystemTestLocal;

	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Props.Enabled"));
	if (!TestNotNull(TEXT("CVar Codex.Props.Enabled exists"), CVar))
	{
		return false;
	}

	const int32 OriginalValue = CVar->GetInt();
	CVar->Set(0, ECVF_SetByCode);

	FScopedTestWorld TestWorld(TEXT("PropsTestDisabledWorld"));
	UCodexLevelPropsSubsystem* Sub = TestWorld.World->GetSubsystem<UCodexLevelPropsSubsystem>();

	const FString PropsPath = SubsystemPropsFixturePath(GSubsystemSmallCrates);
	const int32 Spawned = Sub->SpawnPropsFromFile(PropsPath);

	TestEqual(TEXT("Spawning with Codex.Props.Enabled 0 returns 0"), Spawned, 0);
	TestEqual(TEXT("Subsystem spawned count is 0"), Sub->GetSpawnedCount(), 0);

	CVar->Set(OriginalValue, ECVF_SetByCode);
	return true;
}

// 4. Codex.Props.MinesInert 1 marks mines and trapped crates inert
PROPS_SUBSYSTEM_TEST(FPropsSubsystemMinesInertTest, "MinesInert")
bool FPropsSubsystemMinesInertTest::RunTest(const FString& Parameters)
{
	using namespace LevelPropsSubsystemTestLocal;

	IConsoleVariable* CVarInert = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Props.MinesInert"));
	if (!TestNotNull(TEXT("CVar Codex.Props.MinesInert exists"), CVarInert))
	{
		return false;
	}

	const int32 OrigInert = CVarInert->GetInt();
	CVarInert->Set(1, ECVF_SetByCode);

	FScopedTestWorld TestWorld(TEXT("PropsTestInertWorld"));
	UCodexLevelPropsSubsystem* Sub = TestWorld.World->GetSubsystem<UCodexLevelPropsSubsystem>();

	const FString PropsPath = SubsystemPropsFixturePath(GSubsystemSmallMines);
	const FString LayoutPath = SubsystemPropsFixturePath(GSubsystemSmallLayout);

	const int32 Spawned = Sub->SpawnPropsFromFile(PropsPath, LayoutPath);
	TestTrue(TEXT("Spawned props > 0"), Spawned > 0);

	bool bCheckedInertMine = false;
	bool bCheckedInertTrap = false;

	for (const TWeakObjectPtr<ACodexPropActor>& PropPtr : Sub->GetSpawnedProps())
	{
		ACodexPropActor* Prop = PropPtr.Get();
		if (!Prop)
		{
			continue;
		}

		if (ACodexPropMine* Mine = Cast<ACodexPropMine>(Prop))
		{
			TestTrue(TEXT("Mine marked bMinesInert"), Mine->bMinesInert);
			bCheckedInertMine = true;
		}
		else if (ACodexPropCrate* Crate = Cast<ACodexPropCrate>(Prop))
		{
			if (Crate->TrapComponent)
			{
				TestTrue(TEXT("Trap component marked bInert"), Crate->TrapComponent->bInert);
				// Triggering an inert trap should return false (no explosion)
				TestFalse(TEXT("Inert trap trigger does not detonate"), Crate->TrapComponent->TriggerTrap());
				bCheckedInertTrap = true;
			}
		}
	}

	TestTrue(TEXT("Verified inert mine"), bCheckedInertMine);
	TestTrue(TEXT("Verified inert trapped crate"), bCheckedInertTrap);

	CVarInert->Set(OrigInert, ECVF_SetByCode);
	return true;
}

// 5. Save state / skipped IDs are not spawned
PROPS_SUBSYSTEM_TEST(FPropsSubsystemSkippedIdsTest, "SkippedIds")
bool FPropsSubsystemSkippedIdsTest::RunTest(const FString& Parameters)
{
	using namespace LevelPropsSubsystemTestLocal;

	FScopedTestWorld TestWorld(TEXT("PropsTestSkipWorld"));
	UCodexLevelPropsSubsystem* Sub = TestWorld.World->GetSubsystem<UCodexLevelPropsSubsystem>();

	const FString PropsPath = SubsystemPropsFixturePath(GSubsystemSmallCrates);
	const FString LayoutPath = SubsystemPropsFixturePath(GSubsystemSmallLayout);

	// First spawn all to get total
	const int32 Total = Sub->SpawnPropsFromFile(PropsPath, LayoutPath);
	TestTrue(TEXT("Initial spawn > 0"), Total > 0);

	// Now skip two IDs
	TSet<FString> Skipped;
	Skipped.Add(TEXT("crates_00000015_0"));
	Skipped.Add(TEXT("crates_00000015_1"));

	const int32 AfterSkip = Sub->SpawnPropsFromFile(PropsPath, LayoutPath, &Skipped);
	TestEqual(TEXT("Count reduced by exactly 2"), AfterSkip, Total - 2);

	for (const TWeakObjectPtr<ACodexPropActor>& PropPtr : Sub->GetSpawnedProps())
	{
		if (ACodexPropActor* Prop = PropPtr.Get())
		{
			TestFalse(TEXT("Skipped ID crates_00000015_0 not spawned"), Prop->PropId == TEXT("crates_00000015_0"));
			TestFalse(TEXT("Skipped ID crates_00000015_1 not spawned"), Prop->PropId == TEXT("crates_00000015_1"));
		}
	}

	return true;
}

// 6. DumpProps executes cleanly and reports statistics
PROPS_SUBSYSTEM_TEST(FPropsSubsystemDumpPropsTest, "DumpProps")
bool FPropsSubsystemDumpPropsTest::RunTest(const FString& Parameters)
{
	using namespace LevelPropsSubsystemTestLocal;

	FScopedTestWorld TestWorld(TEXT("PropsTestDumpWorld"));
	UCodexLevelPropsSubsystem* Sub = TestWorld.World->GetSubsystem<UCodexLevelPropsSubsystem>();

	const FString PropsPath = SubsystemPropsFixturePath(GSubsystemSmallCrates);
	Sub->SpawnPropsFromFile(PropsPath, SubsystemPropsFixturePath(GSubsystemSmallLayout));

	// Dump props should log cleanly without error
	Sub->DumpProps();

	TestTrue(TEXT("Last spawn duration was measured"), Sub->GetLastSpawnDurationMs() >= 0.0);

	return true;
}

// 7. Depot spawn test: spawns from L_Gen_Depot_1.props.json on gen_ref_01_02_depot_1 layout, verifies counts and <= 60 ms budget
PROPS_SUBSYSTEM_TEST(FPropsSubsystemSpawnDepotTest, "SpawnDepot")
bool FPropsSubsystemSpawnDepotTest::RunTest(const FString& Parameters)
{
	using namespace LevelPropsSubsystemTestLocal;

	FScopedTestWorld TestWorld(TEXT("PropsTestDepotWorld"));
	UCodexLevelPropsSubsystem* Sub = TestWorld.World->GetSubsystem<UCodexLevelPropsSubsystem>();
	if (!TestNotNull(TEXT("UCodexLevelPropsSubsystem exists"), Sub))
	{
		return false;
	}

	const int32 Spawned = Sub->SpawnPropsForMap(TEXT("L_Gen_Depot_1"));
	TestTrue(TEXT("Depot spawned props > 0"), Spawned > 0);
	TestEqual(TEXT("Depot crates layer count matches 53"), Sub->GetSpawnedCountForLayer(TEXT("crates")), 53);
	TestEqual(TEXT("Depot barrels layer count matches 8"), Sub->GetSpawnedCountForLayer(TEXT("barrels")), 8);
	TestEqual(TEXT("Total Depot props spawned matches 61"), Spawned, 61);

	// Performance budget: <= 60 ms on Depot (PROPS_LAYERS.md 3.1 & 3.3)
	TestTrue(FString::Printf(TEXT("Depot spawn time %.2f ms <= 60 ms budget"), Sub->GetLastSpawnDurationMs()),
		Sub->GetLastSpawnDurationMs() <= 60.0);

	for (const TWeakObjectPtr<ACodexPropActor>& PropPtr : Sub->GetSpawnedProps())
	{
		if (ACodexPropActor* Prop = PropPtr.Get())
		{
			TestFalse(TEXT("Zero ticking prop"), Prop->PrimaryActorTick.bCanEverTick);
			TestTrue(TEXT("Has CodexProp tag"), Prop->ActorHasTag(TEXT("CodexProp")));
		}
	}

	return true;
}


// 6. A live spawn (X6 spawn hitch 2026-11-18): the file / layout / validation work runs on a worker thread, nothing is spawned in the call's
//    frame, and once the frame-sliced spawn ends the props are the same ones, at the same places, as a direct (all-at-once) spawn.
PROPS_SUBSYSTEM_TEST(FPropsSubsystemLiveSpawnOffThreadTest, "LiveSpawnOffThread")
bool FPropsSubsystemLiveSpawnOffThreadTest::RunTest(const FString& Parameters)
{
	using namespace LevelPropsSubsystemTestLocal;

	struct FLiveState
	{
		TUniquePtr<FScopedTestWorld> TestWorld;
		TMap<FString, FVector> DirectPlaces;
		TArray<FString> DirectDropped;
		int32 DirectCount = 0;
		double StartTime = 0.0;
		IConsoleVariable* RealCVar = nullptr;
		int32 OriginalReal = 1;
	};
	TSharedRef<FLiveState> State = MakeShared<FLiveState>();
	State->TestWorld = MakeUnique<FScopedTestWorld>(TEXT("PropsTestLiveWorld"));
	if (!TestTrue(TEXT("Test world created"), State->TestWorld->IsValid()))
	{
		return false;
	}
	UWorld* World = State->TestWorld->World;
	// The frame-slice ticker works only in a world that has begun play (no game mode here: the flag is set directly, no other subsystem starts).
	World->SetBegunPlay(true);
	UCodexLevelPropsSubsystem* Sub = World->GetSubsystem<UCodexLevelPropsSubsystem>();
	if (!TestNotNull(TEXT("UCodexLevelPropsSubsystem exists"), Sub) || !TestTrue(TEXT("world has begun play"), World->HasBegunPlay()))
	{
		return false;
	}
	// Greybox props for both runs (Codex.Props.Real 0): the live spawn takes the same spawn path as the direct one, only threaded + sliced.
	State->RealCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Props.Real"));
	if (!TestNotNull(TEXT("CVar Codex.Props.Real exists"), State->RealCVar))
	{
		return false;
	}
	State->OriginalReal = State->RealCVar->GetInt();
	State->RealCVar->Set(0, ECVF_SetByCode);

	const FString PropsPath = SubsystemPropsFixturePath(GSubsystemSmallMines);
	const FString LayoutPath = SubsystemPropsFixturePath(GSubsystemSmallLayout);
	const auto PlacesOf = [](const UCodexLevelPropsSubsystem& Props)
	{
		TMap<FString, FVector> Places;
		for (const TWeakObjectPtr<ACodexPropActor>& Prop : Props.GetSpawnedProps())
		{
			if (Prop.IsValid())
			{
				Places.Add(Prop->PropId, Prop->GetActorLocation());
			}
		}
		return Places;
	};

	State->DirectCount = Sub->SpawnPropsFromFile(PropsPath, LayoutPath);
	State->DirectPlaces = PlacesOf(*Sub);
	State->DirectDropped = Sub->GetDroppedIds();
	TestTrue(TEXT("the direct spawn placed props"), State->DirectCount > 0);

	const int32 AtCall = Sub->SpawnPropsFromFile(PropsPath, LayoutPath, nullptr, false, /*bLive=*/ true);
	TestEqual(TEXT("a live spawn places nothing in the call's frame (the prepare runs on a worker thread)"), AtCall, 0);
	TestTrue(TEXT("a live spawn is pending after the call"), Sub->IsSpawnPending());
	TestFalse(TEXT("a live spawn has not spawned yet"), Sub->HasSpawned());
	State->StartTime = FPlatformTime::Seconds();

	TWeakObjectPtr<UCodexLevelPropsSubsystem> WeakSub(Sub);
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, State, WeakSub, PlacesOf]() -> bool
	{
		UCodexLevelPropsSubsystem* Props = WeakSub.Get();
		const bool bTimedOut = FPlatformTime::Seconds() - State->StartTime > 20.0;
		if (Props && Props->IsSpawnPending() && !bTimedOut)
		{
			return false; // the engine ticks the frame-slice ticker between latent updates
		}
		if (TestNotNull(TEXT("subsystem still alive"), Props))
		{
			TestFalse(TEXT("the live spawn finished in time"), Props->IsSpawnPending());
			TestTrue(TEXT("the live spawn is marked as spawned"), Props->HasSpawned());
			TestEqual(TEXT("live spawn count = direct spawn count"), Props->GetSpawnedCount(), State->DirectCount);
			TestEqual(TEXT("live spawn dropped ids = direct spawn dropped ids"), FString::Join(Props->GetDroppedIds(), TEXT(",")), FString::Join(State->DirectDropped, TEXT(",")));
			TestTrue(TEXT("the worker-thread prepare was timed"), Props->GetLastPrepareMs() > 0.0);
			const TMap<FString, FVector> LivePlaces = PlacesOf(*Props);
			TestEqual(TEXT("live spawn prop ids = direct spawn prop ids"), LivePlaces.Num(), State->DirectPlaces.Num());
			for (const TPair<FString, FVector>& Pair : State->DirectPlaces)
			{
				const FVector* Live = LivePlaces.Find(Pair.Key);
				TestTrue(FString::Printf(TEXT("prop %s at the same place in the live spawn"), *Pair.Key), Live && Live->Equals(Pair.Value, 0.01));
			}
		}
		State->RealCVar->Set(State->OriginalReal, ECVF_SetByCode);
		State->TestWorld.Reset();
		return true;
	}));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
