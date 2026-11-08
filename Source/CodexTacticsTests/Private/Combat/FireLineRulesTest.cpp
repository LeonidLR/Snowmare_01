// CodexTactics.Combat.FireLine.* / CodexTactics.AI.PmcCommander.NoSuppressWithoutFireLine - fix/pmc-fire-through-wall (game designer PIE
// 2026-11-08, L_Gen_Depot_1 zone Z_ST_south: a hostile PMC fighter lying prone in the open kept firing at the squad standing behind a solid
// wall, the tracers straight into the wall). UE-only, no Godot reference. One model for the squad, the PMC and the armed enemies:
//  - a target is acquired only through honest sight: a heard shot / a muzzle flash without a clear line makes it known, not seen
//    (SightRules::ResolveContact);
//  - fire at a point needs a line of fire from the muzzle that reaches near the point (SuppressionRules::IsFireLineUseful);
//  - the commander does not pick a suppress order without that line (PmcCommanderRules: FPmcFighterView::bFireLine).

#include "AI/PmcCommanderRules.h"
#include "Combat/SightRules.h"
#include "Combat/SuppressionRules.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFireLineUsefulTest, "CodexTactics.Combat.FireLine.Useful",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFireLineUsefulTest::RunTest(const FString&)
{
	const FSuppressionConfig Config; // the shipped values: clear up to 2.5 m short of the point, and at least half the way
	TestEqual(TEXT("defaults"), Config.OrderLineNearPointCm, 250.f);
	TestEqual(TEXT("defaults"), Config.OrderLineMinClearFraction, 0.5f);
	// A clear line, or one stopped only by the target's own cover right in front of him: the suppression's point.
	TestTrue(TEXT("clear line"), SuppressionRules::IsFireLineUseful(Config, 3000.f, -1.f));
	TestTrue(TEXT("stopped by his cover 1 m in front of him (29 of 30 m)"), SuppressionRules::IsFireLineUseful(Config, 3000.f, 2900.f));
	TestTrue(TEXT("stopped 2.5 m short of the point"), SuppressionRules::IsFireLineUseful(Config, 3000.f, 2750.f));
	TestTrue(TEXT("a 4 m shot stopped 1.5 m short of the point (more than half the way)"), SuppressionRules::IsFireLineUseful(Config, 400.f, 250.f));
	TestFalse(TEXT("a 4 m shot stopped at his muzzle's first metre"), SuppressionRules::IsFireLineUseful(Config, 400.f, 100.f));
	// The designer's case: the wall the squad stands behind, 4 m in front of them - 20 of 24 m, 10 of 25 m, or right at his muzzle.
	TestFalse(TEXT("a wall 4 m in front of the squad, 20 m of 24 m: no round"), SuppressionRules::IsFireLineUseful(Config, 2400.f, 2000.f));
	TestFalse(TEXT("a wall 10 m out of 25 m: no round"), SuppressionRules::IsFireLineUseful(Config, 2500.f, 1000.f));
	TestFalse(TEXT("blocked at the muzzle (a prone muzzle in a kerb)"), SuppressionRules::IsFireLineUseful(Config, 2500.f, 0.f));
	FSuppressionConfig Strict = Config;
	Strict.OrderLineMinClearFraction = 1.f;
	Strict.OrderLineNearPointCm = 0.f;
	TestFalse(TEXT("a strict config: any block stops the round"), SuppressionRules::IsFireLineUseful(Strict, 3000.f, 2999.f));
	TestTrue(TEXT("a strict config: a clear line still fires"), SuppressionRules::IsFireLineUseful(Strict, 3000.f, -1.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFireLineHonestContactTest, "CodexTactics.Combat.FireLine.HonestContact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFireLineHonestContactTest::RunTest(const FString&)
{
	using SightRules::EContact;
	TestTrue(TEXT("seen by the eye rules"), SightRules::ResolveContact(true, false, false) == EContact::Seen);
	TestTrue(TEXT("seen by the eye rules (a shot heard too)"), SightRules::ResolveContact(true, true, false) == EContact::Seen);
	TestTrue(TEXT("a shot heard with a clear line to him / his flash: seen"), SightRules::ResolveContact(false, true, true) == EContact::Seen);
	// The bug: the squad fired behind a wall 60 m off - the old rule made them "seen" by the PMC team, a target for its fire.
	TestTrue(TEXT("a shot heard behind a wall: known (his spot), not a target"), SightRules::ResolveContact(false, true, false) == EContact::Heard);
	TestTrue(TEXT("a clear line alone tells nothing (the eye rules decide)"), SightRules::ResolveContact(false, false, true) == EContact::None);
	TestTrue(TEXT("nothing"), SightRules::ResolveContact(false, false, false) == EContact::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcCommanderNoSuppressWithoutFireLineTest, "CodexTactics.AI.PmcCommander.NoSuppressWithoutFireLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcCommanderNoSuppressWithoutFireLineTest::RunTest(const FString&)
{
	const FPmcWeights W;
	// The FlankVsFrontal case where Suppress wins (a mate already flanking): a seen threat 12 m out, in range.
	FPmcFighterView F;
	F.bHasThreat = true;
	F.Threat.Location = FVector(1200.f, 0.f, 0.f);
	F.Threat.DistanceCm = 1200.f;
	F.Threat.bSeenNow = true;
	FPmcSquadView S;
	S.Alive = 3;
	S.EnemiesKnown = 3;
	S.EnemiesSeenNow = 3;
	S.bContact = true;
	S.bEverContact = true;
	S.MatesFlanking = 1;
	const TArray<FPmcOrderScore> With = PmcCommanderRules::ScoreAll(F, S, W);
	TestEqual(TEXT("with a line of fire he suppresses"), PmcCommanderRules::OrderName(With[0].Order), TEXT("Suppress"));
	// A wall between his (prone) muzzle and the threat's spot: no suppress order - another order (a move or a hold) takes over.
	F.bFireLine = false;
	TestEqual(TEXT("no line of fire: Suppress is worth nothing"), PmcCommanderRules::ScoreOrder(EPmcOrder::Suppress, F, S, W), 0.f);
	const TArray<FPmcOrderScore> Without = PmcCommanderRules::ScoreAll(F, S, W);
	TestTrue(*FString::Printf(TEXT("no line of fire: he does something else (%s)"), PmcCommanderRules::OrderName(Without[0].Order)),
		Without[0].Order != EPmcOrder::Suppress && Without[0].Score > 0.f);
	// Heard, not seen (the squad behind the wall fired): no suppress / flank of a memory; he moves on it (Advance) rather than firing.
	FPmcFighterView Heard = F;
	Heard.bFireLine = true;
	Heard.Threat.bSeenNow = false;
	Heard.Threat.SecondsSince = 0.f;
	Heard.Threat.Location = FVector(4000.f, 0.f, 0.f);
	Heard.Threat.DistanceCm = 4000.f;
	TestEqual(TEXT("a heard threat is no suppress target"), PmcCommanderRules::ScoreOrder(EPmcOrder::Suppress, Heard, S, W), 0.f);
	TestTrue(TEXT("a heard threat beyond his band: he advances on it"), PmcCommanderRules::ScoreOrder(EPmcOrder::Advance, Heard, S, W) > 0.f);
	return true;
}

#endif
