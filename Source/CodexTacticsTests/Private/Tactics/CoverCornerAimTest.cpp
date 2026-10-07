// CodexTactics.Tactics.Cover.* — sustained corner aim (user request 2026-10-07; UE-only, no Godot reference): leaned out
// in the corner fire stance he keeps firing and only ducks back to reload, for safety or with no targets (the rule is
// Jev-calibrated by Scripts/Tools/jev_validate_corner_aim.py and mirrored here); the bisected wall-edge probing.

#include "Misc/AutomationTest.h"
#include "Tactics/CoverDecisionRules.h"
#include "Tactics/CoverTraceRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define CORNER_AIM_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Tactics.Cover." TestPath, EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace CornerAimTest
{
	FCornerAimSituation Duel(float Clip = 0.9f)
	{
		FCornerAimSituation S;
		S.ClipFraction = Clip;
		S.bHasReserve = true;
		S.SecondsWithoutTarget = 0.f;
		S.HealthFraction = 0.9f;
		S.SuppressionPressure = 0.35f; // the one enemy he duels fires back
		return S;
	}

	const TCHAR* Name(ECornerAimDecision Decision)
	{
		return CoverDecisionRules::CornerAimDecisionName(Decision);
	}
}

CORNER_AIM_TEST(FCoverSustainedCornerAimTest, "SustainedCornerAimMultipleShots")
bool FCoverSustainedCornerAimTest::RunTest(const FString&)
{
	using namespace CoverDecisionRules;
	const FCoverDecisionConfig Config;
	// A 30-round magazine, a shot every 0.65 s at an enemy round the corner, re-evaluated every 0.1 s: he stays out the
	// whole time the magazine lasts (no duck, no return between the shots).
	int32 Breaks = 0;
	int32 Shots = 0;
	for (int32 Round = 30; Round >= 1; --Round)
	{
		for (int32 Tick = 0; Tick < 5; ++Tick)
		{
			FCornerAimSituation S = CornerAimTest::Duel(Round / 30.f);
			S.SecondsWithoutTarget = Tick * 0.08f; // re-evaluated between the shots; the target is re-seen at each shot
			Breaks += DecideCornerAim(Config, S) != ECornerAimDecision::StayAndFire ? 1 : 0;
		}
		++Shots;
	}
	TestEqual(TEXT("30 shots from the held fire stance"), Shots, 30);
	TestEqual(TEXT("... no duck / return between them while a target is in sight"), Breaks, 0);
	// Switching targets / a target hidden for under the grace period keeps the stance.
	FCornerAimSituation Lost = CornerAimTest::Duel(0.8f);
	Lost.SecondsWithoutTarget = Config.AimNoTargetGraceSeconds - 0.1f;
	TestEqual(TEXT("target out of sight just under the grace: stays"), DecideCornerAim(Config, Lost), ECornerAimDecision::StayAndFire);
	Lost.SecondsWithoutTarget = Config.AimNoTargetGraceSeconds;
	TestEqual(TEXT("no target for the grace period: back to the wall"), DecideCornerAim(Config, Lost), ECornerAimDecision::ReturnNoTargets);
	Lost.bHoldWithoutTargets = true;
	TestEqual(TEXT("turn-based: the pose stays between his shots within his turn"), DecideCornerAim(Config, Lost), ECornerAimDecision::StayAndFire);
	return true;
}

CORNER_AIM_TEST(FCoverDuckBackOnlyToReloadTest, "DuckBackOnlyToReload")
bool FCoverDuckBackOnlyToReloadTest::RunTest(const FString&)
{
	using namespace CoverDecisionRules;
	const FCoverDecisionConfig Config;
	// A calm duel with the target always in sight: the only break is the empty magazine.
	for (int32 Round = 30; Round >= 1; --Round)
	{
		if (DecideCornerAim(Config, CornerAimTest::Duel(Round / 30.f)) != ECornerAimDecision::StayAndFire)
		{
			AddError(FString::Printf(TEXT("broke the aim with %d rounds left and a target in sight"), Round));
		}
	}
	TestEqual(TEXT("empty magazine: duck back and reload"), DecideCornerAim(Config, CornerAimTest::Duel(0.f)), ECornerAimDecision::DuckToReload);
	FCornerAimSituation Dry = CornerAimTest::Duel(0.f);
	Dry.bHasReserve = false;
	TestEqual(TEXT("empty, no spare rounds: duck back (switch weapons behind the corner)"), DecideCornerAim(Config, Dry), ECornerAimDecision::DuckForSafety);
	// A lull with a low magazine: reload early; with a fuller one: stay out.
	FCornerAimSituation Lull = CornerAimTest::Duel(0.2f);
	Lull.SecondsWithoutTarget = 1.f;
	TestEqual(TEXT("low magazine in a lull: reload early"), DecideCornerAim(Config, Lull), ECornerAimDecision::DuckToReload);
	Lull.ClipFraction = 0.6f;
	TestEqual(TEXT("half magazine in a short lull: stays out"), DecideCornerAim(Config, Lull), ECornerAimDecision::StayAndFire);
	return true;
}

CORNER_AIM_TEST(FCoverCornerAimJevRulesTest, "CornerAimDecisionJevRules")
bool FCoverCornerAimJevRulesTest::RunTest(const FString&)
{
	using namespace CoverDecisionRules;
	const FCoverDecisionConfig Config;
	struct FCase
	{
		const TCHAR* Id;
		float Clip;
		bool bReserve;
		float NoTarget;
		float Health;
		float Suppression;
		float Damage;
		bool bLaser;
		bool bGrenade;
		bool bFlank;
		ECornerAimDecision Expected;
	};
	using E = ECornerAimDecision;
	// Mirror of jev_validate_corner_aim.py SCENARIOS and code_decision (2026-10-07: Jev agreed on 19 / 24 = 79 %; the
	// disagreements a_low_but_target, a_half_lull, a_target_just_lost, a_empty_no_reserve, a_full_lull_short are at Jev
	// confidence <= 0.49 and kept by design: no duck while a target is in sight, a grace before relaxing).
	const FCase Cases[] = {
		{ TEXT("a_duel_full"), 0.9f, true, 0.f, 0.9f, 0.35f, 0.f, false, false, false, E::StayAndFire },
		{ TEXT("a_duel_half"), 0.5f, true, 0.f, 0.8f, 0.35f, 0.f, false, false, false, E::StayAndFire },
		{ TEXT("a_two_targets_calm"), 0.7f, true, 0.f, 0.9f, 0.f, 0.f, false, false, false, E::StayAndFire },
		{ TEXT("a_grazed_keeps"), 0.6f, true, 0.f, 0.7f, 0.35f, 12.f, false, false, false, E::StayAndFire },
		{ TEXT("a_wounded_steady"), 0.6f, true, 0.f, 0.5f, 0.35f, 0.f, false, false, false, E::StayAndFire },
		{ TEXT("a_low_but_target"), 0.2f, true, 0.f, 0.9f, 0.35f, 0.f, false, false, false, E::StayAndFire },
		{ TEXT("a_empty"), 0.f, true, 0.f, 0.9f, 0.35f, 0.f, false, false, false, E::DuckToReload },
		{ TEXT("a_empty_heavy_fire"), 0.f, true, 0.f, 0.9f, 0.8f, 0.f, false, false, false, E::DuckToReload },
		{ TEXT("a_low_lull"), 0.2f, true, 1.f, 0.9f, 0.f, 0.f, false, false, false, E::DuckToReload },
		{ TEXT("a_half_lull"), 0.5f, true, 1.f, 0.9f, 0.f, 0.f, false, false, false, E::StayAndFire },
		{ TEXT("a_target_just_lost"), 0.8f, true, 0.4f, 0.9f, 0.f, 0.f, false, false, false, E::StayAndFire },
		{ TEXT("a_no_targets_long"), 0.8f, true, 5.f, 0.9f, 0.f, 0.f, false, false, false, E::ReturnNoTargets },
		{ TEXT("a_no_targets_low_clip"), 0.2f, true, 5.f, 0.9f, 0.f, 0.f, false, false, false, E::DuckToReload },
		{ TEXT("a_heavy_fire"), 0.7f, true, 0.f, 0.8f, 0.9f, 10.f, false, false, false, E::DuckForSafety },
		{ TEXT("a_big_hit"), 0.7f, true, 0.f, 0.6f, 0.35f, 40.f, false, false, false, E::DuckForSafety },
		{ TEXT("a_badly_wounded"), 0.7f, true, 0.f, 0.2f, 0.35f, 0.f, false, false, false, E::DuckForSafety },
		{ TEXT("a_laser"), 0.7f, true, 0.f, 0.9f, 0.f, 0.f, true, false, false, E::DuckForSafety },
		{ TEXT("a_grenade"), 0.7f, true, 0.f, 0.9f, 0.35f, 0.f, false, true, false, E::DuckForSafety },
		{ TEXT("a_flank"), 0.7f, true, 0.f, 0.9f, 0.35f, 0.f, false, false, true, E::DuckForSafety },
		{ TEXT("a_empty_no_reserve"), 0.f, false, 1.f, 0.9f, 0.f, 0.f, false, false, false, E::DuckForSafety },
		{ TEXT("a_wounded_grazed_duel"), 0.6f, true, 0.f, 0.45f, 0.35f, 15.f, false, false, false, E::DuckForSafety },
		{ TEXT("a_heavy_fire_no_target"), 0.7f, true, 3.f, 0.9f, 0.8f, 0.f, false, false, false, E::DuckForSafety },
		{ TEXT("a_full_lull_short"), 0.9f, true, 1.f, 0.9f, 0.f, 0.f, false, false, false, E::StayAndFire },
		{ TEXT("a_low_target_heavy"), 0.2f, true, 0.f, 0.9f, 0.8f, 0.f, false, false, false, E::DuckToReload },
	};
	for (const FCase& Case : Cases)
	{
		FCornerAimSituation S;
		S.ClipFraction = Case.Clip;
		S.bHasReserve = Case.bReserve;
		S.SecondsWithoutTarget = Case.NoTarget;
		S.HealthFraction = Case.Health;
		S.SuppressionPressure = Case.Suppression;
		S.RecentIncomingDamage = Case.Damage;
		S.bSniperLaserOnMe = Case.bLaser;
		S.bGrenadeNearby = Case.bGrenade;
		S.bFlankEnemyNear = Case.bFlank;
		const ECornerAimDecision Got = DecideCornerAim(Config, S);
		if (Got != Case.Expected)
		{
			AddError(FString::Printf(TEXT("%s: %s, expected %s"), Case.Id, CornerAimTest::Name(Got), CornerAimTest::Name(Case.Expected)));
		}
	}
	TestTrue(TEXT("names"), FCString::Strcmp(CornerAimDecisionName(E::DuckToReload), TEXT("DuckToReload")) == 0);
	return true;
}

CORNER_AIM_TEST(FCoverCornerHoldVsHordeTest, "CornerHoldVsMeleeHorde")
bool FCoverCornerHoldVsHordeTest::RunTest(const FString&)
{
	using namespace CoverDecisionRules;
	const FCoverDecisionConfig Config;
	// 2026-10-07 horde regression (Jev re-validated 25 / 31 = 81 %): melee mutants rushing round the corner are targets —
	// they neither suppress nor flank, and wounds alone do not send him behind a corner that does not stop them.
	FCornerAimSituation Horde;
	Horde.ClipFraction = 0.7f;
	Horde.bRangedThreatPresent = false;
	Horde.MeleeRushers = 6;
	const TCHAR* Reason = nullptr;
	TestEqual(TEXT("h_melee_horde: holds and fires"), DecideCornerAim(Config, Horde, &Reason), ECornerAimDecision::StayAndFire);
	Horde.HealthFraction = 0.3f;
	TestEqual(TEXT("h_melee_horde_wounded: holds (the corner does not stop a horde)"), DecideCornerAim(Config, Horde), ECornerAimDecision::StayAndFire);
	Horde.HealthFraction = 0.8f;
	Horde.RecentIncomingDamage = 0.f; // the melee bites are not ranged damage
	TestEqual(TEXT("bitten (melee damage only): holds"), DecideCornerAim(Config, Horde), ECornerAimDecision::StayAndFire);
	Horde.ClipFraction = 0.f;
	TestEqual(TEXT("horde, empty magazine: reloads behind the corner"), DecideCornerAim(Config, Horde, &Reason), ECornerAimDecision::DuckToReload);
	TestTrue(TEXT("... reason"), FCString::Strcmp(Reason, TEXT("magazine empty")) == 0);
	// Ranged fire still breaks it.
	FCornerAimSituation Shooters;
	Shooters.ClipFraction = 0.7f;
	Shooters.SuppressionPressure = 0.9f;
	Shooters.RecentIncomingDamage = 15.f;
	TestEqual(TEXT("h_shooters_many: ducks"), DecideCornerAim(Config, Shooters, &Reason), ECornerAimDecision::DuckForSafety);
	TestTrue(TEXT("... heavy ranged fire"), FCString::Strcmp(Reason, TEXT("heavy ranged fire (2+ shooters)")) == 0);
	FCornerAimSituation OneSpitter;
	OneSpitter.ClipFraction = 0.7f;
	OneSpitter.SuppressionPressure = 0.35f;
	OneSpitter.MeleeRushers = 5;
	TestEqual(TEXT("h_horde_one_spitter: holds"), DecideCornerAim(Config, OneSpitter), ECornerAimDecision::StayAndFire);
	FCornerAimSituation Flank = OneSpitter;
	Flank.bFlankEnemyNear = true; // a gunman on his open side
	TestEqual(TEXT("h_gunman_flank: ducks"), DecideCornerAim(Config, Flank, &Reason), ECornerAimDecision::DuckForSafety);
	return true;
}

CORNER_AIM_TEST(FCoverEdgeProbePrecisionTest, "EdgeProbePrecision")
bool FCoverEdgeProbePrecisionTest::RunTest(const FString&)
{
	using namespace CoverTraceRules;
	const FCoverTraceConfig Config;
	// User decision 2026-10-07: the coarse 40 cm probes bracket the edge, the bisection pins it within a few cm.
	for (const float RealEdge : { 3.f, 37.5f, 50.f, 79.9f, 137.f, 199.f })
	{
		auto WallAt = [RealEdge](float DistanceCm) { return DistanceCm < RealEdge; };
		TArray<bool> Hits;
		for (int32 Probe = 1; Probe <= Config.MaxEdgeProbes; ++Probe)
		{
			Hits.Add(WallAt(Probe * Config.EdgeProbeStepCm));
		}
		float Coarse = 0.f;
		const bool bExposed = EdgeExposedFromProbes(Hits, Config.EdgeProbeStepCm, Coarse);
		TestTrue(FString::Printf(TEXT("edge at %.1f: exposed"), RealEdge), bExposed);
		const float Refined = RefineEdgeDistance(Coarse - Config.EdgeProbeStepCm, Coarse, WallAt, Config.EdgeRefineIterations);
		TestTrue(FString::Printf(TEXT("edge at %.1f: coarse %.0f, refined %.1f (within 3 cm)"), RealEdge, Coarse, Refined),
			FMath::Abs(Refined - RealEdge) <= 3.f);
	}
	// The old coarse answer could be 40 cm long (the case where the _R clip's 40 cm lean did not clear the edge).
	TestTrue(TEXT("bisection bracket after 4 steps: 40 / 16 = 2.5 cm"), Config.EdgeRefineIterations >= 4);
	return true;
}

#undef CORNER_AIM_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
