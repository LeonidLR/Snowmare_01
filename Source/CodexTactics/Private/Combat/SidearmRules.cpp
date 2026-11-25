#include "Combat/SidearmRules.h"

#include "Dom/JsonObject.h"

namespace SidearmRules
{
	namespace SidearmLocal
	{
		FSidearmConfig Current;
	}

	FSidearmConfig Parse(const FJsonObject& Block, TArray<FString>* OutErrors)
	{
		FSidearmConfig Out;
		auto Error = [OutErrors](const FString& Text)
		{
			if (OutErrors)
			{
				OutErrors->Add(Text);
			}
		};
		bool bEnabled = true;
		if (Block.TryGetBoolField(TEXT("enabled"), bEnabled))
		{
			Out.bEnabled = bEnabled;
		}
		bool bReloadInCover = true;
		if (Block.TryGetBoolField(TEXT("reload_in_cover"), bReloadInCover))
		{
			Out.bReloadInCover = bReloadInCover;
		}
		FString Weapon;
		if (Block.TryGetStringField(TEXT("weapon"), Weapon) && !Weapon.IsEmpty())
		{
			Out.WeaponId = Weapon;
		}
		auto Number = [&Block, &Error](const TCHAR* Key, float& Value, float Min, float Max)
		{
			double Read = 0.0;
			if (!Block.TryGetNumberField(Key, Read))
			{
				return;
			}
			if (Read < Min || Read > Max)
			{
				Error(FString::Printf(TEXT("sidearm.%s %.2f is outside %.2f .. %.2f - kept %.2f"), Key, Read, Min, Max, Value));
				return;
			}
			Value = static_cast<float>(Read);
		};
		Number(TEXT("threat_range_m"), Out.ThreatRangeM, 0.f, 100.f);
		Number(TEXT("draw_s"), Out.DrawSeconds, 0.f, 3.f);
		Number(TEXT("holster_after_s"), Out.HolsterAfterSeconds, 0.f, 60.f);
		Number(TEXT("shotgun_switch_range_m"), Out.ShotgunSwitchRangeM, 0.f, 100.f);
		Number(TEXT("shotgun_switch_back_range_m"), Out.ShotgunSwitchBackRangeM, 0.f, 100.f);
		Out.ShotgunSwitchBackRangeM = FMath::Max(Out.ShotgunSwitchBackRangeM, Out.ShotgunSwitchRangeM);
		return Out;
	}

	void Set(const FSidearmConfig& Config)
	{
		SidearmLocal::Current = Config;
	}

	const FSidearmConfig& Get()
	{
		return SidearmLocal::Current;
	}

	bool ShouldDrawSidearm(const FSidearmConfig& Config, bool bHoldingSidearm, bool bPrimaryClipEmpty, bool bPrimaryHasReserve, bool bSidearmUsable,
		float ThreatDistanceCm, bool bRaging, bool bInCover)
	{
		return Config.bEnabled && !bHoldingSidearm && bPrimaryClipEmpty && bPrimaryHasReserve && bSidearmUsable && !bRaging
			&& !(Config.bReloadInCover && bInCover)
			&& ThreatDistanceCm >= 0.f && ThreatDistanceCm <= Config.ThreatRangeM * 100.f;
	}

	bool ShouldReturnToPrimary(const FSidearmConfig& Config, bool bDrawnByRule, float SecondsWithoutTarget, bool bSidearmDry)
	{
		return bDrawnByRule && (bSidearmDry || SecondsWithoutTarget >= Config.HolsterAfterSeconds);
	}

	EShotgunRifleSwitch ChooseShotgunOrRifle(const FSidearmConfig& Config, float NearestEnemyCm, bool bHasShotgun, bool bHasRifle,
		const FString& CurrentWeaponId, bool bPlayerPicked)
	{
		if (bPlayerPicked || !bHasShotgun || !bHasRifle || NearestEnemyCm < 0.f)
		{
			return EShotgunRifleSwitch::Keep;
		}
		const float SwitchCm = Config.ShotgunSwitchRangeM * 100.f;
		const float BackCm = FMath::Max(Config.ShotgunSwitchBackRangeM, Config.ShotgunSwitchRangeM) * 100.f;
		if (CurrentWeaponId != TEXT("shotgun") && NearestEnemyCm <= SwitchCm)
		{
			return EShotgunRifleSwitch::SwitchToShotgun;
		}
		if (CurrentWeaponId == TEXT("shotgun") && NearestEnemyCm > BackCm)
		{
			return EShotgunRifleSwitch::SwitchToRifle;
		}
		return EShotgunRifleSwitch::Keep;
	}
}
