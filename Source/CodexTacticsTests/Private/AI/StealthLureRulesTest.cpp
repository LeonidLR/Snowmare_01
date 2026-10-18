// CodexTactics.AI.StealthLure.* — stealth lure rules (user decisions 2026-10-18; UE-only, no Godot reference): who hears a
// whistle / thrown stone (radius, walls muffle it like footsteps, heightened while searching, only patrol / guard duty), the
// combat / detection gate, the per-operative cooldowns, the approach pace and the data file.

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
	// A file without the section keeps the defaults (older data / a level override).
	TestTrue(TEXT("a file without \"lure\" applies"), EnemyPerception::ApplyJson(TEXT("{ \"search\": { \"duration_seconds\": 30 } }")));
	TestEqual(TEXT("no section: default whistle radius"), EnemyPerception::GetLure().WhistleRadiusCm, FStealthLureParams().WhistleRadiusCm);
	EnemyPerception::ResetToDefaults();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
