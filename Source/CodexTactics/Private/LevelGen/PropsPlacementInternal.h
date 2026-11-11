#pragma once

#include "CoreMinimal.h"
#include "LevelGen/PropsPlacementRules.h"
#include "LevelGen/PropsRng.h"

/**
 * Internals shared by PropsPlacementRules.cpp (C3: rules, crates, barrels, re-roll) and PropsPlacementC4.cpp (C4: turrets, mines, loot)
 * of docs/design/levelgen/PROPS_LAYERS.md. Private to the CodexTactics module. Every decision is exact integer maths; the TS port
 * follows section 7 of the design. UE-only (no Godot reference).
 */
namespace PropsPlacementLocal
{
	inline int64 DistSq(FIntPoint A, FIntPoint B)
	{
		const int64 DX = static_cast<int64>(A.X) - B.X;
		const int64 DY = static_cast<int64>(A.Y) - B.Y;
		return DX * DX + DY * DY;
	}

	/** dist(A, B) < R, exact ("at least R apart" fails). R <= 0 is never closer. */
	inline bool Closer(FIntPoint A, FIntPoint B, int64 R)
	{
		return R > 0 && DistSq(A, B) < R * R;
	}

	/** dist(A, B) <= R, exact (boundary inside, like the layout view's exclusion shapes). R <= 0 never. */
	inline bool Within(FIntPoint A, FIntPoint B, int64 R)
	{
		return R > 0 && DistSq(A, B) <= R * R;
	}

	/** Smallest multiple of the lattice step >= V (also for negatives). */
	inline int32 CeilStep(int32 V)
	{
		const int32 S = PropsPlacementRules::LatticeCm;
		return V >= 0 ? ((V + S - 1) / S) * S : -((-V / S) * S);
	}

	/** Largest multiple of the lattice step <= V (also for negatives). */
	inline int32 FloorStep(int32 V)
	{
		const int32 S = PropsPlacementRules::LatticeCm;
		return V >= 0 ? (V / S) * S : -(((-V) + S - 1) / S) * S;
	}

	inline bool MatchLayer(const FString& Pattern, const FString& Name)
	{
		if (Pattern.EndsWith(TEXT("*"), ESearchCase::CaseSensitive))
		{
			return Name.StartsWith(Pattern.LeftChop(1), ESearchCase::CaseSensitive);
		}
		return Pattern.Equals(Name, ESearchCase::CaseSensitive);
	}

	inline bool MatchAnyLayer(const TArray<FString>& Patterns, const FString& Name)
	{
		for (const FString& P : Patterns)
		{
			if (MatchLayer(P, Name))
			{
				return true;
			}
		}
		return false;
	}

	inline bool ContainsExact(const TArray<FString>& Items, const FString& Id)
	{
		return Items.ContainsByPredicate([&Id](const FString& S) { return S.Equals(Id, ESearchCase::CaseSensitive); });
	}

	inline int32 HalfOf(const FPropsCommonRules& Rules, const FString& Variant)
	{
		const int32* Half = Rules.HalfSizeCm.Find(Variant);
		return Half ? *Half : 0;
	}

	enum class EReject : uint8
	{
		None,
		Blocked,
		Route,
		Spawn,
		Ladder,
		Door,
		Keepout,
		OtherLayer,
		Spacing,
		Other
	};

	inline EReject RejectOfExclusion(EPropsExclusionKind Kind)
	{
		switch (Kind)
		{
		case EPropsExclusionKind::Spawn:
		case EPropsExclusionKind::End:
			return EReject::Spawn;
		case EPropsExclusionKind::LadderFoot:
		case EPropsExclusionKind::LadderTop:
		case EPropsExclusionKind::Stair:
			return EReject::Ladder;
		case EPropsExclusionKind::Door:
			return EReject::Door;
		case EPropsExclusionKind::Route:
		case EPropsExclusionKind::Crossing:
			return EReject::Route;
		default:
			return EReject::Other;
		}
	}

	inline const TCHAR* ExclusionName(EPropsExclusionKind Kind)
	{
		switch (Kind)
		{
		case EPropsExclusionKind::Spawn: return TEXT("spawn");
		case EPropsExclusionKind::End: return TEXT("end");
		case EPropsExclusionKind::LadderFoot: return TEXT("ladder_foot");
		case EPropsExclusionKind::LadderTop: return TEXT("ladder_top");
		case EPropsExclusionKind::Stair: return TEXT("stair");
		case EPropsExclusionKind::Door: return TEXT("door");
		case EPropsExclusionKind::Route: return TEXT("route");
		case EPropsExclusionKind::Crossing: return TEXT("crossing");
		default: return TEXT("hole_rim");
		}
	}

	inline void Count(FPropsRejectCounts& C, EReject R)
	{
		switch (R)
		{
		case EReject::Blocked: ++C.Blocked; break;
		case EReject::Route: ++C.Route; break;
		case EReject::Spawn: ++C.Spawn; break;
		case EReject::Ladder: ++C.Ladder; break;
		case EReject::Door: ++C.Door; break;
		case EReject::Keepout: ++C.Keepout; break;
		case EReject::OtherLayer: ++C.OtherLayer; break;
		case EReject::Spacing: ++C.Spacing; break;
		case EReject::Other: ++C.Other; break;
		default: break;
		}
	}

	struct FCandidate
	{
		FIntPoint Pos = FIntPoint::ZeroValue;
		int32 Layer = 0;
		uint32 Weight = 1;
		/** Turrets: the best arc's yaw and the score (covered metres). */
		int32 Yaw = 0;
		int32 Score = 0;
	};

	/**
	 * Shared state of one placement pass: the static hard filters (everything that does not depend on the picks) for one rule set (the
	 * layer's own, or the mines field sub-rule). Keep-outs of radius 0 always use the layer's top-level min_spacing_cm.
	 */
	struct FContext
	{
		const FPropsPlacementInput& In;
		const FPropsLayoutView& View;
		const FPropsCommonRules& R;
		TArray<FPropsExclusion> Shapes;
		TArray<FString> LayerNames;
		TArray<int32> OtherHalf;

		explicit FContext(const FPropsPlacementInput& InInput)
			: FContext(InInput, InInput.Rules)
		{
		}

		FContext(const FPropsPlacementInput& InInput, const FPropsCommonRules& InRules)
			: In(InInput), View(*InInput.View), R(InRules)
		{
			Shapes = View.BuildExclusions(R.Exclude);
			for (const FPropsWalkLayer& L : View.GetLayers())
			{
				if (MatchAnyLayer(R.WalkLayers, L.Name))
				{
					LayerNames.Add(L.Name);
				}
			}
			for (const FLevelPropsObject& O : In.OtherObjects)
			{
				OtherHalf.Add(HalfOf(R, O.Variant));
			}
		}

		/** An object attached to another (a crate mine, loot in a crate: "attach" set) lives inside its host: never an obstacle (C4). */
		bool IsObstacle(int32 OtherIndex) const
		{
			return In.OtherObjects[OtherIndex].Attach.IsEmpty();
		}

		int32 KeepOutRadius(const FLevelPropsKeepOut& K) const
		{
			return K.RadiusCm > 0 ? K.RadiusCm : In.Rules.MinSpacingCm;
		}

		int32 LayerIndexOf(const FString& Layer) const
		{
			return LayerNames.IndexOfByPredicate([&Layer](const FString& N) { return N.Equals(Layer, ESearchCase::CaseSensitive); });
		}

		/** The first exclusion shape containing P on Layer, or null. */
		const FPropsExclusion* FirstShape(FIntPoint P, const FString& Layer) const
		{
			for (const FPropsExclusion& S : Shapes)
			{
				if (S.Contains(P, Layer))
				{
					return &S;
				}
			}
			return nullptr;
		}

		/**
		 * Hard filters 1-4 of section 2.2 in their fixed order: blocked, exclusions (first containing shape), keep-outs, other layers.
		 * Half = the object's half size used for the blocked / gap tests.
		 */
		EReject StaticReject(FIntPoint P, const FString& Layer, int32 Half) const
		{
			if (View.IsBlocked(P, Layer, Half + R.WallGapCm))
			{
				return EReject::Blocked;
			}
			if (const FPropsExclusion* S = FirstShape(P, Layer))
			{
				return RejectOfExclusion(S->Kind);
			}
			for (const FLevelPropsKeepOut& K : In.KeepOuts)
			{
				if (Closer(P, K.Pos, KeepOutRadius(K)))
				{
					return EReject::Keepout;
				}
			}
			for (int32 I = 0; I < In.OtherObjects.Num(); ++I)
			{
				if (IsObstacle(I) && Closer(P, In.OtherObjects[I].Pos, static_cast<int64>(Half) + OtherHalf[I] + R.MinGapAnyCm))
				{
					return EReject::OtherLayer;
				}
			}
			return EReject::None;
		}

		/** StaticReject, then filter 5: an object of Spaced closer than min_spacing_cm. */
		EReject FullReject(FIntPoint P, const FString& Layer, int32 Half, const TArray<FLevelPropsObject>& Spaced) const
		{
			EReject Reject = StaticReject(P, Layer, Half);
			if (Reject == EReject::None)
			{
				for (const FLevelPropsObject& K : Spaced)
				{
					if (Closer(P, K.Pos, R.MinSpacingCm))
					{
						return EReject::Spacing;
					}
				}
			}
			return Reject;
		}

		bool InZones(FIntPoint P) const
		{
			if (R.Zones.Num() == 0 || ContainsExact(R.Zones, TEXT("*")))
			{
				return true;
			}
			for (const FString& Z : R.Zones)
			{
				if (View.IsInZone(P, Z))
				{
					return true;
				}
			}
			return false;
		}

		int32 ZoneCount() const
		{
			if (R.Zones.Num() == 0 || ContainsExact(R.Zones, TEXT("*")))
			{
				return View.GetZones().Num();
			}
			int32 N = 0;
			for (const FString& Z : R.Zones)
			{
				N += View.FindZone(Z) ? 1 : 0;
			}
			return N;
		}

		uint32 WeightAt(FIntPoint P, const FString& Layer, int32 Half) const
		{
			int64 W = 1;
			for (const FPropsNearClause& C : R.Near)
			{
				const bool bNear = C.What == TEXT("wall") ? View.IsNearWall(P, Layer, Half + C.MaxCm)
					: C.What == TEXT("cover") ? View.IsNearCover(P, Layer, Half + C.MaxCm) : false;
				if (bNear)
				{
					W = FMath::Min<int64>(W * FMath::Clamp(C.Weight, 1, PropsPlacementRules::MaxWeight), PropsPlacementRules::MaxWeight);
				}
			}
			return static_cast<uint32>(W);
		}
	};

	/** The next free id "<layer>_<seed hex8>_<n>". */
	inline FString NextId(const FPropsPlacementInput& In, int32& Counter, TArray<FString>& Used)
	{
		for (;;)
		{
			FString Id = FString::Printf(TEXT("%s_%08x_%d"), *In.LayerId, In.Seed, Counter++);
			if (!ContainsExact(In.ReservedIds, Id) && !ContainsExact(Used, Id))
			{
				Used.Add(Id);
				return Id;
			}
		}
	}

	/** Weighted draw over the alive candidates in list order: roll = NextBelow(total), first cumulative weight > roll. */
	inline int32 DrawWeighted(FPropsRng& Rng, const TArray<FCandidate>& Cands, const TArray<bool>& Alive, uint64 Total)
	{
		const uint64 Roll = Rng.NextBelow(static_cast<uint32>(Total));
		uint64 Cum = 0;
		for (int32 I = 0; I < Cands.Num(); ++I)
		{
			if (Alive[I])
			{
				Cum += Cands[I].Weight;
				if (Roll < Cum)
				{
					return I;
				}
			}
		}
		return INDEX_NONE; // unreachable while Total is the sum of the alive weights
	}

	/** Kills the picked candidate and every alive candidate closer than Spacing; returns the weight removed. */
	inline uint64 DropAround(const TArray<FCandidate>& Cands, TArray<bool>& Alive, int32 Picked, int32 Spacing)
	{
		uint64 Removed = 0;
		const FIntPoint P = Cands[Picked].Pos;
		for (int32 I = 0; I < Cands.Num(); ++I)
		{
			if (Alive[I] && (I == Picked || Closer(Cands[I].Pos, P, Spacing)))
			{
				Alive[I] = false;
				Removed += Cands[I].Weight;
			}
		}
		return Removed;
	}

	/** Sorts candidates by (y, x, layer name ordinal), stable. */
	inline void SortCandidates(const FContext& Ctx, TArray<FCandidate>& Cands)
	{
		Cands.StableSort([&Ctx](const FCandidate& A, const FCandidate& B)
		{
			if (A.Pos.Y != B.Pos.Y) { return A.Pos.Y < B.Pos.Y; }
			if (A.Pos.X != B.Pos.X) { return A.Pos.X < B.Pos.X; }
			return Ctx.LayerNames[A.Layer].Compare(Ctx.LayerNames[B.Layer], ESearchCase::CaseSensitive) < 0;
		});
	}

	/**
	 * Steps 2-4 of section 2.2 on the 50 cm lattice of every matched walk layer: eligible points (walkable + zones) are counted, the
	 * first failing hard filter is counted in Rejected, the rest become weighted candidates sorted by (y, x, layer name).
	 */
	inline void GatherLattice(const FContext& Ctx, int32 Half, const TArray<FLevelPropsObject>& Spaced, int32& Eligible, FPropsRejectCounts& Rejected,
		TArray<FCandidate>& Out)
	{
		const int32 Step = PropsPlacementRules::LatticeCm;
		for (int32 LayerIndex = 0; LayerIndex < Ctx.LayerNames.Num(); ++LayerIndex)
		{
			const FString& Layer = Ctx.LayerNames[LayerIndex];
			FIntPoint Min;
			FIntPoint Max;
			if (!Ctx.View.GetLayerExtent(Layer, Min, Max))
			{
				continue;
			}
			for (int32 Y = CeilStep(Min.Y); Y <= Max.Y; Y += Step)
			{
				for (int32 X = CeilStep(Min.X); X <= Max.X; X += Step)
				{
					const FIntPoint P(X, Y);
					if (!Ctx.View.IsWalkable(P, Layer) || !Ctx.InZones(P))
					{
						continue;
					}
					++Eligible;
					const EReject Reject = Ctx.FullReject(P, Layer, Half, Spaced);
					if (Reject != EReject::None)
					{
						Count(Rejected, Reject);
						continue;
					}
					FCandidate& C = Out.AddDefaulted_GetRef();
					C.Pos = P;
					C.Layer = LayerIndex;
					C.Weight = Ctx.WeightAt(P, Layer, Half);
				}
			}
		}
		SortCandidates(Ctx, Out);
	}

	/** Step 1 of section 2.2: count / density / per_zone, then min(target, count.max, budget), >= 0. */
	inline int32 ComputeRequested(const FPropsCommonRules& R, int32 Eligible, int32 ZoneCount)
	{
		int64 Total = 0;
		if (R.CountMode == TEXT("density"))
		{
			Total = (static_cast<int64>(R.DensityMilli) * Eligible + 200000) / 400000; // Eligible x 0.25 m2, per 100 m2, round half up
		}
		else if (R.CountMode == TEXT("per_zone"))
		{
			Total = static_cast<int64>(R.CountValue) * ZoneCount;
		}
		else
		{
			Total = R.CountValue;
		}
		return static_cast<int32>(FMath::Clamp<int64>(FMath::Min3<int64>(Total, R.CountMax, R.BudgetCap), 0, MAX_int32));
	}

	inline uint64 TotalWeightOf(const TArray<FCandidate>& Cands)
	{
		uint64 Total = 0;
		for (const FCandidate& C : Cands)
		{
			Total += C.Weight;
		}
		return Total;
	}

	/** C4 entry points (PropsPlacementC4.cpp). Result.Objects holds the kept objects on entry. */
	void PlaceTurrets(const FPropsPlacementInput& In, FPropsPlacementResult& Result);
	void PlaceMines(const FPropsPlacementInput& In, FPropsPlacementResult& Result);
	void PlaceLoot(const FPropsPlacementInput& In, FPropsPlacementResult& Result);
	void ValidateC4(const FPropsPlacementInput& In, const TArray<FLevelPropsObject>& Objects, TArray<FPropsViolation>& Out);
}
