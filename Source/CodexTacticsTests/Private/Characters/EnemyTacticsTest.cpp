// CodexTactics.Characters.EnemyTactics.* — pack tactics of the melee enemies (EnemyTacticsRules, UE-only).

#include "Characters/EnemyTacticsRules.h"
#include "Misc/AutomationTest.h"

namespace
{
	FEnemyTacticsTarget MakeTarget(const FVector& Location, float Health = 1.f, float MateDistance = 200.f, bool bCover = false,
		const FVector& Forward = FVector(-1.f, 0.f, 0.f))
	{
		FEnemyTacticsTarget Target;
		Target.Location = Location;
		Target.HealthFraction = Health;
		Target.NearestMateDistance = MateDistance;
		Target.bInCover = bCover;
		Target.Forward = Forward;
		return Target;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyTacticsTargetChoiceTest, "CodexTactics.Characters.EnemyTactics.TargetChoice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyTacticsTargetChoiceTest::RunTest(const FString& Parameters)
{
	const FVector Enemy(0.f, 0.f, 0.f);
	const FEnemyTacticsProfile Hound = EnemyTacticsRules::ProfileFor(EEnemyArchetype::FrostHound);
	const FEnemyTacticsProfile Horde = EnemyTacticsRules::ProfileFor(EEnemyArchetype::Frostbitten);

	// A healthy operative at 10 m vs a badly wounded one at 13 m: the hound takes the wounded, the horde the nearest.
	TArray<FEnemyTacticsTarget> Targets = { MakeTarget(FVector(1000.f, 0.f, 0.f)), MakeTarget(FVector(1300.f, 0.f, 0.f), 0.1f) };
	TestEqual(TEXT("hound -> wounded one"), EnemyTacticsRules::ChooseTarget(Hound, Enemy, Targets), 1);
	TestEqual(TEXT("frostbitten -> nearest"), EnemyTacticsRules::ChooseTarget(Horde, Enemy, Targets), 0);

	// A straggler 9 m from his mates is preferred by the hound over an equally near one in the group.
	Targets = { MakeTarget(FVector(1000.f, 0.f, 0.f)), MakeTarget(FVector(0.f, 1100.f, 0.f), 1.f, 900.f) };
	TestEqual(TEXT("hound -> straggler"), EnemyTacticsRules::ChooseTarget(Hound, Enemy, Targets), 1);

	// Crowded: three hounds on the nearest already -> the next goes for the other operative (surround).
	Targets = { MakeTarget(FVector(1000.f, 0.f, 0.f)), MakeTarget(FVector(0.f, 1300.f, 0.f)) };
	Targets[0].Attackers = 3;
	TestEqual(TEXT("cap reached -> the other operative"), EnemyTacticsRules::ChooseTarget(Hound, Enemy, Targets), 1);

	// Sticky: the current target at 11 m beats a fresh one at 10 m.
	{
		TArray<FEnemyTacticsTarget> Two = { MakeTarget(FVector(1000.f, 0.f, 0.f)), MakeTarget(FVector(-1100.f, 0.f, 0.f), 1.f, 200.f, false, FVector(1.f, 0.f, 0.f)) };
		Two[1].bCurrent = true;
		TestEqual(TEXT("keeps its current target"), EnemyTacticsRules::ChooseTarget(Horde, Enemy, Two), 1);
	}

	// Unusable targets (dead end / fire) are skipped; none usable -> none.
	Targets[1].bUsable = false;
	TestEqual(TEXT("only the crowded one left -> still him"), EnemyTacticsRules::ChooseTarget(Hound, Enemy, Targets), 0);
	Targets[0].bUsable = false;
	TestEqual(TEXT("nothing usable"), EnemyTacticsRules::ChooseTarget(Hound, Enemy, Targets), static_cast<int32>(INDEX_NONE));

	// Cutters like backs: an operative facing away at 11 m beats one facing them at 10 m.
	const FEnemyTacticsProfile Cutter = EnemyTacticsRules::ProfileFor(EEnemyArchetype::Cutter);
	Targets = { MakeTarget(FVector(1000.f, 0.f, 0.f)), MakeTarget(FVector(0.f, 1100.f, 0.f), 1.f, 200.f, false, FVector(0.f, 1.f, 0.f)) };
	TestEqual(TEXT("cutter -> turned back"), EnemyTacticsRules::ChooseTarget(Cutter, Enemy, Targets), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyTacticsRolesTest, "CodexTactics.Characters.EnemyTactics.RolesAndFlank",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyTacticsRolesTest::RunTest(const FString& Parameters)
{
	const FEnemyTacticsProfile Hound = EnemyTacticsRules::ProfileFor(EEnemyArchetype::FrostHound);
	int32 Flankers = 0;
	for (int32 Rank = 0; Rank < 4; ++Rank)
	{
		Flankers += EnemyTacticsRules::RoleFor(Hound, Rank, 4) == EEnemyTacticRole::Flank ? 1 : 0;
	}
	TestEqual(TEXT("4 hounds on one target: 2 go round"), Flankers, 2);
	TestTrue(TEXT("the closest always pins"), EnemyTacticsRules::RoleFor(Hound, 0, 4) == EEnemyTacticRole::Direct);
	TestTrue(TEXT("a lone attacker goes straight"), EnemyTacticsRules::RoleFor(Hound, 0, 1) == EEnemyTacticRole::Direct);
	TestTrue(TEXT("brutes never flank"), EnemyTacticsRules::RoleFor(EnemyTacticsRules::ProfileFor(EEnemyArchetype::Brute), 1, 3)
		== EEnemyTacticRole::Direct);

	// AssignRoles: 4 new hounds -> 2 flank (the farthest); returning roles kept; the frostbitten always pin.
	{
		TArray<FEnemyTacticsProfile> Four = { Hound, Hound, Hound, Hound };
		TArray<const EEnemyTacticRole*> None = { nullptr, nullptr, nullptr, nullptr };
		TArray<EEnemyTacticRole> Roles = EnemyTacticsRules::AssignRoles(Four, None);
		TestTrue(TEXT("new pack: the two farthest go round"), Roles[2] == EEnemyTacticRole::Flank && Roles[3] == EEnemyTacticRole::Flank
			&& Roles[0] == EEnemyTacticRole::Direct && Roles[1] == EEnemyTacticRole::Direct);
		const EEnemyTacticRole Flank = EEnemyTacticRole::Flank;
		const EEnemyTacticRole Direct = EEnemyTacticRole::Direct;
		// The flankers came closest meanwhile (ranks 0 and 1): they keep flanking, nobody reshuffles.
		TArray<const EEnemyTacticRole*> Was = { &Flank, &Flank, &Direct, &Direct };
		Roles = EnemyTacticsRules::AssignRoles(Four, Was);
		TestTrue(TEXT("returning roles kept"), Roles[0] == EEnemyTacticRole::Flank && Roles[1] == EEnemyTacticRole::Flank
			&& Roles[2] == EEnemyTacticRole::Direct && Roles[3] == EEnemyTacticRole::Direct);
		const FEnemyTacticsProfile Horde = EnemyTacticsRules::ProfileFor(EEnemyArchetype::Frostbitten);
		TArray<FEnemyTacticsProfile> Mixed = { Horde, Horde, Hound };
		Roles = EnemyTacticsRules::AssignRoles(Mixed, { nullptr, nullptr, nullptr });
		TestTrue(TEXT("frostbitten pin, the hound may not reach a full share"), Roles[0] == EEnemyTacticRole::Direct && Roles[1] == EEnemyTacticRole::Direct);
	}

	// Squad centre at the origin, target 5 m north: the flank point lies beyond / beside him, on the enemy's side.
	const FVector Target(0.f, 500.f, 0.f);
	const FVector EastEnemy(1500.f, 0.f, 0.f);
	const FVector Point = EnemyTacticsRules::FlankPoint(EastEnemy, Target, FVector::ZeroVector);
	TestTrue(TEXT("flank point is farther from the squad than the target"), Point.Size2D() > Target.Size2D());
	TestTrue(TEXT("flank point on the enemy's (east) side"), Point.X > 100.f);
	TestTrue(TEXT("450 cm from the target"), FMath::IsNearlyEqual(static_cast<float>(FVector::Dist2D(Point, Target)), 450.f, 1.f));

	TestTrue(TEXT("behind: target faces north, enemy south"), EnemyTacticsRules::IsBehind(FVector::ZeroVector, FVector(0.f, 1.f, 0.f), FVector(0.f, -300.f, 0.f)));
	TestFalse(TEXT("in front"), EnemyTacticsRules::IsBehind(FVector::ZeroVector, FVector(0.f, 1.f, 0.f), FVector(50.f, 300.f, 0.f)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyTacticsMoraleTest, "CodexTactics.Characters.EnemyTactics.Morale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyTacticsMoraleTest::RunTest(const FString& Parameters)
{
	const FEnemyTacticsProfile Hound = EnemyTacticsRules::ProfileFor(EEnemyArchetype::FrostHound);
	TestTrue(TEXT("hound at 20 % falls back"), EnemyTacticsRules::ShouldFallBack(Hound, 0.2f, 0));
	TestFalse(TEXT("hound at 60 % holds"), EnemyTacticsRules::ShouldFallBack(Hound, 0.6f, 0));
	TestTrue(TEXT("three pack mates died near him"), EnemyTacticsRules::ShouldFallBack(Hound, 0.9f, 3));
	TestFalse(TEXT("frostbitten never break"), EnemyTacticsRules::ShouldFallBack(EnemyTacticsRules::ProfileFor(EEnemyArchetype::Frostbitten), 0.05f, 9));
	TestFalse(TEXT("brutes never break"), EnemyTacticsRules::ShouldFallBack(EnemyTacticsRules::ProfileFor(EEnemyArchetype::Brute), 0.05f, 9));

	// Away from the squad, bent towards the pack when that still leads away.
	const FVector Enemy(1000.f, 0.f, 0.f);
	const FVector Pack(1500.f, 800.f, 0.f);
	const FVector Point = EnemyTacticsRules::FallBackPoint(Enemy, FVector::ZeroVector, &Pack);
	TestTrue(TEXT("farther from the squad"), Point.Size2D() > Enemy.Size2D() + 500.f);
	TestTrue(TEXT("bent towards the pack"), Point.Y > 100.f);
	return true;
}
