#include "Characters/EnemyAIRules.h"
#include "Data/GodotBalanceAsset.h"

FEnemyAIConfig EnemyAIRules::ConfigFromBalance(const UGodotBalanceAsset* Balance)
{
	FEnemyAIConfig Config;
	if (!Balance)
	{
		return Config;
	}
	Config.bCanTargetTurrets = Balance->GetNumber(TEXT("enemy_target_turrets"), 1.f) > 0.5f;
	Config.TurretThreatDistance = Balance->GetNumber(TEXT("enemy_turret_threat_distance"), Config.TurretThreatDistance / 100.f) * 100.f;
	Config.bFireFearEnabled = Balance->GetNumber(TEXT("enemy_fire_fear_enabled"), 1.f) > 0.5f;
	Config.FireFearRadius = Balance->GetNumber(TEXT("enemy_fire_fear_radius"), Config.FireFearRadius / 100.f) * 100.f;
	Config.FireFearFleeSpeedMultiplier = Balance->GetNumber(TEXT("enemy_fire_fear_flee_speed_mult"), Config.FireFearFleeSpeedMultiplier);
	Config.SpitterPreferredRange = Balance->GetNumber(TEXT("spitter_preferred_range"), Config.SpitterPreferredRange / 100.f) * 100.f;
	Config.CrouchCoverReduction = Balance->GetNumber(TEXT("crouch_barricade_cover_reduction"), Config.CrouchCoverReduction);
	return Config;
}

bool EnemyAIRules::IsSmallEnemy(EEnemyArchetype Archetype)
{
	return Archetype == EEnemyArchetype::FrostHound || Archetype == EEnemyArchetype::Cutter || Archetype == EEnemyArchetype::Frostbitten;
}

FElementalAffinities EnemyAIRules::GetAffinities(EEnemyArchetype Archetype)
{
	FElementalAffinities A; // enemy_base.gd: 1, 1, 1.5, 0, 1, 1.2
	switch (Archetype)
	{
	case EEnemyArchetype::FrostHound: A.Kinetic = 1.f; A.Melee = 1.3f; A.Fire = 1.8f; A.Cryo = 0.f; A.Energy = 1.f; A.Explosive = 1.2f; break;
	case EEnemyArchetype::Spitter: A.Kinetic = 0.6f; A.Melee = 1.f; A.Fire = 1.75f; A.Cryo = 0.f; A.Energy = 1.4f; A.Explosive = 1.5f; break;
	case EEnemyArchetype::Brute: A.Kinetic = 0.25f; A.Melee = 1.f; A.Fire = 1.8f; A.Cryo = 0.f; A.Energy = 2.f; A.Explosive = 1.6f; break;
	case EEnemyArchetype::Frostbitten: A.Kinetic = 1.f; A.Melee = 1.2f; A.Fire = 1.8f; A.Cryo = 0.f; A.Energy = 1.f; A.Explosive = 1.3f; break;
	case EEnemyArchetype::Cutter: A.Kinetic = 1.f; A.Melee = 1.1f; A.Fire = 1.4f; A.Cryo = 0.2f; A.Energy = 1.5f; A.Explosive = 1.3f; break;
	case EEnemyArchetype::CryoDrone: A.Kinetic = 1.25f; A.Melee = 0.5f; A.Fire = 2.f; A.Cryo = 0.f; A.Energy = 1.2f; A.Explosive = 1.5f; break;
	default: break;
	}
	return A;
}

float EnemyAIRules::GetBaseArmor(EEnemyArchetype Archetype)
{
	switch (Archetype)
	{
	case EEnemyArchetype::Spitter: return 0.4f;
	case EEnemyArchetype::Brute: return 0.75f;
	case EEnemyArchetype::Frostbitten:
	case EEnemyArchetype::Cutter: return 0.15f;
	case EEnemyArchetype::CryoDrone: return 0.05f;
	default: return 0.1f;
	}
}

int32 EnemyAIRules::SelectTarget(const FEnemyAIConfig& Config, bool bSmallEnemy, const FVector& Enemy,
	const TArray<FEnemyTargetCandidate>& Candidates, bool bLastAttackerTurret)
{
	int32 Best = INDEX_NONE;
	float MinDistance = 999900.f;
	auto Consider = [&](EEnemyTargetKind Kind, float Weight)
	{
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			const FEnemyTargetCandidate& Candidate = Candidates[Index];
			if (Candidate.Kind == Kind && Candidate.bUsable)
			{
				const float Effective = FVector::Dist(Enemy, Candidate.Location) * Weight;
				if (Effective < MinDistance)
				{
					MinDistance = Effective;
					Best = Index;
				}
			}
		}
	};
	if (bSmallEnemy)
	{
		Consider(EEnemyTargetKind::Generator, 0.4f);
		if (Config.bCanTargetTurrets)
		{
			Consider(EEnemyTargetKind::Turret, 0.5f);
		}
	}
	Consider(EEnemyTargetKind::Operative, bSmallEnemy ? 1.f : 0.7f);
	if (!bSmallEnemy && Config.bCanTargetTurrets)
	{
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			const FEnemyTargetCandidate& Candidate = Candidates[Index];
			if (Candidate.Kind != EEnemyTargetKind::Turret || !Candidate.bUsable)
			{
				continue;
			}
			const float Distance = FVector::Dist(Enemy, Candidate.Location);
			const float Effective = Distance * (bLastAttackerTurret ? 0.6f : 0.85f);
			if (Effective < MinDistance || (Distance <= Config.TurretThreatDistance && (Best == INDEX_NONE || Distance < MinDistance * 1.25f)))
			{
				MinDistance = Effective;
				Best = Index;
			}
		}
	}
	return Best;
}

int32 EnemyAIRules::SelectBlockingObstacle(const FVector& Enemy, const FVector* Target, const TArray<FVector>& Obstacles)
{
	int32 Best = INDEX_NONE;
	float MinDistance = 220.f;
	for (int32 Index = 0; Index < Obstacles.Num(); ++Index)
	{
		const float Distance = FVector::Dist(Enemy, Obstacles[Index]);
		if (Distance >= MinDistance)
		{
			continue;
		}
		if (Target && ((*Target - Enemy).GetSafeNormal() | (Obstacles[Index] - Enemy).GetSafeNormal()) <= 0.15f)
		{
			continue;
		}
		MinDistance = Distance;
		Best = Index;
	}
	return Best;
}

bool EnemyAIRules::CanMelee(float DistanceCm, float HeightDifferenceCm, float AttackRangeCm)
{
	return DistanceCm <= AttackRangeCm && HeightDifferenceCm <= 120.f;
}

FVector EnemyAIRules::FleeDirection(const FVector& Enemy, const FVector& Fire, const FVector* Target)
{
	FVector Away = FVector(Enemy.X - Fire.X, Enemy.Y - Fire.Y, 0.f).GetSafeNormal();
	if (Away.IsNearlyZero())
	{
		Away = FVector(1.f, 0.f, 0.f);
	}
	if (!Target)
	{
		return Away;
	}
	const FVector ToTarget = FVector(Target->X - Enemy.X, Target->Y - Enemy.Y, 0.f).GetSafeNormal();
	if (ToTarget.SizeSquared() <= 0.0001f)
	{
		return Away;
	}
	FVector Tangent(-Away.Y, Away.X, 0.f);
	if ((Tangent | ToTarget) < 0.f)
	{
		Tangent = -Tangent;
	}
	return (Away * 0.7f + Tangent * 0.5f).GetSafeNormal();
}

bool EnemyAIRules::FireDetourWaypoint(const FVector& Enemy, const FVector& Fire, float AvoidRadius, const FVector& Target,
	FVector& OutWaypoint, float StepDegrees)
{
	const FVector2D E(Enemy.X - Fire.X, Enemy.Y - Fire.Y);
	const FVector2D T(Target.X - Fire.X, Target.Y - Fire.Y);
	const FVector2D Segment = T - E;
	// Closest approach of the straight way to the fire.
	const float Along = Segment.SizeSquared() > KINDA_SMALL_NUMBER
		? FMath::Clamp(-FVector2D::DotProduct(E, Segment) / Segment.SizeSquared(), 0.f, 1.f) : 0.f;
	const float Closest = (E + Segment * Along).Size();
	const bool bTargetInside = T.Size() < AvoidRadius;
	if (Closest >= AvoidRadius && !bTargetInside)
	{
		return false;
	}
	const float EnemyAngle = FMath::Atan2(E.Y, E.X);
	const float TargetAngle = FMath::Atan2(T.Y, T.X);
	const float Delta = FMath::UnwindRadians(TargetAngle - EnemyAngle);
	const float Step = FMath::Sign(Delta) * FMath::Min(FMath::Abs(Delta), FMath::DegreesToRadians(StepDegrees));
	const float Orbit = AvoidRadius + 100.f;
	const float Angle = EnemyAngle + Step;
	OutWaypoint = FVector(Fire.X + Orbit * FMath::Cos(Angle), Fire.Y + Orbit * FMath::Sin(Angle), Enemy.Z);
	return true;
}

int32 EnemyAIRules::SelectSpitterTarget(const FVector& Spitter, const TArray<FVector>& Operatives, const TArray<bool>& Elevated)
{
	int32 Best = INDEX_NONE;
	float BestScore = -99999.f;
	for (int32 Index = 0; Index < Operatives.Num(); ++Index)
	{
		const FVector& Target = Operatives[Index];
		if (Target.Z - Spitter.Z >= 180.f && FVector::Dist2D(Target, Spitter) <= 220.f)
		{
			continue; // under the platform
		}
		const float Score = 100.f - FVector::Dist(Spitter, Target) / 100.f + (Elevated.IsValidIndex(Index) && Elevated[Index] ? 50.f : 0.f);
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = Index;
		}
	}
	return Best;
}

ESpitterMove EnemyAIRules::SpitterMove(bool bHasLos, float DistanceCm, float PreferredRangeCm)
{
	if (!bHasLos || DistanceCm > PreferredRangeCm + 200.f)
	{
		return ESpitterMove::Approach;
	}
	return DistanceCm < PreferredRangeCm - 300.f ? ESpitterMove::Retreat : ESpitterMove::Hold;
}

FSpitterLine EnemyAIRules::JudgeSpitterLine(EShotLineHit Hit, EOperativeStance TargetStance, float CrouchCoverReduction)
{
	FSpitterLine Line;
	switch (Hit)
	{
	case EShotLineHit::Clear:
		Line.bHasLos = true;
		break;
	case EShotLineHit::Barricade:
		Line.bHasLos = TargetStance != EOperativeStance::Prone;
		Line.Cover = TargetStance == EOperativeStance::Crouching ? 1.f - CrouchCoverReduction : 1.f;
		break;
	default:
		break;
	}
	return Line;
}
