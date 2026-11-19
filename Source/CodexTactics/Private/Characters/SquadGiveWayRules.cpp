// Give way: an idle squadmate steps out of a moving squadmate's path (bug report 2026-11-02 item 7). See the header. UE-only (no Godot reference).

#include "Characters/SquadGiveWayRules.h"

#include "CodexTactics.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace SquadGiveWayRules
{
	bool IsIdle(const FIdleState& State)
	{
		return State.bWalker && !State.bOwnMoveOrder && !State.bInCover && !State.bCombatAction && !State.bMoving && !State.bHeld && !State.bAtOwnPost;
	}

	FCrossing FindCrossing(TArrayView<const FVector> Path, const FVector& IdleFeet, float CrossCm, const FSquadGiveWayTuning& Tuning)
	{
		FCrossing Out;
		const FVector2D Idle(IdleFeet);
		double Walked = 0.0;
		for (int32 Index = 0; Index + 1 < Path.Num() && Walked < Tuning.LookAheadCm; ++Index)
		{
			const FVector2D A(Path[Index]);
			const FVector2D B(Path[Index + 1]);
			const double Length = FVector2D::Distance(A, B);
			if (Length < UE_KINDA_SMALL_NUMBER)
			{
				continue;
			}
			const FVector2D Direction = (B - A) / Length;
			const double Raw = FVector2D::DotProduct(Idle - A, Direction);
			if (Index == 0 && Raw <= 0.0)
			{
				Walked += Length;
				continue; // beside / behind the mover: not his way ahead
			}
			// Only the part of the leg inside the look-ahead.
			const double T = FMath::Clamp(Raw, 0.0, FMath::Min(Length, static_cast<double>(Tuning.LookAheadCm) - Walked));
			const FVector2D Nearest = A + Direction * T;
			const double PathZ = FMath::Lerp(Path[Index].Z, Path[Index + 1].Z, T / Length);
			if (FVector2D::Distance(Nearest, Idle) < CrossCm && FMath::Abs(PathZ - IdleFeet.Z) <= Tuning.SameLevelCm)
			{
				Out.bCrosses = true;
				Out.Segment = Index;
				Out.AlongCm = static_cast<float>(Walked + T);
				Out.OnPath = Nearest;
				Out.Direction = Direction;
				Out.Side = FVector2D::CrossProduct(Direction, Idle - Nearest) >= 0.0 ? 1 : -1;
				return Out;
			}
			Walked += Length;
		}
		return Out;
	}

	float DistanceToPath(TArrayView<const FVector> Path, const FVector2D& Point, float MaxAlongCm)
	{
		double Best = TNumericLimits<double>::Max();
		double Walked = 0.0;
		for (int32 Index = 0; Index + 1 < Path.Num() && (MaxAlongCm <= 0.f || Walked < MaxAlongCm); ++Index)
		{
			const FVector2D A(Path[Index]);
			const FVector2D B(Path[Index + 1]);
			const double Length = FVector2D::Distance(A, B);
			if (Length < UE_KINDA_SMALL_NUMBER)
			{
				Best = FMath::Min(Best, FVector2D::Distance(A, Point));
				continue;
			}
			const FVector2D Direction = (B - A) / Length;
			const double Cap = MaxAlongCm <= 0.f ? Length : FMath::Min(Length, static_cast<double>(MaxAlongCm) - Walked);
			const double T = FMath::Clamp(FVector2D::DotProduct(Point - A, Direction), 0.0, Cap);
			Best = FMath::Min(Best, FVector2D::Distance(A + Direction * T, Point));
			Walked += Length;
		}
		if (Path.Num() == 1)
		{
			Best = FVector2D::Distance(FVector2D(Path[0]), Point);
		}
		return static_cast<float>(Best);
	}

	TArray<FVector2D> AsideSpots(TArrayView<const FVector> Path, const FCrossing& Crossing, float CrossCm, const FSquadGiveWayTuning& Tuning)
	{
		return AsideSpots(Path, Crossing, CrossCm, Tuning, nullptr);
	}

	TArray<FVector2D> AsideSpots(TArrayView<const FVector> Path, const FCrossing& Crossing, float CrossCm, const FSquadGiveWayTuning& Tuning,
		const FVector2D* IdleFeet, const FFlightApproach* Approach, float ZoneExtraCm)
	{
		TArray<FVector2D> Out;
		if (!Crossing.bCrosses)
		{
			return Out;
		}
		const FVector2D Left(-Crossing.Direction.Y, Crossing.Direction.X);
		const float Window = Crossing.AlongCm + Tuning.LookAheadCm;
		// v1's three distances; v2 (a man already well off the line, a zone to clear) two more.
		const TArray<float> Scales = IdleFeet || Approach ? TArray<float>{ 1.f, 1.5f, 2.f, 2.5f, 3.f } : TArray<float>{ 1.f, 1.5f, 2.f };
		for (const int32 Side : { Crossing.Side, -Crossing.Side })
		{
			const FVector2D Out90 = Left * static_cast<double>(Side);
			const FVector2D Back45 = (Out90 - Crossing.Direction).GetSafeNormal();
			for (const float Scale : Scales)
			{
				for (const FVector2D& Way : { Out90, Back45 })
				{
					const FVector2D Spot = Crossing.OnPath + Way * static_cast<double>(Tuning.StepAsideCm * Scale);
					if (DistanceToPath(Path, Spot, Window) < CrossCm
						|| (IdleFeet && FVector2D::Distance(Spot, *IdleFeet) < Tuning.MinStepCm)
						|| (Approach && Approach->bValid && InApproachZone(*Approach, Spot, ZoneExtraCm, Tuning)))
					{
						continue;
					}
					Out.Add(Spot);
				}
			}
		}
		return Out;
	}

	TArray<FFlightFoot> FindFlightFeet(TArrayView<const FVector> Path, TFunctionRef<bool(const FVector&, FVector2D&)> IsRamp, float SampleCm)
	{
		TArray<FFlightFoot> Out;
		if (Path.Num() < 2)
		{
			return Out;
		}
		const double Step = FMath::Max(5.0, static_cast<double>(SampleCm));
		FVector2D Uphill = FVector2D::ZeroVector;
		bool bWasRamp = IsRamp(Path[0], Uphill);
		for (int32 Index = 0; Index + 1 < Path.Num(); ++Index)
		{
			const FVector A = Path[Index];
			const FVector B = Path[Index + 1];
			const double Length = FVector2D::Distance(FVector2D(A), FVector2D(B));
			if (Length < UE_KINDA_SMALL_NUMBER)
			{
				continue;
			}
			const FVector2D Leg = (FVector2D(B) - FVector2D(A)) / Length;
			const int32 Samples = FMath::Max(1, FMath::CeilToInt(Length / Step));
			for (int32 Sample = 1; Sample <= Samples; ++Sample)
			{
				const FVector Point = FMath::Lerp(A, B, static_cast<double>(Sample) / Samples);
				Uphill = FVector2D::ZeroVector;
				const bool bRamp = IsRamp(Point, Uphill);
				if (bRamp && !bWasRamp)
				{
					FFlightFoot Foot;
					Foot.Foot = Point;
					const FVector2D Axis = Uphill.GetSafeNormal();
					Foot.Direction = Axis.IsNearlyZero() ? Leg : Axis * (FVector2D::DotProduct(Axis, Leg) >= 0.0 ? 1.0 : -1.0);
					Out.Add(Foot);
				}
				bWasRamp = bRamp;
			}
		}
		return Out;
	}

	FFlightApproach SelectApproach(TArrayView<const FFlightFoot> Feet, const FVector& MoverFeet, const FSquadGiveWayTuning& Tuning)
	{
		FFlightApproach Out;
		for (const FFlightFoot& Foot : Feet)
		{
			const double Past = FVector2D::DotProduct(FVector2D(MoverFeet) - FVector2D(Foot.Foot), Foot.Direction);
			if (Past > Tuning.ApproachAfterCm || -Past > Tuning.LookAheadCm)
			{
				continue; // passed, or still far
			}
			if (Past < 0.0 && FMath::Abs(MoverFeet.Z - Foot.Foot.Z) > Tuning.SameLevelCm)
			{
				continue; // another level's flight
			}
			Out.bValid = true;
			Out.Foot = FVector2D(Foot.Foot);
			Out.Direction = Foot.Direction;
			Out.FootZ = static_cast<float>(Foot.Foot.Z);
			Out.ToFootCm = static_cast<float>(-Past);
			return Out;
		}
		return Out;
	}

	bool InApproachZone(const FFlightApproach& Approach, const FVector2D& Point, float ExtraCm, const FSquadGiveWayTuning& Tuning)
	{
		if (!Approach.bValid)
		{
			return false;
		}
		const FVector2D Offset = Point - Approach.Foot;
		const double Along = FVector2D::DotProduct(Offset, Approach.Direction);
		const double Across = FMath::Abs(FVector2D::CrossProduct(Approach.Direction, Offset));
		return Along >= -Tuning.ApproachBeforeCm && Along <= Tuning.ApproachAfterCm && Across <= Tuning.ApproachHalfWidthCm + ExtraCm;
	}

	FCrossing ApproachCrossing(const FFlightApproach& Approach, const FVector2D& IdleFeet)
	{
		FCrossing Out;
		if (!Approach.bValid)
		{
			return Out;
		}
		const FVector2D Offset = IdleFeet - Approach.Foot;
		const double Along = FVector2D::DotProduct(Offset, Approach.Direction);
		Out.bCrosses = true;
		Out.OnPath = Approach.Foot + Approach.Direction * Along;
		Out.Direction = Approach.Direction;
		Out.AlongCm = FMath::Max(0.f, Approach.ToFootCm + static_cast<float>(Along));
		Out.Side = FVector2D::CrossProduct(Approach.Direction, Offset) >= 0.0 ? 1 : -1;
		return Out;
	}

	bool SlotWalkEntersApproach(const FFlightApproach& Approach, const FVector2D& From, const FVector2D& To, float ExtraCm, const FSquadGiveWayTuning& Tuning)
	{
		if (!Approach.bValid || InApproachZone(Approach, From, ExtraCm, Tuning))
		{
			return false; // already in it: let him walk out (holding him there blocks it)
		}
		// Liang-Barsky clip of the walk against the zone rectangle in the flight's frame.
		const FVector2D Side(-Approach.Direction.Y, Approach.Direction.X);
		const FVector2D P0 = From - Approach.Foot;
		const FVector2D D = To - From;
		const double X0 = FVector2D::DotProduct(P0, Approach.Direction), Y0 = FVector2D::DotProduct(P0, Side);
		const double DX = FVector2D::DotProduct(D, Approach.Direction), DY = FVector2D::DotProduct(D, Side);
		const double Half = Tuning.ApproachHalfWidthCm + ExtraCm;
		double T0 = 0.0, T1 = 1.0;
		const double Ps[4] = { -DX, DX, -DY, DY };
		const double Qs[4] = { X0 + Tuning.ApproachBeforeCm, Tuning.ApproachAfterCm - X0, Y0 + Half, Half - Y0 };
		for (int32 Edge = 0; Edge < 4; ++Edge)
		{
			if (FMath::IsNearlyZero(Ps[Edge]))
			{
				if (Qs[Edge] < 0.0)
				{
					return false;
				}
				continue;
			}
			const double T = Qs[Edge] / Ps[Edge];
			if (Ps[Edge] < 0.0)
			{
				T0 = FMath::Max(T0, T);
			}
			else
			{
				T1 = FMath::Min(T1, T);
			}
			if (T0 > T1)
			{
				return false;
			}
		}
		return true;
	}

	bool ShouldRetryBlockedMove(bool bOwnMoveOrder, float ToGoalCm, int32 RetriesSoFar, const FSquadGiveWayTuning& Tuning)
	{
		return Tuning.bEnabled && bOwnMoveOrder && ToGoalCm > Tuning.BlockedRetryMinGoalCm && RetriesSoFar < Tuning.BlockedRetries;
	}
}

namespace SquadGiveWayTuningLocal
{
	FSquadGiveWayTuning GiveWayTuningCurrent;
	bool bGiveWayTuningLoaded = false;

	void ReadGiveWayNumber(const FJsonObject& Json, const TCHAR* Key, float& Out, float Min, float Max)
	{
		double Value = 0.0;
		if (Json.TryGetNumberField(Key, Value))
		{
			Out = FMath::Clamp(static_cast<float>(Value), Min, Max);
		}
	}
}

FString SquadGiveWayTuning::GetDefaultPath()
{
	return FPaths::ProjectContentDir() / TEXT("Data/AI/squad_regroup.json");
}

FSquadGiveWayTuning SquadGiveWayTuning::FromJson(const FJsonObject& GiveWay, const FSquadGiveWayTuning& Base)
{
	using namespace SquadGiveWayTuningLocal;
	FSquadGiveWayTuning Tuning = Base;
	double Enabled = 0.0;
	if (GiveWay.TryGetNumberField(TEXT("enabled"), Enabled))
	{
		Tuning.bEnabled = Enabled != 0.0;
	}
	ReadGiveWayNumber(GiveWay, TEXT("look_ahead_cm"), Tuning.LookAheadCm, 50.f, 2000.f);
	ReadGiveWayNumber(GiveWay, TEXT("clearance_cm"), Tuning.ClearanceCm, 0.f, 200.f);
	ReadGiveWayNumber(GiveWay, TEXT("step_aside_cm"), Tuning.StepAsideCm, 50.f, 500.f);
	ReadGiveWayNumber(GiveWay, TEXT("same_level_cm"), Tuning.SameLevelCm, 30.f, 400.f);
	ReadGiveWayNumber(GiveWay, TEXT("nav_snap_cm"), Tuning.NavSnapCm, 5.f, 100.f);
	ReadGiveWayNumber(GiveWay, TEXT("max_rise_cm"), Tuning.MaxRiseCm, 5.f, 150.f);
	ReadGiveWayNumber(GiveWay, TEXT("retry_s"), Tuning.RetrySeconds, 0.1f, 10.f);
	ReadGiveWayNumber(GiveWay, TEXT("min_step_cm"), Tuning.MinStepCm, 0.f, 300.f);
	ReadGiveWayNumber(GiveWay, TEXT("approach_before_cm"), Tuning.ApproachBeforeCm, 0.f, 1000.f);
	ReadGiveWayNumber(GiveWay, TEXT("approach_after_cm"), Tuning.ApproachAfterCm, 0.f, 500.f);
	ReadGiveWayNumber(GiveWay, TEXT("approach_half_width_cm"), Tuning.ApproachHalfWidthCm, 0.f, 500.f);
	ReadGiveWayNumber(GiveWay, TEXT("ramp_sample_cm"), Tuning.RampSampleCm, 10.f, 200.f);
	ReadGiveWayNumber(GiveWay, TEXT("slot_hold_max_s"), Tuning.SlotHoldMaxSeconds, 0.f, 30.f);
	ReadGiveWayNumber(GiveWay, TEXT("blocked_retry_min_goal_cm"), Tuning.BlockedRetryMinGoalCm, 0.f, 1000.f);
	float Retries = static_cast<float>(Tuning.BlockedRetries);
	ReadGiveWayNumber(GiveWay, TEXT("blocked_retries"), Retries, 0.f, 20.f);
	Tuning.BlockedRetries = FMath::RoundToInt(Retries);
	return Tuning;
}

bool SquadGiveWayTuning::Reload()
{
	using namespace SquadGiveWayTuningLocal;
	bGiveWayTuningLoaded = true;
	FString Text;
	TSharedPtr<FJsonObject> Json;
	const TSharedPtr<FJsonObject>* GiveWay = nullptr;
	if (!FFileHelper::LoadFileToString(Text, *GetDefaultPath()) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid()
		|| !Json->TryGetObjectField(TEXT("give_way"), GiveWay) || !GiveWay || !GiveWay->IsValid())
	{
		return false;
	}
	GiveWayTuningCurrent = FromJson(**GiveWay);
	UE_LOG(LogCodexTactics, Display, TEXT("Squad give-way tuning from %s: enabled %d, look ahead %.0f cm, clearance %.0f cm, step aside %.0f cm, retry %.1f s"),
		*GetDefaultPath(), GiveWayTuningCurrent.bEnabled ? 1 : 0, GiveWayTuningCurrent.LookAheadCm, GiveWayTuningCurrent.ClearanceCm, GiveWayTuningCurrent.StepAsideCm, GiveWayTuningCurrent.RetrySeconds);
	return true;
}

const FSquadGiveWayTuning& SquadGiveWayTuning::Get()
{
	using namespace SquadGiveWayTuningLocal;
	if (!bGiveWayTuningLoaded)
	{
		Reload();
	}
	return GiveWayTuningCurrent;
}