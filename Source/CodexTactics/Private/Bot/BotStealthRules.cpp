#include "Bot/BotStealthRules.h"

#include "Math/RandomStream.h"

float BotStealthRules::SightRisk(const FEnemyPerceptionParams& Params, const FVector& Observer, const FVector& Forward, const FVector& Target,
	EOperativeStance Stance, bool bLineClear, const FBotStealthConfig& Config)
{
	if (!bLineClear)
	{
		return 0.f;
	}
	const float Distance = FMath::Max(FVector::Dist2D(Observer, Target), 1.f);
	const float Range = PerceptionRules::EffectiveSightRange(Params, Stance) * Config.SightMargin;
	FEnemyPerceptionParams Widened = Params;
	Widened.SightHalfAngleDeg = FMath::Min(180.f, Params.SightHalfAngleDeg + Config.FovMarginDeg);
	const bool bNear = Distance <= Params.ProximityCm * Config.SightMargin;
	if (Range <= 0.f || (!bNear && !PerceptionRules::IsInFieldOfView(Widened, Observer, Forward, Target)))
	{
		return 0.f;
	}
	return Range / Distance;
}

float BotStealthRules::HearingRisk(const FEnemyPerceptionParams& Params, ESquadMovementNoise Noise, float DistanceCm, const FBotStealthConfig& Config)
{
	const float Radius = PerceptionRules::HearingRadius(Params, Noise) * Config.HearingMargin;
	return Radius <= 0.f ? 0.f : Radius / FMath::Max(DistanceCm, 1.f);
}

ESquadMovementNoise BotStealthRules::MovingNoise(EOperativeStance Stance)
{
	switch (Stance)
	{
	case EOperativeStance::Prone: return ESquadMovementNoise::Crawl;
	case EOperativeStance::Crouching: return ESquadMovementNoise::CrouchWalk;
	default: return ESquadMovementNoise::Walk;
	}
}

float BotStealthRules::MovementRisk(const TArray<FBotPatrolView>& Patrols, const TArray<bool>& LineClear, const FVector& Position,
	EOperativeStance Stance, const FBotStealthConfig& Config)
{
	float Risk = 0.f;
	for (int32 Index = 0; Index < Patrols.Num(); ++Index)
	{
		const FBotPatrolView& Patrol = Patrols[Index];
		const bool bClear = !LineClear.IsValidIndex(Index) || LineClear[Index];
		Risk = FMath::Max(Risk, SightRisk(Patrol.Params, Patrol.Location, Patrol.Forward, Position, Stance, bClear, Config));
		Risk = FMath::Max(Risk, HearingRisk(Patrol.Params, MovingNoise(Stance), FVector::Dist(Patrol.Location, Position), Config));
	}
	return Risk;
}

EOperativeStance BotStealthRules::ChooseSneakStance(const TArray<FBotPatrolView>& Patrols, const TArray<bool>& LineClear, const FVector& Position,
	const FBotStealthConfig& Config)
{
	for (const EOperativeStance Stance : { EOperativeStance::Standing, EOperativeStance::Crouching })
	{
		if (MovementRisk(Patrols, LineClear, Position, Stance, Config) < 1.f)
		{
			return Stance;
		}
	}
	return EOperativeStance::Prone;
}

EBotStealthAction BotStealthRules::Decide(const FBotStealthInput& In, const FBotStealthConfig& Config, EBotAmbushReason& OutReason)
{
	OutReason = EBotAmbushReason::None;
	const bool bSearch = In.SearcherDistanceCm < TNumericLimits<float>::Max();
	if (!In.bHasTarget)
	{
		return bSearch ? EBotStealthAction::Hide : EBotStealthAction::Sneak;
	}
	const float Range = AmbushRange(In, Config);
	if (In.ElapsedSeconds >= Config.MaxStealthSeconds)
	{
		OutReason = EBotAmbushReason::Forced;
		return EBotStealthAction::Ambush;
	}
	// About to be detected: strike first when the rifles reach, else freeze and hope the suspicion decays.
	if (In.MaxSuspicion >= Config.AlarmSuspicion)
	{
		if (In.SuspiciousDistanceCm <= In.RifleRangeCm)
		{
			OutReason = EBotAmbushReason::PreEmptive;
			return EBotStealthAction::Ambush;
		}
		return EBotStealthAction::Hide;
	}
	const bool bPatient = In.ElapsedSeconds < Config.MinSneakSeconds;
	// A hound's nose is about to find the squad (no stance helps): back off while patient, else strike while the rifles reach.
	if (In.bSmellImminent)
	{
		if (bPatient)
		{
			return EBotStealthAction::Evade;
		}
		if (In.TargetDistanceCm <= In.RifleRangeCm)
		{
			OutReason = EBotAmbushReason::PreEmptive;
			return EBotStealthAction::Ambush;
		}
	}
	// A trap search is on: keep still until a searcher walks into the ambush range.
	if (bSearch)
	{
		if (In.SearcherDistanceCm <= Range)
		{
			OutReason = EBotAmbushReason::SearchContact;
			return EBotStealthAction::Ambush;
		}
		return EBotStealthAction::Hide;
	}
	if (In.bTrapPending)
	{
		return EBotStealthAction::Hide;
	}
	if (In.TargetDistanceCm <= Range)
	{
		if (bPatient)
		{
			return EBotStealthAction::Hold; // in cover / prone, waiting for the moment
		}
		if (In.bLeaderInCover || In.bSquadUnseen)
		{
			OutReason = EBotAmbushReason::InPosition;
			return EBotStealthAction::Ambush;
		}
		return EBotStealthAction::Hold;
	}
	return EBotStealthAction::Sneak;
}

float BotStealthRules::AmbushRange(const FBotStealthInput& In, const FBotStealthConfig& Config)
{
	return FMath::Min(In.RifleRangeCm, FMath::Max(In.RifleRangeCm * Config.AmbushRangeFraction, In.MinAmbushRangeCm));
}

float BotStealthRules::SmellReach(const TArray<FBotPatrolView>& Patrols, const FBotStealthConfig& Config)
{
	float Reach = 0.f;
	for (const FBotPatrolView& Patrol : Patrols)
	{
		Reach = FMath::Max(Reach, Patrol.Params.SmellRadiusCm * Config.HearingMargin);
	}
	return Reach;
}

FVector BotStealthRules::ApproachPoint(const FVector& Patrol, const FVector& PatrolForward, const FVector& Squad, float StandoffCm)
{
	const FVector Behind = -PatrolForward.GetSafeNormal2D();
	const FVector ToSquad = (Squad - Patrol).GetSafeNormal2D();
	FVector Direction = (Behind * 0.6f + ToSquad * 0.4f).GetSafeNormal2D();
	if (Direction.IsNearlyZero())
	{
		Direction = ToSquad.IsNearlyZero() ? Behind : ToSquad;
	}
	if (Direction.IsNearlyZero())
	{
		Direction = FVector::BackwardVector;
	}
	return FVector(Patrol.X, Patrol.Y, Patrol.Z) + Direction * StandoffCm;
}

ESquadFirePosture BotStealthRules::PostureFor(bool bFightStarted)
{
	return bFightStarted ? ESquadFirePosture::Aggressive : ESquadFirePosture::Passive;
}

FBotStealthConfig BotStealthRules::MakeSeededConfig(int32 Seed, bool bHasTrap)
{
	// Small sequential seeds (bot_run.ps1 passes the run number) give correlated FRandomStream draws: scramble first.
	FRandomStream Stream(static_cast<int32>(static_cast<uint32>(Seed) * 2654435761u ^ 0x5bd1e995u));
	for (int32 Warmup = 0; Warmup < 4; ++Warmup)
	{
		Stream.FRand();
	}
	FBotStealthConfig Config;
	Config.AmbushRangeFraction = 0.55f + 0.35f * Stream.FRand();
	Config.AlarmSuspicion = 0.35f + 0.3f * Stream.FRand();
	Config.MaxStealthSeconds = 180.f + 120.f * Stream.FRand();
	const bool bTrapRoll = Stream.FRand() < 0.5f;
	Config.bUseTrap = bHasTrap && bTrapRoll;
	Config.MinSneakSeconds = 20.f + 100.f * Stream.FRand();
	// Caution: how wide a berth the bot gives the patrols' eyes / ears (a careless bot crouches later, a careful one earlier).
	Config.SightMargin = 1.0f + 0.35f * Stream.FRand();
	Config.HearingMargin = 1.15f + 0.45f * Stream.FRand();
	Config.ApproachAngleDeg = -60.f + 120.f * Stream.FRand();
	return Config;
}

const TCHAR* BotStealthRules::ActionName(EBotStealthAction Action)
{
	switch (Action)
	{
	case EBotStealthAction::Hold: return TEXT("hold");
	case EBotStealthAction::Hide: return TEXT("hide");
	case EBotStealthAction::Evade: return TEXT("evade");
	case EBotStealthAction::Ambush: return TEXT("ambush");
	default: return TEXT("sneak");
	}
}

const TCHAR* BotStealthRules::ReasonName(EBotAmbushReason Reason)
{
	switch (Reason)
	{
	case EBotAmbushReason::InPosition: return TEXT("in_position");
	case EBotAmbushReason::PreEmptive: return TEXT("pre_emptive");
	case EBotAmbushReason::SearchContact: return TEXT("search_contact");
	case EBotAmbushReason::Forced: return TEXT("forced");
	default: return TEXT("none");
	}
}

const TCHAR* BotStealthRules::StanceName(EOperativeStance Stance)
{
	switch (Stance)
	{
	case EOperativeStance::Crouching: return TEXT("crouch");
	case EOperativeStance::Prone: return TEXT("prone");
	default: return TEXT("stand");
	}
}
