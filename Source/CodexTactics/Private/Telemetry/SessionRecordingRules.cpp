#include "Telemetry/SessionRecordingRules.h"

#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonWriter.h"

const TCHAR* SessionRecordingRules::OffReason(const FDemoGate& Gate)
{
	if (Gate.Switch == 0) { return TEXT("disabled"); }
	if (!Gate.bGameWorld) { return TEXT("not a game world"); }
	if (Gate.Switch == 2) { return nullptr; }
	if (Gate.bUnattended) { return TEXT("unattended"); }
	if (Gate.bNullRhi) { return TEXT("nullrhi"); }
	if (Gate.bNoTelemetry) { return TEXT("NoTelemetry"); }
	if (Gate.bCommandlet) { return TEXT("commandlet"); }
	return Gate.bAutomation ? TEXT("automation") : nullptr;
}

bool SessionRecordingRules::ShouldRecord(const FDemoGate& Gate)
{
	return OffReason(Gate) == nullptr;
}

bool SessionRecordingRules::ShouldDiscard(double Duration, int32 Accepted, bool bSuppressed)
{
	return Duration < MinSessionSec || Accepted == 0 || bSuppressed;
}

FString SessionRecordingRules::CleanMapName(FString Map)
{
	if (Map.StartsWith(TEXT("UEDPIE_")))
	{
		const int32 IndexEnd = Map.Find(TEXT("_"), ESearchCase::CaseSensitive, ESearchDir::FromStart, 7);
		if (IndexEnd != INDEX_NONE && IndexEnd > 7)
		{
			bool bNumeric = true;
			for (int32 Index = 7; Index < IndexEnd; ++Index)
			{
				bNumeric &= Map[Index] >= '0' && Map[Index] <= '9';
			}
			if (bNumeric) { Map.RightChopInline(IndexEnd + 1); }
		}
	}
	for (TCHAR& Ch : Map)
	{
		if (!(Ch >= 'A' && Ch <= 'Z') && !(Ch >= 'a' && Ch <= 'z') && !(Ch >= '0' && Ch <= '9') && Ch != '_' && Ch != '-')
		{
			Ch = '_';
		}
	}
	return Map.IsEmpty() ? FString(TEXT("Unknown")) : Map;
}

FString SessionRecordingRules::MakeDemoFileName(FDateTime Local, const FString& Map)
{
	return FString::Printf(TEXT("%04d-%02d-%02d_%s_%02d%02d%02d.jsonl"), Local.GetYear(), Local.GetMonth(), Local.GetDay(),
		*CleanMapName(Map), Local.GetHour(), Local.GetMinute(), Local.GetSecond());
}

const TCHAR* SessionRecordingRules::KindName(EDemoOrderKind Kind)
{
	switch (Kind)
	{
	case EDemoOrderKind::Move: return TEXT("Move");
	case EDemoOrderKind::Cover: return TEXT("Cover");
	case EDemoOrderKind::CornerPeek: return TEXT("CornerPeek");
	case EDemoOrderKind::Shot: return TEXT("Shot");
	case EDemoOrderKind::QuickFire: return TEXT("QuickFire");
	case EDemoOrderKind::Suppress: return TEXT("Suppress");
	case EDemoOrderKind::Stance: return TEXT("Stance");
	case EDemoOrderKind::Medkit: return TEXT("Medkit");
	case EDemoOrderKind::Breach: return TEXT("Breach");
	case EDemoOrderKind::Grenade: return TEXT("Grenade");
	}
	return TEXT("Unknown");
}

FDemoMapping SessionRecordingRules::MapToPmcOrder(const FDemoFacts& Facts)
{
	const bool bThreat = Facts.ThreatDistance >= 0.;
	const bool bMove = Facts.ThreatDistanceNew >= 0.;
	const double Delta = Facts.ThreatDistanceNew - Facts.ThreatDistance;
	switch (Facts.Kind)
	{
	case EDemoOrderKind::Cover:
	case EDemoOrderKind::CornerPeek:
		if (!bThreat) { return { TEXT("Ambush") }; }
		if (bMove && Delta >= MoveDeltaCm) { return { TEXT("FallBack") }; }
		return { Facts.bInCover ? TEXT("Relocate") : TEXT("TakeCover") };
	case EDemoOrderKind::Move:
		if (!bThreat)
		{
			return Facts.GoalDistanceNew >= 0. && Facts.GoalDistanceNew <= Facts.GoalDistance - MoveDeltaCm
				? FDemoMapping{ TEXT("Objective") } : FDemoMapping{ nullptr, TEXT("explore_move") };
		}
		if (bMove && Delta >= MoveDeltaCm) { return { TEXT("FallBack") }; }
		if (bMove && Facts.FlankDegrees >= FlankDeg) { return { TEXT("Flank") }; }
		if (bMove && Delta <= -MoveDeltaCm) { return { TEXT("Advance") }; }
		return { nullptr, TEXT("reposition") };
	case EDemoOrderKind::Suppress:
		return Facts.bCease ? FDemoMapping{ nullptr, TEXT("suppress_cease") }
			: Facts.bAccepted ? FDemoMapping{ TEXT("Suppress") } : FDemoMapping{ nullptr, TEXT("refused") };
	case EDemoOrderKind::Stance:
		return Facts.bCrouch && bThreat && !Facts.bMoving && !Facts.bInCover ? FDemoMapping{ TEXT("Crouch") } : FDemoMapping{ nullptr, TEXT("stance") };
	case EDemoOrderKind::Medkit: return { TEXT("Heal") };
	case EDemoOrderKind::Shot:
	case EDemoOrderKind::QuickFire: return { nullptr, TEXT("fire_target") };
	case EDemoOrderKind::Grenade: return { nullptr, TEXT("grenade") };
	case EDemoOrderKind::Breach: return { nullptr, TEXT("breach") };
	}
	return { nullptr, TEXT("unknown") };
}

FVector2D SessionRecordingRules::ThreatDistanceBearing(const FVector& Actor, double Yaw, const FVector* Threat)
{
	if (!Threat) { return FVector2D(-1., 0.); }
	const FVector Delta = *Threat - Actor;
	return FVector2D(Delta.Size2D(), FMath::UnwindDegrees(FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X)) - Yaw));
}

namespace SessionRecordingJson
{
	using FWriter = TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>;

	double Finite(double Value) { return FMath::IsFinite(Value) ? Value : 0.; }
	double Round(double Value, double Scale = 1.) { return FMath::RoundToDouble(Finite(Value) * Scale) / Scale; }
	void Position(FWriter& Writer, const FString& Key, const TOptional<FVector>& Point)
	{
		if (!Point) { Writer.WriteNull(Key); return; }
		Writer.WriteArrayStart(Key);
		Writer.WriteValue(Round(Point->X));
		Writer.WriteValue(Round(Point->Y));
		Writer.WriteValue(Round(Point->Z));
		Writer.WriteArrayEnd();
	}
	void TargetFields(FWriter& Writer, const FDemoThreat& Target)
	{
		Writer.WriteValue(TEXT("id"), Target.Id.ToString());
		Writer.WriteValue(TEXT("arch"), Target.Archetype.ToString());
		Writer.WriteValue(TEXT("yaw"), Round(Target.Yaw, 10.));
		if (Target.InCover.IsSet()) { Writer.WriteValue(TEXT("in_cover"), Target.InCover.GetValue()); }
		Position(Writer, TEXT("p"), Target.Position);
	}
}

FString SessionRecordingRules::SerializeSession(const FDemoSession& Session, const FString& FileStem)
{
	using namespace SessionRecordingJson;
	FString Out;
	const auto Line = [&Out](TFunctionRef<void(FWriter&)> Body)
	{
		FString Text;
		const TSharedRef<FWriter> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
		Writer->WriteObjectStart();
		Body(*Writer);
		Writer->WriteObjectEnd();
		Writer->Close();
		Out += Text;
		Out += TEXT("\n");
	};
	Line([&](FWriter& W)
	{
		W.WriteValue(TEXT("type"), TEXT("header"));
		W.WriteValue(TEXT("v"), 1);
		W.WriteValue(TEXT("session"), FileStem);
		W.WriteValue(TEXT("map"), Session.Map);
		if (Session.SectorBox)
		{
			const FBox2D& Box = *Session.SectorBox;
			W.WriteValue(TEXT("sector_id"), Session.SectorId);
			W.WriteObjectStart(TEXT("sector_box_cm"));
			W.WriteArrayStart(TEXT("min"));
			W.WriteValue(Finite(Box.Min.X));
			W.WriteValue(Finite(Box.Min.Y));
			W.WriteArrayEnd();
			W.WriteArrayStart(TEXT("max"));
			W.WriteValue(Finite(Box.Max.X));
			W.WriteValue(Finite(Box.Max.Y));
			W.WriteArrayEnd();
			W.WriteObjectEnd();
		}
		W.WriteValue(TEXT("started_utc"), Session.StartedUtc.ToIso8601());
		W.WriteValue(TEXT("world"), Session.bPie ? TEXT("PIE") : TEXT("Game"));
		W.WriteValue(TEXT("units"), TEXT("cm,deg,s"));
		W.WriteArrayStart(TEXT("squad"));
		for (const FDemoUnit& Unit : Session.Units)
		{
			W.WriteObjectStart();
			W.WriteValue(TEXT("id"), Unit.Id.ToString());
			W.WriteValue(TEXT("role"), Unit.Role.ToString());
			W.WriteValue(TEXT("hp_max"), Finite(Unit.MaxHealth));
			W.WriteValue(TEXT("weapon"), Unit.Weapon.ToString());
			W.WriteValue(TEXT("clip_max"), Unit.ClipMax);
			W.WriteObjectEnd();
		}
		W.WriteArrayEnd();
		W.WriteObjectStart(TEXT("flags"));
		W.WriteValue(TEXT("immortal"), Session.bImmortal);
		W.WriteValue(TEXT("infinite_ammo"), Session.bInfiniteAmmo);
		W.WriteObjectEnd();
		Position(W, TEXT("goal"), Session.InitialGoal);
	});
	TArray<int32> Indices;
	Indices.Reserve(Session.Frames.Num());
	for (int32 Index = 0; Index < Session.Frames.Num(); ++Index) { Indices.Add(Index); }
	Indices.StableSort([&](int32 Left, int32 Right)
	{
		return Finite(Session.Frames[Left].Time) < Finite(Session.Frames[Right].Time);
	});
	TArray<int32> RoundIndices;
	RoundIndices.Reserve(Session.Rounds.Num());
	for (int32 Index = 0; Index < Session.Rounds.Num(); ++Index) { RoundIndices.Add(Index); }
	RoundIndices.StableSort([&](int32 Left, int32 Right)
	{
		return Finite(Session.Rounds[Left].Time) < Finite(Session.Rounds[Right].Time);
	});
	int32 RoundCursor = 0;
	const auto EmitRound = [&](int32 RoundIndex)
	{
		const FDemoRoundEvent& Event = Session.Rounds[RoundIndex];
		Line([&](FWriter& W)
		{
			if (Event.bPmcRelocated)
			{
				W.WriteValue(TEXT("type"), TEXT("pmc_relocated"));
				W.WriteValue(TEXT("t"), Finite(Event.Time));
				W.WriteValue(TEXT("rt"), Finite(Event.RealTime));
				W.WriteValue(TEXT("sector_id"), Event.SectorId);
				W.WriteValue(TEXT("seed"), Event.Seed);
				W.WriteValue(TEXT("round"), Event.Round);
				W.WriteValue(TEXT("distance_m"), Finite(Event.RelocateDistanceM));
				W.WriteValue(TEXT("los"), Event.bRelocateLos);
				return;
			}
			const TCHAR* Results[] = { TEXT("none"), TEXT("victory"), TEXT("defeat"), TEXT("draw") };
			W.WriteValue(TEXT("type"), Event.bStart ? TEXT("round_start") : TEXT("round_end"));
			W.WriteValue(TEXT("t"), Finite(Event.Time));
			W.WriteValue(TEXT("rt"), Finite(Event.RealTime));
			W.WriteValue(TEXT("sector_id"), Event.SectorId);
			W.WriteValue(TEXT("seed"), Event.Seed);
			W.WriteValue(TEXT("round"), Event.Round);
			W.WriteValue(TEXT("start"), Event.StartIndex);
			W.WriteValue(TEXT("post"), Event.PostIndex);
			W.WriteValue(TEXT("duration_s"), Finite(Event.DurationS));
			W.WriteValue(TEXT("kills_player"), Event.KillsPlayer);
			W.WriteValue(TEXT("kills_pmc"), Event.KillsPmc);
			W.WriteValue(TEXT("score_victory"), Event.ScoreVictory);
			W.WriteValue(TEXT("score_defeat"), Event.ScoreDefeat);
			W.WriteValue(TEXT("score_draw"), Event.ScoreDraw);
			if (!Event.bStart)
			{
				W.WriteValue(TEXT("result"), Results[FMath::Clamp(static_cast<int32>(Event.Result), 0, 3)]);
				auto WriteBuckets = [&](const TCHAR* Key, const int32 (&Arr)[FDemoRoundEvent::ShotBuckets])
				{
					W.WriteArrayStart(Key);
					for (int32 B = 0; B < FDemoRoundEvent::ShotBuckets; ++B) { W.WriteValue(Arr[B]); }
					W.WriteArrayEnd();
				};
				WriteBuckets(TEXT("shots_player"), Event.ShotsPlayer);
				WriteBuckets(TEXT("hits_player"), Event.HitsPlayer);
				WriteBuckets(TEXT("shots_pmc"), Event.ShotsPmc);
				WriteBuckets(TEXT("hits_pmc"), Event.HitsPmc);
			}
		});
	};
	TArray<int32> EventIndices;
	EventIndices.Reserve(Session.Events.Num());
	for (int32 Index = 0; Index < Session.Events.Num(); ++Index) { EventIndices.Add(Index); }
	EventIndices.StableSort([&](int32 Left, int32 Right)
	{
		return Finite(Session.Events[Left].Time) < Finite(Session.Events[Right].Time);
	});
	int32 EventCursor = 0;
	const auto EmitEvent = [&](int32 EventIndex)
	{
		const FDemoEvent& Event = Session.Events[EventIndex];
		Line([&](FWriter& W)
		{
			switch (Event.Type)
			{
			case FDemoEvent::EType::RoundMark:
                W.WriteValue(TEXT("type"), TEXT("round_mark"));
                W.WriteValue(TEXT("t"), Finite(Event.Time));
                W.WriteValue(TEXT("rt"), Finite(Event.RealTime));
                W.WriteValue(TEXT("round"), Event.Round);
                W.WriteValue(TEXT("value"), Event.Value);
                break;
            case FDemoEvent::EType::ShotHit:
				W.WriteValue(TEXT("type"), TEXT("shot_hit"));
				W.WriteValue(TEXT("t"), Finite(Event.Time));
				W.WriteValue(TEXT("rt"), Finite(Event.RealTime));
				W.WriteValue(TEXT("who"), Event.Who.ToString());
				W.WriteValue(TEXT("whom"), Event.Whom.ToString());
				W.WriteValue(TEXT("weapon"), Event.Weapon.ToString());
				W.WriteValue(TEXT("dmg"), Round(Event.Amount, 10.));
				W.WriteValue(TEXT("dist"), Round(Event.DistanceCm));
				break;
			case FDemoEvent::EType::Damage:
				W.WriteValue(TEXT("type"), TEXT("damage"));
				W.WriteValue(TEXT("t"), Finite(Event.Time));
				W.WriteValue(TEXT("rt"), Finite(Event.RealTime));
				W.WriteValue(TEXT("who"), Event.Who.ToString());
				W.WriteValue(TEXT("whom"), Event.Whom.ToString());
				W.WriteValue(TEXT("weapon"), Event.Weapon.ToString());
				W.WriteValue(TEXT("dmg"), Round(Event.Amount, 10.));
				W.WriteValue(TEXT("dist"), Round(Event.DistanceCm));
				break;
			case FDemoEvent::EType::Kill:
			case FDemoEvent::EType::Down:
				W.WriteValue(TEXT("type"), Event.Type == FDemoEvent::EType::Kill ? TEXT("kill") : TEXT("down"));
				W.WriteValue(TEXT("t"), Finite(Event.Time));
				W.WriteValue(TEXT("rt"), Finite(Event.RealTime));
				W.WriteValue(TEXT("killer"), Event.Who.ToString());
				W.WriteValue(TEXT("victim"), Event.Whom.ToString());
				W.WriteValue(TEXT("weapon"), Event.Weapon.ToString());
				W.WriteValue(TEXT("dist"), Round(Event.DistanceCm));
				if (Event.bContextKnown)
				{
					W.WriteValue(TEXT("was_victim_aware"), Event.bFlag);
					W.WriteValue(TEXT("victim_covered_by_ally"), Event.bCoveredByAlly);
				}
				else
				{
					W.WriteNull(TEXT("was_victim_aware"));
					W.WriteNull(TEXT("victim_covered_by_ally"));
				}
				break;
			case FDemoEvent::EType::Stealth:
				W.WriteValue(TEXT("type"), Event.bFlag ? TEXT("stealth_on") : TEXT("stealth_off"));
				W.WriteValue(TEXT("t"), Finite(Event.Time));
				W.WriteValue(TEXT("rt"), Finite(Event.RealTime));
				W.WriteValue(TEXT("op"), Event.Who.ToString());
				break;
			case FDemoEvent::EType::Cover:
				W.WriteValue(TEXT("type"), Event.bFlag ? TEXT("cover_enter") : TEXT("cover_leave"));
				W.WriteValue(TEXT("t"), Finite(Event.Time));
				W.WriteValue(TEXT("rt"), Finite(Event.RealTime));
				W.WriteValue(TEXT("op"), Event.Who.ToString());
				W.WriteValue(TEXT("height"), Event.Kind.ToString());
				break;
			case FDemoEvent::EType::Climb:
				W.WriteValue(TEXT("type"), TEXT("climb"));
				W.WriteValue(TEXT("t"), Finite(Event.Time));
				W.WriteValue(TEXT("rt"), Finite(Event.RealTime));
				W.WriteValue(TEXT("op"), Event.Who.ToString());
				W.WriteValue(TEXT("kind"), Event.Kind.ToString());
				W.WriteValue(TEXT("height_cm"), Round(Event.Amount));
				break;
			}
		});
	};
	int32 Orders = 0;
	int32 Snaps = 0;
	for (int32 Index : Indices)
	{
		while (RoundCursor < RoundIndices.Num() && Finite(Session.Rounds[RoundIndices[RoundCursor]].Time) <= Finite(Session.Frames[Index].Time))
		{
			EmitRound(RoundIndices[RoundCursor]);
			++RoundCursor;
		}
		while (EventCursor < EventIndices.Num() && Finite(Session.Events[EventIndices[EventCursor]].Time) <= Finite(Session.Frames[Index].Time))
		{
			EmitEvent(EventIndices[EventCursor]);
			++EventCursor;
		}
		const FDemoFrame& F = Session.Frames[Index];
		if (F.Type == FDemoFrame::EType::Order && F.Facts.Kind == EDemoOrderKind::Suppress && !F.Facts.bAccepted) { continue; }
		Line([&](FWriter& W)
		{
			const bool bOrder = F.Type == FDemoFrame::EType::Order;
			const bool bMode = F.Type == FDemoFrame::EType::Mode;
			W.WriteValue(TEXT("type"), bOrder ? TEXT("order") : bMode ? TEXT("mode") : TEXT("snap"));
			W.WriteValue(TEXT("t"), Finite(F.Time));
			W.WriteValue(TEXT("rt"), Finite(F.RealTime));
			if (bMode)
			{
				const TCHAR* Names[] = { TEXT("solo"), TEXT("autonomy"), TEXT("cease_fire"), TEXT("pause") };
				W.WriteValue(TEXT("what"), Names[static_cast<int32>(F.Mode)]);
				W.WriteValue(TEXT("on"), F.bModeOn);
				return;
			}
			if (bOrder)
			{
				++Orders;
				const FDemoMapping Map = MapToPmcOrder(F.Facts);
				W.WriteValue(TEXT("kind"), KindName(F.Facts.Kind));
				if (Map.Order) { W.WriteValue(TEXT("pmc_order"), Map.Order); }
				else { W.WriteNull(TEXT("pmc_order")); }
				W.WriteValue(TEXT("unmapped"), Map.Unmapped);
				W.WriteValue(TEXT("ok"), F.Facts.bAccepted);
				W.WriteValue(TEXT("plan"), F.bPlan);
			}
			W.WriteValue(TEXT("mode"), F.bSolo ? TEXT("solo") : TEXT("formation"));
			if (bOrder)
			{
				W.WriteValue(TEXT("actor"), F.Actor.ToString());
				W.WriteArrayStart(TEXT("sel"));
				for (int32 I = 0; I < F.NumOps; ++I)
				{
					const FDemoOp& Op = Session.Ops[F.FirstOp + I];
					if (Op.bSelected) { W.WriteValue(Op.Id.ToString()); }
				}
				W.WriteArrayEnd();
				W.WriteValue(TEXT("sprint"), F.bSprint);
				Position(W, TEXT("dest"), F.Destination);
				if (F.CoverPoint)
				{
					W.WriteObjectStart(TEXT("cover"));
					Position(W, TEXT("p"), F.CoverPoint);
					W.WriteValue(TEXT("yaw"), Round(F.CoverYaw, 10.));
					W.WriteValue(TEXT("h"), F.CoverHeight);
					W.WriteObjectEnd();
				}
				else { W.WriteNull(TEXT("cover")); }
				if (!F.Target.Id.IsNone())
				{
					W.WriteObjectStart(TEXT("target"));
					TargetFields(W, F.Target);
					W.WriteObjectEnd();
				}
				else { W.WriteNull(TEXT("target")); }
				W.WriteObjectStart(TEXT("facts"));
				W.WriteValue(TEXT("thr_d"), Round(F.Facts.ThreatDistance));
				W.WriteValue(TEXT("thr_d_new"), Round(F.Facts.ThreatDistanceNew));
				W.WriteValue(TEXT("flank_deg"), Round(F.Facts.FlankDegrees, 10.));
				W.WriteValue(TEXT("goal_d"), Round(F.Facts.GoalDistance));
				W.WriteValue(TEXT("goal_d_new"), Round(F.Facts.GoalDistanceNew));
				W.WriteObjectEnd();
			}
			else
			{
				++Snaps;
				W.WriteValue(TEXT("formation"), F.Formation.ToString());
				W.WriteValue(TEXT("paused"), F.bPaused);
				W.WriteValue(TEXT("autonomy"), F.bAutonomy);
			}
			W.WriteArrayStart(TEXT("ops"));
			for (int32 I = 0; I < F.NumOps; ++I)
			{
				const FDemoOp& Op = Session.Ops[F.FirstOp + I];
				W.WriteObjectStart();
				W.WriteValue(TEXT("id"), Op.Id.ToString());
				W.WriteValue(TEXT("alive"), Op.bAlive ? 1 : 0);
				Position(W, TEXT("p"), Op.Position);
				W.WriteValue(TEXT("yaw"), Round(Op.Yaw, 10.));
				W.WriteValue(TEXT("look_yaw"), Round(Op.LookYaw, 10.));
				W.WriteValue(TEXT("st"), static_cast<int32>(Op.Stance));
				W.WriteValue(TEXT("mv"), Op.bMoving);
				W.WriteValue(TEXT("cov"), Op.bCover);
				W.WriteValue(TEXT("in_cover"), Op.bCover);
				W.WriteValue(TEXT("stealth"), Op.bStealth);
				W.WriteValue(TEXT("height"), Round(Op.HeightAboveGroundCm));
				W.WriteValue(TEXT("seen_by_enemy"), Op.SeenByEnemy);
				W.WriteValue(TEXT("under_fire"), Op.UnderFireSeconds >= 0. ? Round(Op.UnderFireSeconds, 10.) : -1.);
				W.WriteValue(TEXT("scan_pitch"), Round(Op.ScanPitchDeg, 10.));
				W.WriteValue(TEXT("hp"), Finite(Op.Health));
				W.WriteValue(TEXT("clip"), Op.Clip);
				W.WriteValue(TEXT("res"), Op.Reserve);
				W.WriteValue(TEXT("sel"), Op.bSelected);
				W.WriteValue(TEXT("lead"), Op.bLeader);
				W.WriteValue(TEXT("thr_n"), Op.ThreatCount);
				W.WriteValue(TEXT("thr_d"), Round(Op.ThreatDistance));
				W.WriteValue(TEXT("thr_b"), Round(Op.ThreatBearing, 10.));
				W.WriteValue(TEXT("goal_d"), Round(Op.GoalDistance));
				W.WriteValue(TEXT("lo"), Op.LastOrder.IsNone() ? FString(TEXT("none")) : Op.LastOrder.ToString());
				W.WriteValue(TEXT("lo_t"), Finite(Op.LastOrderTime));
				W.WriteObjectEnd();
			}
			W.WriteArrayEnd();
			W.WriteArrayStart(TEXT("threats"));
			for (int32 I = 0; I < F.NumThreats; ++I)
			{
				W.WriteObjectStart();
				TargetFields(W, Session.Threats[F.FirstThreat + I]);
				W.WriteObjectEnd();
			}
			W.WriteArrayEnd();
		});
	}
	while (RoundCursor < RoundIndices.Num())
	{
		EmitRound(RoundIndices[RoundCursor]);
		++RoundCursor;
	}
	while (EventCursor < EventIndices.Num())
	{
		EmitEvent(EventIndices[EventCursor]);
		++EventCursor;
	}
	Line([&](FWriter& W)
	{
		const TCHAR* Results[] = { TEXT("ABORTED"), TEXT("VICTORY"), TEXT("DEFEAT") };
		W.WriteValue(TEXT("type"), TEXT("footer"));
		W.WriteValue(TEXT("duration_s"), Finite(Session.Duration));
		W.WriteValue(TEXT("orders"), Orders);
		W.WriteValue(TEXT("orders_ok"), Session.AcceptedOrders);
		W.WriteValue(TEXT("snaps"), Snaps);
		W.WriteValue(TEXT("result"), Results[static_cast<int32>(Session.Result)]);
		W.WriteValue(TEXT("alive"), Session.Alive);
		W.WriteValue(TEXT("truncated"), Session.bTruncated);
		W.WriteValue(TEXT("mark"), Session.Mark);
		if (Session.Rounds.Num() > 0)
		{
			int32 Ends = 0;
			int32 Victory = 0;
			int32 Defeat = 0;
			int32 Draw = 0;
			for (const FDemoRoundEvent& Event : Session.Rounds)
			{
				if (!Event.bStart && !Event.bPmcRelocated)
				{
					++Ends;
					Victory = Event.ScoreVictory;
					Defeat = Event.ScoreDefeat;
					Draw = Event.ScoreDraw;
				}
			}
			W.WriteValue(TEXT("rounds"), Ends);
			W.WriteValue(TEXT("score_victory"), Victory);
			W.WriteValue(TEXT("score_defeat"), Defeat);
			W.WriteValue(TEXT("score_draw"), Draw);
		}
	});
	return Out;
}

int32 SessionRecordingRules::ChooseMarkRound(const FDemoSession& Session, double Now, bool bContact, bool bPrevious, int32 ExplicitRound)
{
 int32 Current = INDEX_NONE, Previous = INDEX_NONE;
 double Started = 0.;
 bool bActive = false;
 for (const FDemoRoundEvent& Round : Session.Rounds)
 {
  if (Round.bPmcRelocated || Round.Time > Now) { continue; }
  if (ExplicitRound != INDEX_NONE && Round.Round == ExplicitRound) { return ExplicitRound; }
  if (Round.bStart)
  {
   Previous = Current;
   Current = Round.Round;
   Started = Round.Time;
   bActive = true;
  }
  else { Current = Round.Round; bActive = false; }
 }
 if (ExplicitRound != INDEX_NONE) { return INDEX_NONE; }
 if (!bActive) { return Current; } // The last completed round during intermission.
 if (bPrevious || (Now - Started < 30. && !bContact))
 {
  if (Previous != INDEX_NONE) { return Previous; }
  if (bPrevious) { return INDEX_NONE; }
 }
 return Current;
}
