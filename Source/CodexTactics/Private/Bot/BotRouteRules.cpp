#include "Bot/BotRouteRules.h"

#include "Dom/JsonObject.h"

namespace BotRouteLocal
{
	bool ReadXY(const FJsonObject& Object, const TCHAR* Key, FVector2D& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* XY = nullptr;
		double X = 0.0;
		double Y = 0.0;
		if (!Object.TryGetArrayField(Key, XY) || !XY || XY->Num() < 2 || !(*XY)[0].IsValid() || !(*XY)[1].IsValid()
			|| !(*XY)[0]->TryGetNumber(X) || !(*XY)[1]->TryGetNumber(Y))
		{
			return false;
		}
		Out = FVector2D(X, Y);
		return true;
	}

	float Number(const FJsonObject& Object, const TCHAR* Key, float Default)
	{
		double Value = Default;
		return Object.TryGetNumberField(Key, Value) ? static_cast<float>(Value) : Default;
	}

	void SetError(FString* OutError, const FString& Error)
	{
		if (OutError)
		{
			*OutError = Error;
		}
	}
}

float FBotRouteConfig::LapLengthCm() const
{
	float Length = 0.f;
	for (int32 Each = 1; Each < Points.Num(); ++Each)
	{
		Length += static_cast<float>(FVector2D::Distance(Points[Each - 1].At, Points[Each].At));
	}
	if (bLoop && Points.Num() >= 2)
	{
		Length += static_cast<float>(FVector2D::Distance(Points.Last().At, Points[0].At));
	}
	return Length;
}

bool BotRouteRules::ParseAction(const FString& Name, EBotRouteAction& Out)
{
	if (Name.Equals(TEXT("walk"), ESearchCase::IgnoreCase))
	{
		Out = EBotRouteAction::Walk;
		return true;
	}
	if (Name.Equals(TEXT("wall"), ESearchCase::IgnoreCase))
	{
		Out = EBotRouteAction::Wall;
		return true;
	}
	if (Name.Equals(TEXT("climb"), ESearchCase::IgnoreCase))
	{
		Out = EBotRouteAction::Climb;
		return true;
	}
	return false;
}

const TCHAR* BotRouteRules::ActionName(EBotRouteAction Action)
{
	switch (Action)
	{
	case EBotRouteAction::Wall: return TEXT("wall");
	case EBotRouteAction::Climb: return TEXT("climb");
	default: return TEXT("walk");
	}
}

bool BotRouteRules::Parse(const FJsonObject& Route, FBotRouteConfig& OutConfig, FString* OutError)
{
	using namespace BotRouteLocal;
	FBotRouteConfig Config;
	Route.TryGetStringField(TEXT("name"), Config.Name);
	bool bLoop = true;
	Config.bLoop = !Route.TryGetBoolField(TEXT("loop"), bLoop) || bLoop;
	Config.ReachRadiusCm = FMath::Max(50.f, Number(Route, TEXT("reach_radius_cm"), Config.ReachRadiusCm));
	Config.StuckAfterSeconds = FMath::Max(1.f, Number(Route, TEXT("stuck_after_sec"), Config.StuckAfterSeconds));
	Config.StuckMoveCm = FMath::Max(10.f, Number(Route, TEXT("stuck_move_cm"), Config.StuckMoveCm));
	Config.LegBaseSeconds = FMath::Max(1.f, Number(Route, TEXT("leg_base_sec"), Config.LegBaseSeconds));
	Config.LegMinSpeedCm = FMath::Max(10.f, Number(Route, TEXT("leg_min_speed_cm"), Config.LegMinSpeedCm));
	Config.MaxRetries = FMath::Clamp(FMath::RoundToInt(Number(Route, TEXT("max_retries"), static_cast<float>(Config.MaxRetries))), 0, 10);
	Config.FightHoldSeconds = FMath::Max(0.f, Number(Route, TEXT("fight_hold_sec"), Config.FightHoldSeconds));
	Config.ActionTimeoutSeconds = FMath::Max(5.f, Number(Route, TEXT("action_timeout_sec"), Config.ActionTimeoutSeconds));

	const TArray<TSharedPtr<FJsonValue>>* Points = nullptr;
	if (!Route.TryGetArrayField(TEXT("points"), Points) || !Points || Points->Num() < 2)
	{
		SetError(OutError, TEXT("bot_route.points: at least two waypoints"));
		return false;
	}
	for (int32 Each = 0; Each < Points->Num(); ++Each)
	{
		const TSharedPtr<FJsonObject> Point = (*Points)[Each].IsValid() ? (*Points)[Each]->AsObject() : nullptr;
		FBotRouteWaypoint Waypoint;
		if (!Point.IsValid() || !ReadXY(*Point, TEXT("at"), Waypoint.At))
		{
			SetError(OutError, FString::Printf(TEXT("bot_route.points[%d]: \"at\": [x, y] (world cm)"), Each));
			return false;
		}
		Point->TryGetStringField(TEXT("label"), Waypoint.Label);
		FString Action;
		if (Point->TryGetStringField(TEXT("action"), Action) && !ParseAction(Action, Waypoint.Action))
		{
			SetError(OutError, FString::Printf(TEXT("bot_route.points[%d]: unknown action '%s' (walk | wall | climb)"), Each, *Action));
			return false;
		}
		if (Waypoint.Action == EBotRouteAction::Wall && !ReadXY(*Point, TEXT("wall"), Waypoint.WallPoint))
		{
			SetError(OutError, FString::Printf(TEXT("bot_route.points[%d]: a wall action needs \"wall\": [x, y] (a point on the wall face)"), Each));
			return false;
		}
		Waypoint.HoldSeconds = FMath::Clamp(Number(*Point, TEXT("hold_sec"), Waypoint.HoldSeconds), 0.f, 120.f);
		if (Waypoint.Label.IsEmpty())
		{
			Waypoint.Label = FString::Printf(TEXT("#%d"), Each);
		}
		Config.Points.Add(MoveTemp(Waypoint));
	}
	OutConfig = MoveTemp(Config);
	return true;
}

void FBotRouteTracker::Reset(const FBotRouteConfig& InConfig, float Time, const FVector2D& Leader)
{
	*this = FBotRouteTracker();
	Config = InConfig;
	LastLeader = Leader;
	LastTime = Time;
	LapStartTime = Time;
	ProgressPos = Leader;
	ProgressTime = Time;
	bFinished = !Config.IsValid();
}

void FBotRouteTracker::StartLeg(const FBotRouteInput& In)
{
	ProgressPos = In.Leader;
	ProgressTime = In.Time;
	LegSeconds = 0.f;
	LegLimitSeconds = Config.LegBaseSeconds + static_cast<float>(FVector2D::Distance(In.Leader, GetTarget())) / Config.LegMinSpeedCm;
}

EBotRouteCommand FBotRouteTracker::Tick(const FBotRouteInput& In)
{
	if (bFinished)
	{
		return EBotRouteCommand::None;
	}
	const float Delta = FMath::Max(0.f, In.Time - LastTime);
	LastTime = In.Time;
	DistanceCm += static_cast<float>(FVector2D::Distance(In.Leader, LastLeader));
	LastLeader = In.Leader;
	if (bInAction)
	{
		return EBotRouteCommand::None;
	}
	if (!bOrdered)
	{
		bOrdered = true;
		StartLeg(In);
		return EBotRouteCommand::Move;
	}
	if (In.bPaused)
	{
		ProgressPos = In.Leader;
		ProgressTime = In.Time;
		return EBotRouteCommand::None;
	}
	LegSeconds += Delta;
	if (FVector2D::Distance(In.Leader, GetTarget()) <= Config.ReachRadiusCm)
	{
		if (GetWaypoint()->Action != EBotRouteAction::Walk)
		{
			bInAction = true;
			return EBotRouteCommand::StartAction;
		}
		return Advance(In, true);
	}
	if (FVector2D::Distance(In.Leader, ProgressPos) >= Config.StuckMoveCm)
	{
		ProgressPos = In.Leader;
		ProgressTime = In.Time;
	}
	else if (In.Time - ProgressTime >= Config.StuckAfterSeconds)
	{
		return Stuck(In, TEXT("no_move"));
	}
	if (LegSeconds >= LegLimitSeconds)
	{
		return Stuck(In, TEXT("leg_timeout"));
	}
	return EBotRouteCommand::None;
}

EBotRouteCommand FBotRouteTracker::Stuck(const FBotRouteInput& In, const FString& Reason)
{
	FBotRouteStuck Event;
	Event.Time = In.Time;
	Event.Position = In.Leader;
	Event.Waypoint = Index;
	Event.Label = GetWaypoint() ? GetWaypoint()->Label : FString();
	Event.LastOrder = LastOrder;
	Event.Reason = Reason;
	if (Retries >= Config.MaxRetries)
	{
		Event.bSkipped = true;
		Stucks.Add(MoveTemp(Event));
		return Advance(In, false);
	}
	++Retries;
	Stucks.Add(MoveTemp(Event));
	StartLeg(In);
	return EBotRouteCommand::Retry;
}

EBotRouteCommand FBotRouteTracker::NotifyOrderFailed(const FBotRouteInput& In, const FString& Why)
{
	if (bFinished)
	{
		return EBotRouteCommand::None;
	}
	bOrdered = true;
	return Stuck(In, FString::Printf(TEXT("order_failed:%s"), *Why));
}

EBotRouteCommand FBotRouteTracker::FinishAction(const FBotRouteInput& In, bool bOk, const FString& Why)
{
	if (!bInAction || bFinished)
	{
		return EBotRouteCommand::None;
	}
	bInAction = false;
	if (!bOk)
	{
		FBotRouteStuck Event;
		Event.Time = In.Time;
		Event.Position = In.Leader;
		Event.Waypoint = Index;
		Event.Label = GetWaypoint() ? GetWaypoint()->Label : FString();
		Event.LastOrder = LastOrder;
		Event.Reason = FString::Printf(TEXT("action_failed:%s"), *Why);
		Event.bSkipped = true;
		Stucks.Add(MoveTemp(Event));
	}
	return Advance(In, bOk);
}

EBotRouteCommand FBotRouteTracker::Advance(const FBotRouteInput& In, bool bReached)
{
	const int32 Num = Config.Points.Num();
	const int32 LapSize = Config.bLoop ? Num : Num - 1;
	if (!bStarted)
	{
		// The first waypoint is the start: walking there is no part of a lap.
		bStarted = true;
		LapStartTime = In.Time;
		(bReached ? TotalReached : TotalSkipped)++;
	}
	else
	{
		++LapProcessed;
		if (bReached)
		{
			++LapReached;
			++TotalReached;
			if (Laps.Num() == 0)
			{
				FurthestFirstLap = Index;
			}
		}
		else
		{
			++LapSkipped;
			++TotalSkipped;
		}
		BestLapReached = FMath::Max(BestLapReached, LapReached);
		if (LapProcessed >= LapSize)
		{
			FBotRouteLap Lap;
			Lap.Seconds = In.Time - LapStartTime;
			Lap.Reached = LapReached;
			Lap.Skipped = LapSkipped;
			Laps.Add(Lap);
			LapProcessed = 0;
			LapReached = 0;
			LapSkipped = 0;
			LapStartTime = In.Time;
		}
	}
	Retries = 0;
	if (!Config.bLoop && Index >= Num - 1)
	{
		bFinished = true;
		return EBotRouteCommand::Finished;
	}
	Index = (Index + 1) % Num;
	bOrdered = true;
	StartLeg(In);
	return EBotRouteCommand::Move;
}

bool FBotRouteTracker::IsRouteCompleted() const
{
	return Laps.ContainsByPredicate([](const FBotRouteLap& Lap) { return Lap.Skipped == 0; });
}

float FBotRouteTracker::GetProgress() const
{
	if (IsRouteCompleted())
	{
		return 1.f;
	}
	const int32 LapSize = Config.bLoop ? Config.Points.Num() : Config.Points.Num() - 1;
	return LapSize > 0 ? FMath::Clamp(static_cast<float>(BestLapReached) / static_cast<float>(LapSize), 0.f, 1.f) : 0.f;
}
