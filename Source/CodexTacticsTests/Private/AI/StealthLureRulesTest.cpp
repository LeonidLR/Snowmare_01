// CodexTactics.AI.StealthLure.* — stealth lure rules (user decisions 2026-10-18; UE-only, no Godot reference): who hears a
// whistle / thrown stone (radius, walls muffle it like footsteps, heightened while searching, only patrol / guard duty), the
// combat / detection gate, the per-operative cooldowns, the approach pace, the data file, the noise round walls (walking path) and
// the re-target rule (a newer noise wins over the point it already investigates).

#include "AI/StealthLureRules.h"
#include "Data/EnemyPerception.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"

#if WITH_DEV_AUTOMATION_TESTS

#define STEALTH_LURE_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.AI.StealthLure." TestPath, EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace StealthLureTest
{
	FStealthLureListener Listener(float DistanceCm, int32 Walls = 0, float Occlusion = 0.5f)
	{
		FStealthLureListener Out;
		Out.DistanceCm = DistanceCm;
		Out.Walls = Walls;
		Out.OcclusionPerWall = Occlusion;
		return Out;
	}
}

STEALTH_LURE_TEST(FStealthLureRadiusWallsTest, "RadiusAndWalls")
bool FStealthLureRadiusWallsTest::RunTest(const FString& Parameters)
{
	using namespace StealthLureRules;
	using StealthLureTest::Listener;
	const FStealthLureParams P;
	const float Whistle = GetRadius(P, EStealthLureKind::Whistle);
	const float Throw = GetRadius(P, EStealthLureKind::Throw);
	TestEqual(TEXT("whistle radius 12 m"), Whistle, 1200.f);
	TestEqual(TEXT("throw radius 8 m"), Throw, 800.f);
	const float Search = 1.25f;

	// Open air.
	TestTrue(TEXT("whistle heard at 11.9 m"), Hears(Listener(1190.f), Whistle, Search));
	TestFalse(TEXT("whistle not heard at 12.1 m"), Hears(Listener(1210.f), Whistle, Search));
	TestTrue(TEXT("stone heard at 7.9 m"), Hears(Listener(790.f), Throw, Search));
	TestFalse(TEXT("stone not heard at 8.1 m"), Hears(Listener(810.f), Throw, Search));

	// Walls: the footstep model (x HearingOcclusionPerWall per wall, at most 3 count).
	TestTrue(TEXT("one wall halves the whistle: 5.9 m heard"), Hears(Listener(590.f, 1), Whistle, Search));
	TestFalse(TEXT("one wall halves the whistle: 8 m not heard"), Hears(Listener(800.f, 1), Whistle, Search));
	TestEqual(TEXT("two walls: 3 m"), EffectiveRadius(Listener(0.f, 2), Whistle, Search), 300.f, 0.01f);
	TestEqual(TEXT("ten walls count as three: 1.5 m"), EffectiveRadius(Listener(0.f, 10), Whistle, Search), 150.f, 0.01f);
	TestEqual(TEXT("an archetype that hears through walls (factor 1)"), EffectiveRadius(Listener(0.f, 2, 1.f), Whistle, Search), 1200.f, 0.01f);

	// Already searching: heightened hearing (x the search perception multiplier, like every patrol sense).
	FStealthLureListener Searching = Listener(1400.f);
	Searching.bSearching = true;
	TestTrue(TEXT("searching: a whistle 14 m off is heard (12 x 1.25 = 15 m)"), Hears(Searching, Whistle, Search));
	TestFalse(TEXT("not searching: 14 m is too far"), Hears(Listener(1400.f), Whistle, Search));
	return true;
}

STEALTH_LURE_TEST(FStealthLureEveryListenerTest, "EveryEnemyInRadiusComes")
bool FStealthLureEveryListenerTest::RunTest(const FString& Parameters)
{
	using namespace StealthLureRules;
	using StealthLureTest::Listener;
	const float Radius = GetRadius(FStealthLureParams(), EStealthLureKind::Whistle);
	// User correction 2026-10-18: a hound + a marksman together both come; a horde in range = all of them.
	TArray<FStealthLureListener> Pack;
	for (int32 Index = 0; Index < 12; ++Index)
	{
		Pack.Add(Listener(200.f + Index * 80.f)); // 2 .. 10.8 m, all inside
	}
	int32 Coming = 0;
	for (const FStealthLureListener& Enemy : Pack)
	{
		Coming += Hears(Enemy, Radius, 1.25f) ? 1 : 0;
	}
	TestEqual(TEXT("all 12 enemies inside the radius come (no 'only one')"), Coming, 12);

	FStealthLureListener Dead = Listener(300.f);
	Dead.bAlive = false;
	FStealthLureListener Hunting = Listener(300.f);
	Hunting.bOnPatrol = false;
	FStealthLureListener Held = Listener(300.f);
	Held.bTurnBasedHeld = true;
	TestFalse(TEXT("a dead enemy does not come"), Hears(Dead, Radius, 1.25f));
	TestFalse(TEXT("an enemy already hunting the squad (off patrol) ignores a lure"), Hears(Hunting, Radius, 1.25f));
	TestFalse(TEXT("an enemy held by a turn-based fight ignores it"), Hears(Held, Radius, 1.25f));
	TestFalse(TEXT("an invalid distance hears nothing"), Hears(Listener(-1.f), Radius, 1.25f));
	return true;
}

STEALTH_LURE_TEST(FStealthLureGateTest, "CombatDetectionCooldownGate")
bool FStealthLureGateTest::RunTest(const FString& Parameters)
{
	using namespace StealthLureRules;
	FStealthLureGate Gate;
	TestEqual(TEXT("exploring, undetected, ready: available"), static_cast<int32>(CheckAvailability(Gate)), static_cast<int32>(EStealthLureRefusal::None));

	for (const ECodexGamePhase Phase : { ECodexGamePhase::Cutscene, ECodexGamePhase::Preparation, ECodexGamePhase::WaveCombat,
		ECodexGamePhase::WaveCleared, ECodexGamePhase::PostCombat, ECodexGamePhase::GameOver })
	{
		FStealthLureGate Fight = Gate;
		Fight.Phase = Phase;
		TestEqual(FString::Printf(TEXT("phase %s: refused (combat)"), *UEnum::GetValueAsString(Phase)), static_cast<int32>(CheckAvailability(Fight)), static_cast<int32>(EStealthLureRefusal::Combat));
	}
	for (const ECodexCombatMode Mode : { ECodexCombatMode::RealTime, ECodexCombatMode::TacticalPause, ECodexCombatMode::TurnBased })
	{
		FStealthLureGate Fight = Gate;
		Fight.Phase = ECodexGamePhase::WaveCombat;
		Fight.CombatMode = Mode;
		TestEqual(FString::Printf(TEXT("fight in %s: refused (combat)"), *UEnum::GetValueAsString(Mode)), static_cast<int32>(CheckAvailability(Fight)), static_cast<int32>(EStealthLureRefusal::Combat));
	}
	FStealthLureGate Detected = Gate;
	Detected.bSquadDetected = true;
	TestEqual(TEXT("an enemy hunts the squad: refused (detected)"), static_cast<int32>(CheckAvailability(Detected)), static_cast<int32>(EStealthLureRefusal::Detected));
	FStealthLureGate Cooling = Gate;
	Cooling.CooldownLeftSeconds = 3.f;
	TestEqual(TEXT("cooldown running: refused (cooldown)"), static_cast<int32>(CheckAvailability(Cooling)), static_cast<int32>(EStealthLureRefusal::Cooldown));
	FStealthLureGate Unable = Gate;
	Unable.bOperativeAble = false;
	Unable.bSquadDetected = true;
	TestEqual(TEXT("a downed operative: refused (unable) first"), static_cast<int32>(CheckAvailability(Unable)), static_cast<int32>(EStealthLureRefusal::Unable));
	FStealthLureGate Both = Detected;
	Both.Phase = ECodexGamePhase::WaveCombat;
	Both.CombatMode = ECodexCombatMode::RealTime;
	Both.CooldownLeftSeconds = 2.f;
	TestEqual(TEXT("combat wins over detection and cooldown"), static_cast<int32>(CheckAvailability(Both)), static_cast<int32>(EStealthLureRefusal::Combat));

	// "Detected" = an enemy off patrol that knows of the squad.
	TestTrue(TEXT("off patrol with a target: hunting"), IsHuntingSquad(true, false, true, false, false));
	TestTrue(TEXT("off patrol with an alarm point: hunting"), IsHuntingSquad(true, false, false, true, false));
	TestTrue(TEXT("a horde member knows the squad: hunting"), IsHuntingSquad(true, false, false, false, true));
	TestFalse(TEXT("on patrol (searching included): not hunting"), IsHuntingSquad(true, true, true, true, true));
	TestFalse(TEXT("dead: not hunting"), IsHuntingSquad(false, false, true, true, true));
	TestFalse(TEXT("idle off patrol, knows nothing: not hunting"), IsHuntingSquad(true, false, false, false, false));
	return true;
}

STEALTH_LURE_TEST(FStealthLureCooldownTest, "CooldownAndLabels")
bool FStealthLureCooldownTest::RunTest(const FString& Parameters)
{
	using namespace StealthLureRules;
	const FStealthLureParams P;
	TestEqual(TEXT("whistle cooldown 10 s"), GetCooldown(P, EStealthLureKind::Whistle), 10.f);
	TestEqual(TEXT("throw cooldown 6 s"), GetCooldown(P, EStealthLureKind::Throw), 6.f);
	TestEqual(TEXT("4 s left"), CooldownLeft(104.0, 100.0), 4.f, 0.001f);
	TestEqual(TEXT("over: 0, never negative"), CooldownLeft(90.0, 100.0), 0.f);
	TestEqual(TEXT("throw range standing 15 m"), GetThrowRange(P, EOperativeStance::Standing), 1500.f);
	TestEqual(TEXT("throw range crouched 11.25 m"), GetThrowRange(P, EOperativeStance::Crouching), 1125.f);
	TestEqual(TEXT("throw range prone 7.5 m"), GetThrowRange(P, EOperativeStance::Prone), 750.f);

	TestEqual(TEXT("ready label"), GetButtonLabel(EStealthLureKind::Whistle, EStealthLureRefusal::None, 0.f, TEXT("5")).ToString(), FString(TEXT("WHISTLE\n[5]")));
	TestEqual(TEXT("cooldown label rounds up"), GetButtonLabel(EStealthLureKind::Throw, EStealthLureRefusal::Cooldown, 3.2f, TEXT("6")).ToString(), FString(TEXT("THROW\n4s")));
	TestEqual(TEXT("fight label"), GetButtonLabel(EStealthLureKind::Throw, EStealthLureRefusal::Combat, 0.f, TEXT("6")).ToString(), FString(TEXT("THROW\n--")));
	TestTrue(TEXT("no refusal text when available"), GetRefusalText(EStealthLureKind::Whistle, EStealthLureRefusal::None, 0.f).IsEmpty());
	for (const EStealthLureRefusal Refusal : { EStealthLureRefusal::Unable, EStealthLureRefusal::Combat, EStealthLureRefusal::Detected, EStealthLureRefusal::Cooldown })
	{
		TestFalse(FString::Printf(TEXT("a reason for %s"), *UEnum::GetValueAsString(Refusal)), GetRefusalText(EStealthLureKind::Throw, Refusal, 2.f).IsEmpty());
	}
	TestEqual(TEXT("feed line whistle"), GetFeedLine(EStealthLureKind::Whistle).ToString(), FString(TEXT("Whistle.")));
	TestEqual(TEXT("feed line throw"), GetFeedLine(EStealthLureKind::Throw).ToString(), FString(TEXT("Stone thrown.")));
	return true;
}

STEALTH_LURE_TEST(FStealthLureApproachTest, "ApproachPace")
bool FStealthLureApproachTest::RunTest(const FString& Parameters)
{
	using namespace StealthLureRules;
	// Hounds come fast, Brutes slowly: a fraction of their own speed, never below the patrol pace.
	TestEqual(TEXT("hound 600 cm/s x 0.75"), GetApproachSpeed(190.f, 600.f, 0.75f, 304.f), 450.f, 0.01f);
	TestEqual(TEXT("brute 230 cm/s x 0.75 = 172 -> its patrol pace 190"), GetApproachSpeed(190.f, 230.f, 0.75f, 230.f), 190.f, 0.01f);
	TestEqual(TEXT("a body slower than its patrol pace walks its own speed"), GetApproachSpeed(190.f, 150.f, 0.75f, 190.f), 150.f, 0.01f);
	TestEqual(TEXT("fraction 0: the trap search pace"), GetApproachSpeed(190.f, 600.f, 0.f, 304.f), 304.f, 0.01f);
	TestTrue(TEXT("exploring = no combat mode"), IsExploring(ECodexGamePhase::Exploration, ECodexCombatMode::None));
	TestFalse(TEXT("an exploration phase with a combat mode is not exploring"), IsExploring(ECodexGamePhase::Exploration, ECodexCombatMode::RealTime));
	return true;
}

STEALTH_LURE_TEST(FStealthLureDataFileTest, "DataFile")
bool FStealthLureDataFileTest::RunTest(const FString& Parameters)
{
	FString Text;
	if (!TestTrue(TEXT("enemy_perception.json readable"), FFileHelper::LoadFileToString(Text, *EnemyPerception::GetDefaultPath())))
	{
		return false;
	}
	TestTrue(TEXT("enemy_perception.json applies"), EnemyPerception::ApplyJson(Text));
	const FStealthLureParams L = EnemyPerception::GetLure();
	TestEqual(TEXT("whistle 12 m"), L.WhistleRadiusCm, 1200.f, 0.01f);
	TestEqual(TEXT("throw 8 m"), L.ThrowRadiusCm, 800.f, 0.01f);
	TestEqual(TEXT("throw range 15 m"), L.ThrowRangeCm, 1500.f, 0.01f);
	TestEqual(TEXT("whistle cooldown 10 s"), L.WhistleCooldownSeconds, 10.f, 0.01f);
	TestEqual(TEXT("throw cooldown 6 s"), L.ThrowCooldownSeconds, 6.f, 0.01f);
	TestEqual(TEXT("investigate 60 s"), L.InvestigateSeconds, 60.f, 0.01f);
	TestEqual(TEXT("sweep 4 m"), L.InvestigateSweepRadiusCm, 400.f, 0.01f);
	TestEqual(TEXT("approach x0.75"), L.ApproachSpeedFraction, 0.75f, 0.001f);
	TestEqual(TEXT("listen 0.75 s before walking to the noise"), L.ListenSeconds, 0.75f, 0.001f);
	// A file without the section keeps the defaults (older data / a level override).
	TestTrue(TEXT("a file without \"lure\" applies"), EnemyPerception::ApplyJson(TEXT("{ \"search\": { \"duration_seconds\": 30 } }")));
	TestEqual(TEXT("no section: default whistle radius"), EnemyPerception::GetLure().WhistleRadiusCm, FStealthLureParams().WhistleRadiusCm);
	EnemyPerception::ResetToDefaults();
	return true;
}

// Fix 2026-10-18 (user report: the whistle only made them turn on the spot): the walk to a lure ends where the enemy can get to.
STEALTH_LURE_TEST(FStealthLureInvestigatePointTest, "InvestigatePoint")
bool FStealthLureInvestigatePointTest::RunTest(const FString& Parameters)
{
	using namespace StealthLureRules;
	const FVector Noise(1000.f, 500.f, 2.f);          // the whistler's feet
	const FVector OnNav(1010.f, 505.f, 12.f);         // projected onto the navmesh
	const FVector PartialEnd(600.f, 480.f, 10.f);     // the closest reachable point (behind a wall without a way round)
	TestEqual(TEXT("reachable: the noise on the navmesh"), ChooseInvestigatePoint(Noise, true, OnNav, ELureGoalPath::Full, OnNav), OnNav);
	TestEqual(TEXT("partial path: as close as it gets (not the unreachable point)"), ChooseInvestigatePoint(Noise, true, OnNav, ELureGoalPath::Partial, PartialEnd), PartialEnd);
	TestEqual(TEXT("no path yet: the navmesh point (the move tries, refusals re-path)"), ChooseInvestigatePoint(Noise, true, OnNav, ELureGoalPath::None, OnNav), OnNav);
	TestEqual(TEXT("off the navmesh: the raw noise"), ChooseInvestigatePoint(Noise, false, OnNav, ELureGoalPath::Full, PartialEnd), Noise);
	TestEqual(TEXT("lure walk arrives within 2 m (the whistler may stand on the point)"), GetArriveRadius(150.f, true), 200.f, 0.01f);
	TestEqual(TEXT("a wider search arrival stays"), GetArriveRadius(250.f, true), 250.f, 0.01f);
	TestEqual(TEXT("sweep legs keep the search arrival"), GetArriveRadius(150.f, false), 150.f, 0.01f);
	TestTrue(TEXT("re-path before giving up"), RepathAfterFailures > 0 && RepathAfterFailures < GiveUpAfterFailures);
	// Held just short of the point by a patrol mate standing on it: arrived after a short stand, not after the 10 s leg timeout.
	TestTrue(TEXT("within 2 m: arrived"), HasArrived(199.f, 200.f, 0.f));
	TestFalse(TEXT("2.08 m and still walking: not yet"), HasArrived(208.f, 200.f, 0.f));
	TestTrue(TEXT("2.08 m and stuck 0.5 s: arrived"), HasArrived(208.f, 200.f, ArriveStuckSeconds));
	TestFalse(TEXT("stuck 4 m off: not arrived (the walk is re-issued)"), HasArrived(400.f, 200.f, 2.f));
	return true;
}

// Fix 2026-10-18 (user report on L_PatrolTest: a stone first, then the whistle from behind a wall with a doorway - they kept turning
// at the stone): the noise comes round walls / through doorways, measured as the walking path (numbers from the replay probe).
STEALTH_LURE_TEST(FStealthLurePathHearingTest, "PathHearing")
bool FStealthLurePathHearingTest::RunTest(const FString& Parameters)
{
	using namespace StealthLureRules;
	using StealthLureTest::Listener;
	const float Whistle = 1200.f;
	const float Search = 1.25f;
	// The hound at the stone: 7.5 m straight, 1 wall, searching (15 m x 0.5 = 7.5 m - lost before), 10.9 m through the doorway.
	FStealthLureListener Hound = Listener(751.f, 1);
	Hound.bSearching = true;
	TestFalse(TEXT("straight line only: the wall halves 15 m to 7.5 m, 7.51 m is not heard"), Hears(Hound, Whistle, Search));
	Hound.PathCm = 1089.f;
	TestTrue(TEXT("round the wall: 10.9 m walk within the 15 m open-air radius (searching)"), Hears(Hound, Whistle, Search));
	TestTrue(TEXT("the path part alone"), HearsAlongPath(Hound, Whistle, Search));
	// The marksman: 10.8 m straight, 14.1 m walk: heard while searching (15 m), not while patrolling (12 m).
	FStealthLureListener Marksman = Listener(1081.f, 1);
	Marksman.PathCm = 1413.f;
	Marksman.bSearching = true;
	TestTrue(TEXT("marksman searching: 14.1 m walk < 15 m"), Hears(Marksman, Whistle, Search));
	Marksman.bSearching = false;
	TestFalse(TEXT("marksman on patrol: 14.1 m walk > 12 m"), Hears(Marksman, Whistle, Search));
	TestEqual(TEXT("open radius on patrol 12 m"), OpenRadius(Marksman, Whistle, Search), 1200.f, 0.01f);
	// No full path (unknown / partial = -1): only the straight-line rule.
	FStealthLureListener Closed = Listener(900.f, 1);
	TestFalse(TEXT("no path: the wall rule alone (9 m > 6 m)"), Hears(Closed, Whistle, Search));
	// A 12 m wide wall without a doorway: the walk round it is longer than the radius (StealthLureSmoke's frostbitten 8 m beyond).
	FStealthLureListener Beyond = Listener(800.f, 1);
	Beyond.PathCm = 1464.f;
	TestFalse(TEXT("a long detour round a wide wall is not heard"), Hears(Beyond, Whistle, Search));
	// The gates still apply to the path.
	FStealthLureListener Held = Hound;
	Held.bTurnBasedHeld = true;
	TestFalse(TEXT("turn-based held: never, path or not"), Hears(Held, Whistle, Search));
	FStealthLureListener Hunting = Hound;
	Hunting.bOnPatrol = false;
	TestFalse(TEXT("off patrol duty: never"), HearsAlongPath(Hunting, Whistle, Search));
	return true;
}

// Fix 2026-10-18: a NEWER noise re-targets an enemy that already searches / investigates (the whistle after the stone was lost).
STEALTH_LURE_TEST(FStealthLureRetargetTest, "Retarget")
bool FStealthLureRetargetTest::RunTest(const FString& Parameters)
{
	using namespace StealthLureRules;
	FNoiseReactionInput Idle;
	TestTrue(TEXT("not searching: investigates"), ReactToNoise(Idle) == ENoiseReaction::Investigate);

	// Searching at the stone (set 8 s ago), the whistle 10 m away: the newer noise wins.
	FNoiseReactionInput AtStone;
	AtStone.bSearching = true;
	AtStone.DistanceToCurrentNoiseCm = 1000.f;
	AtStone.SecondsSinceCurrentNoise = 8.f;
	TestTrue(TEXT("searching the stone, a whistle: re-target"), ReactToNoise(AtStone) == ENoiseReaction::Retarget);
	// A second stone elsewhere.
	FNoiseReactionInput SecondStone = AtStone;
	SecondStone.DistanceToCurrentNoiseCm = 600.f;
	SecondStone.SecondsSinceCurrentNoise = 3.f;
	TestTrue(TEXT("a second throw elsewhere: re-target"), ReactToNoise(SecondStone) == ENoiseReaction::Retarget);
	// The same spot again, later (a second whistle from the same place): a new noise, the search restarts there.
	FNoiseReactionInput Again = AtStone;
	Again.DistanceToCurrentNoiseCm = 10.f;
	Again.SecondsSinceCurrentNoise = 10.f;
	TestTrue(TEXT("the same spot 10 s later: re-target (restart)"), ReactToNoise(Again) == ENoiseReaction::Retarget);
	// The same noise this instant (a patrol mate pulled it in, then it heard it itself): nothing changes.
	FNoiseReactionInput Same = AtStone;
	Same.DistanceToCurrentNoiseCm = 0.f;
	Same.SecondsSinceCurrentNoise = 0.f;
	TestTrue(TEXT("the same noise this instant: unchanged"), ReactToNoise(Same) == ENoiseReaction::SameNoise);
	Same.DistanceToCurrentNoiseCm = SameNoiseCm + 1.f;
	TestTrue(TEXT("another point this instant: re-target"), ReactToNoise(Same) == ENoiseReaction::Retarget);

	// Hounds: following a fresh scent close by keeps the nose; sniffing / a far scent re-target.
	FNoiseReactionInput Following = AtStone;
	Following.bFollowingScent = true;
	Following.ScentDistanceCm = 250.f;
	TestTrue(TEXT("following a scent 2.5 m away: keeps following"), ReactToNoise(Following) == ENoiseReaction::KeepScent);
	Following.bSearching = false;
	TestTrue(TEXT("... also when it was not searching"), ReactToNoise(Following) == ENoiseReaction::KeepScent);
	Following.ScentDistanceCm = ScentHoldsNoiseCm + 50.f;
	TestTrue(TEXT("following a scent 3.5 m away: the noise wins"), ReactToNoise(Following) == ENoiseReaction::Investigate);
	FNoiseReactionInput Sniffing;
	Sniffing.bSniffing = true;
	Sniffing.ScentDistanceCm = 100.f;
	TestTrue(TEXT("sniffing (not following yet): the noise wins"), ReactToNoise(Sniffing) == ENoiseReaction::Investigate);

	TestTrue(TEXT("investigate / re-target go to the noise"), GoesToNoise(ENoiseReaction::Investigate) && GoesToNoise(ENoiseReaction::Retarget));
	TestFalse(TEXT("same noise / scent / ignored do not"), GoesToNoise(ENoiseReaction::SameNoise) || GoesToNoise(ENoiseReaction::KeepScent)
		|| GoesToNoise(ENoiseReaction::Ignored));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
