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
		return State.bWalker && !State.bOwnMoveOrder && !State.bInCover && !State.bCombatAction && !State.bMoving && !State.bHeld;
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
		TArray<FVector2D> Out;
		if (!Crossing.bCrosses)
		{
			return Out;
		}
		const FVector2D Left(-Crossing.Direction.Y, Crossing.Direction.X);
		const float Window = Crossing.AlongCm + Tuning.LookAheadCm;
		for (const int32 Side : { Crossing.Side, -Crossing.Side })
		{
			const FVector2D Out90 = Left * static_cast<double>(Side);
			const FVector2D Back45 = (Out90 - Crossing.Direction).GetSafeNormal();
			for (const float Scale : { 1.f, 1.5f, 2.f })
			{
				for (const FVector2D& Way : { Out90, Back45 })
				{
					const FVector2D Spot = Crossing.OnPath + Way * static_cast<double>(Tuning.StepAsideCm * Scale);
					if (DistanceToPath(Path, Spot, Window) >= CrossCm)
					{
						Out.Add(Spot);
					}
				}
			}
		}
		return Out;
	}
}

namespace SquadGiveWayTuningLocal
{
	FSquadGiveWayTuning Current;
	bool bLoaded = false;

	void ReadNumber(const FJsonObject& Json, const TCHAR* Key, float& Out, float Min, float Max)
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
	ReadNumber(GiveWay, TEXT("look_ahead_cm"), Tuning.LookAheadCm, 50.f, 2000.f);
	ReadNumber(GiveWay, TEXT("clearance_cm"), Tuning.ClearanceCm, 0.f, 200.f);
	ReadNumber(GiveWay, TEXT("step_aside_cm"), Tuning.StepAsideCm, 50.f, 500.f);
	ReadNumber(GiveWay, TEXT("same_level_cm"), Tuning.SameLevelCm, 30.f, 400.f);
	ReadNumber(GiveWay, TEXT("nav_snap_cm"), Tuning.NavSnapCm, 5.f, 100.f);
	ReadNumber(GiveWay, TEXT("max_rise_cm"), Tuning.MaxRiseCm, 5.f, 150.f);
	ReadNumber(GiveWay, TEXT("retry_s"), Tuning.RetrySeconds, 0.1f, 10.f);
	return Tuning;
}

bool SquadGiveWayTuning::Reload()
{
	using namespace SquadGiveWayTuningLocal;
	bLoaded = true;
	FString Text;
	TSharedPtr<FJsonObject> Json;
	const TSharedPtr<FJsonObject>* GiveWay = nullptr;
	if (!FFileHelper::LoadFileToString(Text, *GetDefaultPath()) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid()
		|| !Json->TryGetObjectField(TEXT("give_way"), GiveWay) || !GiveWay || !GiveWay->IsValid())
	{
		return false;
	}
	Current = FromJson(**GiveWay);
	UE_LOG(LogCodexTactics, Display, TEXT("Squad give-way tuning from %s: enabled %d, look ahead %.0f cm, clearance %.0f cm, step aside %.0f cm, retry %.1f s"),
		*GetDefaultPath(), Current.bEnabled ? 1 : 0, Current.LookAheadCm, Current.ClearanceCm, Current.StepAsideCm, Current.RetrySeconds);
	return true;
}

const FSquadGiveWayTuning& SquadGiveWayTuning::Get()
{
	using namespace SquadGiveWayTuningLocal;
	if (!bLoaded)
	{
		Reload();
	}
	return Current;
}