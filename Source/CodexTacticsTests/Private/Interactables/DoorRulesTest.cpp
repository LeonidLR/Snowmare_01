// CodexTactics.Doors.* - openable doors R1 (designer 2026-11-21; opening-states.md 1 + F1, ADR-0003 "after R1"): DoorRules (state
// machine, navmesh rule, sight rule, leaf split, swing side, "on his way", auto-open timer, tags) + DoorTuning (doors.json, the switch).
// In-game check: CodexTactics.DoorsSmoke on L_Gen_StoryShowcase. UE-only (no Godot reference).

#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Interactables/DoorRules.h"
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

#endif // WITH_DEV_AUTOMATION_TESTS
