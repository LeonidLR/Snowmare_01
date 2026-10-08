#include "Characters/FirePostureRules.h"

#include "GameFlow/LevelEncounterRules.h"

namespace FirePostureRules
{
	ESquadFirePosture Resolve(ESquadFirePosture SquadPosture, bool bHasOverride, ESquadFirePosture Override)
	{
		return bHasOverride ? Override : SquadPosture;
	}

	bool MayAutoFire(ESquadFirePosture Posture, bool bSelfAttacked, bool bSquadAttacked, const FFirePostureConfig& Config)
	{
		switch (Posture)
		{
		case ESquadFirePosture::Passive:
			return false;
		case ESquadFirePosture::Defensive:
			return bSelfAttacked || (Config.bDefensiveSquadWideProvocation && bSquadAttacked);
		default:
			return true;
		}
	}

	bool MayFire(ESquadFirePosture Posture, bool bDirectOrder, bool bSelfAttacked, bool bSquadAttacked, const FFirePostureConfig& Config)
	{
		return bDirectOrder || MayAutoFire(Posture, bSelfAttacked, bSquadAttacked, Config);
	}

	bool MayAutoFireInExploration(ESquadFirePosture Posture, bool bAmbushLevel, ECodexGamePhase Phase, bool bCombatUnlocked,
		const FFirePostureConfig& Config)
	{
		return Posture == ESquadFirePosture::Aggressive && Config.bAggressiveOpensFireInExploration
			&& LevelEncounterRules::ShouldStartAmbush(bAmbushLevel, Phase, bCombatUnlocked);
	}

	bool ClearsProvocation(ECodexGamePhase NewPhase)
	{
		return NewPhase == ECodexGamePhase::WaveCleared || NewPhase == ECodexGamePhase::PostCombat
			|| NewPhase == ECodexGamePhase::Exploration || NewPhase == ECodexGamePhase::GameOver;
	}

	EPostureOrderScope GetOrderScope(bool bSquadWideModifier, int32 NumSelected)
	{
		return bSquadWideModifier || NumSelected <= 0 ? EPostureOrderScope::Squad : EPostureOrderScope::Selected;
	}

	bool GetCommonPosture(const TArray<ESquadFirePosture>& Postures, ESquadFirePosture& OutCommon)
	{
		if (Postures.IsEmpty())
		{
			return false;
		}
		for (const ESquadFirePosture Posture : Postures)
		{
			if (Posture != Postures[0])
			{
				return false;
			}
		}
		OutCommon = Postures[0];
		return true;
	}

	FString GetLetter(ESquadFirePosture Posture)
	{
		switch (Posture)
		{
		case ESquadFirePosture::Passive: return TEXT("P");
		case ESquadFirePosture::Defensive: return TEXT("D");
		default: return TEXT("A");
		}
	}

	ESquadFirePosture Next(ESquadFirePosture Posture)
	{
		switch (Posture)
		{
		case ESquadFirePosture::Passive: return ESquadFirePosture::Defensive;
		case ESquadFirePosture::Defensive: return ESquadFirePosture::Aggressive;
		default: return ESquadFirePosture::Passive;
		}
	}

	FString GetLabel(ESquadFirePosture Posture)
	{
		switch (Posture)
		{
		case ESquadFirePosture::Passive: return TEXT("PASSIVE");
		case ESquadFirePosture::Defensive: return TEXT("DEFENSIVE");
		default: return TEXT("AGGRESSIVE");
		}
	}

	FString GetShortLabel(ESquadFirePosture Posture)
	{
		switch (Posture)
		{
		case ESquadFirePosture::Passive: return TEXT("PAS");
		case ESquadFirePosture::Defensive: return TEXT("DEF");
		default: return TEXT("AGG");
		}
	}

	FString GetKeyHint(ESquadFirePosture Posture)
	{
		switch (Posture)
		{
		case ESquadFirePosture::Passive: return TEXT(",");
		case ESquadFirePosture::Defensive: return TEXT(".");
		default: return TEXT("/");
		}
	}
}
