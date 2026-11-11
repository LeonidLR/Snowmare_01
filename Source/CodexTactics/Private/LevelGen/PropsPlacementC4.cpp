// Props placement task C4 (docs/design/levelgen/PROPS_LAYERS.md 2.3 / 5 / 6.1 / 7): turrets, mines (doorways, crates, field) and loot.
// Pure, deterministic, integer cm; the RNG call order is the TS port's contract (section 7, "C4"). UE-only (no Godot reference).

#include "LevelGen/PropsPlacementRules.h"
#include "CodexTactics.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "LevelGen/LevelPropsFile.h"
#include "LevelGen/PropsRng.h"

#include "LevelGenJsonRead.h"
#include "PropsPlacementInternal.h"

namespace PropsPlacementC4Local
{
	using namespace PropsPlacementLocal;
	using namespace LevelGenJson;

	const TCHAR* const GC4Ground = TEXT("ground");

	struct FAnchor
	{
		FIntPoint Pos = FIntPoint::ZeroValue;
		FString Layer;
	};

	struct FRouteSample
	{
		FIntPoint Pos = FIntPoint::ZeroValue;
		FString Layer;
	};

	/** cover_route.route, else the layout's first mandatory route. */
	const FPropsRoute* CoverRoute(const FPropsLayoutView& View, const FPropsTurretRules& T)
	{
		if (!T.RouteId.IsEmpty())
		{
			return View.FindRoute(T.RouteId);
		}
		const TArray<FString>& Mandatory = View.GetMandatoryRouteIds();
		return Mandatory.Num() > 0 ? View.FindRoute(Mandatory[0]) : nullptr;
	}

	/**
	 * Samples of a route: every leg with both ends on one layer, n = ceil(isqrt(len2) / 100) samples A + floor((B - A) x k / n),
	 * k = 0..n-1 (the leg's end is the next leg's first sample). One sample = one covered metre.
	 */
	void RouteSamples(const FPropsRoute& Route, TArray<FRouteSample>& Out)
	{
		for (int32 I = 0; I + 1 < Route.Points.Num(); ++I)
		{
			if (Route.Layers[I] != Route.Layers[I + 1])
			{
				continue;
			}
			const FIntPoint A = Route.Points[I];
			const int64 DX = static_cast<int64>(Route.Points[I + 1].X) - A.X;
			const int64 DY = static_cast<int64>(Route.Points[I + 1].Y) - A.Y;
			const int64 Len = PropsGeometry::ISqrt(DX * DX + DY * DY);
			if (Len == 0)
			{
				continue;
			}
			const int64 N = (Len + PropsPlacementRules::RouteSampleCm - 1) / PropsPlacementRules::RouteSampleCm;
			for (int64 K = 0; K < N; ++K)
			{
				FRouteSample& S = Out.AddDefaulted_GetRef();
				S.Pos = FIntPoint(A.X + static_cast<int32>(PropsGeometry::FloorDiv(DX * K, N)), A.Y + static_cast<int32>(PropsGeometry::FloorDiv(DY * K, N)));
				S.Layer = Route.Layers[I];
			}
		}
	}

	/** D inside the arc [Yaw - floor(Arc / 2), + Arc] (ccw), exact with the sin table; Arc >= 360 = everything. */
	struct FArc
	{
		int64 SX = 0;
		int64 SY = 0;
		int64 EX = 0;
		int64 EY = 0;
		int32 Arc = 360;

		FArc(int32 Yaw, int32 InArc)
			: Arc(InArc)
		{
			const int32 Start = Yaw - InArc / 2;
			SX = PropsGeometry::CosQ(Start);
			SY = PropsGeometry::SinQ(Start);
			EX = PropsGeometry::CosQ(Start + InArc);
			EY = PropsGeometry::SinQ(Start + InArc);
		}

		bool Contains(FIntPoint D) const
		{
			if (Arc >= 360)
			{
				return true;
			}
			const int64 X = D.X;
			const int64 Y = D.Y;
			if (Arc <= 180)
			{
				return SX * Y - SY * X >= 0 && X * EY - Y * EX >= 0;
			}
			return !(EX * Y - EY * X > 0 && X * SY - Y * SX > 0);
		}
	};

	/** Turret coverage with the route samples and the per-layer sight blockers cached for one placement. */
	struct FCoverage
	{
		const FPropsLayoutView& View;
		const FPropsTurretRules& T;
		const FPropsRoute* Route = nullptr;
		TArray<FRouteSample> Samples;
		TMap<FString, TArray<FPropsSightSegment>> Blockers;
		TArray<FArc> Arcs;

		FCoverage(const FPropsLayoutView& InView, const FPropsTurretRules& InT)
			: View(InView), T(InT)
		{
			Route = CoverRoute(View, T);
			if (Route)
			{
				RouteSamples(*Route, Samples);
			}
			for (int32 Yaw = 0; Yaw < 360; Yaw += PropsPlacementRules::TurretYawStepDeg)
			{
				Arcs.Emplace(Yaw, FMath::Clamp(T.ArcDeg, 1, 360));
			}
		}

		/** Covered metres from P on Layer; OutYaw = the best arc's yaw (ties: the smallest). */
		int32 Score(FIntPoint P, const FString& Layer, int32& OutYaw)
		{
			OutYaw = 0;
			const FPropsWalkLayer* L = View.FindLayer(Layer);
			if (!Route || !L)
			{
				return 0;
			}
			const bool bRoof = L->Kind == EPropsWalkLayerKind::Roof;
			TArray<FPropsSightSegment>* B = Blockers.Find(Layer);
			if (!B)
			{
				B = &Blockers.Add(Layer);
				View.GetSightBlockers(Layer, *B);
			}
			const int64 Min2 = static_cast<int64>(T.RangeMinCm) * T.RangeMinCm;
			const int64 Max2 = static_cast<int64>(T.RangeMaxCm) * T.RangeMaxCm;
			TArray<FIntPoint> Seen;
			for (const FRouteSample& S : Samples)
			{
				if (S.Layer != Layer && !(bRoof && S.Layer == GC4Ground))
				{
					continue;
				}
				const int64 D2 = DistSq(P, S.Pos);
				if (D2 < Min2 || D2 > Max2 || FPropsLayoutView::IsSightBlocked(P, S.Pos, *B))
				{
					continue;
				}
				Seen.Add(S.Pos - P);
			}
			int32 Best = 0;
			for (int32 I = 0; I < Arcs.Num(); ++I)
			{
				int32 N = 0;
				for (const FIntPoint& D : Seen)
				{
					N += Arcs[I].Contains(D) ? 1 : 0;
				}
				if (N > Best)
				{
					Best = N;
					OutYaw = I * PropsPlacementRules::TurretYawStepDeg;
				}
			}
			return Best;
		}
	};

	FIntPoint SnappedCentre(FIntPoint Min, FIntPoint Max)
	{
		return FIntPoint(FloorStep(static_cast<int32>(PropsGeometry::FloorDiv(static_cast<int64>(Min.X) + Max.X, 2))),
			FloorStep(static_cast<int32>(PropsGeometry::FloorDiv(static_cast<int64>(Min.Y) + Max.Y, 2))));
	}

	bool IsRoofLayer(const FPropsLayoutView& View, const FString& Layer)
	{
		const FPropsWalkLayer* L = View.FindLayer(Layer);
		return L && L->Kind == EPropsWalkLayerKind::Roof;
	}

	/**
	 * The anchor points of where.anchors, in the anchors' order (section 7 "C4 anchors"). FarSideRoute (turrets): a behind_tall_cover
	 * point is kept only when it is farther from that route's ground legs than the cover's centre.
	 */
	void GatherAnchors(const FContext& Ctx, const FPropsRoute* FarSideRoute, int32 BehindGapCm, TArray<FAnchor>& Out)
	{
		const FPropsLayoutView& View = Ctx.View;
		const FPropsLayoutConfig& Cfg = View.GetConfig();
		const FString Ground = GC4Ground;
		const int32 Step = PropsPlacementRules::LatticeCm;
		for (const FString& Name : Ctx.R.Anchors)
		{
			if (Name == TEXT("ambush_points"))
			{
				for (const FPropsAnchorPoint& A : View.GetAmbushPoints())
				{
					Out.Add({ A.Pos, Ground });
				}
			}
			else if (Name == TEXT("roof_edges") || Name == TEXT("roof_centres"))
			{
				for (const FString& Layer : Ctx.LayerNames)
				{
					FIntPoint Min;
					FIntPoint Max;
					if (!IsRoofLayer(View, Layer) || !View.GetLayerExtent(Layer, Min, Max))
					{
						continue;
					}
					if (Name == TEXT("roof_centres"))
					{
						Out.Add({ SnappedCentre(Min, Max), Layer });
						continue;
					}
					// the lattice ring inset RoofEdgeInsetCm .. + one step from the footprint edge
					for (int32 Y = CeilStep(Min.Y); Y <= Max.Y; Y += Step)
					{
						for (int32 X = CeilStep(Min.X); X <= Max.X; X += Step)
						{
							const int32 Edge = FMath::Min(FMath::Min(X - Min.X, Max.X - X), FMath::Min(Y - Min.Y, Max.Y - Y));
							if (Edge >= Cfg.RoofEdgeInsetCm && Edge < Cfg.RoofEdgeInsetCm + Step)
							{
								Out.Add({ FIntPoint(X, Y), Layer });
							}
						}
					}
				}
			}
			else if (Name == TEXT("behind_tall_cover"))
			{
				const int64 K = PropsGeometry::SightScale;
				for (const FPropsCover& C : View.GetCovers())
				{
					if (C.HeightCm < Cfg.TallCoverMinCm)
					{
						continue;
					}
					const int32 CentreDist = FarSideRoute ? View.DistanceToRouteCm(C.Pos, *FarSideRoute, &Ground) : 0;
					const int64 Cs = PropsGeometry::CosQ(C.Yaw);
					const int64 Sn = PropsGeometry::SinQ(C.Yaw);
					const int64 Lx = static_cast<int64>(C.SizeX) + 2 * static_cast<int64>(BehindGapCm);
					const int64 Ly = static_cast<int64>(C.SizeY) + 2 * static_cast<int64>(BehindGapCm);
					// local +x, -x, +y, -y: (half extent + gap) along the cover's axes, the table rotation, floored
					const FIntPoint Offsets[4] = {
						FIntPoint(static_cast<int32>(PropsGeometry::FloorDiv(Cs * Lx, K)), static_cast<int32>(PropsGeometry::FloorDiv(Sn * Lx, K))),
						FIntPoint(static_cast<int32>(PropsGeometry::FloorDiv(-Cs * Lx, K)), static_cast<int32>(PropsGeometry::FloorDiv(-Sn * Lx, K))),
						FIntPoint(static_cast<int32>(PropsGeometry::FloorDiv(-Sn * Ly, K)), static_cast<int32>(PropsGeometry::FloorDiv(Cs * Ly, K))),
						FIntPoint(static_cast<int32>(PropsGeometry::FloorDiv(Sn * Ly, K)), static_cast<int32>(PropsGeometry::FloorDiv(-Cs * Ly, K))),
					};
					for (const FIntPoint& Off : Offsets)
					{
						const FIntPoint Q = C.Pos + Off;
						if (FarSideRoute && !(View.DistanceToRouteCm(Q, *FarSideRoute, &Ground) > CentreDist))
						{
							continue;
						}
						Out.Add({ Q, Ground });
					}
				}
			}
			else if (Name == TEXT("dead_ends"))
			{
				for (const FPropsZone& Z : View.GetZones())
				{
					if (Z.Kind != TEXT("wall_end") || Z.Poly.Num() < 3)
					{
						continue;
					}
					FIntPoint Min = Z.Poly[0];
					FIntPoint Max = Z.Poly[0];
					for (const FIntPoint& P : Z.Poly)
					{
						Min = FIntPoint(FMath::Min(Min.X, P.X), FMath::Min(Min.Y, P.Y));
						Max = FIntPoint(FMath::Max(Max.X, P.X), FMath::Max(Max.Y, P.Y));
					}
					for (int32 Y = CeilStep(Min.Y); Y <= Max.Y; Y += Step)
					{
						for (int32 X = CeilStep(Min.X); X <= Max.X; X += Step)
						{
							if (View.IsInZone(FIntPoint(X, Y), Z.Id))
							{
								Out.Add({ FIntPoint(X, Y), Ground });
							}
						}
					}
				}
			}
			else if (Name == TEXT("building_interiors"))
			{
				for (const FPropsBuildingSummary& B : View.GetBuildings())
				{
					if (!B.bEnterable)
					{
						continue;
					}
					const FIntPoint C = SnappedCentre(B.Min, B.Max);
					Out.Add({ C, Ground });
					for (int32 N = 1; !B.bHollow && N < B.Floors; ++N)
					{
						Out.Add({ C, FString::Printf(TEXT("floor:%s:%d"), *B.Id, N) });
					}
				}
			}
		}
	}

	/**
	 * Anchors -> candidates: on a matched walk layer, walkable, in the zones (= eligible, counted after the (y, x, layer) sort and the
	 * removal of duplicates), then the hard filters (first failure counted). Weights are the caller's.
	 */
	void FilterAnchors(const FContext& Ctx, const TArray<FAnchor>& Anchors, int32 Half, const TArray<FLevelPropsObject>& Spaced, int32& Eligible,
		FPropsRejectCounts& Rejected, TArray<FCandidate>& Out)
	{
		TArray<FCandidate> All;
		for (const FAnchor& A : Anchors)
		{
			const int32 LayerIndex = Ctx.LayerIndexOf(A.Layer);
			if (LayerIndex == INDEX_NONE || !Ctx.View.IsWalkable(A.Pos, A.Layer) || !Ctx.InZones(A.Pos))
			{
				continue;
			}
			FCandidate& C = All.AddDefaulted_GetRef();
			C.Pos = A.Pos;
			C.Layer = LayerIndex;
		}
		SortCandidates(Ctx, All);
		for (int32 I = 0; I < All.Num(); ++I)
		{
			if (I > 0 && All[I].Pos == All[I - 1].Pos && All[I].Layer == All[I - 1].Layer)
			{
				continue;
			}
			++Eligible;
			const EReject Reject = Ctx.FullReject(All[I].Pos, Ctx.LayerNames[All[I].Layer], Half, Spaced);
			if (Reject != EReject::None)
			{
				Count(Rejected, Reject);
				continue;
			}
			Out.Add(All[I]);
		}
	}

	int32 MaxVariantHalf(const FPropsCommonRules& R)
	{
		int32 H = 0;
		for (const TPair<FString, int32>& V : R.Variants)
		{
			H = FMath::Max(H, HalfOf(R, V.Key));
		}
		return H;
	}

	/** Index into In.OtherObjects of the object Id of layer LayerId, or INDEX_NONE. */
	int32 FindOther(const FPropsPlacementInput& In, const FString& LayerId, const FString& Id)
	{
		for (int32 I = 0; I < In.OtherObjects.Num(); ++I)
		{
			if (In.OtherLayerIds.IsValidIndex(I) && In.OtherLayerIds[I].Equals(LayerId, ESearchCase::CaseSensitive)
				&& In.OtherObjects[I].Id.Equals(Id, ESearchCase::CaseSensitive))
			{
				return I;
			}
		}
		return INDEX_NONE;
	}

	/** The source layer's objects (indices into In.OtherObjects) sorted by id, ordinal. */
	TArray<int32> SourceObjects(const FPropsPlacementInput& In, const FString& LayerId)
	{
		TArray<int32> Out;
		for (int32 I = 0; I < In.OtherObjects.Num(); ++I)
		{
			if (In.OtherLayerIds.IsValidIndex(I) && In.OtherLayerIds[I].Equals(LayerId, ESearchCase::CaseSensitive))
			{
				Out.Add(I);
			}
		}
		Out.StableSort([&In](int32 A, int32 B) { return In.OtherObjects[A].Id.Compare(In.OtherObjects[B].Id, ESearchCase::CaseSensitive) < 0; });
		return Out;
	}

	/** Ids that an object of a mines-kind layer attaches to (red crates). */
	TSet<FString> MinedIds(const FPropsPlacementInput& In)
	{
		TSet<FString> Out;
		for (int32 I = 0; I < In.OtherObjects.Num(); ++I)
		{
			const FString* Kind = In.OtherLayerIds.IsValidIndex(I) ? In.LayerKinds.Find(In.OtherLayerIds[I]) : nullptr;
			if (Kind && *Kind == PropsPlacementRules::KindMines && !In.OtherObjects[I].Attach.IsEmpty())
			{
				Out.Add(In.OtherObjects[I].Attach);
			}
		}
		return Out;
	}

	/** The doorway mine position: the door centre moved InsideCm into the building (against Outward); a gate: its centre. */
	FIntPoint DoorMinePos(const FPropsDoorway& W, int32 InsideCm)
	{
		return W.Pos - W.Outward * InsideCm;
	}

	/** Yaw across the opening (the way through it): a wall along x -> 90, along y -> 0. */
	int32 DoorMineYaw(const FPropsDoorway& W)
	{
		return W.Along.X != 0 ? 90 : 0;
	}

	const TCHAR* DoorKindName(const FPropsDoorway& W)
	{
		return W.Kind == EPropsDoorwayKind::Gate ? TEXT("gate") : TEXT("building_door");
	}

	int32 SubShort(const FPropsSubReport& S)
	{
		return FMath::Max(0, S.Requested - S.Kept - S.Placed);
	}

	/** loot_tables value of a table of the layer (its tables or the roof table); 0 when unknown. */
	int32 TableValueOf(const FPropsLootRules& L, const FString& Table)
	{
		const int32 Index = L.Tables.IndexOfByPredicate([&Table](const FString& T) { return T.Equals(Table, ESearchCase::CaseSensitive); });
		if (Index != INDEX_NONE)
		{
			return L.TableValues[Index];
		}
		return !L.RoofTable.IsEmpty() && L.RoofTable.Equals(Table, ESearchCase::CaseSensitive) ? L.RoofTableValue : 0;
	}
}

// ---- turrets -------------------------------------------------------------------------------------------------------------------------

int32 PropsPlacementRules::TurretCoverage(const FPropsLayoutView& View, const FPropsRules& Rules, FIntPoint P, const FString& Layer, int32& OutYaw)
{
	PropsPlacementC4Local::FCoverage Coverage(View, Rules.Turret);
	return Coverage.Score(P, Layer, OutYaw);
}

void PropsPlacementLocal::PlaceTurrets(const FPropsPlacementInput& In, FPropsPlacementResult& Result)
{
	using namespace PropsPlacementC4Local;
	FPropsPlacementReport& Report = Result.Report;
	const FPropsRules& R = In.Rules;
	const auto Finish = [&Report]() { Report.ShortBy = FMath::Max(0, Report.Requested - Report.Kept - Report.Placed); };
	if (R.Variants.Num() == 0)
	{
		Report.Warnings.Add(TEXT("no_variants"));
		return;
	}
	const FContext Ctx(In);
	const FPropsLayoutView& View = *In.View;
	const int32 Half = MaxVariantHalf(R);
	FCoverage Coverage(View, R.Turret);

	// candidates = anchors only, hard filters, then the score (covered metres) with its best yaw
	TArray<FAnchor> Anchors;
	GatherAnchors(Ctx, Coverage.Route, R.Turret.BehindCoverGapCm, Anchors);
	TArray<FCandidate> Filtered;
	FilterAnchors(Ctx, Anchors, Half, In.Kept, Report.Eligible, Report.Rejected, Filtered);
	Report.Requested = ComputeRequested(R, Report.Eligible, Ctx.ZoneCount());
	const int32 Target = FMath::Max(0, Report.Requested - Report.Kept);
	if (!Coverage.Route)
	{
		Report.Warnings.Add(TEXT("no_cover_route"));
		Filtered.Reset();
	}
	const int32 MinScore = FMath::Max(1, R.Turret.MinCoveredM);
	TArray<FCandidate> Cands;
	for (FCandidate C : Filtered)
	{
		C.Score = Coverage.Score(C.Pos, Ctx.LayerNames[C.Layer], C.Yaw);
		if (C.Score < MinScore)
		{
			++Report.Rejected.Coverage;
			continue;
		}
		C.Weight = static_cast<uint32>(FMath::Min(C.Score, PropsPlacementRules::MaxWeight));
		Cands.Add(C);
	}
	if (Target > 0 && Cands.Num() == 0)
	{
		Report.Warnings.Add(TEXT("no_candidates"));
	}

	uint64 TotalWeight = TotalWeightOf(Cands);
	TArray<bool> Alive;
	Alive.Init(true, Cands.Num());
	FPropsRng Rng(PropsRng::StreamSeed(In.Seed, In.LayerId));
	int32 IdCounter = 0;
	TArray<FString> UsedIds;
	int32 VariantTotal = 0;
	for (const TPair<FString, int32>& V : R.Variants)
	{
		VariantTotal += V.Value;
	}
	// per pick: candidate (weighted by score), variant; yaw / arc from the score, no draw
	int32 Placed = 0;
	while (Placed < Target && TotalWeight > 0)
	{
		const int32 Pick = DrawWeighted(Rng, Cands, Alive, TotalWeight);
		if (Pick == INDEX_NONE)
		{
			break;
		}
		const FCandidate& C = Cands[Pick];
		FLevelPropsObject O;
		O.Pos = C.Pos;
		O.WalkLayer = Ctx.LayerNames[C.Layer];
		const uint32 VariantRoll = Rng.NextBelow(static_cast<uint32>(VariantTotal));
		uint32 Cum = 0;
		for (const TPair<FString, int32>& V : R.Variants)
		{
			Cum += static_cast<uint32>(V.Value);
			if (VariantRoll < Cum)
			{
				O.Variant = V.Key;
				break;
			}
		}
		O.Yaw = C.Yaw;
		O.Arc = FMath::Clamp(R.Turret.ArcDeg, 1, 360);
		O.Extra.Add(PropsPlacementRules::CoveredField, MakeShared<FJsonValueNumber>(C.Score));
		O.Id = NextId(In, IdCounter, UsedIds);
		Result.Objects.Add(MoveTemp(O));
		++Placed;
		TotalWeight -= DropAround(Cands, Alive, Pick, R.MinSpacingCm);
	}
	Report.Placed = Placed;
	Finish();
}

// ---- mines ---------------------------------------------------------------------------------------------------------------------------

void PropsPlacementLocal::PlaceMines(const FPropsPlacementInput& In, FPropsPlacementResult& Result)
{
	using namespace PropsPlacementC4Local;
	FPropsPlacementReport& Report = Result.Report;
	const FPropsRules& R = In.Rules;
	const FPropsLayoutView& View = *In.View;
	FPropsSubReport& Doors = Report.SubA;
	FPropsSubReport& Crates = Report.SubB;
	FPropsSubReport& Field = Report.SubC;

	// kept mines: a doorway mine needs its doorway, a crate mine its crate (6.1: a locked mine keeps its crate only while the crate exists
	// - a locked crate survives every re-roll, an unlocked one is re-rolled away -> the mine is dropped and reported)
	TArray<FLevelPropsObject> Kept;
	TSet<FString> MinedDoors;
	TSet<FString> MinedCrates;
	for (const FLevelPropsObject& K : In.Kept)
	{
		if (!K.Door.IsEmpty())
		{
			if (!View.FindDoorway(K.Door))
			{
				Report.Warnings.Add(FString::Printf(TEXT("door_missing %s"), *K.Id));
				++Doors.Dropped;
				continue;
			}
			++Doors.Kept;
			MinedDoors.Add(K.Door);
		}
		else if (!K.Attach.IsEmpty())
		{
			if (FindOther(In, R.MineCrates.SourceLayer, K.Attach) == INDEX_NONE)
			{
				Report.Warnings.Add(FString::Printf(TEXT("attach_dropped %s"), *K.Id));
				++Crates.Dropped;
				continue;
			}
			++Crates.Kept;
			MinedCrates.Add(K.Attach);
		}
		else
		{
			++Field.Kept;
		}
		Kept.Add(K);
	}
	Result.Objects = Kept;
	Report.Kept = Kept.Num();

	const FContext Ctx(In);
	FPropsRng Rng(PropsRng::StreamSeed(In.Seed, In.LayerId));
	int32 IdCounter = 0;
	TArray<FString> UsedIds;
	int64 Budget = R.BudgetCap;
	const auto Take = [&Budget](int64 Target)
	{
		const int32 Requested = static_cast<int32>(FMath::Clamp<int64>(Target, 0, Budget));
		Budget -= Requested;
		return Requested;
	};

	// 1. doorways: enumerate (id order) -> kinds -> forbid mandatory -> exclude / keep-outs; ceil(p x eligible) or N; seeded Fisher-Yates
	if (R.MineDoors.bEnabled)
	{
		const FPropsMineDoorRules& D = R.MineDoors;
		const TArray<FPropsDoorway>& Ways = View.GetDoorways();
		TArray<int32> Free;
		for (int32 I = 0; I < Ways.Num(); ++I)
		{
			const FPropsDoorway& W = Ways[I];
			if (D.Kinds.Num() > 0 && !ContainsExact(D.Kinds, DoorKindName(W)))
			{
				continue;
			}
			if (W.bOnMandatoryRoute && !D.bAllowMandatory)
			{
				++Doors.Forbidden;
				continue;
			}
			if (MinedDoors.Contains(W.Id))
			{
				++Doors.Eligible; // carries a kept mine: eligible and already counted
				continue;
			}
			const FIntPoint Pos = DoorMinePos(W, D.InsideCm);
			if (const FPropsExclusion* S = Ctx.FirstShape(Pos, W.WalkLayer))
			{
				Count(Report.Rejected, RejectOfExclusion(S->Kind));
				continue;
			}
			if (In.KeepOuts.ContainsByPredicate([&Ctx, &Pos](const FLevelPropsKeepOut& K) { return Closer(Pos, K.Pos, Ctx.KeepOutRadius(K)); }))
			{
				++Report.Rejected.Keepout;
				continue;
			}
			++Doors.Eligible;
			Free.Add(I);
		}
		const int64 Target = D.Mode == TEXT("count") ? static_cast<int64>(D.Value) : (static_cast<int64>(D.Value) * Doors.Eligible + 99) / 100;
		Doors.Requested = Take(Target);
		const int32 Want = FMath::Max(0, Doors.Requested - Doors.Kept);
		TArray<int32> Order;
		for (int32 I = 0; I < Free.Num(); ++I)
		{
			Order.Add(I);
		}
		Rng.Shuffle(Order);
		for (int32 K = 0; K < Order.Num() && Doors.Placed < Want; ++K)
		{
			const FPropsDoorway& W = Ways[Free[Order[K]]];
			FLevelPropsObject O;
			O.Id = NextId(In, IdCounter, UsedIds);
			O.Variant = D.Variant;
			O.Pos = DoorMinePos(W, D.InsideCm);
			O.WalkLayer = W.WalkLayer;
			O.Yaw = DoorMineYaw(W);
			O.Door = W.Id;
			O.Trigger = D.Trigger;
			O.Extra.Add(PropsPlacementRules::WidthField, MakeShared<FJsonValueNumber>(W.WidthCm > 0 ? W.WidthCm : View.GetConfig().DoorFallbackWidthCm));
			Result.Objects.Add(MoveTemp(O));
			++Doors.Placed;
		}
	}

	// 2. crates: the source layer's UNLOCKED crates (id order) = eligible; ceil(p x eligible); seeded Fisher-Yates over the free ones
	if (R.MineCrates.bEnabled)
	{
		const FPropsMineCrateRules& C = R.MineCrates;
		if (!In.LayerKinds.Contains(C.SourceLayer))
		{
			Report.Warnings.Add(FString::Printf(TEXT("no_source_layer %s"), *C.SourceLayer));
		}
		TArray<int32> Free;
		for (const int32 Index : SourceObjects(In, C.SourceLayer))
		{
			const FLevelPropsObject& Crate = In.OtherObjects[Index];
			if (Crate.bLocked)
			{
				continue;
			}
			++Crates.Eligible;
			if (!MinedCrates.Contains(Crate.Id))
			{
				Free.Add(Index);
			}
		}
		Crates.Requested = Take((static_cast<int64>(C.Percent) * Crates.Eligible + 99) / 100);
		const int32 Want = FMath::Max(0, Crates.Requested - Crates.Kept);
		TArray<int32> Order;
		for (int32 I = 0; I < Free.Num(); ++I)
		{
			Order.Add(I);
		}
		Rng.Shuffle(Order);
		for (int32 K = 0; K < Order.Num() && Crates.Placed < Want; ++K)
		{
			const FLevelPropsObject& Crate = In.OtherObjects[Free[Order[K]]];
			FLevelPropsObject O;
			O.Id = NextId(In, IdCounter, UsedIds);
			O.Variant = C.Variant;
			O.Pos = Crate.Pos;
			O.WalkLayer = Crate.WalkLayer;
			O.Attach = Crate.Id;
			O.Trigger = C.Trigger;
			Result.Objects.Add(MoveTemp(O));
			++Crates.Placed;
		}
	}

	// 3. field: the common algorithm with the field rules; spacing to every mine so far (kept, doorway, crate)
	if (R.MineField.bEnabled)
	{
		const FPropsMineFieldRules& F = R.MineField;
		const FContext FieldCtx(In, F.Common);
		const TArray<FLevelPropsObject> Spaced = Result.Objects;
		TArray<FCandidate> Cands;
		GatherLattice(FieldCtx, HalfOf(F.Common, F.Variant), Spaced, Field.Eligible, Report.Rejected, Cands);
		Field.Requested = Take(ComputeRequested(F.Common, Field.Eligible, FieldCtx.ZoneCount()));
		const int32 Want = FMath::Max(0, Field.Requested - Field.Kept);
		if (Want > 0 && Cands.Num() == 0)
		{
			Report.Warnings.Add(TEXT("no_candidates"));
		}
		uint64 TotalWeight = TotalWeightOf(Cands);
		TArray<bool> Alive;
		Alive.Init(true, Cands.Num());
		while (Field.Placed < Want && TotalWeight > 0)
		{
			const int32 Pick = DrawWeighted(Rng, Cands, Alive, TotalWeight);
			if (Pick == INDEX_NONE)
			{
				break;
			}
			FLevelPropsObject O;
			O.Id = NextId(In, IdCounter, UsedIds);
			O.Variant = F.Variant;
			O.Pos = Cands[Pick].Pos;
			O.WalkLayer = FieldCtx.LayerNames[Cands[Pick].Layer];
			O.Trigger = F.Trigger;
			Result.Objects.Add(MoveTemp(O));
			++Field.Placed;
			TotalWeight -= DropAround(Cands, Alive, Pick, F.Common.MinSpacingCm);
		}
	}

	Report.Requested = Doors.Requested + Crates.Requested + Field.Requested;
	Report.Placed = Doors.Placed + Crates.Placed + Field.Placed;
	Report.ShortBy = SubShort(Doors) + SubShort(Crates) + SubShort(Field);
}

// ---- loot ----------------------------------------------------------------------------------------------------------------------------

void PropsPlacementLocal::PlaceLoot(const FPropsPlacementInput& In, FPropsPlacementResult& Result)
{
	using namespace PropsPlacementC4Local;
	FPropsPlacementReport& Report = Result.Report;
	const FPropsRules& R = In.Rules;
	const FPropsLootRules& L = R.Loot;
	const FPropsLayoutView& View = *In.View;
	FPropsSubReport& Containers = Report.SubA;
	FPropsSubReport& Loose = Report.SubB;
	Report.BudgetValue = L.BudgetValue;

	// kept loot: a container needs its crate; every kept object's table value is spent first
	TArray<FLevelPropsObject> Kept;
	TSet<FString> Filled;
	int64 KeptValue = 0;
	int64 ContainerSpent = 0;
	for (const FLevelPropsObject& K : In.Kept)
	{
		if (!K.Attach.IsEmpty())
		{
			if (FindOther(In, L.SourceLayer, K.Attach) == INDEX_NONE)
			{
				Report.Warnings.Add(FString::Printf(TEXT("attach_dropped %s"), *K.Id));
				++Containers.Dropped;
				continue;
			}
			++Containers.Kept;
			Filled.Add(K.Attach);
		}
		else
		{
			++Loose.Kept;
		}
		const int32 Value = TableValueOf(L, K.LootTable);
		if (Value == 0)
		{
			Report.Warnings.Add(FString::Printf(TEXT("unknown_table %s"), *K.Id));
		}
		KeptValue += Value;
		ContainerSpent += K.Attach.IsEmpty() ? 0 : Value;
		Kept.Add(K);
	}
	Result.Objects = Kept;
	Report.Kept = Kept.Num();

	const int64 BudgetForNew = FMath::Max<int64>(0, L.BudgetValue - KeptValue);
	int64 Remaining = BudgetForNew;
	int32 CountLeft = FMath::Max(0, R.BudgetCap - Report.Kept);
	// the ground tables = the layer's tables without the roof table (all of them when that leaves none)
	TArray<int32> GroundTables;
	for (int32 I = 0; I < L.Tables.Num(); ++I)
	{
		if (L.RoofTable.IsEmpty() || !L.Tables[I].Equals(L.RoofTable, ESearchCase::CaseSensitive))
		{
			GroundTables.Add(I);
		}
	}
	if (GroundTables.Num() == 0)
	{
		for (int32 I = 0; I < L.Tables.Num(); ++I)
		{
			GroundTables.Add(I);
		}
	}
	int64 Cheapest = MAX_int64;
	for (const int32 I : GroundTables)
	{
		Cheapest = FMath::Min<int64>(Cheapest, L.TableValues[I]);
	}
	if (!L.RoofTable.IsEmpty())
	{
		Cheapest = FMath::Min<int64>(Cheapest, L.RoofTableValue);
	}
	if (L.Tables.Num() == 0)
	{
		Report.Warnings.Add(TEXT("no_tables"));
	}
	const auto Affordable = [&L](const TArray<int32>& Options, int64 Limit)
	{
		TArray<int32> Out;
		for (const int32 I : Options)
		{
			if (L.TableValues[I] <= Limit)
			{
				Out.Add(I);
			}
		}
		return Out;
	};
	const int64 ContainerCap = static_cast<int64>(L.BudgetValue) * L.ContainersBudgetMilli / 1000;

	FPropsRng Rng(PropsRng::StreamSeed(In.Seed, In.LayerId));
	int32 IdCounter = 0;
	TArray<FString> UsedIds;

	// 1. containers: unlocked, not mined (red) crates of the source layer in id order; round(share x eligible); Fisher-Yates; a table each
	{
		const TSet<FString> Mined = MinedIds(In);
		TArray<int32> Free;
		for (const int32 Index : SourceObjects(In, L.SourceLayer))
		{
			const FLevelPropsObject& Crate = In.OtherObjects[Index];
			if (Crate.bLocked || Mined.Contains(Crate.Id))
			{
				continue;
			}
			++Containers.Eligible;
			if (!Filled.Contains(Crate.Id))
			{
				Free.Add(Index);
			}
		}
		Containers.Requested = static_cast<int32>((static_cast<int64>(L.ContainersShareMilli) * Containers.Eligible + 500) / 1000);
		const int32 Want = FMath::Max(0, Containers.Requested - Containers.Kept);
		TArray<int32> Order;
		for (int32 I = 0; I < Free.Num(); ++I)
		{
			Order.Add(I);
		}
		Rng.Shuffle(Order);
		for (int32 K = 0; K < Order.Num() && Containers.Placed < Want && CountLeft > 0; ++K)
		{
			const TArray<int32> Options = Affordable(GroundTables, FMath::Min(Remaining, ContainerCap - ContainerSpent));
			if (Options.Num() == 0)
			{
				break;
			}
			const int32 Table = Options[static_cast<int32>(Rng.NextBelow(static_cast<uint32>(Options.Num())))];
			const FLevelPropsObject& Crate = In.OtherObjects[Free[Order[K]]];
			FLevelPropsObject O;
			O.Id = NextId(In, IdCounter, UsedIds);
			O.Variant = L.ContainerVariant;
			O.Pos = Crate.Pos;
			O.WalkLayer = Crate.WalkLayer;
			O.Attach = Crate.Id;
			O.LootTable = L.Tables[Table];
			Result.Objects.Add(MoveTemp(O));
			Remaining -= L.TableValues[Table];
			ContainerSpent += L.TableValues[Table];
			--CountLeft;
			++Containers.Placed;
		}
	}

	// 2. loose piles at the anchors, weighted by the distance band to the mandatory route; roofs take the roof table
	{
		const FContext Ctx(In);
		TArray<FAnchor> Anchors;
		GatherAnchors(Ctx, nullptr, 0, Anchors);
		TArray<FCandidate> Cands;
		FilterAnchors(Ctx, Anchors, HalfOf(R, L.PileVariant), Result.Objects, Loose.Eligible, Report.Rejected, Cands);
		for (FCandidate& C : Cands)
		{
			const int32 D = View.DistanceToMandatoryRouteAnyLayerCm(C.Pos);
			C.Weight = static_cast<uint32>(D < L.FarMinCm ? L.WeightNear : (D <= L.FarMaxCm ? L.WeightBand : L.WeightFar));
		}
		uint64 TotalWeight = TotalWeightOf(Cands);
		TArray<bool> Alive;
		Alive.Init(true, Cands.Num());
		while (CountLeft > 0 && TotalWeight > 0 && Remaining >= Cheapest)
		{
			const int32 Pick = DrawWeighted(Rng, Cands, Alive, TotalWeight);
			if (Pick == INDEX_NONE)
			{
				break;
			}
			const FCandidate& C = Cands[Pick];
			const FString& Layer = Ctx.LayerNames[C.Layer];
			FString Table;
			int32 Value = 0;
			if (!L.RoofTable.IsEmpty() && IsRoofLayer(View, Layer))
			{
				if (L.RoofTableValue <= Remaining)
				{
					Table = L.RoofTable;
					Value = L.RoofTableValue;
				}
			}
			else
			{
				const TArray<int32> Options = Affordable(GroundTables, Remaining);
				if (Options.Num() > 0)
				{
					const int32 I = Options[static_cast<int32>(Rng.NextBelow(static_cast<uint32>(Options.Num())))];
					Table = L.Tables[I];
					Value = L.TableValues[I];
				}
			}
			if (Table.IsEmpty())
			{
				// nothing this spot can hold is affordable: only this candidate dies
				Alive[Pick] = false;
				TotalWeight -= C.Weight;
				continue;
			}
			FLevelPropsObject O;
			O.Id = NextId(In, IdCounter, UsedIds);
			O.Variant = L.PileVariant;
			O.Pos = C.Pos;
			O.WalkLayer = Layer;
			O.LootTable = Table;
			Result.Objects.Add(MoveTemp(O));
			Remaining -= Value;
			--CountLeft;
			++Loose.Placed;
			TotalWeight -= DropAround(Cands, Alive, Pick, R.MinSpacingCm);
		}
	}

	Report.Placed = Containers.Placed + Loose.Placed;
	Report.Requested = Report.Kept + Report.Placed;
	Report.ShortBy = 0;
	Report.BudgetSpent = static_cast<int32>(FMath::Min<int64>(KeptValue + BudgetForNew - Remaining, MAX_int32));
	if (Cheapest != MAX_int64 && Remaining >= Cheapest)
	{
		Report.bBudgetShort = true;
		Report.Warnings.Add(FString::Printf(TEXT("budget_left %lld"), Remaining));
	}
}

// ---- validation ----------------------------------------------------------------------------------------------------------------------

void PropsPlacementLocal::ValidateC4(const FPropsPlacementInput& In, const TArray<FLevelPropsObject>& Objects, TArray<FPropsViolation>& Out)
{
	using namespace PropsPlacementC4Local;
	const FPropsRules& R = In.Rules;
	const FPropsLayoutView& View = *In.View;
	const auto IsKept = [&In](const FLevelPropsObject& O)
	{
		return In.Kept.ContainsByPredicate([&O](const FLevelPropsObject& K) { return K.Id.Equals(O.Id, ESearchCase::CaseSensitive); });
	};
	const auto Add = [&Out](const FLevelPropsObject& O, const FString& Reason) { Out.Add({ O.Id, Reason }); };
	// the positional checks of the common algorithm for one rule set
	const auto CheckPlaced = [&](const FContext& Ctx, const FLevelPropsObject& O)
	{
		const int32 Half = HalfOf(Ctx.R, O.Variant);
		if (!MatchAnyLayer(Ctx.R.WalkLayers, O.WalkLayer))
		{
			Add(O, FString::Printf(TEXT("walk layer '%s' not allowed by the rules"), *O.WalkLayer));
		}
		if (!View.IsWalkable(O.Pos, O.WalkLayer))
		{
			Add(O, TEXT("not on its walk layer"));
		}
		if (View.IsBlocked(O.Pos, O.WalkLayer, Half + Ctx.R.WallGapCm))
		{
			Add(O, TEXT("blocked"));
		}
		for (const FPropsExclusion& S : Ctx.Shapes)
		{
			if (S.Contains(O.Pos, O.WalkLayer))
			{
				Add(O, FString::Printf(TEXT("excluded: %s %s"), ExclusionName(S.Kind), *S.SourceId));
			}
		}
		for (const FLevelPropsKeepOut& K : In.KeepOuts)
		{
			if (Closer(O.Pos, K.Pos, Ctx.KeepOutRadius(K)))
			{
				Add(O, FString::Printf(TEXT("inside the keep-out of %s"), *K.Id));
			}
		}
		for (int32 I = 0; I < In.OtherObjects.Num(); ++I)
		{
			if (Ctx.IsObstacle(I) && Closer(O.Pos, In.OtherObjects[I].Pos, static_cast<int64>(Half) + Ctx.OtherHalf[I] + Ctx.R.MinGapAnyCm))
			{
				Add(O, FString::Printf(TEXT("min_gap_any to %s"), *In.OtherObjects[I].Id));
			}
		}
	};
	const auto CheckSpacing = [&](const FLevelPropsObject& O, int32 Spacing, TFunctionRef<bool(const FLevelPropsObject&)> Counts)
	{
		for (const FLevelPropsObject& P : Objects)
		{
			if (&P != &O && Counts(P) && Closer(O.Pos, P.Pos, Spacing))
			{
				Add(O, FString::Printf(TEXT("min_spacing to %s"), *P.Id));
			}
		}
	};

	if (R.Kind == PropsPlacementRules::KindTurrets)
	{
		const FContext Ctx(In);
		FCoverage Coverage(View, R.Turret);
		for (const FLevelPropsObject& O : Objects)
		{
			if (IsKept(O))
			{
				continue;
			}
			CheckPlaced(Ctx, O);
			CheckSpacing(O, R.MinSpacingCm, [](const FLevelPropsObject&) { return true; });
			int32 Yaw = 0;
			const int32 Score = Coverage.Score(O.Pos, O.WalkLayer, Yaw);
			if (Score < FMath::Max(1, R.Turret.MinCoveredM))
			{
				Add(O, FString::Printf(TEXT("covers %d m < min_covered_m %d"), Score, R.Turret.MinCoveredM));
			}
			const TSharedPtr<FJsonValue>* Stored = O.Extra.Find(PropsPlacementRules::CoveredField);
			if (!Stored || (*Stored)->Type != EJson::Number || static_cast<int32>((*Stored)->AsNumber()) != Score || O.Yaw != Yaw)
			{
				Add(O, FString::Printf(TEXT("covered_m / yaw differ from the coverage (%d m, yaw %d)"), Score, Yaw));
			}
		}
		return;
	}

	if (R.Kind == PropsPlacementRules::KindMines)
	{
		const FContext FieldCtx(In, R.MineField.Common);
		int32 Total = 0;
		for (const FLevelPropsObject& O : Objects)
		{
			++Total;
			if (IsKept(O))
			{
				continue;
			}
			if (!O.Door.IsEmpty())
			{
				const FPropsDoorway* W = View.FindDoorway(O.Door);
				if (!W)
				{
					Add(O, FString::Printf(TEXT("doorway '%s' does not exist"), *O.Door));
					continue;
				}
				if (W->bOnMandatoryRoute && !R.MineDoors.bAllowMandatory)
				{
					Add(O, FString::Printf(TEXT("doorway %s is on the mandatory route (mandatory_route forbid)"), *W->Id));
				}
				if (R.MineDoors.Kinds.Num() > 0 && !ContainsExact(R.MineDoors.Kinds, DoorKindName(*W)))
				{
					Add(O, FString::Printf(TEXT("doorway kind %s not in the rules"), DoorKindName(*W)));
				}
				if (O.Pos != DoorMinePos(*W, R.MineDoors.InsideCm) || O.WalkLayer != W->WalkLayer)
				{
					Add(O, TEXT("not at its doorway's mine position"));
				}
				for (const FLevelPropsObject& P : Objects)
				{
					if (&P != &O && P.Door == O.Door)
					{
						Add(O, FString::Printf(TEXT("doorway %s mined twice (%s)"), *O.Door, *P.Id));
					}
				}
			}
			else if (!O.Attach.IsEmpty())
			{
				const int32 Crate = FindOther(In, R.MineCrates.SourceLayer, O.Attach);
				if (Crate == INDEX_NONE)
				{
					Add(O, FString::Printf(TEXT("attach target %s is not in layer %s"), *O.Attach, *R.MineCrates.SourceLayer));
				}
				else if (In.OtherObjects[Crate].bLocked)
				{
					Add(O, FString::Printf(TEXT("new mine on the locked crate %s"), *O.Attach));
				}
			}
			else
			{
				CheckPlaced(FieldCtx, O);
				CheckSpacing(O, R.MineField.Common.MinSpacingCm, [](const FLevelPropsObject&) { return true; });
			}
		}
		if (Total > R.BudgetCap)
		{
			Out.Add({ FString(), FString::Printf(TEXT("%d mines > budget %d"), Total, R.BudgetCap) });
		}
		return;
	}

	if (R.Kind == PropsPlacementRules::KindLoot)
	{
		const FContext Ctx(In);
		const TSet<FString> Mined = MinedIds(In);
		int64 Value = 0;
		for (const FLevelPropsObject& O : Objects)
		{
			Value += TableValueOf(R.Loot, O.LootTable);
			if (IsKept(O))
			{
				continue;
			}
			if (TableValueOf(R.Loot, O.LootTable) == 0)
			{
				Add(O, FString::Printf(TEXT("loot table '%s' unknown"), *O.LootTable));
			}
			if (!O.Attach.IsEmpty())
			{
				const int32 Crate = FindOther(In, R.Loot.SourceLayer, O.Attach);
				if (Crate == INDEX_NONE)
				{
					Add(O, FString::Printf(TEXT("attach target %s is not in layer %s"), *O.Attach, *R.Loot.SourceLayer));
				}
				else if (Mined.Contains(O.Attach))
				{
					Add(O, FString::Printf(TEXT("loot in the mined (red) crate %s"), *O.Attach));
				}
				continue;
			}
			CheckPlaced(Ctx, O);
			if (!R.Loot.RoofTable.IsEmpty() && IsRoofLayer(View, O.WalkLayer) != O.LootTable.Equals(R.Loot.RoofTable, ESearchCase::CaseSensitive))
			{
				Add(O, TEXT("roof piles use roof_table, ground piles never"));
			}
			CheckSpacing(O, R.MinSpacingCm, [](const FLevelPropsObject& P) { return P.Attach.IsEmpty(); });
		}
		if (Value > R.Loot.BudgetValue)
		{
			Out.Add({ FString(), FString::Printf(TEXT("loot value %lld > budget_value %d"), Value, R.Loot.BudgetValue) });
		}
		if (Objects.Num() > R.BudgetCap)
		{
			Out.Add({ FString(), FString::Printf(TEXT("%d loot objects > budget %d"), Objects.Num(), R.BudgetCap) });
		}
	}
}

// ---- crate colours -------------------------------------------------------------------------------------------------------------------

TMap<FString, FString> PropsPlacementRules::ResolveCrateColors(const FLevelPropsFile& File, const FJsonObject& Balance)
{
	using namespace LevelGenJson;
	FString Safe = TEXT("blue");
	FString Dangerous = TEXT("red");
	if (const TSharedPtr<FJsonObject> Colors = AsObject(Balance.TryGetField(TEXT("crate_colors"))))
	{
		TArray<FString> Errors;
		FFieldReader C(*Colors, TEXT("props_balance.crate_colors"), Errors);
		C.Str(TEXT("safe"), Safe);
		C.Str(TEXT("dangerous"), Dangerous);
	}
	const auto KindOf = [&File](const FString& LayerId)
	{
		const FLevelPropsLayer* L = File.Layers.FindByPredicate([&LayerId](const FLevelPropsLayer& X) { return X.Id.Equals(LayerId, ESearchCase::CaseSensitive); });
		return L ? L->Kind : FString();
	};
	const FLevelPropsResolved Resolved = LevelPropsFile::ApplyOverrides(File);
	TSet<FString> Mined;
	for (const FLevelPropsResolvedObject& R : Resolved.Objects)
	{
		if (KindOf(R.LayerId) == KindMines && !R.Object.Attach.IsEmpty())
		{
			Mined.Add(R.Object.Attach);
		}
	}
	TMap<FString, FString> Out;
	for (const FLevelPropsResolvedObject& R : Resolved.Objects)
	{
		if (KindOf(R.LayerId) == KindCrates)
		{
			Out.Add(R.Object.Id, Mined.Contains(R.Object.Id) ? Dangerous : Safe);
		}
	}
	return Out;
}
