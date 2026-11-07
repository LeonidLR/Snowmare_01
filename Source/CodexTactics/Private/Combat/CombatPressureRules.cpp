#include "Combat/CombatPressureRules.h"

bool CombatPressureRules::IsAllowedType(EEnemyArchetype Type)
{
	switch (Type)
	{
	case EEnemyArchetype::FrostHound:
	case EEnemyArchetype::Frostbitten:
	case EEnemyArchetype::Cutter:
	case EEnemyArchetype::Marksman:
		return true;
	default:
		return false;
	}
}

FName CombatPressureRules::MilitaryZombieVariant()
{
	static const FName Name(TEXT("MilitaryZombie"));
	return Name;
}

bool CombatPressureRules::IsAllowedEnemy(const FPressureEnemyType& Enemy)
{
	if (!IsAllowedType(Enemy.Type))
	{
		return false;
	}
	return Enemy.Variant.IsNone() || (Enemy.Variant == MilitaryZombieVariant() && Enemy.Type == EEnemyArchetype::Frostbitten);
}

bool CombatPressureRules::ParseVariantKey(const FString& Key, FPressureEnemyType& OutEnemy)
{
	if (Key.Equals(TEXT("MILITARY_ZOMBIE"), ESearchCase::IgnoreCase))
	{
		OutEnemy = FPressureEnemyType(EEnemyArchetype::Frostbitten, MilitaryZombieVariant());
		return true;
	}
	return false;
}

FString CombatPressureRules::EnemyLabel(const FPressureEnemyType& Enemy)
{
	if (Enemy.Variant == MilitaryZombieVariant())
	{
		return TEXT("MILITARY_ZOMBIE");
	}
	const FString Name = UEnum::GetValueAsString(Enemy.Type);
	return Enemy.Variant.IsNone() ? Name : FString::Printf(TEXT("%s (%s)"), *Name, *Enemy.Variant.ToString());
}

TArray<FPressureTypeWeight> CombatPressureRules::EffectiveWeights(const TArray<FPressureTypeWeight>& Weights)
{
	TArray<FPressureTypeWeight> Out;
	for (const FPressureTypeWeight& Each : Weights)
	{
		if (IsAllowedEnemy(FPressureEnemyType(Each.Type, Each.Variant)) && Each.Weight > 0.f)
		{
			Out.Add(Each);
		}
	}
	if (Out.IsEmpty())
	{
		// Task #34 (2026-11-07): the Military Zombie takes the Spitter's share of the default pool.
		Out.Add({ EEnemyArchetype::FrostHound, 1.f });
		Out.Add({ EEnemyArchetype::Frostbitten, 1.f });
		Out.Add({ EEnemyArchetype::Frostbitten, 1.f, MilitaryZombieVariant() });
		Out.Add({ EEnemyArchetype::Cutter, 1.f });
		Out.Add({ EEnemyArchetype::Marksman, 1.f });
	}
	return Out;
}

EPressureEventDecision CombatPressureRules::DecideEvent(float NowSeconds, float DueAtSeconds, int32 Alive, int32 MaxAlive)
{
	if (NowSeconds < DueAtSeconds)
	{
		return EPressureEventDecision::NotDue;
	}
	return Alive < FMath::Max(1, MaxAlive) ? EPressureEventDecision::Spawn : EPressureEventDecision::DelayedCapFull;
}

int32 CombatPressureRules::GroupSize(float Rand01, int32 Min, int32 Max, int32 Alive, int32 MaxAlive)
{
	const int32 Low = FMath::Max(1, Min);
	const int32 High = FMath::Max(Low, Max);
	const int32 Size = FMath::Clamp(Low + FMath::FloorToInt(FMath::Clamp(Rand01, 0.f, 0.9999f) * static_cast<float>(High - Low + 1)), Low, High);
	return FMath::Clamp(Size, 0, FMath::Max(0, MaxAlive - Alive));
}

EPressureSpawnKind CombatPressureRules::PickKind(float Rand01, float PatrolShare)
{
	return Rand01 < FMath::Clamp(PatrolShare, 0.f, 1.f) ? EPressureSpawnKind::Patrol : EPressureSpawnKind::Ambush;
}

EPressureSpawnKind CombatPressureRules::PickEventKind(float Rand01, float RandCoordinated01, float PatrolShare, float CoordinatedShare)
{
	if (PickKind(Rand01, PatrolShare) == EPressureSpawnKind::Patrol)
	{
		return EPressureSpawnKind::Patrol;
	}
	return RandCoordinated01 < FMath::Clamp(CoordinatedShare, 0.f, 1.f) ? EPressureSpawnKind::Coordinated : EPressureSpawnKind::Ambush;
}

EEnemyArchetype CombatPressureRules::PickType(const TArray<FPressureTypeWeight>& Weights, float Rand01)
{
	return PickEnemy(Weights, Rand01).Type;
}

int32 CombatPressureRules::EnsureRangedEnemies(TArray<FPressureEnemyType>& Enemies, const FPressureEnemyType& Ranged, int32 MinRanged)
{
	TArray<EEnemyArchetype> Types;
	for (const FPressureEnemyType& Each : Enemies)
	{
		Types.Add(Each.Type);
	}
	const int32 Swapped = AmbushPlanRules::EnsureRanged(Types, Ranged.Type, MinRanged);
	for (int32 Index = 0; Index < Enemies.Num(); ++Index)
	{
		if (Types[Index] != Enemies[Index].Type)
		{
			Enemies[Index] = Ranged;
		}
	}
	return Swapped;
}

FPressureEnemyType CombatPressureRules::PickEnemy(const TArray<FPressureTypeWeight>& Weights, float Rand01)
{
	const TArray<FPressureTypeWeight> Effective = EffectiveWeights(Weights);
	float Total = 0.f;
	for (const FPressureTypeWeight& Each : Effective)
	{
		Total += Each.Weight;
	}
	float Pick = FMath::Clamp(Rand01, 0.f, 0.9999f) * Total;
	for (const FPressureTypeWeight& Each : Effective)
	{
		if (Pick < Each.Weight)
		{
			return FPressureEnemyType(Each.Type, Each.Variant);
		}
		Pick -= Each.Weight;
	}
	return FPressureEnemyType(Effective.Last().Type, Effective.Last().Variant);
}

float CombatPressureRules::DistanceToBox(const FVector2D& Point, const FPressureBox& Box)
{
	const FVector2D AxisX = Box.AxisX.GetSafeNormal();
	const FVector2D AxisY(-AxisX.Y, AxisX.X);
	const FVector2D Local(FVector2D::DotProduct(Point - Box.Center, AxisX), FVector2D::DotProduct(Point - Box.Center, AxisY));
	const double Dx = FMath::Max(0.0, FMath::Abs(Local.X) - Box.HalfSize.X);
	const double Dy = FMath::Max(0.0, FMath::Abs(Local.Y) - Box.HalfSize.Y);
	return static_cast<float>(FMath::Sqrt(Dx * Dx + Dy * Dy));
}

float CombatPressureRules::Clearance(const FVector2D& Point, const TArray<FPressureBox>& Boxes)
{
	float Best = MAX_flt;
	for (const FPressureBox& Box : Boxes)
	{
		Best = FMath::Min(Best, DistanceToBox(Point, Box));
	}
	return Best;
}

ESpawnPointVerdict CombatPressureRules::CheckSpawnPoint(const FVector2D& Point, const TArray<FPressureBox>& Boxes, float ClearanceCm,
	const TArray<FVector2D>& Squad, float MinSquadDistanceCm, const FBox2D& Bounds)
{
	if (Bounds.bIsValid && !Bounds.IsInside(Point))
	{
		return ESpawnPointVerdict::OutOfBounds;
	}
	if (Clearance(Point, Boxes) < ClearanceCm)
	{
		return ESpawnPointVerdict::NearWall;
	}
	if (AmbushWatchRules::NearestDistance(Squad, Point) < MinSquadDistanceCm)
	{
		return ESpawnPointVerdict::NearSquad;
	}
	return ESpawnPointVerdict::Ok;
}

float CombatPressureRules::ScoreSpot(const FPressureSpotInput& Input)
{
	const float Band = FMath::Max(1.f, Input.MaxDistanceCm - Input.MinDistanceCm);
	const float Mid = (Input.MinDistanceCm + Input.MaxDistanceCm) * 0.5f;
	const float OffBand = Input.DistanceCm < Input.MinDistanceCm || Input.DistanceCm > Input.MaxDistanceCm ? 1.f : 0.f;
	const float FromMid = FMath::Abs(Input.DistanceCm - Mid) / Band; // 0 in the middle, 0.5 at the band's edges
	if (Input.Kind == EPressureSpawnKind::Ambush)
	{
		if (!Input.bConcealed)
		{
			return -1000.f;
		}
		return 2.f * Input.AheadCos - FromMid - 3.f * OffBand + Input.KindBonus;
	}
	return Input.AheadCos * 0.75f + (Input.bConcealed ? 1.f : 0.f) - (Input.bWatched ? 0.5f : 0.f) - 0.5f * FromMid - 3.f * OffBand;
}

TArray<FVector2D> CombatPressureRules::PatrolLoop(const FVector2D& Center, float RadiusCm, int32 Points, float PhaseDeg)
{
	TArray<FVector2D> Out;
	const int32 Count = FMath::Max(2, Points);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float Angle = FMath::DegreesToRadians(PhaseDeg + 360.f * static_cast<float>(Index) / static_cast<float>(Count));
		Out.Add(Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * RadiusCm);
	}
	return Out;
}

FVector2D CombatPressureRules::GroupOffset(int32 Index, float SpacingCm)
{
	if (Index <= 0)
	{
		return FVector2D::ZeroVector;
	}
	const float Angle = FMath::DegreesToRadians(150.f + 120.f * static_cast<float>(Index - 1));
	const float Ring = SpacingCm * static_cast<float>(1 + (Index - 1) / 3);
	return FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Ring;
}

bool CombatPressureRules::ShouldRecycle(bool bEngaged, bool bVisibleToSquad, float NearestSquadCm, float FarSeconds, const FCombatPressureConfig& Config)
{
	return !bEngaged && !bVisibleToSquad && NearestSquadCm > Config.RecycleDistanceCm && FarSeconds >= Config.RecycleAfterSeconds;
}
