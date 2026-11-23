// The pure rules of the PMC order policy (Learning Agents probe P0, 2026-11-23). UE-only, no Godot reference.

#include "AI/PmcPolicyRules.h"

#include "AI/PmcDefenceRules.h"

namespace PmcPolicyRules
{
	namespace PmcPolicyLocal
	{
		inline uint32 Bit(EPmcOrder Order)
		{
			return 1u << static_cast<uint32>(Order);
		}

		inline float Flag(bool bValue)
		{
			return bValue ? 1.f : 0.f;
		}

		inline float Unit(float Value)
		{
			return FMath::Clamp(Value, 0.f, 1.f);
		}

		/** The order's score with a running failure cooldown counted as 0 (PickCommitted's rule). */
		inline float GatedScore(const FPmcOrderScore& Entry, const FPmcCommitment& Commitment)
		{
			const double* FailedUntil = Commitment.FailedUntil.Find(Entry.Order);
			return FailedUntil && Commitment.NowSeconds < *FailedUntil ? 0.f : Entry.Score;
		}
	}

	int32 ActionIndexOf(EPmcOrder Order)
	{
		for (int32 Index = 0; Index < ActionCount; ++Index)
		{
			if (ActionOrders[Index] == Order)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	EPmcOrder OrderOfAction(int32 ActionIndex)
	{
		return ActionIndex >= 0 && ActionIndex < ActionCount ? ActionOrders[ActionIndex] : EPmcOrder::Hold;
	}

	uint32 AllowedOrderMask(const TArray<FPmcOrderScore>& Scored, const FPmcCommitment& Commitment, const FPmcWeights& Weights)
	{
		using namespace PmcPolicyLocal;
		uint32 Mask = 0;
		float CurrentScore = 0.f;
		for (const FPmcOrderScore& Entry : Scored)
		{
			const float Score = GatedScore(Entry, Commitment);
			if (Score > 0.f)
			{
				Mask |= Bit(Entry.Order);
			}
			if (Entry.Order == Commitment.Current)
			{
				CurrentScore = Score;
			}
		}
		// The commitment: a running active order that is still valid holds its minimum time - nothing else may replace it.
		if (Commitment.bRunning && !PmcCommanderRules::IsStaticOrder(Commitment.Current) && CurrentScore > 0.f
			&& Commitment.AgeSeconds < Weights.OrderMinHoldSeconds)
		{
			return Bit(Commitment.Current);
		}
		return Mask != 0 ? Mask : Bit(EPmcOrder::Hold);
	}

	bool IsOrderAllowed(uint32 OrderMask, EPmcOrder Order)
	{
		return Order != EPmcOrder::Count && (OrderMask & PmcPolicyLocal::Bit(Order)) != 0;
	}

	void MaskedActions(uint32 OrderMask, bool bCanAct, TArray<int32>& OutMaskedActions)
	{
		OutMaskedActions.Reset();
		if (bCanAct)
		{
			for (int32 Index = 0; Index < ActionCount; ++Index)
			{
				if (!IsOrderAllowed(OrderMask, ActionOrders[Index]))
				{
					OutMaskedActions.Add(Index);
				}
			}
			if (OutMaskedActions.Num() < ActionCount)
			{
				return;
			}
			OutMaskedActions.Reset();
		}
		// Nothing of the action set may be sampled: Hold stands in (GatePick turns it into the order that is really allowed).
		for (int32 Index = 0; Index < ActionCount; ++Index)
		{
			if (Index != HoldAction)
			{
				OutMaskedActions.Add(Index);
			}
		}
	}

	EPmcOrder GatePick(EPmcOrder Pick, uint32 OrderMask, const FPmcCommitment& Commitment)
	{
		if (IsOrderAllowed(OrderMask, Pick))
		{
			return Pick;
		}
		if (Commitment.bRunning && IsOrderAllowed(OrderMask, Commitment.Current))
		{
			return Commitment.Current;
		}
		return EPmcOrder::Hold;
	}

	EPmcPickReason PickReason(EPmcOrder Pick, const TArray<FPmcOrderScore>& Scored, const FPmcCommitment& Commitment)
	{
		if (!Commitment.bRunning || PmcCommanderRules::IsStaticOrder(Commitment.Current))
		{
			return EPmcPickReason::Best;
		}
		float CurrentScore = 0.f;
		for (const FPmcOrderScore& Entry : Scored)
		{
			if (Entry.Order == Commitment.Current)
			{
				CurrentScore = PmcPolicyLocal::GatedScore(Entry, Commitment);
			}
		}
		if (CurrentScore <= 0.f)
		{
			return EPmcPickReason::Invalid;
		}
		return Pick == Commitment.Current ? EPmcPickReason::Keep : EPmcPickReason::Preempt;
	}

	void BuildObservation(const FPmcFighterView& F, const FPmcSquadView& S, TArrayView<float> Out, float AreaFactor)
	{
		using namespace PmcPolicyLocal;
		if (!ensure(Out.Num() >= ObservationSize))
		{
			return;
		}
		for (float& Value : Out)
		{
			Value = 0.f;
		}
		const FPmcObjectiveView& O = F.Objective;
		const bool bObjective = O.IsActive();
		const bool bThreat = F.bHasThreat && !F.Threat.bStandIn;

		Out[ObsHealth] = Unit(F.HealthFrac);
		Out[ObsClip] = Unit(F.ClipFrac);
		Out[ObsAmmo] = Unit(F.AmmoFrac);
		Out[ObsHasReserve] = Flag(F.bHasReserve);
		Out[ObsInCover] = Flag(F.bInCover);
		Out[ObsCoverQuality] = Unit(F.CoverQuality);
		Out[ObsCoverAvailable] = Flag(F.bCoverAvailable);
		Out[ObsExposed] = Flag(F.bExposed);
		Out[ObsCrouching] = Flag(F.Stance == EOperativeStance::Crouching);
		Out[ObsProne] = Flag(F.Stance == EOperativeStance::Prone);
		Out[ObsMoving] = Flag(F.bMoving);
		Out[ObsUnderFire] = Flag(F.bUnderFire);
		Out[ObsSuppressed] = Flag(F.bSuppressed);
		Out[ObsFireLine] = Flag(bThreat && F.bFireLine);
		Out[ObsLauncherScore] = F.bHasLauncher ? Unit(F.LauncherScore) : 0.f;
		Out[ObsAtPost] = Flag(F.bAtPost);
		Out[ObsHasDefencePost] = Flag(F.bHasDefencePost);

		Out[ObsObjectiveDistance] = bObjective ? Unit(O.DistanceCm / ObjectiveDistanceScaleCm) : 1.f;
		Out[ObsOnPoint] = Flag(bObjective && O.DistanceCm <= O.RadiusCm);
		Out[ObsInsideZone] = Flag(bObjective && (O.bDefenceAreaSet
			? O.bInsideDefenceArea
			: PmcDefenceRules::IsInsideAreaFallback(O.bOnObjectiveLevel, O.DistanceCm, O.RadiusCm, AreaFactor)));

		Out[ObsThreatKnown] = Flag(bThreat);
		Out[ObsThreatSeen] = Flag(bThreat && F.Threat.bSeenNow);
		Out[ObsThreatDistance] = bThreat ? Unit(F.Threat.DistanceCm / ThreatDistanceScaleCm) : 1.f;
		Out[ObsThreatInRange] = Flag(bThreat && F.Threat.DistanceCm <= F.WeaponRangeCm);
		Out[ObsThreatAge] = bThreat ? (F.Threat.bSeenNow ? 0.f : Unit(F.Threat.SecondsSince / ThreatAgeScaleSeconds)) : 1.f;
		if (bThreat && bObjective)
		{
			const FVector2D ToPoint = FVector2D(O.Location - F.Location).GetSafeNormal();
			const FVector2D ToThreat = FVector2D(F.Threat.Location - F.Location).GetSafeNormal();
			if (!ToPoint.IsNearlyZero() && !ToThreat.IsNearlyZero())
			{
				Out[ObsThreatBearingCos] = static_cast<float>(FMath::Clamp(FVector2D::DotProduct(ToPoint, ToThreat), -1.0, 1.0));
				Out[ObsThreatBearingSin] = static_cast<float>(FMath::Clamp(FVector2D::CrossProduct(ToPoint, ToThreat), -1.0, 1.0));
			}
		}
		Out[ObsAttentionOff] = Unit(F.Threat.AttentionOff);

		Out[ObsEnemiesKnown] = Unit(static_cast<float>(S.EnemiesKnown) / EnemiesScale);
		Out[ObsEnemiesSeen] = Unit(static_cast<float>(S.EnemiesSeenNow) / EnemiesScale);
		Out[ObsMatesAlive] = Unit(static_cast<float>(S.Alive - 1) / MatesScale);
		Out[ObsMatesReloading] = Unit(static_cast<float>(S.MatesReloading) / MatesBusyScale);
		Out[ObsMatesSuppressing] = Unit(static_cast<float>(S.MatesSuppressing) / MatesBusyScale);
		Out[ObsContact] = Flag(S.bContact);
		Out[ObsShotsExchanged] = Flag(S.bShotsExchanged);
		Out[ObsQuiet] = Unit(S.QuietSeconds / QuietScaleSeconds);

		Out[ObsTimeFrac] = bObjective ? Unit(O.TimeFrac) : 0.f;
		Out[ObsHoldFrac] = bObjective ? Unit(O.HoldFrac) : 0.f;
		Out[ObsEnemyOnPoint] = Flag(bObjective && O.bEnemyOnPoint);

		if (const int32 Running = ActionIndexOf(F.RunningOrder); Running != INDEX_NONE)
		{
			Out[ObsRunningOrder + Running] = 1.f;
		}
	}

	float StepReward(const FPmcPolicyEvents& Events, const FPmcPolicyRewards& Rewards)
	{
		return Rewards.AttackerKilled * static_cast<float>(FMath::Max(Events.AttackersKilled, 0))
			+ (Events.bDied ? Rewards.OwnDeath : 0.f)
			+ (Events.bRoundWon ? Rewards.RoundWon : 0.f)
			+ (Events.bPointCaptured ? Rewards.PointCaptured : 0.f)
			+ Rewards.EnemyOnPointPerSecond * FMath::Max(Events.EnemyOnPointSeconds, 0.f);
	}

	void OutcomeEvents(const FPmcRoundOutcome& Outcome, bool bDefenderIsA, bool& bOutRoundWon, bool& bOutPointCaptured)
	{
		bOutRoundWon = Outcome.bOver && Outcome.Winner == (bDefenderIsA ? TEXT("A") : TEXT("B"));
		bOutPointCaptured = Outcome.bOver && Outcome.ObjectiveResult == TEXT("captured");
	}
}
