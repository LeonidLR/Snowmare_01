#include "Vehicles/VehicleCombatRules.h"

// --- Fade timeline -------------------------------------------------------------------------------------------------

bool FVehicleFadeTimeline::Request(const FVehicleFadeConfig& Config)
{
	switch (Phase)
	{
	case EVehicleFadePhase::Idle:
		Phase = EVehicleFadePhase::FadeOut;
		PhaseTime = 0.f;
		HoldTime = 0.f;
		bLateJoin = false;
		return true;
	case EVehicleFadePhase::FadeOut:
		return true;
	case EVehicleFadePhase::Hold:
		// Late joiner: executed now on black; the hold is stretched a little for him (bounded by MaxHoldSeconds).
		bLateJoin = true;
		return true;
	case EVehicleFadePhase::FadeIn:
	default:
		bQueued = true;
		return false;
	}
}

bool FVehicleFadeTimeline::Advance(float DeltaSeconds, const FVehicleFadeConfig& Config)
{
	const float Dt = FMath::Max(0.f, DeltaSeconds);
	bool bBecameBlack = false;
	switch (Phase)
	{
	case EVehicleFadePhase::FadeOut:
		PhaseTime += Dt;
		if (PhaseTime >= Config.FadeOutSeconds)
		{
			Phase = EVehicleFadePhase::Hold;
			PhaseTime = 0.f;
			HoldTime = 0.f;
			bBecameBlack = true;
		}
		break;
	case EVehicleFadePhase::Hold:
	{
		PhaseTime += Dt;
		HoldTime += Dt;
		const float MinHold = bLateJoin ? Config.HoldSeconds * 2.f : Config.HoldSeconds;
		if (PhaseTime >= FMath::Min(MinHold, Config.MaxHoldSeconds) || HoldTime >= Config.MaxHoldSeconds)
		{
			Phase = EVehicleFadePhase::FadeIn;
			PhaseTime = 0.f;
			bLateJoin = false;
		}
		break;
	}
	case EVehicleFadePhase::FadeIn:
		PhaseTime += Dt;
		if (PhaseTime >= Config.FadeInSeconds)
		{
			Phase = EVehicleFadePhase::Idle;
			PhaseTime = 0.f;
			if (bQueued)
			{
				bQueued = false;
				Phase = EVehicleFadePhase::FadeOut;
			}
		}
		break;
	default:
		break;
	}
	return bBecameBlack;
}

float FVehicleFadeTimeline::GetAlpha(const FVehicleFadeConfig& Config) const
{
	switch (Phase)
	{
	case EVehicleFadePhase::FadeOut:
		return Config.FadeOutSeconds > 0.f ? FMath::Clamp(PhaseTime / Config.FadeOutSeconds, 0.f, 1.f) : 1.f;
	case EVehicleFadePhase::Hold:
		return 1.f;
	case EVehicleFadePhase::FadeIn:
		return Config.FadeInSeconds > 0.f ? FMath::Clamp(1.f - PhaseTime / Config.FadeInSeconds, 0.f, 1.f) : 0.f;
	default:
		return 0.f;
	}
}

namespace VehicleCombatRules
{
	// --- Turret ------------------------------------------------------------------------------------------------------

	bool IsInDeadZone(float HullDistanceCm, const FVehicleTurretConfig& Config)
	{
		return HullDistanceCm < Config.DeadZoneCm;
	}

	float GetMaxRangeCm(int32 ShellsLeft, bool bMgOverheated, const FVehicleTurretConfig& Config)
	{
		return FMath::Max(ShellsLeft > 0 ? Config.CannonRangeCm : 0.f, bMgOverheated ? 0.f : Config.MgRangeCm);
	}

	bool IsEngageable(const FTurretCandidate& Candidate, const FVehicleTurretConfig& Config)
	{
		return Candidate.bAlive && Candidate.bVisible && !IsInDeadZone(Candidate.HullDistanceCm, Config)
			&& Candidate.DistanceCm <= FMath::Max(Config.CannonRangeCm, Config.MgRangeCm);
	}

	int32 ChooseTarget(const TArray<FTurretCandidate>& Candidates, const FTurretContext& Context, const FVehicleTurretConfig& Config)
	{
		// A direct order (Ctrl+click) is obeyed in every posture while the target can be engaged.
		if (Candidates.IsValidIndex(Context.PriorityIndex) && IsEngageable(Candidates[Context.PriorityIndex], Config))
		{
			return Context.PriorityIndex;
		}
		if (Context.Posture == ESquadFirePosture::Passive || (Context.Posture == ESquadFirePosture::Defensive && !Context.bProvoked))
		{
			return INDEX_NONE;
		}
		const float Reach = FMath::Min(GetMaxRangeCm(Context.ShellsLeft, Context.bMgOverheated, Config),
			Context.Posture == ESquadFirePosture::Defensive ? Config.DefensiveRadiusCm : TNumericLimits<float>::Max());
		int32 Best = INDEX_NONE;
		float BestScore = TNumericLimits<float>::Max();
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			const FTurretCandidate& Candidate = Candidates[Index];
			if (!IsEngageable(Candidate, Config) || Candidate.DistanceCm > Reach)
			{
				continue;
			}
			// A large enemy close to the hull is the real threat (design §1.5): first; else the nearest.
			const bool bThreat = Candidate.bLarge && Candidate.DistanceCm <= Config.DefensiveRadiusCm;
			const float Score = Candidate.DistanceCm - (bThreat ? 100000.f : 0.f);
			if (Score < BestScore)
			{
				BestScore = Score;
				Best = Index;
			}
		}
		return Best;
	}

	EVehicleTurretWeapon ChooseWeapon(const FTurretCandidate& Target, bool bPriority, int32 ShellsLeft, bool bMgOverheated,
		const FVehicleTurretConfig& Config)
	{
		const bool bCannonReady = ShellsLeft > 0 && Target.DistanceCm <= Config.CannonRangeCm;
		const bool bMgReady = !bMgOverheated && Target.DistanceCm <= Config.MgRangeCm;
		const bool bCannonTarget = bPriority || Target.bLarge || Target.bMarksman || Target.NeighbourCount + 1 >= Config.CannonGroupCount;
		if (bCannonTarget && bCannonReady)
		{
			return EVehicleTurretWeapon::Cannon;
		}
		if (bMgReady)
		{
			return EVehicleTurretWeapon::MachineGun;
		}
		// MG hot / out of its reach: the cannon only for the targets that deserve a shell.
		return bCannonTarget && bCannonReady ? EVehicleTurretWeapon::Cannon : EVehicleTurretWeapon::None;
	}

	float StepAngle(float CurrentDeg, float DesiredDeg, float RateDegPerSec, float DeltaSeconds)
	{
		const float Delta = FMath::FindDeltaAngleDegrees(CurrentDeg, DesiredDeg);
		const float MaxStep = FMath::Max(0.f, RateDegPerSec) * FMath::Max(0.f, DeltaSeconds);
		return FRotator::NormalizeAxis(CurrentDeg + FMath::Clamp(Delta, -MaxStep, MaxStep));
	}

	float ClampElevation(float PitchDeg, const FVehicleTurretConfig& Config)
	{
		return FMath::Clamp(PitchDeg, Config.MinElevationDeg, Config.MaxElevationDeg);
	}

	bool IsAimed(float YawErrorDeg, float PitchErrorDeg, const FVehicleTurretConfig& Config)
	{
		return FMath::Abs(YawErrorDeg) <= Config.AimToleranceDeg && FMath::Abs(PitchErrorDeg) <= Config.AimToleranceDeg * 2.f;
	}

	float StepHeat(float Heat, bool bFiring, float DeltaSeconds, bool& bInOutOverheated, const FVehicleTurretConfig& Config)
	{
		const float Dt = FMath::Max(0.f, DeltaSeconds);
		float Next = Heat;
		if (bFiring && !bInOutOverheated)
		{
			Next += Dt / FMath::Max(0.1f, Config.MgOverheatSeconds);
		}
		else
		{
			Next -= Dt / FMath::Max(0.1f, Config.MgCooldownSeconds);
		}
		Next = FMath::Clamp(Next, 0.f, 1.f);
		if (Next >= 1.f - KINDA_SMALL_NUMBER)
		{
			bInOutOverheated = true;
		}
		else if (Next <= 0.f)
		{
			bInOutOverheated = false;
		}
		return Next;
	}

	float AddSuppression(float Meter, float Added, bool& bOutPinned, const FVehicleTurretConfig& Config)
	{
		const float Next = FMath::Max(0.f, Meter) + FMath::Max(0.f, Added);
		bOutPinned = Next >= Config.MgSuppressionThreshold;
		return bOutPinned ? 0.f : Next;
	}

	float GetCannonDamage(float DistanceFromImpactCm, bool bDirectHit, const FVehicleTurretConfig& Config)
	{
		if (bDirectHit)
		{
			return Config.CannonDamage;
		}
		return DistanceFromImpactCm <= Config.CannonSplashRadiusCm ? Config.CannonSplashDamage : 0.f;
	}

	int32 GetShellsForMap(int32 LevelOverride, const FVehicleTurretConfig& Config)
	{
		return LevelOverride >= 0 ? LevelOverride : FMath::Max(0, Config.CannonShellsPerMap);
	}

	FString GetHudLine(bool bHasGunner, int32 ShellsLeft, int32 ShellsMax, float MgHeat, bool bMgOverheated, ESquadFirePosture Posture)
	{
		if (!bHasGunner)
		{
			return FString::Printf(TEXT("TURRET: NO GUNNER   30mm %d/%d"), ShellsLeft, ShellsMax);
		}
		const FString Mg = bMgOverheated ? TEXT("MG OVERHEAT") : FString::Printf(TEXT("MG heat %d%%"), FMath::RoundToInt(FMath::Clamp(MgHeat, 0.f, 1.f) * 100.f));
		return FString::Printf(TEXT("TURRET %s   30mm %d/%d   %s"), *FirePostureRules::GetShortLabel(Posture), ShellsLeft, ShellsMax, *Mg);
	}

	// --- Run over / ram ------------------------------------------------------------------------------------------------

	bool IsCrushable(EEnemyArchetype Archetype, const FVehicleCrushConfig& Config)
	{
		return Config.CrushableArchetypes.Contains(Archetype) && !Config.RammedArchetypes.Contains(Archetype);
	}

	bool IsRammed(EEnemyArchetype Archetype, const FVehicleCrushConfig& Config)
	{
		return Config.RammedArchetypes.Contains(Archetype);
	}

	EVehicleContact EvaluateContact(const FContactInput& Contact, float SpeedCmS, const FVehicleCrushConfig& Config)
	{
		if (!Contact.bAlive || Contact.bFlying)
		{
			return EVehicleContact::None;
		}
		if (Contact.Kind != EContactKind::Enemy)
		{
			return EVehicleContact::Brake; // never runs over the squad, recruits or NPCs
		}
		const float Speed = FMath::Abs(SpeedCmS);
		if (IsRammed(Contact.Archetype, Config))
		{
			if (Contact.bDowned)
			{
				return Config.bCrushDownedRammed && Speed >= Config.MinCrushSpeedCmS ? EVehicleContact::Crush : EVehicleContact::Block;
			}
			return Speed >= Config.RamKnockdownSpeedCmS && !Contact.bKnockdownImmune ? EVehicleContact::KnockDown : EVehicleContact::Block;
		}
		if (IsCrushable(Contact.Archetype, Config))
		{
			return Speed >= Config.MinCrushSpeedCmS ? EVehicleContact::Crush : EVehicleContact::Push;
		}
		return EVehicleContact::Block;
	}

	float GetSpeedAfterImpacts(float SpeedCmS, int32 Crushed, bool bRammed, const FVehicleCrushConfig& Config)
	{
		float Speed = SpeedCmS * FMath::Pow(FMath::Clamp(Config.CrushSpeedFactor, 0.f, 1.f), static_cast<float>(FMath::Max(0, Crushed)));
		if (bRammed)
		{
			Speed *= FMath::Clamp(Config.RamSpeedFactor, 0.f, 1.f);
		}
		return Speed;
	}

	// --- Group boarding fade ---------------------------------------------------------------------------------------------

	bool ShouldStartGroupFade(float ReadyWaitSeconds, const TArray<float>& OtherWalkersEtaSeconds, const FVehicleFadeConfig& Config)
	{
		const float Left = Config.GroupWaitSeconds - ReadyWaitSeconds;
		if (Left <= 0.f)
		{
			return true;
		}
		for (const float Eta : OtherWalkersEtaSeconds)
		{
			if (Eta <= Left)
			{
				return false; // a mate is about to arrive: one fade for both
			}
		}
		return true;
	}

	float EstimateArrivalSeconds(float DistanceCm, float SpeedCmS, float MinSpeedCmS, float PathFactor)
	{
		return FMath::Max(0.f, DistanceCm) * FMath::Max(1.f, PathFactor) / FMath::Max(FMath::Max(1.f, MinSpeedCmS), SpeedCmS);
	}
}
