// CodexTactics.Power.Rules.* - turret power phase 1 (docs/design/TURRET_POWER.md sections 3, 5, 7, 10, 12; designer decisions 2026-11-17):
// fuel burn per nominal hour, the four generator states, the owner / target rule, the panel action and its refusals, the start setups,
// the plug reach, the observation clock and the tuning file. Pure, no world. UE-only (no Godot reference).

#include "Power/PowerRules.h"
#include "Misc/AutomationTest.h"

#include "Misc/FileHelper.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPowerFuelBurnTest, "CodexTactics.Power.Rules.FuelBurn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPowerFuelBurnTest::RunTest(const FString&)
{
	const FPowerTuning T;
	// Section 3.1 table: burn = 1.0 + 0.25 per live turret, one nominal hour = 50 s.
	TestEqual(TEXT("1 turret 1.25 L/h"), PowerRules::BurnLPerHour(T, 1), 1.25f);
	TestEqual(TEXT("4 turrets 2.0 L/h"), PowerRules::BurnLPerHour(T, 4), 2.f);
	TestEqual(TEXT("a negative load counts as none"), PowerRules::BurnLPerHour(T, -3), 1.f);
	TestTrue(TEXT("one can, 1 turret = 8 h = 400 s (6.7 min)"), FMath::IsNearlyEqual(PowerRules::SecondsLeft(T, 10.f, 1), 400.f, 0.01f));
	TestTrue(TEXT("one can, 2 turrets = 333 s (5.6 min)"), FMath::IsNearlyEqual(PowerRules::SecondsLeft(T, 10.f, 2), 333.33f, 0.05f));
	TestTrue(TEXT("one can, 4 turrets = 250 s (4.2 min)"), FMath::IsNearlyEqual(PowerRules::SecondsLeft(T, 10.f, 4), 250.f, 0.01f));
	TestTrue(TEXT("full tank, 1 turret = 800 s (13.3 min)"), FMath::IsNearlyEqual(PowerRules::SecondsLeft(T, 20.f, 1), 800.f, 0.01f));
	TestEqual(TEXT("an empty tank lasts 0 s"), PowerRules::SecondsLeft(T, 0.f, 2), 0.f);
	TestTrue(TEXT("100 s at 1 turret burn 2.5 L"), FMath::IsNearlyEqual(PowerRules::FuelAfterBurn(T, 10.f, 1, 100.f), 7.5f, 0.001f));
	TestEqual(TEXT("never below 0"), PowerRules::FuelAfterBurn(T, 0.1f, 4, 1000.f), 0.f);
	TestEqual(TEXT("a negative step burns nothing"), PowerRules::FuelAfterBurn(T, 5.f, 1, -1.f), 5.f);
	// Pause / turn-based / contact (section 3.1: world time, no burn in turn-based; pre-powered waits for contact).
	TestTrue(TEXT("running + contact burns"), PowerRules::BurnsNow(T, true, true, false));
	TestFalse(TEXT("turn-based freezes the burn"), PowerRules::BurnsNow(T, true, true, true));
	TestFalse(TEXT("before contact a pre-powered one waits"), PowerRules::BurnsNow(T, true, false, false));
	TestFalse(TEXT("a stopped one never burns"), PowerRules::BurnsNow(T, false, true, false));
	FPowerTuning Early = T;
	Early.bBurnBeforeContact = true;
	TestTrue(TEXT("burn_before_contact true burns at once"), PowerRules::BurnsNow(Early, true, false, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPowerStateTest, "CodexTactics.Power.Rules.StateDerivation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPowerStateTest::RunTest(const FString&)
{
	const FPowerTuning T;
	TestEqual(TEXT("0 L off = dry"), PowerRules::DeriveState(false, 0.f, false), EGeneratorState::Dry);
	TestEqual(TEXT("0 L on = dry (it cannot run dry)"), PowerRules::DeriveState(false, 0.f, true), EGeneratorState::Dry);
	TestEqual(TEXT("fuel off = fuelled"), PowerRules::DeriveState(false, 4.f, false), EGeneratorState::Fuelled);
	TestEqual(TEXT("fuel on = running"), PowerRules::DeriveState(false, 4.f, true), EGeneratorState::Running);
	TestEqual(TEXT("broken wins over everything"), PowerRules::DeriveState(true, 12.f, true), EGeneratorState::Broken);
	TestEqual(TEXT("a breakdown loses half the fuel"), PowerRules::FuelAfterBreak(T, 12.f), 6.f);
	TestEqual(TEXT("a can adds 10 L"), PowerRules::FuelAfterRefuel(T, 3.f), 13.f);
	TestEqual(TEXT("the tank caps at 20 L"), PowerRules::FuelAfterRefuel(T, 15.f), 20.f);
	// Low fuel warnings at 60 / 15 s (section 11).
	TestEqual(TEXT("crossing 60 s"), PowerRules::CrossedWarning(T, 61.f, 59.f), 0);
	TestEqual(TEXT("crossing 15 s"), PowerRules::CrossedWarning(T, 16.f, 14.f), 1);
	TestEqual(TEXT("one step over both: the lower one"), PowerRules::CrossedWarning(T, 61.f, 14.f), 1);
	TestEqual(TEXT("no mark crossed"), PowerRules::CrossedWarning(T, 59.f, 58.f), INDEX_NONE);
	TestEqual(TEXT("running dry is not a warning"), PowerRules::CrossedWarning(T, 10.f, 0.f), INDEX_NONE);
	TestEqual(TEXT("m:ss"), PowerRules::FormatMinutes(250.f), FString(TEXT("4:10")));
	TestEqual(TEXT("m:ss rounds up"), PowerRules::FormatMinutes(59.2f), FString(TEXT("1:00")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPowerTargetRuleTest, "CodexTactics.Power.Rules.TargetRule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPowerTargetRuleTest::RunTest(const FString&)
{
	using PowerRules::NoOwner;
	using PowerRules::PlayerTeam;
	// Rule 3: monsters always (no team), humans of every team but the owner's; no owner = no humans.
	TestFalse(TEXT("neutral: never the squad"), PowerRules::MayTargetHuman(NoOwner, PlayerTeam));
	TestFalse(TEXT("neutral: never a PMC"), PowerRules::MayTargetHuman(NoOwner, 1));
	TestFalse(TEXT("friendly: never the squad"), PowerRules::MayTargetHuman(PlayerTeam, PlayerTeam));
	TestTrue(TEXT("friendly: the PMC"), PowerRules::MayTargetHuman(PlayerTeam, 1));
	TestTrue(TEXT("hostile (PMC 1): the squad"), PowerRules::MayTargetHuman(1, PlayerTeam));
	TestFalse(TEXT("hostile (PMC 1): not its own team"), PowerRules::MayTargetHuman(1, 1));
	TestTrue(TEXT("hostile (PMC 1): another PMC team"), PowerRules::MayTargetHuman(1, 2));
	TestEqual(TEXT("no owner = neutral"), PowerRules::AllegianceOf(NoOwner), EPowerAllegiance::Neutral);
	TestEqual(TEXT("the squad's = friendly"), PowerRules::AllegianceOf(PlayerTeam), EPowerAllegiance::Friendly);
	TestEqual(TEXT("a PMC team's = hostile"), PowerRules::AllegianceOf(2), EPowerAllegiance::Hostile);
	TestEqual(TEXT("seen from PMC 2 its own is friendly"), PowerRules::AllegianceOf(2, 2), EPowerAllegiance::Friendly);
	TestEqual(TEXT("names"), PowerRules::AllegianceName(EPowerAllegiance::Hostile), FString(TEXT("HOSTILE")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPowerActionsTest, "CodexTactics.Power.Rules.Actions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPowerActionsTest::RunTest(const FString&)
{
	using PowerRules::NoOwner;
	const FPowerTuning T;
	// The panel's one action by state (OwnerFlip: whoever starts owns; seize = not ours).
	TestEqual(TEXT("dry -> refuel"), PowerRules::PrimaryAction(EGeneratorState::Dry, NoOwner, 0), EGeneratorAction::Refuel);
	TestEqual(TEXT("fuelled -> start"), PowerRules::PrimaryAction(EGeneratorState::Fuelled, NoOwner, 0), EGeneratorAction::Start);
	TestEqual(TEXT("running ours -> stop"), PowerRules::PrimaryAction(EGeneratorState::Running, 0, 0), EGeneratorAction::Stop);
	TestEqual(TEXT("running hostile -> seize"), PowerRules::PrimaryAction(EGeneratorState::Running, 1, 0), EGeneratorAction::Seize);
	TestEqual(TEXT("running neutral -> seize (designer Q3)"), PowerRules::PrimaryAction(EGeneratorState::Running, NoOwner, 0), EGeneratorAction::Seize);
	TestEqual(TEXT("broken -> repair"), PowerRules::PrimaryAction(EGeneratorState::Broken, NoOwner, 0), EGeneratorAction::Repair);
	using ER = EGeneratorRefusal;
	TestEqual(TEXT("refuel without a can"), PowerRules::CheckAction(T, EGeneratorAction::Refuel, EGeneratorState::Dry, 0.f, 0, NoOwner, 0, false), ER::NoJerrycan);
	TestEqual(TEXT("refuel with a can"), PowerRules::CheckAction(T, EGeneratorAction::Refuel, EGeneratorState::Dry, 0.f, 1, NoOwner, 0, false), ER::None);
	TestEqual(TEXT("refuel a full tank"), PowerRules::CheckAction(T, EGeneratorAction::Refuel, EGeneratorState::Fuelled, 20.f, 1, NoOwner, 0, false), ER::TankFull);
	TestEqual(TEXT("refuel a broken one"), PowerRules::CheckAction(T, EGeneratorAction::Refuel, EGeneratorState::Broken, 0.f, 1, NoOwner, 0, false), ER::Broken);
	TestEqual(TEXT("start dry"), PowerRules::CheckAction(T, EGeneratorAction::Start, EGeneratorState::Dry, 0.f, 0, NoOwner, 0, false), ER::NoFuel);
	TestEqual(TEXT("start fuelled"), PowerRules::CheckAction(T, EGeneratorAction::Start, EGeneratorState::Fuelled, 5.f, 0, NoOwner, 0, false), ER::None);
	TestEqual(TEXT("start running"), PowerRules::CheckAction(T, EGeneratorAction::Start, EGeneratorState::Running, 5.f, 0, 0, 0, false), ER::AlreadyRunning);
	TestEqual(TEXT("seize our own"), PowerRules::CheckAction(T, EGeneratorAction::Seize, EGeneratorState::Running, 5.f, 0, 0, 0, false), ER::AlreadyOurs);
	TestEqual(TEXT("seize a hostile one"), PowerRules::CheckAction(T, EGeneratorAction::Seize, EGeneratorState::Running, 5.f, 0, 1, 0, false), ER::None);
	TestEqual(TEXT("stop a stopped one"), PowerRules::CheckAction(T, EGeneratorAction::Stop, EGeneratorState::Fuelled, 5.f, 0, NoOwner, 0, false), ER::NotRunning);
	TestEqual(TEXT("repair an intact one"), PowerRules::CheckAction(T, EGeneratorAction::Repair, EGeneratorState::Dry, 0.f, 0, NoOwner, 0, false), ER::NotBroken);
	TestEqual(TEXT("nothing in turn-based (designer Q6)"), PowerRules::CheckAction(T, EGeneratorAction::Start, EGeneratorState::Fuelled, 5.f, 0, NoOwner, 0, true),
		ER::TurnBased);
	// Times (section 7): engineer x0.5, start / seize x1.5 in a blizzard.
	TestEqual(TEXT("refuel 4 s"), PowerRules::ActionSeconds(T, EGeneratorAction::Refuel, false, false), 4.f);
	TestEqual(TEXT("start 3 s"), PowerRules::ActionSeconds(T, EGeneratorAction::Start, false, false), 3.f);
	TestEqual(TEXT("engineer start 1.5 s"), PowerRules::ActionSeconds(T, EGeneratorAction::Start, true, false), 1.5f);
	TestEqual(TEXT("start in a blizzard 4.5 s"), PowerRules::ActionSeconds(T, EGeneratorAction::Start, false, true), 4.5f);
	TestEqual(TEXT("engineer seize in a blizzard 3 s"), PowerRules::ActionSeconds(T, EGeneratorAction::Seize, true, true), 3.f);
	TestEqual(TEXT("a blizzard does not slow a refuel"), PowerRules::ActionSeconds(T, EGeneratorAction::Refuel, false, true), 4.f);
	TestEqual(TEXT("stop 1 s"), PowerRules::ActionSeconds(T, EGeneratorAction::Stop, false, false), 1.f);
	TestEqual(TEXT("repair 5 s"), PowerRules::ActionSeconds(T, EGeneratorAction::Repair, false, false), 5.f);
	// Carry (designer Q5): one jerrycan per operative.
	TestEqual(TEXT("room for one"), PowerRules::JerrycanRoom(T, 0), 1);
	TestEqual(TEXT("full with one"), PowerRules::JerrycanRoom(T, 1), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPowerStartSetupTest, "CodexTactics.Power.Rules.StartSetup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPowerStartSetupTest::RunTest(const FString&)
{
	const FPowerTuning T;
	EGeneratorStart Start = EGeneratorStart::Dry;
	TestTrue(TEXT("parse running_hostile"), PowerRules::ParseStart(TEXT("running_hostile"), Start) && Start == EGeneratorStart::RunningHostile);
	TestTrue(TEXT("parse is case-insensitive"), PowerRules::ParseStart(TEXT(" Fuelled_Off "), Start) && Start == EGeneratorStart::FuelledOff);
	TestFalse(TEXT("unknown start"), PowerRules::ParseStart(TEXT("humming"), Start));
	const FGeneratorSetup Hostile = PowerRules::StartSetup(T, EGeneratorStart::RunningHostile, TOptional<float>());
	TestTrue(TEXT("hostile: on, full tank, the PMC team, waits for contact"),
		Hostile.bSwitchOn && Hostile.FuelL == 20.f && Hostile.OwnerTeam == T.HostileOwnerTeam && !Hostile.bContact && !Hostile.bBroken);
	const FGeneratorSetup Neutral = PowerRules::StartSetup(T, EGeneratorStart::RunningNeutral, 12.f);
	TestTrue(TEXT("neutral: on, 12 L, no owner"), Neutral.bSwitchOn && Neutral.FuelL == 12.f && Neutral.OwnerTeam == PowerRules::NoOwner);
	const FGeneratorSetup Fuelled = PowerRules::StartSetup(T, EGeneratorStart::FuelledOff, TOptional<float>());
	TestTrue(TEXT("fuelled: off, one can by default"), !Fuelled.bSwitchOn && Fuelled.FuelL == 10.f && Fuelled.OwnerTeam == PowerRules::NoOwner);
	const FGeneratorSetup Broken = PowerRules::StartSetup(T, EGeneratorStart::Broken, 6.f);
	TestTrue(TEXT("broken: keeps its residual fuel"), Broken.bBroken && !Broken.bSwitchOn && Broken.FuelL == 6.f);
	const FGeneratorSetup Dry = PowerRules::StartSetup(T, EGeneratorStart::Dry, 7.f);
	TestTrue(TEXT("dry: 0 L whatever is given"), !Dry.bBroken && !Dry.bSwitchOn && Dry.FuelL == 0.f);
	TestEqual(TEXT("fuel is clamped to the tank"), PowerRules::StartSetup(T, EGeneratorStart::FuelledOff, 50.f).FuelL, 20.f);
	const FGeneratorSetup Empty = PowerRules::StartSetup(T, EGeneratorStart::RunningHostile, 0.f);
	TestTrue(TEXT("a running start with 0 L is off and ownerless"), !Empty.bSwitchOn && Empty.OwnerTeam == PowerRules::NoOwner);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPowerReachTest, "CodexTactics.Power.Rules.PlugReachAndObserve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPowerReachTest::RunTest(const FString&)
{
	const FPowerTuning T;
	const TArray<FVector> Turrets = { FVector(3000, 0, 0), FVector(500, 0, 0), FVector(5000, 0, 0), FVector(0, 1000, 0), FVector(0, -2000, 0),
		FVector(0, 0, 3900), FVector(1000, 0, 0) };
	const TArray<int32> Picked = PowerRules::PickTurretsInReach(T, FVector::ZeroVector, Turrets);
	TestEqual(TEXT("at most 4"), Picked.Num(), 4);
	TestTrue(TEXT("nearest first: 5 m, 10 m (tie: lower index), 10 m, 20 m"), Picked.Num() == 4 && Picked[0] == 1 && Picked[1] == 3 && Picked[2] == 6
		&& Picked[3] == 4);
	TestFalse(TEXT("50 m is beyond the 40 m cable"), Picked.Contains(2));
	// Observation (section 4 step 3a): 2 s without a break.
	float Watched = PowerRules::ObserveStep(0.f, 1.2f, true);
	TestFalse(TEXT("1.2 s is not enough"), PowerRules::IsObserved(T, Watched));
	TestEqual(TEXT("a break resets it"), PowerRules::ObserveStep(Watched, 0.2f, false), 0.f);
	Watched = PowerRules::ObserveStep(Watched, 0.8f, true);
	TestTrue(TEXT("2.0 s identifies"), PowerRules::IsObserved(T, Watched));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPowerTuningFileTest, "CodexTactics.Power.Rules.TuningFileMirrorsDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPowerTuningFileTest::RunTest(const FString&)
{
	FString Json;
	if (!TestTrue(TEXT("turret_power.json readable"), FFileHelper::LoadFileToString(Json, *PowerRules::GetTuningPath())))
	{
		return false;
	}
	FPowerTuning File;
	// Start from deliberately wrong values: every key must come from the file.
	File.TankL = -1.f;
	File.StartNoiseCm = -1.f;
	File.MaxTurrets = 99;
	File.LowFuelWarnSeconds = { 1.f };
	if (!TestTrue(TEXT("turret_power.json parses"), PowerRules::ParseTuning(Json, File)))
	{
		return false;
	}
	const FPowerTuning D;
	TestEqual(TEXT("nominal hour"), File.NominalHourRealSeconds, D.NominalHourRealSeconds);
	TestEqual(TEXT("burn before contact"), File.bBurnBeforeContact, D.bBurnBeforeContact);
	TestEqual(TEXT("contact wake"), File.ContactWakeCm, D.ContactWakeCm);
	TestEqual(TEXT("tank"), File.TankL, D.TankL);
	TestEqual(TEXT("jerrycan"), File.JerrycanL, D.JerrycanL);
	TestEqual(TEXT("burn base"), File.BurnBaseLPerHour, D.BurnBaseLPerHour);
	TestEqual(TEXT("burn per turret"), File.BurnPerTurretLPerHour, D.BurnPerTurretLPerHour);
	TestEqual(TEXT("hp"), File.GeneratorHp, D.GeneratorHp);
	TestEqual(TEXT("fuel lost on break"), File.FuelLostOnBreakFrac, D.FuelLostOnBreakFrac);
	TestEqual(TEXT("heat radius"), File.HeatRadiusCm, D.HeatRadiusCm);
	TestTrue(TEXT("warnings 60 / 15"), File.LowFuelWarnSeconds == D.LowFuelWarnSeconds);
	TestEqual(TEXT("refuel"), File.RefuelSeconds, D.RefuelSeconds);
	TestEqual(TEXT("start"), File.StartSeconds, D.StartSeconds);
	TestEqual(TEXT("stop"), File.StopSeconds, D.StopSeconds);
	TestEqual(TEXT("seize"), File.SeizeSeconds, D.SeizeSeconds);
	TestEqual(TEXT("seize dark after"), File.SeizeDarkAfterSeconds, D.SeizeDarkAfterSeconds);
	TestEqual(TEXT("repair"), File.RepairSeconds, D.RepairSeconds);
	TestEqual(TEXT("engineer mult"), File.EngineerTimeMult, D.EngineerTimeMult);
	TestEqual(TEXT("blizzard mult"), File.StartBlizzardMult, D.StartBlizzardMult);
	TestEqual(TEXT("interrupt reach"), File.InterruptReachCm, D.InterruptReachCm);
	TestEqual(TEXT("start noise 30 m"), File.StartNoiseCm, D.StartNoiseCm);
	TestEqual(TEXT("work noise"), File.WorkNoiseCm, D.WorkNoiseCm);
	TestEqual(TEXT("cable min"), File.CableMinCm, D.CableMinCm);
	TestEqual(TEXT("cable max"), File.CableMaxCm, D.CableMaxCm);
	TestEqual(TEXT("max turrets"), File.MaxTurrets, D.MaxTurrets);
	TestEqual(TEXT("plug range"), File.PlugRangeCm, D.PlugRangeCm);
	TestEqual(TEXT("spin up"), File.SpinUpSeconds, D.SpinUpSeconds);
	TestEqual(TEXT("wind down"), File.WindDownSeconds, D.WindDownSeconds);
	TestEqual(TEXT("observe range"), File.ObserveCm, D.ObserveCm);
	TestEqual(TEXT("observe time"), File.ObserveSeconds, D.ObserveSeconds);
	TestEqual(TEXT("jerrycans per operative"), File.JerrycansPerOperative, D.JerrycansPerOperative);
	TestEqual(TEXT("hostile owner team"), File.HostileOwnerTeam, D.HostileOwnerTeam);

	FPowerTuning Partial;
	TestTrue(TEXT("a partial file parses"), PowerRules::ParseTuning(TEXT("{ \"generator\": { \"tank_l\": 30 }, \"network\": { \"cable_max_m\": 25 } }"), Partial));
	TestTrue(TEXT("only the given keys change"), Partial.TankL == 30.f && Partial.CableMaxCm == 2500.f && Partial.JerrycanL == D.JerrycanL);
	FPowerTuning Broken;
	TestFalse(TEXT("not JSON = refused"), PowerRules::ParseTuning(TEXT("not json"), Broken));
	TestEqual(TEXT("refused leaves the defaults"), Broken.TankL, D.TankL);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
