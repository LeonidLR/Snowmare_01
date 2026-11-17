#include "LevelGen/TacticalMapRules.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

const TCHAR* const TacticalMapRules::Schema = TEXT("codex.tactical_map/1");
const TCHAR* const TacticalMapRules::ConfigSchema = TEXT("codex.tactical_runtime/1");

namespace TacticalMapRulesLocal
{
	using FJsonArray = TArray<TSharedPtr<FJsonValue>>;

	bool IsNumber(const TSharedPtr<FJsonValue>& Value, double& Out)
	{
		if (!Value.IsValid() || Value->Type != EJson::Number)
		{
			return false;
		}
		Out = Value->AsNumber();
		return FMath::IsFinite(Out);
	}

	/** A number field; Default when missing / null / not a number. */
	float Num(const FJsonObject& Object, const TCHAR* Key, float Default)
	{
		double Value = 0.0;
		return IsNumber(Object.TryGetField(Key), Value) ? static_cast<float>(Value) : Default;
	}

	FString Str(const FJsonObject& Object, const TCHAR* Key)
	{
		FString Value;
		Object.TryGetStringField(Key, Value);
		return Value;
	}

	bool Bool(const FJsonObject& Object, const TCHAR* Key, bool Default)
	{
		bool Value = Default;
		return Object.TryGetBoolField(Key, Value) ? Value : Default;
	}

	/** [x, y] or [x, y, z] -> a vector (z 0 when absent). */
	bool Vec(const TSharedPtr<FJsonValue>& Value, FVector& Out)
	{
		if (!Value.IsValid() || Value->Type != EJson::Array)
		{
			return false;
		}
		const FJsonArray& A = Value->AsArray();
		double X = 0.0, Y = 0.0, Z = 0.0;
		if (A.Num() < 2 || A.Num() > 3 || !IsNumber(A[0], X) || !IsNumber(A[1], Y) || (A.Num() == 3 && !IsNumber(A[2], Z)))
		{
			return false;
		}
		Out = FVector(X, Y, Z);
		return true;
	}

	bool VecField(const FJsonObject& Object, const TCHAR* Key, FVector& Out)
	{
		return Vec(Object.TryGetField(Key), Out);
	}

	TArray<FVector> VecArray(const FJsonObject& Object, const TCHAR* Key)
	{
		TArray<FVector> Out;
		const FJsonArray* A = nullptr;
		if (Object.TryGetArrayField(Key, A) && A)
		{
			for (const TSharedPtr<FJsonValue>& Item : *A)
			{
				FVector Point;
				if (Vec(Item, Point))
				{
					Out.Add(Point);
				}
			}
		}
		return Out;
	}

	TArray<FVector2D> Vec2Array(const FJsonObject& Object, const TCHAR* Key)
	{
		TArray<FVector2D> Out;
		for (const FVector& Point : VecArray(Object, Key))
		{
			Out.Add(FVector2D(Point.X, Point.Y));
		}
		return Out;
	}

	TArray<FString> StrArray(const FJsonObject& Object, const TCHAR* Key)
	{
		TArray<FString> Out;
		const FJsonArray* A = nullptr;
		if (Object.TryGetArrayField(Key, A) && A)
		{
			for (const TSharedPtr<FJsonValue>& Item : *A)
			{
				if (Item.IsValid() && Item->Type == EJson::String)
				{
					Out.Add(Item->AsString());
				}
			}
		}
		return Out;
	}

	/** Each object of a top-level list; a missing list is reported, a non-object item skipped with an error. */
	template <typename FnType>
	void ForEachItem(const FJsonObject& Root, const TCHAR* Key, TArray<FString>& Errors, FnType&& Fn)
	{
		const FJsonArray* A = nullptr;
		if (!Root.TryGetArrayField(Key, A) || !A)
		{
			Errors.Add(FString::Printf(TEXT("%s: missing (read as empty)"), Key));
			return;
		}
		for (int32 Index = 0; Index < A->Num(); ++Index)
		{
			const TSharedPtr<FJsonValue>& Item = (*A)[Index];
			if (!Item.IsValid() || Item->Type != EJson::Object || !Item->AsObject().IsValid())
			{
				Errors.Add(FString::Printf(TEXT("%s[%d]: expected an object"), Key, Index));
				continue;
			}
			if (!Fn(*Item->AsObject()))
			{
				Errors.Add(FString::Printf(TEXT("%s[%d]: skipped (needs an id / a building and its points)"), Key, Index));
			}
		}
	}

	TArray<FName> NameArray(const FJsonObject& Object, const TCHAR* Key)
	{
		TArray<FName> Out;
		for (const FString& S : StrArray(Object, Key))
		{
			Out.Add(FName(*S));
		}
		return Out;
	}

	float Dist2D(const FVector& A, const FVector& B)
	{
		return static_cast<float>(FVector2D::Distance(FVector2D(A.X, A.Y), FVector2D(B.X, B.Y)));
	}

	/** Distance from P to the segment A-B (2D) and the parameter t of the nearest point (0..1). */
	float DistToSegment2D(const FVector& P, const FVector& A, const FVector& B, float& OutT)
	{
		const FVector2D A2(A.X, A.Y), B2(B.X, B.Y), P2(P.X, P.Y), AB = B2 - A2;
		const double L2 = AB.SizeSquared();
		OutT = L2 <= KINDA_SMALL_NUMBER ? 0.f : static_cast<float>(FMath::Clamp(FVector2D::DotProduct(P2 - A2, AB) / L2, 0.0, 1.0));
		return static_cast<float>(FVector2D::Distance(P2, A2 + AB * OutT));
	}

	bool InsidePolygon(const FVector2D& Point, TConstArrayView<FVector2D> Polygon)
	{
		const int32 Count = Polygon.Num();
		if (Count < 3)
		{
			return false;
		}
		bool bInside = false;
		for (int32 I = 0, J = Count - 1; I < Count; J = I++)
		{
			const FVector2D& A = Polygon[I];
			const FVector2D& B = Polygon[J];
			if ((A.Y > Point.Y) != (B.Y > Point.Y))
			{
				const double CrossX = (B.X - A.X) * (Point.Y - A.Y) / (B.Y - A.Y) + A.X;
				if (Point.X < CrossX)
				{
					bInside = !bInside;
				}
			}
		}
		return bInside;
	}

	void AddKind(TArray<FName>& Kinds, FName Kind)
	{
		Kinds.AddUnique(Kind);
	}

	/** The point lies inside a place (same floor, inside its outline) whose score is at least MinScore. */
	void ZonesContaining(const FTacticalMapFile& File, const FTacticalRuntimeConfig& Config, const FVector& Point, float MinScore, TArray<int32>& Out, TBitArray<>& Seen)
	{
		const FVector2D P2(Point.X, Point.Y);
		for (int32 Index = 0; Index < File.Chokepoints.Num(); ++Index)
		{
			const FTacticalChokepoint& Zone = File.Chokepoints[Index];
			if (Seen[Index] || Zone.Score < MinScore || !Zone.Bounds.bIsValid || !Zone.Bounds.IsInside(P2)
				|| !TacticalMapRules::IsSameFloor(Point.Z, Zone.Location.Z, Config.FloorToleranceCm) || !InsidePolygon(P2, Zone.Poly))
			{
				continue;
			}
			Seen[Index] = true;
			Out.Add(Index);
		}
	}

	/** The path sampled every Step cm (each vertex included). */
	TArray<FVector> SamplePath(TConstArrayView<FVector> Path, float Step)
	{
		TArray<FVector> Out;
		if (Path.IsEmpty())
		{
			return Out;
		}
		Out.Add(Path[0]);
		const float SafeStep = FMath::Max(10.f, Step);
		for (int32 I = 1; I < Path.Num(); ++I)
		{
			const FVector A = Path[I - 1], B = Path[I];
			const float Length = static_cast<float>(FVector::Dist(A, B));
			const int32 Count = FMath::Max(1, FMath::CeilToInt(Length / SafeStep));
			for (int32 K = 1; K <= Count; ++K)
			{
				Out.Add(FMath::Lerp(A, B, static_cast<double>(K) / Count));
			}
		}
		return Out;
	}

	bool SortPicks(const FTacticalPick& A, const FTacticalPick& B)
	{
		if (A.Score != B.Score)
		{
			return A.Score > B.Score;
		}
		if (A.Index != B.Index)
		{
			return A.Index < B.Index;
		}
		return A.SpotIndex < B.SpotIndex;
	}

	bool SortPicksByDistance(const FTacticalPick& A, const FTacticalPick& B)
	{
		if (A.DistanceCm != B.DistanceCm)
		{
			return A.DistanceCm < B.DistanceCm;
		}
		return A.Index < B.Index;
	}
}

bool TacticalMapRules::Parse(const FString& Json, FTacticalMapFile& Out, TArray<FString>& OutErrors)
{
	using namespace TacticalMapRulesLocal;
	Out = FTacticalMapFile();
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
	{
		OutErrors.Add(TEXT("not a JSON object"));
		return false;
	}
	FString SchemaId;
	if (!Root->TryGetStringField(TEXT("schema"), SchemaId) || !SchemaId.StartsWith(TEXT("codex.tactical_map/")))
	{
		OutErrors.Add(FString::Printf(TEXT("schema: expected %s, got '%s'"), Schema, *SchemaId));
		return false;
	}
	if (SchemaId != Schema)
	{
		OutErrors.Add(FString::Printf(TEXT("schema %s is not supported (this build reads %s)"), *SchemaId, Schema));
		return false;
	}
	Out.Map = Str(*Root, TEXT("map"));
	Out.Layout = Str(*Root, TEXT("layout"));
	Out.GeometryHash = Str(*Root, TEXT("geometry_hash"));
	Out.AnalysisSource = Str(*Root, TEXT("analysis_source"));
	Out.GroundZ = Num(*Root, TEXT("ground_z"), 0.f);
	const TSharedPtr<FJsonObject>* Conditions = nullptr;
	if (Root->TryGetObjectField(TEXT("conditions"), Conditions) && Conditions && Conditions->IsValid())
	{
		Out.Weather = Str(**Conditions, TEXT("weather"));
		Out.TimeOfDay = Str(**Conditions, TEXT("time_of_day"));
	}
	const TSharedPtr<FJsonObject>* Thresholds = nullptr;
	if (Root->TryGetObjectField(TEXT("thresholds"), Thresholds) && Thresholds && Thresholds->IsValid())
	{
		const FJsonObject& T = **Thresholds;
		FTacticalThresholds& O = Out.Thresholds;
		O.DropReachCm = Num(T, TEXT("drop_reach_cm"), O.DropReachCm);
		O.WindowRangeM = Num(T, TEXT("window_range_m"), O.WindowRangeM);
		O.SniperRangeM = Num(T, TEXT("sniper_range_m"), O.SniperRangeM);
		O.RevealCloseM = Num(T, TEXT("reveal_close_m"), O.RevealCloseM);
		O.DetourCriticalM = Num(T, TEXT("detour_critical_m"), O.DetourCriticalM);
		O.MinScore = Num(T, TEXT("min_score"), O.MinScore);
		O.LadderOnlyFactor = FMath::Clamp(Num(T, TEXT("ladder_only_factor"), O.LadderOnlyFactor), 0.f, 1.f);
	}

	ForEachItem(*Root, TEXT("chokepoints"), OutErrors, [&Out](const FJsonObject& J)
	{
		FTacticalChokepoint C;
		C.Id = Str(J, TEXT("id"));
		if (C.Id.IsEmpty() || !VecField(J, TEXT("pos"), C.Location))
		{
			return false;
		}
		C.Kind = FName(*Str(J, TEXT("kind")));
		C.KindName = Str(J, TEXT("kind_name"));
		C.Tags = NameArray(J, TEXT("tags"));
		C.Score = FMath::Clamp(Num(J, TEXT("score"), 0.f), 0.f, 1.f);
		C.Layer = Str(J, TEXT("layer"));
		C.Line = VecArray(J, TEXT("line"));
		C.Poly = Vec2Array(J, TEXT("poly"));
		C.LengthM = Num(J, TEXT("length_m"), 0.f);
		C.WidthM = Num(J, TEXT("width_m"), -1.f);
		C.LaneM = Num(J, TEXT("lane_m"), -1.f);
		C.Route = Str(J, TEXT("route"));
		C.Alternative = Str(J, TEXT("alternative"));
		C.DetourM = Num(J, TEXT("detour_m"), -1.f);
		C.DropSpots = StrArray(J, TEXT("drop_spots"));
		C.Windows = StrArray(J, TEXT("windows"));
		C.Overwatch = StrArray(J, TEXT("overwatch"));
		C.HidingSpots = StrArray(J, TEXT("hiding_spots"));
		C.RevealM = Num(J, TEXT("reveal_m"), -1.f);
		C.RevealStealthM = Num(J, TEXT("reveal_stealth_m"), -1.f);
		Out.Chokepoints.Add(MoveTemp(C));
		return true;
	});
	ForEachItem(*Root, TEXT("drop_spots"), OutErrors, [&Out](const FJsonObject& J)
	{
		FTacticalDropSpot D;
		D.Id = Str(J, TEXT("id"));
		if (D.Id.IsEmpty() || !VecField(J, TEXT("edge"), D.Edge) || !VecField(J, TEXT("land"), D.Land))
		{
			return false;
		}
		D.Surface = Str(J, TEXT("surface"));
		D.bWallTop = Str(J, TEXT("kind")) == TEXT("wall_top");
		D.Layer = Str(J, TEXT("layer"));
		D.DropCm = Num(J, TEXT("drop_cm"), 0.f);
		D.GapCm = Num(J, TEXT("gap_cm"), 0.f);
		D.Access = Str(J, TEXT("access"));
		D.bLadderOnly = Bool(J, TEXT("ladder_only"), D.Access == TEXT("ladder"));
		D.Jumpers = StrArray(J, TEXT("jumpers"));
		D.Line = Str(J, TEXT("line"));
		D.Places = StrArray(J, TEXT("places"));
		Out.DropSpots.Add(MoveTemp(D));
		return true;
	});
	ForEachItem(*Root, TEXT("windows"), OutErrors, [&Out](const FJsonObject& J)
	{
		FTacticalWindow W;
		W.Id = Str(J, TEXT("id"));
		if (W.Id.IsEmpty() || !VecField(J, TEXT("pos"), W.Location))
		{
			return false;
		}
		if (!VecField(J, TEXT("stand"), W.Stand))
		{
			W.Stand = W.Location;
		}
		W.Building = Str(J, TEXT("building"));
		W.Floor = FMath::RoundToInt(Num(J, TEXT("floor"), 0.f));
		W.Layer = Str(J, TEXT("layer"));
		W.Kind = Str(J, TEXT("kind"));
		W.Yaw = Num(J, TEXT("yaw"), 0.f);
		W.CoveredM = Num(J, TEXT("covered_m"), 0.f);
		W.MinDistM = Num(J, TEXT("min_dist_m"), 0.f);
		W.Places = StrArray(J, TEXT("places"));
		Out.Windows.Add(MoveTemp(W));
		return true;
	});
	ForEachItem(*Root, TEXT("overwatch_roofs"), OutErrors, [&Out](const FJsonObject& J)
	{
		FTacticalOverwatchRoof R;
		R.Building = Str(J, TEXT("building"));
		if (R.Building.IsEmpty())
		{
			return false;
		}
		R.Layer = Str(J, TEXT("layer"));
		R.Level = FMath::RoundToInt(Num(J, TEXT("level"), 0.f));
		R.Z = Num(J, TEXT("z"), 0.f);
		R.HeightCm = Num(J, TEXT("height_cm"), 0.f);
		R.RangeM = Num(J, TEXT("range_m"), 0.f);
		R.Access = StrArray(J, TEXT("access"));
		R.AccessKind = Str(J, TEXT("access_kind"));
		R.bLadderOnly = Bool(J, TEXT("ladder_only"), false);
		R.Places = StrArray(J, TEXT("places"));
		const FJsonArray* Spots = nullptr;
		if (J.TryGetArrayField(TEXT("spots"), Spots) && Spots)
		{
			for (const TSharedPtr<FJsonValue>& SpotValue : *Spots)
			{
				const TSharedPtr<FJsonObject> S = SpotValue.IsValid() && SpotValue->Type == EJson::Object ? SpotValue->AsObject() : nullptr;
				FTacticalOverwatchSpot Spot;
				if (S.IsValid() && VecField(*S, TEXT("pos"), Spot.Location))
				{
					Spot.FacingYaw = Num(*S, TEXT("facing_yaw"), 0.f);
					Spot.CoveredM = Num(*S, TEXT("covered_m"), 0.f);
					Spot.LongestLosM = Num(*S, TEXT("longest_los_m"), 0.f);
					R.Spots.Add(Spot);
				}
			}
		}
		Out.OverwatchRoofs.Add(MoveTemp(R));
		return true;
	});
	ForEachItem(*Root, TEXT("hiding_spots"), OutErrors, [&Out](const FJsonObject& J)
	{
		FTacticalHidingSpot H;
		H.Id = Str(J, TEXT("id"));
		if (H.Id.IsEmpty() || !VecField(J, TEXT("pos"), H.Location))
		{
			return false;
		}
		H.Layer = Str(J, TEXT("layer"));
		H.FacingYaw = Num(J, TEXT("facing_yaw"), 0.f);
		H.RevealNormalM = Num(J, TEXT("reveal_normal_m"), 0.f);
		H.RevealStealthM = Num(J, TEXT("reveal_stealth_m"), 0.f);
		H.bStealthOnly = Bool(J, TEXT("stealth_only"), false);
		H.bBlind = Bool(J, TEXT("blind"), false);
		H.RushM = Num(J, TEXT("rush_m"), 0.f);
		H.Cover = Str(J, TEXT("cover"));
		H.CoverHeightCm = Num(J, TEXT("cover_height_cm"), 0.f);
		H.Place = Str(J, TEXT("place"));
		H.ThreatLocation = H.Location;
		const TSharedPtr<FJsonObject>* Threat = nullptr;
		if (J.TryGetObjectField(TEXT("threatens"), Threat) && Threat && Threat->IsValid())
		{
			H.ThreatLine = Str(**Threat, TEXT("line"));
			H.ThreatRoute = Str(**Threat, TEXT("route"));
			VecField(**Threat, TEXT("pos"), H.ThreatLocation);
		}
		Out.HidingSpots.Add(MoveTemp(H));
		return true;
	});
	ForEachItem(*Root, TEXT("ladder_only_roofs"), OutErrors, [&Out](const FJsonObject& J)
	{
		FTacticalLadderRoof L;
		L.Building = Str(J, TEXT("building"));
		if (L.Building.IsEmpty() || !VecField(J, TEXT("center"), L.Center))
		{
			return false;
		}
		L.Layer = Str(J, TEXT("layer"));
		L.Z = Num(J, TEXT("z"), static_cast<float>(L.Center.Z));
		L.Poly = Vec2Array(J, TEXT("poly"));
		L.Ladders = StrArray(J, TEXT("ladders"));
		L.LadderTops = VecArray(J, TEXT("ladder_tops"));
		L.bOverwatch = Bool(J, TEXT("overwatch"), false);
		L.DropSpots = StrArray(J, TEXT("drop_spots"));
		Out.LadderOnlyRoofs.Add(MoveTemp(L));
		return true;
	});
	UpdateDerived(Out);
	return true;
}

void TacticalMapRules::UpdateDerived(FTacticalMapFile& File)
{
	TMap<FString, int32> PlaceIndex;
	for (int32 Index = 0; Index < File.Chokepoints.Num(); ++Index)
	{
		FTacticalChokepoint& Zone = File.Chokepoints[Index];
		Zone.Bounds = Zone.Poly.Num() >= 3 ? FBox2D(Zone.Poly) : FBox2D(ForceInit);
		PlaceIndex.FindOrAdd(Zone.Id, Index);
	}
	for (FTacticalHidingSpot& Spot : File.HidingSpots)
	{
		const int32* Found = Spot.Place.IsEmpty() ? nullptr : PlaceIndex.Find(Spot.Place);
		Spot.PlaceIndex = Found ? *Found : INDEX_NONE;
	}
}

bool TacticalMapRules::ParseConfig(const FString& Json, FTacticalRuntimeConfig& Out, TArray<FString>& OutErrors)
{
	using namespace TacticalMapRulesLocal;
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
	{
		OutErrors.Add(TEXT("not a JSON object"));
		return false;
	}
	FString SchemaId;
	if (!Root->TryGetStringField(TEXT("schema"), SchemaId) || SchemaId != ConfigSchema)
	{
		OutErrors.Add(FString::Printf(TEXT("schema: expected %s, got '%s'"), ConfigSchema, *SchemaId));
		return false;
	}
	FTacticalRuntimeConfig& C = Out;
	C.FloorToleranceCm = Num(*Root, TEXT("floor_tolerance_cm"), C.FloorToleranceCm);
	const auto Section = [&Root](const TCHAR* Key) -> TSharedPtr<FJsonObject>
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		return Root->TryGetObjectField(Key, Object) && Object ? *Object : MakeShared<FJsonObject>();
	};
	if (const TSharedPtr<FJsonObject> D = Section(TEXT("danger")))
	{
		C.ZoneFalloffCm = Num(*D, TEXT("zone_falloff_cm"), C.ZoneFalloffCm);
		C.DropWeight = Num(*D, TEXT("drop_weight"), C.DropWeight);
		C.WindowWeight = Num(*D, TEXT("window_weight"), C.WindowWeight);
		C.SniperWeight = Num(*D, TEXT("sniper_weight"), C.SniperWeight);
		C.HidingWeight = Num(*D, TEXT("hiding_weight"), C.HidingWeight);
		C.bScaleLadderOnlyDrops = Bool(*D, TEXT("scale_ladder_only_drops"), C.bScaleLadderOnlyDrops);
		C.WindowFullM = Num(*D, TEXT("window_full_m"), C.WindowFullM);
		C.WindowHalfFovDeg = Num(*D, TEXT("window_half_fov_deg"), C.WindowHalfFovDeg);
		C.HidingThreatRadiusCm = Num(*D, TEXT("hiding_threat_radius_cm"), C.HidingThreatRadiusCm);
	}
	if (const TSharedPtr<FJsonObject> A = Section(TEXT("ambush")))
	{
		C.AmbushSegmentRadiusCm = Num(*A, TEXT("segment_radius_cm"), C.AmbushSegmentRadiusCm);
		C.AmbushRevealWeight = Num(*A, TEXT("reveal_weight"), C.AmbushRevealWeight);
		C.AmbushExitWeight = Num(*A, TEXT("exit_weight"), C.AmbushExitWeight);
		C.AmbushPlaceWeight = Num(*A, TEXT("place_weight"), C.AmbushPlaceWeight);
		C.AmbushStealthOnlyFactor = Num(*A, TEXT("stealth_only_factor"), C.AmbushStealthOnlyFactor);
	}
	if (const TSharedPtr<FJsonObject> O = Section(TEXT("overwatch")))
	{
		C.OverwatchDistanceWeight = Num(*O, TEXT("distance_weight"), C.OverwatchDistanceWeight);
		C.OverwatchCoverageWeight = Num(*O, TEXT("coverage_weight"), C.OverwatchCoverageWeight);
		C.OverwatchCoverageFullM = FMath::Max(1.f, Num(*O, TEXT("coverage_full_m"), C.OverwatchCoverageFullM));
	}
	if (const TSharedPtr<FJsonObject> P = Section(TEXT("path")))
	{
		C.PathSampleStepCm = FMath::Max(10.f, Num(*P, TEXT("sample_step_cm"), C.PathSampleStepCm));
		C.ShortDetourM = Num(*P, TEXT("short_detour_m"), C.ShortDetourM);
	}
	if (const TSharedPtr<FJsonObject> L = Section(TEXT("ladder_roofs")))
	{
		C.LadderOnlyFactorFallback = FMath::Clamp(Num(*L, TEXT("factor_fallback"), C.LadderOnlyFactorFallback), 0.f, 1.f);
	}
	if (const TSharedPtr<FJsonObject> Dr = Section(TEXT("draw")))
	{
		C.DrawRadiusCm = Num(*Dr, TEXT("radius_cm"), C.DrawRadiusCm);
		C.DrawLiftCm = Num(*Dr, TEXT("lift_cm"), C.DrawLiftCm);
	}
	if (const TSharedPtr<FJsonObject> S = Section(TEXT("smoke")))
	{
		FVector Extent;
		if (VecField(*S, TEXT("project_extent_cm"), Extent))
		{
			C.ProjectExtentCm = Extent;
		}
		C.SurfaceToleranceCm = Num(*S, TEXT("surface_tolerance_cm"), C.SurfaceToleranceCm);
		C.SmokeHighDanger = Num(*S, TEXT("high_danger"), C.SmokeHighDanger);
		C.SmokeLowDanger = Num(*S, TEXT("low_danger"), C.SmokeLowDanger);
		C.SmokeOpenClearanceCm = Num(*S, TEXT("open_clearance_cm"), C.SmokeOpenClearanceCm);
	}
	return true;
}

float TacticalMapRules::DistanceToPolygon(const FVector2D& Point, TConstArrayView<FVector2D> Polygon)
{
	using namespace TacticalMapRulesLocal;
	if (Polygon.IsEmpty())
	{
		return TNumericLimits<float>::Max();
	}
	if (InsidePolygon(Point, Polygon))
	{
		return 0.f;
	}
	double Best = TNumericLimits<double>::Max();
	for (int32 I = 0; I < Polygon.Num(); ++I)
	{
		const FVector2D A = Polygon[I], B = Polygon[(I + 1) % Polygon.Num()], AB = B - A;
		const double L2 = AB.SizeSquared();
		const double T = L2 <= KINDA_SMALL_NUMBER ? 0.0 : FMath::Clamp(FVector2D::DotProduct(Point - A, AB) / L2, 0.0, 1.0);
		Best = FMath::Min(Best, FVector2D::Distance(Point, A + AB * T));
	}
	return static_cast<float>(Best);
}

bool TacticalMapRules::IsSameFloor(float ZA, float ZB, float ToleranceCm)
{
	return FMath::Abs(ZA - ZB) <= FMath::Max(0.f, ToleranceCm);
}

TArray<int32> TacticalMapRules::GetZonesNear(const FTacticalMapFile& File, const FTacticalRuntimeConfig& Config, const FVector& Point, float RadiusCm)
{
	TArray<TPair<float, int32>> Found;
	for (int32 Index = 0; Index < File.Chokepoints.Num(); ++Index)
	{
		const FTacticalChokepoint& Zone = File.Chokepoints[Index];
		if (!IsSameFloor(Point.Z, Zone.Location.Z, Config.FloorToleranceCm))
		{
			continue;
		}
		const float Distance = Zone.Poly.Num() >= 3 ? DistanceToPolygon(FVector2D(Point.X, Point.Y), Zone.Poly) : TacticalMapRulesLocal::Dist2D(Point, Zone.Location);
		if (Distance <= FMath::Max(0.f, RadiusCm))
		{
			Found.Emplace(Distance, Index);
		}
	}
	Found.Sort([&File](const TPair<float, int32>& A, const TPair<float, int32>& B)
	{
		if (A.Key != B.Key)
		{
			return A.Key < B.Key;
		}
		const FTacticalChokepoint& ZA = File.Chokepoints[A.Value];
		const FTacticalChokepoint& ZB = File.Chokepoints[B.Value];
		if (ZA.Score != ZB.Score)
		{
			return ZA.Score > ZB.Score;
		}
		return ZA.Id < ZB.Id;
	});
	TArray<int32> Out;
	for (const TPair<float, int32>& Pair : Found)
	{
		Out.Add(Pair.Value);
	}
	return Out;
}

FTacticalDanger TacticalMapRules::DangerAt(const FTacticalMapFile& File, const FTacticalRuntimeConfig& Config, const FVector& Point, ETacticalSide Side)
{
	using namespace TacticalMapRulesLocal;
	FTacticalDanger Out;
	const FVector2D P2(Point.X, Point.Y);
	// the places
	int32 BestZone = INDEX_NONE;
	for (int32 Index = 0; Index < File.Chokepoints.Num(); ++Index)
	{
		const FTacticalChokepoint& Zone = File.Chokepoints[Index];
		if (Zone.Poly.Num() < 3 || !Zone.Bounds.bIsValid || !Zone.Bounds.ExpandBy(static_cast<double>(FMath::Max(0.f, Config.ZoneFalloffCm))).IsInside(P2)
			|| !IsSameFloor(Point.Z, Zone.Location.Z, Config.FloorToleranceCm))
		{
			continue;
		}
		const float Distance = DistanceToPolygon(P2, Zone.Poly);
		const float Term = Distance <= 0.f ? Zone.Score
			: (Config.ZoneFalloffCm > 0.f && Distance < Config.ZoneFalloffCm ? Zone.Score * (1.f - Distance / Config.ZoneFalloffCm) : 0.f);
		if (Term > Out.ZoneTerm)
		{
			Out.ZoneTerm = Term;
			BestZone = Index;
		}
	}
	if (BestZone != INDEX_NONE)
	{
		Out.ZoneId = File.Chokepoints[BestZone].Id;
		for (const FName Tag : File.Chokepoints[BestZone].Tags)
		{
			AddKind(Out.Kinds, Tag);
		}
		AddKind(Out.Kinds, File.Chokepoints[BestZone].Kind);
	}
	float Safe = 1.f - Out.ZoneTerm;
	if (Side == ETacticalSide::Squad)
	{
		// drop ambush: under a jumper's landing reach
		float Drop = 0.f;
		for (const FTacticalDropSpot& Spot : File.DropSpots)
		{
			if (IsSameFloor(Point.Z, Spot.Land.Z, Config.FloorToleranceCm) && Dist2D(Point, Spot.Land) <= File.Thresholds.DropReachCm)
			{
				const float Factor = Spot.bLadderOnly && Config.bScaleLadderOnlyDrops ? File.Thresholds.LadderOnlyFactor : 1.f;
				Drop = FMath::Max(Drop, Config.DropWeight * Factor);
			}
		}
		// window fire: inside a window's sector and range
		float Window = 0.f;
		const float WindowRange = File.Thresholds.WindowRangeM * 100.f, WindowFull = FMath::Min(Config.WindowFullM * 100.f, WindowRange);
		const float CosFov = FMath::Cos(FMath::DegreesToRadians(Config.WindowHalfFovDeg));
		for (const FTacticalWindow& W : File.Windows)
		{
			const FVector2D To = P2 - FVector2D(W.Location.X, W.Location.Y);
			const float Distance = static_cast<float>(To.Size());
			if (Distance > WindowRange || Distance < 1.f)
			{
				continue;
			}
			const FVector2D Dir(FMath::Cos(FMath::DegreesToRadians(W.Yaw)), FMath::Sin(FMath::DegreesToRadians(W.Yaw)));
			if (FVector2D::DotProduct(Dir, To / Distance) < CosFov)
			{
				continue;
			}
			const float Value = Distance <= WindowFull ? 1.f : 1.f - (Distance - WindowFull) / FMath::Max(1.f, WindowRange - WindowFull);
			Window = FMath::Max(Window, Config.WindowWeight * Value);
		}
		// marksman on a roof: within a roof spot's range
		float Sniper = 0.f;
		for (const FTacticalOverwatchRoof& Roof : File.OverwatchRoofs)
		{
			const double Range2 = FMath::Square(static_cast<double>(Roof.RangeM) * 100.0);
			for (const FTacticalOverwatchSpot& Spot : Roof.Spots)
			{
				if (FVector::DistSquared2D(Point, Spot.Location) <= Range2)
				{
					Sniper = Config.SniperWeight;
					break;
				}
			}
			if (Sniper > 0.f)
			{
				break;
			}
		}
		// a hidden enemy: next to the route point a hiding spot threatens
		float Hiding = 0.f;
		for (const FTacticalHidingSpot& Spot : File.HidingSpots)
		{
			if (IsSameFloor(Point.Z, Spot.ThreatLocation.Z, Config.FloorToleranceCm) && Dist2D(Point, Spot.ThreatLocation) <= Config.HidingThreatRadiusCm)
			{
				Hiding = FMath::Max(Hiding, Config.HidingWeight * (Spot.bStealthOnly ? Config.AmbushStealthOnlyFactor : 1.f));
			}
		}
		const TPair<float, FName> Threats[] = {
			{ Drop, FName(TEXT("drop_ambush")) }, { Window, FName(TEXT("window_fire")) }, { Sniper, FName(TEXT("sniper")) }, { Hiding, FName(TEXT("hiding")) } };
		for (const TPair<float, FName>& Threat : Threats)
		{
			const float Term = FMath::Clamp(Threat.Key, 0.f, 1.f);
			if (Term > 0.f)
			{
				Safe *= 1.f - Term;
				AddKind(Out.Kinds, Threat.Value);
			}
		}
	}
	Out.Danger = FMath::Clamp(1.f - Safe, 0.f, 1.f);
	return Out;
}

TArray<FTacticalPick> TacticalMapRules::BestAmbushSpots(const FTacticalMapFile& File, const FTacticalRuntimeConfig& Config, const FVector& Start, const FVector& End, int32 N)
{
	using namespace TacticalMapRulesLocal;
	TArray<FTacticalPick> Picks;
	const float RevealClose = FMath::Max(0.1f, File.Thresholds.RevealCloseM);
	for (int32 Index = 0; Index < File.HidingSpots.Num(); ++Index)
	{
		const FTacticalHidingSpot& Spot = File.HidingSpots[Index];
		float T = 0.f;
		const float Distance = DistToSegment2D(Spot.ThreatLocation, Start, End, T);
		const float SegmentZ = static_cast<float>(FMath::Lerp(Start.Z, End.Z, static_cast<double>(T)));
		if (Distance > Config.AmbushSegmentRadiusCm || !IsSameFloor(SegmentZ, Spot.ThreatLocation.Z, Config.FloorToleranceCm))
		{
			continue;
		}
		const float Reveal = Spot.bStealthOnly ? Spot.RevealStealthM : Spot.RevealNormalM;
		const float PlaceScore = File.Chokepoints.IsValidIndex(Spot.PlaceIndex) ? File.Chokepoints[Spot.PlaceIndex].Score : 0.f;
		FTacticalPick Pick;
		Pick.Index = Index;
		Pick.DistanceCm = Distance;
		Pick.Score = (Config.AmbushRevealWeight * (1.f - FMath::Clamp(Reveal / RevealClose, 0.f, 1.f)) + Config.AmbushExitWeight * T
			+ Config.AmbushPlaceWeight * PlaceScore) * (Spot.bStealthOnly ? Config.AmbushStealthOnlyFactor : 1.f);
		Picks.Add(Pick);
	}
	Picks.Sort(&SortPicks);
	if (N > 0 && Picks.Num() > N)
	{
		Picks.SetNum(N);
	}
	return Picks;
}

TArray<FTacticalPick> TacticalMapRules::OverwatchSpots(const FTacticalMapFile& File, const FTacticalRuntimeConfig& Config, const FVector& Target, float RadiusCm, int32 N)
{
	using namespace TacticalMapRulesLocal;
	TArray<FTacticalPick> Picks;
	for (int32 RoofIndex = 0; RoofIndex < File.OverwatchRoofs.Num(); ++RoofIndex)
	{
		const FTacticalOverwatchRoof& Roof = File.OverwatchRoofs[RoofIndex];
		const float Reach = Roof.RangeM * 100.f + FMath::Max(0.f, RadiusCm);
		for (int32 SpotIndex = 0; SpotIndex < Roof.Spots.Num(); ++SpotIndex)
		{
			const FTacticalOverwatchSpot& Spot = Roof.Spots[SpotIndex];
			const float Distance = Dist2D(Spot.Location, Target);
			if (Reach <= 0.f || Distance > Reach)
			{
				continue;
			}
			FTacticalPick Pick;
			Pick.Index = RoofIndex;
			Pick.SpotIndex = SpotIndex;
			Pick.DistanceCm = Distance;
			Pick.Score = Config.OverwatchDistanceWeight * (1.f - Distance / Reach)
				+ Config.OverwatchCoverageWeight * FMath::Min(1.f, Spot.CoveredM / Config.OverwatchCoverageFullM);
			Picks.Add(Pick);
		}
	}
	Picks.Sort(&SortPicks);
	if (N > 0 && Picks.Num() > N)
	{
		Picks.SetNum(N);
	}
	return Picks;
}

TArray<FTacticalPick> TacticalMapRules::WindowPositions(const FTacticalMapFile& File, const FTacticalRuntimeConfig& Config, const FVector& Target, float RadiusCm)
{
	TArray<FTacticalPick> Picks;
	const float Range = File.Thresholds.WindowRangeM * 100.f, Radius = FMath::Max(0.f, RadiusCm);
	for (int32 Index = 0; Index < File.Windows.Num(); ++Index)
	{
		const FTacticalWindow& W = File.Windows[Index];
		const FVector2D To = FVector2D(Target.X, Target.Y) - FVector2D(W.Location.X, W.Location.Y);
		const float Distance = static_cast<float>(To.Size());
		if (Distance - Radius > Range)
		{
			continue;
		}
		if (Distance > Radius)
		{
			// the area round the target overlaps the fire sector: its angle off the yaw minus the area's angular half width
			const float Off = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(FVector2D::DotProduct(
				FVector2D(FMath::Cos(FMath::DegreesToRadians(W.Yaw)), FMath::Sin(FMath::DegreesToRadians(W.Yaw))), To / Distance)), -1.f, 1.f)));
			const float Slack = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Radius / Distance, 0.f, 1.f)));
			if (Off - Slack > Config.WindowHalfFovDeg)
			{
				continue;
			}
		}
		FTacticalPick Pick;
		Pick.Index = Index;
		Pick.DistanceCm = Distance;
		Pick.Score = 1.f - FMath::Clamp(Distance / FMath::Max(1.f, Range + Radius), 0.f, 1.f);
		Picks.Add(Pick);
	}
	Picks.Sort(&TacticalMapRulesLocal::SortPicksByDistance);
	return Picks;
}

TArray<FTacticalPick> TacticalMapRules::DropSpotsAbove(const FTacticalMapFile& File, const FTacticalRuntimeConfig& Config, const FVector& Center, float RadiusCm)
{
	TArray<FTacticalPick> Picks;
	for (int32 Index = 0; Index < File.DropSpots.Num(); ++Index)
	{
		const FTacticalDropSpot& Spot = File.DropSpots[Index];
		const float Distance = TacticalMapRulesLocal::Dist2D(Spot.Land, Center);
		if (Distance <= FMath::Max(0.f, RadiusCm) && IsSameFloor(Spot.Land.Z, Center.Z, Config.FloorToleranceCm))
		{
			FTacticalPick Pick;
			Pick.Index = Index;
			Pick.DistanceCm = Distance;
			Pick.Score = RadiusCm > 0.f ? 1.f - Distance / RadiusCm : 1.f;
			Picks.Add(Pick);
		}
	}
	Picks.Sort(&TacticalMapRulesLocal::SortPicksByDistance);
	return Picks;
}

TArray<int32> TacticalMapRules::ChokepointsOnPath(const FTacticalMapFile& File, const FTacticalRuntimeConfig& Config, TConstArrayView<FVector> Path)
{
	TArray<int32> Out;
	TBitArray<> Seen(false, File.Chokepoints.Num());
	for (const FVector& Sample : TacticalMapRulesLocal::SamplePath(Path, Config.PathSampleStepCm))
	{
		TacticalMapRulesLocal::ZonesContaining(File, Config, Sample, File.Thresholds.MinScore, Out, Seen);
	}
	return Out;
}

bool TacticalMapRules::IsChokepoint(const FTacticalMapFile& File, const FTacticalRuntimeConfig& Config, TConstArrayView<FVector> Path, int32* OutIndex)
{
	const TArray<int32> Crossed = ChokepointsOnPath(File, Config, Path);
	if (OutIndex)
	{
		*OutIndex = Crossed.IsEmpty() ? INDEX_NONE : Crossed[0];
	}
	return !Crossed.IsEmpty();
}

FTacticalDetour TacticalMapRules::SafeDetour(const FTacticalMapFile& File, const FTacticalRuntimeConfig& Config, const FVector& From, const FVector& To)
{
	FTacticalDetour Out;
	const FVector Path[] = { From, To };
	for (const int32 Index : ChokepointsOnPath(File, Config, Path))
	{
		if (Out.ChokepointIndex == INDEX_NONE || File.Chokepoints[Index].Score > File.Chokepoints[Out.ChokepointIndex].Score)
		{
			Out.ChokepointIndex = Index;
		}
	}
	if (Out.ChokepointIndex == INDEX_NONE)
	{
		return Out;
	}
	const FTacticalChokepoint& Zone = File.Chokepoints[Out.ChokepointIndex];
	Out.bCrossesChokepoint = true;
	Out.Alternative = Zone.Alternative;
	Out.DetourM = Zone.DetourM;
	Out.bOnlyWay = Zone.Alternative == TEXT("none");
	Out.bTakeDetour = Zone.Alternative == TEXT("detour") && Zone.DetourM >= 0.f && Zone.DetourM <= Config.ShortDetourM;
	return Out;
}

double TacticalMapRules::SeededRoll(int32 Seed, const FString& Key)
{
	uint32 Hash = 2166136261u;
	for (const TCHAR Char : Key)
	{
		Hash ^= static_cast<uint32>(Char) & 0xFFFFu;
		Hash *= 16777619u;
	}
	Hash ^= static_cast<uint32>(Seed) * 0x9E3779B9u;
	// murmur3 fmix32
	Hash ^= Hash >> 16;
	Hash *= 0x85EBCA6Bu;
	Hash ^= Hash >> 13;
	Hash *= 0xC2B2AE35u;
	Hash ^= Hash >> 16;
	return static_cast<double>(Hash) / 4294967296.0;
}

TArray<int32> TacticalMapRules::PickLadderOnlyRoofs(const FTacticalMapFile& File, int32 Seed, float Factor)
{
	const double Share = FMath::Clamp(Factor < 0.f ? File.Thresholds.LadderOnlyFactor : Factor, 0.f, 1.f);
	TArray<int32> Out;
	for (int32 Index = 0; Index < File.LadderOnlyRoofs.Num(); ++Index)
	{
		if (SeededRoll(Seed, File.LadderOnlyRoofs[Index].Building) < Share)
		{
			Out.Add(Index);
		}
	}
	return Out;
}

FString TacticalMapRules::JoinKinds(TConstArrayView<FName> Kinds)
{
	TArray<FString> Names;
	for (const FName Kind : Kinds)
	{
		Names.Add(Kind.ToString());
	}
	return FString::Join(Names, TEXT(", "));
}

FString TacticalMapRules::GetDefaultPathForMap(const FString& MapName)
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("Data/LevelGen/tactical") / (MapName + TEXT(".tactical.json")));
}

FString TacticalMapRules::GetConfigPath()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("Data/LevelGen/tactical_runtime.json"));
}
