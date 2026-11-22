// CodexTactics.Doors.* - openable doors R1 (designer 2026-11-21; opening-states.md 1 + F1, ADR-0003 "after R1"): DoorRules (state
// machine, navmesh rule, sight rule, leaf split, swing side, "on his way", auto-open timer, tags) + DoorTuning (doors.json, the switch).
// In-game check: CodexTactics.DoorsSmoke on L_Gen_StoryShowcase. UE-only (no Godot reference).

#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Data/CombatTypes.h"
#include "Interactables/DoorRules.h"
#include "Interactables/LootRules.h"
#include "LevelGen/MissionGoalsRules.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

#define DOORS_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Doors." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

DOORS_TEST(FDoorsStateParseTest, "State.Parse")
bool FDoorsStateParseTest::RunTest(const FString& Parameters)
{
	const TCHAR* Names[] = { TEXT("open"), TEXT("closed"), TEXT("locked"), TEXT("barricaded"), TEXT("sealed") };
	for (const TCHAR* Name : Names)
	{
		EDoorLeafState State;
		TestTrue(FString::Printf(TEXT("%s parses"), Name), DoorRules::ParseState(Name, State));
		TestEqual(FString::Printf(TEXT("%s round trip"), Name), FString(DoorRules::StateName(State)), FString(Name));
	}
	EDoorLeafState State;
	TestTrue(TEXT("case and spaces"), DoorRules::ParseState(TEXT(" Locked "), State) && State == EDoorLeafState::Locked);
	TestFalse(TEXT("unknown refused"), DoorRules::ParseState(TEXT("ajar"), State));
	TestTrue(TEXT("unknown reads as open"), State == EDoorLeafState::Open);
	TestTrue(TEXT("closed adopted"), DoorRules::IsAdoptable(EDoorLeafState::Closed));
	TestTrue(TEXT("locked adopted"), DoorRules::IsAdoptable(EDoorLeafState::Locked));
	TestFalse(TEXT("open not adopted (no leaf)"), DoorRules::IsAdoptable(EDoorLeafState::Open));
	TestFalse(TEXT("barricaded stays the builder's"), DoorRules::IsAdoptable(EDoorLeafState::Barricaded));
	TestFalse(TEXT("sealed stays the builder's"), DoorRules::IsAdoptable(EDoorLeafState::Sealed));
	return true;
}

DOORS_TEST(FDoorsStateMachineTest, "State.Machine")
bool FDoorsStateMachineTest::RunTest(const FString& Parameters)
{
	FDoorTuning Tuning;
	EDoorLeafState Next;
	TestTrue(TEXT("closed -> open"), DoorRules::ApplyAction(EDoorLeafState::Closed, EDoorAction::Open, Tuning, Next) && Next == EDoorLeafState::Open);
	TestTrue(TEXT("closed -> open by walking through"), DoorRules::ApplyAction(EDoorLeafState::Closed, EDoorAction::AutoOpen, Tuning, Next) && Next == EDoorLeafState::Open);
	TestTrue(TEXT("open -> closed"), DoorRules::ApplyAction(EDoorLeafState::Open, EDoorAction::Close, Tuning, Next) && Next == EDoorLeafState::Closed);
	TestFalse(TEXT("open again refused"), DoorRules::ApplyAction(EDoorLeafState::Open, EDoorAction::Open, Tuning, Next));
	TestTrue(TEXT("refused leaves the state"), Next == EDoorLeafState::Open);
	TestFalse(TEXT("close a closed door refused"), DoorRules::ApplyAction(EDoorLeafState::Closed, EDoorAction::Close, Tuning, Next));
	TestFalse(TEXT("a locked door does not open"), DoorRules::ApplyAction(EDoorLeafState::Locked, EDoorAction::Open, Tuning, Next));
	TestFalse(TEXT("a locked door never auto-opens"), DoorRules::ApplyAction(EDoorLeafState::Locked, EDoorAction::AutoOpen, Tuning, Next));
	TestFalse(TEXT("a locked door does not close"), DoorRules::ApplyAction(EDoorLeafState::Locked, EDoorAction::Close, Tuning, Next));
	TestTrue(TEXT("kick opens a locked door"), DoorRules::ApplyAction(EDoorLeafState::Locked, EDoorAction::Kick, Tuning, Next) && Next == EDoorLeafState::Open);
	TestTrue(TEXT("kick opens a closed door"), DoorRules::ApplyAction(EDoorLeafState::Closed, EDoorAction::Kick, Tuning, Next) && Next == EDoorLeafState::Open);
	TestFalse(TEXT("kick on an open door refused"), DoorRules::ApplyAction(EDoorLeafState::Open, EDoorAction::Kick, Tuning, Next));
	for (EDoorLeafState Blocked : { EDoorLeafState::Barricaded, EDoorLeafState::Sealed })
	{
		for (EDoorAction Action : { EDoorAction::Open, EDoorAction::Close, EDoorAction::Kick, EDoorAction::AutoOpen, EDoorAction::Unlock,
			EDoorAction::ShootLock, EDoorAction::Blast })
		{
			TestFalse(FString::Printf(TEXT("%s refuses action %d"), DoorRules::StateName(Blocked), static_cast<int32>(Action)),
				DoorRules::ApplyAction(Blocked, Action, Tuning, Next));
		}
	}
	Tuning.bKickEnabled = false;
	TestFalse(TEXT("kicks disabled"), DoorRules::ApplyAction(EDoorLeafState::Locked, EDoorAction::Kick, Tuning, Next));
	TestTrue(TEXT("the key: locked -> closed (unlocked)"), DoorRules::ApplyAction(EDoorLeafState::Locked, EDoorAction::Unlock, Tuning, Next) && Next == EDoorLeafState::Closed);
	TestFalse(TEXT("a key on an unlocked door does nothing"), DoorRules::ApplyAction(EDoorLeafState::Closed, EDoorAction::Unlock, Tuning, Next));
	TestTrue(TEXT("the lock shot off: locked -> open"), DoorRules::ApplyAction(EDoorLeafState::Locked, EDoorAction::ShootLock, Tuning, Next) && Next == EDoorLeafState::Open);
	TestFalse(TEXT("no lock to shoot on an open door"), DoorRules::ApplyAction(EDoorLeafState::Open, EDoorAction::ShootLock, Tuning, Next));
	TestTrue(TEXT("a blast opens a locked door"), DoorRules::ApplyAction(EDoorLeafState::Locked, EDoorAction::Blast, Tuning, Next) && Next == EDoorLeafState::Open);
	TestTrue(TEXT("a blast takes an open leaf too"), DoorRules::ApplyAction(EDoorLeafState::Open, EDoorAction::Blast, Tuning, Next) && Next == EDoorLeafState::Open);
	TestFalse(TEXT("a blown-off leaf cannot shut"), DoorRules::CanClose(EDoorLeafState::Open, false, true));
	// A kicked door's lock is broken: closed again it is Closed, it opens without a kick.
	Tuning.bKickEnabled = true;
	EDoorLeafState State = EDoorLeafState::Locked;
	DoorRules::ApplyAction(State, EDoorAction::Kick, Tuning, State);
	DoorRules::ApplyAction(State, EDoorAction::Close, Tuning, State);
	TestTrue(TEXT("kicked + closed = closed, not locked"), State == EDoorLeafState::Closed);
	TestTrue(TEXT("then opens by hand"), DoorRules::ApplyAction(State, EDoorAction::Open, Tuning, Next) && Next == EDoorLeafState::Open);
	TestTrue(TEXT("close only with the doorway clear"), DoorRules::CanClose(EDoorLeafState::Open, false));
	TestFalse(TEXT("somebody in the doorway"), DoorRules::CanClose(EDoorLeafState::Open, true));
	TestFalse(TEXT("a closed door cannot close"), DoorRules::CanClose(EDoorLeafState::Closed, false));
	return true;
}

DOORS_TEST(FDoorsPathCostTest, "Nav.PathCost")
bool FDoorsPathCostTest::RunTest(const FString& Parameters)
{
	FDoorTuning Tuning;
	Tuning.NavEnteringCostCm = 350.f;
	TestTrue(TEXT("open is free"), DoorRules::GetPassage(EDoorLeafState::Open) == EDoorPassage::Free);
	TestTrue(TEXT("closed is priced (open delay)"), DoorRules::GetPassage(EDoorLeafState::Closed) == EDoorPassage::Priced);
	TestTrue(TEXT("locked is blocked (route elsewhere, story 3)"), DoorRules::GetPassage(EDoorLeafState::Locked) == EDoorPassage::Blocked);
	TestTrue(TEXT("barricaded is blocked"), DoorRules::GetPassage(EDoorLeafState::Barricaded) == EDoorPassage::Blocked);
	TestTrue(TEXT("sealed is blocked"), DoorRules::GetPassage(EDoorLeafState::Sealed) == EDoorPassage::Blocked);
	TestEqual(TEXT("closed costs the entering cost"), DoorRules::GetNavEnteringCost(EDoorLeafState::Closed, Tuning), 350.f);
	TestEqual(TEXT("open costs nothing"), DoorRules::GetNavEnteringCost(EDoorLeafState::Open, Tuning), 0.f);
	TestEqual(TEXT("locked has no price (no edge)"), DoorRules::GetNavEnteringCost(EDoorLeafState::Locked, Tuning), 0.f);
	return true;
}

DOORS_TEST(FDoorsSightTest, "Sight.Blocks")
bool FDoorsSightTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("open passes sight and fire"), DoorRules::BlocksSightAndFire(EDoorLeafState::Open));
	TestTrue(TEXT("closed blocks"), DoorRules::BlocksSightAndFire(EDoorLeafState::Closed));
	TestTrue(TEXT("locked blocks"), DoorRules::BlocksSightAndFire(EDoorLeafState::Locked));
	TestTrue(TEXT("barricaded blocks"), DoorRules::BlocksSightAndFire(EDoorLeafState::Barricaded));
	TestTrue(TEXT("sealed blocks"), DoorRules::BlocksSightAndFire(EDoorLeafState::Sealed));
	return true;
}

DOORS_TEST(FDoorsLeavesTest, "Leaves.SplitAndSwing")
bool FDoorsLeavesTest::RunTest(const FString& Parameters)
{
	FDoorTuning Tuning;
	Tuning.SingleLeafMaxCm = 130.f;
	TestEqual(TEXT("80 cm: one leaf"), DoorRules::CountLeaves(80.f, Tuning), 1);
	TestEqual(TEXT("130 cm: one leaf"), DoorRules::CountLeaves(130.f, Tuning), 1);
	TestEqual(TEXT("150 cm: a pair"), DoorRules::CountLeaves(150.f, Tuning), 2);
	TestEqual(TEXT("opener on +Y pushes it to -Y"), DoorRules::SwingSign(80.f), -1.f);
	TestEqual(TEXT("opener on -Y pushes it to +Y"), DoorRules::SwingSign(-80.f), 1.f);
	TestEqual(TEXT("unknown side: +Y"), DoorRules::SwingSign(0.f), 1.f);
	return true;
}

DOORS_TEST(FDoorsOnHisWayTest, "AutoOpen.OnHisWay")
bool FDoorsOnHisWayTest::RunTest(const FString& Parameters)
{
	FDoorTuning Tuning;
	Tuning.ZoneDepthCm = 130.f;
	Tuning.ZoneSlackCm = 25.f;
	Tuning.HeadingSpeedCmS = 40.f;
	const float Half = 60.f;
	TestTrue(TEXT("in front of the leaf: in the zone"), DoorRules::IsInZone(FVector2D(10.f, -100.f), Half, Tuning));
	TestTrue(TEXT("just past the jamb: in the zone"), DoorRules::IsInZone(FVector2D(80.f, 50.f), Half, Tuning));
	TestFalse(TEXT("too far out"), DoorRules::IsInZone(FVector2D(0.f, -150.f), Half, Tuning));
	TestFalse(TEXT("beside the doorway"), DoorRules::IsInZone(FVector2D(100.f, 20.f), Half, Tuning));

	// Path through the doorway from -Y to +Y.
	TestTrue(TEXT("path through the doorway"), DoorRules::PathCrossesDoorway({ FVector2D(0.f, -100.f), FVector2D(10.f, 200.f) }, Half, 300.f));
	TestFalse(TEXT("path through the wall beside it"), DoorRules::PathCrossesDoorway({ FVector2D(150.f, -100.f), FVector2D(150.f, 200.f) }, Half, 300.f));
	TestFalse(TEXT("path along the wall"), DoorRules::PathCrossesDoorway({ FVector2D(-200.f, -80.f), FVector2D(200.f, -80.f) }, Half, 300.f));
	TestFalse(TEXT("crossing beyond the look-ahead"),
		DoorRules::PathCrossesDoorway({ FVector2D(-300.f, -100.f), FVector2D(300.f, -100.f), FVector2D(0.f, -100.f), FVector2D(0.f, 100.f) }, Half, 300.f));
	TestTrue(TEXT("crossing in the second segment"),
		DoorRules::PathCrossesDoorway({ FVector2D(-100.f, -100.f), FVector2D(0.f, -50.f), FVector2D(0.f, 100.f) }, Half, 300.f));
	TestTrue(TEXT("from the other side"), DoorRules::PathCrossesDoorway({ FVector2D(0.f, 90.f), FVector2D(0.f, -90.f) }, Half, 300.f));
	TestFalse(TEXT("one point is no path"), DoorRules::PathCrossesDoorway({ FVector2D(0.f, 90.f) }, Half, 300.f));

	TestTrue(TEXT("walking into the leaf"), DoorRules::IsHeadingInto(FVector2D(0.f, -80.f), FVector2D(0.f, 300.f), Tuning));
	TestFalse(TEXT("walking away"), DoorRules::IsHeadingInto(FVector2D(0.f, -80.f), FVector2D(0.f, -300.f), Tuning));
	TestFalse(TEXT("standing"), DoorRules::IsHeadingInto(FVector2D(0.f, -80.f), FVector2D(0.f, 10.f), Tuning));
	TestFalse(TEXT("walking along the wall"), DoorRules::IsHeadingInto(FVector2D(0.f, -80.f), FVector2D(300.f, 5.f), Tuning));
	return true;
}

DOORS_TEST(FDoorsAutoOpenTimerTest, "AutoOpen.Delay")
bool FDoorsAutoOpenTimerTest::RunTest(const FString& Parameters)
{
	FDoorTuning Tuning;
	Tuning.AutoOpenDelaySeconds = 0.3f;
	bool bOpen = false;
	float Held = DoorRules::AdvanceAutoOpen(EDoorLeafState::Closed, 0.f, true, 0.1f, Tuning, bOpen);
	TestFalse(TEXT("0.1 s: still shut"), bOpen);
	Held = DoorRules::AdvanceAutoOpen(EDoorLeafState::Closed, Held, true, 0.1f, Tuning, bOpen);
	TestFalse(TEXT("0.2 s: still shut"), bOpen);
	Held = DoorRules::AdvanceAutoOpen(EDoorLeafState::Closed, Held, true, 0.11f, Tuning, bOpen);
	TestTrue(TEXT("0.31 s: opens"), bOpen);
	Held = DoorRules::AdvanceAutoOpen(EDoorLeafState::Closed, 0.25f, false, 0.1f, Tuning, bOpen);
	TestEqual(TEXT("nobody heading: the timer resets"), Held, 0.f);
	TestFalse(TEXT("and it stays shut"), bOpen);
	Held = DoorRules::AdvanceAutoOpen(EDoorLeafState::Locked, 5.f, true, 1.f, Tuning, bOpen);
	TestFalse(TEXT("a locked door never auto-opens"), bOpen);
	TestEqual(TEXT("its timer stays 0"), Held, 0.f);
	Tuning.AutoOpenDelaySeconds = 0.f;
	DoorRules::AdvanceAutoOpen(EDoorLeafState::Closed, 0.f, true, 0.f, Tuning, bOpen);
	TestTrue(TEXT("no delay: at once"), bOpen);
	return true;
}

DOORS_TEST(FDoorsTagsTest, "Tags")
bool FDoorsTagsTest::RunTest(const FString& Parameters)
{
	const TArray<FName> Tags = { FName(TEXT("building=S03")), FName(TEXT("floor=0")), FName(TEXT("opening=S03_d0")), FName(TEXT("door_leaf")),
		FName(TEXT("door_state=locked")), FName(TEXT("door_lock_plate")) };
	FString Value;
	TestTrue(TEXT("door_state"), DoorRules::FindTagValue(Tags, TEXT("door_state"), Value) && Value == TEXT("locked"));
	TestTrue(TEXT("building"), DoorRules::FindTagValue(Tags, TEXT("building"), Value) && Value == TEXT("S03"));
	TestTrue(TEXT("opening"), DoorRules::FindTagValue(Tags, TEXT("opening"), Value) && Value == TEXT("S03_d0"));
	TestFalse(TEXT("a flag tag has no value"), DoorRules::FindTagValue(Tags, TEXT("door_leaf"), Value));
	TestFalse(TEXT("absent"), DoorRules::FindTagValue(Tags, TEXT("window_state"), Value));
	return true;
}

DOORS_TEST(FDoorsTuningTest, "Tuning.FileAndSwitch")
bool FDoorsTuningTest::RunTest(const FString& Parameters)
{
	FString Text;
	TestTrue(TEXT("doors.json exists"), FFileHelper::LoadFileToString(Text, *DoorTuning::GetDefaultPath()));
	TSharedPtr<FJsonObject> Json;
	TestTrue(TEXT("doors.json parses"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) && Json.IsValid());
	if (!Json.IsValid())
	{
		return false;
	}
	const FDoorTuning File = DoorTuning::FromJson(*Json);
	TestFalse(TEXT("the shipped file keeps the switch OFF (roadmap: on in main only after the enemy-AI A/B)"), File.bEnabled);
	TestTrue(TEXT("a kick attempt takes a moment"), File.KickSeconds > 0.f && File.KickSeconds <= 5.f);
	TestTrue(TEXT("a locked door gives to a kick only sometimes"), File.KickChanceLocked > 0.f && File.KickChanceLocked < 1.f);
	TestEqual(TEXT("an unlocked door gives to the first kick"), File.KickChanceClosed, 1.f);
	TestTrue(TEXT("the shotgun is the best lock breaker"), DoorRules::GetLockShotChance(TEXT("shotgun"), File) >= DoorRules::GetLockShotChance(TEXT("m16"), File)
		&& DoorRules::GetLockShotChance(TEXT("shotgun"), File) >= DoorRules::GetLockShotChance(TEXT("pistol"), File));
	TestTrue(TEXT("the squad starts without keys"), File.StartKeys.IsEmpty());
	TestEqual(TEXT("N_kick 15 m"), File.KickNoiseCm, 1500.f);
	TestEqual(TEXT("single leaf up to 130 cm (kit sizes)"), File.SingleLeafMaxCm, 130.f);
	TestTrue(TEXT("a short auto-open delay"), File.AutoOpenDelaySeconds > 0.f && File.AutoOpenDelaySeconds <= 1.f);

	TSharedRef<FJsonObject> Over = MakeShared<FJsonObject>();
	Over->SetNumberField(TEXT("enabled"), 1);
	Over->SetNumberField(TEXT("kick_noise_m"), 20);
	Over->SetNumberField(TEXT("open_s"), 99);
	Over->SetBoolField(TEXT("kick_enabled"), false);
	const FDoorTuning Changed = DoorTuning::FromJson(*Over, File);
	TestTrue(TEXT("enabled read"), Changed.bEnabled);
	TestEqual(TEXT("metres to cm"), Changed.KickNoiseCm, 2000.f);
	TestEqual(TEXT("clamped"), Changed.OpenSeconds, 5.f);
	TestFalse(TEXT("bool flag"), Changed.bKickEnabled);
	TestEqual(TEXT("absent keys keep the base"), Changed.AutoOpenDelaySeconds, File.AutoOpenDelaySeconds);

	TestFalse(TEXT("-1 follows the file (off)"), DoorTuning::ResolveSwitch(-1, File));
	TestTrue(TEXT("-1 follows the file (on)"), DoorTuning::ResolveSwitch(-1, Changed));
	TestFalse(TEXT("0 is off whatever the file"), DoorTuning::ResolveSwitch(0, Changed));
	TestTrue(TEXT("1 is on whatever the file"), DoorTuning::ResolveSwitch(1, File));
	return true;
}

DOORS_TEST(FDoorsKickTest, "Kick.ChanceAndInjury")
bool FDoorsKickTest::RunTest(const FString& Parameters)
{
	FDoorTuning Tuning;
	Tuning.KickChanceClosed = 1.f;
	Tuning.KickChanceLocked = 0.4f;
	Tuning.KickHeavyFactor = 0.5f;
	Tuning.KickInjuryBase = 0.f;
	Tuning.KickInjuryWounded = 0.3f;
	Tuning.KickWoundedBelow = 0.5f;
	Tuning.KickInjuryHeavy = 0.1f;
	FDoorKickOutcome Kick = DoorRules::ResolveKick(EDoorLeafState::Closed, false, 1.f, 0.99f, 0.f, Tuning);
	TestTrue(TEXT("an unlocked door: the first kick gives"), Kick.bOpens && FMath::IsNearlyEqual(Kick.OpenChance, 1.f));
	TestFalse(TEXT("healthy, light door: no injury"), Kick.bInjures);
	Kick = DoorRules::ResolveKick(EDoorLeafState::Locked, false, 1.f, 0.39f, 1.f, Tuning);
	TestTrue(TEXT("locked: a roll under 0.4 gives"), Kick.bOpens && FMath::IsNearlyEqual(Kick.OpenChance, 0.4f));
	Kick = DoorRules::ResolveKick(EDoorLeafState::Locked, false, 1.f, 0.41f, 1.f, Tuning);
	TestFalse(TEXT("locked: a roll over 0.4 holds"), Kick.bOpens);
	Kick = DoorRules::ResolveKick(EDoorLeafState::Locked, true, 1.f, 0.3f, 0.05f, Tuning);
	TestTrue(TEXT("heavy: half the chance (0.2)"), !Kick.bOpens && FMath::IsNearlyEqual(Kick.OpenChance, 0.2f));
	TestTrue(TEXT("heavy: injury chance 0.1"), Kick.bInjures && FMath::IsNearlyEqual(Kick.InjuryChance, 0.1f));
	Kick = DoorRules::ResolveKick(EDoorLeafState::Closed, true, 0.3f, 0.f, 0.39f, Tuning);
	TestTrue(TEXT("wounded on a heavy door: 0.4"), Kick.bInjures && FMath::IsNearlyEqual(Kick.InjuryChance, 0.4f));
	Kick = DoorRules::ResolveKick(EDoorLeafState::Closed, false, 0.6f, 0.f, 0.f, Tuning);
	TestTrue(TEXT("not wounded enough: no injury chance"), FMath::IsNearlyZero(Kick.InjuryChance) && !Kick.bInjures);
	Kick = DoorRules::ResolveKick(EDoorLeafState::Open, false, 1.f, 0.f, 0.f, Tuning);
	TestTrue(TEXT("an open door: nothing to kick"), !Kick.bOpens && !Kick.bInjures);
	Tuning.HeavyMinWidthCm = 140.f;
	TestTrue(TEXT("a 150 cm entrance is heavy"), DoorRules::IsHeavy(150.f, Tuning));
	TestFalse(TEXT("a 90 cm flat door is not"), DoorRules::IsHeavy(90.f, Tuning));
	return true;
}

DOORS_TEST(FDoorsLockShotTest, "LockShot.ChanceByWeapon")
bool FDoorsLockShotTest::RunTest(const FString& Parameters)
{
	FDoorTuning Tuning;
	Tuning.LockShotChance.Add(TEXT("shotgun"), 0.9f);
	Tuning.LockShotChance.Add(TEXT("pistol"), 0.3f);
	Tuning.LockShotChanceDefault = 0.35f;
	TestEqual(TEXT("shotgun"), DoorRules::GetLockShotChance(TEXT("shotgun"), Tuning), 0.9f);
	TestEqual(TEXT("case-insensitive"), DoorRules::GetLockShotChance(TEXT("Pistol"), Tuning), 0.3f);
	TestEqual(TEXT("unlisted: default"), DoorRules::GetLockShotChance(TEXT("m16"), Tuning), 0.35f);
	TestEqual(TEXT("no weapon: no chance"), DoorRules::GetLockShotChance(FString(), Tuning), 0.f);
	return true;
}

DOORS_TEST(FDoorsKeysTest, "Keys.Fit")
bool FDoorsKeysTest::RunTest(const FString& Parameters)
{
	TSet<FString> Keys;
	TestFalse(TEXT("no keys"), DoorRules::KeyFits(Keys, TEXT("S03"), TEXT("S03_d19"), FString()));
	Keys.Add(TEXT("S03"));
	TestTrue(TEXT("the building's key opens its doors"), DoorRules::KeyFits(Keys, TEXT("S03"), TEXT("S03_d19"), FString()));
	TestFalse(TEXT("not another building's"), DoorRules::KeyFits(Keys, TEXT("S09"), TEXT("S09_d19"), FString()));
	Keys.Reset();
	Keys.Add(TEXT("S09_d20"));
	TestTrue(TEXT("one door's key"), DoorRules::KeyFits(Keys, TEXT("S09"), TEXT("S09_d20"), FString()));
	TestFalse(TEXT("not its neighbour"), DoorRules::KeyFits(Keys, TEXT("S09"), TEXT("S09_d19"), FString()));
	Keys.Reset();
	Keys.Add(TEXT("boiler_room"));
	TestTrue(TEXT("a door_key tag"), DoorRules::KeyFits(Keys, TEXT("S09"), TEXT("S09_d19"), TEXT("boiler_room")));
	Keys.Reset();
	Keys.Add(TEXT("*"));
	TestTrue(TEXT("the master key"), DoorRules::KeyFits(Keys, TEXT("X"), TEXT("Y"), FString()));
	return true;
}

DOORS_TEST(FDoorsClickTest, "Click.Actions")
bool FDoorsClickTest::RunTest(const FString& Parameters)
{
	EDoorAction Action;
	TestTrue(TEXT("unlocked, click: open by hand"), DoorRules::ChooseClickAction(EDoorLeafState::Closed, false, false, false, false, Action) && Action == EDoorAction::Open);
	TestTrue(TEXT("unlocked, double click: kick"), DoorRules::ChooseClickAction(EDoorLeafState::Closed, true, false, false, false, Action) && Action == EDoorAction::Kick);
	TestFalse(TEXT("unlocked, Ctrl + click: an ordinary shot"), DoorRules::ChooseClickAction(EDoorLeafState::Closed, false, true, false, false, Action));
	TestTrue(TEXT("open, click: shut"), DoorRules::ChooseClickAction(EDoorLeafState::Open, false, false, false, false, Action) && Action == EDoorAction::Close);
	TestFalse(TEXT("blown off: nothing to shut"), DoorRules::ChooseClickAction(EDoorLeafState::Open, false, false, false, true, Action));
	TestTrue(TEXT("locked + key, click: unlock"), DoorRules::ChooseClickAction(EDoorLeafState::Locked, false, false, true, false, Action) && Action == EDoorAction::Unlock);
	TestFalse(TEXT("locked, no key, click: refused (a line)"), DoorRules::ChooseClickAction(EDoorLeafState::Locked, false, false, false, false, Action));
	TestTrue(TEXT("locked, double click: kick"), DoorRules::ChooseClickAction(EDoorLeafState::Locked, true, false, false, false, Action) && Action == EDoorAction::Kick);
	TestTrue(TEXT("locked, Ctrl + click: shoot the lock"), DoorRules::ChooseClickAction(EDoorLeafState::Locked, false, true, false, false, Action) && Action == EDoorAction::ShootLock);
	TestFalse(TEXT("sealed: no click action"), DoorRules::ChooseClickAction(EDoorLeafState::Sealed, true, false, true, false, Action));
	return true;
}

// --- R2 (designer 2026-11-22): door clips, enemies breach locked doors, keys as items, deterministic rolls -------------------------

DOORS_TEST(FDoorsClipSideTest, "Clips.HandSide")
bool FDoorsClipSideTest::RunTest(const FString& Parameters)
{
	// A single 90 cm leaf: hinge at -45, handle (free edge) at +33. He stands on +Y facing -Y: his right hand points to +X.
	const float Handle = 33.f;
	TestTrue(TEXT("+Y side, standing left of the handle: handle on his right"), DoorRules::ChooseHandSide(FVector2D(0.f, 100.f), Handle) == EDoorSide::Right);
	TestTrue(TEXT("-Y side, same spot: the handle on his left"), DoorRules::ChooseHandSide(FVector2D(0.f, -100.f), Handle) == EDoorSide::Left);
	TestTrue(TEXT("+Y side, standing past the handle: on his left"), DoorRules::ChooseHandSide(FVector2D(80.f, 100.f), Handle) == EDoorSide::Left);
	TestTrue(TEXT("-Y side, past the handle: on his right"), DoorRules::ChooseHandSide(FVector2D(80.f, -100.f), Handle) == EDoorSide::Right);
	TestTrue(TEXT("a pair (handle 0), +Y, left of the middle: right"), DoorRules::ChooseHandSide(FVector2D(-20.f, 120.f), 0.f) == EDoorSide::Right);
	TestTrue(TEXT("straight in front of the handle: right"), DoorRules::ChooseHandSide(FVector2D(Handle, 90.f), Handle) == EDoorSide::Right);
	TestTrue(TEXT("swap flips the pick"), DoorRules::ChooseHandSide(FVector2D(0.f, 100.f), Handle, true) == EDoorSide::Left);
	return true;
}

DOORS_TEST(FDoorsClipKickLegsTest, "Clips.KickLegsAlternate")
bool FDoorsClipKickLegsTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("first kick: the first leg"), DoorRules::ChooseKickLeg(EDoorSide::Right, 0) == EDoorSide::Right);
	TestTrue(TEXT("second: the other"), DoorRules::ChooseKickLeg(EDoorSide::Right, 1) == EDoorSide::Left);
	TestTrue(TEXT("third: back"), DoorRules::ChooseKickLeg(EDoorSide::Right, 2) == EDoorSide::Right);
	TestTrue(TEXT("fourth: other"), DoorRules::ChooseKickLeg(EDoorSide::Right, 3) == EDoorSide::Left);
	TestTrue(TEXT("left first"), DoorRules::ChooseKickLeg(EDoorSide::Left, 0) == EDoorSide::Left && DoorRules::ChooseKickLeg(EDoorSide::Left, 1) == EDoorSide::Right);
	TestEqual(TEXT("clip key kick_l"), DoorRules::ClipKey(EDoorClip::Kick, EDoorSide::Left), FString(TEXT("kick_l")));
	TestEqual(TEXT("clip key push_r"), DoorRules::ClipKey(EDoorClip::Push, EDoorSide::Right), FString(TEXT("push_r")));
	TestEqual(TEXT("clip key open_r"), DoorRules::ClipKey(EDoorClip::Open, EDoorSide::Right), FString(TEXT("open_r")));
	return true;
}

DOORS_TEST(FDoorsClipPushTest, "Clips.PushAfterForcedLock")
bool FDoorsClipPushTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("a plain unlocked door: the hand open"), DoorRules::ChooseOpenClip(false) == EDoorClip::Open);
	TestTrue(TEXT("after the key / a shot lock: the push"), DoorRules::ChooseOpenClip(true) == EDoorClip::Push);
	// A shot lock with lock_shot_push breaks to an unlocked, still shut door (the Unlock transition), the push opens it.
	FDoorTuning Tuning;
	EDoorLeafState Next;
	TestTrue(TEXT("the broken lock: locked -> closed"), DoorRules::ApplyAction(EDoorLeafState::Locked, EDoorAction::Unlock, Tuning, Next) && Next == EDoorLeafState::Closed);
	TestTrue(TEXT("then it opens by hand (the push)"), DoorRules::ApplyAction(Next, EDoorAction::Open, Tuning, Next) && Next == EDoorLeafState::Open);
	// The timings: the clip contact time when a clip plays, else the doors.json seconds.
	Tuning.HandOpenSeconds = 0.5f;
	Tuning.KickSeconds = 1.2f;
	Tuning.PushOpenSeconds = 0.7f;
	Tuning.MontageContactSeconds.Add(TEXT("kick"), 0.45f);
	TestEqual(TEXT("kick with its clip: the impact frame"), DoorRules::GetContactSeconds(EDoorClip::Kick, true, Tuning), 0.45f);
	TestEqual(TEXT("kick without a clip: kick_s"), DoorRules::GetContactSeconds(EDoorClip::Kick, false, Tuning), 1.2f);
	TestEqual(TEXT("push without a measured contact: push_s"), DoorRules::GetContactSeconds(EDoorClip::Push, true, Tuning), 0.7f);
	TestEqual(TEXT("open: hand_open_s"), DoorRules::GetContactSeconds(EDoorClip::Open, false, Tuning), 0.5f);
	return true;
}

DOORS_TEST(FDoorsClipPathTest, "Clips.MontagePerSkeleton")
bool FDoorsClipPathTest::RunTest(const FString& Parameters)
{
	FDoorTuning Tuning;
	Tuning.HandOpenMontage = TEXT("/Game/R1/Hand");
	TestEqual(TEXT("no sets: the R1 hook for operatives"), DoorRules::FindClipPath(Tuning, TEXT("UE4_Mannequin_Skeleton"), true, EDoorClip::Open, EDoorSide::Left),
		FString(TEXT("/Game/R1/Hand")));
	TestTrue(TEXT("no sets: nothing for a monster"), DoorRules::FindClipPath(Tuning, TEXT("SK_Brute"), false, EDoorClip::Open, EDoorSide::Left).IsEmpty());
	Tuning.MontageSets.Add(TEXT("default")).Add(TEXT("kick_l"), TEXT("/Game/D/KickL"));
	Tuning.MontageSets.Add(TEXT("SK_PmcOperator_Skeleton")).Add(TEXT("kick_l"), TEXT("/Game/P/KickL"));
	TestEqual(TEXT("an operative skeleton not listed: default"), DoorRules::FindClipPath(Tuning, TEXT("UE4_Mannequin_Skeleton"), true, EDoorClip::Kick, EDoorSide::Left),
		FString(TEXT("/Game/D/KickL")));
	TestEqual(TEXT("the PMC body: its own set"), DoorRules::FindClipPath(Tuning, TEXT("SK_PmcOperator_Skeleton"), true, EDoorClip::Kick, EDoorSide::Left),
		FString(TEXT("/Game/P/KickL")));
	TestTrue(TEXT("a monster never takes the operatives default"), DoorRules::FindClipPath(Tuning, TEXT("SK_Brute"), false, EDoorClip::Kick, EDoorSide::Left).IsEmpty());
	return true;
}

DOORS_TEST(FDoorsBreachChoiceTest, "Breach.ChoicePerArchetype")
bool FDoorsBreachChoiceTest::RunTest(const FString& Parameters)
{
	FString Text;
	TestTrue(TEXT("doors.json exists"), FFileHelper::LoadFileToString(Text, *DoorTuning::GetDefaultPath()));
	TSharedPtr<FJsonObject> Json;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid())
	{
		AddError(TEXT("doors.json does not parse"));
		return false;
	}
	const FDoorTuning File = DoorTuning::FromJson(*Json);
	TestTrue(TEXT("breach on in the data"), File.bBreachEnabled);
	const TArray<EDoorAction>& Pmc = DoorRules::GetBreachActions(File, DoorRules::GetArchetypeKey(true, EEnemyArchetype::Base));
	TestEqual(TEXT("PMC: the full set"), Pmc.Num(), 4);
	TestTrue(TEXT("PMC: the key first"), Pmc.Num() > 0 && Pmc[0] == EDoorAction::Unlock);
	TestTrue(TEXT("PMC: the grenade last"), Pmc.Num() > 0 && Pmc.Last() == EDoorAction::Blast);
	TestTrue(TEXT("dogs leave locked doors alone"), DoorRules::GetBreachActions(File, DoorRules::GetArchetypeKey(false, EEnemyArchetype::FrostHound)).IsEmpty());
	TestTrue(TEXT("the horde leaves locked doors alone"), DoorRules::GetBreachActions(File, DoorRules::GetArchetypeKey(false, EEnemyArchetype::Frostbitten)).IsEmpty());
	const TArray<EDoorAction>& Brute = DoorRules::GetBreachActions(File, DoorRules::GetArchetypeKey(false, EEnemyArchetype::Brute));
	TestTrue(TEXT("the Brute bashes"), Brute.Num() == 1 && Brute[0] == EDoorAction::Kick);
	TestTrue(TEXT("an unlisted key: default (none)"), DoorRules::GetBreachActions(File, TEXT("nobody")).IsEmpty());

	FDoorBreachAbility Ability;
	TSet<EDoorAction> Tried;
	EDoorAction Next = EDoorAction::Open;
	Ability.bCanShoot = true;
	Ability.bHasGrenade = true;
	TestTrue(TEXT("no key: he shoots the lock first"), DoorRules::NextBreachAction(Pmc, Tried, Ability, File, Next) && Next == EDoorAction::ShootLock);
	Ability.bHasKey = true;
	TestTrue(TEXT("with a key: the key"), DoorRules::NextBreachAction(Pmc, Tried, Ability, File, Next) && Next == EDoorAction::Unlock);
	Ability.bHasKey = false;
	Tried.Add(EDoorAction::ShootLock);
	TestTrue(TEXT("the lock held: the kick"), DoorRules::NextBreachAction(Pmc, Tried, Ability, File, Next) && Next == EDoorAction::Kick);
	Tried.Add(EDoorAction::Kick);
	TestTrue(TEXT("the kicks held: the grenade"), DoorRules::NextBreachAction(Pmc, Tried, Ability, File, Next) && Next == EDoorAction::Blast);
	Ability.bHasGrenade = false;
	TestFalse(TEXT("no grenade left: he gives up"), DoorRules::NextBreachAction(Pmc, Tried, Ability, File, Next));
	FDoorTuning NoKicks = File;
	NoKicks.bKickEnabled = false;
	TestFalse(TEXT("kicks off: a Brute has nothing"), DoorRules::NextBreachAction(Brute, TSet<EDoorAction>(), FDoorBreachAbility(), NoKicks, Next));

	EDoorAction Parsed = EDoorAction::Open;
	TestTrue(TEXT("parse key"), DoorRules::ParseBreachAction(TEXT(" Key "), Parsed) && Parsed == EDoorAction::Unlock);
	TestTrue(TEXT("parse blast"), DoorRules::ParseBreachAction(TEXT("blast"), Parsed) && Parsed == EDoorAction::Blast);
	TestFalse(TEXT("unknown"), DoorRules::ParseBreachAction(TEXT("charm"), Parsed));
	TestTrue(TEXT("the shipped file keeps the R1 switch OFF"), !File.bEnabled);
	TestTrue(TEXT("shot locks are pushed open (R2)"), File.bLockShotPush);
	TestTrue(TEXT("the operatives clip set"), File.MontageSets.Contains(TEXT("default")) && File.MontageSets[TEXT("default")].Num() == 6);
	// Root motion (designer 2026-11-22, option a): every clip kind has its start pose, and it lies far enough out for the travel to the leaf.
	for (const EDoorClip Clip : { EDoorClip::Open, EDoorClip::Kick, EDoorClip::Push })
	{
		const TCHAR* Kind = DoorRules::ClipKindName(Clip);
		TestTrue(FString::Printf(TEXT("%s: a measured start pose"), Kind), DoorRules::HasClipApproach(Clip, File));
		TestTrue(FString::Printf(TEXT("%s: a root travel"), Kind), File.MontageRootCm.Contains(Kind));
	}
	TestTrue(TEXT("the push starts farther out than the hand open (its long walk-in)"),
		DoorRules::GetClipApproachCm(EDoorClip::Push, File) > DoorRules::GetClipApproachCm(EDoorClip::Open, File));
	FDoorTuning Plain;
	Plain.ApproachCm = 100.f;
	TestFalse(TEXT("no data: no start pose"), DoorRules::HasClipApproach(EDoorClip::Kick, Plain));
	TestEqual(TEXT("no data: approach_cm"), DoorRules::GetClipApproachCm(EDoorClip::Kick, Plain), 100.f);
	return true;
}

DOORS_TEST(FDoorsClipMovesPawnTest, "Clips.RootMotionMovesPawn")
bool FDoorsClipMovesPawnTest::RunTest(const FString& Parameters)
{
	// The operatives' default set (UE4 copies, root motion baked): it moves them whatever the clip asset says.
	TestTrue(TEXT("an operative on the default set"), DoorRules::ClipMovesPawn(true, false, false, false));
	// The PMC body's own set: only clips retargeted ONTO its skeleton with root motion move him (start pose + travel).
	TestTrue(TEXT("PMC: retargeted onto his skeleton, root motion"), DoorRules::ClipMovesPawn(true, true, true, true));
	TestFalse(TEXT("PMC: the pack clips by bone name (another skeleton) play in place"), DoorRules::ClipMovesPawn(true, true, false, true));
	TestFalse(TEXT("PMC: on his skeleton but no root motion"), DoorRules::ClipMovesPawn(true, true, true, false));
	// Monsters bash in place (no operative body, no start pose).
	TestFalse(TEXT("not an operative"), DoorRules::ClipMovesPawn(false, false, true, true));
	TestFalse(TEXT("not an operative, own set"), DoorRules::ClipMovesPawn(false, true, true, true));
	// The shipped file keeps the PMC set at the retargeted copies' paths.
	FString Text;
	TestTrue(TEXT("doors.json exists"), FFileHelper::LoadFileToString(Text, *DoorTuning::GetDefaultPath()));
	TSharedPtr<FJsonObject> Json;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid())
	{
		AddError(TEXT("doors.json does not parse"));
		return false;
	}
	const FDoorTuning File = DoorTuning::FromJson(*Json);
	const TMap<FString, FString>* Pmc = File.MontageSets.Find(TEXT("SK_PmcOperator_Skeleton"));
	TestTrue(TEXT("doors.json: the PMC body has its own set of six"), Pmc && Pmc->Num() == 6);
	if (Pmc)
	{
		for (const TPair<FString, FString>& Entry : *Pmc)
		{
			TestTrue(FString::Printf(TEXT("PMC %s under /Game/Animations/Door/Pmc/"), *Entry.Key), Entry.Value.StartsWith(TEXT("/Game/Animations/Door/Pmc/AM_Door_")));
		}
	}
	return true;
}

DOORS_TEST(FDoorsBreachPathTest, "Breach.PathStoppedByDoor")
bool FDoorsBreachPathTest::RunTest(const FString& Parameters)
{
	const float Half = 65.f;
	const float Detect = 260.f;
	// Door frame: the pawn and the path end on -Y, the goal on +Y behind the door.
	TestTrue(TEXT("a partial path ending at the door, the goal behind it"),
		DoorRules::IsPathStoppedByDoor(FVector2D(0.f, -900.f), FVector2D(20.f, -110.f), FVector2D(0.f, 600.f), Half, Detect));
	TestFalse(TEXT("the goal on his own side (the path just ends short)"),
		DoorRules::IsPathStoppedByDoor(FVector2D(0.f, -900.f), FVector2D(20.f, -110.f), FVector2D(300.f, -500.f), Half, Detect));
	TestFalse(TEXT("the path ends far from this door"),
		DoorRules::IsPathStoppedByDoor(FVector2D(0.f, -900.f), FVector2D(900.f, -110.f), FVector2D(0.f, 600.f), Half, Detect));
	TestFalse(TEXT("the goal right in the doorway (not beyond)"),
		DoorRules::IsPathStoppedByDoor(FVector2D(0.f, -900.f), FVector2D(0.f, -110.f), FVector2D(0.f, 10.f), Half, Detect));
	TestTrue(TEXT("mirrored: everything on +Y, the goal on -Y"),
		DoorRules::IsPathStoppedByDoor(FVector2D(-50.f, 400.f), FVector2D(-30.f, 90.f), FVector2D(0.f, -700.f), Half, Detect));
	// The path stops a storey below: the goal right behind a locked door at its level - work it from the other side.
	TestEqual(TEXT("goal 250 cm behind the door on +Y"), DoorRules::GoalBehindDoorSide(FVector2D(0.f, 250.f), Half, 800.f), 1.f);
	TestEqual(TEXT("goal on -Y"), DoorRules::GoalBehindDoorSide(FVector2D(100.f, -400.f), Half, 800.f), -1.f);
	TestEqual(TEXT("goal in the doorway: not behind"), DoorRules::GoalBehindDoorSide(FVector2D(0.f, 10.f), Half, 800.f), 0.f);
	TestEqual(TEXT("goal far away: another door's business"), DoorRules::GoalBehindDoorSide(FVector2D(0.f, 1200.f), Half, 800.f), 0.f);
	TestEqual(TEXT("goal far along the wall"), DoorRules::GoalBehindDoorSide(FVector2D(1000.f, 200.f), Half, 800.f), 0.f);
	return true;
}

DOORS_TEST(FDoorsDeterministicRollTest, "Rolls.Deterministic")
bool FDoorsDeterministicRollTest::RunTest(const FString& Parameters)
{
	const uint32 Kick0 = DoorRules::RollSalt(TEXT("S03/S03_d19"), TEXT("BP_Operative_C_7"), 0, TEXT("kick"));
	const uint32 Kick1 = DoorRules::RollSalt(TEXT("S03/S03_d19"), TEXT("BP_Operative_C_7"), 1, TEXT("kick"));
	const uint32 Injury0 = DoorRules::RollSalt(TEXT("S03/S03_d19"), TEXT("BP_Operative_C_7"), 0, TEXT("injury"));
	const float A = DoorRules::DeterministicRoll(42, Kick0);
	TestEqual(TEXT("same seed + salt: the same roll"), DoorRules::DeterministicRoll(42, Kick0), A);
	TestTrue(TEXT("0..1"), A >= 0.f && A < 1.f);
	TestNotEqual(TEXT("the next attempt rolls anew"), Kick0, Kick1);
	TestNotEqual(TEXT("kick and injury are separate rolls"), Kick0, Injury0);
	int32 Differ = 0;
	for (int32 Seed = 0; Seed < 16; ++Seed)
	{
		Differ += DoorRules::DeterministicRoll(Seed, Kick0) != DoorRules::DeterministicRoll(Seed + 1, Kick0) ? 1 : 0;
	}
	TestTrue(TEXT("another seed, another roll"), Differ >= 14);
	// Spread: 200 rolls over the salts fall on both sides of 0.5.
	int32 Low = 0;
	for (int32 Attempt = 0; Attempt < 200; ++Attempt)
	{
		Low += DoorRules::DeterministicRoll(7, DoorRules::RollSalt(TEXT("D"), TEXT("P"), Attempt, TEXT("lock"))) < 0.5f ? 1 : 0;
	}
	TestTrue(TEXT("not all on one side"), Low > 60 && Low < 140);
	return true;
}

DOORS_TEST(FDoorsKeyItemTest, "Keys.LootItem")
bool FDoorsKeyItemTest::RunTest(const FString& Parameters)
{
	// Keys as items: a container or a searched body holds KeyIds; the window lists one "Key" entry; taking it empties them.
	FLootContents Crate;
	Crate.DestroyAll();
	TestTrue(TEXT("an emptied crate is empty"), Crate.IsEmpty());
	Crate.KeyIds = { TEXT("S09_broken_back_windows"), TEXT("boiler_room") };
	TestFalse(TEXT("a key makes it not empty"), Crate.IsEmpty());
	const TArray<FLootEntry> Items = Crate.GetItems();
	TestEqual(TEXT("one key entry"), Items.Num(), 1);
	TestTrue(TEXT("it is the door key"), Items.Num() == 1 && Items[0].Item == ELootItem::DoorKey && Items[0].Count == 2);
	TestTrue(TEXT("the label names the keys"), Items.Num() == 1 && Items[0].GetLabel().ToString().Contains(TEXT("boiler_room")));
	TestEqual(TEXT("taken: both"), Crate.Take(ELootItem::DoorKey), 2);
	TestTrue(TEXT("then empty"), Crate.IsEmpty() && Crate.KeyIds.IsEmpty());
	// The default crate has no keys (nothing changes without keys placed).
	const FLootContents Default;
	TestEqual(TEXT("no keys by default"), Default.GetCount(ELootItem::DoorKey), 0);
	return true;
}

DOORS_TEST(FDoorsKeyGoalTest, "Keys.MissionGoalGrant")
bool FDoorsKeyGoalTest::RunTest(const FString& Parameters)
{
	FMissionGoalsFile File;
	TArray<FString> Errors;
	TestTrue(TEXT("parses"), MissionGoalsRules::Parse(TEXT(R"({"schema":"codex.mission_goals/1","goals":[{"id":"A","pos":[0,0,0],"grant_keys":["S09_broken_back_windows",""]},{"id":"B","pos":[100,0,0]}]})"),
		File, Errors));
	TestTrue(TEXT("goal A grants the key (empty ids dropped)"), File.Goals.Num() == 2 && File.Goals[0].GrantKeys.Num() == 1
		&& File.Goals[0].GrantKeys[0] == TEXT("S09_broken_back_windows"));
	TestTrue(TEXT("goal B grants nothing"), File.Goals.Num() == 2 && File.Goals[1].GrantKeys.IsEmpty());
	return true;
}

DOORS_TEST(FDoorsKitOpenLeavesTest, "Click.OpenKitLeavesPair")
bool FDoorsKitOpenLeavesTest::RunTest(const FString& Parameters)
{
	// kit_dress_lib.leaves: an open leaf stands out of the wall (+X = the wall normal n), its +Y = rot90(n) = -d (the wall direction d);
	// a pair: hinge 0 at the doorway's start, hinge 1 two leaf widths along d. Wall along +X world here (d = +X), n = +Y.
	TArray<FDoorKitLeafPiece> Leaves;
	FDoorKitLeafPiece A;
	A.Hinge = FVector2D(100.f, 0.f);
	A.Along = FVector2D(0.f, 1.f);
	A.Right = FVector2D(-1.f, 0.f);
	A.WidthCm = 75.f;
	FDoorKitLeafPiece B = A;
	B.Hinge = FVector2D(250.f, 0.f);
	FDoorKitLeafPiece Single = A;
	Single.Hinge = FVector2D(1000.f, 0.f);
	Single.WidthCm = 80.f;
	Leaves = { B, Single, A };
	const TArray<FDoorKitDoorway> Ways = DoorRules::PairKitOpenLeaves(Leaves);
	TestEqual(TEXT("one pair + one single"), Ways.Num(), 2);
	const FDoorKitDoorway* Pair = Ways.FindByPredicate([](const FDoorKitDoorway& W) { return W.LeafB != INDEX_NONE; });
	TestTrue(TEXT("the pair found whatever the order"), Pair != nullptr);
	if (Pair)
	{
		TestTrue(TEXT("pair centre between the hinges"), Pair->Center.Equals(FVector2D(175.f, 0.f), 0.5f));
		TestEqual(TEXT("pair width = the hinge distance"), Pair->WidthCm, 150.f);
		TestTrue(TEXT("the wall direction d"), Pair->Tangent.Equals(FVector2D(1.f, 0.f), 0.01f));
	}
	const FDoorKitDoorway* One = Ways.FindByPredicate([](const FDoorKitDoorway& W) { return W.LeafB == INDEX_NONE; });
	TestTrue(TEXT("a single leaf: its doorway on its -Y side"), One && One->Center.Equals(FVector2D(1040.f, 0.f), 0.5f) && One->WidthCm == 80.f);
	return true;
}

DOORS_TEST(FDoorsClickPriorityTest, "Click.OpenLeafRayAndShutSnap")
bool FDoorsClickPriorityTest::RunTest(const FString& Parameters)
{
	// An open leaf (no collision): the cursor ray through it is the door; a ray past it is not.
	TestTrue(TEXT("down through the leaf"), DoorRules::RayHitsLeaf(FVector(40.f, 300.f, 600.f), FVector(40.f, -60.f, 0.f), 80.f, 5.f, 205.f, false, 10.f));
	TestFalse(TEXT("past its free edge"), DoorRules::RayHitsLeaf(FVector(200.f, 300.f, 600.f), FVector(200.f, -60.f, 0.f), 80.f, 5.f, 205.f, false, 10.f));
	TestTrue(TEXT("leaf B runs along -X"), DoorRules::RayHitsLeaf(FVector(-40.f, 300.f, 600.f), FVector(-40.f, -60.f, 0.f), 80.f, 5.f, 205.f, true, 10.f));
	TestFalse(TEXT("over its top"), DoorRules::RayHitsLeaf(FVector(40.f, 300.f, 900.f), FVector(40.f, -300.f, 600.f), 80.f, 5.f, 205.f, false, 10.f));
	// A shut doorway: the floor right at it is the door (the click must not become a cover hug on the jamb).
	TestTrue(TEXT("the floor in the doorway"), DoorRules::IsInShutDoorwaySnap(FVector2D(10.f, 30.f), 60.f, 45.f));
	TestTrue(TEXT("by the jamb"), DoorRules::IsInShutDoorwaySnap(FVector2D(68.f, -20.f), 60.f, 45.f));
	TestFalse(TEXT("a step away in the room"), DoorRules::IsInShutDoorwaySnap(FVector2D(0.f, 120.f), 60.f, 45.f));
	TestFalse(TEXT("along the wall"), DoorRules::IsInShutDoorwaySnap(FVector2D(150.f, 10.f), 60.f, 45.f));
	// The kick facing: square to the leaf from his side = 0 deg; sideways = 90.
	TestTrue(TEXT("facing the leaf from +Y"), DoorRules::FacingErrorDeg(FVector2D(0.f, -1.f), FVector2D(0.f, 100.f), FVector2D(0.f, 1.f)) < 1.f);
	TestTrue(TEXT("facing the leaf from -Y"), DoorRules::FacingErrorDeg(FVector2D(0.f, 1.f), FVector2D(10.f, -100.f), FVector2D(0.f, 1.f)) < 1.f);
	TestTrue(TEXT("turned sideways: 90"), FMath::IsNearlyEqual(DoorRules::FacingErrorDeg(FVector2D(1.f, 0.f), FVector2D(0.f, 100.f), FVector2D(0.f, 1.f)), 90.f, 0.5f));
	// Click actions: an open door's click shuts it, a shut one's opens it.
	EDoorAction Action;
	TestTrue(TEXT("open -> close"), DoorRules::ChooseClickAction(EDoorLeafState::Open, false, false, false, false, Action) && Action == EDoorAction::Close);
	TestTrue(TEXT("closed -> open"), DoorRules::ChooseClickAction(EDoorLeafState::Closed, false, false, false, false, Action) && Action == EDoorAction::Open);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
