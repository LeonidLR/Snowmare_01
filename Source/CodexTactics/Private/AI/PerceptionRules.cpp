#include "AI/PerceptionRules.h"

#include "Combat/SightRules.h"

namespace PerceptionRules
{
	static FEnemyPerceptionParams MakePerception(float SightM, float HalfAngle, float Crouch, float Prone, float DetectSeconds,
		float WalkM, float RunM, float CrouchM, float CrawlM, float GunshotM, float ExplosionM, float SmellM)
	{
		FEnemyPerceptionParams Params;
		Params.SightRangeCm = SightM * 100.f;
		Params.SightHalfAngleDeg = HalfAngle;
		Params.StandingVisibility = 1.f;
		Params.CrouchingVisibility = Crouch;
		Params.ProneVisibility = Prone;
		Params.TimeToDetectSeconds = DetectSeconds;
		Params.HearWalkCm = WalkM * 100.f;
		Params.HearRunCm = RunM * 100.f;
		Params.HearCrouchWalkCm = CrouchM * 100.f;
		Params.HearCrawlCm = CrawlM * 100.f;
		Params.HearGunshotCm = GunshotM * 100.f;
		Params.HearExplosionCm = ExplosionM * 100.f;
		Params.SmellRadiusCm = SmellM * 100.f;
		return Params;
	}
}

FEnemyPerceptionParams PerceptionRules::GetArchetypeDefaults(EEnemyArchetype Archetype)
{
	// sight m, FOV half-angle, crouch / prone visibility, time to detect s, hearing walk / run / crouch / crawl m,
	// gunshot m, grenade m, smell m. Mirrors Content/Data/AI/enemy_perception.json. Footstep radii lowered 2026-10-06
	// (user playtest: heard from very far when running): run ~10-12 m, walk ~5-6 m, hounds a bit more, crouch / crawl small.
	switch (Archetype)
	{
	case EEnemyArchetype::FrostHound: // short sight, wide view, good ears, big nose
		return MakePerception(15.f, 70.f, 0.75f, 0.4f, 0.6f, 7.f, 14.f, 3.5f, 1.5f, 45.f, 60.f, 12.f);
	case EEnemyArchetype::Marksman: // long sight through a narrow scope view, average ears
		return MakePerception(45.f, 30.f, 0.7f, 0.35f, 1.0f, 5.f, 10.f, 2.f, 1.f, 40.f, 60.f, 0.f);
	case EEnemyArchetype::Spitter:
		return MakePerception(25.f, 50.f, 0.7f, 0.4f, 0.8f, 5.5f, 11.f, 2.5f, 1.f, 40.f, 60.f, 0.f);
	case EEnemyArchetype::Brute: // slow, dull senses
		return MakePerception(20.f, 45.f, 0.7f, 0.4f, 1.2f, 5.f, 10.f, 2.f, 1.f, 35.f, 50.f, 0.f);
	case EEnemyArchetype::Cutter: // agile predator
		return MakePerception(25.f, 55.f, 0.7f, 0.4f, 0.7f, 6.f, 12.f, 3.f, 1.5f, 40.f, 60.f, 0.f);
	case EEnemyArchetype::Frostbitten:
	default:
		return MakePerception(20.f, 50.f, 0.7f, 0.4f, 1.0f, 5.5f, 11.f, 2.5f, 1.f, 35.f, 50.f, 0.f);
	}
}

bool PerceptionRules::CanSmell(EEnemyArchetype Archetype)
{
	return Archetype == EEnemyArchetype::FrostHound;
}

FEnemyPerceptionParams PerceptionRules::Sanitize(EEnemyArchetype Archetype, const FEnemyPerceptionParams& Params)
{
	FEnemyPerceptionParams Out = Params;
	if (!CanSmell(Archetype))
	{
		Out.SmellRadiusCm = 0.f;
	}
	Out.SightHalfAngleDeg = FMath::Clamp(Out.SightHalfAngleDeg, 0.f, 180.f);
	Out.HearingOcclusionPerWall = FMath::Clamp(Out.HearingOcclusionPerWall, 0.f, 1.f);
	return Out;
}

FEnemyPerceptionParams PerceptionRules::Scaled(const FEnemyPerceptionParams& Params, float Multiplier)
{
	const float M = FMath::Max(Multiplier, 0.f);
	FEnemyPerceptionParams Out = Params;
	Out.SightRangeCm *= M;
	Out.ProximityCm *= M;
	Out.HearWalkCm *= M;
	Out.HearRunCm *= M;
	Out.HearCrouchWalkCm *= M;
	Out.HearCrawlCm *= M;
	Out.HearGunshotCm *= M;
	Out.HearExplosionCm *= M;
	Out.SmellRadiusCm *= M;
	return Out;
}

float PerceptionRules::StanceVisibility(const FEnemyPerceptionParams& Params, EOperativeStance Stance)
{
	switch (Stance)
	{
	case EOperativeStance::Crouching:
		return Params.CrouchingVisibility;
	case EOperativeStance::Prone:
		return Params.ProneVisibility;
	default:
		return Params.StandingVisibility;
	}
}

float PerceptionRules::EffectiveSightRange(const FEnemyPerceptionParams& Params, EOperativeStance Stance)
{
	return FMath::Max(Params.SightRangeCm * StanceVisibility(Params, Stance), 0.f);
}

bool PerceptionRules::IsInFieldOfView(const FEnemyPerceptionParams& Params, const FVector& Observer, const FVector& Forward, const FVector& Target)
{
	if (Params.SightHalfAngleDeg >= 180.f)
	{
		return true;
	}
	const FVector ToTarget = FVector(Target.X - Observer.X, Target.Y - Observer.Y, 0.f).GetSafeNormal();
	const FVector Facing = FVector(Forward.X, Forward.Y, 0.f).GetSafeNormal();
	if (ToTarget.IsNearlyZero() || Facing.IsNearlyZero())
	{
		return true;
	}
	return FVector::DotProduct(Facing, ToTarget) >= FMath::Cos(FMath::DegreesToRadians(Params.SightHalfAngleDeg)) - KINDA_SMALL_NUMBER;
}

bool PerceptionRules::CanSee(const FEnemyPerceptionParams& Params, const FVector& Observer, const FVector& Forward, const FVector& Target,
	EOperativeStance TargetStance, bool bLineClear)
{
	if (!bLineClear)
	{
		return false;
	}
	const float Distance = FVector::Dist(Observer, Target);
	if (Distance > EffectiveSightRange(Params, TargetStance))
	{
		return false;
	}
	return Distance <= Params.ProximityCm || IsInFieldOfView(Params, Observer, Forward, Target);
}

bool PerceptionRules::IsLineClearOverCover(EOperativeStance TargetStance, float CoverDistanceCm, float TargetDistanceCm, float CoverTopCm,
	float ObserverEyeHeightCm)
{
	return SightRules::ClearsCover(ObserverEyeHeightCm, SightRules::ProfileHeight(TargetStance), CoverDistanceCm, TargetDistanceCm, CoverTopCm);
}

ESquadMovementNoise PerceptionRules::ClassifyMovement(EOperativeStance Stance, float SpeedCmS, bool bSprinting)
{
	if (SpeedCmS < 20.f)
	{
		return ESquadMovementNoise::Still;
	}
	switch (Stance)
	{
	case EOperativeStance::Prone:
		return ESquadMovementNoise::Crawl;
	case EOperativeStance::Crouching:
		return ESquadMovementNoise::CrouchWalk;
	default:
		return bSprinting ? ESquadMovementNoise::Run : ESquadMovementNoise::Walk;
	}
}

float PerceptionRules::HearingRadius(const FEnemyPerceptionParams& Params, ESquadMovementNoise Noise)
{
	switch (Noise)
	{
	case ESquadMovementNoise::Crawl:
		return Params.HearCrawlCm;
	case ESquadMovementNoise::CrouchWalk:
		return Params.HearCrouchWalkCm;
	case ESquadMovementNoise::Walk:
		return Params.HearWalkCm;
	case ESquadMovementNoise::Run:
		return Params.HearRunCm;
	default:
		return 0.f;
	}
}

bool PerceptionRules::HearsMovement(const FEnemyPerceptionParams& Params, ESquadMovementNoise Noise, float DistanceCm)
{
	const float Radius = HearingRadius(Params, Noise);
	return Radius > 0.f && DistanceCm >= 0.f && DistanceCm <= Radius;
}

float PerceptionRules::OccludedRadius(float RadiusCm, int32 Walls, float PerWallFactor)
{
	const int32 Count = FMath::Clamp(Walls, 0, MaxHearingOccluders);
	return FMath::Max(RadiusCm, 0.f) * FMath::Pow(FMath::Clamp(PerWallFactor, 0.f, 1.f), static_cast<float>(Count));
}

bool PerceptionRules::HearsMovementThroughWalls(const FEnemyPerceptionParams& Params, ESquadMovementNoise Noise, float DistanceCm, int32 Walls)
{
	const float Radius = OccludedRadius(HearingRadius(Params, Noise), Walls, Params.HearingOcclusionPerWall);
	return Radius > 0.f && DistanceCm >= 0.f && DistanceCm <= Radius;
}

bool PerceptionRules::HearsGunshot(const FEnemyPerceptionParams& Params, float DistanceCm)
{
	return Params.HearGunshotCm > 0.f && DistanceCm >= 0.f && DistanceCm <= Params.HearGunshotCm;
}

bool PerceptionRules::HearsExplosion(const FEnemyPerceptionParams& Params, float DistanceCm)
{
	return Params.HearExplosionCm > 0.f && DistanceCm >= 0.f && DistanceCm <= Params.HearExplosionCm;
}

bool PerceptionRules::Smells(const FEnemyPerceptionParams& Params, float DistanceCm)
{
	return Params.SmellRadiusCm > 0.f && DistanceCm >= 0.f && DistanceCm <= Params.SmellRadiusCm;
}

float PerceptionRules::StepSuspicion(const FEnemyPerceptionParams& Params, float Current, float DeltaSeconds, bool bSeesSomeone,
	float DistanceCm, float EffectiveRangeCm)
{
	if (!bSeesSomeone)
	{
		return FMath::Max(Current - Params.SuspicionDecayPerSecond * DeltaSeconds, 0.f);
	}
	if (Params.TimeToDetectSeconds <= 0.f)
	{
		return 1.f;
	}
	const float Closeness = EffectiveRangeCm > 0.f ? 1.f - FMath::Clamp(DistanceCm / EffectiveRangeCm, 0.f, 1.f) : 1.f;
	const float Rate = (1.f + 2.f * Closeness) / Params.TimeToDetectSeconds;
	return FMath::Min(Current + Rate * DeltaSeconds, 1.f);
}
