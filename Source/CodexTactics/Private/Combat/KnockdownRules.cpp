#include "Combat/KnockdownRules.h"

#include "HAL/IConsoleManager.h"

namespace KnockdownTunables
{
	static TAutoConsoleVariable<float> CVarDamageThreshold(TEXT("Codex.Knockdown.DamageThreshold"), -1.f,
		TEXT("Single-hit damage that knocks down (-1 = default 40)."));
	static TAutoConsoleVariable<float> CVarExplosionRadius(TEXT("Codex.Knockdown.ExplosionRadius"), -1.f,
		TEXT("Blast distance that knocks down, cm (-1 = default 250)."));
	static TAutoConsoleVariable<float> CVarDownedSeconds(TEXT("Codex.Knockdown.DownedSeconds"), -1.f,
		TEXT("Seconds on the ground while the recovery bar fills (-1 = default 0.6)."));
	static TAutoConsoleVariable<float> CVarGetUpSeconds(TEXT("Codex.Knockdown.GetUpSeconds"), -1.f,
		TEXT("Wanted get-up time, s (-1 = default 0.8)."));
	static TAutoConsoleVariable<float> CVarGetUpMaxRate(TEXT("Codex.Knockdown.GetUpMaxPlayRate"), -1.f,
		TEXT("Fastest play rate of the get-up clip (-1 = default 2.1)."));
	static TAutoConsoleVariable<int32> CVarGetUpAP(TEXT("Codex.Knockdown.GetUpActionPoints"), -1,
		TEXT("Turn-based AP cost of getting up (-1 = default 2)."));
	static TAutoConsoleVariable<float> CVarChance(TEXT("Codex.Knockdown.Chance"), -1.f,
		TEXT("Probability 0..1 that a qualifying heavy hit / heavy melee blow knocks down; blasts and pounces always do (-1 = default 0.5)."));
	static TAutoConsoleVariable<float> CVarDownedDamage(TEXT("Codex.Knockdown.DownedDamage"), -1.f,
		TEXT("Damage multiplier for any blow on a unit lying after a knockdown (-1 = default 1.33)."));
	static TAutoConsoleVariable<float> CVarReknock(TEXT("Codex.Knockdown.ReknockImmunitySeconds"), -1.f,
		TEXT("No new knockdown this long after getting up, s (-1 = default 1.5)."));
}

namespace KnockdownRules
{
	const FKnockdownConfig& GetConfig()
	{
		static FKnockdownConfig Config;
		const FKnockdownConfig Defaults;
		auto Pick = [](float Override, float Default) { return Override >= 0.f ? Override : Default; };
		Config = Defaults;
		Config.DamageThreshold = Pick(KnockdownTunables::CVarDamageThreshold.GetValueOnGameThread(), Defaults.DamageThreshold);
		Config.ExplosionRadius = Pick(KnockdownTunables::CVarExplosionRadius.GetValueOnGameThread(), Defaults.ExplosionRadius);
		Config.DownedSeconds = Pick(KnockdownTunables::CVarDownedSeconds.GetValueOnGameThread(), Defaults.DownedSeconds);
		Config.GetUpSeconds = Pick(KnockdownTunables::CVarGetUpSeconds.GetValueOnGameThread(), Defaults.GetUpSeconds);
		Config.GetUpMaxPlayRate = Pick(KnockdownTunables::CVarGetUpMaxRate.GetValueOnGameThread(), Defaults.GetUpMaxPlayRate);
		const int32 AP = KnockdownTunables::CVarGetUpAP.GetValueOnGameThread();
		Config.GetUpActionPoints = AP >= 0 ? AP : Defaults.GetUpActionPoints;
		Config.KnockdownChance = FMath::Clamp(Pick(KnockdownTunables::CVarChance.GetValueOnGameThread(), Defaults.KnockdownChance), 0.f, 1.f);
		Config.DownedDamageMultiplier = Pick(KnockdownTunables::CVarDownedDamage.GetValueOnGameThread(), Defaults.DownedDamageMultiplier);
		Config.ReknockImmunitySeconds = Pick(KnockdownTunables::CVarReknock.GetValueOnGameThread(), Defaults.ReknockImmunitySeconds);
		return Config;
	}

	float ImpactAngleDeg(const FVector& ActorForward, const FVector& ToAttackSource)
	{
		const FVector Forward = FVector(ActorForward.X, ActorForward.Y, 0.f).GetSafeNormal();
		const FVector ToSource = FVector(ToAttackSource.X, ToAttackSource.Y, 0.f).GetSafeNormal();
		if (Forward.IsNearlyZero() || ToSource.IsNearlyZero())
		{
			return 0.f;
		}
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Forward, ToSource), -1.f, 1.f)));
	}

	EKnockdownDirection DirectionFromAttack(const FVector& ActorForward, const FVector& ToAttackSource)
	{
		return ImpactAngleDeg(ActorForward, ToAttackSource) < 90.f ? EKnockdownDirection::Back : EKnockdownDirection::Front;
	}

	EKnockdownDirection DirectionFromLocations(const FVector& ActorForward, const FVector& VictimLocation, const FVector& SourceLocation)
	{
		return DirectionFromAttack(ActorForward, SourceLocation - VictimLocation);
	}

	bool ShouldKnockDown(const FKnockdownConfig& Config, const FKnockdownHit& Hit, const FKnockdownTarget& Target)
	{
		if (!Target.bCanBeKnockedDown || Target.bAlreadyDown || Target.SecondsSinceGetUp < Config.ReknockImmunitySeconds)
		{
			return false;
		}
		bool bQualifies = false;
		switch (Hit.Cause)
		{
		case EKnockdownCause::Pounce:
		case EKnockdownCause::HeavyMelee:
			bQualifies = !Target.bHeavyPoise;
			break;
		case EKnockdownCause::Explosion:
			bQualifies = Hit.ExplosionDistance < Config.ExplosionRadius;
			break;
		case EKnockdownCause::HeavyHit:
			bQualifies = Hit.Damage >= Config.DamageThreshold && (!Target.bHeavyPoise || Hit.bCritical);
			break;
		default:
			break;
		}
		// User decision 2026-10-11: a qualifying heavy hit / blow floors him only with KnockdownChance (50 %).
		return bQualifies && (!UsesKnockdownChance(Hit.Cause) || Hit.ChanceRoll < Config.KnockdownChance);
	}

	bool UsesKnockdownChance(EKnockdownCause Cause)
	{
		return Cause == EKnockdownCause::HeavyHit || Cause == EKnockdownCause::HeavyMelee;
	}

	float DownedDamageMultiplier(const FKnockdownConfig& Config, bool bIsDown)
	{
		return bIsDown ? FMath::Max(0.f, Config.DownedDamageMultiplier) : 1.f;
	}

	float ClipPlayRate(float ClipSeconds, float TargetSeconds, float MaxRate)
	{
		if (ClipSeconds <= 0.f || TargetSeconds <= 0.f)
		{
			return 1.f;
		}
		return FMath::Clamp(ClipSeconds / TargetSeconds, 1.f, FMath::Max(1.f, MaxRate));
	}

	FKnockdownState Start(const FKnockdownConfig& Config, EKnockdownDirection Direction, EKnockdownCause Cause,
		float FallClipSeconds, float GetUpClipSeconds, bool bTurnBased)
	{
		FKnockdownState State;
		State.Phase = EKnockdownPhase::Falling;
		State.Direction = Direction == EKnockdownDirection::None ? EKnockdownDirection::Back : Direction;
		State.Cause = Cause;
		State.FallDuration = FallClipSeconds > 0.f ? FallClipSeconds : Config.FallSeconds;
		State.DownedDuration = FMath::Max(0.f, Config.DownedSeconds);
		State.GetUpDuration = GetUpClipSeconds > 0.f
			? GetUpClipSeconds / ClipPlayRate(GetUpClipSeconds, Config.GetUpSeconds, Config.GetUpMaxPlayRate)
			: Config.GetUpSeconds;
		State.bWaitForTurn = bTurnBased;
		return State;
	}

	bool AreTimersFrozen(bool bTacticalPause, bool bDialoguePause)
	{
		return bTacticalPause || bDialoguePause;
	}

	EKnockdownStep Advance(FKnockdownState& State, float DeltaSeconds, bool bFrozen)
	{
		if (bFrozen || DeltaSeconds <= 0.f || State.Phase == EKnockdownPhase::None)
		{
			return EKnockdownStep::None;
		}
		State.PhaseElapsed += DeltaSeconds;
		switch (State.Phase)
		{
		case EKnockdownPhase::Falling:
			if (State.PhaseElapsed >= State.FallDuration)
			{
				State.PhaseElapsed -= State.FallDuration;
				State.Phase = EKnockdownPhase::Downed;
				return EKnockdownStep::Landed;
			}
			break;
		case EKnockdownPhase::Downed:
			if (State.PhaseElapsed >= State.DownedDuration)
			{
				if (State.bWaitForTurn)
				{
					State.PhaseElapsed = State.DownedDuration;
					State.bGetUpReady = true;
					return EKnockdownStep::None;
				}
				State.PhaseElapsed -= State.DownedDuration;
				State.Phase = EKnockdownPhase::GettingUp;
				return EKnockdownStep::StartGetUp;
			}
			break;
		case EKnockdownPhase::GettingUp:
			if (State.PhaseElapsed >= State.GetUpDuration)
			{
				State = FKnockdownState();
				return EKnockdownStep::Recovered;
			}
			break;
		default:
			break;
		}
		return EKnockdownStep::None;
	}

	void SetTurnBased(FKnockdownState& State, bool bTurnBased)
	{
		State.bWaitForTurn = bTurnBased;
		if (!bTurnBased)
		{
			State.bGetUpReady = false;
		}
	}

	FKnockdownTurnDecision DecideTurnGetUp(const FKnockdownConfig& Config, const FKnockdownState& State, int32 ActionPoints)
	{
		FKnockdownTurnDecision Decision;
		if (State.Phase != EKnockdownPhase::Downed) // still falling: decided once he lies
		{
			return Decision;
		}
		if (ActionPoints >= Config.GetUpActionPoints)
		{
			Decision.bGetUp = true;
			Decision.ActionPointsSpent = Config.GetUpActionPoints;
		}
		else
		{
			Decision.bNotEnoughAP = true; // he stays down; the turn goes on (the player ends it)
		}
		return Decision;
	}

	bool BeginTurnGetUp(FKnockdownState& State)
	{
		if (State.Phase != EKnockdownPhase::Downed)
		{
			return false;
		}
		State.Phase = EKnockdownPhase::GettingUp;
		State.PhaseElapsed = 0.f;
		State.bGetUpReady = false;
		return true;
	}

	float RecoveryFraction(const FKnockdownState& State)
	{
		switch (State.Phase)
		{
		case EKnockdownPhase::Falling:
			return 0.f;
		case EKnockdownPhase::Downed:
			if (State.bGetUpReady || State.DownedDuration <= 0.f)
			{
				return 1.f;
			}
			return FMath::Clamp(State.PhaseElapsed / State.DownedDuration, 0.f, 1.f);
		case EKnockdownPhase::GettingUp:
			return 1.f;
		default:
			return 0.f;
		}
	}

	bool IsDown(const FKnockdownState& State)
	{
		return State.Phase != EKnockdownPhase::None;
	}

	bool PreferDownedTarget(const FKnockdownConfig& Config, bool bMeleeAttacker, float DistanceToDowned, float DistanceToCurrent)
	{
		return bMeleeAttacker && DistanceToDowned <= Config.DownedTargetPreferenceCm
			&& DistanceToDowned <= DistanceToCurrent + Config.DownedTargetExtraCm;
	}

	bool IsHoundPounce(const FKnockdownConfig& Config, float RunUpSeconds, float SecondsSinceLastPounce)
	{
		return RunUpSeconds >= Config.HoundPounceRunUpSeconds && SecondsSinceLastPounce >= Config.HoundPounceCooldownSeconds;
	}

	const TCHAR* FallClipName(EKnockdownDirection Direction)
	{
		return Direction == EKnockdownDirection::Front ? TEXT("Knocked_Front") : TEXT("Knocked_Back");
	}

	const TCHAR* GetUpClipName(EKnockdownDirection Direction)
	{
		return Direction == EKnockdownDirection::Front ? TEXT("Revive_Front") : TEXT("Revive_Back");
	}

	const TCHAR* DeathClipName(EKnockdownDirection Direction)
	{
		return Direction == EKnockdownDirection::Front ? TEXT("Death_Front") : TEXT("Death_Back");
	}
}
