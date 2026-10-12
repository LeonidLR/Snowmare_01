#include "Misc/AutomationTest.h"
#include "Algo/Count.h"
#include "Dom/JsonObject.h"
#include "Data/TacticalHeatTuning.h"
#include "Survival/ColdRules.h"
#include "Survival/TacticalHeatRules.h"
#include "Survival/WarmIdleRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Weather idle poses (user request 2026-10-12 + the fire-role design of the same day; UE-only).

#define WARM_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Survival.WarmIdle." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	FWarmIdleInput IdleInput(float Cold, EWarmHeatKind Heat = EWarmHeatKind::None)
	{
		FWarmIdleInput Input;
		Input.Cold = Cold;
		Input.Heat = Heat;
		Input.IdleSeconds = 10.f;
		return Input;
	}
}

WARM_TEST(FWarmIdleExplorationOnlyTest, "ExplorationOnly")
bool FWarmIdleExplorationOnlyTest::RunTest(const FString&)
{
	TestTrue(TEXT("exploration"), WarmIdleRules::IsExplorationPhase(ECodexGamePhase::Exploration, ECodexCombatMode::None));
	TestTrue(TEXT("post-combat return"), WarmIdleRules::IsExplorationPhase(ECodexGamePhase::PostCombat, ECodexCombatMode::None));
	TestFalse(TEXT("wave real time"), WarmIdleRules::IsExplorationPhase(ECodexGamePhase::WaveCombat, ECodexCombatMode::RealTime));
	TestFalse(TEXT("tactical pause in a fight"), WarmIdleRules::IsExplorationPhase(ECodexGamePhase::WaveCombat, ECodexCombatMode::TacticalPause));
	TestFalse(TEXT("turn-based"), WarmIdleRules::IsExplorationPhase(ECodexGamePhase::WaveCombat, ECodexCombatMode::TurnBased));
	TestFalse(TEXT("pre-combat cutscene"), WarmIdleRules::IsExplorationPhase(ECodexGamePhase::Cutscene, ECodexCombatMode::None));
	TestFalse(TEXT("preparation"), WarmIdleRules::IsExplorationPhase(ECodexGamePhase::Preparation, ECodexCombatMode::None));

	const FTacticalHeatConfig Config;
	FWarmIdleInput Fire = IdleInput(60.f, EWarmHeatKind::Campfire);
	TestEqual(TEXT("exploring at a campfire: a pose"), WarmIdleRules::ChooseSoloPose(Fire, Config), EWarmIdlePose::HandWarm);
	Fire.bExploration = false;
	TestEqual(TEXT("in combat: nobody sits or warms hands, they just stand"), WarmIdleRules::ChooseSoloPose(Fire, Config), EWarmIdlePose::None);
	FWarmIdleInput Cold = IdleInput(60.f);
	Cold.bExploration = false;
	TestEqual(TEXT("in combat: no cold idle"), WarmIdleRules::ChooseSoloPose(Cold, Config), EWarmIdlePose::None);
	return true;
}

WARM_TEST(FWarmIdleColdIdleTest, "ColdIdleThresholdAndHysteresis")
bool FWarmIdleColdIdleTest::RunTest(const FString&)
{
	const FTacticalHeatConfig Config;
	TestEqual(TEXT("threshold = Chills 40"), Config.WarmIdleColdThreshold, 40.f);
	TestEqual(TEXT("39: nothing"), WarmIdleRules::ChooseSoloPose(IdleInput(39.f), Config), EWarmIdlePose::None);
	TestEqual(TEXT("40: shivering"), WarmIdleRules::ChooseSoloPose(IdleInput(40.f), Config), EWarmIdlePose::ColdIdle);
	FWarmIdleInput Warming = IdleInput(37.f);
	Warming.bInColdIdle = true;
	TestEqual(TEXT("in the cold idle at 37: kept (hysteresis 5)"), WarmIdleRules::ChooseSoloPose(Warming, Config), EWarmIdlePose::ColdIdle);
	Warming.Cold = 34.f;
	TestEqual(TEXT("34: ends"), WarmIdleRules::ChooseSoloPose(Warming, Config), EWarmIdlePose::None);
	FWarmIdleInput Fresh = IdleInput(60.f);
	Fresh.IdleSeconds = 1.f;
	TestEqual(TEXT("idle 1 s < 2 s delay: not yet"), WarmIdleRules::ChooseSoloPose(Fresh, Config), EWarmIdlePose::None);
	FWarmIdleInput Crouched = IdleInput(60.f);
	Crouched.Stance = EOperativeStance::Crouching;
	TestEqual(TEXT("crouched: the standing set does not play"), WarmIdleRules::ChooseSoloPose(Crouched, Config), EWarmIdlePose::None);
	TestEqual(TEXT("generator / flare zone: warm, no pose"), WarmIdleRules::ChooseSoloPose(IdleInput(60.f, EWarmHeatKind::Other), Config), EWarmIdlePose::None);
	TestEqual(TEXT("barrel: standing hand warming"), WarmIdleRules::ChooseSoloPose(IdleInput(10.f, EWarmHeatKind::Barrel), Config), EWarmIdlePose::HandWarm);
	return true;
}

WARM_TEST(FWarmIdleInterruptsTest, "Interrupts")
bool FWarmIdleInterruptsTest::RunTest(const FString&)
{
	const FTacticalHeatConfig Config;
	const auto Blocked = [&](TFunctionRef<void(FWarmIdleInput&)> Change)
	{
		FWarmIdleInput Input = IdleInput(70.f, EWarmHeatKind::Campfire);
		Change(Input);
		return !WarmIdleRules::CanHoldWarmPose(Input, Config);
	};
	TestTrue(TEXT("baseline holds"), WarmIdleRules::CanHoldWarmPose(IdleInput(70.f, EWarmHeatKind::Campfire), Config));
	TestTrue(TEXT("an order / a move"), Blocked([](FWarmIdleInput& I) { I.bMoving = true; }));
	TestTrue(TEXT("a threat"), Blocked([](FWarmIdleInput& I) { I.bThreat = true; }));
	TestTrue(TEXT("knocked down"), Blocked([](FWarmIdleInput& I) { I.bKnockedDown = true; }));
	TestTrue(TEXT("dead"), Blocked([](FWarmIdleInput& I) { I.bAlive = false; }));
	TestTrue(TEXT("frostbitten (lies prone)"), Blocked([](FWarmIdleInput& I) { I.bFrostbitten = true; }));
	TestTrue(TEXT("in cover"), Blocked([](FWarmIdleInput& I) { I.bInCover = true; }));
	TestTrue(TEXT("carrying"), Blocked([](FWarmIdleInput& I) { I.bCarrying = true; }));
	TestTrue(TEXT("a task / panic"), Blocked([](FWarmIdleInput& I) { I.bBusy = true; }));
	TestTrue(TEXT("prone"), Blocked([](FWarmIdleInput& I) { I.Stance = EOperativeStance::Prone; }));
	TestTrue(TEXT("combat started"), Blocked([](FWarmIdleInput& I) { I.bExploration = false; }));
	// How it ends: orders / threats blend out at once (orders take priority), the body layers cut, the rest plays the End clip.
	TestTrue(TEXT("order: quick blend"), WarmIdleRules::GetExitStyle(EWarmExitReason::Moved, true) == EWarmExitStyle::QuickBlend);
	TestTrue(TEXT("threat: quick blend"), WarmIdleRules::GetExitStyle(EWarmExitReason::Threat, true) == EWarmExitStyle::QuickBlend);
	TestTrue(TEXT("knockdown: cut"), WarmIdleRules::GetExitStyle(EWarmExitReason::BodyTaken, true) == EWarmExitStyle::Cut);
	TestTrue(TEXT("combat start: stand up (End clip)"), WarmIdleRules::GetExitStyle(EWarmExitReason::CombatStarted, true) == EWarmExitStyle::PlayEnd);
	TestTrue(TEXT("no End clip (She Cold_1): blend out"), WarmIdleRules::GetExitStyle(EWarmExitReason::NoLongerWanted, false) == EWarmExitStyle::QuickBlend);
	return true;
}

WARM_TEST(FWarmIdleGenderTest, "GenderChoice")
bool FWarmIdleGenderTest::RunTest(const FString&)
{
	TestTrue(TEXT("SK_Female_soldier"), WarmIdleRules::UsesFemaleClips(TEXT("SK_Female_soldier"), TEXT("Female_Soldier_Skeleton")));
	TestTrue(TEXT("female skeleton, other mesh name"), WarmIdleRules::UsesFemaleClips(TEXT("SK_Medic_Body"), TEXT("Female_Soldier_Skeleton")));
	TestFalse(TEXT("Post_Apo operative"), WarmIdleRules::UsesFemaleClips(TEXT("SK_Post_Apo_Survivor_a"), TEXT("UE4_Mannequin_Skeleton")));
	TestFalse(TEXT("recruit (Susanin): male set"), WarmIdleRules::UsesFemaleClips(TEXT("SK_Post_Apo_Survivor_c"), TEXT("UE4_Mannequin_Skeleton")));
	return true;
}

WARM_TEST(FWarmIdleMultiplierTest, "CombatWarmMultiplier")
bool FWarmIdleMultiplierTest::RunTest(const FString&)
{
	const FTacticalHeatConfig Heat;
	TestEqual(TEXT("exploration x1"), WarmIdleRules::GetHeatSourceWarmthMultiplier(Heat, false), 1.f);
	TestEqual(TEXT("combat x0.5"), WarmIdleRules::GetHeatSourceWarmthMultiplier(Heat, true), 0.5f);
	TestTrue(TEXT("wave fight = combat"), WarmIdleRules::IsCombatForWarming(ECodexGamePhase::WaveCombat));
	TestFalse(TEXT("exploration"), WarmIdleRules::IsCombatForWarming(ECodexGamePhase::Exploration));
	TestFalse(TEXT("preparation keeps its warming"), WarmIdleRules::IsCombatForWarming(ECodexGamePhase::Preparation));
	// Cold step: a plain source warms at 8 %/s (fortitude 0): 50 -> 42 exploring, 50 -> 46 in a fight.
	const FColdConfig Cold;
	FColdEnvironment Warm;
	Warm.bWarm = true;
	TestEqual(TEXT("exploring: 8 %/s"), ColdRules::StepCold(Cold, 50.f, 1.f, Warm, EOperativeStance::Standing, 0.f), 42.f, 0.001f);
	Warm.HeatWarmthMultiplier = WarmIdleRules::GetHeatSourceWarmthMultiplier(Heat, true);
	TestEqual(TEXT("fighting: 4 %/s"), ColdRules::StepCold(Cold, 50.f, 1.f, Warm, EOperativeStance::Standing, 0.f), 46.f, 0.001f);
	// Preparation without a heat source is not scaled.
	FColdEnvironment Prep;
	Prep.bPreparation = true;
	Prep.HeatWarmthMultiplier = 0.5f;
	TestEqual(TEXT("preparation warming unscaled"), ColdRules::StepCold(Cold, 50.f, 1.f, Prep, EOperativeStance::Standing, 0.f), 42.f, 0.001f);
	// tactical_heat.json keys.
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetNumberField(TEXT("combat_warmth_multiplier"), 0.25);
	Json->SetNumberField(TEXT("warm_min_sitters"), 3);
	Json->SetNumberField(TEXT("warm_extra_sitter_cold"), 65);
	const FTacticalHeatConfig Read = TacticalHeatTuning::FromJson(*Json);
	TestEqual(TEXT("json combat multiplier"), Read.CombatWarmthMultiplier, 0.25f);
	TestEqual(TEXT("json min sitters"), Read.WarmMinSitters, 3);
	TestEqual(TEXT("json extra sitter cold"), Read.WarmExtraSitterCold, 65.f);
	TestEqual(TEXT("json round trip"), TacticalHeatTuning::FromJson(*TacticalHeatTuning::ToJson(Read)).WarmSpotSpacing, Read.WarmSpotSpacing);
	return true;
}

WARM_TEST(FWarmIdleFireRolesTest, "FireRolesColdestSit")
bool FWarmIdleFireRolesTest::RunTest(const FString&)
{
	using R = EWarmFireRole;
	const TArray<R> Fresh3 = { R::Unassigned, R::Unassigned, R::Unassigned };
	// Three at the fire, cold 30 / 20 / 45: the two coldest sit, the warmest stands (min 2, extra threshold 50 not reached).
	const TArray<R> Three = WarmIdleRules::AssignFireRoles(Fresh3, { 30.f, 20.f, 45.f }, 2, 50.f, 8);
	TestTrue(TEXT("45 sits"), Three[2] == R::Sit);
	TestTrue(TEXT("30 sits"), Three[0] == R::Sit);
	TestTrue(TEXT("20 stands"), Three[1] == R::Stand);
	// A very cold third operative (>= 50) sits too.
	const TArray<R> Cold = WarmIdleRules::AssignFireRoles(Fresh3, { 52.f, 55.f, 60.f }, 2, 50.f, 8);
	TestTrue(TEXT("all three sit"), Cold[0] == R::Sit && Cold[1] == R::Sit && Cold[2] == R::Sit);
	// One operative alone sits; two always both sit.
	TestTrue(TEXT("alone: sits"), WarmIdleRules::AssignFireRoles({ R::Unassigned }, { 5.f }, 2, 50.f, 8)[0] == R::Sit);
	const TArray<R> Two = WarmIdleRules::AssignFireRoles({ R::Unassigned, R::Unassigned }, { 5.f, 0.f }, 2, 50.f, 8);
	TestTrue(TEXT("two: both sit"), Two[0] == R::Sit && Two[1] == R::Sit);
	// No flicker: kept roles survive a cold change (the stander has become the coldest - he keeps standing).
	const TArray<R> Kept = WarmIdleRules::AssignFireRoles(Three, { 10.f, 60.f, 12.f }, 2, 50.f, 8);
	TestTrue(TEXT("roles kept"), Kept == Three);
	// A newcomer (4th, recruit): sits only when cold enough, else stands in the outer ring.
	const TArray<R> Arrive = WarmIdleRules::AssignFireRoles({ R::Sit, R::Stand, R::Sit, R::Unassigned }, { 30.f, 20.f, 45.f, 25.f }, 2, 50.f, 8);
	TestTrue(TEXT("recruit stands, the others keep"), Arrive[3] == R::Stand && Arrive[0] == R::Sit && Arrive[1] == R::Stand && Arrive[2] == R::Sit);
	const TArray<R> ArriveCold = WarmIdleRules::AssignFireRoles({ R::Sit, R::Stand, R::Sit, R::Unassigned }, { 30.f, 20.f, 45.f, 70.f }, 2, 50.f, 8);
	TestTrue(TEXT("a frozen recruit sits"), ArriveCold[3] == R::Sit && ArriveCold[1] == R::Stand);
	// A sitter left (ordered away): the coldest stander sits down so two still sit.
	const TArray<R> Left = WarmIdleRules::AssignFireRoles({ R::Sit, R::Stand, R::Stand }, { 30.f, 20.f, 25.f }, 2, 50.f, 8);
	TestTrue(TEXT("coldest stander promoted"), Left[0] == R::Sit && Left[2] == R::Sit && Left[1] == R::Stand);
	// Seats cap the sitters (a crowded stove).
	const TArray<R> Capped = WarmIdleRules::AssignFireRoles(Fresh3, { 80.f, 90.f, 70.f }, 2, 50.f, 2);
	TestEqual(TEXT("two seats: two sit"), static_cast<int32>(Algo::CountIf(Capped, [](R Role) { return Role == R::Sit; })), 2);
	TestTrue(TEXT("the 70 one stands"), Capped[2] == R::Stand);
	// Fire state changed: everyone Unassigned again -> a full re-evaluation by the current cold.
	const TArray<R> Again = WarmIdleRules::AssignFireRoles(Fresh3, { 10.f, 60.f, 12.f }, 2, 50.f, 8);
	TestTrue(TEXT("re-evaluated after a relight"), Again[1] == R::Sit && Again[2] == R::Sit && Again[0] == R::Stand);
	TestEqual(TEXT("sit -> sitting set"), WarmIdleRules::PoseForFireRole(R::Sit), EWarmIdlePose::SitWarm);
	TestEqual(TEXT("stand -> hand warming"), WarmIdleRules::PoseForFireRole(R::Stand), EWarmIdlePose::HandWarm);
	return true;
}

WARM_TEST(FWarmIdleSeatsTest, "SeatsSpreadAndFacing")
bool FWarmIdleSeatsTest::RunTest(const FString&)
{
	const FTacticalHeatConfig Config;
	const FVector Fire(1000.f, 500.f, 20.f);
	const int32 Seats = WarmIdleRules::RingCapacity(Config.CampfireSeatDistance, Config.WarmSpotSpacing);
	TestEqual(TEXT("campfire inner ring: 8 seats (115 cm, 85 cm apart)"), Seats, 8);
	const TArray<FVector> Ring = WarmIdleRules::ComputeRing(Fire, Config.CampfireSeatDistance, Seats, 0.f);
	TestEqual(TEXT("first seat east of the fire"), Ring[0], FVector(1115.f, 500.f, 20.f));
	for (const FVector& Seat : Ring)
	{
		TestEqual(TEXT("on the ring"), static_cast<float>(FVector::Dist2D(Seat, Fire)), Config.CampfireSeatDistance, 0.01f);
	}
	TestTrue(TEXT("neighbours >= spacing apart"), FVector::Dist2D(Ring[0], Ring[1]) >= Config.WarmSpotSpacing);
	// Three operatives standing west of the fire: the first takes the nearest seat, the others spread round the fire.
	TArray<bool> Taken;
	Taken.Init(false, Ring.Num());
	const FVector West(700.f, 500.f, 20.f);
	const int32 First = WarmIdleRules::PickSlot(Ring, Taken, Fire, West);
	TestEqual(TEXT("nearest seat (west = index 4)"), First, 4);
	Taken[First] = true;
	const int32 Second = WarmIdleRules::PickSlot(Ring, Taken, Fire, West + FVector(0.f, 50.f, 0.f));
	TestEqual(TEXT("second: across the fire"), Second, 0);
	Taken[Second] = true;
	const int32 Third = WarmIdleRules::PickSlot(Ring, Taken, Fire, West);
	TestTrue(TEXT("third: a side seat (90 deg)"), Third == 2 || Third == 6);
	Taken[Third] = true;
	TestTrue(TEXT("distinct"), First != Second && Second != Third && First != Third);
	// Facing: from a seat to the fire.
	TestEqual(TEXT("west seat faces east (0)"), WarmIdleRules::FaceYaw(Ring[4], Fire), 0.f, 0.01f);
	TestEqual(TEXT("east seat faces west (180)"), FMath::Abs(WarmIdleRules::FaceYaw(Ring[0], Fire)), 180.f, 0.01f);
	// No seat free -> none.
	TArray<bool> Full;
	Full.Init(true, Ring.Num());
	TestEqual(TEXT("full ring"), WarmIdleRules::PickSlot(Ring, Full, Fire, West), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("barrel: 9 hand-warming places at 125 cm"), WarmIdleRules::RingCapacity(Config.BarrelStandDistance, Config.WarmSpotSpacing), 9);
	return true;
}

WARM_TEST(FWarmIdleClipStepsTest, "ClipSequence")
bool FWarmIdleClipStepsTest::RunTest(const FString&)
{
	using S = EWarmClipStep;
	TestEqual(TEXT("start -> loop A"), WarmIdleRules::NextClipStep(S::Start, 0, 2, true), S::LoopA);
	TestEqual(TEXT("loop A once more"), WarmIdleRules::NextClipStep(S::LoopA, 1, 2, true), S::LoopA);
	TestEqual(TEXT("loop A done -> transit"), WarmIdleRules::NextClipStep(S::LoopA, 2, 2, true), S::TransitAB);
	TestEqual(TEXT("transit -> loop B"), WarmIdleRules::NextClipStep(S::TransitAB, 0, 2, true), S::LoopB);
	TestEqual(TEXT("loop B done -> transit back"), WarmIdleRules::NextClipStep(S::LoopB, 3, 3, true), S::TransitBA);
	TestEqual(TEXT("transit back -> loop A"), WarmIdleRules::NextClipStep(S::TransitBA, 0, 2, true), S::LoopA);
	TestEqual(TEXT("no loop B: loop A forever"), WarmIdleRules::NextClipStep(S::LoopA, 9, 2, false), S::LoopA);
	TestEqual(TEXT("end -> none"), WarmIdleRules::NextClipStep(S::End, 0, 2, true), S::None);
	return true;
}

#endif
