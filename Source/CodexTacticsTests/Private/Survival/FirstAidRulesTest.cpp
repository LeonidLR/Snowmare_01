// CodexTactics.Survival.FirstAid.* - first aid by wound tier (the game designer 2026-11-16, FirstAidRules; Content/Data/Survival/first_aid.json):
// light = self standing, medium = self kneeling, heavy = a mate treats him (the medkit fast, the medical bag longer and stronger); the
// interruptions and the item consumption.

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include <limits>
#include "Survival/FirstAidRules.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace FirstAidTest
{
	using namespace FirstAidRules;

	/** A config with fixed numbers (independent of the shipped JSON). */
	FFirstAidConfig MakeConfig()
	{
		FFirstAidConfig Config;
		Config.Medkit.Heal = 80.f;
		Config.Medkit.TierSteps = 1;
		const float MedkitSelf[TierCount] = { 3.5f, 4.f, 5.5f, 0.f };
		const float MedkitAid[TierCount] = { 4.f, 4.f, 5.f, 6.f };
		FMemory::Memcpy(Config.Medkit.SelfSeconds, MedkitSelf, sizeof(MedkitSelf));
		FMemory::Memcpy(Config.Medkit.AidSeconds, MedkitAid, sizeof(MedkitAid));
		Config.MedicalBag.Heal = 140.f;
		Config.MedicalBag.TierSteps = 2;
		const float BagSelf[TierCount] = { 6.f, 7.f, 9.f, 0.f };
		const float BagAid[TierCount] = { 7.f, 7.f, 9.f, 11.f };
		FMemory::Memcpy(Config.MedicalBag.SelfSeconds, BagSelf, sizeof(BagSelf));
		FMemory::Memcpy(Config.MedicalBag.AidSeconds, BagAid, sizeof(BagAid));
		Config.MedicalBag.MaxCarried = 1;
		return Config;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstAidTierModeTest, "CodexTactics.Survival.FirstAid.TierToMode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstAidTierModeTest::RunTest(const FString&)
{
	using namespace FirstAidRules;
	using CriticalWoundRules::EWoundTier;
	TestTrue(TEXT("not wounded -> none"), FromWoundTier(EWoundTier::None) == ETier::None);
	TestTrue(TEXT("Light -> light"), FromWoundTier(EWoundTier::Light) == ETier::Light);
	TestTrue(TEXT("the critical rule's Heavy (holds his side, limps) -> the designer's MEDIUM"), FromWoundTier(EWoundTier::Heavy) == ETier::Medium);
	TestTrue(TEXT("Critical (lying, crawling) -> the designer's HEAVY"), FromWoundTier(EWoundTier::Critical) == ETier::Heavy);
	TestTrue(TEXT("light: self standing"), ChooseSelfMode(ETier::Light) == EMode::SelfStanding);
	TestTrue(TEXT("none (scratched): self standing"), ChooseSelfMode(ETier::None) == EMode::SelfStanding);
	TestTrue(TEXT("medium: self kneeling"), ChooseSelfMode(ETier::Medium) == EMode::SelfKneeling);
	TestTrue(TEXT("heavy: he cannot treat himself"), ChooseSelfMode(ETier::Heavy) == EMode::None);
	TestTrue(TEXT("prone light: the lying clip"), ChooseSelfMode(ETier::Light, true) == EMode::SelfLying);
	TestTrue(TEXT("prone medium: the lying clip"), ChooseSelfMode(ETier::Medium, true) == EMode::SelfLying);
	TestTrue(TEXT("prone heavy: still no self-treatment"), ChooseSelfMode(ETier::Heavy, true) == EMode::None);
	ETier Parsed = ETier::None;
	TestTrue(TEXT("tier keys parse case-insensitively"), ParseTierKey(TEXT(" Medium "), Parsed) && Parsed == ETier::Medium);
	TestFalse(TEXT("an unknown key"), ParseTierKey(TEXT("critical"), Parsed));
	TestEqual(TEXT("the JSON key of heavy"), FString(TierKey(ETier::Heavy)), FString(TEXT("heavy")));
	TestEqual(TEXT("the tutorial's medkit event id"), FString(ItemEventId(EFirstAidItem::Medkit)), FString(TEXT("MEDKIT")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstAidSelfCheckTest, "CodexTactics.Survival.FirstAid.SelfCheck",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstAidSelfCheckTest::RunTest(const FString&)
{
	using namespace FirstAidRules;
	const FFirstAidConfig Config = FirstAidTest::MakeConfig();
	TestTrue(TEXT("light, a medkit, hurt -> allowed"), CheckSelf(Config, EFirstAidItem::Medkit, ETier::Light, 40.f, 100.f, 1, false) == ERefusal::None);
	TestTrue(TEXT("medium -> allowed (kneeling)"), CheckSelf(Config, EFirstAidItem::Medkit, ETier::Medium, 30.f, 100.f, 1, false) == ERefusal::None);
	TestTrue(TEXT("heavy -> he cannot treat himself"),
		CheckSelf(Config, EFirstAidItem::Medkit, ETier::Heavy, 15.f, 100.f, 1, false) == ERefusal::CannotTreatSelf);
	TestTrue(TEXT("heavy with the medical bag -> still not himself"),
		CheckSelf(Config, EFirstAidItem::MedicalBag, ETier::Heavy, 15.f, 100.f, 1, false) == ERefusal::CannotTreatSelf);
	TestTrue(TEXT("no medkit"), CheckSelf(Config, EFirstAidItem::Medkit, ETier::Light, 40.f, 100.f, 0, false) == ERefusal::NoItem);
	TestTrue(TEXT("full health"), CheckSelf(Config, EFirstAidItem::Medkit, ETier::None, 100.f, 100.f, 1, false) == ERefusal::FullHealth);
	TestTrue(TEXT("dead"), CheckSelf(Config, EFirstAidItem::Medkit, ETier::None, 0.f, 100.f, 1, false) == ERefusal::Dead);
	TestTrue(TEXT("busy (cover / ladder / another treatment)"), CheckSelf(Config, EFirstAidItem::Medkit, ETier::Light, 40.f, 100.f, 1, true) == ERefusal::Busy);
	FFirstAidConfig NoMedium = Config;
	NoMedium.Medkit.SelfSeconds[static_cast<int32>(ETier::Medium)] = 0.f;
	TestTrue(TEXT("a tier without a self time = not allowed"),
		CheckSelf(NoMedium, EFirstAidItem::Medkit, ETier::Medium, 30.f, 100.f, 1, false) == ERefusal::TierNotAllowed);
	FFirstAidConfig Off = Config;
	Off.bEnabled = false;
	TestTrue(TEXT("enabled false -> the old instant medkit"), CheckSelf(Off, EFirstAidItem::Medkit, ETier::Light, 40.f, 100.f, 1, false) == ERefusal::Disabled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstAidAidCheckTest, "CodexTactics.Survival.FirstAid.AidCheck",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstAidAidCheckTest::RunTest(const FString&)
{
	using namespace FirstAidRules;
	const FFirstAidConfig Config = FirstAidTest::MakeConfig();
	auto Check = [&Config](EFirstAidItem Item, bool bSame, bool bHelperAlive, bool bKnocked, ETier HelperTier, int32 Count, bool bHelperBusy,
		bool bPatientAlive, ETier PatientTier, float Health)
	{
		return CheckAid(Config, Item, bSame, bHelperAlive, bKnocked, HelperTier, Count, bHelperBusy, bPatientAlive, PatientTier, Health, 100.f, false);
	};
	TestTrue(TEXT("a fit mate with a medkit on a heavy wound -> allowed"),
		Check(EFirstAidItem::Medkit, false, true, false, ETier::None, 1, false, true, ETier::Heavy, 15.f) == ERefusal::None);
	TestTrue(TEXT("the medical bag on a heavy wound -> allowed"),
		Check(EFirstAidItem::MedicalBag, false, true, false, ETier::Light, 1, false, true, ETier::Heavy, 15.f) == ERefusal::None);
	TestTrue(TEXT("on himself through the aid path"),
		Check(EFirstAidItem::Medkit, true, true, false, ETier::None, 1, false, true, ETier::Heavy, 15.f) == ERefusal::SamePerson);
	TestTrue(TEXT("a dead helper"), Check(EFirstAidItem::Medkit, false, false, false, ETier::None, 1, false, true, ETier::Heavy, 15.f) == ERefusal::Dead);
	TestTrue(TEXT("a dead patient"), Check(EFirstAidItem::Medkit, false, true, false, ETier::None, 1, false, false, ETier::Heavy, 0.f) == ERefusal::Dead);
	TestTrue(TEXT("a knocked-down helper"),
		Check(EFirstAidItem::Medkit, false, true, true, ETier::None, 1, false, true, ETier::Heavy, 15.f) == ERefusal::HelperUnfit);
	TestTrue(TEXT("a heavily wounded helper (lying himself)"),
		Check(EFirstAidItem::Medkit, false, true, false, ETier::Heavy, 1, false, true, ETier::Heavy, 15.f) == ERefusal::HelperUnfit);
	TestTrue(TEXT("a medium-wounded helper may help (helper_may_be_medium)"),
		Check(EFirstAidItem::Medkit, false, true, false, ETier::Medium, 1, false, true, ETier::Heavy, 15.f) == ERefusal::None);
	FFirstAidConfig Strict = Config;
	Strict.bHelperMayBeMedium = false;
	TestTrue(TEXT("... not with helper_may_be_medium false"), CheckAid(Strict, EFirstAidItem::Medkit, false, true, false, ETier::Medium, 1, false, true,
		ETier::Heavy, 15.f, 100.f, false) == ERefusal::HelperUnfit);
	TestTrue(TEXT("the helper has no medkit"),
		Check(EFirstAidItem::Medkit, false, true, false, ETier::None, 0, false, true, ETier::Heavy, 15.f) == ERefusal::NoItem);
	TestTrue(TEXT("the patient is not hurt"),
		Check(EFirstAidItem::Medkit, false, true, false, ETier::None, 1, false, true, ETier::None, 100.f) == ERefusal::FullHealth);
	TestTrue(TEXT("a busy helper"), Check(EFirstAidItem::Medkit, false, true, false, ETier::None, 1, true, true, ETier::Heavy, 15.f) == ERefusal::Busy);
	FFirstAidConfig NoHeavy = Config;
	NoHeavy.Medkit.AidSeconds[static_cast<int32>(ETier::Heavy)] = 0.f;
	TestTrue(TEXT("no aid time for the tier = not allowed"), CheckAid(NoHeavy, EFirstAidItem::Medkit, false, true, false, ETier::None, 1, false, true,
		ETier::Heavy, 15.f, 100.f, false) == ERefusal::TierNotAllowed);
	// The drop (designer 2026-11-17): any listed tier with HP below max is buddy aid; Shift and full HP hand the item over.
	TestTrue(TEXT("drop on a heavy wound treats"), TreatsOnDrop(Config, ETier::Heavy, false, 15.f, 100.f));
	TestTrue(TEXT("drop on a medium wound treats"), TreatsOnDrop(Config, ETier::Medium, false, 28.f, 100.f));
	TestTrue(TEXT("drop on a light wound treats"), TreatsOnDrop(Config, ETier::Light, false, 40.f, 100.f));
	TestTrue(TEXT("a scratch (tier none, HP under max) treats"), TreatsOnDrop(Config, ETier::None, false, 90.f, 100.f));
	TestFalse(TEXT("full HP is a hand-over"), TreatsOnDrop(Config, ETier::None, false, 100.f, 100.f));
	TestFalse(TEXT("a dead mate is not treated"), TreatsOnDrop(Config, ETier::Heavy, false, 0.f, 100.f));
	TestFalse(TEXT("Shift held: always a hand-over"), TreatsOnDrop(Config, ETier::Heavy, true, 15.f, 100.f));
	FFirstAidConfig HeavyOnly = Config;
	for (bool& bTier : HeavyOnly.TreatOnDropTiers)
	{
		bTier = false;
	}
	HeavyOnly.TreatOnDropTiers[static_cast<int32>(ETier::Heavy)] = true;
	TestFalse(TEXT("a heavy-only list still hands a light wound over"), TreatsOnDrop(HeavyOnly, ETier::Light, false, 40.f, 100.f));
	TestTrue(TEXT("a heavy-only list still treats the heavy wound"), TreatsOnDrop(HeavyOnly, ETier::Heavy, false, 15.f, 100.f));
	FFirstAidConfig Off = Config;
	Off.bEnabled = false;
	TestFalse(TEXT("first aid off: a hand-over"), TreatsOnDrop(Off, ETier::Heavy, false, 15.f, 100.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstAidHealTest, "CodexTactics.Survival.FirstAid.HealAndTiers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstAidHealTest::RunTest(const FString&)
{
	using namespace FirstAidRules;
	const FFirstAidConfig Config = FirstAidTest::MakeConfig();
	const FTierBounds Bounds; // 0.5 / 0.35 / 0.2
	TestTrue(TEXT("tier steps: heavy - 1 = medium"), TierAfter(ETier::Heavy, 1) == ETier::Medium);
	TestTrue(TEXT("heavy - 2 = light"), TierAfter(ETier::Heavy, 2) == ETier::Light);
	TestTrue(TEXT("never past none"), TierAfter(ETier::Light, 5) == ETier::None);
	TestEqual(TEXT("medium's floor = critical_from"), TierFloorShare(Bounds, ETier::Medium), 0.2f);
	TestEqual(TEXT("light's floor = heavy_from"), TierFloorShare(Bounds, ETier::Light), 0.35f);
	TestEqual(TEXT("none's floor = the wounded threshold"), TierFloorShare(Bounds, ETier::None), 0.5f);
	// The numbers: the bag heals more and more tiers, and takes longer.
	TestTrue(TEXT("the bag heals more HP than the medkit"), Config.MedicalBag.Heal > Config.Medkit.Heal);
	TestTrue(TEXT("the bag improves more tiers"), Config.MedicalBag.TierSteps > Config.Medkit.TierSteps);
	TestTrue(TEXT("the bag's aid on a heavy wound takes longer"),
		TreatmentSeconds(Config, EFirstAidItem::MedicalBag, ETier::Heavy, false) > TreatmentSeconds(Config, EFirstAidItem::Medkit, ETier::Heavy, false));
	TestTrue(TEXT("self on a medium wound takes longer than on a light one"),
		TreatmentSeconds(Config, EFirstAidItem::Medkit, ETier::Medium, true) > TreatmentSeconds(Config, EFirstAidItem::Medkit, ETier::Light, true));
	TestEqual(TEXT("medkit on 15/140 HP heavy: +80 = 95"), HealedHealth(Config.Medkit, Bounds, ETier::Heavy, 15.f, 140.f), 95.f);
	TestEqual(TEXT("the bag on 15/140: +140, capped at 140"), HealedHealth(Config.MedicalBag, Bounds, ETier::Heavy, 15.f, 140.f), 140.f);
	FItemConfig Bandage;
	Bandage.Heal = 0.f;
	Bandage.TierSteps = 1;
	TestEqual(TEXT("a weak item still lifts heavy to medium's floor (20 % + 0.5 HP)"), HealedHealth(Bandage, Bounds, ETier::Heavy, 10.f, 100.f), 20.5f);
	Bandage.TierSteps = 2;
	TestEqual(TEXT("two steps: light's floor (35 % + 0.5)"), HealedHealth(Bandage, Bounds, ETier::Heavy, 10.f, 100.f), 35.5f);
	Bandage.TierSteps = 3;
	TestEqual(TEXT("three steps: no longer wounded (50 % + 0.5)"), HealedHealth(Bandage, Bounds, ETier::Heavy, 10.f, 100.f), 50.5f);
	Bandage.TierSteps = 0;
	TestEqual(TEXT("no steps, no heal: unchanged"), HealedHealth(Bandage, Bounds, ETier::Heavy, 10.f, 100.f), 10.f);
	TestEqual(TEXT("a dead body stays dead"), HealedHealth(Config.MedicalBag, Bounds, ETier::Heavy, 0.f, 100.f), 0.f);
	TestEqual(TEXT("never below the current health"), HealedHealth(Bandage, Bounds, ETier::None, 90.f, 100.f), 90.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstAidClockTest, "CodexTactics.Survival.FirstAid.ClockAndClips",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstAidClockTest::RunTest(const FString&)
{
	using namespace FirstAidRules;
	TestEqual(TEXT("apply at 0.8 of 6 s"), ApplyDelay(6.f, 0.8f), 4.8f, 0.001f);
	TestEqual(TEXT("apply_at clamped"), ApplyDelay(6.f, 2.f), 6.f);
	FClock Clock;
	Clock.Seconds = 6.f;
	Clock.ApplyDelay = 4.8f;
	int32 Applies = 0;
	for (int32 Step = 0; Step < 70; ++Step)
	{
		Applies += AdvanceClock(Clock, 0.1f) ? 1 : 0;
		if (Step == 40)
		{
			TestFalse(TEXT("not finished at 4.1 s"), IsFinished(Clock));
			TestFalse(TEXT("not applied before 4.8 s"), Clock.bApplied);
		}
	}
	TestEqual(TEXT("the heal lands exactly once"), Applies, 1);
	TestTrue(TEXT("finished after 7 s"), IsFinished(Clock));
	// Clips.
	TestEqual(TEXT("no clips"), PickClipIndex(0, 0.5f), INDEX_NONE);
	TestEqual(TEXT("one clip"), PickClipIndex(1, 0.9f, 0), 0);
	TestEqual(TEXT("4 clips, 0.6 -> 2"), PickClipIndex(4, 0.6f), 2);
	bool bRepeat = false;
	for (int32 Step = 0; Step < 20; ++Step)
	{
		bRepeat |= PickClipIndex(3, Step / 20.f, 1) == 1;
	}
	TestFalse(TEXT("never the previous clip twice in a row"), bRepeat);
	TestEqual(TEXT("bad random value -> a valid index"), PickClipIndex(3, std::numeric_limits<float>::quiet_NaN()), 0);
	TestEqual(TEXT("a 4 s clip in 5 s: rate 0.8"), FitPlayRate(4.f, 5.f, 0.6f, 1.6f), 0.8f, 0.001f);
	TestEqual(TEXT("a 2 s clip in 8 s: rate clamped to 0.6"), FitPlayRate(2.f, 8.f, 0.6f, 1.6f), 0.6f, 0.001f);
	TestEqual(TEXT("... and looped 3 times (2 / 0.6 = 3.33 s each)"), LoopCount(2.f, 8.f, 0.6f), 3);
	TestEqual(TEXT("a 10 s clip in 4 s: rate clamped to 1.6"), FitPlayRate(10.f, 4.f, 0.6f, 1.6f), 1.6f, 0.001f);
	TestEqual(TEXT("... one loop (cut by the end of the treatment)"), LoopCount(10.f, 4.f, 1.6f), 1);
	TestEqual(TEXT("a bad clip length -> rate 1"), FitPlayRate(0.f, 4.f, 0.6f, 1.6f), 1.f);
	FItemConfig Item;
	Item.SelfStandingClips = { TEXT("A") };
	Item.SelfKneelingClips = { TEXT("B") };
	Item.SelfLyingClips = { TEXT("C") };
	Item.HelperClips = { TEXT("D") };
	Item.PatientClips = { TEXT("E") };
	TestEqual(TEXT("standing set"), ClipSet(Item, EMode::SelfStanding, false)[0], FString(TEXT("A")));
	TestEqual(TEXT("kneeling set"), ClipSet(Item, EMode::SelfKneeling, false)[0], FString(TEXT("B")));
	TestEqual(TEXT("lying set"), ClipSet(Item, EMode::SelfLying, false)[0], FString(TEXT("C")));
	TestEqual(TEXT("the helper's set"), ClipSet(Item, EMode::Aid, false)[0], FString(TEXT("D")));
	TestEqual(TEXT("the patient's set"), ClipSet(Item, EMode::Aid, true)[0], FString(TEXT("E")));
	TestEqual(TEXT("no mode: empty"), ClipSet(Item, EMode::None, false).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstAidInterruptTest, "CodexTactics.Survival.FirstAid.Interrupts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstAidInterruptTest::RunTest(const FString&)
{
	using namespace FirstAidRules;
	const FFirstAidConfig Config = FirstAidTest::MakeConfig();
	const FInterruptInput Calm;
	TestTrue(TEXT("nothing happens -> goes on"), CheckInterrupt(Config, Calm) == EInterrupt::None);
	auto With = [&Calm](TFunctionRef<void(FInterruptInput&)> Change)
	{
		FInterruptInput Input = Calm;
		Change(Input);
		return Input;
	};
	TestTrue(TEXT("an actor gone -> lost (no crash)"), CheckInterrupt(Config, With([](FInterruptInput& I) { I.bPatientValid = false; })) == EInterrupt::Lost);
	TestTrue(TEXT("the patient died"), CheckInterrupt(Config, With([](FInterruptInput& I) { I.bPatientAlive = false; })) == EInterrupt::PatientDied);
	TestTrue(TEXT("the helper died"), CheckInterrupt(Config, With([](FInterruptInput& I) { I.bHelperAlive = false; })) == EInterrupt::HelperDied);
	TestTrue(TEXT("the helper knocked down"), CheckInterrupt(Config, With([](FInterruptInput& I) { I.bHelperKnockedDown = true; })) == EInterrupt::HelperDown);
	TestTrue(TEXT("the helper hit (5 HP)"), CheckInterrupt(Config, With([](FInterruptInput& I) { I.HelperDamage = 5.f; })) == EInterrupt::Damaged);
	TestTrue(TEXT("the patient hit"), CheckInterrupt(Config, With([](FInterruptInput& I) { I.PatientDamage = 2.f; })) == EInterrupt::Damaged);
	TestTrue(TEXT("the cold's per-frame loss (0.05 HP) does not interrupt"),
		CheckInterrupt(Config, With([](FInterruptInput& I) { I.HelperDamage = 0.05f; })) == EInterrupt::None);
	TestTrue(TEXT("a move order for the helper"), CheckInterrupt(Config, With([](FInterruptInput& I) { I.bHelperOrdered = true; })) == EInterrupt::Order);
	TestTrue(TEXT("a move order for the patient"), CheckInterrupt(Config, With([](FInterruptInput& I) { I.bPatientOrdered = true; })) == EInterrupt::Order);
	TestTrue(TEXT("the helper pushed away 60 cm"), CheckInterrupt(Config, With([](FInterruptInput& I) { I.HelperMovedCm = 60.f; })) == EInterrupt::Moved);
	TestTrue(TEXT("out of reach"), CheckInterrupt(Config, With([](FInterruptInput& I) { I.DistanceCm = 400.f; })) == EInterrupt::OutOfReach);
	TestTrue(TEXT("a death before damage (order of the checks)"),
		CheckInterrupt(Config, With([](FInterruptInput& I) { I.bPatientAlive = false; I.HelperDamage = 10.f; })) == EInterrupt::PatientDied);
	// Walking over.
	TestTrue(TEXT("approaching: a hit does not stop the walk (default)"),
		CheckInterrupt(Config, With([](FInterruptInput& I) { I.bApproaching = true; I.HelperDamage = 10.f; I.DistanceCm = 900.f; })) == EInterrupt::None);
	FFirstAidConfig Strict = Config;
	Strict.bCancelOnDamageWhileApproaching = true;
	TestTrue(TEXT("... it does with cancel_on_damage_while_approaching"),
		CheckInterrupt(Strict, With([](FInterruptInput& I) { I.bApproaching = true; I.HelperDamage = 10.f; })) == EInterrupt::Damaged);
	TestTrue(TEXT("approaching: far away is fine"), CheckInterrupt(Config, With([](FInterruptInput& I) { I.bApproaching = true; I.DistanceCm = 900.f; })) == EInterrupt::None);
	TestTrue(TEXT("approaching: a new order cancels"),
		CheckInterrupt(Config, With([](FInterruptInput& I) { I.bApproaching = true; I.bHelperOrdered = true; })) == EInterrupt::Order);
	TestTrue(TEXT("approaching: the walk ended short"),
		CheckInterrupt(Config, With([](FInterruptInput& I) { I.bApproaching = true; I.bApproachStalled = true; })) == EInterrupt::ApproachFailed);
	TestTrue(TEXT("approaching: timeout"),
		CheckInterrupt(Config, With([](FInterruptInput& I) { I.bApproaching = true; I.ApproachSeconds = 30.f; })) == EInterrupt::ApproachFailed);
	TestTrue(TEXT("approaching: the patient died"),
		CheckInterrupt(Config, With([](FInterruptInput& I) { I.bApproaching = true; I.bPatientAlive = false; })) == EInterrupt::PatientDied);
	// Self: the patient fields are the helper's own (distance 0).
	TestTrue(TEXT("self: the distance is not checked"),
		CheckInterrupt(Config, With([](FInterruptInput& I) { I.bSelf = true; I.DistanceCm = 900.f; })) == EInterrupt::None);
	TestTrue(TEXT("self: his own move order cancels"),
		CheckInterrupt(Config, With([](FInterruptInput& I) { I.bSelf = true; I.bHelperOrdered = true; })) == EInterrupt::Order);
	// The item.
	TestFalse(TEXT("cancelled before the apply moment: the item is kept (default)"), ConsumesOnInterrupt(Config, false));
	TestFalse(TEXT("cancelled after the apply moment: already used, not twice"), ConsumesOnInterrupt(Config, true));
	FFirstAidConfig Lose = Config;
	Lose.bConsumeOnCancel = true;
	TestTrue(TEXT("consume_on_cancel: lost when cancelled before"), ConsumesOnInterrupt(Lose, false));
	TestEqual(TEXT("bag room: 0 of 1 carried -> 1"), RoomFor(Config, EFirstAidItem::MedicalBag, 0), 1);
	TestEqual(TEXT("bag room: 1 of 1 -> 0"), RoomFor(Config, EFirstAidItem::MedicalBag, 1), 0);
	TestEqual(TEXT("medkits: no limit"), RoomFor(Config, EFirstAidItem::Medkit, 9), MAX_int32);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstAidParseTest, "CodexTactics.Survival.FirstAid.Parse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstAidParseTest::RunTest(const FString&)
{
	using namespace FirstAidRules;
	FFirstAidConfig Config;
	TestFalse(TEXT("not JSON"), ParseConfig(TEXT("nope"), Config));
	const FString Text = TEXT("{\"enabled\": false, \"consume_on_cancel\": true, \"reach_cm\": 200, \"treat_on_drop_tiers\": [\"medium\", \"heavy\"],")
		TEXT("\"start_medical_bags\": {\"Engineer\": 2},")
		TEXT("\"items\": {\"medical_bag\": {\"heal\": 200, \"tier_steps\": 3, \"aid_seconds\": {\"heavy\": 15}, \"max_carried\": 2,")
		TEXT("\"clips\": {\"helper\": [\"X\", \"X\", \" Y \"]}}}}");
	TestTrue(TEXT("parses"), ParseConfig(Text, Config));
	TestFalse(TEXT("enabled"), Config.bEnabled);
	TestTrue(TEXT("consume_on_cancel"), Config.bConsumeOnCancel);
	TestEqual(TEXT("reach_cm"), Config.ReachCm, 200.f);
	TestTrue(TEXT("drop tiers: medium on"), Config.TreatOnDropTiers[static_cast<int32>(ETier::Medium)]);
	TestTrue(TEXT("drop tiers: heavy on"), Config.TreatOnDropTiers[static_cast<int32>(ETier::Heavy)]);
	TestFalse(TEXT("drop tiers: light off"), Config.TreatOnDropTiers[static_cast<int32>(ETier::Light)]);
	TestEqual(TEXT("bag heal"), Config.MedicalBag.Heal, 200.f);
	TestEqual(TEXT("bag tier steps"), Config.MedicalBag.TierSteps, 3);
	TestEqual(TEXT("bag aid heavy"), Config.MedicalBag.AidSeconds[static_cast<int32>(ETier::Heavy)], 15.f);
	TestEqual(TEXT("a listed seconds map replaces the row: medium left out = not allowed"),
		Config.MedicalBag.AidSeconds[static_cast<int32>(ETier::Medium)], 0.f);
	TestEqual(TEXT("bag max carried"), Config.MedicalBag.MaxCarried, 2);
	TestEqual(TEXT("helper clips trimmed, unique"), Config.MedicalBag.HelperClips.Num(), 2);
	TestEqual(TEXT("an unlisted clip set keeps its defaults"), Config.MedicalBag.PatientClips.Num(), FFirstAidConfig().MedicalBag.PatientClips.Num());
	TestEqual(TEXT("start bags by role"), StartMedicalBags(Config, TEXT("engineer")), 2);
	TestEqual(TEXT("an unlisted role starts with none"), StartMedicalBags(Config, TEXT("Commander")), 0);
	TestEqual(TEXT("the medkit untouched"), Config.Medkit.Heal, FFirstAidConfig().Medkit.Heal);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstAidDataFileTest, "CodexTactics.Survival.FirstAid.DataFile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstAidDataFileTest::RunTest(const FString&)
{
	using namespace FirstAidRules;
	FString Text;
	if (!TestTrue(TEXT("first_aid.json present"), FFileHelper::LoadFileToString(Text, *GetConfigPath())))
	{
		return false;
	}
	FFirstAidConfig File;
	TestTrue(TEXT("first_aid.json parses"), ParseConfig(Text, File));
	const FFirstAidConfig Code;
	TestEqual(TEXT("enabled mirrors"), File.bEnabled, Code.bEnabled);
	TestEqual(TEXT("consume_on_cancel mirrors"), File.bConsumeOnCancel, Code.bConsumeOnCancel);
	TestEqual(TEXT("reach_cm mirrors"), File.ReachCm, Code.ReachCm);
	TestEqual(TEXT("approach_stop_cm mirrors"), File.ApproachStopCm, Code.ApproachStopCm);
	TestEqual(TEXT("move_tolerance_cm mirrors"), File.MoveToleranceCm, Code.MoveToleranceCm);
	for (int32 Tier = 0; Tier < TierCount; ++Tier)
	{
		TestEqual(FString::Printf(TEXT("drop tier %d mirrors"), Tier), File.TreatOnDropTiers[Tier], Code.TreatOnDropTiers[Tier]);
	}
	for (const EFirstAidItem Item : { EFirstAidItem::Medkit, EFirstAidItem::MedicalBag })
	{
		const FItemConfig& F = File.Item(Item);
		const FItemConfig& C = Code.Item(Item);
		const FString Name = ItemKey(Item);
		TestEqual(Name + TEXT(" heal mirrors"), F.Heal, C.Heal);
		TestEqual(Name + TEXT(" tier_steps mirrors"), F.TierSteps, C.TierSteps);
		TestEqual(Name + TEXT(" apply_at mirrors"), F.ApplyAt, C.ApplyAt);
		TestEqual(Name + TEXT(" max_carried mirrors"), F.MaxCarried, C.MaxCarried);
		for (int32 Tier = 0; Tier < TierCount; ++Tier)
		{
			TestEqual(FString::Printf(TEXT("%s self %d mirrors"), *Name, Tier), F.SelfSeconds[Tier], C.SelfSeconds[Tier]);
			TestEqual(FString::Printf(TEXT("%s aid %d mirrors"), *Name, Tier), F.AidSeconds[Tier], C.AidSeconds[Tier]);
		}
		TestTrue(Name + TEXT(" standing clips mirror"), F.SelfStandingClips == C.SelfStandingClips);
		TestTrue(Name + TEXT(" kneeling clips mirror"), F.SelfKneelingClips == C.SelfKneelingClips);
		TestTrue(Name + TEXT(" lying clips mirror"), F.SelfLyingClips == C.SelfLyingClips);
		TestTrue(Name + TEXT(" helper clips mirror"), F.HelperClips == C.HelperClips);
		TestTrue(Name + TEXT(" patient clips mirror"), F.PatientClips == C.PatientClips);
		TestTrue(Name + TEXT(": every tier but heavy has a self clip set"), F.SelfStandingClips.Num() > 0 && F.SelfKneelingClips.Num() > 0);
		TestTrue(Name + TEXT(": the heavy aid has helper + patient clips"), F.HelperClips.Num() > 0 && F.PatientClips.Num() > 0);
		TestTrue(Name + TEXT(": heavy cannot be self-treated"), F.SelfSeconds[static_cast<int32>(ETier::Heavy)] <= 0.f);
		TestTrue(Name + TEXT(": heavy can be treated by a mate"), F.AidSeconds[static_cast<int32>(ETier::Heavy)] > 0.f);
	}
	// The designer: the medical bag restores more, heals the wound better, takes longer.
	TestTrue(TEXT("data: bag heal > medkit heal"), File.MedicalBag.Heal > File.Medkit.Heal);
	TestTrue(TEXT("data: bag tier steps > medkit's"), File.MedicalBag.TierSteps > File.Medkit.TierSteps);
	TestTrue(TEXT("data: bag on a heavy wound takes longer"), File.MedicalBag.AidSeconds[3] > File.Medkit.AidSeconds[3]);
	TestTrue(TEXT("data: the medkit's helper clip is the fast one"), File.Medkit.HelperClips.Contains(TEXT("AS_BanadageHealingFriendFast")));
	TestEqual(TEXT("the medic-sapper starts with a bag"), StartMedicalBags(File, TEXT("MedicSapper")), 1);
	// The lying patient (R17-6).
	const FPatientConfig& FP = File.Patient;
	const FPatientConfig& CP = Code.Patient;
	TestEqual(TEXT("hold_body_heading mirrors"), FP.bHoldBodyHeading, CP.bHoldBodyHeading);
	TestEqual(TEXT("counter_turn_max_deg_s mirrors"), FP.CounterTurnMaxDegPerSecond, CP.CounterTurnMaxDegPerSecond);
	TestEqual(TEXT("max_body_turn_deg_s mirrors"), FP.MaxBodyTurnDegPerSecond, CP.MaxBodyTurnDegPerSecond);
	TestEqual(TEXT("helper_beside_lying mirrors"), FP.bHelperBesideLying, CP.bHelperBesideLying);
	TestEqual(TEXT("helper_chest_cm mirrors"), FP.HelperChestCm, CP.HelperChestCm);
	TestEqual(TEXT("helper_turn_deg_s mirrors"), FP.HelperTurnDegPerSecond, CP.HelperTurnDegPerSecond);
	for (int32 Index = 0; Index < PatientPoseCount; ++Index)
	{
		const FString Key = PatientPoseKey(static_cast<EPatientPose>(Index));
		const FPatientPoseConfig& F = FP.Poses[Index];
		const FPatientPoseConfig& C = CP.Poses[Index];
		TestTrue(Key + TEXT(" clips mirror"), F.Clips == C.Clips);
		TestEqual(Key + TEXT(" blend_in_s mirrors"), F.BlendInSeconds, C.BlendInSeconds);
		TestEqual(Key + TEXT(" blend_out_s mirrors"), F.BlendOutSeconds, C.BlendOutSeconds);
		TestEqual(Key + TEXT(" fit_rate mirrors"), F.bFitRate, C.bFitRate);
		TestTrue(Key + TEXT(" end mirrors"), F.End == C.End);
	}
	TestEqual(TEXT("clip_data entries mirror"), FP.ClipData.Num(), CP.ClipData.Num());
	for (const TPair<FString, FPatientClipData>& Pair : CP.ClipData)
	{
		const FPatientClipData* F = FP.ClipData.Find(Pair.Key);
		if (TestNotNull(Pair.Key + TEXT(" clip_data in the file"), F))
		{
			TestEqual(Pair.Key + TEXT(" long_axis_deg mirrors"), F->LongAxisDeg, Pair.Value.LongAxisDeg);
			TestTrue(Pair.Key + TEXT(" long_axis_curve mirrors"), F->LongAxisCurve == Pair.Value.LongAxisCurve);
			TestTrue(Pair.Key + TEXT(" data: the curve starts at 0 and ends near the long axis"), F->LongAxisCurve.IsEmpty()
				|| (FMath::IsNearlyZero(F->LongAxisCurve[0], 0.5f) && FMath::Abs(F->LongAxisCurve.Last() - F->LongAxisDeg) < 10.f));
			TestEqual(Pair.Key + TEXT(" blend_in_s mirrors"), F->BlendInSeconds, Pair.Value.BlendInSeconds);
			TestEqual(Pair.Key + TEXT(" blend_out_s mirrors"), F->BlendOutSeconds, Pair.Value.BlendOutSeconds);
			TestEqual(Pair.Key + TEXT(" fit_rate mirrors"), F->FitRate, Pair.Value.FitRate);
		}
	}
	// The designer: several variants per pose, the heavily wounded one (on all fours) stands up after the aid, no lie-down faster than 1 s.
	TestTrue(TEXT("data: the crawl pose has 2+ variants"), FP.Pose(EPatientPose::Crawl).Clips.Num() >= 2);
	TestTrue(TEXT("data: the prone pose has 2+ variants"), FP.Pose(EPatientPose::Prone).Clips.Num() >= 2);
	TestTrue(TEXT("data: the crawl patient gets up after a completed aid"), FP.Pose(EPatientPose::Crawl).End == EPatientEnd::Stand);
	for (const EPatientPose Pose : { EPatientPose::Crawl, EPatientPose::Prone, EPatientPose::Sitting })
	{
		for (const FString& Clip : FP.Pose(Pose).Clips)
		{
			const FPatientClipPlay Play = PatientClipPlay(File, Pose, Clip);
			TestTrue(FString::Printf(TEXT("data: %s / %s - a clip that turns the body more than 45 deg blends in 1 s or slower, and is counter-turned"),
				PatientPoseKey(Pose), *Clip), FMath::Abs(Play.LongAxisDeg) <= 45.f || (Play.BlendInSeconds >= 1.f && Play.BlendOutSeconds >= 1.f && FP.bHoldBodyHeading));
			TestTrue(FString::Printf(TEXT("data: %s / %s - the counter-turn keeps up with the blend-in"), PatientPoseKey(Pose), *Clip),
				FMath::Abs(Play.LongAxisDeg) <= FP.CounterTurnMaxDegPerSecond * FMath::Max(0.05f, Play.BlendInSeconds));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstAidPatientPoseTest, "CodexTactics.Survival.FirstAid.PatientPose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstAidPatientPoseTest::RunTest(const FString&)
{
	using namespace FirstAidRules;
	// The pose (bKnockedDown, bProne, bOnAllFours, bCrouching).
	TestTrue(TEXT("knocked down first"), ChoosePatientPose(true, true, true, false) == EPatientPose::Knockdown);
	TestTrue(TEXT("prone on all fours = crawl"), ChoosePatientPose(false, true, true, false) == EPatientPose::Crawl);
	TestTrue(TEXT("prone flat = prone"), ChoosePatientPose(false, true, false, false) == EPatientPose::Prone);
	TestTrue(TEXT("crouching = sitting"), ChoosePatientPose(false, false, false, true) == EPatientPose::Sitting);
	TestTrue(TEXT("standing = none"), ChoosePatientPose(false, false, false, false) == EPatientPose::None);
	TestTrue(TEXT("all fours only counts prone"), ChoosePatientPose(false, false, true, false) == EPatientPose::None);
	for (int32 Index = 0; Index < PatientPoseCount; ++Index)
	{
		EPatientPose Parsed = EPatientPose::None;
		const EPatientPose Pose = static_cast<EPatientPose>(Index);
		TestTrue(FString::Printf(TEXT("key '%s' round-trips"), PatientPoseKey(Pose)), ParsePatientPoseKey(FString(PatientPoseKey(Pose)).ToUpper(), Parsed) && Parsed == Pose);
	}
	EPatientPose Bad = EPatientPose::Crawl;
	TestFalse(TEXT("an unknown key"), ParsePatientPoseKey(TEXT("lying"), Bad));
	TestTrue(TEXT("lying poses"), IsLyingPose(EPatientPose::Crawl) && IsLyingPose(EPatientPose::Prone) && !IsLyingPose(EPatientPose::Sitting)
		&& !IsLyingPose(EPatientPose::None) && !IsLyingPose(EPatientPose::Knockdown));
	// The sets: the pose's list, else the item's patient list for a lying pose.
	FFirstAidConfig Config;
	Config.MinPlayRate = 0.6f;
	Config.MaxPlayRate = 1.6f;
	FItemConfig Item;
	Item.PatientClips = { TEXT("Fallback") };
	Config.Patient.Pose(EPatientPose::Crawl).Clips = { TEXT("Injured/Idle_A"), TEXT("Side") };
	Config.Patient.Pose(EPatientPose::Prone).Clips.Reset();
	Config.Patient.Pose(EPatientPose::Sitting).Clips.Reset();
	TestEqual(TEXT("crawl: its own list"), PatientClipSet(Config, Item, EPatientPose::Crawl).Num(), 2);
	TestEqual(TEXT("prone without a list: the item's patient list"), PatientClipSet(Config, Item, EPatientPose::Prone)[0], FString(TEXT("Fallback")));
	TestEqual(TEXT("sitting without a list: nothing (no lying clip for a crouching man)"), PatientClipSet(Config, Item, EPatientPose::Sitting).Num(), 0);
	TestEqual(TEXT("standing: nothing"), PatientClipSet(Config, Item, EPatientPose::None).Num(), 0);
	TestEqual(TEXT("knockdown: nothing (the aid waits)"), PatientClipSet(Config, Item, EPatientPose::Knockdown).Num(), 0);
	// How a clip plays: the pose's settings, the clip's overrides.
	FPatientPoseConfig& Crawl = Config.Patient.Pose(EPatientPose::Crawl);
	Crawl.BlendInSeconds = 0.6f;
	Crawl.BlendOutSeconds = 0.5f;
	Crawl.bFitRate = false;
	FPatientClipData Side;
	Side.LongAxisDeg = -153.f;
	Side.BlendInSeconds = 1.8f;
	Side.FitRate = 1;
	Config.Patient.ClipData.Add(TEXT("Side"), Side);
	const FPatientClipPlay Idle = PatientClipPlay(Config, EPatientPose::Crawl, TEXT("Injured/Idle_A"));
	TestEqual(TEXT("idle: the pose's blend-in"), Idle.BlendInSeconds, 0.6f);
	TestEqual(TEXT("idle: own speed (rate 1..1)"), Idle.MinRate + Idle.MaxRate, 2.f);
	TestEqual(TEXT("idle: no long axis"), Idle.LongAxisDeg, 0.f);
	const FPatientClipPlay Lie = PatientClipPlay(Config, EPatientPose::Crawl, TEXT("Side"));
	TestEqual(TEXT("side: its own blend-in"), Lie.BlendInSeconds, 1.8f);
	TestEqual(TEXT("side: the pose's blend-out (not overridden)"), Lie.BlendOutSeconds, 0.5f);
	TestEqual(TEXT("side: fitted rate range"), Lie.MaxRate, 1.6f);
	TestEqual(TEXT("side: the measured long axis"), Lie.LongAxisDeg, -153.f);
	// The end.
	Crawl.End = EPatientEnd::Stand;
	TestTrue(TEXT("a completed aid, able to stand: he gets up"), StandsAfter(Config, EPatientPose::Crawl, true, true));
	TestFalse(TEXT("interrupted: he stays down"), StandsAfter(Config, EPatientPose::Crawl, false, true));
	TestFalse(TEXT("still critical: he stays down"), StandsAfter(Config, EPatientPose::Crawl, true, false));
	TestFalse(TEXT("a 'stay' pose stays"), StandsAfter(Config, EPatientPose::Prone, true, true));
	// The "patient" block parses (a listed pose replaces its clips, unlisted poses keep the defaults).
	FFirstAidConfig Parsed;
	const FString Text = TEXT("{\"patient\": {\"hold_body_heading\": false, \"counter_turn_max_deg_s\": 90, \"helper_turn_deg_s\": 0, \"helper_chest_cm\": 35,")
		TEXT("\"poses\": {\"Crawl\": {\"clips\": [\"A\", \" B \", \"A\"], \"blend_in_s\": 2.5, \"fit_rate\": true, \"end\": \"STAND\"}, \"bogus\": {\"clips\": [\"Z\"]}},")
		TEXT("\"clip_data\": {\"B\": {\"long_axis_deg\": 200, \"long_axis_curve\": [0, -80, -160], \"blend_out_s\": 1.25}}}}");
	TestTrue(TEXT("parses"), ParseConfig(Text, Parsed));
	TestFalse(TEXT("hold_body_heading"), Parsed.Patient.bHoldBodyHeading);
	TestEqual(TEXT("counter_turn_max_deg_s"), Parsed.Patient.CounterTurnMaxDegPerSecond, 90.f);
	TestEqual(TEXT("helper_turn_deg_s 0 = at once"), Parsed.Patient.HelperTurnDegPerSecond, 0.f);
	TestEqual(TEXT("helper_chest_cm"), Parsed.Patient.HelperChestCm, 35.f);
	const FPatientPoseConfig& PCrawl = Parsed.Patient.Pose(EPatientPose::Crawl);
	TestEqual(TEXT("crawl clips trimmed, unique"), PCrawl.Clips.Num(), 2);
	TestEqual(TEXT("crawl blend_in_s"), PCrawl.BlendInSeconds, 2.5f);
	TestTrue(TEXT("crawl fit_rate"), PCrawl.bFitRate);
	TestTrue(TEXT("crawl end (case-insensitive)"), PCrawl.End == EPatientEnd::Stand);
	TestTrue(TEXT("an unlisted pose keeps its defaults"), Parsed.Patient.Pose(EPatientPose::Prone).Clips == FFirstAidConfig().Patient.Pose(EPatientPose::Prone).Clips);
	const FPatientClipData* PB = Parsed.Patient.ClipData.Find(TEXT("B"));
	TestTrue(TEXT("clip_data replaces the table, long axis normalized (200 -> -160), blend-in unset"),
		Parsed.Patient.ClipData.Num() == 1 && PB && FMath::IsNearlyEqual(PB->LongAxisDeg, -160.f, 0.01f) && PB->BlendInSeconds < 0.f
			&& FMath::IsNearlyEqual(PB->BlendOutSeconds, 1.25f) && PB->LongAxisCurve.Num() == 3 && FMath::IsNearlyEqual(PB->LongAxisCurve[1], -80.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstAidPatientHeadingTest, "CodexTactics.Survival.FirstAid.PatientHeading",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstAidPatientHeadingTest::RunTest(const FString&)
{
	using namespace FirstAidRules;
	// The counter-turn: the clip's body lies -153 deg off his forward -> his capsule turns +153 x the clip's weight.
	TestEqual(TEXT("weight 0: the base yaw"), CompensatedYaw(10.f, -153.f, 0.f), 10.f, 0.01f);
	TestEqual(TEXT("weight 1: base + 153"), CompensatedYaw(10.f, -153.f, 1.f), 163.f, 0.01f);
	TestEqual(TEXT("weight 0.5: half"), CompensatedYaw(10.f, -153.f, 0.5f), 86.5f, 0.01f);
	TestEqual(TEXT("wraps to (-180, 180]"), CompensatedYaw(170.f, -153.f, 1.f), -37.f, 0.01f);
	TestEqual(TEXT("a weight over 1 is clamped"), CompensatedYaw(0.f, 90.f, 3.f), -90.f, 0.01f);
	TestEqual(TEXT("a bad weight = 0"), CompensatedYaw(5.f, 90.f, std::numeric_limits<float>::quiet_NaN()), 5.f, 0.01f);
	// World body heading = capsule yaw + axis x weight: constant through the blend.
	for (float Weight = 0.f; Weight <= 1.f; Weight += 0.25f)
	{
		TestEqual(FString::Printf(TEXT("body heading held at weight %.2f"), Weight),
			FRotator3f::NormalizeAxis(CompensatedYaw(40.f, -153.f, Weight) + -153.f * Weight), 40.f, 0.01f);
	}
	// The measured turn curve (S-shaped) vs the linear fallback.
	const TArray<float> Curve = { 0.f, -10.f, -40.f, -100.f, -150.f };
	TestEqual(TEXT("curve: weight 0"), BodyTurnAt(Curve, -150.f, 0.f), 0.f, 0.01f);
	TestEqual(TEXT("curve: a sample (0.5 -> -40)"), BodyTurnAt(Curve, -150.f, 0.5f), -40.f, 0.01f);
	TestEqual(TEXT("curve: between samples (0.625 -> -70)"), BodyTurnAt(Curve, -150.f, 0.625f), -70.f, 0.01f);
	TestEqual(TEXT("curve: weight 1"), BodyTurnAt(Curve, -150.f, 1.f), -150.f, 0.01f);
	TestEqual(TEXT("curve: clamped above 1"), BodyTurnAt(Curve, -150.f, 2.f), -150.f, 0.01f);
	TestEqual(TEXT("no curve: linear"), BodyTurnAt(TArray<float>(), -150.f, 0.5f), -75.f, 0.01f);
	TestEqual(TEXT("a one-sample curve: linear"), BodyTurnAt(TArray<float>{ 5.f }, -150.f, 0.5f), -75.f, 0.01f);
	TestEqual(TEXT("the capsule counter-turns the curve"), CompensatedYaw(20.f, BodyTurnAt(Curve, -150.f, 0.75f), 1.f), 120.f, 0.01f);
	// The turn.
	TestEqual(TEXT("limited by the rate"), TurnToward(0.f, 90.f, 180.f, 0.1f), 18.f, 0.01f);
	TestEqual(TEXT("arrives without overshoot"), TurnToward(80.f, 90.f, 180.f, 0.1f), 90.f, 0.01f);
	TestEqual(TEXT("the short way across 180"), TurnToward(170.f, -170.f, 100.f, 0.05f), 175.f, 0.01f);
	TestEqual(TEXT("rate 0: at once"), TurnToward(0.f, 135.f, 0.f, 0.016f), 135.f, 0.01f);
	// The smoke's measure: the fastest turn over 0.2 s windows.
	TArray<FVector2D> Slow;
	for (int32 Step = 0; Step <= 60; ++Step)
	{
		Slow.Add(FVector2D(Step / 60.0, 170.0 + 30.0 * Step / 60.0)); // 30 deg/s, across +-180
	}
	TestEqual(TEXT("30 deg/s across the wrap"), MaxTurnRate(Slow, 0.2f), 30.f, 0.5f);
	TArray<FVector2D> Spin;
	for (int32 Step = 0; Step <= 60; ++Step)
	{
		const double T = Step / 60.0;
		Spin.Add(FVector2D(T, T < 0.5 ? 0.0 : (T < 0.75 ? -150.0 * (T - 0.5) / 0.25 : -150.0))); // the old 0.25 s blend: 150 deg
	}
	TestTrue(TEXT("the old spin: ~600 deg/s"), MaxTurnRate(Spin, 0.2f) > 500.f);
	TestEqual(TEXT("one sample: 0"), MaxTurnRate(TArray<FVector2D>{ FVector2D(0.0, 5.0) }, 0.2f), 0.f);
	TestEqual(TEXT("a series shorter than the window: one span"), MaxTurnRate(TArray<FVector2D>{ FVector2D(0.0, 0.0), FVector2D(0.1, 3.0) }, 0.2f), 30.f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstAidHelperSpotTest, "CodexTactics.Survival.FirstAid.HelperSpot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFirstAidHelperSpotTest::RunTest(const FString&)
{
	using namespace FirstAidRules;
	FFirstAidConfig Config;
	Config.ApproachStopCm = 85.f;
	Config.Patient.HelperChestCm = 20.f;
	Config.Patient.bHelperBesideLying = true;
	const FVector Patient(0.f, 0.f, 10.f);
	const FVector Forward(1.f, 0.f, 0.f); // he lies along +X (head forward), his right is +Y
	TestTrue(TEXT("lying, the helper behind-right: his right side, level with the chest"),
		HelperGoal(Config, true, Patient, Forward, FVector(-600.f, 100.f, 0.f)).Equals(FVector(20.f, 85.f, 10.f), 0.01f));
	TestTrue(TEXT("lying, the helper behind-left: his left side"),
		HelperGoal(Config, true, Patient, Forward, FVector(-600.f, -100.f, 0.f)).Equals(FVector(20.f, -85.f, 10.f), 0.01f));
	TestTrue(TEXT("lying, the helper straight behind: the right side (a side, never his feet)"),
		HelperGoal(Config, true, Patient, Forward, FVector(-600.f, 0.f, 0.f)).Equals(FVector(20.f, 85.f, 10.f), 0.01f));
	TestTrue(TEXT("not lying: on the helper's side, approach_stop_cm away (the old spot)"),
		HelperGoal(Config, false, Patient, Forward, FVector(-600.f, 0.f, 10.f)).Equals(FVector(-85.f, 0.f, 10.f), 0.01f));
	Config.Patient.bHelperBesideLying = false;
	TestTrue(TEXT("helper_beside_lying off: the old spot for a lying patient too"),
		HelperGoal(Config, true, Patient, Forward, FVector(-600.f, 100.f, 10.f)).Equals(Patient + FVector(-600.f, 100.f, 0.f).GetSafeNormal2D() * 85.f, 0.01f));
	TestTrue(TEXT("the face point: his chest"), HelperFacePoint(Config, true, Patient, Forward).Equals(FVector(20.f, 0.f, 10.f), 0.01f));
	TestTrue(TEXT("the face point of a sitting patient: himself"), HelperFacePoint(Config, false, Patient, Forward).Equals(Patient, 0.01f));

	// G023: HelperFaceYaw derives chest position from PatientBaseYaw and validates pre-turn readiness.
	const FVector ChestBehindHelper(20.f, 0.f, 10.f);
	const FVector HelperInFrontOfPatient(120.f, 0.f, 10.f);
	const float ExpectedYawBehind = static_cast<float>(FRotator::NormalizeAxis((ChestBehindHelper - HelperInFrontOfPatient).Rotation().Yaw));
	TestEqual(TEXT("HelperFaceYaw points from helper to patient's chest using PatientBaseYaw"),
		HelperFaceYaw(Config, true, Patient, 0.f, HelperInFrontOfPatient), ExpectedYawBehind, 0.05f);
	TestFalse(TEXT("Helper facing away (180 deg) is not ready"), IsHelperFacingReady(0.f, ExpectedYawBehind));
	TestFalse(TEXT("Helper facing side (90 deg) is not ready"), IsHelperFacingReady(-90.f, ExpectedYawBehind));
	TestTrue(TEXT("Helper within 15 deg is ready for montage"), IsHelperFacingReady(ExpectedYawBehind + 10.f, ExpectedYawBehind));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
