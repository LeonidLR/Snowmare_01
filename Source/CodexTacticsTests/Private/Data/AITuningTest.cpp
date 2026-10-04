// CodexTactics.AI.Tuning.* — the Jev AI coach's ai_tuning.json is applied to Codex.* console variables only.

#include "Data/AITuning.h"
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
