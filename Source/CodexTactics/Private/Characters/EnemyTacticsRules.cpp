#include "Characters/EnemyTacticsRules.h"

FEnemyTacticsProfile EnemyTacticsRules::ProfileFor(EEnemyArchetype Archetype)
{
	FEnemyTacticsProfile Profile;
	switch (Archetype)
	{
	case EEnemyArchetype::FrostHound:
		// Pack hunter: the wounded and the stragglers, round the flanks, breaks off when the pack bleeds.
		Profile.WoundedWeight = 0.45f;
		Profile.IsolatedWeight = 0.35f;
		Profile.ExposedWeight = 0.1f;
		Profile.BackWeight = 0.15f;
		Profile.MaxAttackersPerTarget = 3;
		Profile.FlankShare = 0.5f;
		Profile.MoraleHealthFraction = 0.25f;
		Profile.MoraleNearbyDeaths = 3;
		break;
	case EEnemyArchetype::Cutter:
		// Ambusher: backs and open ground, mostly round the side; falls back hurt.
		Profile.WoundedWeight = 0.2f;
		Profile.IsolatedWeight = 0.2f;
		Profile.ExposedWeight = 0.25f;
		Profile.BackWeight = 0.35f;
		Profile.MaxAttackersPerTarget = 2;
		Profile.FlankShare = 0.67f;
		Profile.MoraleHealthFraction = 0.3f;
		Profile.MoraleNearbyDeaths = 0;
		break;
	case EEnemyArchetype::Frostbitten:
		// Mindless horde: the nearest body, piles on, never breaks.
		Profile.WoundedWeight = 0.f;
		Profile.IsolatedWeight = 0.f;
		Profile.ExposedWeight = 0.f;
		Profile.BackWeight = 0.f;
		Profile.MaxAttackersPerTarget = 5;
		Profile.FlankShare = 0.f;
		Profile.bHoldsGround = true;
		break;
	case EEnemyArchetype::Brute:
		// Juggernaut: straight at the strongest point, never breaks, never shares a target with many.
		Profile.WoundedWeight = 0.f;
		Profile.IsolatedWeight = 0.f;
		Profile.ExposedWeight = -0.2f; // goes for the one in cover: he smashes the barricade
		Profile.BackWeight = 0.f;
		Profile.MaxAttackersPerTarget = 1;
		Profile.FlankShare = 0.f;
		Profile.bHoldsGround = true;
		break;
	default:
		break;
	}
	return Profile;
}

float EnemyTacticsRules::TargetCost(const FEnemyTacticsProfile& Profile, const FVector& Enemy, const FEnemyTacticsTarget& Target)
{
	const float Meters = FVector::Dist2D(Enemy, Target.Location) / 100.f;
	float Preference = Profile.WoundedWeight * (1.f - FMath::Clamp(Target.HealthFraction, 0.f, 1.f))
		+ Profile.IsolatedWeight * FMath::Clamp((Target.NearestMateDistance - 300.f) / 300.f, 0.f, 1.f)
		+ Profile.ExposedWeight * (Target.bInCover ? 0.f : 1.f)
		+ Profile.BackWeight * (IsBehind(Target.Location, Target.Forward, Enemy) ? 1.f : 0.f);
	Preference = FMath::Clamp(Preference, -0.5f, 0.8f);
	// A full target is not refused (a lone survivor still gets everyone), it costs 8 m per extra attacker.
	const int32 Over = FMath::Max(0, Target.Attackers - Profile.MaxAttackersPerTarget + 1);
	const float Stick = Target.bCurrent ? 0.8f : 1.f;
	return Meters * (1.f - Preference) * Stick + Over * 8.f;
}

int32 EnemyTacticsRules::ChooseTarget(const FEnemyTacticsProfile& Profile, const FVector& Enemy, const TArray<FEnemyTacticsTarget>& Targets)
{
	int32 Best = INDEX_NONE;
	float BestCost = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Targets.Num(); ++Index)
	{
		if (!Targets[Index].bUsable)
		{
			continue;
		}
		const float Cost = TargetCost(Profile, Enemy, Targets[Index]);
		if (Cost < BestCost)
		{
			BestCost = Cost;
			Best = Index;
		}
	}
	return Best;
}

EEnemyTacticRole EnemyTacticsRules::RoleFor(const FEnemyTacticsProfile& Profile, int32 Rank, int32 AttackerCount)
{
	if (Profile.bHoldsGround || Rank <= 0 || AttackerCount < 2 || Profile.FlankShare <= 0.f)
	{
		return EEnemyTacticRole::Direct;
	}
	// The flankers are spread evenly through the ranks: with share 0.5 every second one goes round.
	const int32 Flankers = FMath::Clamp(FMath::RoundToInt(Profile.FlankShare * AttackerCount), 0, AttackerCount - 1);
	const int32 FlankersUpToHere = FMath::FloorToInt(static_cast<float>(Rank + 1) * Flankers / AttackerCount);
	const int32 FlankersBefore = FMath::FloorToInt(static_cast<float>(Rank) * Flankers / AttackerCount);
	return FlankersUpToHere > FlankersBefore ? EEnemyTacticRole::Flank : EEnemyTacticRole::Direct;
}

TArray<EEnemyTacticRole> EnemyTacticsRules::AssignRoles(const TArray<FEnemyTacticsProfile>& Profiles,
	const TArray<const EEnemyTacticRole*>& PreviousRoles)
{
	const int32 Count = Profiles.Num();
	TArray<EEnemyTacticRole> Roles;
	Roles.Init(EEnemyTacticRole::Direct, Count);
	float Share = 0.f;
	for (const FEnemyTacticsProfile& Profile : Profiles)
	{
		Share += Profile.bHoldsGround ? 0.f : Profile.FlankShare;
	}
	int32 Wanted = FMath::Clamp(FMath::RoundToInt(Share), 0, FMath::Max(Count - 1, 0));
	auto CanFlank = [&Profiles](int32 Index) { return !Profiles[Index].bHoldsGround && Profiles[Index].FlankShare > 0.f; };
	// 1. Returning flankers keep going round (farthest first).
	for (int32 Index = Count - 1; Index >= 0 && Wanted > 0; --Index)
	{
		if (CanFlank(Index) && PreviousRoles.IsValidIndex(Index) && PreviousRoles[Index] && *PreviousRoles[Index] == EEnemyTacticRole::Flank)
		{
			Roles[Index] = EEnemyTacticRole::Flank;
			--Wanted;
		}
	}
	// 2. Newcomers, then (if still short) returning direct attackers, farthest first.
	for (int32 Pass = 0; Pass < 2 && Wanted > 0; ++Pass)
	{
		for (int32 Index = Count - 1; Index >= 0 && Wanted > 0; --Index)
		{
			const bool bNew = !PreviousRoles.IsValidIndex(Index) || !PreviousRoles[Index];
			if (Roles[Index] == EEnemyTacticRole::Direct && CanFlank(Index) && (Pass == 1 || bNew))
			{
				Roles[Index] = EEnemyTacticRole::Flank;
				--Wanted;
			}
		}
	}
	return Roles;
}

FVector EnemyTacticsRules::FlankPoint(const FVector& Enemy, const FVector& Target, const FVector& SquadCentre, float DistanceCm,
	float AngleDegrees)
{
	FVector Axis = FVector(Target.X - SquadCentre.X, Target.Y - SquadCentre.Y, 0.f).GetSafeNormal();
	if (Axis.IsNearlyZero())
	{
		// A lone target: his side, seen from the enemy.
		Axis = FVector(Target.X - Enemy.X, Target.Y - Enemy.Y, 0.f).GetSafeNormal();
		if (Axis.IsNearlyZero())
		{
			Axis = FVector::ForwardVector;
		}
	}
	const FVector ToEnemy = FVector(Enemy.X - Target.X, Enemy.Y - Target.Y, 0.f);
	const float Side = FVector::CrossProduct(Axis, ToEnemy).Z >= 0.f ? 1.f : -1.f;
	const FVector Direction = Axis.RotateAngleAxis(Side * AngleDegrees, FVector::UpVector);
	return FVector(Target.X, Target.Y, Target.Z) + Direction * DistanceCm;
}

bool EnemyTacticsRules::IsBehind(const FVector& TargetLocation, const FVector& TargetForward, const FVector& Enemy)
{
	const FVector Facing = FVector(TargetForward.X, TargetForward.Y, 0.f).GetSafeNormal();
	const FVector ToEnemy = FVector(Enemy.X - TargetLocation.X, Enemy.Y - TargetLocation.Y, 0.f).GetSafeNormal();
	return !Facing.IsNearlyZero() && !ToEnemy.IsNearlyZero() && FVector::DotProduct(Facing, ToEnemy) < 0.5f; // outside +-60 deg
}

bool EnemyTacticsRules::ShouldFallBack(const FEnemyTacticsProfile& Profile, float HealthFraction, int32 RecentNearbyDeaths)
{
	if (Profile.bHoldsGround)
	{
		return false;
	}
	return (Profile.MoraleHealthFraction > 0.f && HealthFraction < Profile.MoraleHealthFraction)
		|| (Profile.MoraleNearbyDeaths > 0 && RecentNearbyDeaths >= Profile.MoraleNearbyDeaths);
}

FVector EnemyTacticsRules::FallBackPoint(const FVector& Enemy, const FVector& SquadCentre, const FVector* PackCentre, float DistanceCm)
{
	FVector Away = FVector(Enemy.X - SquadCentre.X, Enemy.Y - SquadCentre.Y, 0.f).GetSafeNormal();
	if (Away.IsNearlyZero())
	{
		Away = FVector::ForwardVector;
	}
	if (PackCentre)
	{
		const FVector ToPack = FVector(PackCentre->X - Enemy.X, PackCentre->Y - Enemy.Y, 0.f).GetSafeNormal();
		// Towards the pack only while that still leads away from the squad.
		if (FVector::DotProduct(ToPack, Away) > 0.f)
		{
			Away = (Away + ToPack).GetSafeNormal();
		}
	}
	return Enemy + Away * DistanceCm;
}
