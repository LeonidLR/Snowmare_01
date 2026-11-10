// CodexTactics.Squad.Regroup.* - the fast regroup on B (the game designer's bug 2026-11-10, SquadRegroupRules): a member far from the formation
// first runs / sprints to the leader (sprint out of combat, run in combat, the wounded run, the critically wounded left out) and joins the
// formation close to it; the squad_regroup.json tuning.

#include "Misc/AutomationTest.h"
#include "Characters/SquadRegroupRules.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

#define SQUAD_REGROUP_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Squad.Regroup." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

SQUAD_REGROUP_TEST(FSquadRegroupWhoTest, "WhoRegroupsFast")
bool FSquadRegroupWhoTest::RunTest(const FString&)
{
	using namespace SquadRegroupRules;
	const FSquadRegroupTuning Tuning;
	TestEqual(TEXT("Default start 12 m"), Tuning.StartCm, 1200.f);
	TestEqual(TEXT("Default join 6 m"), Tuning.JoinCm, 600.f);
	// The gap: the nearer of his slot and the leader (a slot beyond the leader is not "far").
	TestEqual(TEXT("Gap = the nearer of slot / leader"), FormationGapCm(4000.f, 3800.f), 3800.f);
	TestEqual(TEXT("Gap = slot when nearer"), FormationGapCm(300.f, 500.f), 300.f);
	// 40 m away: fast; 12 m exactly or less: straight into the formation.
	TestTrue(TEXT("40 m from the formation: fast regroup"), ShouldStartFastRegroup(4000.f, false, Tuning));
	TestTrue(TEXT("12.5 m: fast regroup"), ShouldStartFastRegroup(1250.f, false, Tuning));
	TestFalse(TEXT("12 m exactly: the formation"), ShouldStartFastRegroup(1200.f, false, Tuning));
	TestFalse(TEXT("8 m: the formation"), ShouldStartFastRegroup(800.f, false, Tuning));
	TestFalse(TEXT("Critically wounded (the prone crawl): never fast"), ShouldStartFastRegroup(4000.f, true, Tuning));
	// HP below 20 % of max = critical; max 0 never.
	TestTrue(TEXT("19 HP of 100: critical"), IsCriticallyWounded(19.f, 100.f, Tuning));
	TestFalse(TEXT("20 HP of 100: not critical"), IsCriticallyWounded(20.f, 100.f, Tuning));
	TestFalse(TEXT("Max health 0: not critical"), IsCriticallyWounded(0.f, 0.f, Tuning));
	return true;
}

SQUAD_REGROUP_TEST(FSquadRegroupJoinTest, "JoinsWhenClose")
bool FSquadRegroupJoinTest::RunTest(const FString&)
{
	using namespace SquadRegroupRules;
	const FSquadRegroupTuning Tuning;
	// Hysteresis: started beyond 12 m, he keeps running until within 6 m (between the two lines he does not drop back into the pacing).
	TestTrue(TEXT("9 m (started at 40 m): keeps running"), KeepsFastRegroup(900.f, false, Tuning));
	TestTrue(TEXT("6.1 m: keeps running"), KeepsFastRegroup(610.f, false, Tuning));
	TestFalse(TEXT("6 m: joins the formation"), KeepsFastRegroup(600.f, false, Tuning));
	TestFalse(TEXT("2 m: joins the formation"), KeepsFastRegroup(200.f, false, Tuning));
	TestFalse(TEXT("Critically wounded on the way: ends (the formation's crawl)"), KeepsFastRegroup(3000.f, true, Tuning));
	// The leader walking away only lengthens the gap - the chase goes on (the leader is never stopped).
	float Gap = 3000.f;
	for (int32 Step = 0; Step < 10; ++Step)
	{
		Gap += 150.f; // he walks away faster than the chaser closes this step
		TestTrue(*FString::Printf(TEXT("Leader walking away, gap %.0f cm: still chasing"), Gap), KeepsFastRegroup(Gap, false, Tuning));
	}
	return true;
}

SQUAD_REGROUP_TEST(FSquadRegroupPaceTest, "PaceSprintRunWounded")
bool FSquadRegroupPaceTest::RunTest(const FString&)
{
	using namespace SquadRegroupRules;
	FSquadRegroupTuning Tuning;
	TestTrue(TEXT("No combat, fit: sprint"), ChoosePace(false, false, true, Tuning) == ESquadRegroupPace::Sprint);
	TestTrue(TEXT("Combat: run (no sprint)"), ChoosePace(true, false, true, Tuning) == ESquadRegroupPace::Run);
	TestTrue(TEXT("No combat but no sprint allowed (cold / carrying): run"), ChoosePace(false, false, false, Tuning) == ESquadRegroupPace::Run);
	TestTrue(TEXT("Wounded, no combat: the wounded run, never a sprint"), ChoosePace(false, true, true, Tuning) == ESquadRegroupPace::WoundedRun);
	TestTrue(TEXT("Wounded, combat: the wounded run"), ChoosePace(true, true, false, Tuning) == ESquadRegroupPace::WoundedRun);
	Tuning.bSprintOutOfCombat = false;
	TestTrue(TEXT("sprint_out_of_combat 0: run out of combat too"), ChoosePace(false, false, true, Tuning) == ESquadRegroupPace::Run);

	const FSquadRegroupTuning Defaults;
	TestEqual(TEXT("Sprint = his sprint speed"), PaceSpeed(ESquadRegroupPace::Sprint, 725.f, 0.65f, 1.f, Defaults), 725.f);
	TestEqual(TEXT("Run = 480 cm/s"), PaceSpeed(ESquadRegroupPace::Run, 725.f, 0.65f, 1.f, Defaults), 480.f);
	TestEqual(TEXT("Run in the cold (x0.8) = 384 cm/s"), PaceSpeed(ESquadRegroupPace::Run, 725.f, 0.65f, 0.8f, Defaults), 384.f, 0.01f);
	TestEqual(TEXT("Wounded run = 480 x 0.65 = 312 cm/s"), PaceSpeed(ESquadRegroupPace::WoundedRun, 725.f, 0.65f, 1.f, Defaults), 312.f, 0.01f);
	// Every regroup pace beats the formation's walking catch-up (220 x 1.25 = 275 cm/s), the wounded one too.
	TestTrue(TEXT("The wounded run beats the formation catch-up"), PaceSpeed(ESquadRegroupPace::WoundedRun, 725.f, 0.65f, 1.f, Defaults) > 275.f);
	return true;
}

SQUAD_REGROUP_TEST(FSquadRegroupTuningTest, "TuningJson")
bool FSquadRegroupTuningTest::RunTest(const FString&)
{
	TSharedPtr<FJsonObject> Json;
	const FString Text = TEXT("{\"start_m\": 15, \"join_m\": 4, \"run_speed_cm_s\": 500, \"sprint_out_of_combat\": 0, \"critical_health_pct\": 25, \"stand_up_to_run\": false, \"repath_s\": 0.25}");
	TestTrue(TEXT("Parses"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) && Json.IsValid());
	if (!Json.IsValid())
	{
		return false;
	}
	const FSquadRegroupTuning Tuning = SquadRegroupTuning::FromJson(*Json);
	TestEqual(TEXT("start_m 15 -> 1500 cm"), Tuning.StartCm, 1500.f);
	TestEqual(TEXT("join_m 4 -> 400 cm"), Tuning.JoinCm, 400.f);
	TestEqual(TEXT("run_speed_cm_s"), Tuning.RunSpeed, 500.f);
	TestFalse(TEXT("sprint_out_of_combat 0"), Tuning.bSprintOutOfCombat);
	TestEqual(TEXT("critical_health_pct 25 -> 0.25"), Tuning.CriticalHealthFraction, 0.25f, 1e-4f);
	TestFalse(TEXT("stand_up_to_run false"), Tuning.bStandUpToRun);
	TestEqual(TEXT("repath_s"), Tuning.RepathSeconds, 0.25f, 1e-4f);

	TSharedPtr<FJsonObject> Bad;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(TEXT("{\"start_m\": 5, \"join_m\": 9}"))), Bad);
	const FSquadRegroupTuning Clamped = SquadRegroupTuning::FromJson(*Bad);
	TestEqual(TEXT("join never beyond start"), Clamped.JoinCm, 500.f);

	// The shipped file: the game designer's defaults (12 m / 6 m, sprint out of combat).
	FString FileText;
	TSharedPtr<FJsonObject> File;
	TestTrue(TEXT("squad_regroup.json readable"), FFileHelper::LoadFileToString(FileText, *SquadRegroupTuning::GetDefaultPath())
		&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FileText), File) && File.IsValid());
	if (File.IsValid())
	{
		const FSquadRegroupTuning Shipped = SquadRegroupTuning::FromJson(*File);
		TestEqual(TEXT("file start 12 m"), Shipped.StartCm, 1200.f);
		TestEqual(TEXT("file join 6 m"), Shipped.JoinCm, 600.f);
		TestTrue(TEXT("file sprints out of combat"), Shipped.bSprintOutOfCombat);
		TestEqual(TEXT("file critical 20 %"), Shipped.CriticalHealthFraction, 0.2f, 1e-4f);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
