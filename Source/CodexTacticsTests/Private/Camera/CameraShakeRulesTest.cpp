#include "Misc/AutomationTest.h"
#include "../GodotBalanceFixture.h"
#include "Camera/CameraShakeRules.h"
#include "Data/GodotBalanceAsset.h"

#if WITH_DEV_AUTOMATION_TESTS

// Godot Scenes/movements/camera.gd trigger_weapon_shake / add_trauma / _process_shake parity.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraShakeRulesTest, "CodexTactics.Camera.Shake.Rules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCameraShakeRulesTest::RunTest(const FString&)
{
	const FCameraShakeConfig Defaults = CameraShakeRules::ConfigFromBalance(nullptr);
	TestEqual(TEXT("Rifle"), CameraShakeRules::GetPower(Defaults, TEXT("rifle")), 0.35f);
	TestEqual(TEXT("Pistol"), CameraShakeRules::GetPower(Defaults, TEXT("Pistol")), 0.2f);
	TestEqual(TEXT("Turret (Russian)"), CameraShakeRules::GetPower(Defaults, TEXT("Турель")), 0.28f);
	TestEqual(TEXT("Unknown -> rifle"), CameraShakeRules::GetPower(Defaults, TEXT("shotgun")), 0.35f);

	float Trauma = CameraShakeRules::AddTrauma(0.f, 0.35f);
	Trauma = CameraShakeRules::AddTrauma(Trauma, 0.35f);
	Trauma = CameraShakeRules::AddTrauma(Trauma, 0.35f);
	TestEqual(TEXT("Trauma capped at 1"), Trauma, 1.f);
	TestEqual(TEXT("Decay 4 / s: 0.1 s -> 0.6"), CameraShakeRules::Decay(Defaults, Trauma, 0.1f), 0.6f, 0.0001f);
	TestEqual(TEXT("Never below 0"), CameraShakeRules::Decay(Defaults, 0.1f, 1.f), 0.f);
	TestEqual(TEXT("Offset scale = amplitude x trauma²"), CameraShakeRules::GetOffsetScale(Defaults, 0.5f), 0.18f * 0.25f, 0.0001f);

	const UGodotBalanceAsset* Config = GodotBalanceFixture::MakeGameBalanceConfig();
	if (!TestNotNull(TEXT("DA_GameBalanceConfig"), Config))
	{
		return false;
	}
	const FCameraShakeConfig Imported = CameraShakeRules::ConfigFromBalance(Config);
	TestTrue(TEXT("Imported camera_shake_* (game_balance_config.gd defaults)"), FMath::IsNearlyEqual(Imported.Amplitude, 0.18f)
		&& FMath::IsNearlyEqual(Imported.PowerRifle, 0.35f) && FMath::IsNearlyEqual(Imported.Decay, 4.f) && FMath::IsNearlyEqual(Imported.Frequency, 45.f));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
