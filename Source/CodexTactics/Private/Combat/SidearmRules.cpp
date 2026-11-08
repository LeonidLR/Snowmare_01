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
		float ThreatDistanceCm, bool bRaging)
	{
		return Config.bEnabled && !bHoldingSidearm && bPrimaryClipEmpty && bPrimaryHasReserve && bSidearmUsable && !bRaging
			&& ThreatDistanceCm >= 0.f && ThreatDistanceCm <= Config.ThreatRangeM * 100.f;
	}

	bool ShouldReturnToPrimary(const FSidearmConfig& Config, bool bDrawnByRule, float SecondsWithoutTarget, bool bSidearmDry)
	{
		return bDrawnByRule && (bSidearmDry || SecondsWithoutTarget >= Config.HolsterAfterSeconds);
	}
}
