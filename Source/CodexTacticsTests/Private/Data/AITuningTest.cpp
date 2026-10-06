// CodexTactics.AI.Tuning.* — the Jev AI coach's ai_tuning.json is applied to Codex.* console variables only.

#include "Data/AITuning.h"
#include "Data/EnemyPerception.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAITuningAppliesCodexCVarsTest, "CodexTactics.AI.Tuning.AppliesCodexCVarsOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAITuningAppliesCodexCVarsTest::RunTest(const FString& Parameters)
{
	IConsoleVariable* Damage = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Marksman.ShotDamage"));
	if (!TestNotNull(TEXT("Codex.Marksman.ShotDamage exists"), Damage))
	{
		return false;
	}
	const FString Before = Damage->GetString();
	const int32 Applied = AITuning::ApplyJson(
		TEXT("{\"cvars\": {\"Codex.Marksman.ShotDamage\": \"33\", \"r.ScreenPercentage\": \"10\", \"Codex.Unknown\": \"1\"}}"));
	TestEqual(TEXT("only the existing Codex.* variable is set"), Applied, 1);
	TestEqual(TEXT("value applied"), Damage->GetFloat(), 33.f);
	TestEqual(TEXT("no cvars object -> nothing"), AITuning::ApplyJson(TEXT("{\"x\": 1}")), 0);
	TestEqual(TEXT("broken JSON -> nothing"), AITuning::ApplyJson(TEXT("{")), 0);
	Damage->Set(*Before, ECVF_SetByGameSetting);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAITuningStealthKnobsTest, "CodexTactics.AI.Tuning.StealthKnobs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAITuningStealthKnobsTest::RunTest(const FString& Parameters)
{
	// The Jev AI coach's stealth knobs (jev_ai_coach.py --stealth) exist as Codex.* variables, so ai_tuning.json can set them.
	const TCHAR* Knobs[] = { TEXT("Codex.Perception.SightRangeScale"), TEXT("Codex.Perception.FovScale"), TEXT("Codex.Perception.ProneVisibilityScale"),
		TEXT("Codex.Perception.HearingScale"), TEXT("Codex.Perception.SmellScale"), TEXT("Codex.Perception.TimeToDetectScale"),
		TEXT("Codex.Patrol.SearchSeconds"), TEXT("Codex.Patrol.SearchRadius"), TEXT("Codex.Patrol.SearchSpeedScale") };
	for (const TCHAR* Name : Knobs)
	{
		TestNotNull(FString::Printf(TEXT("%s exists"), Name), IConsoleManager::Get().FindConsoleVariable(Name));
	}
	// Defaults keep the data values.
	const FEnemyPerceptionParams Hound = PerceptionRules::GetArchetypeDefaults(EEnemyArchetype::FrostHound);
	const FPerceptionTuning Neutral;
	const FEnemyPerceptionParams Same = EnemyPerception::ApplyTuning(Hound, Neutral);
	TestEqual(TEXT("neutral: sight kept"), Same.SightRangeCm, Hound.SightRangeCm);
	TestEqual(TEXT("neutral: smell kept"), Same.SmellRadiusCm, Hound.SmellRadiusCm);
	const FPatrolSearchParams Search;
	TestEqual(TEXT("neutral: search time kept"), EnemyPerception::ApplyTuning(Search, Neutral).DurationSeconds, Search.DurationSeconds);
	// Scales / overrides.
	FPerceptionTuning Tuned;
	Tuned.SightRangeScale = 2.f;
	Tuned.FovScale = 4.f;
	Tuned.HearingScale = 0.5f;
	Tuned.SmellScale = 0.f;
	Tuned.TimeToDetectScale = 2.f;
	Tuned.SearchSeconds = 30.f;
	Tuned.SearchRadiusCm = 800.f;
	Tuned.SearchSpeedScale = 0.5f;
	const FEnemyPerceptionParams Out = EnemyPerception::ApplyTuning(Hound, Tuned);
	TestEqual(TEXT("sight x2"), Out.SightRangeCm, Hound.SightRangeCm * 2.f);
	TestEqual(TEXT("FOV clamped to 180"), Out.SightHalfAngleDeg, 180.f);
	TestEqual(TEXT("hearing x0.5"), Out.HearWalkCm, Hound.HearWalkCm * 0.5f);
	TestEqual(TEXT("gunshot x0.5"), Out.HearGunshotCm, Hound.HearGunshotCm * 0.5f);
	TestEqual(TEXT("smell off"), Out.SmellRadiusCm, 0.f);
	TestEqual(TEXT("time to detect x2"), Out.TimeToDetectSeconds, Hound.TimeToDetectSeconds * 2.f);
	const FPatrolSearchParams TunedSearch = EnemyPerception::ApplyTuning(Search, Tuned);
	TestEqual(TEXT("search 30 s"), TunedSearch.DurationSeconds, 30.f);
	TestEqual(TEXT("search radius 8 m"), TunedSearch.SweepRadiusCm, 800.f);
	TestEqual(TEXT("search pace x0.5"), TunedSearch.SpeedMultiplier, Search.SpeedMultiplier * 0.5f);

	// ai_tuning.json reaches them like every other Codex.* knob.
	IConsoleVariable* Hearing = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Perception.HearingScale"));
	if (TestNotNull(TEXT("hearing knob"), Hearing))
	{
		const FString Before = Hearing->GetString();
		TestEqual(TEXT("json sets the stealth knob"), AITuning::ApplyJson(TEXT("{\"cvars\": {\"Codex.Perception.HearingScale\": \"0.75\"}}")), 1);
		TestEqual(TEXT("hearing knob value"), Hearing->GetFloat(), 0.75f);
		TestEqual(TEXT("applied to the archetype data"), EnemyPerception::Get(EEnemyArchetype::Spitter).HearWalkCm,
			PerceptionRules::GetArchetypeDefaults(EEnemyArchetype::Spitter).HearWalkCm * 0.75f, 0.01f);
		Hearing->Set(*Before, ECVF_SetByGameSetting);
	}
	return true;
}
