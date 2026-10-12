#include "Vehicles/VehicleRules.h"

namespace VehicleRules
{
	int32 GetCapacity(const FVehicleCrewConfig& Config)
	{
		return 2 + FMath::Max(0, Config.PassengerSeats);
	}

	TArray<EVehicleSeat> AssignSeats(const TArray<EOperativeRole>& Crew, const FVehicleCrewConfig& Config)
	{
		TArray<EVehicleSeat> Seats;
		Seats.Init(EVehicleSeat::None, Crew.Num());
		if (Crew.IsEmpty())
		{
			return Seats;
		}
		const int32 Capacity = GetCapacity(Config);
		// Driver: the Engineer, else the first one aboard who is not the Commander, else the Commander (a crew of one drives).
		int32 Driver = Crew.IndexOfByKey(EOperativeRole::Engineer);
		if (Driver == INDEX_NONE)
		{
			Driver = Crew.IndexOfByPredicate([](EOperativeRole Role) { return Role != EOperativeRole::Commander; });
		}
		if (Driver == INDEX_NONE)
		{
			Driver = 0;
		}
		Seats[Driver] = EVehicleSeat::Driver;
		// Gunner: the Commander, else the next one aboard.
		int32 Gunner = INDEX_NONE;
		for (int32 Index = 0; Index < Crew.Num(); ++Index)
		{
			if (Index != Driver && Crew[Index] == EOperativeRole::Commander)
			{
				Gunner = Index;
				break;
			}
		}
		if (Gunner == INDEX_NONE)
		{
			for (int32 Index = 0; Index < Crew.Num(); ++Index)
			{
				if (Index != Driver)
				{
					Gunner = Index;
					break;
				}
			}
		}
		if (Gunner != INDEX_NONE)
		{
			Seats[Gunner] = EVehicleSeat::Gunner;
		}
		int32 Taken = Gunner != INDEX_NONE ? 2 : 1;
		for (int32 Index = 0; Index < Crew.Num(); ++Index)
		{
			if (Seats[Index] == EVehicleSeat::None && Taken < Capacity)
			{
				Seats[Index] = EVehicleSeat::Passenger;
				++Taken;
			}
		}
		return Seats;
	}

	EVehicleRefusal CanBoard(const FBoardCheck& Check, const FVehicleCrewConfig& Config)
	{
		if (!Check.bAlive)
		{
			return EVehicleRefusal::Dead;
		}
		if (Check.bMounted)
		{
			return EVehicleRefusal::AlreadyMounted;
		}
		if (Check.bTurnBased)
		{
			return EVehicleRefusal::TurnBased;
		}
		if (Check.bTacticalPause)
		{
			return EVehicleRefusal::TacticalPause;
		}
		if (Check.bKnockedDown)
		{
			return EVehicleRefusal::KnockedDown;
		}
		if (Check.DistanceCm > Config.BoardRangeCm)
		{
			return EVehicleRefusal::TooFar;
		}
		if (Check.FreeSeats <= 0)
		{
			return EVehicleRefusal::NoSeat;
		}
		if (FMath::Abs(Check.VehicleSpeedCmS) > Config.StoppedSpeedCmS)
		{
			return EVehicleRefusal::VehicleMoving;
		}
		return EVehicleRefusal::None;
	}

	EVehicleRefusal CanDisembark(bool bMounted, bool bTurnBased, bool bTacticalPause, bool bHasFreeExit)
	{
		if (!bMounted)
		{
			return EVehicleRefusal::NotMounted;
		}
		if (bTurnBased)
		{
			return EVehicleRefusal::TurnBased;
		}
		if (bTacticalPause)
		{
			return EVehicleRefusal::TacticalPause;
		}
		return bHasFreeExit ? EVehicleRefusal::None : EVehicleRefusal::NoExit;
	}

	EVehicleRefusal CanDrive(bool bHasDriver, bool bTurnBased, bool bTacticalPause)
	{
		if (bTurnBased)
		{
			return EVehicleRefusal::TurnBased;
		}
		if (bTacticalPause)
		{
			return EVehicleRefusal::TacticalPause;
		}
		return bHasDriver ? EVehicleRefusal::None : EVehicleRefusal::NoDriver;
	}

	EVehicleKeyAction ResolveKeyAction(int32 AddressedCount, int32 MountedCount)
	{
		if (AddressedCount <= 0)
		{
			return EVehicleKeyAction::None;
		}
		return MountedCount >= AddressedCount ? EVehicleKeyAction::Disembark : EVehicleKeyAction::Board;
	}

	TArray<int32> RankExitPoints(const TArray<FVector>& Candidates, const TArray<bool>& bFree, const FVector& VehicleCentre,
		const TArray<FVector>& Threats, const FVector& ViewPoint)
	{
		// Nearest threat (2D) to the vehicle.
		const FVector* Nearest = nullptr;
		float NearestDistance = TNumericLimits<float>::Max();
		for (const FVector& Threat : Threats)
		{
			const float Distance = FVector::Dist2D(Threat, VehicleCentre);
			if (Distance < NearestDistance)
			{
				NearestDistance = Distance;
				Nearest = &Threat;
			}
		}
		const FVector ThreatDirection = Nearest ? (*Nearest - VehicleCentre).GetSafeNormal2D() : FVector::ZeroVector;
		TArray<TPair<float, int32>> Scored;
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			if (!bFree.IsValidIndex(Index) || !bFree[Index])
			{
				continue;
			}
			float Score = 0.f;
			if (Nearest)
			{
				// Lower = further round the hull from the threat; the distance to the threat breaks near ties.
				Score = FVector::DotProduct((Candidates[Index] - VehicleCentre).GetSafeNormal2D(), ThreatDirection) * 10000.f
					- FVector::Dist2D(Candidates[Index], *Nearest) * 0.01f;
			}
			else
			{
				Score = FVector::Dist2D(Candidates[Index], ViewPoint);
			}
			Scored.Emplace(Score, Index);
		}
		Scored.StableSort([](const TPair<float, int32>& A, const TPair<float, int32>& B) { return A.Key < B.Key; });
		TArray<int32> Ranked;
		for (const TPair<float, int32>& Entry : Scored)
		{
			Ranked.Add(Entry.Value);
		}
		return Ranked;
	}

	float GetMaxYawRateDeg(float SpeedCmS, float MinTurnRadiusCm)
	{
		return MinTurnRadiusCm > 0.f ? FMath::RadiansToDegrees(FMath::Abs(SpeedCmS) / MinTurnRadiusCm) : 0.f;
	}

	float StepYaw(float CurrentYawDeg, float DesiredYawDeg, float SpeedCmS, float MinTurnRadiusCm, float DeltaSeconds)
	{
		const float Delta = FMath::FindDeltaAngleDegrees(CurrentYawDeg, DesiredYawDeg);
		const float MaxStep = GetMaxYawRateDeg(SpeedCmS, MinTurnRadiusCm) * FMath::Max(0.f, DeltaSeconds);
		return FRotator::NormalizeAxis(CurrentYawDeg + FMath::Clamp(Delta, -MaxStep, MaxStep));
	}

	bool ShouldReverse(const FVector2D& Forward, const FVector2D& ToGoal, const FVehicleDriveConfig& Config)
	{
		const float Distance = ToGoal.Size();
		if (Distance <= KINDA_SMALL_NUMBER || Distance > Config.ReverseMaxDistanceCm || Forward.IsNearlyZero())
		{
			return false;
		}
		const float Cos = FVector2D::DotProduct(Forward.GetSafeNormal(), ToGoal / Distance);
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Cos, -1.f, 1.f))) >= Config.ReverseMinAngleDeg;
	}

	float StepSpeed(float CurrentCmS, float TargetCmS, const FVehicleDriveConfig& Config, float DeltaSeconds)
	{
		const bool bBraking = FMath::Abs(TargetCmS) < FMath::Abs(CurrentCmS) || (CurrentCmS * TargetCmS < 0.f);
		const float Rate = (bBraking ? Config.BrakingCmS2 : Config.AccelerationCmS2) * FMath::Max(0.f, DeltaSeconds);
		return FMath::Abs(TargetCmS - CurrentCmS) <= Rate ? TargetCmS : CurrentCmS + FMath::Sign(TargetCmS - CurrentCmS) * Rate;
	}

	float GetDesiredSpeed(bool bRush, bool bReverse, float RemainingCm, float HeadingErrorDeg, const FVehicleDriveConfig& Config)
	{
		float Speed = bReverse ? Config.ReverseSpeedCmS : (bRush ? Config.RushSpeedCmS : Config.CruiseSpeedCmS);
		if (FMath::Abs(HeadingErrorDeg) > Config.TurnSlowdownAngleDeg)
		{
			Speed = FMath::Min(Speed, Config.MinTurnSpeedCmS);
		}
		// Stop at the goal: never faster than the braking distance allows (a crawl floor keeps it creeping in).
		const float BrakeCap = FMath::Sqrt(2.f * Config.BrakingCmS2 * FMath::Max(0.f, RemainingCm));
		return FMath::Min(Speed, FMath::Max(BrakeCap, 60.f));
	}

	FVector GetLookaheadPoint(const TArray<FVector>& Path, const FVector& Location, int32& InOutSegment, float LookaheadCm, float& OutRemainingCm)
	{
		OutRemainingCm = 0.f;
		if (Path.Num() == 0)
		{
			return Location;
		}
		if (Path.Num() == 1)
		{
			OutRemainingCm = FVector::Dist2D(Location, Path[0]);
			return Path[0];
		}
		InOutSegment = FMath::Clamp(InOutSegment, 0, Path.Num() - 2);
		// Advance to the closest segment ahead (projection beyond a segment's end moves on).
		auto Project = [&](int32 Segment, float& OutT)
		{
			const FVector2D A(Path[Segment]);
			const FVector2D B(Path[Segment + 1]);
			const FVector2D AB = B - A;
			const float Length2 = AB.SizeSquared();
			OutT = Length2 > KINDA_SMALL_NUMBER ? FMath::Clamp(FVector2D::DotProduct(FVector2D(Location) - A, AB) / Length2, 0.f, 1.f) : 1.f;
			return FVector2D::Distance(FVector2D(Location), A + AB * OutT);
		};
		float T = 0.f;
		float Best = Project(InOutSegment, T);
		while (InOutSegment < Path.Num() - 2)
		{
			float NextT = 0.f;
			const float Next = Project(InOutSegment + 1, NextT);
			if (T >= 1.f - KINDA_SMALL_NUMBER || Next <= Best)
			{
				++InOutSegment;
				Best = Next;
				T = NextT;
				continue;
			}
			break;
		}
		// Remaining length from the projection.
		const FVector Start = FMath::Lerp(Path[InOutSegment], Path[InOutSegment + 1], T);
		OutRemainingCm = FVector::Dist2D(Start, Path[InOutSegment + 1]);
		for (int32 Index = InOutSegment + 1; Index < Path.Num() - 1; ++Index)
		{
			OutRemainingCm += FVector::Dist2D(Path[Index], Path[Index + 1]);
		}
		// Walk LookaheadCm along the path. Past a real corner (> CornerAngleDeg) only CornerCarryCm more: a long hull must
		// not cut the inside of a corner round a wall end (the navmesh erosion models its width, not its length).
		constexpr float CornerAngleDeg = 25.f;
		constexpr float CornerCarryCm = 100.f;
		float Left = LookaheadCm;
		FVector Point = Start;
		for (int32 Index = InOutSegment; Index < Path.Num() - 1; ++Index)
		{
			const FVector SegmentStart = Index == InOutSegment ? Start : Path[Index];
			const float Length = FVector::Dist2D(SegmentStart, Path[Index + 1]);
			if (Length >= Left && Length > KINDA_SMALL_NUMBER)
			{
				return FMath::Lerp(SegmentStart, Path[Index + 1], Left / Length);
			}
			Left -= Length;
			Point = Path[Index + 1];
			if (Index + 2 < Path.Num())
			{
				const FVector2D In = FVector2D(Path[Index + 1] - Path[Index]).GetSafeNormal();
				const FVector2D Out = FVector2D(Path[Index + 2] - Path[Index + 1]).GetSafeNormal();
				if (FVector2D::DotProduct(In, Out) < FMath::Cos(FMath::DegreesToRadians(CornerAngleDeg)))
				{
					Left = FMath::Min(Left, CornerCarryCm);
				}
			}
		}
		return Point;
	}

	bool GetHullSideFacing(const FVector2D& LocalClick, const FVector2D& LocalViewDir, const FVector2D& HalfExtent,
		FVector2D& OutLocalPoint, FVector2D& OutLocalNormal)
	{
		const FVector2D Back = -LocalViewDir.GetSafeNormal();
		if (Back.IsNearlyZero())
		{
			return false;
		}
		// Clamp the click into the box, then march out along Back to the first side it leaves through (slab method).
		const FVector2D P(FMath::Clamp(LocalClick.X, -HalfExtent.X, HalfExtent.X), FMath::Clamp(LocalClick.Y, -HalfExtent.Y, HalfExtent.Y));
		float TX = TNumericLimits<float>::Max();
		float TY = TNumericLimits<float>::Max();
		if (!FMath::IsNearlyZero(Back.X))
		{
			TX = ((Back.X > 0.f ? HalfExtent.X : -HalfExtent.X) - P.X) / Back.X;
		}
		if (!FMath::IsNearlyZero(Back.Y))
		{
			TY = ((Back.Y > 0.f ? HalfExtent.Y : -HalfExtent.Y) - P.Y) / Back.Y;
		}
		if (TX <= TY)
		{
			OutLocalPoint = P + Back * TX;
			OutLocalNormal = FVector2D(Back.X > 0.f ? 1.f : -1.f, 0.f);
		}
		else
		{
			OutLocalPoint = P + Back * TY;
			OutLocalNormal = FVector2D(0.f, Back.Y > 0.f ? 1.f : -1.f);
		}
		return true;
	}

	EVehicleEngineState GetEngineState(bool bRunning, float SpeedCmS, bool bRush)
	{
		if (!bRunning)
		{
			return EVehicleEngineState::Off;
		}
		if (FMath::Abs(SpeedCmS) < 50.f)
		{
			return EVehicleEngineState::Idle;
		}
		return bRush ? EVehicleEngineState::Rush : EVehicleEngineState::Cruise;
	}

	float GetNoiseRadiusCm(EVehicleEngineState State, const FVehicleEngineConfig& Config)
	{
		switch (State)
		{
		case EVehicleEngineState::Idle: return Config.IdleNoiseRadiusCm;
		case EVehicleEngineState::Cruise: return Config.CruiseNoiseRadiusCm;
		case EVehicleEngineState::Rush: return Config.RushNoiseRadiusCm;
		default: return 0.f;
		}
	}

	float GetWheelSpinDegPerSec(float SpeedCmS, float WheelRadiusCm)
	{
		return WheelRadiusCm > 0.f ? FMath::RadiansToDegrees(SpeedCmS / WheelRadiusCm) : 0.f;
	}

	float GetAnimWheelSpeed(float GroundSpeedCmS, float WheelRadiusCm, float Sign)
	{
		if (FMath::Abs(GroundSpeedCmS) < 1.f)
		{
			return 0.f;
		}
		return GetWheelSpinDegPerSec(GroundSpeedCmS, WheelRadiusCm) * Sign;
	}

	float StepWheelAngle(float WheelAngleDeg, float SpinDegPerSec, float DeltaSeconds)
	{
		const float Next = FMath::Fmod(WheelAngleDeg + SpinDegPerSec * FMath::Max(0.f, DeltaSeconds), 360.f);
		return Next < 0.f ? Next + 360.f : Next;
	}

	float GetAnimWheelSpeedForAngle(float WheelAngleDeg, float OffsetHold, float DeltaSeconds)
	{
		const float Clock = FMath::Max(0.01f, OffsetHold + FMath::Max(0.f, DeltaSeconds));
		return WheelAngleDeg / Clock;
	}

	float GetLaunchAccelerationFactor(float SecondsSinceLaunch, const FVehicleDriveConfig& Config)
	{
		const float Floor = FMath::Clamp(Config.LaunchAccelerationFraction, 0.05f, 1.f);
		if (Config.AccelerationRampSeconds <= 0.f)
		{
			return 1.f;
		}
		return FMath::Lerp(Floor, 1.f, FMath::Clamp(SecondsSinceLaunch / Config.AccelerationRampSeconds, 0.f, 1.f));
	}

	float GetSteerAngleDeg(float YawRateDegPerSec, float SpeedCmS, const FVehicleDriveConfig& Config)
	{
		if (FMath::Abs(SpeedCmS) < 1.f)
		{
			return 0.f;
		}
		// Bicycle model: tan(steer) = wheelbase * yaw rate / v (reverse flips the sign of the speed, so the wheels too).
		const float Steer = FMath::RadiansToDegrees(FMath::Atan(Config.WheelbaseCm * FMath::DegreesToRadians(YawRateDegPerSec) / SpeedCmS));
		return FMath::Clamp(Steer, -Config.MaxSteerAngleDeg, Config.MaxSteerAngleDeg);
	}

	FMountedOperativeEffects GetMountedEffects(bool bEngineRunning)
	{
		FMountedOperativeEffects Effects;
		Effects.bColdDrain = !bEngineRunning;
		return Effects;
	}

	FString GetSeatName(EVehicleSeat Seat)
	{
		switch (Seat)
		{
		case EVehicleSeat::Driver: return TEXT("Driver");
		case EVehicleSeat::Gunner: return TEXT("Gunner");
		case EVehicleSeat::Passenger: return TEXT("Passenger");
		default: return TEXT("None");
		}
	}

	EVehicleSeat ParseSeatName(const FString& Name)
	{
		for (const EVehicleSeat Seat : { EVehicleSeat::Driver, EVehicleSeat::Gunner, EVehicleSeat::Passenger })
		{
			if (Name.Equals(GetSeatName(Seat), ESearchCase::IgnoreCase))
			{
				return Seat;
			}
		}
		return EVehicleSeat::None;
	}
}
