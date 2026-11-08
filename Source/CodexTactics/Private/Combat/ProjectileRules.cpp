#include "Combat/ProjectileRules.h"

float FProjectileFlightState::FlightSeconds() const
{
	return Steps * ProjectileRules::StepSeconds;
}

namespace ProjectileRules
{
	bool SolveLaunch(const FVector& From, const FVector& Target, float SpeedCmS, float GravityZ, EProjectileTrajectory Trajectory, FVector& OutVelocity)
	{
		OutVelocity = FVector::ZeroVector;
		const FVector Delta = Target - From;
		if (SpeedCmS <= 0.f || Delta.IsNearlyZero())
		{
			return false;
		}
		const double G = -static_cast<double>(GravityZ);
		if (Trajectory == EProjectileTrajectory::Flat || G <= UE_KINDA_SMALL_NUMBER)
		{
			OutVelocity = Delta.GetSafeNormal() * SpeedCmS;
			return true;
		}
		const FVector Flat(Delta.X, Delta.Y, 0.f);
		const double X = Flat.Size();
		const double Y = Delta.Z;
		const FVector FlatDir = X > UE_KINDA_SMALL_NUMBER ? Flat / X : FVector::ForwardVector;
		if (X <= UE_KINDA_SMALL_NUMBER)
		{
			// Straight up / down: the flat answer (a plunging round dropped on its own spot is not a launch).
			OutVelocity = Delta.GetSafeNormal() * SpeedCmS;
			return Y > 0.0 ? (static_cast<double>(SpeedCmS) * SpeedCmS >= 2.0 * G * Y) : true;
		}
		const double V2 = static_cast<double>(SpeedCmS) * SpeedCmS;
		const double Disc = V2 * V2 - G * (G * X * X + 2.0 * Y * V2);
		if (Disc < 0.0)
		{
			const double Angle = UE_DOUBLE_PI / 4.0;
			OutVelocity = (FlatDir * FMath::Cos(Angle) + FVector::UpVector * FMath::Sin(Angle)) * SpeedCmS;
			return false;
		}
		const double Root = FMath::Sqrt(Disc);
		const double TanTheta = (V2 + (Trajectory == EProjectileTrajectory::HighAngle ? Root : -Root)) / (G * X);
		const double Theta = FMath::Atan(TanTheta);
		OutVelocity = (FlatDir * FMath::Cos(Theta) + FVector::UpVector * FMath::Sin(Theta)) * SpeedCmS;
		return true;
	}

	float FlightTimeTo(const FVector& Velocity, float HorizontalCm)
	{
		const float Horizontal = static_cast<float>(FVector(Velocity.X, Velocity.Y, 0.f).Size());
		return Horizontal > UE_KINDA_SMALL_NUMBER ? HorizontalCm / Horizontal : 0.f;
	}

	float GravityFor(const FWeaponProjectileStats& Stats, float WorldGravityZ)
	{
		return Stats.Trajectory == EProjectileTrajectory::Flat ? 0.f : WorldGravityZ * FMath::Max(0.f, Stats.GravityScale);
	}

	float MaxFlightSeconds(const FWeaponProjectileStats& Stats)
	{
		return Stats.MaxFlightSeconds > 0.f ? Stats.MaxFlightSeconds : DefaultMaxFlightSeconds;
	}

	void Step(FProjectileFlightState& State)
	{
		const float Dt = StepSeconds;
		const FVector Gravity(0.f, 0.f, State.GravityZ);
		const FVector Next = State.Position + State.Velocity * Dt + Gravity * (0.5f * Dt * Dt);
		State.DistanceCm += static_cast<float>(FVector::Dist(State.Position, Next));
		State.Position = Next;
		State.Velocity += Gravity * Dt;
		++State.Steps;
	}

	bool Advance(FProjectileFlightState& State, float DeltaSeconds, TFunctionRef<bool(const FVector& From, const FVector& To, int32 StepIndex)> OnSegment)
	{
		State.Accumulator += FMath::Max(0.f, DeltaSeconds);
		int32 Flown = 0;
		// A tiny tolerance so frame times that add up to whole steps (1/60 = 2 steps) do not lose a step to rounding.
		while (State.Accumulator + 1e-9 >= StepSeconds && Flown < MaxStepsPerAdvance)
		{
			State.Accumulator -= StepSeconds;
			const FVector From = State.Position;
			Step(State);
			++Flown;
			if (OnSegment(From, State.Position, State.Steps))
			{
				State.Accumulator = 0.0;
				return true;
			}
		}
		if (Flown >= MaxStepsPerAdvance)
		{
			State.Accumulator = FMath::Min(State.Accumulator, static_cast<double>(StepSeconds));
		}
		return false;
	}

	bool IsArmed(const FProjectileFlightState& State, float ArmingDistanceCm)
	{
		return State.DistanceCm >= FMath::Max(0.f, ArmingDistanceCm);
	}

	bool IsWithinFireBand(float HorizontalCm, float MinCm, float MaxCm)
	{
		return HorizontalCm >= FMath::Max(0.f, MinCm) && (MaxCm <= 0.f || HorizontalCm <= MaxCm);
	}
}
