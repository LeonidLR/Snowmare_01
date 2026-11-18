// The arena round's behaviour metrics per side (docs/design/TACTICAL_MAP.md P3, Grok G002 schema codex.pmc_arena_behaviour/1). UE-only.

#include "AI/PmcArenaBehaviourRules.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "LevelGen/TacticalMapRules.h"

const TCHAR* const PmcArenaBehaviourRules::Schema = TEXT("codex.pmc_arena_behaviour/1");

float FPmcBehaviour::Value(const FString& Key) const
{
	if (Key == TEXT("time_in_contact_s")) { return TimeInContactS; }
	if (Key == TEXT("flanks_completed")) { return static_cast<float>(FlanksCompleted); }
	if (Key == TEXT("moves")) { return static_cast<float>(Moves); }
	if (Key == TEXT("suppress_then_move")) { return static_cast<float>(SuppressThenMove); }
	if (Key == TEXT("ambush_success")) { return static_cast<float>(AmbushSuccess); }
	if (Key == TEXT("overwatch_kills")) { return static_cast<float>(OverwatchKills); }
	if (Key == TEXT("time_in_chokepoint_s")) { return TimeInChokepointS; }
	if (Key == TEXT("window_positions_used")) { return static_cast<float>(WindowPositionsUsed); }
	if (Key == TEXT("quiet_spells")) { return static_cast<float>(QuietSpells); }
	if (Key == TEXT("order_churn")) { return static_cast<float>(OrderChurn); }
	if (Key == TEXT("stalemate_share")) { return StalemateShare; }
	if (Key == TEXT("defender_spread_m")) { return DefenderSpreadM; }
	if (Key == TEXT("guard_near_access_share")) { return bGuardShareApplicable ? GuardNearAccessShare : 0.f; }
	return 0.f;
}

namespace PmcArenaBehaviourRules
{
	const TArray<FPmcBehaviourMetricSpec>& Catalog()
	{
		// The G002 table (docs/port/grok/outbox/G002-arena-behaviour-schema.md): the same keys, names, units, ranges and directions.
		static const TArray<FPmcBehaviourMetricSpec> Specs = {
			{ TEXT("time_in_contact_s"), TEXT("Time in contact"), TEXT("s"), 0.f, 240.f, TEXT("higher") },
			{ TEXT("flanks_completed"), TEXT("Flanks completed"), TEXT("count"), 0.f, 8.f, TEXT("higher") },
			{ TEXT("moves"), TEXT("Position changes"), TEXT("count"), 0.f, 40.f, TEXT("higher") },
			{ TEXT("suppress_then_move"), TEXT("Suppress-then-move"), TEXT("count"), 0.f, 8.f, TEXT("higher") },
			{ TEXT("ambush_success"), TEXT("Ambush success at chokepoint exits"), TEXT("count"), 0.f, 4.f, TEXT("higher") },
			{ TEXT("overwatch_kills"), TEXT("Overwatch kills"), TEXT("count"), 0.f, 8.f, TEXT("higher") },
			{ TEXT("time_in_chokepoint_s"), TEXT("Time inside a chokepoint"), TEXT("s"), 0.f, 240.f, TEXT("lower") },
			{ TEXT("window_positions_used"), TEXT("Window positions used"), TEXT("count"), 0.f, 6.f, TEXT("higher") },
			{ TEXT("quiet_spells"), TEXT("Quiet spells"), TEXT("count"), 0.f, 8.f, TEXT("context") },
			{ TEXT("order_churn"), TEXT("Order churn"), TEXT("count"), 0.f, 40.f, TEXT("lower") },
			{ TEXT("stalemate_share"), TEXT("Stalemate share"), TEXT("fraction"), 0.f, 1.f, TEXT("lower") },
			{ TEXT("defender_spread_m"), TEXT("Defender spread"), TEXT("m"), 0.f, 40.f, TEXT("higher") },
			{ TEXT("guard_near_access_share"), TEXT("Guard near the access"), TEXT("fraction"), 0.f, 1.f, TEXT("higher") },
		};
		return Specs;
	}

	float TimeInContact(TArray<float> ContactTimes, float DurationS, float WindowS)
	{
		ContactTimes.Sort();
		float Covered = 0.f;
		float CoveredUntil = 0.f;
		for (const float Time : ContactTimes)
		{
			const float Start = FMath::Clamp(Time, 0.f, DurationS);
			const float End = FMath::Clamp(Time + FMath::Max(WindowS, 0.f), 0.f, DurationS);
			const float From = FMath::Max(Start, CoveredUntil);
			if (End > From)
			{
				Covered += End - From;
				CoveredUntil = End;
			}
		}
		return Covered;
	}

	int32 QuietSpells(TArray<float> ContactTimes, float DurationS, float MinGapS)
	{
		if (ContactTimes.IsEmpty())
		{
			return 0;
		}
		ContactTimes.Sort();
		int32 Spells = 0;
		for (int32 Index = 1; Index < ContactTimes.Num(); ++Index)
		{
			Spells += ContactTimes[Index] - ContactTimes[Index - 1] >= MinGapS ? 1 : 0;
		}
		Spells += DurationS - ContactTimes.Last() >= MinGapS ? 1 : 0;
		return Spells;
	}

	int32 SuppressThenMove(TArray<FPmcOrderEvent> Events, float WindowS)
	{
		Events.StableSort([](const FPmcOrderEvent& Left, const FPmcOrderEvent& Right) { return Left.Time < Right.Time; });
		int32 Count = 0;
		for (int32 Index = 0; Index < Events.Num(); ++Index)
		{
			const FPmcOrderEvent& Suppress = Events[Index];
			if (Suppress.Order != EPmcOrder::Suppress)
			{
				continue;
			}
			float End = Suppress.Time + FMath::Max(WindowS, 0.f);
			for (int32 Next = Index + 1; Next < Events.Num(); ++Next)
			{
				if (Events[Next].Fighter == Suppress.Fighter)
				{
					End = FMath::Min(End, Events[Next].Time);
					break;
				}
			}
			for (int32 Next = Index + 1; Next < Events.Num() && Events[Next].Time <= End; ++Next)
			{
				const FPmcOrderEvent& Move = Events[Next];
				if (Move.Fighter != Suppress.Fighter && Move.Time > Suppress.Time && PmcCommanderRules::IsMoveOrder(Move.Order))
				{
					++Count;
					break;
				}
			}
		}
		return Count;
	}

	int32 OrderChurn(TArray<FPmcOrderEvent> Events)
	{
		Events.StableSort([](const FPmcOrderEvent& Left, const FPmcOrderEvent& Right) { return Left.Time < Right.Time; });
		TMap<int32, EPmcOrder> Last;
		int32 Churn = 0;
		for (const FPmcOrderEvent& Event : Events)
		{
			if (const EPmcOrder* Previous = Last.Find(Event.Fighter))
			{
				Churn += *Previous != Event.Order ? 1 : 0;
			}
			Last.Add(Event.Fighter, Event.Order);
		}
		return Churn;
	}

	bool IsCompletedMove(EPmcOrder Order, const FVector& From, const FVector& To)
	{
		return PmcCommanderRules::IsMoveOrder(Order) && FVector::Dist2D(From, To) >= MoveMinCm;
	}

	float StalemateShare(bool bContact, const FString& Winner, const FString& EndReason)
	{
		return !bContact || (Winner == TEXT("draw") && EndReason == TEXT("time_limit")) ? 1.f : 0.f;
	}

	bool IsInChokepoint(const FTacticalMapFile& File, const FTacticalRuntimeConfig& Config, const FVector& Feet)
	{
		return !TacticalMapRules::GetZonesNear(File, Config, Feet, 0.f).IsEmpty();
	}

	int32 WindowAt(const FTacticalMapFile& File, const FTacticalRuntimeConfig& Config, const FVector& Feet)
	{
		for (int32 Index = 0; Index < File.Windows.Num(); ++Index)
		{
			const FVector& Stand = File.Windows[Index].Stand;
			if (FVector::Dist2D(Stand, Feet) <= WindowRadiusCm
				&& TacticalMapRules::IsSameFloor(static_cast<float>(Stand.Z), static_cast<float>(Feet.Z), Config.FloorToleranceCm))
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	bool IsAmbushHit(const FTacticalMapFile& File, const FTacticalRuntimeConfig& Config, const FVector& ShooterFeet, const FVector& TargetFeet)
	{
		for (const FTacticalHidingSpot& Spot : File.HidingSpots)
		{
			if (Spot.PlaceIndex == INDEX_NONE || FVector::Dist2D(Spot.Location, ShooterFeet) > SpotRadiusCm
				|| !TacticalMapRules::IsSameFloor(static_cast<float>(Spot.Location.Z), static_cast<float>(ShooterFeet.Z), Config.FloorToleranceCm))
			{
				continue;
			}
			if (FVector::Dist2D(Spot.ThreatLocation, TargetFeet) <= AmbushThreatRadiusCm)
			{
				return true;
			}
		}
		return false;
	}

	bool IsAtOverwatchSpot(const FTacticalMapFile& File, const FTacticalRuntimeConfig& Config, const FVector& ShooterFeet)
	{
		for (const FTacticalOverwatchRoof& Roof : File.OverwatchRoofs)
		{
			for (const FTacticalOverwatchSpot& Spot : Roof.Spots)
			{
				if (FVector::Dist2D(Spot.Location, ShooterFeet) <= SpotRadiusCm
					&& TacticalMapRules::IsSameFloor(static_cast<float>(Spot.Location.Z), static_cast<float>(ShooterFeet.Z), Config.FloorToleranceCm))
				{
					return true;
				}
			}
		}
		return false;
	}

	TSharedRef<FJsonObject> ToJson(const FPmcBehaviour& Behaviour, const FString& Side, bool bTacticalMap)
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("schema"), Schema);
		Root->SetStringField(TEXT("side"), Side);
		Root->SetBoolField(TEXT("tactical_map"), bTacticalMap);
		TSharedRef<FJsonObject> Metrics = MakeShared<FJsonObject>();
		for (const FPmcBehaviourMetricSpec& Spec : Catalog())
		{
			TSharedRef<FJsonObject> Metric = MakeShared<FJsonObject>();
			Metric->SetStringField(TEXT("name"), Spec.Name);
			Metric->SetStringField(TEXT("unit"), Spec.Unit);
			Metric->SetArrayField(TEXT("range"), { MakeShared<FJsonValueNumber>(Spec.RangeMin), MakeShared<FJsonValueNumber>(Spec.RangeMax) });
			Metric->SetStringField(TEXT("direction"), Spec.Direction);
			if (Spec.Key == FString(TEXT("guard_near_access_share")) && !Behaviour.bGuardShareApplicable)
			{
				Metric->SetField(TEXT("value"), MakeShared<FJsonValueNull>());
			}
			else
			{
				Metric->SetNumberField(TEXT("value"), FMath::RoundToFloat(Behaviour.Value(Spec.Key) * 10.f) / 10.f);
			}
			Metrics->SetObjectField(Spec.Key, Metric);
		}
		Root->SetObjectField(TEXT("metrics"), Metrics);
		return Root;
	}
}
