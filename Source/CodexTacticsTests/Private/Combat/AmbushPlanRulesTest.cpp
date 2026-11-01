#include "Misc/AutomationTest.h"
#include "Combat/AmbushPlanRules.h"
#include "Combat/AmbushRelocateRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Coordinated multi-direction ambushes and the relocation of a bypassed ambush (game designer answers 2 and 3, 2026-11-01,
// docs/design/COMBAT_LAB.md).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAmbushPlanPartiesTest, "CodexTactics.Combat.AmbushPlan.Parties",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAmbushPlanPartiesTest::RunTest(const FString&)
{
	TestTrue(TEXT("Body 0: flank left"), AmbushPlanRules::PartyOf(0) == EAmbushParty::FlankLeft);
	TestTrue(TEXT("Body 1: flank right"), AmbushPlanRules::PartyOf(1) == EAmbushParty::FlankRight);
	TestTrue(TEXT("Body 2: front"), AmbushPlanRules::PartyOf(2) == EAmbushParty::Front);
	TestTrue(TEXT("Body 3: rear"), AmbushPlanRules::PartyOf(3) == EAmbushParty::Rear);
	TestTrue(TEXT("Body 4: front again"), AmbushPlanRules::PartyOf(4) == EAmbushParty::Front);
	TestTrue(TEXT("Body 5: rear again"), AmbushPlanRules::PartyOf(5) == EAmbushParty::Rear);
	TestTrue(TEXT("Spitter / Marksman are ranged"), AmbushPlanRules::IsRanged(EEnemyArchetype::Spitter) && AmbushPlanRules::IsRanged(EEnemyArchetype::Marksman));
	TestFalse(TEXT("Hound / Cutter / Frostbitten are not"), AmbushPlanRules::IsRanged(EEnemyArchetype::FrostHound) || AmbushPlanRules::IsRanged(EEnemyArchetype::Cutter)
		|| AmbushPlanRules::IsRanged(EEnemyArchetype::Frostbitten));
	// Bodies: 4..6 under the plans' own cap (6).
	FAmbushPlanParams Params;
	TestEqual(TEXT("Rand 0: 4 bodies"), AmbushPlanRules::PlanBodies(0.f, Params, 0), 4);
	TestEqual(TEXT("Rand 0.99: 6 bodies"), AmbushPlanRules::PlanBodies(0.99f, Params, 0), 6);
	TestEqual(TEXT("1 alive: cut to 5"), AmbushPlanRules::PlanBodies(0.99f, Params, 1), 5);
	TestEqual(TEXT("2 alive: still 4"), AmbushPlanRules::PlanBodies(0.99f, Params, 2), 4);
	TestEqual(TEXT("3 alive: no room for a plan"), AmbushPlanRules::PlanBodies(0.5f, Params, 3), 0);
	Params.BodiesMin = 2;
	TestEqual(TEXT("A plan never has fewer than 4 bodies"), AmbushPlanRules::PlanBodies(0.f, Params, 0), 4);
	// At least one ranged: the front's melee body is swapped first, the flanks stay melee.
	TArray<EEnemyArchetype> Melee = { EEnemyArchetype::FrostHound, EEnemyArchetype::Cutter, EEnemyArchetype::Frostbitten, EEnemyArchetype::FrostHound };
	TestEqual(TEXT("One swap"), AmbushPlanRules::EnsureRanged(Melee, EEnemyArchetype::Marksman, 1), 1);
	TestTrue(TEXT("... the front body is the marksman now"), Melee[2] == EEnemyArchetype::Marksman);
	TestTrue(TEXT("... the flanks keep their hound / cutter"), Melee[0] == EEnemyArchetype::FrostHound && Melee[1] == EEnemyArchetype::Cutter);
	TArray<EEnemyArchetype> Two = Melee;
	TestEqual(TEXT("Two ranged wanted: the rear's body next"), AmbushPlanRules::EnsureRanged(Two, EEnemyArchetype::Spitter, 2), 1);
	TestTrue(TEXT("... the rear is the spitter"), Two[3] == EEnemyArchetype::Spitter);
	TArray<EEnemyArchetype> Already = { EEnemyArchetype::FrostHound, EEnemyArchetype::Cutter, EEnemyArchetype::Spitter, EEnemyArchetype::FrostHound };
	TestEqual(TEXT("Already ranged: nothing swapped"), AmbushPlanRules::EnsureRanged(Already, EEnemyArchetype::Marksman, 1), 0);
	TestEqual(TEXT("A melee 'ranged' pick swaps nothing"), AmbushPlanRules::EnsureRanged(Melee, EEnemyArchetype::Cutter, 3), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAmbushPlanLayoutTest, "CodexTactics.Combat.AmbushPlan.Layout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAmbushPlanLayoutTest::RunTest(const FString&)
{
	FAmbushPlanParams Params;
	// The squad at the origin heading +X (yaw 0): the anchor 5 m ahead; UE yaw +90 = +Y = the squad's right.
	const FVector2D Anchor = AmbushPlanRules::Anchor(FVector2D::ZeroVector, 0.f, Params);
	TestEqual(TEXT("Anchor 5 m ahead"), Anchor.X, 500.0, 0.01);
	TestTrue(TEXT("Slot right = +Y"), AmbushPlanRules::SlotPoint(Anchor, 0.f, EAmbushParty::FlankRight, Params).Equals(FVector2D(500.0, 1500.0), 0.5));
	TestTrue(TEXT("Slot left = -Y"), AmbushPlanRules::SlotPoint(Anchor, 0.f, EAmbushParty::FlankLeft, Params).Equals(FVector2D(500.0, -1500.0), 0.5));
	TestTrue(TEXT("Slot front"), AmbushPlanRules::SlotPoint(Anchor, 0.f, EAmbushParty::Front, Params).Equals(FVector2D(2100.0, 0.0), 0.5));
	TestTrue(TEXT("Slot rear"), AmbushPlanRules::SlotPoint(Anchor, 0.f, EAmbushParty::Rear, Params).Equals(FVector2D(-1000.0, 0.0), 0.5));
	TestTrue(TEXT("Sector: ahead = front"), AmbushPlanRules::SectorOf(Anchor, 0.f, FVector2D(2000.0, 300.0)) == EAmbushParty::Front);
	TestTrue(TEXT("Sector: +Y = right flank"), AmbushPlanRules::SectorOf(Anchor, 0.f, FVector2D(600.0, 1200.0)) == EAmbushParty::FlankRight);
	TestTrue(TEXT("Sector: -Y = left flank"), AmbushPlanRules::SectorOf(Anchor, 0.f, FVector2D(600.0, -1200.0)) == EAmbushParty::FlankLeft);
	TestTrue(TEXT("Sector: behind = rear"), AmbushPlanRules::SectorOf(Anchor, 0.f, FVector2D(-900.0, 100.0)) == EAmbushParty::Rear);
	TestTrue(TEXT("Sector turns with the heading (yaw 90: +Y is front)"), AmbushPlanRules::SectorOf(FVector2D::ZeroVector, 90.f, FVector2D(0.0, 900.0)) == EAmbushParty::Front);
	// Assignment: one candidate per side, the nearest to each slot; a second candidate too close to a taken one is skipped.
	const TArray<FVector2D> Candidates = {
		FVector2D(2300.0, 200.0),    // 0 front
		FVector2D(700.0, 1700.0),    // 1 right
		FVector2D(400.0, -1300.0),   // 2 left
		FVector2D(-1100.0, -200.0),  // 3 rear
		FVector2D(600.0, -1500.0),   // 4 left, nearer the left slot than 2 -> wins the left
		FVector2D(2100.0, 3000.0) }; // 5 right sector (62 deg off the heading), farther from the right slot than 1
	const TArray<int32> Assigned = AmbushPlanRules::AssignSpots(Anchor, 0.f, Candidates, Params);
	TestTrue(TEXT("Complete"), AmbushPlanRules::IsComplete(Assigned));
	TestEqual(TEXT("Left = the nearest left candidate"), Assigned[0], 4);
	TestEqual(TEXT("Right"), Assigned[1], 1);
	TestEqual(TEXT("Front"), Assigned[2], 0);
	TestEqual(TEXT("Rear"), Assigned[3], 3);
	// No candidate behind the squad: incomplete (the plan falls back to a single ambush).
	const TArray<int32> NoRear = AmbushPlanRules::AssignSpots(Anchor, 0.f, { Candidates[0], Candidates[1], Candidates[2] }, Params);
	TestFalse(TEXT("No rear spot: incomplete"), AmbushPlanRules::IsComplete(NoRear));
	TestEqual(TEXT("... the rear is unassigned"), NoRear[3], static_cast<int32>(INDEX_NONE));
	// Separation: the front candidate nearest its slot stands 2.8 m from the right flank's spot (across the sector line) - refused, the next
	// front candidate serves; without the separation the near one would be taken.
	const TArray<FVector2D> Crowded = { FVector2D(500.0, -1500.0), FVector2D(1700.0, 1300.0), FVector2D(1900.0, 1100.0), FVector2D(3500.0, 0.0),
		FVector2D(-1000.0, 0.0) };
	Params.MinSeparationCm = 600.f;
	const TArray<int32> Tight = AmbushPlanRules::AssignSpots(Anchor, 0.f, Crowded, Params);
	TestTrue(TEXT("Complete with the separation"), AmbushPlanRules::IsComplete(Tight));
	TestEqual(TEXT("The right flank takes its only candidate"), Tight[1], 1);
	TestEqual(TEXT("The front skips the one 2.8 m from it"), Tight[2], 3);
	Params.MinSeparationCm = 0.f;
	TestEqual(TEXT("No separation: the nearer front candidate"), AmbushPlanRules::AssignSpots(Anchor, 0.f, Crowded, Params)[2], 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAmbushPlanPhasesTest, "CodexTactics.Combat.AmbushPlan.Phases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAmbushPlanPhasesTest::RunTest(const FString&)
{
	FAmbushPlanParams Params;
	FAmbushPlanTick Tick;
	Tick.FlankMembersAlive = 2;
	// Waiting -> the squad comes in range: holding while both flanks are watched.
	Tick.NearestSquadCm = 2500.f;
	TestTrue(TEXT("Beyond the trigger range: waiting"), AmbushPlanRules::NextPhase(Tick, Params) == EAmbushPlanPhase::Waiting);
	Tick.NearestSquadCm = 900.f;
	TestTrue(TEXT("In range, flanks watched: holding"), AmbushPlanRules::NextPhase(Tick, Params) == EAmbushPlanPhase::Holding);
	Tick.Phase = EAmbushPlanPhase::Holding;
	Tick.FlanksUnwatched = 1;
	TestTrue(TEXT("A flank side unwatched: the flanks strike"), AmbushPlanRules::NextPhase(Tick, Params) == EAmbushPlanPhase::FlankBlock);
	Tick.FlanksUnwatched = 0;
	Tick.bAnyEngaged = true;
	TestTrue(TEXT("A party spotted / shot at: the plan starts at once with the flanks"), AmbushPlanRules::NextPhase(Tick, Params) == EAmbushPlanPhase::FlankBlock);
	Tick.Phase = EAmbushPlanPhase::Waiting;
	Tick.NearestSquadCm = 5000.f;
	TestTrue(TEXT("... even from waiting"), AmbushPlanRules::NextPhase(Tick, Params) == EAmbushPlanPhase::FlankBlock);
	// Flank block -> assault: not before the minimum, then as soon as the flanks are in place, at the latest at the maximum.
	Tick.Phase = EAmbushPlanPhase::FlankBlock;
	Tick.bAnyEngaged = false;
	Tick.FlankMembersInPlace = 2;
	Tick.SecondsInPhase = 0.5f;
	TestTrue(TEXT("Flanks in place at 0.5 s: still blocking (min 1.5 s)"), AmbushPlanRules::NextPhase(Tick, Params) == EAmbushPlanPhase::FlankBlock);
	Tick.SecondsInPhase = 1.6f;
	TestTrue(TEXT("In place after the minimum: rear + front strike"), AmbushPlanRules::NextPhase(Tick, Params) == EAmbushPlanPhase::Assault);
	Tick.FlankMembersInPlace = 1;
	TestTrue(TEXT("One flank still on its way: blocking"), AmbushPlanRules::NextPhase(Tick, Params) == EAmbushPlanPhase::FlankBlock);
	Tick.SecondsInPhase = 4.f;
	TestTrue(TEXT("At the maximum (4 s): the assault anyway"), AmbushPlanRules::NextPhase(Tick, Params) == EAmbushPlanPhase::Assault);
	Tick.SecondsInPhase = 1.6f;
	Tick.FlankMembersAlive = 0;
	Tick.FlankMembersInPlace = 0;
	TestTrue(TEXT("Both flanks dead: the assault"), AmbushPlanRules::NextPhase(Tick, Params) == EAmbushPlanPhase::Assault);
	Tick.Phase = EAmbushPlanPhase::Assault;
	Tick.NearestSquadCm = 99999.f;
	TestTrue(TEXT("The assault stays"), AmbushPlanRules::NextPhase(Tick, Params) == EAmbushPlanPhase::Assault);
	// Who strikes when.
	TestFalse(TEXT("Holding: nobody"), AmbushPlanRules::Springs(EAmbushPlanPhase::Holding, EAmbushParty::FlankLeft, false));
	TestTrue(TEXT("Flank block: the flanks"), AmbushPlanRules::Springs(EAmbushPlanPhase::FlankBlock, EAmbushParty::FlankRight, false));
	TestTrue(TEXT("Flank block: a ranged front member suppresses at once"), AmbushPlanRules::Springs(EAmbushPlanPhase::FlankBlock, EAmbushParty::Front, true));
	TestFalse(TEXT("Flank block: the melee front waits"), AmbushPlanRules::Springs(EAmbushPlanPhase::FlankBlock, EAmbushParty::Front, false));
	TestFalse(TEXT("Flank block: the melee rear waits"), AmbushPlanRules::Springs(EAmbushPlanPhase::FlankBlock, EAmbushParty::Rear, false));
	TestTrue(TEXT("Assault: the rear"), AmbushPlanRules::Springs(EAmbushPlanPhase::Assault, EAmbushParty::Rear, false));
	TestEqual(TEXT("Phase names"), FString(AmbushPlanRules::PhaseName(EAmbushPlanPhase::FlankBlock)), FString(TEXT("flank block")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAmbushRelocateTest, "CodexTactics.Combat.AmbushRelocate.Decide",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAmbushRelocateTest::RunTest(const FString&)
{
	FAmbushRelocateParams Params;
	const float Strike = 1800.f;
	TestFalse(TEXT("In strike range: never bypassed (it can strike)"), AmbushRelocateRules::IsBypassed(1500.f, 170.f, Strike, Params));
	TestTrue(TEXT("Behind the squad's way, out of range: bypassed"), AmbushRelocateRules::IsBypassed(2500.f, 150.f, Strike, Params));
	TestFalse(TEXT("Ahead, 25 m: still waiting for them"), AmbushRelocateRules::IsBypassed(2500.f, 30.f, Strike, Params));
	TestTrue(TEXT("Ahead but 40 m away (they went another way): bypassed"), AmbushRelocateRules::IsBypassed(4000.f, 30.f, Strike, Params));
	TestFalse(TEXT("Beside the way at 95 deg, 25 m: not yet"), AmbushRelocateRules::IsBypassed(2500.f, 95.f, Strike, Params));
	// The clock.
	float Seconds = 0.f;
	for (int32 Step = 0; Step < 151; ++Step)
	{
		Seconds = AmbushRelocateRules::StepBypassedSeconds(true, Seconds, 0.1f);
	}
	TestEqual(TEXT("15.1 s bypassed"), Seconds, 15.1f, 0.01f);
	TestTrue(TEXT("Due and unseen: relocate"), AmbushRelocateRules::Decide(Seconds, false, Params) == EAmbushRelocateDecision::Relocate);
	TestTrue(TEXT("Due but seen: wait until unseen"), AmbushRelocateRules::Decide(Seconds, true, Params) == EAmbushRelocateDecision::WaitUnseen);
	TestTrue(TEXT("Not due: stay"), AmbushRelocateRules::Decide(10.f, false, Params) == EAmbushRelocateDecision::Stay);
	TestEqual(TEXT("Not bypassed any more: the clock resets"), AmbushRelocateRules::StepBypassedSeconds(false, Seconds, 0.1f), 0.f);
	Params.bEnabled = false;
	TestTrue(TEXT("Relocation off: stay (the first version: wait until recycled)"), AmbushRelocateRules::Decide(99.f, false, Params) == EAmbushRelocateDecision::Stay);
	Params.bEnabled = true;
	// The destination: ahead and concealed.
	TestTrue(TEXT("Ahead + concealed: good"), AmbushRelocateRules::IsGoodDestination(0.8f, true, Params));
	TestFalse(TEXT("Behind: no"), AmbushRelocateRules::IsGoodDestination(-0.5f, true, Params));
	TestFalse(TEXT("In sight: no"), AmbushRelocateRules::IsGoodDestination(0.9f, false, Params));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
