#include "AI/PmcCommanderRules.h"

#include "AI/AmbushWatchRules.h"
#include "Dom/JsonObject.h"
#include "Math/RandomStream.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

FPmcWeights::FPmcWeights()
	: Bands(PmcCommanderRules::DefaultBands())
{
}

bool FPmcObjectiveClock::Step(int32 AttackersOnPoint, int32 DefendersOnPoint, float DeltaSeconds)
{
	const float Delta = FMath::Max(DeltaSeconds, 0.f);
	bAttackersHolding = AttackersOnPoint > 0 && DefendersOnPoint == 0;
	bDefendersHolding = DefendersOnPoint > 0 && AttackersOnPoint == 0;
	if (bAttackersHolding)
	{
		ControlSeconds[0] += Delta;
	}
	else if (DefendersOnPoint > 0 && AttackersOnPoint == 0)
	{
		ControlSeconds[1] += Delta;
	}
	// Cumulative hold with a reset (user decision 2026-11-07): the attackers' sole-control time adds up across stints
	// (leaving, or a contested moment, only pauses it); the defenders taking the point back alone resets it.
	if (bAttackersHolding)
	{
		Held += Delta;
	}
	else if (DefendersOnPoint > 0 && AttackersOnPoint == 0)
	{
		Held = 0.f;
	}
	if (!bCaptured && bAttackersHolding && Held >= HoldSeconds)
	{
		bCaptured = true;
		return true;
	}
	return false;
}

namespace PmcCommanderRules
{
	namespace PmcCommanderLocal
	{
		inline float Clamp01(float Value)
		{
			return FMath::Clamp(Value, 0.f, 1.f);
		}

		inline float Step(bool bCondition)
		{
			return bCondition ? 1.f : 0.f;
		}

		/** A close-quarters advance must move him at least this far (the arrival radius of a walk). */
		constexpr float MinAdvanceStepCm = 250.f;

		inline void ReadNumber(const FJsonObject& Json, const TCHAR* Key, float& Out)
		{
			double Value = 0.0;
			if (Json.TryGetNumberField(Key, Value))
			{
				Out = static_cast<float>(Value);
			}
		}
	}

	const TCHAR* OrderName(EPmcOrder Order)
	{
		switch (Order)
		{
		case EPmcOrder::Hold: return TEXT("Hold");
		case EPmcOrder::TakeCover: return TEXT("TakeCover");
		case EPmcOrder::Advance: return TEXT("Advance");
		case EPmcOrder::Flank: return TEXT("Flank");
		case EPmcOrder::Suppress: return TEXT("Suppress");
		case EPmcOrder::FallBack: return TEXT("FallBack");
		case EPmcOrder::Heal: return TEXT("Heal");
		case EPmcOrder::Relocate: return TEXT("Relocate");
		case EPmcOrder::Ambush: return TEXT("Ambush");
		case EPmcOrder::Reload: return TEXT("Reload");
		case EPmcOrder::Sidearm: return TEXT("Sidearm");
		case EPmcOrder::Crouch: return TEXT("Crouch");
		case EPmcOrder::Objective: return TEXT("Objective");
		case EPmcOrder::RpgStrike: return TEXT("RpgStrike");
		default: return TEXT("?");
		}
	}

	float NumbersFactor(int32 Alive, int32 EnemiesKnown)
	{
		return PmcCommanderLocal::Clamp01(0.5f + 0.25f * static_cast<float>(Alive - EnemiesKnown));
	}

	float ScoreOrder(EPmcOrder Order, const FPmcFighterView& F, const FPmcSquadView& S, const FPmcWeights& W)
	{
		using namespace PmcCommanderLocal;
		if (F.bKnockedDown)
		{
			return 0.f;
		}
		const float Contact = Step(S.bContact);
		const float SeenNow = Step(F.bHasThreat && F.Threat.bSeenNow);
		const float InRange = Step(F.bHasThreat && F.Threat.DistanceCm <= F.WeaponRangeCm);
		// Engagement bands (2026-11-04): a static order outside his weapon's band is worth less; too close (not a shotgun) he backs off.
		const bool bBand = F.Band.IsValid();
		const float BandHere = bBand && F.bHasThreat && !F.Threat.bStandIn ? BandFactor(F.Threat.DistanceCm, F.Band, W) : 1.f;
		const float StaticBand = FMath::Lerp(1.f, BandHere, Clamp01(W.BandPosition));
		// Hysteresis: a running retreat goes on to the band's middle (else it would stop the moment he crosses the trigger distance).
		// A running flank passes the threat inside the band on purpose: no standoff then.
		const bool bTooClose = bBand && F.bHasThreat && !F.Threat.bStandIn && F.RunningOrder != EPmcOrder::Flank && (IsTooClose(F.Threat.DistanceCm, F.Band, W)
			|| (F.RunningOrder == EPmcOrder::FallBack && !F.Band.bCloseQuarters && F.Threat.DistanceCm < 0.5f * (F.Band.MinCm + F.Band.MaxCm)));
		// The round objective (task #53): an attacker on the point and a defender within its overwatch value holding where they are.
		const FPmcObjectiveView& O = F.Objective;
		const bool bObjective = O.IsActive();
		const float OverwatchCm = bObjective ? OverwatchRangeCm(O, F.Band, F.WeaponRangeCm) : 0.f;
		// The push (fix/pmc-arena-stalemate, the designer 2026-11-08): quiet for PushAfterSeconds in an objective round, a man off the point
		// stops sitting in cover - his static / cover orders lose x 1 / Push and the walk onto the point (Objective) gains.
		const bool bOffPoint = bObjective && O.DistanceCm > 0.7f * O.RadiusCm;
		const bool bPush = IsPushing(F, S, W);
		const float PushGain = FMath::Max(1.f, W.Push);
		const float PushDamp = bPush && bOffPoint ? 1.f / PushGain : 1.f;
		float HoldHere = 1.f;
		if (bObjective && O.Role == EPmcRole::Attacker && O.DistanceCm <= O.RadiusCm)
		{
			HoldHere = 1.f + FMath::Max(W.ObjectiveApproach, 0.f);
		}
		else if (bObjective && O.Role == EPmcRole::Defender && O.DistanceCm <= OverwatchCm)
		{
			HoldHere = 1.f + FMath::Max(W.Overwatch, 0.f);
		}
		switch (Order)
		{
		case EPmcOrder::Hold:
			// The baseline: the fighter keeps shooting from where he is; worth more in cover and in contact.
			return W.Hold * (F.bInCover ? 0.6f + 0.4f * F.CoverQuality : 0.3f) * (S.bContact ? 1.f : 0.5f) * StaticBand * HoldHere * PushDamp;
		case EPmcOrder::TakeCover:
			// Exposed and badly covered against a known threat, with somewhere to go (a slot at his band's distance preferred).
			// Not from a stand-in (the assault / objective point with no enemy known): hiding from a point nobody holds flapped with the
			// Objective walk 160 times a round (fix/pmc-arena-stalemate, the Depot chokepoint round).
			return W.TakeCover * Step(F.bCoverAvailable && F.bHasThreat && !F.Threat.bStandIn) * (1.f - F.CoverQuality) * (0.4f + 0.6f * Contact) * (0.5f + 0.5f * Step(F.bExposed))
				* Step(!F.bInCover || F.CoverQuality < 0.3f) * FMath::Lerp(1.f, Clamp01(F.CoverBandFactor), Clamp01(W.BandPosition)) * PushDamp;
		case EPmcOrder::Advance:
		{
			// Bounding overwatch: one man moves while the others hold; worth it with numbers, health, ammo, and a threat beyond his band (the
			// stop: AdvanceStopCm). A close-quarters weapon (the shotgun) closes in harder (CloseIn) and less afraid of the numbers.
			const float Numbers = F.Band.bCloseQuarters ? FMath::Max(0.5f, NumbersFactor(S.Alive, S.EnemiesKnown)) : NumbersFactor(S.Alive, S.EnemiesKnown);
			return W.Advance * (F.Band.bCloseQuarters ? FMath::Max(W.CloseIn, 0.f) : 1.f) * Step(F.bHasThreat) * (S.bContact ? 1.f : 0.4f) * Numbers * F.HealthFrac
				* Step(F.AmmoFrac > 0.2f)
				// A band's advance needs a real step (a shotgunner 4.4 m off a 4.2 m stop re-issued a 20 cm walk every decision, Depot arena).
				* Step(F.Threat.DistanceCm > (bBand && !F.Band.bCloseQuarters ? F.Band.MaxCm : AdvanceStopCm(F.WeaponRangeCm, F.Band, W) + (bBand ? MinAdvanceStepCm : 0.f)))
				* (S.MatesAdvancing == 0 || S.Alive == 1 ? 1.f : 0.15f)
				// Target attention (2026-11-07): a rifleman's bound waits for the moment they look away; a shotgunner rushes in regardless
				// (his close-in is the job - PmcStandoffSmoke's shotgunners stalled at 8.9 m under the squad's eyes with it).
				* (F.Band.bCloseQuarters ? 1.f : AttentionFactor(F, W));
		}
		case EPmcOrder::Flank:
			// A second axis onto a seen threat while a mate pins it; one flanker at a time, a healthy man, within a sensible band.
			// A running flank goes on while the threat is only remembered (2026-11-04: the band's longer flank walk loses sight of it on the way).
			return W.Flank * (F.RunningOrder == EPmcOrder::Flank ? Step(F.bHasThreat) : SeenNow) * (S.MatesSuppressing > 0 || S.Alive >= 2 ? 1.f : 0.3f) * (S.MatesFlanking == 0 ? 1.f : 0.2f) * F.HealthFrac
				* Step(F.Threat.DistanceCm >= 600.f && F.Threat.DistanceCm <= 3500.f) * (F.bExposed ? 0.7f : 1.f) * Step(F.AmmoFrac > 0.2f) * AttentionFactor(F, W);
		case EPmcOrder::Suppress:
			// Pin a seen threat so a mate can move; needs ammo and a reason (a mate moving), one suppressor at a time - and a line of fire that
			// reaches it (2026-11-08: no rounds into the wall in front of him).
			return W.Suppress * SeenNow * InRange * Step(F.bFireLine) * Step(F.AmmoFrac >= W.SuppressAmmoMin) * (S.MatesFlanking + S.MatesAdvancing > 0 ? 1.f : 0.5f)
				* (S.MatesSuppressing == 0 ? 1.f : 0.4f) * StaticBand;
		case EPmcOrder::FallBack:
		{
			// Losing: badly wounded or outnumbered in contact.
			const float Losing = W.FallBack * Contact * Step(F.bHasThreat) * Clamp01(Step(F.HealthFrac < W.FallBackBelow) * (1.f - F.HealthFrac) + Step(S.Alive - S.EnemiesKnown <= -1) * 0.6f);
			// Too close for his weapon (2026-11-04: "never fights at point-blank range"): back off to the band, the closer the more urgent.
			const float Severity = bTooClose ? Clamp01(1.f - F.Threat.DistanceCm / FMath::Max(F.Band.MinCm, 1.f)) : 0.f;
			const float Standoff = Step(bTooClose) * FMath::Max(W.Standoff, 0.f) * (0.6f + 0.4f * Severity) * (S.bContact ? 1.f : 0.6f);
			return FMath::Max(Losing, Standoff);
		}
		case EPmcOrder::Heal:
			// A wounded mate within reach, a medkit in the kit, and nobody looking (seen threats make it a last resort).
			return W.Heal * Step(F.bHasMedkit && F.WoundedMateDistanceCm >= 0.f && F.WoundedMateHealthFrac < W.HealBelow) * Clamp01(1.f - F.WoundedMateHealthFrac)
				* (S.EnemiesSeenNow == 0 ? 1.f : 0.3f) * (S.MatesHealing == 0 ? 1.f : 0.2f);
		case EPmcOrder::Relocate:
			// Spotted at this spot for too long: move to another cover.
			return W.Relocate * Contact * Step(!F.Threat.bStandIn) * Step(F.bExposed && F.SecondsExposedHere >= W.RelocateAfterSeconds && F.bCoverAvailable) * PushDamp;
		case EPmcOrder::Ambush:
			// No contact yet in this fight: hold in cover, crouched, and let them come.
			return W.Ambush * Step(!S.bEverContact) * (F.bInCover ? 1.f : 0.6f) * HoldHere * PushDamp;
		case EPmcOrder::Reload:
			// A low clip with rounds in reserve; best behind cover or out of contact.
			return W.Reload * Step(F.ClipFrac < 0.3f && F.bHasReserve) * (F.bInCover || S.EnemiesSeenNow == 0 ? 1.f : 0.5f);
		case EPmcOrder::Sidearm:
			// An empty rifle with the enemy at point-blank range: the pistol.
			return W.Sidearm * Step(F.bHasSidearm && F.ClipFrac <= 0.f && F.bHasThreat && F.Threat.DistanceCm < 400.f && F.Threat.bSeenNow);
		case EPmcOrder::Crouch:
			// In contact, standing still in the open: get low.
			return W.Crouch * Contact * Step(!F.bMoving && !F.bInCover && F.Stance == EOperativeStance::Standing) * StaticBand * PushDamp;
		case EPmcOrder::Objective:
		{
			if (!bObjective)
			{
				return 0.f;
			}
			if (O.Role == EPmcRole::Attacker)
			{
				// Onto the point: more urgent as the clock runs down (the defenders win at the time limit), less under fire / against men on it.
				const float Urgency = Clamp01(W.ObjectiveUrgency);
				if (bPush)
				{
					// Quiet too long: onto the point, whatever the clock or his scratches (nobody shoots at him).
					return FMath::Max(W.Objective, 0.f) * PushGain * Step(bOffPoint) * FMath::Max(0.5f, F.HealthFrac) * Step(F.AmmoFrac > 0.1f)
						* (O.bEnemyOnPoint ? 0.8f : 1.f);
				}
				return FMath::Max(W.Objective, 0.f) * Step(O.DistanceCm > 0.7f * O.RadiusCm) * (1.f - Urgency + Urgency * Clamp01(O.TimeFrac)) * F.HealthFrac
					* Step(F.AmmoFrac > 0.1f) * (SeenNow > 0.f ? 0.6f : 1.f) * (O.bEnemyOnPoint ? 0.8f : 1.f) * AttentionFactor(F, W);
			}
			// A defender: back onto the point while the attackers stand on it (the more of their clock ran, the more urgent), or back into
			// its overwatch when he strayed.
			const float Retake = Step(O.bEnemyOnPoint && O.DistanceCm > 0.7f * O.RadiusCm) * (0.2f + 0.8f * Clamp01(O.HoldFrac));
			const float Return = Step(!O.bEnemyOnPoint && O.DistanceCm > OverwatchCm) * 0.7f;
			// fix/pmc-arena-stalemate: the defenders win on time only standing on the point - onto it when the round goes quiet (the push)
			// and as the time limit nears (from 60 % of the round, full at 85 %).
			const float Occupy = Step(bOffPoint) * FMath::Max(Step(bPush) * 0.8f, Clamp01((O.TimeFrac - 0.6f) / 0.25f) * 0.9f);
			return FMath::Max(W.Objective, 0.f) * FMath::Max(FMath::Max(Retake, Return), Occupy) * FMath::Max(0.5f, F.HealthFrac);
		}
		case EPmcOrder::RpgStrike:
			// The squad's RPG gunner (2026-11-09): a rocket target the team sees (the shared launcher rule's score, capped at 1) - worth more
			// while a mate moves (the rocket makes them duck), one rocket at a time per squad.
			// A strike in flight stays valid while the launcher works on it (draw / aim; the launcher's own timeout ends it): its target may
			// drop out of the rule's score for a moment, and a Suppress taking over cancelled the rocket (fix/pmc-arena-stalemate, RpgPmcSmoke).
			return FMath::Max(W.RpgStrike, 0.f) * Step(F.bHasLauncher)
				* (F.RunningOrder == EPmcOrder::RpgStrike ? FMath::Max(Clamp01(F.LauncherScore), 0.5f) : Clamp01(F.LauncherScore))
				* (S.MatesFlanking + S.MatesAdvancing > 0 ? 1.f : 0.85f)
				* (S.MatesRpgStriking == 0 ? 1.f : 0.2f);
		default:
			return 0.f;
		}
	}

	TArray<FPmcOrderScore> ScoreAll(const FPmcFighterView& Fighter, const FPmcSquadView& Squad, const FPmcWeights& Weights)
	{
		TArray<FPmcOrderScore> Out;
		for (int32 Index = 0; Index < static_cast<int32>(EPmcOrder::Count); ++Index)
		{
			const EPmcOrder Order = static_cast<EPmcOrder>(Index);
			Out.Add({ Order, ScoreOrder(Order, Fighter, Squad, Weights) });
		}
		Out.StableSort([](const FPmcOrderScore& A, const FPmcOrderScore& B) { return A.Score > B.Score; });
		return Out;
	}

	EPmcOrder PickWithMargin(const TArray<FPmcOrderScore>& Scored, EPmcOrder Current, bool bCurrentRunning, float Margin)
	{
		if (Scored.IsEmpty() || Scored[0].Score <= 0.f)
		{
			return EPmcOrder::Hold;
		}
		if (bCurrentRunning)
		{
			for (const FPmcOrderScore& Entry : Scored)
			{
				if (Entry.Order == Current)
				{
					return Entry.Score > 0.f && Entry.Score >= Scored[0].Score - Margin ? Current : Scored[0].Order;
				}
			}
		}
		return Scored[0].Order;
	}

	bool IsStaticOrder(EPmcOrder Order)
	{
		return Order == EPmcOrder::Hold || Order == EPmcOrder::Ambush || Order == EPmcOrder::Crouch;
	}

	bool IsPushing(const FPmcFighterView& Fighter, const FPmcSquadView& Squad, const FPmcWeights& Weights)
	{
		return Fighter.Objective.IsActive() && Weights.PushAfterSeconds > 0.f && Squad.QuietSeconds >= Weights.PushAfterSeconds;
	}

	EPmcOrder PickCommitted(const TArray<FPmcOrderScore>& Scored, const FPmcCommitment& C, const FPmcWeights& W, EPmcPickReason* OutReason)
	{
		EPmcPickReason Reason = EPmcPickReason::Best;
		const bool bCooling = C.Failed != EPmcOrder::Count && C.FailedAgeSeconds < W.FailedOrderCooldownSeconds;
		EPmcOrder Best = EPmcOrder::Hold;
		float BestScore = 0.f;
		float CurrentScore = 0.f;
		for (const FPmcOrderScore& Entry : Scored)
		{
			const float Score = bCooling && Entry.Order == C.Failed ? 0.f : Entry.Score;
			if (Score > BestScore)
			{
				BestScore = Score;
				Best = Entry.Order;
			}
			if (Entry.Order == C.Current)
			{
				CurrentScore = Score;
			}
		}
		EPmcOrder Pick = Best;
		if (C.bRunning && !IsStaticOrder(C.Current))
		{
			if (CurrentScore <= 0.f)
			{
				Reason = EPmcPickReason::Invalid;
			}
			else if (C.AgeSeconds < W.OrderMinHoldSeconds)
			{
				Pick = C.Current;
				Reason = EPmcPickReason::Keep;
			}
			else
			{
				const float Margin = IsMoveOrder(C.Current) || C.AgeSeconds < W.CommitSeconds ? W.MoveSwitchMargin : W.SwitchMargin;
				const bool bKeep = CurrentScore >= BestScore - Margin;
				Pick = bKeep ? C.Current : Best;
				Reason = bKeep ? EPmcPickReason::Keep : EPmcPickReason::Preempt;
			}
		}
		if (OutReason)
		{
			*OutReason = Reason;
		}
		return Pick;
	}

	EPmcOrder Pick(const TArray<FPmcOrderScore>& Scored, EPmcOrder Current, bool bCurrentRunning, const FPmcWeights& Weights)
	{
		return PickWithMargin(Scored, Current, bCurrentRunning, Weights.SwitchMargin);
	}

	bool IsMoveOrder(EPmcOrder Order)
	{
		return Order == EPmcOrder::Advance || Order == EPmcOrder::Flank || Order == EPmcOrder::FallBack || Order == EPmcOrder::Relocate || Order == EPmcOrder::TakeCover
			|| Order == EPmcOrder::Objective;
	}

	TMap<FString, FPmcEngagementBand> DefaultBands()
	{
		TMap<FString, FPmcEngagementBand> Out;
		Out.Add(TEXT("m16"), { 2500.f, 6000.f, false });
		Out.Add(TEXT("sniper_rifle"), { 4500.f, 11000.f, false });
		Out.Add(TEXT("pistol"), { 1000.f, 2500.f, false });
		Out.Add(TEXT("shotgun"), { 400.f, 1400.f, true });
		Out.Add(TEXT("lmg"), { 3000.f, 7000.f, false });
		return Out;
	}

	FPmcEngagementBand ResolveBand(const FString& WeaponId, float WeaponRangeCm, const FPmcWeights& Weights)
	{
		FPmcEngagementBand Band;
		if (!Weights.bUseBands || WeaponRangeCm <= 0.f)
		{
			return Band;
		}
		if (const FPmcEngagementBand* Row = Weights.Bands.Find(WeaponId))
		{
			Band = *Row;
		}
		else
		{
			Band.MinCm = WeaponRangeCm * FMath::Max(Weights.DefaultBandMinFrac, 0.f);
			Band.MaxCm = WeaponRangeCm * FMath::Max(Weights.DefaultBandMaxFrac, 0.f);
		}
		// He must still hit from there: the band ends at the weapon's reach, and keeps a fifth of its width below that.
		const float Reach = WeaponRangeCm * FMath::Clamp(Weights.BandReachFrac, 0.1f, 1.f);
		Band.MaxCm = FMath::Min(FMath::Max(Band.MaxCm, 0.f), Reach);
		Band.MinCm = FMath::Clamp(Band.MinCm, 0.f, 0.8f * Band.MaxCm);
		return Band;
	}

	float BandFactor(float DistanceCm, const FPmcEngagementBand& Band, const FPmcWeights& Weights)
	{
		if (!Band.IsValid())
		{
			return 1.f;
		}
		if (DistanceCm < Band.MinCm)
		{
			const float T = PmcCommanderLocal::Clamp01(DistanceCm / FMath::Max(Band.MinCm, 1.f));
			return FMath::Lerp(PmcCommanderLocal::Clamp01(Weights.BandCloseFloor), 1.f, T * T);
		}
		if (DistanceCm > Band.MaxCm)
		{
			return FMath::Lerp(1.f, 0.5f, PmcCommanderLocal::Clamp01((DistanceCm - Band.MaxCm) / FMath::Max(Band.MaxCm, 1.f)));
		}
		return 1.f;
	}

	bool IsTooClose(float DistanceCm, const FPmcEngagementBand& Band, const FPmcWeights& Weights)
	{
		return Band.IsValid() && !Band.bCloseQuarters && DistanceCm < Band.MinCm * FMath::Max(Weights.StandoffTriggerFrac, 0.f);
	}

	float AdvanceStopCm(float WeaponRangeCm, const FPmcEngagementBand& Band, const FPmcWeights& Weights)
	{
		const float Stop = WeaponRangeCm * Weights.AdvanceStopRangeFrac;
		return Band.IsValid() ? FMath::Clamp(Stop, Band.MinCm, Band.MaxCm) : Stop;
	}

	float FlankDistanceFor(const FPmcEngagementBand& Band, const FPmcWeights& Weights)
	{
		return Band.IsValid() ? FMath::Clamp(Weights.FlankDistanceCm, Band.MinCm, Band.MaxCm) : Weights.FlankDistanceCm;
	}

	FVector StandoffPoint(const FVector& Fighter, const FVector& Threat, const FPmcEngagementBand& Band)
	{
		const float Distance = FVector::Dist2D(Fighter, Threat);
		const float Want = 0.5f * (Band.MinCm + Band.MaxCm);
		if (!Band.IsValid() || Distance >= Want)
		{
			return Fighter;
		}
		FVector Away = (Fighter - Threat).GetSafeNormal2D();
		if (Away.IsNearlyZero())
		{
			Away = FVector::BackwardVector;
		}
		return Fighter + Away * (Want - Distance);
	}

	FVector FlankPoint(const FVector& Threat, const FVector& SquadCentre, const FVector& Fighter, const FPmcWeights& Weights, float DistanceCm)
	{
		FVector Axis = (SquadCentre - Threat).GetSafeNormal2D();
		if (Axis.IsNearlyZero())
		{
			Axis = FVector::ForwardVector;
		}
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Axis).GetSafeNormal2D();
		// The side away from the fighter: he comes at the threat from the other flank than the one he stands on.
		const float Sign = FVector::DotProduct(Fighter - Threat, Side) >= 0.f ? -1.f : 1.f;
		const FVector Direction = (Axis * FMath::Cos(FMath::DegreesToRadians(Weights.FlankAngleDeg)) + Side * Sign * FMath::Sin(FMath::DegreesToRadians(Weights.FlankAngleDeg))).GetSafeNormal2D();
		return FVector(Threat.X, Threat.Y, Threat.Z) + Direction * (DistanceCm >= 0.f ? DistanceCm : Weights.FlankDistanceCm);
	}

	FVector AdvancePoint(const FVector& Fighter, const FVector& Threat, float WeaponRangeCm, const FPmcWeights& Weights, const FPmcEngagementBand& Band)
	{
		const float Distance = FVector::Dist2D(Fighter, Threat);
		const float Stop = AdvanceStopCm(WeaponRangeCm, Band, Weights);
		if (Distance <= Stop)
		{
			return Fighter;
		}
		const FVector Direction = (Threat - Fighter).GetSafeNormal2D();
		return Fighter + Direction * (Distance - Stop);
	}

	FVector FallBackPoint(const FVector& Fighter, const FVector& Threat, float DistanceCm)
	{
		FVector Away = (Fighter - Threat).GetSafeNormal2D();
		if (Away.IsNearlyZero())
		{
			Away = FVector::BackwardVector;
		}
		return Fighter + Away * DistanceCm;
	}

	float CoverQualityAgainst(const FVector& SlotLocation, const FVector& WallNormal, const FVector& Threat)
	{
		const FVector ToThreat = (Threat - SlotLocation).GetSafeNormal2D();
		return PmcCommanderLocal::Clamp01(FVector::DotProduct(-WallNormal.GetSafeNormal2D(), ToThreat));
	}

	float ApproachAngleDeg(const FVector& Threat, const FVector& A, const FVector& B)
	{
		const FVector DirA = (A - Threat).GetSafeNormal2D();
		const FVector DirB = (B - Threat).GetSafeNormal2D();
		if (DirA.IsNearlyZero() || DirB.IsNearlyZero())
		{
			return 0.f;
		}
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(DirA, DirB), -1.f, 1.f)));
	}

	float ScoreMortarFire(int32 ClusterCount, bool bManned, bool bInRangeBand, const FPmcWeights& Weights)
	{
		if (!bManned || !bInRangeBand || ClusterCount <= 0)
		{
			return 0.f;
		}
		return Weights.MortarFire * FMath::Clamp(ClusterCount / FMath::Max(1.f, Weights.MortarClusterFull), 0.f, 1.f);
	}

	FPmcWeights FromJson(const FJsonObject& Json, const FPmcWeights& Base)
	{
		using namespace PmcCommanderLocal;
		FPmcWeights Out = Base;
		FString Name;
		if (Json.TryGetStringField(TEXT("name"), Name))
		{
			Out.Name = Name;
		}
		ReadNumber(Json, TEXT("hold"), Out.Hold);
		ReadNumber(Json, TEXT("take_cover"), Out.TakeCover);
		ReadNumber(Json, TEXT("advance"), Out.Advance);
		ReadNumber(Json, TEXT("flank"), Out.Flank);
		ReadNumber(Json, TEXT("suppress"), Out.Suppress);
		ReadNumber(Json, TEXT("fall_back"), Out.FallBack);
		ReadNumber(Json, TEXT("heal"), Out.Heal);
		ReadNumber(Json, TEXT("relocate"), Out.Relocate);
		ReadNumber(Json, TEXT("ambush"), Out.Ambush);
		ReadNumber(Json, TEXT("reload"), Out.Reload);
		ReadNumber(Json, TEXT("sidearm"), Out.Sidearm);
		ReadNumber(Json, TEXT("crouch"), Out.Crouch);
		ReadNumber(Json, TEXT("rpg_strike"), Out.RpgStrike);
		ReadNumber(Json, TEXT("heal_below"), Out.HealBelow);
		ReadNumber(Json, TEXT("fall_back_below"), Out.FallBackBelow);
		ReadNumber(Json, TEXT("suppress_ammo_min"), Out.SuppressAmmoMin);
		ReadNumber(Json, TEXT("advance_stop_range_frac"), Out.AdvanceStopRangeFrac);
		ReadNumber(Json, TEXT("flank_distance_cm"), Out.FlankDistanceCm);
		ReadNumber(Json, TEXT("flank_angle_deg"), Out.FlankAngleDeg);
		ReadNumber(Json, TEXT("relocate_after_seconds"), Out.RelocateAfterSeconds);
		ReadNumber(Json, TEXT("contact_memory_seconds"), Out.ContactMemorySeconds);
		ReadNumber(Json, TEXT("decision_interval"), Out.DecisionInterval);
		ReadNumber(Json, TEXT("mortar_fire"), Out.MortarFire);
		ReadNumber(Json, TEXT("mortar_threshold"), Out.MortarThreshold);
		ReadNumber(Json, TEXT("mortar_cluster_radius_cm"), Out.MortarClusterRadiusCm);
		ReadNumber(Json, TEXT("mortar_cluster_full"), Out.MortarClusterFull);
		ReadNumber(Json, TEXT("switch_margin"), Out.SwitchMargin);
		ReadNumber(Json, TEXT("move_switch_margin"), Out.MoveSwitchMargin);
		ReadNumber(Json, TEXT("commit_seconds"), Out.CommitSeconds);
		// Order commitment and the push (fix/pmc-arena-stalemate 2026-11-10).
		ReadNumber(Json, TEXT("order_min_hold_s"), Out.OrderMinHoldSeconds);
		ReadNumber(Json, TEXT("failed_order_cooldown_s"), Out.FailedOrderCooldownSeconds);
		ReadNumber(Json, TEXT("repath_after_s"), Out.RepathAfterSeconds);
		ReadNumber(Json, TEXT("push_after_s"), Out.PushAfterSeconds);
		ReadNumber(Json, TEXT("push"), Out.Push);
		ReadNumber(Json, TEXT("rpg_gunner_share"), Out.RpgGunnerShare);
		Out.RepathAfterSeconds = FMath::Max(1.f, Out.RepathAfterSeconds);
		ReadNumber(Json, TEXT("high_ground"), Out.HighGround);
		float MaxLevels = static_cast<float>(Out.HighGroundMaxLevels);
		ReadNumber(Json, TEXT("high_ground_max_levels"), MaxLevels);
		Out.HighGroundMaxLevels = FMath::Max(0, FMath::RoundToInt(MaxLevels));
		Out.DecisionInterval = FMath::Max(0.1f, Out.DecisionInterval);
		// Engagement bands (2026-11-04).
		bool bUseBands = Out.bUseBands;
		if (Json.TryGetBoolField(TEXT("use_engagement_bands"), bUseBands))
		{
			Out.bUseBands = bUseBands;
		}
		ReadNumber(Json, TEXT("default_band_min_frac"), Out.DefaultBandMinFrac);
		ReadNumber(Json, TEXT("default_band_max_frac"), Out.DefaultBandMaxFrac);
		ReadNumber(Json, TEXT("band_reach_frac"), Out.BandReachFrac);
		ReadNumber(Json, TEXT("standoff"), Out.Standoff);
		ReadNumber(Json, TEXT("standoff_trigger_frac"), Out.StandoffTriggerFrac);
		ReadNumber(Json, TEXT("band_position"), Out.BandPosition);
		ReadNumber(Json, TEXT("band_close_floor"), Out.BandCloseFloor);
		ReadNumber(Json, TEXT("corner_bonus"), Out.CornerBonus);
		ReadNumber(Json, TEXT("close_in"), Out.CloseIn);
		ReadNumber(Json, TEXT("prone_min_band_cm"), Out.ProneMinBandCm);
		// The round objective (task #53).
		ReadNumber(Json, TEXT("objective"), Out.Objective);
		ReadNumber(Json, TEXT("objective_urgency"), Out.ObjectiveUrgency);
		ReadNumber(Json, TEXT("objective_approach"), Out.ObjectiveApproach);
		ReadNumber(Json, TEXT("overwatch"), Out.Overwatch);
		// Target attention (2026-11-07).
		ReadNumber(Json, TEXT("attention"), Out.Attention);
		ReadNumber(Json, TEXT("ambush_patience"), Out.AmbushPatience);
		ReadNumber(Json, TEXT("kill_zone_frac"), Out.KillZoneFrac);
		ReadNumber(Json, TEXT("ambush_max_hold_s"), Out.AmbushMaxHoldSeconds);
		const TSharedPtr<FJsonObject>* Bands = nullptr;
		if (Json.TryGetObjectField(TEXT("engagement_bands"), Bands) && Bands && Bands->IsValid())
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Bands)->Values)
			{
				const TSharedPtr<FJsonObject>* Row = nullptr;
				if (Entry.Key.StartsWith(TEXT("_")) || !Entry.Value.IsValid() || !Entry.Value->TryGetObject(Row) || !Row || !Row->IsValid())
				{
					continue; // "_comment" and the like
				}
				FPmcEngagementBand Band = Out.Bands.FindRef(Entry.Key);
				double Meters = 0.0;
				if ((*Row)->TryGetNumberField(TEXT("min_m"), Meters))
				{
					Band.MinCm = FMath::Max(static_cast<float>(Meters) * 100.f, 0.f);
				}
				if ((*Row)->TryGetNumberField(TEXT("max_m"), Meters))
				{
					Band.MaxCm = FMath::Max(static_cast<float>(Meters) * 100.f, 0.f);
				}
				bool bCloseQuarters = Band.bCloseQuarters;
				if ((*Row)->TryGetBoolField(TEXT("close_quarters"), bCloseQuarters))
				{
					Band.bCloseQuarters = bCloseQuarters;
				}
				Band.MaxCm = FMath::Max(Band.MaxCm, Band.MinCm);
				Out.Bands.Add(Entry.Key, Band);
			}
		}
		return Out;
	}

	float OverwatchRangeCm(const FPmcObjectiveView& Objective, const FPmcEngagementBand& Band, float WeaponRangeCm)
	{
		const float Reach = Band.IsValid() ? Band.MaxCm : 0.8f * FMath::Max(WeaponRangeCm, 0.f);
		return FMath::Max(2.f * FMath::Max(Objective.RadiusCm, 0.f), Reach);
	}

	float ObjectivePositionFactor(const FVector& Position, const FVector& FighterLocation, const FPmcObjectiveView& Objective,
		const FPmcEngagementBand& Band, float WeaponRangeCm, const FPmcWeights& Weights)
	{
		if (!Objective.IsActive())
		{
			return 1.f;
		}
		const float SlotDistance = static_cast<float>(FVector::Dist2D(Position, Objective.Location));
		if (Objective.Role == EPmcRole::Attacker)
		{
			const float FighterDistance = static_cast<float>(FVector::Dist2D(FighterLocation, Objective.Location));
			const float Gain = SlotDistance <= Objective.RadiusCm ? 1.f
				: FMath::Clamp((FighterDistance - SlotDistance) / FMath::Max(Objective.RadiusCm, 1.f), -1.f, 1.f);
			return FMath::Max(0.25f, 1.f + FMath::Max(Weights.ObjectiveApproach, 0.f) * Gain);
		}
		const float Overwatch = OverwatchRangeCm(Objective, Band, WeaponRangeCm);
		const float Reach = SlotDistance <= Overwatch ? 1.f : PmcCommanderLocal::Clamp01(1.f - (SlotDistance - Overwatch) / FMath::Max(Overwatch, 1.f));
		return 1.f + FMath::Max(Weights.Overwatch, 0.f) * Reach;
	}

	FPmcRoundOutcome ResolveRound(int32 AliveA, int32 AliveB, bool bTimeUp, bool bObjective, const FPmcObjectiveClock& Clock, bool bAttackerIsA, bool bContact)
	{
		FPmcRoundOutcome Out;
		const TCHAR* Attacker = bAttackerIsA ? TEXT("A") : TEXT("B");
		const TCHAR* Defender = bAttackerIsA ? TEXT("B") : TEXT("A");
		if (AliveA == 0 || AliveB == 0)
		{
			Out.bOver = true;
			Out.Reason = TEXT("elimination");
			Out.Winner = AliveA > 0 ? TEXT("A") : (AliveB > 0 ? TEXT("B") : TEXT("draw"));
			Out.ObjectiveResult = bObjective ? TEXT("eliminated") : TEXT("none");
		}
		else if (bObjective && Clock.bCaptured)
		{
			Out.bOver = true;
			Out.Reason = TEXT("objective");
			Out.Winner = Attacker;
			Out.ObjectiveResult = TEXT("captured");
		}
		else if (bTimeUp)
		{
			Out.bOver = true;
			Out.Reason = TEXT("time_limit");
			if (!bContact)
			{
				// No shot between the sides all round: nobody earned anything (the designer 2026-11-08).
				Out.Winner = TEXT("draw");
				Out.ObjectiveResult = bObjective ? TEXT("no_contact") : TEXT("none");
			}
			else if (bObjective)
			{
				// The defenders win on time only standing on the point alone (fix/pmc-arena-stalemate).
				Out.Winner = Clock.bDefendersHolding ? Defender : TEXT("draw");
				Out.ObjectiveResult = Clock.bDefendersHolding ? TEXT("held") : (Clock.bAttackersHolding ? TEXT("contested") : TEXT("empty"));
			}
		}
		return Out;
	}

	float AttentionOff(const TArray<FVector2D>& WatcherPositions, const TArray<float>& WatcherYawsDeg, const FVector2D& Point, float HalfAngleDeg, float RangeCm)
	{
		float Off = 1.f;
		for (int32 Index = 0; Index < FMath::Min(WatcherPositions.Num(), WatcherYawsDeg.Num()); ++Index)
		{
			if (FVector2D::Distance(WatcherPositions[Index], Point) > RangeCm)
			{
				continue;
			}
			const float Angle = AmbushWatchRules::AngleOffDeg(WatcherYawsDeg[Index], AmbushWatchRules::BearingDeg(WatcherPositions[Index], Point));
			Off = FMath::Min(Off, PmcCommanderLocal::Clamp01((Angle - HalfAngleDeg) / 45.f));
		}
		return Off;
	}

	float AttentionFactor(const FPmcFighterView& Fighter, const FPmcWeights& Weights)
	{
		if (!Fighter.bHasThreat || !Fighter.Threat.bSeenNow || Fighter.Threat.bStandIn)
		{
			return 1.f;
		}
		return FMath::Max(0.f, 1.f + FMath::Max(Weights.Attention, 0.f) * (PmcCommanderLocal::Clamp01(Fighter.Threat.AttentionOff) - 0.5f));
	}

	bool ShouldHoldFire(const FPmcFighterView& F, const FPmcSquadView& S, const FPmcWeights& W)
	{
		if (W.AmbushPatience <= 0.f || !F.Objective.IsActive() || F.Objective.Role != EPmcRole::Defender)
		{
			return false;
		}
		if (S.bShotsExchanged || F.bExposed || S.ContactSeconds >= W.AmbushMaxHoldSeconds)
		{
			return false; // the ambush is blown (a shot exchanged, he is seen) or held long enough
		}
		if (!F.bHasThreat || !F.Threat.bSeenNow || F.Threat.bStandIn)
		{
			return true; // lying in wait: nobody seen now - the discipline stays armed (no auto fire the moment one appears)
		}
		const float Reach = F.Band.IsValid() ? F.Band.MaxCm : F.WeaponRangeCm;
		if (F.Objective.bEnemyOnPoint || F.Threat.DistanceCm <= PmcCommanderLocal::Clamp01(W.KillZoneFrac) * Reach)
		{
			return false; // deep in the kill zone: strike now
		}
		return F.Threat.AttentionOff < PmcCommanderLocal::Clamp01(W.AmbushPatience);
	}

	const TCHAR* ScenarioName(EPmcScenario Scenario)
	{
		switch (Scenario)
		{
		case EPmcScenario::Chokepoint: return TEXT("chokepoint");
		case EPmcScenario::Street: return TEXT("street");
		case EPmcScenario::Roofs: return TEXT("roofs");
		case EPmcScenario::Blizzard: return TEXT("blizzard");
		default: return TEXT("skirmish");
		}
	}

	bool ParseScenario(const FString& Name, EPmcScenario& Out)
	{
		for (int32 Index = 0; Index < static_cast<int32>(EPmcScenario::Count); ++Index)
		{
			if (Name.Equals(ScenarioName(static_cast<EPmcScenario>(Index)), ESearchCase::IgnoreCase))
			{
				Out = static_cast<EPmcScenario>(Index);
				return true;
			}
		}
		return false;
	}

	EPmcScenario PickScenario(int32 RoundSeed)
	{
		FRandomStream Stream(RoundSeed * 31 + 17);
		return static_cast<EPmcScenario>(Stream.RandRange(0, static_cast<int32>(EPmcScenario::Count) - 1));
	}

	bool IsPassage(const float Hits[8], const bool Tall[8], float MinWidthCm, float MaxWidthCm, int32& OutAxisIndex, float& OutWidthCm)
	{
		bool bFound = false;
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const int32 Opposite = Index + 4;
			if (Hits[Index] <= 0.f || Hits[Opposite] <= 0.f || !Tall[Index] || !Tall[Opposite])
			{
				continue;
			}
			const float Width = Hits[Index] + Hits[Opposite];
			const int32 Along = (Index + 2) % 8;
			const int32 AlongBack = (Index + 6) % 8;
			if (Width < MinWidthCm || Width > MaxWidthCm || Hits[Along] > 0.f || Hits[AlongBack] > 0.f)
			{
				continue;
			}
			if (!bFound || Width < OutWidthCm)
			{
				bFound = true;
				OutWidthCm = Width;
				OutAxisIndex = Along;
			}
		}
		return bFound;
	}

	float HighGroundFactor(int32 Level, const FPmcWeights& Weights)
	{
		return 1.f + FMath::Max(0.f, Weights.HighGround) * static_cast<float>(FMath::Clamp(Level, 0, FMath::Max(0, Weights.HighGroundMaxLevels)));
	}

	bool PickSpawnPair(const TArray<FPmcSpawnCandidate>& Candidates, float MinDistCm, float MaxDistCm, int32 Seed,
		TFunctionRef<bool(int32 A, int32 B)> IsReachable, int32& OutA, int32& OutB, int32 MaxReachChecks)
	{
		struct FPair
		{
			int32 A = 0;
			int32 B = 0;
			float Key = 0.f;
		};
		FRandomStream Stream(Seed);
		TArray<FPair> Pairs;
		for (int32 I = 0; I < Candidates.Num(); ++I)
		{
			for (int32 J = I + 1; J < Candidates.Num(); ++J)
			{
				const float Distance = static_cast<float>(FVector::Dist2D(Candidates[I].Location, Candidates[J].Location));
				if (Distance < MinDistCm || Distance > MaxDistCm)
				{
					continue;
				}
				const int32 LevelI = Candidates[I].Level;
				const int32 LevelJ = Candidates[J].Level;
				const float Key = Stream.FRand() + (LevelI != LevelJ ? 0.35f : 0.f) + (FMath::Max(LevelI, LevelJ) > 0 ? 0.15f : 0.f);
				Pairs.Add({ I, J, Key });
			}
		}
		// Stable: equal keys keep the deterministic candidate order.
		Pairs.StableSort([](const FPair& Left, const FPair& Right) { return Left.Key > Right.Key; });
		const int32 Tries = FMath::Min(Pairs.Num(), FMath::Max(1, MaxReachChecks));
		for (int32 Index = 0; Index < Tries; ++Index)
		{
			if (IsReachable(Pairs[Index].A, Pairs[Index].B))
			{
				const bool bSwap = (Stream.RandHelper(2) == 1);
				OutA = bSwap ? Pairs[Index].B : Pairs[Index].A;
				OutB = bSwap ? Pairs[Index].A : Pairs[Index].B;
				return true;
			}
		}
		return false;
	}

	FString DefaultWeightsPath()
	{
		return FPaths::ProjectContentDir() / TEXT("Data/AI/pmc_commander.json");
	}

	bool LoadWeightsFile(const FString& PathOrName, FPmcWeights& Out)
	{
		FString Path = PathOrName.IsEmpty() ? DefaultWeightsPath() : PathOrName;
		if (!FPaths::FileExists(Path))
		{
			Path = FPaths::ProjectContentDir() / TEXT("Data/AI") / PathOrName;
		}
		FString Text;
		TSharedPtr<FJsonObject> Json;
		if (!FPaths::FileExists(Path) || !FFileHelper::LoadFileToString(Text, *Path)
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid())
		{
			Out = FPmcWeights();
			return false;
		}
		Out = FromJson(*Json);
		if (Out.Name == TEXT("default"))
		{
			Out.Name = FPaths::GetBaseFilename(Path);
		}
		return true;
	}
}
