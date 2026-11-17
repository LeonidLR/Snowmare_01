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
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
