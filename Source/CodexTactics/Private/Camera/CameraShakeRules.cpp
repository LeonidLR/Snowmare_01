#include "Camera/CameraShakeRules.h"
#include "Data/GodotBalanceAsset.h"
#include "Internationalization/Text.h"

FCameraShakeConfig CameraShakeRules::ConfigFromBalance(const UGodotBalanceAsset* Balance)
{
	FCameraShakeConfig Config;
	if (Balance)
	{
		Config.Amplitude = Balance->GetNumber(TEXT("camera_shake_amplitude"), Config.Amplitude);
		Config.PowerRifle = Balance->GetNumber(TEXT("camera_shake_power_rifle"), Config.PowerRifle);
		Config.PowerPistol = Balance->GetNumber(TEXT("camera_shake_power_pistol"), Config.PowerPistol);
		Config.PowerTurret = Balance->GetNumber(TEXT("camera_shake_power_turret"), Config.PowerTurret);
		Config.Decay = Balance->GetNumber(TEXT("camera_shake_decay"), Config.Decay);
		Config.Frequency = Balance->GetNumber(TEXT("camera_shake_frequency"), Config.Frequency);
	}
	return Config;
}

float CameraShakeRules::GetPower(const FCameraShakeConfig& Config, const FString& WeaponType)
{
	// FText lowers Cyrillic too (FString::ToLower does not); Godot to_lower is Unicode-aware.
	const FString Lower = FText::FromString(WeaponType).ToLower().ToString();
	if (Lower.Contains(TEXT("pistol")) || Lower.Contains(TEXT("пистолет")) /* legacy Russian data names */) // cyrillic-ok: legacy Russian data
	{
		return Config.PowerPistol;
	}
	if (Lower.Contains(TEXT("turret")) || Lower.Contains(TEXT("турел")) /* legacy Russian data names */) // cyrillic-ok: legacy Russian data
	{
		return Config.PowerTurret;
	}
	return Config.PowerRifle;
}

float CameraShakeRules::AddTrauma(float Trauma, float Amount)
{
	return FMath::Clamp(Trauma + Amount, 0.f, 1.f);
}

float CameraShakeRules::Decay(const FCameraShakeConfig& Config, float Trauma, float DeltaSeconds)
{
	return FMath::Max(0.f, Trauma - Config.Decay * DeltaSeconds);
}

float CameraShakeRules::GetOffsetScale(const FCameraShakeConfig& Config, float Trauma)
{
	return Config.Amplitude * Trauma * Trauma;
}
