#include "LevelGen/PropsPlacementRules.h"
#include "CodexTactics.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "LevelGen/LevelPropsFile.h"
#include "LevelGen/PropsRng.h"

#include "LevelGenJsonRead.h"

namespace PropsPlacementLocal
{
	using namespace LevelGenJson;

	const TCHAR* const GPresets = TEXT("presets");
	const TCHAR* const GPresetRef = TEXT("$preset");

	int64 DistSq(FIntPoint A, FIntPoint B)
	{
		const int64 DX = static_cast<int64>(A.X) - B.X;
		const int64 DY = static_cast<int64>(A.Y) - B.Y;
		return DX * DX + DY * DY;
	}

	/** dist(A, B) < R, exact ("at least R apart" fails). R <= 0 is never closer. */
	bool Closer(FIntPoint A, FIntPoint B, int64 R)
	{
		return R > 0 && DistSq(A, B) < R * R;
	}

	/** dist(A, B) <= R, exact (boundary inside, like the layout view's exclusion shapes). R <= 0 never. */
	bool Within(FIntPoint A, FIntPoint B, int64 R)
	{
		return R > 0 && DistSq(A, B) <= R * R;
	}

	/** Smallest multiple of the lattice step >= V (also for negatives). */
	int32 CeilStep(int32 V)
	{
		const int32 S = PropsPlacementRules::LatticeCm;
		return V >= 0 ? ((V + S - 1) / S) * S : -((-V / S) * S);
	}

	bool MatchLayer(const FString& Pattern, const FString& Name)
	{
		if (Pattern.EndsWith(TEXT("*"), ESearchCase::CaseSensitive))
		{
			return Name.StartsWith(Pattern.LeftChop(1), ESearchCase::CaseSensitive);
		}
		return Pattern.Equals(Name, ESearchCase::CaseSensitive);
	}

	bool MatchAnyLayer(const TArray<FString>& Patterns, const FString& Name)
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

	bool ContainsExact(const TArray<FString>& Items, const FString& Id)
	{
		return Items.ContainsByPredicate([&Id](const FString& S) { return S.Equals(Id, ESearchCase::CaseSensitive); });
	}

	int32 HalfOf(const FPropsRules& Rules, const FString& Variant)
	{
		const int32* Half = Rules.HalfSizeCm.Find(Variant);
		return Half ? *Half : 0;
	}

	/** The barrel's cluster key: its "cluster" field, else its own id (a lone / hand barrel is its own anchor). */
	FString ClusterOf(const FLevelPropsObject& O)
	{
		const TSharedPtr<FJsonValue>* V = O.Extra.Find(PropsPlacementRules::ClusterField);
		return V && !IsNullValue(*V) && (*V)->Type == EJson::String ? (*V)->AsString() : O.Id;
	}

	// ---- rules ------------------------------------------------------------------------------------------------------------

	/** New object: Base with Over applied; nested objects merge recursively, everything else is replaced. */
	TSharedRef<FJsonObject> MergeObjects(const FJsonObject* Base, const FJsonObject* Over)
	{
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		if (Base)
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& E : SortedEntries(*Base))
			{
				Out->SetField(E.Key, E.Value);
			}
		}
		if (Over)
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& E : SortedEntries(*Over))
			{
				const TSharedPtr<FJsonObject> B = AsObject(Out->TryGetField(E.Key));
				const TSharedPtr<FJsonObject> O = AsObject(E.Value);
				if (B.IsValid() && O.IsValid())
				{
					Out->SetField(E.Key, MakeShared<FJsonValueObject>(MergeObjects(B.Get(), O.Get())));
				}
				else
				{
					Out->SetField(E.Key, E.Value);
				}
			}
		}
		return Out;
	}

	/** A JSON number in thousandths, rounded half up (JS Math.round(x * 1000)). Config parsing only, never a placement decision. */
	bool Milli(FFieldReader& R, const TCHAR* Key, int32& Out)
	{
		const TSharedPtr<FJsonValue> V = R.Get(Key);
		if (!V.IsValid())
		{
			return false;
		}
		int32 Value = 0;
		if (V->Type != EJson::Number || !ToInt32(V->AsNumber() * 1000.0, Value))
		{
			return R.Bad(Key, TEXT("a number"));
		}
		Out = Value;
		return true;
	}

	void Strings(FFieldReader& R, const TCHAR* Key, TArray<FString>& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Items = R.Array(Key);
		if (!Items)
		{
			return;
		}
		Out.Reset();
		for (int32 I = 0; I < Items->Num(); ++I)
		{
			const TSharedPtr<FJsonValue>& V = (*Items)[I];
			if (IsNullValue(V) || V->Type != EJson::String)
			{
				R.Fail(FString::Printf(TEXT("'%s'[%d] must be a string"), Key, I));
				continue;
			}
			Out.Add(V->AsString());
		}
	}

	void NonNegative(FFieldReader& R, const TCHAR* Key, int32& Value)
	{
		int32 V = Value;
		if (R.Int(Key, V))
		{
			if (V < 0)
			{
				R.Fail(FString::Printf(TEXT("'%s' must be >= 0"), Key));
			}
			else
			{
				Value = V;
			}
		}
	}

	void ReadExclude(const FJsonObject& J, const FString& Ctx, FPropsExclusionParams& Out, TArray<FString>& Errors)
	{
		FFieldReader R(J, Ctx, Errors);
		NonNegative(R, TEXT("spawn_cm"), Out.SpawnCm);
		NonNegative(R, TEXT("end_cm"), Out.EndCm);
		NonNegative(R, TEXT("ladder_cm"), Out.LadderCm);
		NonNegative(R, TEXT("stair_cm"), Out.StairCm);
		NonNegative(R, TEXT("door_cm"), Out.DoorCm);
		NonNegative(R, TEXT("hole_rim_cm"), Out.HoleRimCm);
		R.Bool(TEXT("crossing"), Out.bCrossing);
		const TSharedPtr<FJsonValue> Route = R.Get(TEXT("route_cm"));
		if (Route.IsValid() && Route->Type == EJson::Number)
		{
			NonNegative(R, TEXT("route_cm"), Out.RouteMandatoryCm);
			Out.RouteVariantsCm = Out.RouteMandatoryCm;
		}
		else if (const TSharedPtr<FJsonObject> RouteObject = R.Object(TEXT("route_cm")))
		{
			FFieldReader RR(*RouteObject, Ctx + TEXT(".route_cm"), Errors);
			NonNegative(RR, TEXT("mandatory"), Out.RouteMandatoryCm);
			NonNegative(RR, TEXT("variants"), Out.RouteVariantsCm);
		}
	}

	// ---- placement --------------------------------------------------------------------------------------------------------

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

	EReject RejectOfExclusion(EPropsExclusionKind Kind)
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

	const TCHAR* ExclusionName(EPropsExclusionKind Kind)
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

	void Count(FPropsRejectCounts& C, EReject R)
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
	};

	/** Shared state of one Place call: the static hard filters (everything that does not depend on the picks). */
	struct FContext
	{
		const FPropsPlacementInput& In;
		const FPropsLayoutView& View;
		const FPropsRules& R;
		TArray<FPropsExclusion> Shapes;
		TArray<FString> LayerNames;
		TArray<int32> OtherHalf;

		FContext(const FPropsPlacementInput& InInput)
			: In(InInput), View(*InInput.View), R(InInput.Rules)
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

		int32 KeepOutRadius(const FLevelPropsKeepOut& K) const
		{
			return K.RadiusCm > 0 ? K.RadiusCm : R.MinSpacingCm;
		}

		/**
		 * Hard filters 2-5 of section 2.2 in their fixed order: blocked, exclusions (first containing shape), keep-outs, other layers.
		 * Half = the object's half size used for the blocked / gap tests.
		 */
		EReject StaticReject(FIntPoint P, const FString& Layer, int32 Half) const
		{
			if (View.IsBlocked(P, Layer, Half + R.MinGapAnyCm))
			{
				return EReject::Blocked;
			}
			for (const FPropsExclusion& S : Shapes)
			{
				if (S.Contains(P, Layer))
				{
					return RejectOfExclusion(S.Kind);
				}
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
				if (Closer(P, In.OtherObjects[I].Pos, static_cast<int64>(Half) + OtherHalf[I] + R.MinGapAnyCm))
				{
					return EReject::OtherLayer;
				}
			}
			return EReject::None;
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
	FString NextId(const FPropsPlacementInput& In, int32& Counter, TArray<FString>& Used)
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
	int32 DrawWeighted(FPropsRng& Rng, const TArray<FCandidate>& Cands, const TArray<bool>& Alive, uint64 Total)
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
	uint64 DropAround(const TArray<FCandidate>& Cands, TArray<bool>& Alive, int32 Picked, int32 Spacing)
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
}

// ---- BuildRules ------------------------------------------------------------------------------------------------------------------

bool PropsPlacementRules::BuildRules(const FJsonObject& Balance, const FLevelPropsLayer& Layer, FPropsRules& Out, TArray<FString>& Errors)
{
	using namespace PropsPlacementLocal;
	Out = FPropsRules();
	Out.Kind = Layer.Kind;
	Out.WalkLayers = { TEXT("ground") };
	const int32 FirstError = Errors.Num();
	const FString Ctx = FString::Printf(TEXT("layer '%s' rules"), *Layer.Id);

	const TSharedPtr<FJsonObject> Presets = AsObject(Balance.TryGetField(GPresets));
	const FString PresetName = Layer.Preset.IsEmpty() ? Layer.Kind + TEXT("_default") : Layer.Preset;
	const TSharedPtr<FJsonObject> Preset = Presets.IsValid() ? AsObject(Presets->TryGetField(PresetName)) : nullptr;
	if (!Preset.IsValid() && !Layer.Preset.IsEmpty())
	{
		Errors.Add(FString::Printf(TEXT("%s: preset '%s' not in props_balance.json"), *Ctx, *PresetName));
	}
	const TSharedRef<FJsonObject> Merged = MergeObjects(Preset.Get(), Layer.Rules.Get());

	// exclude.$preset -> presets.<name>.exclude with the remaining exclude keys on top
	if (const TSharedPtr<FJsonObject> Ex = AsObject(Merged->TryGetField(TEXT("exclude"))))
	{
		const TSharedPtr<FJsonValue> Ref = Ex->TryGetField(GPresetRef);
		if (!IsNullValue(Ref))
		{
			TSharedRef<FJsonObject> Rest = MakeShared<FJsonObject>();
			for (const TPair<FString, TSharedPtr<FJsonValue>>& E : SortedEntries(*Ex))
			{
				if (!E.Key.Equals(GPresetRef, ESearchCase::CaseSensitive))
				{
					Rest->SetField(E.Key, E.Value);
				}
			}
			const TSharedPtr<FJsonObject> RefPreset = Ref->Type == EJson::String && Presets.IsValid() ? AsObject(Presets->TryGetField(Ref->AsString())) : nullptr;
			const TSharedPtr<FJsonObject> RefExclude = RefPreset.IsValid() ? AsObject(RefPreset->TryGetField(TEXT("exclude"))) : nullptr;
			if (!RefExclude.IsValid())
			{
				Errors.Add(FString::Printf(TEXT("%s: exclude.$preset names no preset with an 'exclude' block"), *Ctx));
			}
			Merged->SetField(TEXT("exclude"), MakeShared<FJsonValueObject>(MergeObjects(RefExclude.Get(), &Rest.Get())));
		}
	}

	FFieldReader R(*Merged, Ctx, Errors);
	if (const TSharedPtr<FJsonObject> CountObject = R.Object(TEXT("count")))
	{
		FFieldReader C(*CountObject, Ctx + TEXT(".count"), Errors);
		C.Str(TEXT("mode"), Out.CountMode);
		NonNegative(C, TEXT("value"), Out.CountValue);
		Milli(C, TEXT("per_100m2"), Out.DensityMilli);
		NonNegative(C, TEXT("max"), Out.CountMax);
		if (Out.CountMode != TEXT("count") && Out.CountMode != TEXT("density") && Out.CountMode != TEXT("per_zone"))
		{
			C.Fail(FString::Printf(TEXT("unknown count mode '%s' (count | density | per_zone)"), *Out.CountMode));
			Out.CountMode = TEXT("count");
		}
	}
	if (const TSharedPtr<FJsonObject> Where = R.Object(TEXT("where")))
	{
		FFieldReader W(*Where, Ctx + TEXT(".where"), Errors);
		Strings(W, TEXT("zones"), Out.Zones);
		Strings(W, TEXT("walk_layers"), Out.WalkLayers);
		if (const TArray<TSharedPtr<FJsonValue>>* Near = W.Array(TEXT("near")))
		{
			for (int32 I = 0; I < Near->Num(); ++I)
			{
				const TSharedPtr<FJsonObject> NearObject = AsObject((*Near)[I]);
				if (!NearObject.IsValid())
				{
					W.Fail(FString::Printf(TEXT("'near'[%d] must be an object"), I));
					continue;
				}
				FFieldReader N(*NearObject, FString::Printf(TEXT("%s.where.near[%d]"), *Ctx, I), Errors);
				FPropsNearClause Clause;
				N.Str(TEXT("what"), Clause.What);
				NonNegative(N, TEXT("max_cm"), Clause.MaxCm);
				N.Int(TEXT("weight"), Clause.Weight);
				if (Clause.What != TEXT("wall") && Clause.What != TEXT("cover"))
				{
					N.Fail(FString::Printf(TEXT("'what' must be wall | cover (got '%s'); clause ignored"), *Clause.What));
					continue;
				}
				Clause.Weight = FMath::Clamp(Clause.Weight, 1, MaxWeight);
				Out.Near.Add(Clause);
			}
		}
	}
	if (const TSharedPtr<FJsonObject> Variants = R.Object(TEXT("variants")))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& E : SortedEntries(*Variants)) // ordinal key order
		{
			int32 Weight = 0;
			if (IsNullValue(E.Value) || E.Value->Type != EJson::Number || !ToInt32(E.Value->AsNumber(), Weight) || Weight < 0)
			{
				R.Fail(FString::Printf(TEXT("variants.%s must be an integer >= 0"), *E.Key));
				continue;
			}
			if (Weight > 0)
			{
				Out.Variants.Emplace(E.Key, Weight);
			}
		}
	}
	NonNegative(R, TEXT("min_spacing_cm"), Out.MinSpacingCm);
	NonNegative(R, TEXT("min_gap_any_cm"), Out.MinGapAnyCm);
	if (const TSharedPtr<FJsonObject> Ex = R.Object(TEXT("exclude")))
	{
		ReadExclude(*Ex, Ctx + TEXT(".exclude"), Out.Exclude, Errors);
	}
	if (const TSharedPtr<FJsonObject> Cluster = R.Object(TEXT("cluster")))
	{
		FFieldReader C(*Cluster, Ctx + TEXT(".cluster"), Errors);
		NonNegative(C, TEXT("min"), Out.ClusterMin);
		NonNegative(C, TEXT("max"), Out.ClusterMax);
		if (const TArray<TSharedPtr<FJsonValue>>* Ring = C.Array(TEXT("ring_cm")))
		{
			if (Ring->Num() != 2 || IsNullValue((*Ring)[0]) || IsNullValue((*Ring)[1]) || (*Ring)[0]->Type != EJson::Number
				|| (*Ring)[1]->Type != EJson::Number || !ToInt32((*Ring)[0]->AsNumber(), Out.RingMinCm) || !ToInt32((*Ring)[1]->AsNumber(), Out.RingMaxCm)
				|| Out.RingMinCm < 0 || Out.RingMaxCm < Out.RingMinCm || Out.RingMaxCm > FPropsLayoutView::MaxRadiusCm)
			{
				C.Fail(TEXT("'ring_cm' must be [min, max] with 0 <= min <= max"));
				Out.RingMinCm = 0;
				Out.RingMaxCm = 0;
			}
		}
		if (Out.ClusterMin < 1 || Out.ClusterMax < Out.ClusterMin)
		{
			C.Fail(TEXT("cluster needs 1 <= min <= max"));
			Out.ClusterMin = FMath::Max(1, Out.ClusterMin);
			Out.ClusterMax = FMath::Max(Out.ClusterMin, Out.ClusterMax);
		}
	}
	if (const TSharedPtr<FJsonObject> Explosive = R.Object(TEXT("explosive")))
	{
		FFieldReader E(*Explosive, Ctx + TEXT(".explosive"), Errors);
		Milli(E, TEXT("share"), Out.ExplosiveShareMilli);
		Out.ExplosiveShareMilli = FMath::Clamp(Out.ExplosiveShareMilli, 0, 1000);
		NonNegative(E, TEXT("min_gap_cm"), Out.ExplosiveMinGapCm);
		if (const TSharedPtr<FJsonObject> Never = E.Object(TEXT("never_within_cm_of")))
		{
			FFieldReader N(*Never, Ctx + TEXT(".explosive.never_within_cm_of"), Errors);
			NonNegative(N, TEXT("spawn"), Out.ExplosiveNeverSpawnCm);
			NonNegative(N, TEXT("end"), Out.ExplosiveNeverEndCm);
		}
	}

	// half sizes and the budget from the balance file
	if (const TSharedPtr<FJsonObject> Kinds = AsObject(Balance.TryGetField(TEXT("kinds"))))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& E : SortedEntries(*Kinds))
		{
			const TSharedPtr<FJsonObject> K = AsObject(E.Value);
			if (!K.IsValid())
			{
				continue;
			}
			int32 Half = 0;
			const TSharedPtr<FJsonValue> Radius = K->TryGetField(TEXT("radius_cm"));
			const TSharedPtr<FJsonValue> Size = K->TryGetField(TEXT("size_cm"));
			if (!IsNullValue(Radius) && Radius->Type == EJson::Number && ToInt32(Radius->AsNumber(), Half))
			{
				Out.HalfSizeCm.Add(E.Key, FMath::Max(0, Half));
			}
			else if (!IsNullValue(Size) && Size->Type == EJson::Array && Size->AsArray().Num() >= 2)
			{
				int32 X = 0;
				int32 Y = 0;
				const TArray<TSharedPtr<FJsonValue>>& S = Size->AsArray();
				if (!IsNullValue(S[0]) && !IsNullValue(S[1]) && S[0]->Type == EJson::Number && S[1]->Type == EJson::Number
					&& ToInt32(S[0]->AsNumber(), X) && ToInt32(S[1]->AsNumber(), Y))
				{
					Out.HalfSizeCm.Add(E.Key, (FMath::Max3(X, Y, 0) + 1) / 2);
				}
			}
		}
	}
	if (const TSharedPtr<FJsonObject> Budgets = AsObject(Balance.TryGetField(TEXT("budgets"))))
	{
		FFieldReader B(*Budgets, TEXT("props_balance.budgets"), Errors);
		NonNegative(B, *Layer.Kind, Out.BudgetCap);
	}

	for (int32 I = FirstError; I < Errors.Num(); ++I)
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("Props rules: %s"), *Errors[I]);
	}
	return Errors.Num() == FirstError;
}

// ---- MakeInput -------------------------------------------------------------------------------------------------------------------

bool PropsPlacementRules::MakeInput(const FLevelPropsFile& File, const FJsonObject& Balance, const FPropsLayoutView& View, const FString& LayerId,
	FPropsPlacementInput& Out, TArray<FString>& Errors)
{
	using namespace PropsPlacementLocal;
	Out = FPropsPlacementInput();
	const FLevelPropsLayer* Layer = File.Layers.FindByPredicate([&LayerId](const FLevelPropsLayer& L) { return L.Id.Equals(LayerId, ESearchCase::CaseSensitive); });
	if (!Layer)
	{
		Errors.Add(FString::Printf(TEXT("props: no layer '%s'"), *LayerId));
		return false;
	}
	BuildRules(Balance, *Layer, Out.Rules, Errors);
	Out.View = &View;
	Out.LayerId = Layer->Id;
	Out.Seed = Layer->Seed;

	const FLevelPropsResolved Resolved = LevelPropsFile::ApplyOverrides(File);
	for (const FLevelPropsResolvedObject& R : Resolved.Objects)
	{
		if (R.LayerId.Equals(Layer->Id, ESearchCase::CaseSensitive))
		{
			if (R.Object.bLocked || R.bPinned || R.bHand)
			{
				Out.Kept.Add(R.Object);
				Out.ReservedIds.Add(R.Object.Id);
			}
		}
		else
		{
			Out.OtherObjects.Add(R.Object);
			Out.ReservedIds.Add(R.Object.Id);
		}
	}
	for (const FLevelPropsKeepOut& K : Resolved.KeepOuts)
	{
		if (K.LayerId.Equals(Layer->Id, ESearchCase::CaseSensitive))
		{
			Out.KeepOuts.Add(K);
		}
	}
	for (const FLevelPropsOverride& Op : File.Overrides)
	{
		for (const FString* Id : { &Op.Id, &Op.Object.Id })
		{
			if (!Id->IsEmpty() && !ContainsExact(Out.ReservedIds, *Id))
			{
				Out.ReservedIds.Add(*Id);
			}
		}
	}
	for (const FLevelPropsLayer& L : File.Layers)
	{
		if (&L != Layer)
		{
			for (const FLevelPropsObject& O : L.Objects)
			{
				if (!ContainsExact(Out.ReservedIds, O.Id))
				{
					Out.ReservedIds.Add(O.Id);
				}
			}
		}
	}
	return true;
}

// ---- Place -----------------------------------------------------------------------------------------------------------------------

FPropsPlacementResult PropsPlacementRules::Place(const FPropsPlacementInput& In)
{
	using namespace PropsPlacementLocal;
	FPropsPlacementResult Result;
	FPropsPlacementReport& Report = Result.Report;
	Result.Objects = In.Kept;
	Report.Kept = In.Kept.Num();
	const FPropsRules& R = In.Rules;
	const bool bCrates = R.Kind == KindCrates;
	const bool bBarrels = R.Kind == KindBarrels;

	const auto Finish = [&Report]()
	{
		Report.ShortBy = FMath::Max(0, Report.Requested - Report.Kept - Report.Placed);
	};
	if (!bCrates && !bBarrels)
	{
		Report.Warnings.Add(FString::Printf(TEXT("kind_unsupported %s"), *R.Kind));
		return Result;
	}
	if (!In.View)
	{
		Report.Warnings.Add(TEXT("no_layout"));
		return Result;
	}
	if (bCrates && R.Variants.Num() == 0)
	{
		Report.Warnings.Add(TEXT("no_variants"));
		return Result;
	}

	const FContext Ctx(In);
	const FPropsLayoutView& View = *In.View;
	const int32 Half = bBarrels ? FMath::Max(HalfOf(R, VariantBarrel), HalfOf(R, VariantBarrelExplosive))
		: [&R]() { int32 H = 0; for (const TPair<FString, int32>& V : R.Variants) { H = FMath::Max(H, HalfOf(R, V.Key)); } return H; }();

	// 2. candidates: the 50 cm lattice of every matched walk layer, sorted by (y, x, layer name); 3. hard filters; 4. weights
	TArray<FCandidate> Cands;
	for (int32 LayerIndex = 0; LayerIndex < Ctx.LayerNames.Num(); ++LayerIndex)
	{
		const FString& Layer = Ctx.LayerNames[LayerIndex];
		FIntPoint Min;
		FIntPoint Max;
		if (!View.GetLayerExtent(Layer, Min, Max))
		{
			continue;
		}
		for (int32 Y = CeilStep(Min.Y); Y <= Max.Y; Y += LatticeCm)
		{
			for (int32 X = CeilStep(Min.X); X <= Max.X; X += LatticeCm)
			{
				const FIntPoint P(X, Y);
				if (!View.IsWalkable(P, Layer) || !Ctx.InZones(P))
				{
					continue;
				}
				++Report.Eligible;
				EReject Reject = Ctx.StaticReject(P, Layer, Half);
				if (Reject == EReject::None)
				{
					for (const FLevelPropsObject& K : In.Kept)
					{
						if (Closer(P, K.Pos, R.MinSpacingCm))
						{
							Reject = EReject::Spacing;
							break;
						}
					}
				}
				if (Reject != EReject::None)
				{
					Count(Report.Rejected, Reject);
					continue;
				}
				FCandidate& C = Cands.AddDefaulted_GetRef();
				C.Pos = P;
				C.Layer = LayerIndex;
				C.Weight = Ctx.WeightAt(P, Layer, Half);
			}
		}
	}
	Cands.StableSort([&Ctx](const FCandidate& A, const FCandidate& B)
	{
		if (A.Pos.Y != B.Pos.Y) { return A.Pos.Y < B.Pos.Y; }
		if (A.Pos.X != B.Pos.X) { return A.Pos.X < B.Pos.X; }
		return Ctx.LayerNames[A.Layer].Compare(Ctx.LayerNames[B.Layer], ESearchCase::CaseSensitive) < 0;
	});

	// 1. target count
	int64 Total = 0;
	if (R.CountMode == TEXT("density"))
	{
		Total = (static_cast<int64>(R.DensityMilli) * Report.Eligible + 200000) / 400000; // Eligible x 0.25 m2, per 100 m2, round half up
	}
	else if (R.CountMode == TEXT("per_zone"))
	{
		Total = static_cast<int64>(R.CountValue) * Ctx.ZoneCount();
	}
	else
	{
		Total = R.CountValue;
	}
	Total = FMath::Clamp<int64>(FMath::Min3<int64>(Total, R.CountMax, R.BudgetCap), 0, MAX_int32);
	Report.Requested = static_cast<int32>(Total);
	const int32 Target = FMath::Max(0, Report.Requested - Report.Kept);

	uint64 TotalWeight = 0;
	for (const FCandidate& C : Cands)
	{
		TotalWeight += C.Weight;
	}
	TArray<bool> Alive;
	Alive.Init(true, Cands.Num());
	if (Target > 0 && Cands.Num() == 0)
	{
		Report.Warnings.Add(TEXT("no_candidates"));
	}

	FPropsRng Rng(PropsRng::StreamSeed(In.Seed, In.LayerId));
	int32 IdCounter = 0;
	TArray<FString> UsedIds;
	TArray<FLevelPropsObject> New;
	const FPropsNearClause* WallClause = R.Near.FindByPredicate([](const FPropsNearClause& C) { return C.What == TEXT("wall"); });

	if (bCrates)
	{
		int32 VariantTotal = 0;
		for (const TPair<FString, int32>& V : R.Variants)
		{
			VariantTotal += V.Value;
		}
		// 5. pick: candidate, variant, yaw (in this order)
		while (New.Num() < Target && TotalWeight > 0)
		{
			const int32 Pick = DrawWeighted(Rng, Cands, Alive, TotalWeight);
			if (Pick == INDEX_NONE)
			{
				break;
			}
			const FCandidate& C = Cands[Pick];
			FLevelPropsObject& O = New.AddDefaulted_GetRef();
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
			bool bAlongX = false;
			if (WallClause && View.FindNearestWallAxis(O.Pos, O.WalkLayer, Half + WallClause->MaxCm, bAlongX))
			{
				O.Yaw = bAlongX ? 0 : 90;
			}
			else
			{
				O.Yaw = static_cast<int32>(Rng.NextU32() % 4u) * 90;
			}
			O.Id = NextId(In, IdCounter, UsedIds);
			TotalWeight -= DropAround(Cands, Alive, Pick, R.MinSpacingCm);
		}
	}
	else
	{
		// ring offsets on the lattice, RingMin <= |d| <= RingMax, sorted by (dy, dx)
		TArray<FIntPoint> Ring;
		const int32 Reach = (R.RingMaxCm / LatticeCm) * LatticeCm;
		for (int32 DY = -Reach; DY <= Reach; DY += LatticeCm)
		{
			for (int32 DX = -Reach; DX <= Reach; DX += LatticeCm)
			{
				const int64 D2 = static_cast<int64>(DX) * DX + static_cast<int64>(DY) * DY;
				if (D2 >= static_cast<int64>(R.RingMinCm) * R.RingMinCm && D2 <= static_cast<int64>(R.RingMaxCm) * R.RingMaxCm && D2 > 0)
				{
					Ring.Add(FIntPoint(DX, DY));
				}
			}
		}
		const int32 BarrelHalf = HalfOf(R, VariantBarrel);
		const int32 MemberGap = FMath::Max(R.RingMinCm, 2 * BarrelHalf);
		const auto MemberOk = [&](FIntPoint Q, const FString& Layer)
		{
			if (!View.IsWalkable(Q, Layer) || Ctx.StaticReject(Q, Layer, BarrelHalf) != EReject::None)
			{
				return false;
			}
			for (const FLevelPropsObject& K : In.Kept)
			{
				if (Closer(Q, K.Pos, MemberGap)) { return false; }
			}
			for (const FLevelPropsObject& O : New)
			{
				if (Closer(Q, O.Pos, MemberGap)) { return false; }
			}
			return true;
		};

		TArray<TArray<int32>> Clusters;
		// 5. pick per cluster: anchor, size, members
		while (New.Num() < Target && TotalWeight > 0)
		{
			const int32 Pick = DrawWeighted(Rng, Cands, Alive, TotalWeight);
			if (Pick == INDEX_NONE)
			{
				break;
			}
			const FCandidate& C = Cands[Pick];
			const FString& Layer = Ctx.LayerNames[C.Layer];
			int32 Size = R.ClusterMin + static_cast<int32>(Rng.NextBelow(static_cast<uint32>(R.ClusterMax - R.ClusterMin + 1)));
			Size = FMath::Min(Size, Target - New.Num());
			TArray<int32>& Members = Clusters.AddDefaulted_GetRef();

			FLevelPropsObject Anchor;
			Anchor.Id = NextId(In, IdCounter, UsedIds);
			Anchor.Variant = VariantBarrel;
			Anchor.Pos = C.Pos;
			Anchor.WalkLayer = Layer;
			Anchor.Extra.Add(ClusterField, MakeShared<FJsonValueString>(Anchor.Id));
			const FString AnchorId = Anchor.Id;
			Members.Add(New.Add(MoveTemp(Anchor)));
			for (int32 M = 1; M < Size; ++M)
			{
				TArray<FIntPoint> Valid;
				for (const FIntPoint& Off : Ring)
				{
					if (MemberOk(C.Pos + Off, Layer))
					{
						Valid.Add(C.Pos + Off);
					}
				}
				if (Valid.Num() == 0)
				{
					break;
				}
				FLevelPropsObject Member;
				Member.Pos = Valid[static_cast<int32>(Rng.NextBelow(static_cast<uint32>(Valid.Num())))];
				Member.Id = NextId(In, IdCounter, UsedIds);
				Member.Variant = VariantBarrel;
				Member.WalkLayer = Layer;
				Member.Extra.Add(ClusterField, MakeShared<FJsonValueString>(AnchorId));
				Members.Add(New.Add(MoveTemp(Member)));
			}
			TotalWeight -= DropAround(Cands, Alive, Pick, R.MinSpacingCm);
		}

		// explosive clusters: round(share x clusters), shuffled order, a cluster qualifies when every member keeps the gaps
		const int32 Wanted = (R.ExplosiveShareMilli * Clusters.Num() + 500) / 1000;
		if (Wanted > 0)
		{
			TArray<int32> Order;
			for (int32 I = 0; I < Clusters.Num(); ++I)
			{
				Order.Add(I);
			}
			Rng.Shuffle(Order);
			TArray<FIntPoint> Explosives;
			for (const FLevelPropsObject& K : In.Kept)
			{
				if (K.Variant == VariantBarrelExplosive)
				{
					Explosives.Add(K.Pos);
				}
			}
			int32 Chosen = 0;
			for (const int32 ClusterIndex : Order)
			{
				if (Chosen == Wanted)
				{
					break;
				}
				bool bOk = true;
				for (const int32 MemberIndex : Clusters[ClusterIndex])
				{
					const FIntPoint P = New[MemberIndex].Pos;
					bOk &= !Within(P, View.GetSpawnPos(), R.ExplosiveNeverSpawnCm) && !Within(P, View.GetEndPos(), R.ExplosiveNeverEndCm);
					for (const FIntPoint& E : Explosives)
					{
						bOk &= !Closer(P, E, R.ExplosiveMinGapCm);
					}
				}
				if (!bOk)
				{
					continue;
				}
				for (const int32 MemberIndex : Clusters[ClusterIndex])
				{
					New[MemberIndex].Variant = VariantBarrelExplosive;
					Explosives.Add(New[MemberIndex].Pos);
				}
				++Chosen;
			}
			if (Chosen < Wanted)
			{
				Report.Warnings.Add(FString::Printf(TEXT("explosive_short %d"), Wanted - Chosen));
			}
		}
	}

	Report.Placed = New.Num();
	Result.Objects.Append(MoveTemp(New));
	Finish();
	return Result;
}

// ---- Validate --------------------------------------------------------------------------------------------------------------------

TArray<FPropsViolation> PropsPlacementRules::Validate(const FPropsPlacementInput& In, const TArray<FLevelPropsObject>& Objects)
{
	using namespace PropsPlacementLocal;
	TArray<FPropsViolation> Out;
	if (!In.View)
	{
		return Out;
	}
	const FContext Ctx(In);
	const FPropsLayoutView& View = *In.View;
	const FPropsRules& R = In.Rules;
	const bool bBarrels = R.Kind == KindBarrels;
	const auto IsKept = [&In](const FLevelPropsObject& O)
	{
		return In.Kept.ContainsByPredicate([&O](const FLevelPropsObject& K) { return K.Id.Equals(O.Id, ESearchCase::CaseSensitive); });
	};
	const auto Add = [&Out](const FLevelPropsObject& O, const FString& Reason)
	{
		Out.Add({ O.Id, Reason });
	};
	const int32 MemberGap = FMath::Max(R.RingMinCm, 2 * HalfOf(R, VariantBarrel));

	for (const FLevelPropsObject& O : Objects)
	{
		if (IsKept(O))
		{
			continue;
		}
		const int32 Half = HalfOf(R, O.Variant);
		if (!MatchAnyLayer(R.WalkLayers, O.WalkLayer))
		{
			Add(O, FString::Printf(TEXT("walk layer '%s' not allowed by the rules"), *O.WalkLayer));
		}
		if (!View.IsWalkable(O.Pos, O.WalkLayer))
		{
			Add(O, TEXT("not on its walk layer"));
		}
		if (View.IsBlocked(O.Pos, O.WalkLayer, Half + R.MinGapAnyCm))
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
			if (Closer(O.Pos, In.OtherObjects[I].Pos, static_cast<int64>(Half) + Ctx.OtherHalf[I] + R.MinGapAnyCm))
			{
				Add(O, FString::Printf(TEXT("min_gap_any to %s"), *In.OtherObjects[I].Id));
			}
		}
		if (!bBarrels)
		{
			for (const FLevelPropsObject& P : Objects)
			{
				if (&P != &O && Closer(O.Pos, P.Pos, R.MinSpacingCm))
				{
					Add(O, FString::Printf(TEXT("min_spacing to %s"), *P.Id));
				}
			}
			continue;
		}

		// barrels: anchors keep min_spacing to every kept barrel and every other anchor; all barrels keep the member gap
		const FString Cluster = ClusterOf(O);
		const bool bAnchor = Cluster.Equals(O.Id, ESearchCase::CaseSensitive);
		for (const FLevelPropsObject& P : Objects)
		{
			if (&P == &O)
			{
				continue;
			}
			const bool bOtherAnchorLike = IsKept(P) || ClusterOf(P).Equals(P.Id, ESearchCase::CaseSensitive);
			if (bAnchor && bOtherAnchorLike && Closer(O.Pos, P.Pos, R.MinSpacingCm))
			{
				Add(O, FString::Printf(TEXT("anchor min_spacing to %s"), *P.Id));
			}
			if (Closer(O.Pos, P.Pos, MemberGap))
			{
				Add(O, FString::Printf(TEXT("member gap to %s"), *P.Id));
			}
			if (!bAnchor && P.Id.Equals(Cluster, ESearchCase::CaseSensitive) && !Within(O.Pos, P.Pos, R.RingMaxCm))
			{
				Add(O, FString::Printf(TEXT("farther than ring_cm max from its anchor %s"), *P.Id));
			}
			if (O.Variant == VariantBarrelExplosive && P.Variant == VariantBarrelExplosive && !ClusterOf(P).Equals(Cluster, ESearchCase::CaseSensitive)
				&& Closer(O.Pos, P.Pos, R.ExplosiveMinGapCm))
			{
				Add(O, FString::Printf(TEXT("explosive min_gap to %s"), *P.Id));
			}
		}
		if (O.Variant == VariantBarrelExplosive
			&& (Within(O.Pos, View.GetSpawnPos(), R.ExplosiveNeverSpawnCm) || Within(O.Pos, View.GetEndPos(), R.ExplosiveNeverEndCm)))
		{
			Add(O, TEXT("explosive within never_within_cm_of"));
		}
	}
	return Out;
}

// ---- report / reroll -------------------------------------------------------------------------------------------------------------

TSharedRef<FJsonObject> PropsPlacementRules::ReportToJson(const FPropsPlacementReport& Report)
{
	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetNumberField(TEXT("requested"), Report.Requested);
	J->SetNumberField(TEXT("placed"), Report.Placed);
	J->SetNumberField(TEXT("kept"), Report.Kept);
	J->SetNumberField(TEXT("short_by"), Report.ShortBy);
	J->SetNumberField(TEXT("eligible"), Report.Eligible);
	J->SetStringField(TEXT("status"), Report.IsShort() ? TEXT("short") : TEXT("ok"));
	J->SetStringField(TEXT("gate"), Report.Gate);
	TSharedRef<FJsonObject> Rejected = MakeShared<FJsonObject>();
	Rejected->SetNumberField(TEXT("blocked"), Report.Rejected.Blocked);
	Rejected->SetNumberField(TEXT("spacing"), Report.Rejected.Spacing);
	Rejected->SetNumberField(TEXT("route"), Report.Rejected.Route);
	Rejected->SetNumberField(TEXT("spawn"), Report.Rejected.Spawn);
	Rejected->SetNumberField(TEXT("ladder"), Report.Rejected.Ladder);
	Rejected->SetNumberField(TEXT("door"), Report.Rejected.Door);
	Rejected->SetNumberField(TEXT("keepout"), Report.Rejected.Keepout);
	Rejected->SetNumberField(TEXT("other_layer"), Report.Rejected.OtherLayer);
	Rejected->SetNumberField(TEXT("other"), Report.Rejected.Other);
	J->SetObjectField(TEXT("rejected"), Rejected);
	TArray<TSharedPtr<FJsonValue>> Warnings;
	for (const FString& W : Report.Warnings)
	{
		Warnings.Add(MakeShared<FJsonValueString>(W));
	}
	J->SetArrayField(TEXT("warnings"), Warnings);
	return J;
}

bool PropsPlacementRules::RerollLayer(FLevelPropsFile& File, const FJsonObject& Balance, const FPropsLayoutView& View, const FString& LayerId,
	uint32 Seed, TArray<FString>& Errors, FPropsPlacementResult* OutResult)
{
	FLevelPropsLayer* Layer = File.Layers.FindByPredicate([&LayerId](const FLevelPropsLayer& L) { return L.Id.Equals(LayerId, ESearchCase::CaseSensitive); });
	if (File.bFrozen)
	{
		Errors.Add(TEXT("props: the file is frozen; re-roll refused"));
		return false;
	}
	if (!Layer)
	{
		Errors.Add(FString::Printf(TEXT("props: no layer '%s'"), *LayerId));
		return false;
	}
	if (Layer->bLocked)
	{
		Errors.Add(FString::Printf(TEXT("props: layer '%s' is locked; re-roll refused"), *LayerId));
		return false;
	}
	if (Layer->Kind != KindCrates && Layer->Kind != KindBarrels)
	{
		Errors.Add(FString::Printf(TEXT("props: layer '%s' kind '%s' is placed by C4 (not yet)"), *LayerId, *Layer->Kind));
		return false;
	}
	const uint32 OldSeed = Layer->Seed;
	Layer->Seed = Seed;
	FPropsPlacementInput In;
	if (!MakeInput(File, Balance, View, LayerId, In, Errors))
	{
		Layer->Seed = OldSeed;
		return false;
	}
	FPropsPlacementResult Result = Place(In);

	TArray<FLevelPropsObject> Stored;
	for (int32 I = 0; I < Result.Objects.Num(); ++I)
	{
		const FLevelPropsObject& O = Result.Objects[I];
		if (I < In.Kept.Num())
		{
			// a kept object keeps its stored record (its overrides re-apply at load); hand-added ones live in the overrides
			if (const FLevelPropsObject* Record = Layer->Objects.FindByPredicate([&O](const FLevelPropsObject& S) { return S.Id.Equals(O.Id, ESearchCase::CaseSensitive); }))
			{
				Stored.Add(*Record);
			}
			continue;
		}
		Stored.Add(O);
	}
	Layer->Objects = MoveTemp(Stored);
	Layer->Report = ReportToJson(Result.Report);
	if (OutResult)
	{
		*OutResult = MoveTemp(Result);
	}
	return true;
}
