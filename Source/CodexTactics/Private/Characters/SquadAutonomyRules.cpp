#include "Characters/SquadAutonomyRules.h"

float SquadAutonomyRules::LeashRadius(const FSquadROE& ROE, bool bForAid)
{
	const float Radius = FMath::Max(0.f, ROE.AnchorRadiusMeters) * 100.f;
	return bForAid && ROE.LeashStrictness == ELeashStrictness::Flexible ? FMath::Max(Radius, FlexibleAidLeashCm) : Radius;
}

FTacticalAnchor SquadAutonomyRules::MakeAnchor(const FSquadROE& ROE, const FVector& From, const FVector& Destination, const FRotator& CurrentFacing)
{
	FTacticalAnchor Anchor;
	Anchor.Location = Destination;
	Anchor.Radius = LeashRadius(ROE, false);
	const FVector Direction = (Destination - From).GetSafeNormal2D();
	Anchor.GuardFacing = Direction.IsNearlyZero() ? FRotator(0.f, CurrentFacing.Yaw, 0.f) : Direction.Rotation();
	Anchor.bIsActive = true;
	return Anchor;
}

bool SquadAutonomyRules::IsInsideLeash(const FTacticalAnchor& Anchor, const FVector& Point, float RadiusCm)
{
	return !Anchor.bIsActive || FVector::Dist2D(Anchor.Location, Point) <= RadiusCm;
}

FVector SquadAutonomyRules::ClampToLeash(const FTacticalAnchor& Anchor, const FVector& Point, float RadiusCm)
{
	if (IsInsideLeash(Anchor, Point, RadiusCm))
	{
		return Point;
	}
	const FVector Offset = (Point - Anchor.Location).GetSafeNormal2D() * RadiusCm;
	return FVector(Anchor.Location.X + Offset.X, Anchor.Location.Y + Offset.Y, Point.Z);
}

EOperativeStance SquadAutonomyRules::DesiredStance(const FSquadROE& ROE, bool bInCover, bool bSniperAiming, bool bCoverReachable)
{
	const EOperativeStance Cover = ROE.CoverStance == ECoverStance::Standing ? EOperativeStance::Standing : EOperativeStance::Crouching;
	if (bSniperAiming)
	{
		if (ROE.SniperReaction == ESniperReaction::DropProne)
		{
			return EOperativeStance::Prone;
		}
		// DiveToCover: behind the barricade at least crouched; in the open (no cover to reach) flat on the ground.
		if (bInCover)
		{
			return EOperativeStance::Crouching;
		}
		return bCoverReachable ? EOperativeStance::Crouching : EOperativeStance::Prone;
	}
	if (bInCover)
	{
		return Cover;
	}
	switch (ROE.OpenGroundStance)
	{
	case EOpenGroundStance::Prone:
		return EOperativeStance::Prone;
	case EOpenGroundStance::Standing:
		return EOperativeStance::Standing;
	default:
		return EOperativeStance::Crouching;
	}
}

bool SquadAutonomyRules::IsFlankThreat(const FVector& Forward, const FVector& Position, const FVector& Enemy, float AngleDeg)
{
	const FVector Facing = Forward.GetSafeNormal2D();
	const FVector ToEnemy = (Enemy - Position).GetSafeNormal2D();
	if (Facing.IsNearlyZero() || ToEnemy.IsNearlyZero())
	{
		return false;
	}
	return FVector::DotProduct(Facing, ToEnemy) < FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(AngleDeg, 0.f, 180.f)));
}

bool SquadAutonomyRules::ShouldReload(const FSquadROE& ROE, int32 Clip, int32 MaxClip, int32 Reserve, bool bReloading, bool bInCover,
	float NearestEnemyCm)
{
	if (bReloading || MaxClip <= 0 || Reserve <= 0 || Clip >= MaxClip)
	{
		return false;
	}
	if (Clip == 0)
	{
		return true; // nothing to shoot with: reload wherever he is
	}
	const bool bLow = static_cast<float>(Clip) < static_cast<float>(MaxClip) * FMath::Clamp(ROE.AutoReloadThresholdPct, 0.f, 100.f) / 100.f;
	return bLow && (bInCover || NearestEnemyCm > UnderThreatDistanceCm);
}

bool SquadAutonomyRules::ShouldSwitchToSidearm(const FSquadROE& ROE, float NearestEnemyCm, int32 PrimaryClip, bool bReloading, int32 SidearmRounds)
{
	return SidearmRounds > 0 && (PrimaryClip <= 0 || bReloading) && NearestEnemyCm < ROE.EmergencySidearmDistMeters * 100.f;
}

bool SquadAutonomyRules::ShouldSwitchBackToPrimary(const FSquadROE& ROE, float NearestEnemyCm, int32 PrimaryRounds)
{
	return PrimaryRounds > 0 && NearestEnemyCm >= ROE.EmergencySidearmDistMeters * 200.f;
}

int32 SquadAutonomyRules::ThreatTier(EEnemyArchetype Archetype)
{
	switch (Archetype)
	{
	case EEnemyArchetype::Marksman:
	case EEnemyArchetype::Spitter:
		return 3;
	case EEnemyArchetype::FrostHound:
	case EEnemyArchetype::CryoDrone:
		return 2;
	case EEnemyArchetype::Cutter:
	case EEnemyArchetype::Brute:
		return 1;
	default:
		return 0;
	}
}

float SquadAutonomyRules::TargetScore(const FSquadROE& ROE, const FAutonomyTargetCandidate& Candidate)
{
	const float DistanceM = Candidate.DistanceCm / 100.f;
	float Score = -DistanceM + (Candidate.bCurrent ? 3.f : 0.f);
	if (Candidate.DistanceCm < ROE.EmergencySidearmDistMeters * 100.f)
	{
		Score += 10000.f; // point-blank: self-defense before any policy
	}
	switch (ROE.TargetPriorityPolicy)
	{
	case ETargetPriorityPolicy::ThreatLevel:
		Score += 100.f * (ThreatTier(Candidate.Archetype) + (Candidate.bAimingAtSquad ? 1 : 0));
		break;
	case ETargetPriorityPolicy::LowestHP:
		Score += 100.f * (1.f - FMath::Clamp(Candidate.HealthFraction, 0.f, 1.f));
		break;
	case ETargetPriorityPolicy::AssistLeader:
		Score += Candidate.bLeaderTarget ? 1000.f : 0.f;
		break;
	default:
		break;
	}
	return Score;
}

int32 SquadAutonomyRules::ChooseTarget(const FSquadROE& ROE, const TArray<FAutonomyTargetCandidate>& Candidates)
{
	int32 Best = INDEX_NONE;
	float BestScore = -TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		if (!Candidates[Index].bCanHit)
		{
			continue;
		}
		const float Score = TargetScore(ROE, Candidates[Index]);
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = Index;
		}
	}
	return Best;
}

bool SquadAutonomyRules::NeedsAid(const FSquadROE& ROE, float PatientHealthFraction, bool bDowned)
{
	return bDowned || (PatientHealthFraction > 0.f && PatientHealthFraction * 100.f < ROE.AidHealthThresholdPct);
}

bool SquadAutonomyRules::CanGiveAid(const FSquadROE& ROE, float RescuerHealthFraction, int32 Medkits)
{
	if (Medkits <= 0)
	{
		return false;
	}
	const bool bKeepLast = ROE.bReservePersonalMedkit && RescuerHealthFraction < ReserveMedkitHealthFraction;
	return !bKeepLast || Medkits >= 2;
}

bool SquadAutonomyRules::IsSafeAidRoute(const FSquadROE& ROE, bool bSniperAimingOnRoute, float NearestEnemyToPatientCm)
{
	return !ROE.bRequireSafeRouteForAid || (!bSniperAimingOnRoute && NearestEnemyToPatientCm > SafeAidEnemyClearanceCm);
}

FString SquadAutonomyRules::PolicyName(ETargetPriorityPolicy Policy)
{
	switch (Policy)
	{
	case ETargetPriorityPolicy::ClosestFirst:
		return TEXT("ClosestFirst");
	case ETargetPriorityPolicy::LowestHP:
		return TEXT("LowestHP");
	case ETargetPriorityPolicy::AssistLeader:
		return TEXT("AssistLeader");
	default:
		return TEXT("ThreatLevel");
	}
}
