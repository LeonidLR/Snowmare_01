// CodexTactics.Characters.SquadAutonomy.* — Commander Mode rules (Sprint 07): leash, stance, flank, ammo, targets, aid, ROE file.

#include "Characters/SquadAutonomyRules.h"
#include "Data/SquadROE.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSquadAutonomyLeashTest, "CodexTactics.Characters.SquadAutonomy.Leash",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSquadAutonomyLeashTest::RunTest(const FString& Parameters)
{
	using namespace SquadAutonomyRules;
	FSquadROE ROE;
	TestEqual(TEXT("7 m anchor"), LeashRadius(ROE, false), 700.f);
	TestEqual(TEXT("flexible aid stretches to 10 m"), LeashRadius(ROE, true), 1000.f);
	ROE.LeashStrictness = ELeashStrictness::Strict;
	TestEqual(TEXT("strict aid keeps 7 m"), LeashRadius(ROE, true), 700.f);

	const FTacticalAnchor Anchor = MakeAnchor(ROE, FVector::ZeroVector, FVector(1000.f, 0.f, 0.f), FRotator::ZeroRotator);
	TestTrue(TEXT("active"), Anchor.bIsActive);
	TestEqual(TEXT("radius from the ROE"), Anchor.Radius, 700.f);
	TestEqual(TEXT("guards the walk direction"), static_cast<float>(Anchor.GuardFacing.Yaw), 0.f, 0.1f);
	TestTrue(TEXT("6 m inside"), IsInsideLeash(Anchor, FVector(1600.f, 0.f, 0.f), 700.f));
	TestFalse(TEXT("8 m outside"), IsInsideLeash(Anchor, FVector(1800.f, 0.f, 0.f), 700.f));
	const FVector Clamped = ClampToLeash(Anchor, FVector(1000.f, 2000.f, 50.f), 700.f);
	TestEqual(TEXT("clamped onto the circle"), static_cast<float>(FVector::Dist2D(Clamped, Anchor.Location)), 700.f, 1.f);
	TestEqual(TEXT("clamp keeps Z"), Clamped.Z, 50.0);
	TestTrue(TEXT("inactive anchor never leashes"), IsInsideLeash(FTacticalAnchor(), FVector(99999.f, 0.f, 0.f), 700.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSquadAutonomyStanceTest, "CodexTactics.Characters.SquadAutonomy.Stance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSquadAutonomyStanceTest::RunTest(const FString& Parameters)
{
	using namespace SquadAutonomyRules;
	FSquadROE ROE;
	TestEqual(TEXT("cover: crouch"), DesiredStance(ROE, true, false, true), EOperativeStance::Crouching);
	TestEqual(TEXT("open ground: crouch"), DesiredStance(ROE, false, false, true), EOperativeStance::Crouching);
	TestEqual(TEXT("sniper, cover reachable: crouch (moving into it)"), DesiredStance(ROE, false, true, true), EOperativeStance::Crouching);
	TestEqual(TEXT("sniper, no cover: prone"), DesiredStance(ROE, false, true, false), EOperativeStance::Prone);
	ROE.SniperReaction = ESniperReaction::DropProne;
	TestEqual(TEXT("drop prone even in cover"), DesiredStance(ROE, true, true, true), EOperativeStance::Prone);
	ROE.OpenGroundStance = EOpenGroundStance::Standing;
	ROE.CoverStance = ECoverStance::Standing;
	TestEqual(TEXT("open ground standing"), DesiredStance(ROE, false, false, false), EOperativeStance::Standing);
	TestEqual(TEXT("cover standing"), DesiredStance(ROE, true, false, false), EOperativeStance::Standing);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSquadAutonomyFlankAmmoTest, "CodexTactics.Characters.SquadAutonomy.FlankAndAmmo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSquadAutonomyFlankAmmoTest::RunTest(const FString& Parameters)
{
	using namespace SquadAutonomyRules;
	FSquadROE ROE;
	const FVector Forward(1.f, 0.f, 0.f);
	TestFalse(TEXT("ahead"), IsFlankThreat(Forward, FVector::ZeroVector, FVector(1000.f, 200.f, 0.f), 75.f));
	TestFalse(TEXT("70 deg"), IsFlankThreat(Forward, FVector::ZeroVector, FVector(342.f, 940.f, 0.f), 75.f));
	TestTrue(TEXT("90 deg"), IsFlankThreat(Forward, FVector::ZeroVector, FVector(0.f, 1000.f, 0.f), 75.f));
	TestTrue(TEXT("behind"), IsFlankThreat(Forward, FVector::ZeroVector, FVector(-1000.f, 0.f, 0.f), 75.f));

	TestTrue(TEXT("7/30 in cover"), ShouldReload(ROE, 7, 30, 60, false, true, 300.f));
	TestFalse(TEXT("7/30 in the open, enemy at 3 m: keep firing"), ShouldReload(ROE, 7, 30, 60, false, false, 300.f));
	TestTrue(TEXT("7/30 in the open, nobody close"), ShouldReload(ROE, 7, 30, 60, false, false, 2000.f));
	TestFalse(TEXT("10/30 above 25 %"), ShouldReload(ROE, 10, 30, 60, false, true, 2000.f));
	TestTrue(TEXT("empty clip: reload anywhere"), ShouldReload(ROE, 0, 30, 60, false, false, 100.f));
	TestFalse(TEXT("no reserve"), ShouldReload(ROE, 0, 30, 0, false, true, 2000.f));
	TestFalse(TEXT("already reloading"), ShouldReload(ROE, 0, 30, 60, true, true, 2000.f));

	TestTrue(TEXT("empty rifle, hound at 2 m: pistol"), ShouldSwitchToSidearm(ROE, 200.f, 0, false, 12));
	TestTrue(TEXT("reloading rifle, hound at 2 m: pistol"), ShouldSwitchToSidearm(ROE, 200.f, 5, true, 12));
	TestFalse(TEXT("rifle loaded"), ShouldSwitchToSidearm(ROE, 200.f, 10, false, 12));
	TestFalse(TEXT("enemy at 5 m"), ShouldSwitchToSidearm(ROE, 500.f, 0, false, 12));
	TestFalse(TEXT("empty pistol"), ShouldSwitchToSidearm(ROE, 200.f, 0, false, 0));
	TestFalse(TEXT("back to rifle: still 5 m"), ShouldSwitchBackToPrimary(ROE, 500.f, 30));
	TestTrue(TEXT("back to rifle at 8 m"), ShouldSwitchBackToPrimary(ROE, 800.f, 30));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSquadAutonomyTargetTest, "CodexTactics.Characters.SquadAutonomy.Targets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSquadAutonomyTargetTest::RunTest(const FString& Parameters)
{
	using namespace SquadAutonomyRules;
	FSquadROE ROE;
	auto Make = [](EEnemyArchetype Archetype, float Distance, float Health = 1.f, bool bLeader = false)
	{
		FAutonomyTargetCandidate Candidate;
		Candidate.Archetype = Archetype;
		Candidate.DistanceCm = Distance;
		Candidate.HealthFraction = Health;
		Candidate.bLeaderTarget = bLeader;
		return Candidate;
	};
	TArray<FAutonomyTargetCandidate> Candidates = {
		Make(EEnemyArchetype::Frostbitten, 600.f, 0.2f), Make(EEnemyArchetype::FrostHound, 900.f, 1.f, true), Make(EEnemyArchetype::Marksman, 2500.f) };
	TestEqual(TEXT("threat: marksman first"), ChooseTarget(ROE, Candidates), 2);
	ROE.TargetPriorityPolicy = ETargetPriorityPolicy::ClosestFirst;
	TestEqual(TEXT("closest"), ChooseTarget(ROE, Candidates), 0);
	ROE.TargetPriorityPolicy = ETargetPriorityPolicy::LowestHP;
	TestEqual(TEXT("lowest HP"), ChooseTarget(ROE, Candidates), 0);
	ROE.TargetPriorityPolicy = ETargetPriorityPolicy::AssistLeader;
	TestEqual(TEXT("the leader's target"), ChooseTarget(ROE, Candidates), 1);

	ROE.TargetPriorityPolicy = ETargetPriorityPolicy::ThreatLevel;
	Candidates.Add(Make(EEnemyArchetype::Brute, 250.f));
	TestEqual(TEXT("point-blank brute beats the far marksman"), ChooseTarget(ROE, Candidates), 3);
	Candidates[3].bCanHit = false;
	Candidates[2].bCanHit = false;
	TestEqual(TEXT("only hittable ones: the hound (tier 2)"), ChooseTarget(ROE, Candidates), 1);
	for (FAutonomyTargetCandidate& Candidate : Candidates)
	{
		Candidate.bCanHit = false;
	}
	TestEqual(TEXT("none hittable"), ChooseTarget(ROE, Candidates), INDEX_NONE);

	// Stickiness: a current target 2 m further is kept.
	ROE.TargetPriorityPolicy = ETargetPriorityPolicy::ClosestFirst;
	TArray<FAutonomyTargetCandidate> Pair = { Make(EEnemyArchetype::Base, 1000.f), Make(EEnemyArchetype::Base, 1200.f) };
	Pair[1].bCurrent = true;
	TestEqual(TEXT("keeps the current target"), ChooseTarget(ROE, Pair), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSquadAutonomyAidTest, "CodexTactics.Characters.SquadAutonomy.Aid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSquadAutonomyAidTest::RunTest(const FString& Parameters)
{
	using namespace SquadAutonomyRules;
	FSquadROE ROE;
	TestTrue(TEXT("20 % needs aid"), NeedsAid(ROE, 0.2f, false));
	TestFalse(TEXT("30 % does not"), NeedsAid(ROE, 0.3f, false));
	TestTrue(TEXT("downed"), NeedsAid(ROE, 0.6f, true));
	TestFalse(TEXT("dead"), NeedsAid(ROE, 0.f, false));

	TestTrue(TEXT("healthy rescuer, one medkit"), CanGiveAid(ROE, 0.9f, 1));
	TestFalse(TEXT("wounded rescuer keeps the last medkit"), CanGiveAid(ROE, 0.4f, 1));
	TestTrue(TEXT("wounded rescuer with two"), CanGiveAid(ROE, 0.4f, 2));
	TestFalse(TEXT("no medkit"), CanGiveAid(ROE, 1.f, 0));
	ROE.bReservePersonalMedkit = false;
	TestTrue(TEXT("no reserve rule"), CanGiveAid(ROE, 0.4f, 1));

	TestTrue(TEXT("safe"), IsSafeAidRoute(ROE, false, 1000.f));
	TestFalse(TEXT("marksman aiming"), IsSafeAidRoute(ROE, true, 1000.f));
	TestFalse(TEXT("enemy 4 m from the patient"), IsSafeAidRoute(ROE, false, 400.f));
	ROE.bRequireSafeRouteForAid = false;
	TestTrue(TEXT("check off"), IsSafeAidRoute(ROE, true, 100.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSquadROEJsonTest, "CodexTactics.Data.SquadROE.JsonRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSquadROEJsonTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FJsonObject> Json;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TEXT("{\"anchor_radius_meters\": 9, \"leash_strictness\": \"Strict\", "
		"\"open_ground_stance\": \"prone\", \"sniper_reaction\": \"DropProne\", \"target_priority_policy\": \"LowestHP\", "
		"\"reserve_personal_medkit\": false, \"emergency_sidearm_dist_m\": 4.5, \"cover_stance\": \"Sideways\"}")), Json);
	if (!TestTrue(TEXT("json parsed"), Json.IsValid()))
	{
		return false;
	}
	const FSquadROE ROE = SquadROE::FromJson(*Json);
	TestEqual(TEXT("radius"), ROE.AnchorRadiusMeters, 9.f);
	TestTrue(TEXT("strict"), ROE.LeashStrictness == ELeashStrictness::Strict);
	TestTrue(TEXT("prone (any case)"), ROE.OpenGroundStance == EOpenGroundStance::Prone);
	TestTrue(TEXT("drop prone"), ROE.SniperReaction == ESniperReaction::DropProne);
	TestTrue(TEXT("lowest HP"), ROE.TargetPriorityPolicy == ETargetPriorityPolicy::LowestHP);
	TestFalse(TEXT("no reserve"), ROE.bReservePersonalMedkit);
	TestEqual(TEXT("sidearm 4.5 m"), ROE.EmergencySidearmDistMeters, 4.5f);
	TestTrue(TEXT("unknown enum keeps the default"), ROE.CoverStance == ECoverStance::Crouch);
	TestEqual(TEXT("missing key keeps the default"), ROE.FlankDefenseAngleDeg, 75.f);

	const FSquadROE Back = SquadROE::FromJson(*SquadROE::ToJson(ROE));
	TestEqual(TEXT("round trip radius"), Back.AnchorRadiusMeters, ROE.AnchorRadiusMeters);
	TestTrue(TEXT("round trip policy"), Back.TargetPriorityPolicy == ROE.TargetPriorityPolicy);
	TestTrue(TEXT("round trip stance"), Back.OpenGroundStance == ROE.OpenGroundStance);
	return true;
}
