#include "LevelGen/PropsPlacementRules.h"
#include "CodexTactics.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "LevelGen/LevelPropsFile.h"
#include "LevelGen/PropsRng.h"

#include "LevelGenJsonRead.h"
#include "PropsPlacementInternal.h"

namespace PropsPlacementLocal
{
	using namespace LevelGenJson;

	const TCHAR* const GPresets = TEXT("presets");
	const TCHAR* const GPresetRef = TEXT("$preset");

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

	const TCHAR* const GAnchorNames[] = { TEXT("ambush_points"), TEXT("roof_edges"), TEXT("behind_tall_cover"), TEXT("dead_ends"),
		TEXT("roof_centres"), TEXT("building_interiors") };

	bool IsAnchorName(const FString& Name)
	{
		for (const TCHAR* A : GAnchorNames)
		{
			if (Name.Equals(A, ESearchCase::CaseSensitive))
			{
				return true;
			}
		}
		return false;
	}

	/** where: zones, walk_layers, near, anchors (C4). */
	void ReadWhere(const FJsonObject& Where, const FString& Ctx, FPropsCommonRules& Out, TArray<FString>& Errors)
	{
		FFieldReader W(Where, Ctx, Errors);
		Strings(W, TEXT("zones"), Out.Zones);
		Strings(W, TEXT("walk_layers"), Out.WalkLayers);
		if (const TArray<TSharedPtr<FJsonValue>>* Near = W.Array(TEXT("near")))
		{
			Out.Near.Reset();
			for (int32 I = 0; I < Near->Num(); ++I)
			{
				const TSharedPtr<FJsonObject> NearObject = AsObject((*Near)[I]);
				if (!NearObject.IsValid())
				{
					W.Fail(FString::Printf(TEXT("'near'[%d] must be an object"), I));
					continue;
				}
				FFieldReader N(*NearObject, FString::Printf(TEXT("%s.near[%d]"), *Ctx, I), Errors);
				FPropsNearClause Clause;
				N.Str(TEXT("what"), Clause.What);
				NonNegative(N, TEXT("max_cm"), Clause.MaxCm);
				N.Int(TEXT("weight"), Clause.Weight);
				if (Clause.What != TEXT("wall") && Clause.What != TEXT("cover"))
				{
					N.Fail(FString::Printf(TEXT("'what' must be wall | cover (got '%s'); clause ignored"), *Clause.What));
					continue;
				}
				Clause.Weight = FMath::Clamp(Clause.Weight, 1, PropsPlacementRules::MaxWeight);
				Out.Near.Add(Clause);
			}
		}
		if (W.Has(TEXT("anchors")))
		{
			TArray<FString> Anchors;
			Strings(W, TEXT("anchors"), Anchors);
			Out.Anchors.Reset();
			for (const FString& A : Anchors)
			{
				if (IsAnchorName(A))
				{
					Out.Anchors.Add(A);
				}
				else
				{
					W.Fail(FString::Printf(TEXT("unknown anchor '%s'; ignored"), *A));
				}
			}
		}
	}

	/** The common block (count, where, variants, spacing, gaps, exclude), in the C3 read order. */
	void ReadCommon(FFieldReader& R, const FString& Ctx, FPropsCommonRules& Out, TArray<FString>& Errors)
	{
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
			ReadWhere(*Where, Ctx + TEXT(".where"), Out, Errors);
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
		Out.WallGapCm = Out.MinGapAnyCm;
		NonNegative(R, TEXT("wall_gap_cm"), Out.WallGapCm);
		if (const TSharedPtr<FJsonObject> Ex = R.Object(TEXT("exclude")))
		{
			ReadExclude(*Ex, Ctx + TEXT(".exclude"), Out.Exclude, Errors);
		}
	}

	/** [min, max] of two integers, 0 <= min <= max (each clamped to Cap). False (error) on anything else; Out unchanged then. */
	bool ReadRange(FFieldReader& R, const TCHAR* Key, int32& OutMin, int32& OutMax, int32 Cap)
	{
		const TArray<TSharedPtr<FJsonValue>>* Range = R.Array(Key);
		if (!Range)
		{
			return false;
		}
		int32 A = 0;
		int32 B = 0;
		if (Range->Num() != 2 || IsNullValue((*Range)[0]) || IsNullValue((*Range)[1]) || (*Range)[0]->Type != EJson::Number
			|| (*Range)[1]->Type != EJson::Number || !ToInt32((*Range)[0]->AsNumber(), A) || !ToInt32((*Range)[1]->AsNumber(), B)
			|| A < 0 || B < A)
		{
			R.Fail(FString::Printf(TEXT("'%s' must be [min, max] with 0 <= min <= max"), Key));
			return false;
		}
		OutMin = FMath::Min(A, Cap);
		OutMax = FMath::Min(B, Cap);
		return true;
	}

	/** Container.exclude.$preset resolved like the layer's own (Container is a copy; the balance JSON is never mutated). */
	void ResolveExcludePreset(FJsonObject& Container, const TSharedPtr<FJsonObject>& Presets, const FString& Ctx, TArray<FString>& Errors)
	{
		const TSharedPtr<FJsonObject> Ex = AsObject(Container.TryGetField(TEXT("exclude")));
		if (!Ex.IsValid())
		{
			return;
		}
		const TSharedPtr<FJsonValue> Ref = Ex->TryGetField(GPresetRef);
		if (IsNullValue(Ref))
		{
			return;
		}
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
		Container.SetField(TEXT("exclude"), MakeShared<FJsonValueObject>(MergeObjects(RefExclude.Get(), &Rest.Get())));
	}

	void ReadTurret(FFieldReader& R, const FString& Ctx, FPropsTurretRules& Out, TArray<FString>& Errors)
	{
		const TSharedPtr<FJsonObject> Cover = R.Object(TEXT("cover_route"));
		if (!Cover.IsValid())
		{
			return;
		}
		FFieldReader C(*Cover, Ctx + TEXT(".cover_route"), Errors);
		C.Str(TEXT("route"), Out.RouteId);
		ReadRange(C, TEXT("range_cm"), Out.RangeMinCm, Out.RangeMaxCm, FPropsLayoutView::MaxRadiusCm);
		if (C.Int(TEXT("arc_deg"), Out.ArcDeg) && (Out.ArcDeg < 1 || Out.ArcDeg > 360))
		{
			C.Fail(TEXT("'arc_deg' must be 1..360"));
			Out.ArcDeg = FMath::Clamp(Out.ArcDeg, 1, 360);
		}
		NonNegative(C, TEXT("min_covered_m"), Out.MinCoveredM);
		NonNegative(C, TEXT("behind_cover_gap_cm"), Out.BehindCoverGapCm);
	}

	void ReadMines(FJsonObject& Merged, FFieldReader& R, const FString& Ctx, const TSharedPtr<FJsonObject>& Presets, FPropsRules& Out, TArray<FString>& Errors)
	{
		if (const TSharedPtr<FJsonObject> Doors = R.Object(TEXT("doorways")))
		{
			FPropsMineDoorRules& D = Out.MineDoors;
			FFieldReader DR(*Doors, Ctx + TEXT(".doorways"), Errors);
			D.bEnabled = true;
			DR.Str(TEXT("mode"), D.Mode);
			if (D.Mode != TEXT("count") && D.Mode != TEXT("percent"))
			{
				DR.Fail(FString::Printf(TEXT("unknown mode '%s' (count | percent); percent kept"), *D.Mode));
				D.Mode = TEXT("percent");
			}
			NonNegative(DR, TEXT("value"), D.Value);
			Strings(DR, TEXT("kinds"), D.Kinds);
			for (const FString& K : D.Kinds)
			{
				if (K != TEXT("building_door") && K != TEXT("gate"))
				{
					DR.Fail(FString::Printf(TEXT("unknown doorway kind '%s' (building_door | gate)"), *K));
				}
			}
			FString Mandatory = TEXT("forbid");
			DR.Str(TEXT("mandatory_route"), Mandatory);
			if (Mandatory != TEXT("forbid") && Mandatory != TEXT("allow"))
			{
				DR.Fail(FString::Printf(TEXT("'mandatory_route' must be forbid | allow (got '%s'); forbid kept"), *Mandatory));
				Mandatory = TEXT("forbid");
			}
			D.bAllowMandatory = Mandatory == TEXT("allow");
			NonNegative(DR, TEXT("inside_cm"), D.InsideCm);
			D.Trigger = TEXT("tripwire");
			DR.Str(TEXT("trigger"), D.Trigger);
			D.Variant = TEXT("mine_tripwire");
			DR.Str(TEXT("variant"), D.Variant);
		}
		if (const TSharedPtr<FJsonObject> Crates = R.Object(TEXT("crates")))
		{
			FPropsMineCrateRules& C = Out.MineCrates;
			FFieldReader CR(*Crates, Ctx + TEXT(".crates"), Errors);
			C.bEnabled = true;
			CR.Str(TEXT("source_layer"), C.SourceLayer);
			NonNegative(CR, TEXT("percent"), C.Percent);
			C.Trigger = TEXT("crate_open");
			CR.Str(TEXT("trigger"), C.Trigger);
			C.Variant = TEXT("mine_crate");
			CR.Str(TEXT("variant"), C.Variant);
		}
		if (const TSharedPtr<FJsonObject> FieldIn = R.Object(TEXT("field")))
		{
			// a copy: its exclude.$preset is resolved on it, never on the balance file's object
			const TSharedRef<FJsonObject> Field = MergeObjects(FieldIn.Get(), nullptr);
			ResolveExcludePreset(*Field, Presets, Ctx + TEXT(".field"), Errors);
			Merged.SetField(TEXT("field"), MakeShared<FJsonValueObject>(Field));
			FPropsMineFieldRules& F = Out.MineField;
			F.bEnabled = true;
			F.Common.WalkLayers = { TEXT("ground") };
			FFieldReader FR(*Field, Ctx + TEXT(".field"), Errors);
			ReadCommon(FR, Ctx + TEXT(".field"), F.Common, Errors);
			F.Trigger = TEXT("proximity");
			FR.Str(TEXT("trigger"), F.Trigger);
			F.Variant = TEXT("mine_proximity");
			FR.Str(TEXT("variant"), F.Variant);
		}
	}

	void ReadLoot(const FJsonObject& Balance, FFieldReader& R, const FString& Ctx, FPropsRules& Out, TArray<FString>& Errors)
	{
		FPropsLootRules& L = Out.Loot;
		NonNegative(R, TEXT("budget_value"), L.BudgetValue);
		const TSharedPtr<FJsonObject> LootTables = AsObject(Balance.TryGetField(TEXT("loot_tables")));
		const auto TableValue = [&LootTables](const FString& Name, int32& OutValue)
		{
			const TSharedPtr<FJsonObject> T = LootTables.IsValid() ? AsObject(LootTables->TryGetField(Name)) : nullptr;
			const TSharedPtr<FJsonValue> V = T.IsValid() ? T->TryGetField(TEXT("value")) : nullptr;
			return !IsNullValue(V) && V->Type == EJson::Number && ToInt32(V->AsNumber(), OutValue) && OutValue > 0;
		};
		TArray<FString> Tables;
		Strings(R, TEXT("tables"), Tables);
		for (const FString& T : Tables)
		{
			int32 Value = 0;
			if (!TableValue(T, Value))
			{
				R.Fail(FString::Printf(TEXT("loot table '%s' is not in props_balance.json loot_tables (with a value > 0); ignored"), *T));
				continue;
			}
			if (!ContainsExact(L.Tables, T))
			{
				L.Tables.Add(T);
				L.TableValues.Add(Value);
			}
		}
		if (const TSharedPtr<FJsonObject> Containers = R.Object(TEXT("containers")))
		{
			FFieldReader C(*Containers, Ctx + TEXT(".containers"), Errors);
			Milli(C, TEXT("crates_share"), L.ContainersShareMilli);
			L.ContainersShareMilli = FMath::Clamp(L.ContainersShareMilli, 0, 1000);
			Milli(C, TEXT("budget_share"), L.ContainersBudgetMilli);
			L.ContainersBudgetMilli = FMath::Clamp(L.ContainersBudgetMilli, 0, 1000);
			C.Str(TEXT("source_layer"), L.SourceLayer);
			C.Str(TEXT("variant"), L.ContainerVariant);
		}
		if (const TSharedPtr<FJsonObject> Loose = R.Object(TEXT("loose")))
		{
			FFieldReader LR(*Loose, Ctx + TEXT(".loose"), Errors);
			if (const TSharedPtr<FJsonObject> Where = LR.Object(TEXT("where")))
			{
				ReadWhere(*Where, Ctx + TEXT(".loose.where"), Out, Errors);
			}
			ReadRange(LR, TEXT("prefer_far_from_route_cm"), L.FarMinCm, L.FarMaxCm, MAX_int32);
			LR.Str(TEXT("variant"), L.PileVariant);
			if (LR.Str(TEXT("roof_table"), L.RoofTable) && !L.RoofTable.IsEmpty() && !TableValue(L.RoofTable, L.RoofTableValue))
			{
				LR.Fail(FString::Printf(TEXT("roof_table '%s' is not in loot_tables (with a value > 0); ignored"), *L.RoofTable));
				L.RoofTable.Reset();
				L.RoofTableValue = 0;
			}
			if (const TSharedPtr<FJsonObject> Weights = LR.Object(TEXT("weights")))
			{
				FFieldReader WR(*Weights, Ctx + TEXT(".loose.weights"), Errors);
				WR.Int(TEXT("near"), L.WeightNear);
				WR.Int(TEXT("band"), L.WeightBand);
				WR.Int(TEXT("far"), L.WeightFar);
				L.WeightNear = FMath::Clamp(L.WeightNear, 1, PropsPlacementRules::MaxWeight);
				L.WeightBand = FMath::Clamp(L.WeightBand, 1, PropsPlacementRules::MaxWeight);
				L.WeightFar = FMath::Clamp(L.WeightFar, 1, PropsPlacementRules::MaxWeight);
			}
		}
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
	ReadCommon(R, Ctx, Out, Errors);
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
	if (Layer.Kind == KindTurrets)
	{
		ReadTurret(R, Ctx, Out.Turret, Errors);
	}
	else if (Layer.Kind == KindMines)
	{
		ReadMines(*Merged, R, Ctx, Presets, Out, Errors);
	}
	else if (Layer.Kind == KindLoot)
	{
		ReadLoot(Balance, R, Ctx, Out, Errors);
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
	Out.MineField.Common.HalfSizeCm = Out.HalfSizeCm;
	Out.MineField.Common.BudgetCap = Out.BudgetCap;

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
			Out.OtherLayerIds.Add(R.LayerId);
			Out.ReservedIds.Add(R.Object.Id);
		}
	}
	for (const FLevelPropsLayer& L : File.Layers)
	{
		Out.LayerKinds.Add(L.Id, L.Kind);
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
	Report.Kind = R.Kind;
	const bool bCrates = R.Kind == KindCrates;
	const bool bBarrels = R.Kind == KindBarrels;

	const auto Finish = [&Report]()
	{
		Report.ShortBy = FMath::Max(0, Report.Requested - Report.Kept - Report.Placed);
	};
	if (!IsPlaceableKind(R.Kind))
	{
		Report.Warnings.Add(FString::Printf(TEXT("kind_unsupported %s"), *R.Kind));
		return Result;
	}
	if (!In.View)
	{
		Report.Warnings.Add(TEXT("no_layout"));
		return Result;
	}
	if (R.Kind == KindTurrets)
	{
		PlaceTurrets(In, Result);
		return Result;
	}
	if (R.Kind == KindMines)
	{
		PlaceMines(In, Result);
		return Result;
	}
	if (R.Kind == KindLoot)
	{
		PlaceLoot(In, Result);
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
	GatherLattice(Ctx, Half, In.Kept, Report.Eligible, Report.Rejected, Cands);

	// 1. target count
	Report.Requested = ComputeRequested(R, Report.Eligible, Ctx.ZoneCount());
	const int32 Target = FMath::Max(0, Report.Requested - Report.Kept);

	uint64 TotalWeight = TotalWeightOf(Cands);
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
	if (In.Rules.Kind == KindTurrets || In.Rules.Kind == KindMines || In.Rules.Kind == KindLoot)
	{
		ValidateC4(In, Objects, Out);
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
		if (View.IsBlocked(O.Pos, O.WalkLayer, Half + R.WallGapCm))
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
			if (Ctx.IsObstacle(I) && Closer(O.Pos, In.OtherObjects[I].Pos, static_cast<int64>(Half) + Ctx.OtherHalf[I] + R.MinGapAnyCm))
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
	const auto Sub = [](const FPropsSubReport& S)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("eligible"), S.Eligible);
		O->SetNumberField(TEXT("requested"), S.Requested);
		O->SetNumberField(TEXT("placed"), S.Placed);
		O->SetNumberField(TEXT("kept"), S.Kept);
		O->SetNumberField(TEXT("dropped"), S.Dropped);
		O->SetNumberField(TEXT("forbidden"), S.Forbidden);
		return O;
	};
	if (Report.Kind == KindTurrets)
	{
		Rejected->SetNumberField(TEXT("coverage"), Report.Rejected.Coverage);
	}
	else if (Report.Kind == KindMines)
	{
		J->SetObjectField(TEXT("doorways"), Sub(Report.SubA));
		J->SetObjectField(TEXT("crates"), Sub(Report.SubB));
		J->SetObjectField(TEXT("field"), Sub(Report.SubC));
	}
	else if (Report.Kind == KindLoot)
	{
		J->SetObjectField(TEXT("containers"), Sub(Report.SubA));
		J->SetObjectField(TEXT("loose"), Sub(Report.SubB));
		TSharedRef<FJsonObject> Budget = MakeShared<FJsonObject>();
		Budget->SetNumberField(TEXT("value"), Report.BudgetValue);
		Budget->SetNumberField(TEXT("spent"), Report.BudgetSpent);
		Budget->SetNumberField(TEXT("left"), FMath::Max(0, Report.BudgetValue - Report.BudgetSpent));
		J->SetObjectField(TEXT("budget"), Budget);
	}
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
	if (!IsPlaceableKind(Layer->Kind))
	{
		Errors.Add(FString::Printf(TEXT("props: layer '%s' kind '%s' cannot be placed (crates | barrels | turrets | mines | loot)"), *LayerId, *Layer->Kind));
		return false;
	}
	const auto LayerIds = [&LayerId](const FLevelPropsResolved& Resolved)
	{
		TSet<FString> Ids;
		for (const FLevelPropsResolvedObject& R : Resolved.Objects)
		{
			if (R.LayerId.Equals(LayerId, ESearchCase::CaseSensitive))
			{
				Ids.Add(R.Object.Id);
			}
		}
		return Ids;
	};
	const TSet<FString> IdsBefore = LayerIds(LevelPropsFile::ApplyOverrides(File));
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
		// a kept object, by id (the C4 kinds drop kept objects whose crate / door is gone)
		if (In.Kept.ContainsByPredicate([&O](const FLevelPropsObject& K) { return K.Id.Equals(O.Id, ESearchCase::CaseSensitive); }))
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

	// other layers' objects attached to an object of this layer that the re-roll removed: reported, dropped by their own re-roll / C5
	const FLevelPropsResolved After = LevelPropsFile::ApplyOverrides(File);
	const TSet<FString> IdsAfter = LayerIds(After);
	for (const FLevelPropsResolvedObject& R : After.Objects)
	{
		if (!R.LayerId.Equals(LayerId, ESearchCase::CaseSensitive) && !R.Object.Attach.IsEmpty() && IdsBefore.Contains(R.Object.Attach)
			&& !IdsAfter.Contains(R.Object.Attach))
		{
			Result.Report.Warnings.Add(FString::Printf(TEXT("attach_orphaned %s"), *R.Object.Id));
		}
	}
	Layer->Report = ReportToJson(Result.Report);
	if (OutResult)
	{
		*OutResult = MoveTemp(Result);
	}
	return true;
}

bool PropsPlacementRules::RerollAll(FLevelPropsFile& File, const FJsonObject& Balance, const FPropsLayoutView& View, TArray<FString>& Errors)
{
	bool bOk = true;
	for (const TCHAR* Kind : { KindCrates, KindBarrels, KindTurrets, KindMines, KindLoot })
	{
		TArray<FString> Ids;
		for (const FLevelPropsLayer& L : File.Layers)
		{
			if (L.Kind == Kind && L.bEnabled && !L.bLocked)
			{
				Ids.Add(L.Id);
			}
		}
		for (const FString& Id : Ids)
		{
			const FLevelPropsLayer* L = File.Layers.FindByPredicate([&Id](const FLevelPropsLayer& X) { return X.Id.Equals(Id, ESearchCase::CaseSensitive); });
			bOk &= L && RerollLayer(File, Balance, View, Id, L->Seed, Errors);
		}
	}
	return bOk;
}

bool PropsPlacementRules::IsPlaceableKind(const FString& Kind)
{
	return Kind == KindCrates || Kind == KindBarrels || Kind == KindTurrets || Kind == KindMines || Kind == KindLoot;
}
