#include "Misc/AutomationTest.h"
#include "Characters/EnemyCharacter.h"
#include "AI/PerceptionRules.h"
#include "Combat/HealthComponent.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UI/OverheadLabel.h"

#if WITH_DEV_AUTOMATION_TESTS

// Cleanup 2026-10-19 (docs/port/REFACTOR_ENEMY_2026-10.md): AEnemyCharacter's members now live in one translation unit per
// cluster (Private/Characters/EnemyCharacter*.cpp). These nets call world-free members of several units on one enemy and
// check the units' source hygiene (every member defined once, the class header first, the two EnemyCharacterLocal objects in
// their units), so a definition lost or duplicated by a future move shows up as a test, not only as a smoke. UE-only, no
// Godot reference.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyUnitsWorldFreeTest, "CodexTactics.Characters.EnemyUnits.WorldFreeMembers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyUnitsWorldFreeTest::RunTest(const FString&)
{
	AEnemyCharacter* Hound = NewObject<AEnemyCharacter>();
	if (!TestNotNull(TEXT("enemy"), Hound))
	{
		return false;
	}

	// EnemyCharacterArchetype.cpp: the spawn scope nests and restores; the archetype defaults (SetMaxHealth resets the health).
	{
		const AEnemyCharacter::FScopedSpawnArchetype Outer(EEnemyArchetype::Brute);
		const AEnemyCharacter::FScopedSpawnArchetype Inner(EEnemyArchetype::FrostHound);
	}
	Hound->InitializeArchetype(EEnemyArchetype::FrostHound);
	TestTrue(TEXT("archetype set"), Hound->GetArchetype() == EEnemyArchetype::FrostHound);
	TestEqual(TEXT("hound attack range 180 cm"), Hound->GetAttackRange(), 180.f);
	TestEqual(TEXT("hound max health 45"), Hound->GetHealthComponent()->GetMaxHealth(), 45.f);

	// EnemyCharacter.cpp: the overhead label of a living hound.
	FOverheadLabel Label;
	if (TestTrue(TEXT("overhead label shown"), Hound->GetOverheadLabel(Label)))
	{
		TestTrue(TEXT("label names the hound"), Label.Text.StartsWith(TEXT("Frost Hound")));
		TestTrue(TEXT("label shows its health"), Label.Text.Contains(TEXT("45/45")));
		TestEqual(TEXT("hound label height"), Label.HeightCm, 115.f);
	}

	// EnemyCharacterHealth.cpp: not knocked down; the trap blast scope (TrapBlastDepth) counts up and back.
	TestFalse(TEXT("not knocked down"), Hound->IsKnockedDown());
	TestFalse(TEXT("no trap blast"), AEnemyCharacter::IsTrapBlastInProgress());
	{
		const AEnemyCharacter::FScopedTrapBlast Blast;
		TestTrue(TEXT("trap blast in progress"), AEnemyCharacter::IsTrapBlastInProgress());
	}
	TestFalse(TEXT("trap blast over"), AEnemyCharacter::IsTrapBlastInProgress());

	// EnemyCharacterBehavior.cpp: no world, no squad -> no target; its melee close-in CVar is registered.
	TestNull(TEXT("no target without a world"), Hound->FindTarget());
	TestNotNull(TEXT("Codex.Enemy.MeleeCloseIn registered"), IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Enemy.MeleeCloseIn")));

	// EnemyCharacterPerception.cpp: the vision cone follows the (unscaled, not searching) patrol perception.
	const FEnemyPerceptionParams Perception = Hound->GetPerception();
	TestEqual(TEXT("cone half angle = perception half angle"), Hound->GetVisionConeHalfAngle(), Perception.SightHalfAngleDeg);
	TestEqual(TEXT("cone range = standing sight range"), Hound->GetVisionConeRange(),
		PerceptionRules::EffectiveSightRange(Perception, EOperativeStance::Standing));

	// EnemyCharacterPatrol.cpp (+ EnemyCharacterScent.cpp ResetScent): no route, no leader, no post -> not on patrol.
	Hound->StartPatrol(nullptr, nullptr);
	TestFalse(TEXT("not on patrol without a route"), Hound->IsOnPatrol());
	TestFalse(TEXT("no scent"), Hound->IsOnScent());

	// EnemyCharacterSearch.cpp: alive, off patrol, no target / alarm -> not hunting the squad.
	TestFalse(TEXT("not hunting the squad"), Hound->IsHuntingSquad());
	TestFalse(TEXT("not searching"), Hound->IsSearching());

	// EnemyCharacterSave.cpp: the snapshot of an enemy off patrol.
	const FEnemyPatrolSnapshot Snapshot = Hound->CapturePatrolSnapshot();
	TestFalse(TEXT("snapshot off patrol"), Snapshot.bOnPatrol);
	TestFalse(TEXT("snapshot not searching"), Snapshot.bSearching);
	TestFalse(TEXT("snapshot no lure search"), Snapshot.bLureSearch);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyUnitsSourceLayoutTest, "CodexTactics.Characters.EnemyUnits.SourceLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyUnitsSourceLayoutTest::RunTest(const FString&)
{
	static const TCHAR* Units[] = {
		TEXT("EnemyCharacter.cpp"), TEXT("EnemyCharacterArchetype.cpp"), TEXT("EnemyCharacterBehavior.cpp"),
		TEXT("EnemyCharacterCombat.cpp"), TEXT("EnemyCharacterHealth.cpp"), TEXT("EnemyCharacterPatrol.cpp"),
		TEXT("EnemyCharacterSave.cpp"), TEXT("EnemyCharacterPerception.cpp"), TEXT("EnemyCharacterScent.cpp"),
		TEXT("EnemyCharacterSearch.cpp") };
	const FString CharactersDir = FPaths::ConvertRelativePathToFull(FPaths::GameSourceDir() / TEXT("CodexTactics/Private/Characters"));
	TArray<FString> Found;
	IFileManager::Get().FindFiles(Found, *(CharactersDir / TEXT("EnemyCharacter*.cpp")), true, false);
	if (!TestTrue(FString::Printf(TEXT("enemy units found in %s"), *CharactersDir), Found.Num() >= static_cast<int32>(UE_ARRAY_COUNT(Units))))
	{
		return false;
	}
	for (const TCHAR* Unit : Units)
	{
		TestTrue(FString::Printf(TEXT("%s exists (layout: docs/port/REFACTOR_ENEMY_2026-10.md)"), Unit), Found.Contains(FString(Unit)));
	}

	const FString ClassScope = TEXT("AEnemyCharacter::");
	TMap<FString, FString> DefinedIn; // member -> unit
	int32 Members = 0;
	int32 SpawnArchetypeDefinitions = 0;
	int32 MeleeCloseInDefinitions = 0;
	for (const FString& Unit : Found)
	{
		FString Text;
		if (!TestTrue(FString::Printf(TEXT("%s readable"), *Unit), FFileHelper::LoadFileToString(Text, *(CharactersDir / Unit))))
		{
			continue;
		}
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, false);

		// The class header first (IWYU, one class over several units).
		const FString* FirstInclude = Lines.FindByPredicate([](const FString& Line) { return Line.StartsWith(TEXT("#include")); });
		TestTrue(FString::Printf(TEXT("%s includes its class header first"), *Unit),
			FirstInclude && *FirstInclude == TEXT("#include \"Characters/EnemyCharacter.h\""));

		// Unity builds merge the units: file-local state stays in the named namespace, one definition per object.
		TestFalse(FString::Printf(TEXT("%s has no anonymous namespace (use a file-unique named one)"), *Unit),
			Lines.ContainsByPredicate([](const FString& Line) { return Line.TrimEnd() == TEXT("namespace"); }));
		if (Text.Contains(TEXT("TOptional<EEnemyArchetype> SpawnArchetype;")))
		{
			++SpawnArchetypeDefinitions;
			TestEqual(TEXT("EnemyCharacterLocal::SpawnArchetype in the archetype unit"), Unit, FString(TEXT("EnemyCharacterArchetype.cpp")));
		}
		if (Text.Contains(TEXT("TEXT(\"Codex.Enemy.MeleeCloseIn\")")))
		{
			++MeleeCloseInDefinitions;
			TestEqual(TEXT("Codex.Enemy.MeleeCloseIn defined in the behavior unit"), Unit, FString(TEXT("EnemyCharacterBehavior.cpp")));
		}

		// Member definitions start at column 0 ("Type AEnemyCharacter::Name(").
		for (const FString& Line : Lines)
		{
			if (Line.IsEmpty() || FChar::IsWhitespace(Line[0]) || Line[0] == TEXT('#') || Line[0] == TEXT('/') || Line[0] == TEXT('}'))
			{
				continue;
			}
			const int32 Scope = Line.Find(ClassScope);
			const int32 Paren = Scope == INDEX_NONE ? INDEX_NONE : Line.Find(TEXT("("), ESearchCase::CaseSensitive, ESearchDir::FromStart, Scope);
			if (Paren == INDEX_NONE)
			{
				continue;
			}
			const FString Member = Line.Mid(Scope + ClassScope.Len(), Paren - Scope - ClassScope.Len());
			++Members;
			if (const FString* Previous = DefinedIn.Find(Member))
			{
				AddError(FString::Printf(TEXT("%s defined twice: %s and %s"), *Member, **Previous, *Unit));
				continue;
			}
			DefinedIn.Add(Member, Unit);
		}
	}
	TestEqual(TEXT("EnemyCharacterLocal::SpawnArchetype defined by exactly one unit"), SpawnArchetypeDefinitions, 1);
	TestEqual(TEXT("Codex.Enemy.MeleeCloseIn registered by exactly one unit"), MeleeCloseInDefinitions, 1);
	TestTrue(FString::Printf(TEXT("member definitions found (%d)"), Members), Members >= 87);
	TestEqual(TEXT("TickBehavior in the behavior unit"), DefinedIn.FindRef(TEXT("TickBehavior")), FString(TEXT("EnemyCharacterBehavior.cpp")));
	TestEqual(TEXT("Tick in the main unit"), DefinedIn.FindRef(TEXT("Tick")), FString(TEXT("EnemyCharacter.cpp")));
	TestEqual(TEXT("TickPatrolSearch in the search unit"), DefinedIn.FindRef(TEXT("TickPatrolSearch")), FString(TEXT("EnemyCharacterSearch.cpp")));
	TestEqual(TEXT("RestorePatrolSnapshot in the save unit"), DefinedIn.FindRef(TEXT("RestorePatrolSnapshot")), FString(TEXT("EnemyCharacterSave.cpp")));
	return true;
}

#endif
