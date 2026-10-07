// CodexTactics.Tactics.Cover.* — Sprint 12 tactical cover (TANDEM «SPRINT 12 DIRECTIVE»; UE-only, Gemini spec, user
// decisions 2026-10-06: high cover absorbs 90 % of frontal hits, cover blind fire -40 % is distinct from the Sprint 08
// ghost blind fire -80 %, both multiply with a 5 % floor; the decision rules are Jev-calibrated, see
// Scripts/Tools/jev_validate_cover.py).

#include "Misc/AutomationTest.h"
#include "Survival/ColdRules.h"
#include "Tactics/CoverDecisionRules.h"
#include "Tactics/CoverRules.h"
#include "Tactics/CoverTraceRules.h"
#include "Tactics/CoverTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoverTraceHeightTest, "CodexTactics.Tactics.Cover.TraceDetectsHighWallVsLowBarricade",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCoverTraceHeightTest::RunTest(const FString&)
{
	using namespace CoverTraceRules;
	const FCoverTraceConfig Config;
	TestEqual(TEXT("60 cm barricade = low cover"), ClassifyHeight(60.f, Config), ECoverHeight::LowCover);
	TestEqual(TEXT("100 cm = low cover"), ClassifyHeight(100.f, Config), ECoverHeight::LowCover);
	TestEqual(TEXT("150 cm (130-180 band) = low cover, decision 2026-10-06"), ClassifyHeight(150.f, Config), ECoverHeight::LowCover);
	TestEqual(TEXT("180 cm = high cover"), ClassifyHeight(180.f, Config), ECoverHeight::HighCover);
	TestEqual(TEXT("300 cm wall = high cover"), ClassifyHeight(300.f, Config), ECoverHeight::HighCover);
	TestEqual(TEXT("30 cm kerb = no cover"), ClassifyHeight(30.f, Config), ECoverHeight::None);
	TestEqual(TEXT("chest + head traces hit = high"), ClassifyFromTraces(true, true), ECoverHeight::HighCover);
	TestEqual(TEXT("chest only = low"), ClassifyFromTraces(true, false), ECoverHeight::LowCover);
	TestEqual(TEXT("nothing at knee / chest height = none"), ClassifyFromTraces(false, true, false), ECoverHeight::None);
	TestEqual(TEXT("knee only (60 cm barricade) = low"), ClassifyFromTraces(false, false, true), ECoverHeight::LowCover);
	// The slot: 45 cm off the wall along the planar normal, on the ground.
	const FVector Slot = ComputeSlotLocation(FVector(1000.f, 0.f, 90.f), FVector(-1.f, 0.f, 0.2f), 45.f, 10.f);
	TestTrue(TEXT("slot 45 cm in front of the wall on the ground"), Slot.Equals(FVector(955.f, 0.f, 10.f), 0.5f));
	FCoverSlot A;
	A.WallNormal = FVector(-1.f, 0.f, 0.f);
	A.Height = ECoverHeight::HighCover;
	TestTrue(TEXT("facing along the normal: yaw 180"), FMath::IsNearlyEqual(FacingYaw(A), 180.f, 0.01f));
	TestTrue(TEXT("right tangent of a -X normal is -Y"), A.RightTangent().Equals(FVector(0.f, -1.f, 0.f), 0.001f));
	TestEqual(TEXT("high cover enters standing"), CoverRules::DefaultStanceFor(ECoverHeight::HighCover), EOperativeStance::Standing);
	TestEqual(TEXT("low cover enters crouched"), CoverRules::DefaultStanceFor(ECoverHeight::LowCover), EOperativeStance::Crouching);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoverCornerTest, "CodexTactics.Tactics.Cover.CornerExposureCalculation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCoverCornerTest::RunTest(const FString&)
{
	using namespace CoverTraceRules;
	float Distance = 0.f;
	TestTrue(TEXT("a probe missing the wall exposes the corner"), EdgeExposedFromProbes({ true, true, false, false, false }, 40.f, Distance));
	TestEqual(TEXT("corner 120 cm along the wall (third probe)"), Distance, 120.f);
	TestFalse(TEXT("every probe hits: no corner in reach"), EdgeExposedFromProbes({ true, true, true, true, true }, 40.f, Distance));
	TestEqual(TEXT("no corner: distance 0"), Distance, 0.f);
	TestTrue(TEXT("the first probe missing = corner at 40 cm"), EdgeExposedFromProbes({ false }, 40.f, Distance) && Distance == 40.f);

	FCoverSlot A;
	A.WallNormal = FVector(-1.f, 0.f, 0.f);
	A.WallPoint = FVector(1000.f, 0.f, 90.f);
	A.WorldLocation = FVector(955.f, 0.f, 0.f);
	A.Height = ECoverHeight::HighCover;
	FCoverSlot B = A;
	B.WallPoint = FVector(1005.f, 300.f, 90.f);
	B.WorldLocation = FVector(960.f, 300.f, 0.f);
	TestTrue(TEXT("3 m along the same wall = same wall"), IsSameWall(A, B));
	FCoverSlot C = A;
	C.WallNormal = FVector(0.f, 1.f, 0.f);
	TestFalse(TEXT("a perpendicular wall is another wall"), IsSameWall(A, C));
	FCoverSlot D = A;
	D.WallPoint = FVector(1200.f, 0.f, 90.f);
	TestFalse(TEXT("a parallel wall 2 m further back is another wall"), IsSameWall(A, D));
	TestTrue(TEXT("along-wall distance: -Y is the right of a man facing -X"), FMath::IsNearlyEqual(AlongWallDistance(A, FVector(955.f, -250.f, 0.f)), 250.f, 0.01f));
	TestTrue(TEXT("... +Y is his left"), FMath::IsNearlyEqual(AlongWallDistance(A, FVector(955.f, 250.f, 0.f)), -250.f, 0.01f));

	A.bLeftEdgeExposed = true;
	A.bRightEdgeExposed = false;
	TestEqual(TEXT("the only exposed corner is worked"), ChooseFacing(A, nullptr), ECoverFacing::Left);
	A.bRightEdgeExposed = true;
	const FVector ThreatRight(955.f, -800.f, 0.f);
	const FVector ThreatLeft(955.f, 800.f, 0.f);
	TestEqual(TEXT("both exposed: the corner towards the threat (right)"), ChooseFacing(A, &ThreatRight), ECoverFacing::Right);
	TestEqual(TEXT("both exposed: the corner towards the threat (left)"), ChooseFacing(A, &ThreatLeft), ECoverFacing::Left);
	TestEqual(TEXT("no threat known: right"), ChooseFacing(A, nullptr), ECoverFacing::Right);
	const FVector Muzzle = CoverRules::CornerMuzzle(A, ECoverFacing::Right, FVector(955.f, 0.f, 140.f), 60.f);
	TestTrue(TEXT("corner muzzle 60 cm round the right corner"), Muzzle.Equals(FVector(955.f, -60.f, 140.f), 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoverDamageArcTest, "CodexTactics.Tactics.Cover.DamageBlockedFromFrontalArc",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCoverDamageArcTest::RunTest(const FString&)
{
	using namespace CoverRules;
	const FCoverCombatConfig Config;
	TestEqual(TEXT("user decision: 90 %, not 100 %"), Config.HighCoverFrontalAbsorb, 0.9f);
	TestEqual(TEXT("160 deg arc"), Config.FrontalArcDeg, 160.f);
	// The wall is behind a man facing +X (normal +X): shooters at -X are behind the wall.
	const FVector Normal(1.f, 0.f, 0.f);
	const FVector Slot(0.f, 0.f, 0.f);
	TestTrue(TEXT("straight behind the wall: in the arc"), IsInFrontalArc(Normal, Slot, FVector(-1000.f, 0.f, 0.f), Config.FrontalArcDeg));
	TestTrue(TEXT("17 deg off: in the arc"), IsInFrontalArc(Normal, Slot, FVector(-1000.f, 300.f, 0.f), Config.FrontalArcDeg));
	TestTrue(TEXT("75 deg off: still in the 160 deg arc"), IsInFrontalArc(Normal, Slot, FVector(-268.f, 1000.f, 0.f), Config.FrontalArcDeg));
	TestFalse(TEXT("90 deg (along the wall): flank"), IsInFrontalArc(Normal, Slot, FVector(0.f, 1000.f, 0.f), Config.FrontalArcDeg));
	TestFalse(TEXT("from his open side: no wall"), IsInFrontalArc(Normal, Slot, FVector(1000.f, 0.f, 0.f), Config.FrontalArcDeg));
	TestTrue(TEXT("angle helper"), FMath::IsNearlyEqual(AngleFromWallDeg(Normal, Slot, FVector(-1000.f, 1000.f, 0.f)), 45.f, 0.01f));

	TestEqual(TEXT("high cover, frontal: 90 % absorbed"), AbsorbFraction(Config, ECoverHeight::HighCover, EOperativeStance::Standing, false, true), 0.9f);
	TestEqual(TEXT("high cover, crouched, frontal: 90 %"), AbsorbFraction(Config, ECoverHeight::HighCover, EOperativeStance::Crouching, false, true), 0.9f);
	TestEqual(TEXT("flank hit passes fully"), AbsorbFraction(Config, ECoverHeight::HighCover, EOperativeStance::Standing, false, false), 0.f);
	TestEqual(TEXT("leaning out: the body shows, nothing absorbed"), AbsorbFraction(Config, ECoverHeight::HighCover, EOperativeStance::Standing, true, true), 0.f);
	TestEqual(TEXT("low cover crouched: the barricade's 35 %"), AbsorbFraction(Config, ECoverHeight::LowCover, EOperativeStance::Crouching, false, true), 0.35f);
	TestEqual(TEXT("low cover standing: exposed above it"), AbsorbFraction(Config, ECoverHeight::LowCover, EOperativeStance::Standing, false, true), 0.f);
	TestEqual(TEXT("low cover prone: hidden like a wall (Sprint 08 60 cm rule)"), AbsorbFraction(Config, ECoverHeight::LowCover, EOperativeStance::Prone, false, true), 0.9f);
	TestEqual(TEXT("not in cover: nothing"), AbsorbFraction(Config, ECoverHeight::None, EOperativeStance::Crouching, false, true), 0.f);
	TestTrue(TEXT("100 damage -> 10 through a high wall"), FMath::IsNearlyEqual(ApplyAbsorb(100.f, 0.9f), 10.f, 0.001f));
	TestTrue(TEXT("flank: 100 stays 100"), FMath::IsNearlyEqual(ApplyAbsorb(100.f, 0.f), 100.f, 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoverColdTest, "CodexTactics.Tactics.Cover.ColdDrainReducedBehindWall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCoverColdTest::RunTest(const FString&)
{
	FColdConfig Config;
	TestEqual(TEXT("cover wind chill x0.5"), Config.CoverWindChillMultiplier, 0.5f);
	FColdEnvironment Open;
	FColdEnvironment Sheltered;
	Sheltered.bInCover = true;
	const float GainOpen = ColdRules::StepCold(Config, 20.f, 10.f, Open, EOperativeStance::Standing, 0.f) - 20.f;
	const float GainCover = ColdRules::StepCold(Config, 20.f, 10.f, Sheltered, EOperativeStance::Standing, 0.f) - 20.f;
	TestTrue(TEXT("open: 10 s at 1 %/s = +10"), FMath::IsNearlyEqual(GainOpen, 10.f, 0.001f));
	TestTrue(TEXT("behind the wall: half the drain (+5)"), FMath::IsNearlyEqual(GainCover, 5.f, 0.001f));
	// Elevated ground doubles the wind; the wall still halves it.
	Sheltered.bElevated = true;
	Open.bElevated = true;
	const float ElevatedOpen = ColdRules::StepCold(Config, 20.f, 10.f, Open, EOperativeStance::Standing, 0.f) - 20.f;
	const float ElevatedCover = ColdRules::StepCold(Config, 20.f, 10.f, Sheltered, EOperativeStance::Standing, 0.f) - 20.f;
	TestTrue(TEXT("ridge open: +20"), FMath::IsNearlyEqual(ElevatedOpen, 20.f, 0.001f));
	TestTrue(TEXT("ridge behind a wall: +10"), FMath::IsNearlyEqual(ElevatedCover, 10.f, 0.001f));
	// Warm zones ignore it (nothing to shelter from).
	Sheltered.bElevated = false;
	Sheltered.bWarm = true;
	TestTrue(TEXT("near heat the cold still drops"), ColdRules::StepCold(Config, 20.f, 1.f, Sheltered, EOperativeStance::Standing, 0.f) < 20.f);
	TestEqual(TEXT("helper: 0.5 in cover"), CoverRules::WindChillMultiplier(Config, true), 0.5f);
	TestEqual(TEXT("helper: 1 outside"), CoverRules::WindChillMultiplier(Config, false), 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoverBlindFireTest, "CodexTactics.Tactics.Cover.BlindFirePenaltyAndImmunity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCoverBlindFireTest::RunTest(const FString&)
{
	using namespace CoverRules;
	const FCoverCombatConfig Config;
	TestEqual(TEXT("cover blind fire -40 %"), Config.CoverBlindFireAccuracyMultiplier, 0.6f);
	TestEqual(TEXT("5 % floor"), Config.MinBlindFireHitChance, 0.05f);
	TestEqual(TEXT("0 % headshots while the head is down"), Config.CoverHeadshotChance, 0.f);
	TestTrue(TEXT("0.8 -> 0.48 blind from cover"), FMath::IsNearlyEqual(CoverBlindFireHitChance(Config, 0.8f), 0.48f, 0.001f));
	// The two blind mechanics are distinct and multiply (user decision 2026-10-06), floored at 5 %.
	TestTrue(TEXT("cover only: x0.6"), FMath::IsNearlyEqual(CombinedBlindFireHitChance(Config, 0.8f, true, false, 0.2f), 0.48f, 0.001f));
	TestTrue(TEXT("ghost only: x0.2 (Sprint 08)"), FMath::IsNearlyEqual(CombinedBlindFireHitChance(Config, 0.8f, false, true, 0.2f), 0.16f, 0.001f));
	TestTrue(TEXT("both: 0.8 x 0.6 x 0.2 = 0.096"), FMath::IsNearlyEqual(CombinedBlindFireHitChance(Config, 0.8f, true, true, 0.2f), 0.096f, 0.001f));
	TestTrue(TEXT("both from a poor base: floored at 0.05"), FMath::IsNearlyEqual(CombinedBlindFireHitChance(Config, 0.2f, true, true, 0.2f), 0.05f, 0.001f));
	TestTrue(TEXT("neither: untouched"), FMath::IsNearlyEqual(CombinedBlindFireHitChance(Config, 0.8f, false, false, 0.2f), 0.8f, 0.001f));
	// Immunity: in cover with the head down (blind fire keeps it down), not while leaning out.
	TestTrue(TEXT("high cover, head down: immune"), IsHeadshotImmune(ECoverHeight::HighCover, false));
	TestTrue(TEXT("low cover, head down: immune"), IsHeadshotImmune(ECoverHeight::LowCover, false));
	TestFalse(TEXT("leaning out: a headshot is possible"), IsHeadshotImmune(ECoverHeight::HighCover, true));
	TestFalse(TEXT("no cover: no immunity"), IsHeadshotImmune(ECoverHeight::None, false));
	TestEqual(TEXT("headshot chance 0 while immune"), HeadshotChance(Config, 0.3f, true), 0.f);
	TestEqual(TEXT("the attacker's chance otherwise"), HeadshotChance(Config, 0.3f, false), 0.3f);
	// Sight: high cover hides him from the wall's side while the head is down.
	TestTrue(TEXT("high cover, head down, observer behind the wall: unseen"), HiddenFromObserver(ECoverHeight::HighCover, false, true));
	TestFalse(TEXT("leaning out: seen"), HiddenFromObserver(ECoverHeight::HighCover, true, true));
	TestFalse(TEXT("observer on the open side: seen"), HiddenFromObserver(ECoverHeight::HighCover, false, false));
	TestFalse(TEXT("low cover: the Sprint 08 trace decides"), HiddenFromObserver(ECoverHeight::LowCover, false, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoverDecisionFireTest, "CodexTactics.Tactics.Cover.DecisionPeekVsBlindFire",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCoverDecisionFireTest::RunTest(const FString&)
{
	using namespace CoverDecisionRules;
	const FCoverDecisionConfig Config;
	auto Situation = [](float Health, float Suppression, float Damage, float DistanceM, bool bEdge = true, bool bLaser = false,
		ECoverHeight Height = ECoverHeight::HighCover)
	{
		FCoverFireSituation S;
		S.Height = Height;
		S.bEdgeExposed = bEdge;
		S.bSniperLaserOnMe = bLaser;
		S.HealthFraction = Health;
		S.SuppressionPressure = Suppression;
		S.RecentIncomingDamage = Damage;
		S.DistanceToEnemyCm = DistanceM * 100.f;
		return S;
	};
	// Mirror of jev_validate_cover.py (Jev agreed on every one of these, 2026-10-06).
	TestEqual(TEXT("calm, enemy 10 m: corner peek"), DecideFire(Config, Situation(0.9f, 0.f, 0.f, 10.f)), ECoverFireDecision::CornerPeek);
	TestEqual(TEXT("calm, enemy 22 m: corner peek"), DecideFire(Config, Situation(0.9f, 0.f, 0.f, 22.f)), ECoverFireDecision::CornerPeek);
	TestEqual(TEXT("heavy fire, hit hard, 9 m: blind fire"), DecideFire(Config, Situation(0.8f, 0.9f, 30.f, 9.f)), ECoverFireDecision::BlindFire);
	TestEqual(TEXT("heavy fire, 24 m (within blind reach): blind fire"), DecideFire(Config, Situation(0.8f, 0.9f, 30.f, 24.f)), ECoverFireDecision::BlindFire);
	TestEqual(TEXT("heavy fire, 30 m: blind fire futile, danger below extreme: peek"), DecideFire(Config, Situation(0.8f, 0.9f, 30.f, 30.f)), ECoverFireDecision::CornerPeek);
	TestEqual(TEXT("extreme danger, enemy 30 m out of blind reach: hold"), DecideFire(Config, Situation(0.5f, 1.f, 40.f, 30.f)), ECoverFireDecision::Hold);
	TestEqual(TEXT("badly wounded, light fire, 11 m: hold"), DecideFire(Config, Situation(0.2f, 0.3f, 10.f, 11.f)), ECoverFireDecision::Hold);
	TestEqual(TEXT("badly wounded, calm, 20 m: hold"), DecideFire(Config, Situation(0.25f, 0.f, 0.f, 20.f)), ECoverFireDecision::Hold);
	TestEqual(TEXT("badly wounded, enemy point-blank: blind fire (self-defence)"), DecideFire(Config, Situation(0.2f, 0.3f, 10.f, 4.f)), ECoverFireDecision::BlindFire);
	TestEqual(TEXT("point-blank, grazed under light fire: blind fire"), DecideFire(Config, Situation(0.9f, 0.3f, 15.f, 4.f)), ECoverFireDecision::BlindFire);
	TestEqual(TEXT("point-blank, calm: peek"), DecideFire(Config, Situation(0.9f, 0.f, 0.f, 4.f)), ECoverFireDecision::CornerPeek);
	TestEqual(TEXT("sniper laser, 12 m: hold"), DecideFire(Config, Situation(0.9f, 0.2f, 0.f, 12.f, true, true)), ECoverFireDecision::Hold);
	TestEqual(TEXT("sniper laser, 28 m: hold"), DecideFire(Config, Situation(0.9f, 0.2f, 0.f, 28.f, true, true)), ECoverFireDecision::Hold);
	TestEqual(TEXT("high wall without a corner: hold"), DecideFire(Config, Situation(0.9f, 0.5f, 0.f, 10.f, false)), ECoverFireDecision::Hold);
	TestEqual(TEXT("low cover, calm: peek (rise and fire over it)"), DecideFire(Config, Situation(0.9f, 0.1f, 0.f, 12.f, true, false, ECoverHeight::LowCover)), ECoverFireDecision::CornerPeek);
	TestEqual(TEXT("low cover, heavy fire: blind fire over the top"), DecideFire(Config, Situation(0.6f, 0.8f, 25.f, 8.f, true, false, ECoverHeight::LowCover)), ECoverFireDecision::BlindFire);
	TestEqual(TEXT("light fire, 13 m: peek"), DecideFire(Config, Situation(0.6f, 0.4f, 0.f, 13.f)), ECoverFireDecision::CornerPeek);
	TestTrue(TEXT("danger score: 0.5 x pressure + 0.5 x damage share"), FMath::IsNearlyEqual(DangerScore(Config, Situation(0.9f, 0.4f, 20.f, 10.f)), 0.45f, 0.001f));
	TestTrue(TEXT("two shooters = 0.7 pressure"), FMath::IsNearlyEqual(SuppressionFromShooters(Config, 2), 0.7f, 0.001f));
	TestTrue(TEXT("five shooters cap at 1"), FMath::IsNearlyEqual(SuppressionFromShooters(Config, 5), 1.f, 0.001f));
	TestTrue(TEXT("recent damage decays 40 HP over 5 s"), FMath::IsNearlyEqual(DecayRecentDamage(Config, 40.f, 2.5f), 20.f, 0.001f));
	TestEqual(TEXT("peek -> corner lean mode"), ToFireMode(ECoverFireDecision::CornerPeek), ECoverFireMode::CornerLean);
	TestEqual(TEXT("blind -> blind fire mode"), ToFireMode(ECoverFireDecision::BlindFire), ECoverFireMode::BlindFire);
	// User decision 2026-10-06: an enemy out in front of the wall gets a normal shot off the wall (no peek / blind fire),
	// also from a wall without a corner; the sniper laser still holds him down.
	FCoverFireSituation Front = Situation(0.9f, 0.9f, 30.f, 9.f);
	Front.bEnemyInFrontOfCover = true;
	TestEqual(TEXT("enemy in front of the wall: open shot (not blind fire)"), DecideFire(Config, Front), ECoverFireDecision::OpenShot);
	Front.bEdgeExposed = false;
	TestEqual(TEXT("enemy in front of a wall without a corner: open shot"), DecideFire(Config, Front), ECoverFireDecision::OpenShot);
	Front.bSniperLaserOnMe = true;
	TestEqual(TEXT("enemy in front, laser on him: hold"), DecideFire(Config, Front), ECoverFireDecision::Hold);
	TestTrue(TEXT("open shot name"), FCString::Strcmp(FireDecisionName(ECoverFireDecision::OpenShot), TEXT("OpenShot")) == 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoverDecisionStanceTest, "CodexTactics.Tactics.Cover.LaserForcesCrouch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCoverDecisionStanceTest::RunTest(const FString&)
{
	using namespace CoverDecisionRules;
	const FCoverDecisionConfig Config;
	auto Situation = [](ECoverHeight Height, bool bLaser, float Health, float Suppression, bool bElevated, bool bWantsFire)
	{
		FCoverStanceSituation S;
		S.Height = Height;
		S.bSniperLaserOnMe = bLaser;
		S.HealthFraction = Health;
		S.SuppressionPressure = Suppression;
		S.bEnemyElevated = bElevated;
		S.bWantsAimedFire = bWantsFire;
		return S;
	};
	TestEqual(TEXT("laser on him at a high wall: crouch at once"), DecideStance(Config, Situation(ECoverHeight::HighCover, true, 0.9f, 0.1f, false, true)), ECoverStanceDecision::Crouch);
	TestEqual(TEXT("laser on him at a low cover: crouch at once"), DecideStance(Config, Situation(ECoverHeight::LowCover, true, 0.9f, 0.f, false, true)), ECoverStanceDecision::Crouch);
	TestEqual(TEXT("high wall, calm: stand"), DecideStance(Config, Situation(ECoverHeight::HighCover, false, 0.9f, 0.f, false, true)), ECoverStanceDecision::Stand);
	TestEqual(TEXT("high wall, heavy fire: crouch"), DecideStance(Config, Situation(ECoverHeight::HighCover, false, 0.9f, 0.8f, false, true)), ECoverStanceDecision::Crouch);
	TestEqual(TEXT("high wall, badly wounded: crouch"), DecideStance(Config, Situation(ECoverHeight::HighCover, false, 0.2f, 0.1f, false, false)), ECoverStanceDecision::Crouch);
	TestEqual(TEXT("high wall, enemy on a rooftop: crouch"), DecideStance(Config, Situation(ECoverHeight::HighCover, false, 0.9f, 0.2f, true, false)), ECoverStanceDecision::Crouch);
	TestEqual(TEXT("low cover, wants to fire, calm: stand up over it"), DecideStance(Config, Situation(ECoverHeight::LowCover, false, 0.9f, 0.1f, false, true)), ECoverStanceDecision::Stand);
	TestEqual(TEXT("low cover, wants to fire under heavy fire: crouch"), DecideStance(Config, Situation(ECoverHeight::LowCover, false, 0.9f, 0.8f, false, true)), ECoverStanceDecision::Crouch);
	TestEqual(TEXT("low cover, waiting: crouch"), DecideStance(Config, Situation(ECoverHeight::LowCover, false, 0.9f, 0.1f, false, false)), ECoverStanceDecision::Crouch);
	TestEqual(TEXT("low cover, badly wounded, wants to fire: crouch"), DecideStance(Config, Situation(ECoverHeight::LowCover, false, 0.2f, 0.1f, false, true)), ECoverStanceDecision::Crouch);
	TestEqual(TEXT("stand -> standing"), ToStance(ECoverStanceDecision::Stand), EOperativeStance::Standing);
	TestEqual(TEXT("crouch -> crouching"), ToStance(ECoverStanceDecision::Crouch), EOperativeStance::Crouching);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
