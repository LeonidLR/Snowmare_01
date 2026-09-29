#include "Combat/GrenadeRules.h"

float GrenadeRules::EffectiveRange(float BaseRange, EOperativeStance Stance)
{
	switch (Stance)
	{
	case EOperativeStance::Crouching: return BaseRange * 0.75f;
	case EOperativeStance::Prone: return BaseRange * 0.5f;
	default: return BaseRange;
	}
}

float GrenadeRules::DamageFalloff(float Distance, float Radius)
{
	return FMath::Clamp(1.f - (Distance / FMath::Max(Radius, 0.01f)) * 0.5f, 0.5f, 1.f);
}

FVector GrenadeRules::ClampTarget(const FVector& From, const FVector& Target, float MaxRange)
{
	const FVector Flat(Target.X - From.X, Target.Y - From.Y, 0.f);
	const float Distance = Flat.Size();
	if (Distance <= MaxRange || Distance < 0.001f)
	{
		return Target;
	}
	const FVector Clamped = Flat / Distance * MaxRange;
	return FVector(From.X + Clamped.X, From.Y + Clamped.Y, Target.Z);
}

float GrenadeRules::FlightDuration(float FlatDistance)
{
	return FMath::Max(0.12f, FlatDistance / ThrowSpeed);
}

FVector GrenadeRules::ArcPoint(const FVector& Start, const FVector& End, float Progress)
{
	const float T = FMath::Clamp(Progress, 0.f, 1.f);
	FVector Point = FMath::Lerp(Start, End, T);
	Point.Z += FMath::Sin(T * PI) * ArcHeight;
	return Point;
}
