#include "Characters/EnemyAIRules.h"
#include "Combat/FireGateRules.h"
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
	// UE-only Marksman (no Godot reference): a humanoid in winter gear, weak to fire and melee up close.
	case EEnemyArchetype::Marksman: A.Kinetic = 1.f; A.Melee = 1.3f; A.Fire = 1.6f; A.Cryo = 0.3f; A.Energy = 1.2f; A.Explosive = 1.3f; break;
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
	case EEnemyArchetype::Cutter:
	case EEnemyArchetype::Marksman: return 0.15f;
	case EEnemyArchetype::CryoDrone: return 0.05f;
	default: return 0.1f;
	}
}

EEnemyArchetype EnemyAIRules::ResolvePlacedArchetype(EEnemyArchetype InstanceValue, EEnemyArchetype ClassDefaultValue, const TArray<FString>& ClassChain,
	const TArray<TPair<EEnemyArchetype, FString>>& Mapping, FString& OutSource)
{
	if (InstanceValue != ClassDefaultValue)
	{
		OutSource = TEXT("instance");
		return InstanceValue;
	}
	for (const FString& ClassPath : ClassChain)
	{
		for (const TPair<EEnemyArchetype, FString>& Entry : Mapping)
		{
			if (!Entry.Value.IsEmpty() && Entry.Value.Equals(ClassPath, ESearchCase::IgnoreCase))
			{
				OutSource = TEXT("class mapping");
				return Entry.Key;
			}
		}
	}
	OutSource = TEXT("class default");
	return ClassDefaultValue;
}

FString EnemyAIRules::ResolveDisplayName(const FString& ArchetypeName, const FString& Override)
{
	const FString Trimmed = Override.TrimStartAndEnd();
	return Trimmed.IsEmpty() ? ArchetypeName : Trimmed;
}

FString EnemyAIRules::NormalizeKnockdownClipFolder(const FString& Folder)
{
	FString Result = Folder.TrimStartAndEnd().Replace(TEXT("\\"), TEXT("/"));
	const FString Prefix = TEXT("/Game/Animations_KnockDown");
	if (Result.StartsWith(Prefix, ESearchCase::IgnoreCase))
	{
		Result.RightChopInline(Prefix.Len());
	}
	while (Result.StartsWith(TEXT("/")))
	{
		Result.RightChopInline(1);
	}
	while (Result.EndsWith(TEXT("/")))
	{
		Result.LeftChopInline(1);
	}
	return Result;
}

int32 EnemyAIRules::SelectTarget(const FEnemyAIConfig& Config, bool bSmallEnemy, const FVector& Enemy,
	const TArray<FEnemyTargetCandidate>& Candidates, bool bLastAttackerTurret, bool bObjectsAllowed,
	int32 CurrentTargetIndex)
{
	int32 Best = INDEX_NONE;
	float MinDistance = 999900.f;
	// Operatives first (2026-10-25): a turret only distracts when allowed, or when no operative / manned vehicle is left to go for.
	bool bSquadTarget = false;
	for (const FEnemyTargetCandidate& Candidate : Candidates)
	{
		bSquadTarget |= Candidate.bUsable && (Candidate.Kind == EEnemyTargetKind::Operative || Candidate.Kind == EEnemyTargetKind::Vehicle);
	}
	const bool bTurrets = Config.bCanTargetTurrets && (bObjectsAllowed || !bSquadTarget);
	auto Consider = [&](EEnemyTargetKind Kind, float Weight)
	{
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			const FEnemyTargetCandidate& Candidate = Candidates[Index];
			if (Candidate.Kind == Kind && Candidate.bUsable)
			{
				float Effective = FVector::Dist(Enemy, Candidate.Location) * Weight;
				if (Index == CurrentTargetIndex && Config.TargetHysteresisFactor > 0.f)
				{
					Effective *= Config.TargetHysteresisFactor;
				}
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
		if (bTurrets)
		{
			Consider(EEnemyTargetKind::Turret, 0.5f);
		}
	}
	Consider(EEnemyTargetKind::Operative, bSmallEnemy ? 1.f : 0.7f);
	// APC P4: a manned vehicle (the crew inside cannot be hit: the hull is the target), at its own weight.
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		const FEnemyTargetCandidate& Candidate = Candidates[Index];
		if (Candidate.Kind == EEnemyTargetKind::Vehicle && Candidate.bUsable)
		{
			float Effective = FVector::Dist(Enemy, Candidate.Location) * Candidate.Weight;
			if (Index == CurrentTargetIndex && Config.TargetHysteresisFactor > 0.f)
			{
				Effective *= Config.TargetHysteresisFactor;
			}
			if (Effective < MinDistance)
			{
				MinDistance = Effective;
				Best = Index;
			}
		}
	}
	if (!bSmallEnemy && bTurrets)
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

bool EnemyAIRules::MayBeDistractedByObjects(TConstArrayView<int32> OtherAttackersPerOperative, int32 AttackersPerOperativeCap)
{
	if (AttackersPerOperativeCap <= 0)
	{
		return true; // the rule is off: the Godot weighting
	}
	for (const int32 Attackers : OtherAttackersPerOperative)
	{
		if (Attackers < AttackersPerOperativeCap)
		{
			return false; // an operative still has room: he comes first
		}
	}
	return true; // every operative is busy (or there is none to go for)
}

bool EnemyAIRules::ShouldSmashBlockingObstacle(bool bTargetIsOperative, bool bTargetReachable, bool bObjectsAllowed)
{
	return !bTargetIsOperative || !bTargetReachable || bObjectsAllowed;
}

bool EnemyAIRules::PathEndReachesTarget(const FVector& PathEnd, const FVector& Target, float AttackRangeCm, float HeightSlackCm)
{
	const float Height = FMath::Max(0.f, FMath::Abs(Target.Z - PathEnd.Z) - FMath::Max(0.f, HeightSlackCm));
	return CanMelee(FMath::Sqrt(FMath::Square(FVector::Dist2D(PathEnd, Target)) + FMath::Square(Height)), Height, AttackRangeCm);
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

bool EnemyAIRules::CanMeleeOnGround(const FVector& AttackerFeet, const FVector& TargetFeet, float AttackRangeCm, bool bSharedGround,
	const FMeleeReachConfig& Config)
{
	const float Rise = TargetFeet.Z - AttackerFeet.Z;
	if (FMath::Abs(Rise) > Config.HeightCapCm)
	{
		return false;
	}
	const float Horizontal = FVector::Dist2D(AttackerFeet, TargetFeet);
	float Vertical = Rise + Config.BodyOffsetCm; // to the Godot body point
	if (bSharedGround && Rise > 0.f)
	{
		// Flat shared ground = the body point (the old rule); the steeper the shared slope, the more feet-to-feet. Uphill only
		// (G007 final review 1): downhill the body point (Rise + offset) is already nearer than the feet, so the blend would
		// shrink the reach - a target lower on the ramp keeps the old body-point reach.
		const float AngleDeg = FMath::RadiansToDegrees(FMath::Atan2(FMath::Abs(Rise), FMath::Max(Horizontal, 1.f)));
		const float Blend = Config.SlopeFullDeg > KINDA_SMALL_NUMBER ? FMath::Clamp(AngleDeg / Config.SlopeFullDeg, 0.f, 1.f) : 1.f;
		Vertical = FMath::Lerp(Rise + Config.BodyOffsetCm, Rise, Blend);
	}
	return FMath::Sqrt(FMath::Square(Horizontal) + FMath::Square(Vertical)) <= AttackRangeCm;
}

bool EnemyAIRules::CanMeleeOnGround(const FVector& AttackerFeet, const FVector& TargetFeet, float AttackRangeCm, bool bSharedGround)
{
	return CanMeleeOnGround(AttackerFeet, TargetFeet, AttackRangeCm, bSharedGround, FireGateRules::GetConfig().MeleeReach);
}

bool EnemyAIRules::CanMeleeOverProfile(const FVector& AttackerFeet, const FVector& TargetFeet, float AttackRangeCm,
	TConstArrayView<FFloorSample> Profile, const FMeleeReachConfig& Config)
{
	return CanMeleeOnGround(AttackerFeet, TargetFeet, AttackRangeCm, CombatGroundRules::IsSharedGround(Profile, Config.StepMaxCm), Config);
}

float EnemyAIRules::MeleeReachPlanarOnGround(float AttackRangeCm, float TargetFeetHeightCm, bool bSharedGround, const FMeleeReachConfig& Config)
{
	const FVector Target(0.f, 0.f, TargetFeetHeightCm);
	auto Reaches = [&](float Planar)
	{
		return CanMeleeOnGround(FVector::ZeroVector, Target + FVector(Planar, 0.f, 0.f), AttackRangeCm, bSharedGround, Config);
	};
	// The measured distance grows with the planar one (a flatter angle blends toward the body point): bisect the edge.
	float Low = 0.f;
	float High = FMath::Max(0.f, AttackRangeCm);
	if (!Reaches(Low))
	{
		return 0.f;
	}
	if (Reaches(High))
	{
		return High;
	}
	for (int32 Step = 0; Step < 24; ++Step)
	{
		const float Mid = 0.5f * (Low + High);
		if (Reaches(Mid))
		{
			Low = Mid;
		}
		else
		{
			High = Mid;
		}
	}
	return Low;
}

float EnemyAIRules::GridMeleeRange(float AttackRangeCm, float PlanarCm, const FMeleeReachConfig& Config)
{
	return FMath::Max(AttackRangeCm, FMath::Sqrt(FMath::Square(PlanarCm) + FMath::Square(Config.BodyOffsetCm)) + 1.f);
}

float EnemyAIRules::MeleeReachPlanar(float AttackRangeCm, float HeightDifferenceCm)
{
	return FMath::Sqrt(FMath::Max(0.f, FMath::Square(AttackRangeCm) - FMath::Square(HeightDifferenceCm)));
}

float EnemyAIRules::MeleeApproachAcceptance(float AttackRangeCm, float HeightDifferenceCm, float SelfRadiusCm, float GoalRadiusCm, float MarginCm)
{
	return MeleeApproachAcceptanceForReach(MeleeReachPlanar(AttackRangeCm, HeightDifferenceCm), AttackRangeCm, SelfRadiusCm, GoalRadiusCm, MarginCm);
}

float EnemyAIRules::MeleeApproachAcceptanceForReach(float PlanarReachCm, float AttackRangeCm, float SelfRadiusCm, float GoalRadiusCm, float MarginCm)
{
	const float Inside = PlanarReachCm - SelfRadiusCm - GoalRadiusCm - MarginCm;
	// Floored to 5 cm: a few cm of feet height jitter must not change the request (a new acceptance = a new MoveTo, RepathRules).
	return FMath::Clamp(FMath::FloorToFloat(Inside / 5.f) * 5.f, 0.f, FMath::Max(0.f, AttackRangeCm * 0.5f));
}

bool EnemyAIRules::FeelsHeat(float Distance2DCm, float ReachCm, bool bLineClear)
{
	return Distance2DCm < ReachCm && bLineClear;
}

FVector EnemyAIRules::FleeDirection(const FVector& Enemy, const FVector& Fire, const FVector* Target, int8 TurnDirection)
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
	if (TurnDirection < 0 || (TurnDirection == 0 && (Tangent | ToTarget) < 0.f))
	{
		Tangent = -Tangent;
	}
	return (Away * 0.7f + Tangent * 0.5f).GetSafeNormal();
}

bool EnemyAIRules::FireDetourWaypoint(const FVector& Enemy, const FVector& Fire, float AvoidRadius, const FVector& Target,
	FVector& OutWaypoint, float StepDegrees, int8 TurnDirection)
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
	float Delta = FMath::UnwindRadians(TargetAngle - EnemyAngle);
	// The assigned side holds while the target is well round the zone; once the shortest arc is small (the prey ran round to this
	// side, or the rounds moved the centre past him) he takes it instead of circling ~330 deg (047 API review).
	if (!bTargetInside && Delta * TurnDirection < 0.f && FMath::Abs(Delta) > UE_HALF_PI)
	{
		Delta += 2.f * UE_PI * TurnDirection;
	}
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
