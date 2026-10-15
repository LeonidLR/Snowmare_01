#include "Misc/AutomationTest.h"
#include "Characters/InteractionAnimRules.h"
#include "Dom/JsonObject.h"
#include "Data/TacticalHeatTuning.h"
#include "Interactables/TrapApproachRules.h"
#include "Survival/TacticalHeatRules.h"
#include "Survival/WarmIdleRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Interaction clips + the disarm walk-up (game designer 2026-10-15; UE-only).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInteractionAnimLayerByStanceTest, "CodexTactics.Characters.InteractionAnim.LayerByStance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FInteractionAnimLayerByStanceTest::RunTest(const FString&)
{
	TestEqual(TEXT("standing -> full body"), InteractionAnimRules::ChooseLayer(EOperativeStance::Standing), EInteractionAnimLayer::FullBody);
	TestEqual(TEXT("crouched -> upper body (legs keep the crouch)"), InteractionAnimRules::ChooseLayer(EOperativeStance::Crouching),
		EInteractionAnimLayer::UpperBody);
	TestEqual(TEXT("prone -> none for now"), InteractionAnimRules::ChooseLayer(EOperativeStance::Prone), EInteractionAnimLayer::None);
	TestTrue(TEXT("plays while time runs"), InteractionAnimRules::ShouldPlayClip(false, EInteractionAnimLayer::UpperBody));
	TestFalse(TEXT("never in the tactical pause"), InteractionAnimRules::ShouldPlayClip(true, EInteractionAnimLayer::FullBody));
	TestFalse(TEXT("never prone"), InteractionAnimRules::ShouldPlayClip(false, EInteractionAnimLayer::None));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInteractionAnimLootWindowDelayTest, "CodexTactics.Characters.InteractionAnim.LootWindowDelay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FInteractionAnimLootWindowDelayTest::RunTest(const FString&)
{
	// Chest open 3.0 s, blend-out 0.25: the window opens as it starts to blend out.
	TestEqual(TEXT("clip first, then the window"), InteractionAnimRules::GetLootWindowDelay(3.f, 0.25f, 1.8f, false), 2.75f);
	TestEqual(TEXT("no clip (prone / none): the crate's 1.8 s"), InteractionAnimRules::GetLootWindowDelay(0.f, 0.25f, 1.8f, false), 1.8f);
	TestEqual(TEXT("tactical pause: at once"), InteractionAnimRules::GetLootWindowDelay(3.f, 0.25f, 1.8f, true), 0.f);
	TestEqual(TEXT("blend-out longer than the clip clamps"), InteractionAnimRules::GetLootWindowDelay(0.1f, 0.25f, 1.8f, false), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInteractionAnimColdVariantTest, "CodexTactics.Survival.WarmIdle.ColdVariant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FInteractionAnimColdVariantTest::RunTest(const FString&)
{
	TestTrue(TEXT("roll under the chance after the cooldown"), WarmIdleRules::ShouldPlayColdVariant(0.2f, 0.35f, 9.f, 8.f, true));
	TestFalse(TEXT("roll over the chance"), WarmIdleRules::ShouldPlayColdVariant(0.5f, 0.35f, 9.f, 8.f, true));
	TestFalse(TEXT("inside the cooldown"), WarmIdleRules::ShouldPlayColdVariant(0.f, 1.f, 7.9f, 8.f, true));
	TestFalse(TEXT("chance 0 = never"), WarmIdleRules::ShouldPlayColdVariant(0.f, 0.f, 100.f, 0.f, true));
	TestFalse(TEXT("no variant clip"), WarmIdleRules::ShouldPlayColdVariant(0.f, 1.f, 100.f, 0.f, false));
	TestTrue(TEXT("chance 1, no cooldown: always (smoke setting)"), WarmIdleRules::ShouldPlayColdVariant(0.999f, 1.f, 0.f, 0.f, true));

	// Data-driven in tactical_heat.json (the existing weather idle data).
	const FTacticalHeatConfig Defaults;
	TestEqual(TEXT("default chance"), Defaults.ColdIdleVariantChance, 0.35f);
	TestEqual(TEXT("default cooldown"), Defaults.ColdIdleVariantCooldownSeconds, 8.f);
	TestEqual(TEXT("default loops"), Defaults.ColdIdleVariantLoops, 3);
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetNumberField(TEXT("cold_idle_variant_chance"), 0.5);
	Json->SetNumberField(TEXT("cold_idle_variant_cooldown_seconds"), 4.0);
	Json->SetNumberField(TEXT("cold_idle_variant_loops"), 2);
	const FTacticalHeatConfig Read = TacticalHeatTuning::FromJson(*Json);
	TestEqual(TEXT("json chance"), Read.ColdIdleVariantChance, 0.5f);
	TestEqual(TEXT("json cooldown"), Read.ColdIdleVariantCooldownSeconds, 4.f);
	TestEqual(TEXT("json loops"), Read.ColdIdleVariantLoops, 2);
	const TSharedRef<FJsonObject> Written = TacticalHeatTuning::ToJson(Read);
	TestEqual(TEXT("round trip loops"), static_cast<int32>(Written->GetNumberField(TEXT("cold_idle_variant_loops"))), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrapApproachDistanceTest, "CodexTactics.Interactables.TrapApproach.Distance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTrapApproachDistanceTest::RunTest(const FString&)
{
	using namespace TrapApproachRules;
	// Mine at the origin, operative 5 m east: he stops 60 cm east of the charge (towards himself, never on it).
	const FVector2D Mine(0.f, 0.f);
	const FVector2D MinePoint = MineApproachPoint(Mine, FVector2D(500.f, 0.f), DefaultStandOffCm);
	TestTrue(TEXT("mine: 60 cm towards him"), MinePoint.Equals(FVector2D(60.f, 0.f), 0.01f));
	TestTrue(TEXT("mine: on his side, not past it"), MinePoint.X > 0.f);
	TestTrue(TEXT("mine: from the north-west too"), MineApproachPoint(Mine, FVector2D(-300.f, 300.f), 60.f).Equals(FVector2D(-42.43f, 42.43f), 0.05f));
	TestTrue(TEXT("mine: standing on it -> any side, still 60 cm"), FMath::IsNearlyEqual(static_cast<float>(MineApproachPoint(Mine, Mine, 60.f).Size()), 60.f, 0.01f));

	// Wire from (-100, 0) to (100, 0); operative at (30, -400): closest point (30, 0), he stands at (30, -60).
	const FVector2D A(-100.f, 0.f);
	const FVector2D B(100.f, 0.f);
	TestTrue(TEXT("wire: closest point"), ClosestPointOnWire(A, B, FVector2D(30.f, -400.f)).Equals(FVector2D(30.f, 0.f), 0.01f));
	TestTrue(TEXT("wire: 60 cm on his side"), WireApproachPoint(A, B, FVector2D(30.f, -400.f), 60.f).Equals(FVector2D(30.f, -60.f), 0.01f));
	TestTrue(TEXT("wire: the other side"), WireApproachPoint(A, B, FVector2D(-20.f, 250.f), 60.f).Equals(FVector2D(-20.f, 60.f), 0.01f));
	TestTrue(TEXT("wire: beyond an anchor -> beyond that end"), WireApproachPoint(A, B, FVector2D(400.f, 0.f), 60.f).Equals(FVector2D(160.f, 0.f), 0.01f));
	TestTrue(TEXT("wire: on the line -> its normal"), FMath::IsNearlyEqual(static_cast<float>(FMath::Abs(WireApproachPoint(A, B, FVector2D(0.f, 0.f), 60.f).Y)), 60.f, 0.01f));

	// Reach: stand-off 60 + tolerance 25 -> the menu opens at <= 85 cm (well inside the 0.9 m the designer asked for).
	TestTrue(TEXT("at 60 cm"), IsAtDisarmSpot(60.f, DefaultStandOffCm, DefaultReachToleranceCm));
	TestTrue(TEXT("at 85 cm (acceptance radius)"), IsAtDisarmSpot(85.f, DefaultStandOffCm, DefaultReachToleranceCm));
	TestFalse(TEXT("at 90 cm still walking"), IsAtDisarmSpot(90.f, DefaultStandOffCm, DefaultReachToleranceCm));
	TestFalse(TEXT("the old mine stop (~2.7 m) is far out"), IsAtDisarmSpot(270.f, DefaultStandOffCm, DefaultReachToleranceCm));
	TestTrue(TEXT("stand-off + tolerance <= 90 cm"), DefaultStandOffCm + DefaultReachToleranceCm <= 90.f);
	// Walking in: the menu waits until 70 cm (it would stop him at the outer edge), the goal lies 15 cm inside the stand-off.
	TestFalse(TEXT("still walking at 80 cm: walk on"), IsAtDisarmSpot(80.f, DefaultStandOffCm, DefaultReachToleranceCm, true));
	TestTrue(TEXT("still walking at 70 cm: there"), IsAtDisarmSpot(70.f, DefaultStandOffCm, DefaultReachToleranceCm, true));
	TestTrue(TEXT("stopped at 80 cm (path ended early): there"), IsAtDisarmSpot(80.f, DefaultStandOffCm, DefaultReachToleranceCm, false));
	TestEqual(TEXT("goal 45 cm from the charge / the wire"), GoalDistance(DefaultStandOffCm), 45.f);
	TestTrue(TEXT("a wire goal keeps a 40 cm capsule off the wire"), GoalDistance(DefaultStandOffCm) > 40.f);
	TestEqual(TEXT("no negative goal"), GoalDistance(5.f), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrapApproachDefuserLeaseTest, "CodexTactics.Interactables.TrapApproach.DefuserLease",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTrapApproachDefuserLeaseTest::RunTest(const FString&)
{
	using namespace TrapApproachRules;
	const float Trigger = 160.f;
	TestFalse(TEXT("approaching defuser outside the radius: no lease yet"), UpdateDefuserLease(false, true, 300.f, Trigger));
	TestTrue(TEXT("he steps in as the defuser: lease"), UpdateDefuserLease(false, true, 150.f, Trigger));
	TestTrue(TEXT("menu closed (no longer approaching), still inside: kept"), UpdateDefuserLease(true, false, 60.f, Trigger));
	TestTrue(TEXT("walking out, inside the exit margin: kept"), UpdateDefuserLease(true, false, 185.f, Trigger));
	TestFalse(TEXT("stepped out: the mine counts for him again"), UpdateDefuserLease(true, false, 195.f, Trigger));
	TestFalse(TEXT("anyone else inside: no lease"), UpdateDefuserLease(false, false, 60.f, Trigger));
	return true;
}

#endif
