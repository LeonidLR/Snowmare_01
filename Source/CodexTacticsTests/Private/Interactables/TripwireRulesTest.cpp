// CodexTactics.Interactables.Tripwire.* — Sprint 09 tripwire mine (МУВ-3 + 2 Ф-1).

#include "Combat/SightRules.h"
#include "Interactables/DeployableRules.h"
#include "Interactables/TripwireRules.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTripwireSpanTest, "CodexTactics.Interactables.Tripwire.SpanAndTrigger",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTripwireSpanTest::RunTest(const FString& Parameters)
{
	using namespace TripwireRules;
	TestFalse(TEXT("0.8 m too short"), IsSpanValid(80.f));
	TestTrue(TEXT("1 m"), IsSpanValid(100.f));
	TestTrue(TEXT("5 m"), IsSpanValid(500.f));
	TestFalse(TEXT("5.2 m too long"), IsSpanValid(520.f));

	// Stance heights of Sprint 08: standing / crouched trip the 30 cm wire, prone crawls under it.
	TestTrue(TEXT("standing trips"), TripsWire(SightRules::ProfileHeight(EOperativeStance::Standing)));
	TestTrue(TEXT("crouched trips"), TripsWire(SightRules::ProfileHeight(EOperativeStance::Crouching)));
	TestFalse(TEXT("prone passes under"), TripsWire(SightRules::ProfileHeight(EOperativeStance::Prone)));

	const FVector2D A(0.f, 0.f);
	const FVector2D B(400.f, 0.f);
	TestEqual(TEXT("distance to the middle"), DistanceToWire(FVector2D(200.f, 30.f), A, B), 30.f, 0.01f);
	TestEqual(TEXT("distance past the end"), DistanceToWire(FVector2D(500.f, 0.f), A, B), 100.f, 0.01f);
	TestTrue(TEXT("a body on the wire"), TouchesWire(FVector2D(200.f, 20.f), 35.f, A, B));
	TestFalse(TEXT("a body beside it"), TouchesWire(FVector2D(200.f, 60.f), 35.f, A, B));
	TestFalse(TEXT("a body beyond the anchor"), TouchesWire(FVector2D(-20.f, 0.f), 35.f, A, B));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTripwireBlastTest, "CodexTactics.Interactables.Tripwire.CostBlastDisarm",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTripwireBlastTest::RunTest(const FString& Parameters)
{
	using namespace TripwireRules;
	TestEqual(TEXT("2 grenades"), GrenadeCost, 2);
	TestEqual(TEXT("wire at 30 cm"), WireHeightCm, 30.f);
	TestEqual(TEXT("fuse 0.25 s"), FuseDelaySeconds, 0.25f);
	TestEqual(TEXT("140 damage"), BlastDamage, 140.f);
	TestEqual(TEXT("4.5 m radius"), BlastRadiusCm, 450.f);
	TestEqual(TEXT("100 % armour"), ArmorPenetration, 1.f);
	TestEqual(TEXT("rig 2 s"), RigSeconds, 2.f);
	TestEqual(TEXT("disarm 3 s"), DisarmSeconds, 3.f);
	TestEqual(TEXT("clean disarm returns 2"), GrenadesReturned(false), 2);
	TestEqual(TEXT("fumble returns 1"), GrenadesReturned(true), 1);
	return true;
}

// --- Demo 01 G3: PMC tripwires placed in the level (user decisions 2026-10-11) ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTripwireHostileTriggerTest, "CodexTactics.Interactables.Tripwire.HostileTrigger",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTripwireHostileTriggerTest::RunTest(const FString& Parameters)
{
	using namespace TripwireRules;
	TestTrue(TEXT("PMC wire: an operative trips it"), TriggersOn(true, true));
	TestFalse(TEXT("PMC wire: an enemy walks through"), TriggersOn(true, false));
	TestTrue(TEXT("squad wire: an enemy trips it"), TriggersOn(false, false));
	TestTrue(TEXT("squad wire: friendly fire"), TriggersOn(false, true));
	// Prone still crawls under a PMC wire (same wire height).
	TestFalse(TEXT("prone passes under"), TripsWire(SightRules::ProfileHeight(EOperativeStance::Prone)));
	TestTrue(TEXT("crouched steps into it"), TripsWire(SightRules::ProfileHeight(EOperativeStance::Crouching)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTripwireHiddenSpotTest, "CodexTactics.Interactables.Tripwire.HiddenSpot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTripwireHiddenSpotTest::RunTest(const FString& Parameters)
{
	using namespace TripwireRules;
	// The mine rule: 4.5 m, the Medic-Sapper +1.5 m (DeployableRules::GetSpotRangeCm, shared with AProximityMineActor).
	TestEqual(TEXT("spot range 4.5 m"), SpotRangeCm, 450.f);
	TestEqual(TEXT("sapper bonus 1.5 m"), SapperSpotBonusCm, 150.f);
	TestEqual(TEXT("shared rule"), DeployableRules::GetSpotRangeCm(450.f, 150.f, EOperativeRole::MedicSapper), 600.f);
	TestTrue(TEXT("commander at 4.4 m spots"), CanSpot(440.f, EOperativeRole::Commander));
	TestFalse(TEXT("commander at 4.6 m does not"), CanSpot(460.f, EOperativeRole::Commander));
	TestFalse(TEXT("engineer at 5.5 m does not"), CanSpot(550.f, EOperativeRole::Engineer));
	TestTrue(TEXT("sapper at 5.5 m spots"), CanSpot(550.f, EOperativeRole::MedicSapper));
	TestFalse(TEXT("sapper at 6.2 m does not"), CanSpot(620.f, EOperativeRole::MedicSapper));
	TestTrue(TEXT("a placed range is honoured"), CanSpot(700.f, EOperativeRole::Commander, 800.f, 0.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTripwireHostileDisarmTest, "CodexTactics.Interactables.Tripwire.HostileDisarmRoleCold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTripwireHostileDisarmTest::RunTest(const FString& Parameters)
{
	using namespace TripwireRules;
	TestTrue(TEXT("PMC wire: the commander may try"), CanDisarm(true, EOperativeRole::Commander));
	TestTrue(TEXT("PMC wire: the engineer may try"), CanDisarm(true, EOperativeRole::Engineer));
	TestFalse(TEXT("squad wire: only the sapper"), CanDisarm(false, EOperativeRole::Commander));
	TestTrue(TEXT("squad wire: the sapper"), CanDisarm(false, EOperativeRole::MedicSapper));

	TestEqual(TEXT("sapper 3 s"), GetDisarmSeconds(true, EOperativeRole::MedicSapper), 3.f);
	TestEqual(TEXT("engineer 4 s"), GetDisarmSeconds(true, EOperativeRole::Engineer), 4.f);
	TestEqual(TEXT("commander 5 s"), GetDisarmSeconds(true, EOperativeRole::Commander), 5.f);
	TestEqual(TEXT("squad wire 3 s"), GetDisarmSeconds(false, EOperativeRole::MedicSapper), 3.f);
	TestEqual(TEXT("turn-based 2 AP"), TurnBasedDisarmAP, 2);

	// The mine formula, crouched (+10), default luck: sapper 60 + 10 + 17.5, engineer 45 + 10 + 15, commander 35 + 10 + 12.5.
	const FDefusalChance Sapper = GetHostileDisarmChance(EOperativeRole::MedicSapper, 35.f, 0.f, 0, false);
	const FDefusalChance Engineer = GetHostileDisarmChance(EOperativeRole::Engineer, 30.f, 0.f, 0, false);
	const FDefusalChance Commander = GetHostileDisarmChance(EOperativeRole::Commander, 25.f, 0.f, 0, false);
	TestEqual(TEXT("sapper 87.5 %"), Sapper.Chance, 87.5f, 0.01f);
	TestEqual(TEXT("engineer 70 %"), Engineer.Chance, 70.f, 0.01f);
	TestEqual(TEXT("commander 57.5 %"), Commander.Chance, 57.5f, 0.01f);
	TestFalse(TEXT("warm commander is not dangerous"), Commander.bDangerous);

	// Cold lowers it like a mine: -15 / -30 / -50 above 25 / 50 / 75 %.
	TestEqual(TEXT("cold 30: -15"), GetHostileDisarmChance(EOperativeRole::Commander, 25.f, 30.f, 0, false).Chance, 42.5f, 0.01f);
	TestEqual(TEXT("cold 60: -30"), GetHostileDisarmChance(EOperativeRole::Commander, 25.f, 60.f, 0, false).Chance, 27.5f, 0.01f);
	const FDefusalChance Frozen = GetHostileDisarmChance(EOperativeRole::Commander, 25.f, 80.f, 0, false);
	TestEqual(TEXT("cold 80: -50"), Frozen.Chance, 7.5f, 0.01f);
	TestEqual(TEXT("cold penalty reported"), Frozen.ColdPenalty, 50.f);
	TestTrue(TEXT("numb hands: dangerous"), Frozen.bDangerous);
	TestEqual(TEXT("frozen sapper still 37.5 %"), GetHostileDisarmChance(EOperativeRole::MedicSapper, 35.f, 80.f, 0, false).Chance, 37.5f, 0.01f);

	// The mine failure rule: a dangerous first attempt only warns, then a miss risks a detonation.
	bool bWarned = false;
	int32 Failed = 0;
	TestTrue(TEXT("first frozen attempt warns"), DeployableRules::ResolveDefusal(Frozen, bWarned, Failed, 99.f, 0.f) == EDefusalResult::Warning);
	TestTrue(TEXT("a miss after the warning detonates on a low risk roll"), DeployableRules::ResolveDefusal(Frozen, bWarned, Failed, 99.f, 10.f) == EDefusalResult::Detonation);
	bool bWarmWarned = false;
	int32 WarmFailed = 0;
	TestTrue(TEXT("warm commander: a good roll disarms"), DeployableRules::ResolveDefusal(Commander, bWarmWarned, WarmFailed, 10.f, 99.f) == EDefusalResult::Success);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTripwireKnownTrapPathingTest, "CodexTactics.Interactables.Tripwire.KnownTrapPathingAutoOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTripwireKnownTrapPathingTest::RunTest(const FString& Parameters)
{
	// Rev 5e (game designer 2026-10-14): spotted wires / mines are avoided automatically ONLY in the squad AUTO mode (Ctrl+T);
	// in normal player control (exploration and ordinary combat) a move order is literal - through the wire if that is the way.
	using namespace TripwireRules;
	TestFalse(TEXT("player control: no automatic avoidance"), ShouldAvoidKnownTraps(false));
	TestTrue(TEXT("AUTO mode: known traps are avoided"), ShouldAvoidKnownTraps(true));
	TestTrue(TEXT("standing, player control: crawl areas closed, traps literal"),
		ChooseOperativeMoveFilter(false, false) == EOperativeMoveFilter::NoCrawlTrapLiteral);
	TestTrue(TEXT("prone, player control: traps literal (crawl areas open)"), ChooseOperativeMoveFilter(true, false) == EOperativeMoveFilter::TrapLiteral);
	TestTrue(TEXT("standing, AUTO: crawl areas closed, traps costly"), ChooseOperativeMoveFilter(false, true) == EOperativeMoveFilter::NoCrawl);
	TestTrue(TEXT("prone, AUTO: the default filter (traps costly)"), ChooseOperativeMoveFilter(true, true) == EOperativeMoveFilter::Default);
	return true;
}
