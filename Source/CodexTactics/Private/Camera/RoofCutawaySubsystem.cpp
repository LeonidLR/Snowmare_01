#include "Camera/RoofCutawaySubsystem.h"
#include "Camera/CameraZoneVolume.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "CollisionQueryParams.h"
#include "Combat/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Vehicles/VehicleSubsystem.h"

namespace
{
	TAutoConsoleVariable<int32> CVarRoofCutaway(TEXT("Codex.RoofCutaway"), 1,
		TEXT("1: roofs / ceilings over squad operatives are cut away from the main render pass (rev 5h, URoofCutawaySubsystem); 0: off"));
	TAutoConsoleVariable<int32> CVarRoofCutawayFollowSelected(TEXT("Codex.RoofCutaway.FollowSelected"), 1,
		TEXT("Bug report 2026-11-02 item 8: 1 = only the SELECTED operative's roof is cut (the squad leader: the selected man, a group's leader, the turn-based active unit) - a selected man on a roof keeps it drawn while a team-mate stands under it; 0 = every operative's roof (the old rule, A/B)"));
	TAutoConsoleVariable<int32> CVarRoofCutawayStoreys(TEXT("Codex.RoofCutaway.Storeys"), 1,
		TEXT("Designer bug 2026-11-22: 1 = in a building tagged building=<id> / floor=<n> (generated grey pieces, kit_dress per-storey components) every storey above the selected operative's is cut, the roof included, his own storey never; 0 = the head-clearance rule alone (A/B)"));

	TAutoConsoleVariable<int32> CVarRoofCutawayHiddenShadow(TEXT("Codex.RoofCutaway.HiddenShadow"), 0,
		TEXT("Designer 2026-11-22: 0 = a cut-away roof / storey casts no shadow (and no distance-field shadow / AO): the interior is lit; 1 = it keeps casting its shadow (the old rule). Applied when a piece is cut; its own setting comes back on restore"));

	TAutoConsoleVariable<int32> CVarCursorThroughCutaway(TEXT("Codex.Cursor.ThroughCutaway"), 1,
		TEXT("Designer bug 2026-11-22 (dressed showcase: clicks land on the invisible upper storey): 1 = every cursor trace passes through what the storey cutaway hides for the selected operative - cut pieces, the storeys above his in his building (the kit map's hidden grey proxies too), the wall tops under them; 0 = the raw cursor hit (A/B)"));

	/** A building's slab tags (the level generator: build_levelgen_map.py floor / roof slabs). */
	const FName FloorSlabTag(TEXT("floor_slab"));
	const FName RoofSlabTag(TEXT("roof_slab"));

	/** A head never counts lower than a standing operative's (a prone one must not cut a table he lies under), cm above the feet. */
	constexpr float RoofMinHeadHeightCm = 180.f;
	/** Pieces the upward trace may step over before it gives up (triggers, pawns, walls of a one-mesh building). */
	constexpr int32 RoofMaxTraceSteps = 8;

	bool RoofClassNameContains(const UObject* Object, const TCHAR* Part)
	{
		return Object && Object->GetClass()->GetName().Contains(Part);
	}

	FCollisionObjectQueryParams RoofObjectTypes()
	{
		// Object-type queries see a cut piece too (its Visibility response is Ignore while cut): the roof stays found.
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		return Objects;
	}
}

namespace RoofCutawayRules
{
	FName OptOutTag()
	{
		static const FName Tag(TEXT("NoRoofCutaway"));
		return Tag;
	}

	FName OptInTag()
	{
		static const FName Tag(TEXT("RoofCutaway"));
		return Tag;
	}

	bool IsCuttableKind(const FRoofPieceTraits& Traits)
	{
		return Traits.bStaticMesh && Traits.bVisible && !Traits.bPawn && !Traits.bTerrain && !Traits.bFoliage && !Traits.bOptOut;
	}

	bool IsAboveHead(float PieceBottomZ, float HeadZ, float HeadClearanceCm)
	{
		return PieceBottomZ >= HeadZ + HeadClearanceCm;
	}

	bool ShouldCutPiece(const FRoofPieceTraits& Traits, float PieceBottomZ, float HeadZ, float HeadClearanceCm)
	{
		return IsCuttableKind(Traits) && (Traits.bOptIn || IsAboveHead(PieceBottomZ, HeadZ, HeadClearanceCm));
	}

	bool OverlapsFootprint(const FBox& Roof, const FBox& Piece, float InsetCm)
	{
		const float OverlapX = FMath::Min(Roof.Max.X, Piece.Max.X) - FMath::Max(Roof.Min.X, Piece.Min.X);
		const float OverlapY = FMath::Min(Roof.Max.Y, Piece.Max.Y) - FMath::Max(Roof.Min.Y, Piece.Min.Y);
		return OverlapX > InsetCm && OverlapY > InsetCm;
	}

	bool IsBuildingSized(const FBox& Roof, float MaxFootprintCm)
	{
		return Roof.IsValid && Roof.Max.X - Roof.Min.X <= MaxFootprintCm && Roof.Max.Y - Roof.Min.Y <= MaxFootprintCm;
	}

	bool ShouldCutForOperative(bool bInsideShownZoneCamera)
	{
		return !bInsideShownZoneCamera;
	}

	bool ShouldEvaluateOperative(bool bIsSelected, bool bFollowSelected)
	{
		return bIsSelected || !bFollowSelected;
	}

	bool ShouldRestore(double LastCoveredSeconds, double NowSeconds, float RestoreDelaySeconds)
	{
		return NowSeconds - LastCoveredSeconds >= RestoreDelaySeconds;
	}

	bool CastsShadowWhileCut(bool bOriginal, bool bHiddenShadow)
	{
		return bOriginal && bHiddenShadow;
	}

	bool ParseStoreyTags(TConstArrayView<FName> Tags, FString& OutBuilding, int32& OutStorey)
	{
		static const FString BuildingPrefix(TEXT("building="));
		static const FString FloorPrefix(TEXT("floor="));
		OutBuilding.Reset();
		OutStorey = INDEX_NONE;
		for (const FName& Tag : Tags)
		{
			const FString Text = Tag.ToString();
			if (Text.StartsWith(BuildingPrefix, ESearchCase::CaseSensitive))
			{
				OutBuilding = Text.Mid(BuildingPrefix.Len());
			}
			else if (Text.StartsWith(FloorPrefix, ESearchCase::CaseSensitive))
			{
				const FString Value = Text.Mid(FloorPrefix.Len());
				if (!Value.IsEmpty() && Value.IsNumeric() && !Value.Contains(TEXT(".")))
				{
					OutStorey = FCString::Atoi(*Value);
				}
			}
		}
		return !OutBuilding.IsEmpty();
	}

	bool IsSlabTag(const FName& Tag)
	{
		return Tag == FloorSlabTag || Tag == RoofSlabTag;
	}

	TArray<FStoreyBase> StoreyBasesFromBottoms(const TMap<int32, TArray<float>>& BottomsByStorey)
	{
		TArray<FStoreyBase> Bases;
		for (const TPair<int32, TArray<float>>& Pair : BottomsByStorey)
		{
			if (Pair.Value.IsEmpty())
			{
				continue;
			}
			TArray<float> Sorted = Pair.Value;
			Sorted.Sort();
			// The lower median: a storey's walls stand on its base, the odd slab bottom / lintel does not move it.
			Bases.Add({ Pair.Key, Sorted[(Sorted.Num() - 1) / 2] });
		}
		Bases.Sort([](const FStoreyBase& A, const FStoreyBase& B) { return A.Storey < B.Storey; });
		return Bases;
	}

	int32 StoreyAt(TConstArrayView<FStoreyBase> Bases, float Z, float ToleranceCm)
	{
		int32 Storey = INDEX_NONE;
		for (const FStoreyBase& Base : Bases)
		{
			if (Z >= Base.BaseZ - ToleranceCm)
			{
				Storey = FMath::Max(Storey, Base.Storey);
			}
		}
		return Storey;
	}

	bool ShouldCutStorey(int32 PieceStorey, int32 OperativeStorey)
	{
		return PieceStorey != INDEX_NONE && OperativeStorey != INDEX_NONE && PieceStorey > OperativeStorey;
	}

	float DistanceToBox2D(const FBox& Box, const FVector2D& P)
	{
		if (!Box.IsValid)
		{
			return TNumericLimits<float>::Max();
		}
		const double DX = FMath::Max3(Box.Min.X - P.X, 0.0, P.X - Box.Max.X);
		const double DY = FMath::Max3(Box.Min.Y - P.Y, 0.0, P.Y - Box.Max.Y);
		return static_cast<float>(FMath::Sqrt(DX * DX + DY * DY));
	}

	bool IsInsideBuilding(float DistanceToNearestSlabCm, bool bWasInside, float KeepCm)
	{
		return DistanceToNearestSlabCm <= 0.f || (bWasInside && DistanceToNearestSlabCm <= KeepCm);
	}

	bool ClaimsUntaggedPiece(const FBox& SlabBounds, const FBox& Piece, float MarginCm)
	{
		if (!SlabBounds.IsValid || !Piece.IsValid)
		{
			return false;
		}
		const FVector Centre = Piece.GetCenter();
		const bool bCentreIn = DistanceToBox2D(SlabBounds, FVector2D(Centre.X, Centre.Y)) <= MarginCm;
		const bool bNotWider = Piece.Max.X - Piece.Min.X <= SlabBounds.Max.X - SlabBounds.Min.X + 2.f * MarginCm
			&& Piece.Max.Y - Piece.Min.Y <= SlabBounds.Max.Y - SlabBounds.Min.Y + 2.f * MarginCm;
		return bCentreIn && bNotWider;
	}

	bool ShouldSkipCursorHit(bool bCutAway, int32 PieceStorey, int32 SelectedStorey, float HitZ, float SelectedCeilingZ, bool bStairPiece, float CeilingSlackCm)
	{
		if (bCutAway)
		{
			return true;
		}
		if (PieceStorey == INDEX_NONE || SelectedStorey == INDEX_NONE)
		{
			return false; // another building, a piece without a storey, or he is in no building: the hit is what he sees
		}
		if (PieceStorey > SelectedStorey)
		{
			return true; // a storey the cutaway hides (or is about to)
		}
		return PieceStorey == SelectedStorey && !bStairPiece && HitZ >= SelectedCeilingZ - CeilingSlackCm;
	}

	bool IsInteriorFloorHit(float ImpactNormalZ, float DistanceToSlabsCm, float HitZ, float BottomZ, float RoofTopZ, float ToleranceCm)
	{
		return ImpactNormalZ >= 0.7f && DistanceToSlabsCm <= 0.f && HitZ >= BottomZ - ToleranceCm && HitZ <= RoofTopZ - ToleranceCm;
	}
}

namespace
{
	/** The piece (its actor or itself) is a stair flight / landing of the generator (tag stair=<id>). */
	bool IsStairPiece(const UPrimitiveComponent& Piece)
	{
		const auto IsStairTag = [](const FName& Tag) { return Tag.ToString().StartsWith(TEXT("stair="), ESearchCase::CaseSensitive); };
		const AActor* Owner = Piece.GetOwner();
		return Piece.ComponentTags.ContainsByPredicate(IsStairTag) || (Owner && Owner->Tags.ContainsByPredicate(IsStairTag));
	}
}

bool URoofCutawaySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId URoofCutawaySubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(URoofCutawaySubsystem, STATGROUP_Tickables);
}

void URoofCutawaySubsystem::Deinitialize()
{
	RestoreAll();
	StoreyBuildings.Reset();
	StoreyOfPiece.Reset();
	LastStoreyBuilding.Reset();
	PreviousStoreyBuilding.Reset();
	Super::Deinitialize();
}

void URoofCutawaySubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (CVarRoofCutaway.GetValueOnGameThread() == 0)
	{
		RestoreAll();
		return;
	}
	// Real time: the tactical pause slows the game clock, the roofs still follow the squad.
	UpdateAccumulator += GetWorld() ? GetWorld()->DeltaRealTimeSeconds : DeltaTime;
	if (UpdateAccumulator < Config.UpdateIntervalSeconds)
	{
		return;
	}
	UpdateAccumulator = 0.f;
	UpdateNow();
}

bool URoofCutawaySubsystem::IsCutAway(const UPrimitiveComponent* Piece) const
{
	return Piece && CutPieces.ContainsByPredicate([Piece](const FCutPiece& Cut) { return Cut.Piece.Get() == Piece; });
}

int32 URoofCutawaySubsystem::GetCutAwayCount() const
{
	int32 Count = 0;
	for (const FCutPiece& Cut : CutPieces)
	{
		Count += Cut.Piece.IsValid() ? 1 : 0;
	}
	return Count;
}

void URoofCutawaySubsystem::UpdateNow()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double Now = World->GetRealTimeSeconds();
	RefreshOptInActors(Now);
	if (CVarRoofCutawayStoreys.GetValueOnGameThread() != 0 && Now - StoreyIndexSeconds >= Config.StoreyIndexRefreshSeconds)
	{
		RebuildStoreyIndex();
		StoreyIndexSeconds = Now;
	}
	TArray<ACameraZoneVolume*> ShownZones;
	for (TActorIterator<ACameraZoneVolume> It(World); It; ++It)
	{
		if (It->IsZoneCameraShown())
		{
			ShownZones.Add(*It);
		}
	}
	PreviousStoreyBuilding = MoveTemp(LastStoreyBuilding);
	LastStoreyBuilding.Reset();
	TSet<UPrimitiveComponent*> Covered;
	if (const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>())
	{
		const bool bFollowSelected = CVarRoofCutawayFollowSelected.GetValueOnGameThread() != 0;
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			if (Member && RoofCutawayRules::ShouldEvaluateOperative(Member == Squad->GetLeader(), bFollowSelected)
				&& !Member->IsHidden() && Member->HealthComponent && Member->HealthComponent->IsAlive() && !VehicleQueries::IsMounted(Member))
			{
				CollectRoofPieces(*Member, ShownZones, Covered);
			}
		}
	}
	for (UPrimitiveComponent* Piece : Covered)
	{
		if (FCutPiece* Existing = CutPieces.FindByPredicate([Piece](const FCutPiece& Cut) { return Cut.Piece.Get() == Piece; }))
		{
			Existing->LastCoveredSeconds = Now;
		}
		else
		{
			CutPiece(*Piece, Now);
		}
	}
	for (int32 Index = CutPieces.Num() - 1; Index >= 0; --Index)
	{
		FCutPiece& Cut = CutPieces[Index];
		UPrimitiveComponent* Piece = Cut.Piece.Get();
		if (!Piece)
		{
			CutPieces.RemoveAtSwap(Index);
			continue;
		}
		if (!Covered.Contains(Piece) && RoofCutawayRules::ShouldRestore(Cut.LastCoveredSeconds, Now, Config.RestoreDelaySeconds))
		{
			RestorePiece(Cut);
			CutPieces.RemoveAtSwap(Index);
		}
	}
}

void URoofCutawaySubsystem::RestoreAll()
{
	for (FCutPiece& Cut : CutPieces)
	{
		RestorePiece(Cut);
	}
	CutPieces.Reset();
}

RoofCutawayRules::FRoofPieceTraits URoofCutawaySubsystem::GetTraits(const UPrimitiveComponent& Piece)
{
	RoofCutawayRules::FRoofPieceTraits Traits;
	const AActor* Owner = Piece.GetOwner();
	Traits.bPawn = Owner && Owner->IsA<APawn>();
	// By class name: the Landscape / Foliage modules are no dependencies of the game module.
	Traits.bTerrain = RoofClassNameContains(Owner, TEXT("Landscape")) || RoofClassNameContains(&Piece, TEXT("Landscape"));
	Traits.bFoliage = RoofClassNameContains(Owner, TEXT("InstancedFoliageActor")) || RoofClassNameContains(&Piece, TEXT("Foliage"));
	Traits.bStaticMesh = Piece.IsA<UStaticMeshComponent>();
	Traits.bVisible = Piece.IsVisible() && !(Owner && Owner->IsHidden());
	Traits.bOptOut = Owner && Owner->ActorHasTag(RoofCutawayRules::OptOutTag());
	Traits.bOptIn = Owner && Owner->ActorHasTag(RoofCutawayRules::OptInTag());
	return Traits;
}

float URoofCutawaySubsystem::GetFloorFeetZ(const AOperativeCharacter& Operative, FVector& OutLocation)
{
	// On a ladder he counts on the floor he left until he is off it (LADDERS.md 7.3: no cutaway flicker mid-ladder).
	OutLocation = Operative.GetFloorReferenceLocation();
	const float HalfHeight = Operative.GetCapsuleComponent() ? Operative.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
	return static_cast<float>(OutLocation.Z) - HalfHeight;
}

void URoofCutawaySubsystem::CollectRoofPieces(const AOperativeCharacter& Operative, const TArray<ACameraZoneVolume*>& ShownZones,
	TSet<UPrimitiveComponent*>& OutPieces)
{
	FVector Location;
	const float FeetZ = GetFloorFeetZ(Operative, Location);
	const bool bInShownZone = ShownZones.ContainsByPredicate([&Location](const ACameraZoneVolume* Zone) { return Zone->ContainsLocation(Location); });
	if (!RoofCutawayRules::ShouldCutForOperative(bInShownZone))
	{
		return;
	}
	TSet<UPrimitiveComponent*> Pieces;
	CollectHeadClearancePieces(Operative, Location, Pieces);

	int32 Storey = INDEX_NONE;
	const int32 BuildingIndex = CVarRoofCutawayStoreys.GetValueOnGameThread() != 0
		? FindOperativeStorey(Location, FeetZ, PreviousStoreyBuilding.Find(&Operative), Storey) : INDEX_NONE;
	if (BuildingIndex == INDEX_NONE)
	{
		OutPieces.Append(Pieces);
		return;
	}
	const FStoreyBuilding& Building = StoreyBuildings[BuildingIndex];
	LastStoreyBuilding.Add(&Operative, Building.Id);
	// His own storey and those below are never cut (a lintel, a tall wall piece just over his head stays: designer 2026-11-22).
	for (UPrimitiveComponent* Piece : Pieces)
	{
		const TPair<int32, int32>* Of = StoreyOfPiece.Find(Piece);
		if (!Of || Of->Key != BuildingIndex || RoofCutawayRules::ShouldCutStorey(Of->Value, Storey))
		{
			OutPieces.Add(Piece);
		}
	}
	// Every visible piece of a storey above his: upper storeys with their partitions, the ceiling over him (the slab above), the roof.
	for (const TPair<TWeakObjectPtr<UPrimitiveComponent>, int32>& Entry : Building.Pieces)
	{
		UPrimitiveComponent* Piece = Entry.Key.Get();
		if (Piece && RoofCutawayRules::ShouldCutStorey(Entry.Value, Storey) && RoofCutawayRules::IsCuttableKind(GetTraits(*Piece)))
		{
			OutPieces.Add(Piece);
		}
	}
}

int32 URoofCutawaySubsystem::FindOperativeStorey(const FVector& FloorLocation, float FeetZ, const FString* WasInside, int32& OutStorey) const
{
	OutStorey = INDEX_NONE;
	const FVector2D P(FloorLocation.X, FloorLocation.Y);
	int32 Best = INDEX_NONE;
	float BestDistance = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < StoreyBuildings.Num(); ++Index)
	{
		const FStoreyBuilding& Building = StoreyBuildings[Index];
		if (Building.Bases.IsEmpty() || !Building.SlabBounds.IsValid || FeetZ < Building.Bases[0].BaseZ - Config.StoreyToleranceCm
			|| FeetZ > Building.SlabBounds.Max.Z + Config.StoreyToleranceCm)
		{
			continue;
		}
		float Distance = TNumericLimits<float>::Max();
		for (const FBox& Slab : Building.Slabs)
		{
			Distance = FMath::Min(Distance, RoofCutawayRules::DistanceToBox2D(Slab, P));
		}
		const bool bWasInside = WasInside && *WasInside == Building.Id;
		if (RoofCutawayRules::IsInsideBuilding(Distance, bWasInside, Config.StoreyKeepInsideCm) && Distance < BestDistance)
		{
			Best = Index;
			BestDistance = Distance;
		}
	}
	if (Best != INDEX_NONE)
	{
		OutStorey = RoofCutawayRules::StoreyAt(StoreyBuildings[Best].Bases, FeetZ, Config.StoreyToleranceCm);
		if (OutStorey == INDEX_NONE)
		{
			Best = INDEX_NONE;
		}
	}
	return Best;
}

bool URoofCutawaySubsystem::GetOperativeStorey(const AOperativeCharacter& Operative, FString& OutBuilding, int32& OutStorey) const
{
	FVector Location;
	const float FeetZ = GetFloorFeetZ(Operative, Location);
	const int32 Index = FindOperativeStorey(Location, FeetZ, LastStoreyBuilding.Find(&Operative), OutStorey);
	OutBuilding = Index != INDEX_NONE ? StoreyBuildings[Index].Id : FString();
	return Index != INDEX_NONE;
}

TArray<TPair<FString, int32>> URoofCutawaySubsystem::GetStoreyBuildings() const
{
	TArray<TPair<FString, int32>> Out;
	for (const FStoreyBuilding& Building : StoreyBuildings)
	{
		Out.Emplace(Building.Id, Building.Bases.IsEmpty() ? 0 : Building.Bases.Last().Storey + 1);
	}
	return Out;
}

TArray<TPair<UPrimitiveComponent*, int32>> URoofCutawaySubsystem::GetStoreyPieces(const FString& BuildingId) const
{
	TArray<TPair<UPrimitiveComponent*, int32>> Out;
	if (const FStoreyBuilding* Building = StoreyBuildings.FindByPredicate([&BuildingId](const FStoreyBuilding& B) { return B.Id == BuildingId; }))
	{
		for (const TPair<TWeakObjectPtr<UPrimitiveComponent>, int32>& Entry : Building->Pieces)
		{
			if (UPrimitiveComponent* Piece = Entry.Key.Get())
			{
				Out.Emplace(Piece, Entry.Value);
			}
		}
	}
	return Out;
}

TArray<FBox> URoofCutawaySubsystem::GetStoreySlabs(const FString& BuildingId) const
{
	const FStoreyBuilding* Building = StoreyBuildings.FindByPredicate([&BuildingId](const FStoreyBuilding& B) { return B.Id == BuildingId; });
	return Building ? Building->Slabs : TArray<FBox>();
}

float URoofCutawaySubsystem::GetStoreyCeilingZ(int32 BuildingIndex, int32 Storey) const
{
	if (!StoreyBuildings.IsValidIndex(BuildingIndex))
	{
		return TNumericLimits<float>::Max();
	}
	for (const RoofCutawayRules::FStoreyBase& Base : StoreyBuildings[BuildingIndex].Bases)
	{
		if (Base.Storey > Storey)
		{
			return Base.BaseZ; // Bases are sorted by storey: the next one up
		}
	}
	return TNumericLimits<float>::Max(); // the roof storey: nothing over it is cut
}

bool URoofCutawaySubsystem::GetStoreyBaseZ(const FString& BuildingId, int32 Storey, float& OutZ) const
{
	if (const FStoreyBuilding* Building = StoreyBuildings.FindByPredicate([&BuildingId](const FStoreyBuilding& B) { return B.Id == BuildingId; }))
	{
		for (const RoofCutawayRules::FStoreyBase& Base : Building->Bases)
		{
			if (Base.Storey == Storey)
			{
				OutZ = Base.BaseZ;
				return true;
			}
		}
	}
	return false;
}

bool URoofCutawaySubsystem::RefineCursorHit(FHitResult& Hit, ECollisionChannel Channel, const AOperativeCharacter* Selected) const
{
	if (!Hit.bBlockingHit || CVarCursorThroughCutaway.GetValueOnGameThread() == 0 || CVarRoofCutaway.GetValueOnGameThread() == 0)
	{
		return Hit.bBlockingHit;
	}
	int32 SelectedBuilding = INDEX_NONE;
	int32 SelectedStorey = INDEX_NONE;
	float Ceiling = TNumericLimits<float>::Max();
	if (Selected && CVarRoofCutawayStoreys.GetValueOnGameThread() != 0 && !StoreyBuildings.IsEmpty())
	{
		FVector Location;
		const float FeetZ = GetFloorFeetZ(*Selected, Location);
		SelectedBuilding = FindOperativeStorey(Location, FeetZ, LastStoreyBuilding.Find(Selected), SelectedStorey);
		Ceiling = GetStoreyCeilingZ(SelectedBuilding, SelectedStorey);
	}
	const auto Skip = [this, SelectedBuilding, SelectedStorey, Ceiling](const FHitResult& Candidate)
	{
		const UPrimitiveComponent* Piece = Candidate.GetComponent();
		if (!Piece)
		{
			return false;
		}
		int32 PieceStorey = INDEX_NONE;
		if (SelectedBuilding != INDEX_NONE)
		{
			if (const TPair<int32, int32>* Of = StoreyOfPiece.Find(Piece); Of && Of->Key == SelectedBuilding)
			{
				PieceStorey = Of->Value;
			}
		}
		return RoofCutawayRules::ShouldSkipCursorHit(IsCutAway(Piece), PieceStorey, SelectedStorey, static_cast<float>(Candidate.ImpactPoint.Z), Ceiling,
			IsStairPiece(*Piece), Config.CursorCeilingSlackCm);
	};
	if (!Skip(Hit))
	{
		return true;
	}
	UWorld* World = GetWorld();
	const FVector Start = Hit.TraceStart;
	const FVector End = Hit.TraceEnd;
	if (!World || Start.Equals(End, 1.0))
	{
		return true; // a constructed hit (no ray): kept
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CursorThroughCutaway), false);
	FString Skipped;
	for (int32 Step = 0; Step < Config.CursorMaxSkips; ++Step)
	{
		UPrimitiveComponent* Piece = Hit.GetComponent();
		if (!Piece)
		{
			break;
		}
		Params.AddIgnoredComponent(Piece);
		if (Skipped.Len() < 400)
		{
			Skipped += FString::Printf(TEXT(" %s.%s@%.0f"), Piece->GetOwner() ? *Piece->GetOwner()->GetName() : TEXT("-"), *Piece->GetName(), Hit.ImpactPoint.Z);
		}
		FHitResult Next;
		if (!World->LineTraceSingleByChannel(Next, Start, End, Channel, Params))
		{
			UE_LOG(LogCodexTactics, Verbose, TEXT("[Cursor] through the cutaway:%s -> nothing behind"), *Skipped);
			Hit = FHitResult(1.f);
			Hit.TraceStart = Start;
			Hit.TraceEnd = End;
			return false;
		}
		Hit = Next;
		if (!Skip(Hit))
		{
			break;
		}
	}
	UE_LOG(LogCodexTactics, Verbose, TEXT("[Cursor] through the cutaway (selected on storey %d):%s -> %s at %s"), SelectedStorey, *Skipped,
		*GetNameSafe(Hit.GetActor()), *Hit.ImpactPoint.ToCompactString());
	return Hit.bBlockingHit;
}

bool URoofCutawaySubsystem::TraceCursorRay(const FVector& Start, const FVector& End, ECollisionChannel Channel, const AOperativeCharacter* Selected,
	FHitResult& OutHit) const
{
	OutHit = FHitResult(1.f);
	OutHit.TraceStart = Start;
	OutHit.TraceEnd = End;
	UWorld* World = GetWorld();
	if (!World || !World->LineTraceSingleByChannel(OutHit, Start, End, Channel, FCollisionQueryParams(SCENE_QUERY_STAT(ClickableTrace), false)))
	{
		return false;
	}
	return RefineCursorHit(OutHit, Channel, Selected);
}

bool URoofCutawaySubsystem::IsInteriorFloor(const FHitResult& Hit, FString* OutBuilding, int32* OutStorey) const
{
	if (!Hit.bBlockingHit)
	{
		return false;
	}
	const FVector2D P(Hit.ImpactPoint.X, Hit.ImpactPoint.Y);
	const float Z = static_cast<float>(Hit.ImpactPoint.Z);
	for (const FStoreyBuilding& Building : StoreyBuildings)
	{
		if (Building.Bases.IsEmpty() || !Building.SlabBounds.IsValid)
		{
			continue;
		}
		float Distance = TNumericLimits<float>::Max();
		for (const FBox& Slab : Building.Slabs)
		{
			Distance = FMath::Min(Distance, RoofCutawayRules::DistanceToBox2D(Slab, P));
		}
		if (RoofCutawayRules::IsInteriorFloorHit(static_cast<float>(Hit.ImpactNormal.Z), Distance, Z, Building.Bases[0].BaseZ,
			static_cast<float>(Building.SlabBounds.Max.Z), Config.StoreyToleranceCm))
		{
			if (OutBuilding)
			{
				*OutBuilding = Building.Id;
			}
			if (OutStorey)
			{
				*OutStorey = RoofCutawayRules::StoreyAt(Building.Bases, Z, Config.StoreyToleranceCm);
			}
			return true;
		}
	}
	return false;
}

void URoofCutawaySubsystem::RebuildStoreyIndex()
{
	StoreyBuildings.Reset();
	StoreyOfPiece.Reset();
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	struct FLoose
	{
		UPrimitiveComponent* Piece = nullptr;
		FBox Box;
		/** Building index when the piece names its building but no storey; INDEX_NONE: claimed by the slabs' footprint. */
		int32 Building = INDEX_NONE;
	};
	TArray<FLoose> Loose;
	TArray<TMap<int32, TArray<float>>> Bottoms;
	TMap<FString, int32> IndexOf;
	FString BuildingId;
	int32 Storey = INDEX_NONE;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor || Actor->IsA<APawn>() || Actor->ActorHasTag(RoofCutawayRules::OptOutTag()))
		{
			continue;
		}
		// A pawn's gear (weapons, attachments) walks with him: no building piece.
		bool bOnPawn = false;
		for (const AActor* Parent = Actor->GetAttachParentActor(); Parent && !bOnPawn; Parent = Parent->GetAttachParentActor())
		{
			bOnPawn = Parent->IsA<APawn>();
		}
		if (bOnPawn || (Actor->GetOwner() && Actor->GetOwner()->IsA<APawn>()))
		{
			continue;
		}
		TInlineComponentArray<UStaticMeshComponent*> Meshes(Actor);
		for (UStaticMeshComponent* Mesh : Meshes)
		{
			if (!Mesh || !Mesh->IsRegistered() || !Mesh->GetStaticMesh())
			{
				continue;
			}
			const RoofCutawayRules::FRoofPieceTraits Traits = GetTraits(*Mesh);
			if (Traits.bTerrain || Traits.bFoliage)
			{
				continue;
			}
			const UInstancedStaticMeshComponent* Instanced = Cast<UInstancedStaticMeshComponent>(Mesh);
			if (Instanced && Instanced->GetInstanceCount() == 0)
			{
				continue;
			}
			const FBox Box = Mesh->Bounds.GetBox();
			// A component's own tags (kit_dress per-storey components) win over its actor's (the generator's pieces).
			const bool bTagged = RoofCutawayRules::ParseStoreyTags(Mesh->ComponentTags, BuildingId, Storey)
				|| RoofCutawayRules::ParseStoreyTags(Actor->Tags, BuildingId, Storey);
			if (!bTagged)
			{
				// Untagged movable pieces come and go (a selection ring, dropped gear): only fixed level pieces are claimed by the slabs.
				if (Mesh->Mobility != EComponentMobility::Movable)
				{
					Loose.Add({ Mesh, Box, INDEX_NONE });
				}
				continue;
			}
			int32& Index = IndexOf.FindOrAdd(BuildingId, INDEX_NONE);
			if (Index == INDEX_NONE)
			{
				Index = StoreyBuildings.AddDefaulted();
				StoreyBuildings[Index].Id = BuildingId;
				Bottoms.AddDefaulted();
			}
			FStoreyBuilding& Building = StoreyBuildings[Index];
			const bool bSlab = Mesh->ComponentTags.ContainsByPredicate(&RoofCutawayRules::IsSlabTag)
				|| Actor->Tags.ContainsByPredicate(&RoofCutawayRules::IsSlabTag);
			if (bSlab)
			{
				Building.Slabs.Add(Box);
				Building.SlabBounds += Box;
			}
			if (Storey == INDEX_NONE)
			{
				Loose.Add({ Mesh, Box, Index });
				continue;
			}
			Bottoms[Index].FindOrAdd(Storey).Add(static_cast<float>(Box.Min.Z));
			Building.Pieces.Emplace(Mesh, Storey);
		}
	}
	for (int32 Index = 0; Index < StoreyBuildings.Num(); ++Index)
	{
		StoreyBuildings[Index].Bases = RoofCutawayRules::StoreyBasesFromBottoms(Bottoms[Index]);
	}
	// Pieces without a storey: by their bottom, in the building they name or whose slabs they lie over (the smallest such building).
	for (const FLoose& Piece : Loose)
	{
		int32 Index = Piece.Building;
		if (Index == INDEX_NONE)
		{
			double BestArea = TNumericLimits<double>::Max();
			for (int32 Candidate = 0; Candidate < StoreyBuildings.Num(); ++Candidate)
			{
				const FBox& Slabs = StoreyBuildings[Candidate].SlabBounds;
				const double Area = Slabs.IsValid ? (Slabs.Max.X - Slabs.Min.X) * (Slabs.Max.Y - Slabs.Min.Y) : 0.0;
				if (RoofCutawayRules::ClaimsUntaggedPiece(Slabs, Piece.Box, Config.StoreyClaimMarginCm) && Area < BestArea)
				{
					Index = Candidate;
					BestArea = Area;
				}
			}
		}
		if (Index == INDEX_NONE)
		{
			continue;
		}
		const int32 PieceStorey = RoofCutawayRules::StoreyAt(StoreyBuildings[Index].Bases, static_cast<float>(Piece.Box.Min.Z), Config.StoreyToleranceCm);
		if (PieceStorey != INDEX_NONE)
		{
			StoreyBuildings[Index].Pieces.Emplace(Piece.Piece, PieceStorey);
		}
	}
	// Buildings without slabs (legacy layouts: untagged slabs) never take part; the head-clearance rule alone covers them.
	StoreyBuildings.RemoveAll([](const FStoreyBuilding& Building) { return Building.Slabs.IsEmpty() || Building.Bases.IsEmpty(); });
	for (int32 Index = 0; Index < StoreyBuildings.Num(); ++Index)
	{
		for (const TPair<TWeakObjectPtr<UPrimitiveComponent>, int32>& Entry : StoreyBuildings[Index].Pieces)
		{
			StoreyOfPiece.Add(Entry.Key.Get(), TPair<int32, int32>(Index, Entry.Value));
		}
	}
	UE_LOG(LogCodexTactics, Verbose, TEXT("[RoofCutaway] storey index: %d buildings, %d pieces"), StoreyBuildings.Num(), StoreyOfPiece.Num());
}

void URoofCutawaySubsystem::CollectHeadClearancePieces(const AOperativeCharacter& Operative, const FVector& Location, TSet<UPrimitiveComponent*>& OutPieces) const
{
	UWorld* World = Operative.GetWorld();
	const float HalfHeight = Operative.GetCapsuleComponent() ? Operative.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
	const float FeetZ = Location.Z - HalfHeight;
	const float HeadZ = FMath::Max(Location.Z + HalfHeight, FeetZ + RoofMinHeadHeightCm);
	const FVector Start(Location.X, Location.Y, HeadZ + Config.TraceStartAboveHeadCm);
	const FVector End(Location.X, Location.Y, FeetZ + Config.TraceTopAboveFeetCm);
	const FCollisionObjectQueryParams Objects = RoofObjectTypes();

	auto AddActorPieces = [this, HeadZ, &OutPieces](const AActor& Actor, const FBox* Footprint)
	{
		TInlineComponentArray<UPrimitiveComponent*> Prims(&Actor);
		for (UPrimitiveComponent* Prim : Prims)
		{
			const FBox Bounds = Prim->Bounds.GetBox();
			if (RoofCutawayRules::ShouldCutPiece(GetTraits(*Prim), Bounds.Min.Z, HeadZ, Config.HeadClearanceCm)
				&& (!Footprint || RoofCutawayRules::OverlapsFootprint(*Footprint, Bounds, Config.FootprintInsetCm)))
			{
				OutPieces.Add(Prim);
			}
		}
	};

	// 1. The roof: the first piece above his head that may be cut (triggers, pawns, a one-mesh building's walls are stepped over).
	const AActor* Roof = nullptr;
	if (End.Z > Start.Z)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(RoofCutawayTrace), false, &Operative);
		for (int32 Step = 0; Step < RoofMaxTraceSteps && !Roof; ++Step)
		{
			FHitResult Hit;
			if (!World->LineTraceSingleByObjectType(Hit, Start, End, Objects, Params) || !Hit.GetComponent())
			{
				break;
			}
			UPrimitiveComponent* HitPiece = Hit.GetComponent();
			if (HitPiece->GetOwner() && RoofCutawayRules::ShouldCutPiece(GetTraits(*HitPiece), HitPiece->Bounds.GetBox().Min.Z, HeadZ, Config.HeadClearanceCm))
			{
				Roof = HitPiece->GetOwner();
				break;
			}
			Params.AddIgnoredComponent(HitPiece);
		}
	}

	FBox Footprint(ForceInit);
	if (Roof)
	{
		AddActorPieces(*Roof, nullptr);
		TInlineComponentArray<UPrimitiveComponent*> Prims(Roof);
		for (UPrimitiveComponent* Prim : Prims)
		{
			if (OutPieces.Contains(Prim))
			{
				Footprint += Prim->Bounds.GetBox();
			}
		}
		if (!RoofCutawayRules::IsBuildingSized(Footprint, Config.MaxFootprintCm))
		{
			for (UPrimitiveComponent* Prim : Prims)
			{
				OutPieces.Remove(Prim); // a level slab / sky mesh over him: no building
			}
			Footprint = FBox(ForceInit);
		}
	}

	// 2. Graybox buildings of separate actors: the pieces of other actors over the roof's footprint, above his head (ceilings,
	// upper floors); actors tagged RoofCutaway whole.
	if (Footprint.IsValid)
	{
		const float BottomZ = HeadZ + Config.HeadClearanceCm;
		const float TopZ = FMath::Max(Footprint.Max.Z, End.Z);
		const FVector Extent((Footprint.Max.X - Footprint.Min.X) * 0.5f - Config.FootprintInsetCm,
			(Footprint.Max.Y - Footprint.Min.Y) * 0.5f - Config.FootprintInsetCm, (TopZ - BottomZ) * 0.5f);
		if (Extent.X > 0.f && Extent.Y > 0.f && Extent.Z > 0.f)
		{
			const FVector Centre((Footprint.Min.X + Footprint.Max.X) * 0.5f, (Footprint.Min.Y + Footprint.Max.Y) * 0.5f, (BottomZ + TopZ) * 0.5f);
			TArray<FOverlapResult> Overlaps;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(RoofCutawayFootprint), false, &Operative);
			World->OverlapMultiByObjectType(Overlaps, Centre, FQuat::Identity, Objects, FCollisionShape::MakeBox(Extent), Params);
			for (const FOverlapResult& Overlap : Overlaps)
			{
				const UPrimitiveComponent* Piece = Overlap.GetComponent();
				const AActor* Owner = Piece ? Piece->GetOwner() : nullptr;
				if (Owner && Owner != Roof)
				{
					AddActorPieces(*Owner, &Footprint);
				}
			}
		}
	}
	// Opted-in actors without collision: over the roof's footprint, or right over his head when no roof was found.
	for (const TWeakObjectPtr<AActor>& Weak : OptInActors)
	{
		const AActor* Actor = Weak.Get();
		if (!Actor || Actor == Roof)
		{
			continue;
		}
		const FBox Bounds = Actor->GetComponentsBoundingBox(true); // incl. pieces without collision (a lamp, a trim)
		const bool bOverRoof = Footprint.IsValid && RoofCutawayRules::OverlapsFootprint(Footprint, Bounds, Config.FootprintInsetCm);
		const bool bOverHead = Bounds.IsValid && Bounds.Max.Z > HeadZ && Bounds.Min.X <= Location.X && Location.X <= Bounds.Max.X
			&& Bounds.Min.Y <= Location.Y && Location.Y <= Bounds.Max.Y;
		if (bOverRoof || bOverHead)
		{
			AddActorPieces(*Actor, nullptr);
		}
	}
}

void URoofCutawaySubsystem::CutPiece(UPrimitiveComponent& Piece, double Now)
{
	FCutPiece& Cut = CutPieces.AddDefaulted_GetRef();
	Cut.Piece = &Piece;
	Cut.VisibilityResponse = Piece.GetCollisionResponseToChannel(ECC_Visibility);
	Cut.bRenderInMainPass = Piece.bRenderInMainPass;
	Cut.bRenderInDepthPass = Piece.bRenderInDepthPass;
	Cut.bCastShadow = Piece.CastShadow;
	Cut.bAffectDistanceFieldLighting = Piece.bAffectDistanceFieldLighting;
	Cut.LastCoveredSeconds = Now;
	// Not drawn (main + depth pass); its shadow per Codex.RoofCutaway.HiddenShadow; clicks and the silhouette trace pass through.
	Piece.SetRenderInMainPass(false);
	Piece.SetRenderInDepthPass(false);
	Piece.SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	// Only on the state change (cut / restore), never per update.
	const bool bHiddenShadow = CVarRoofCutawayHiddenShadow.GetValueOnGameThread() != 0;
	if (RoofCutawayRules::CastsShadowWhileCut(Cut.bCastShadow, bHiddenShadow) != Cut.bCastShadow)
	{
		Piece.SetCastShadow(false);
	}
	if (RoofCutawayRules::CastsShadowWhileCut(Cut.bAffectDistanceFieldLighting, bHiddenShadow) != Cut.bAffectDistanceFieldLighting)
	{
		Piece.SetAffectDistanceFieldLighting(false);
	}
	UE_LOG(LogCodexTactics, Verbose, TEXT("[RoofCutaway] cut %s.%s"), Piece.GetOwner() ? *Piece.GetOwner()->GetName() : TEXT("-"), *Piece.GetName());
	OnCutawayChanged.Broadcast(&Piece, true);
}

void URoofCutawaySubsystem::RestorePiece(FCutPiece& Cut)
{
	UPrimitiveComponent* Piece = Cut.Piece.Get();
	if (!IsValid(Piece))
	{
		return;
	}
	Piece->SetRenderInMainPass(Cut.bRenderInMainPass);
	Piece->SetRenderInDepthPass(Cut.bRenderInDepthPass);
	Piece->SetCollisionResponseToChannel(ECC_Visibility, Cut.VisibilityResponse);
	if (Piece->CastShadow != Cut.bCastShadow)
	{
		Piece->SetCastShadow(Cut.bCastShadow);
	}
	if (Piece->bAffectDistanceFieldLighting != Cut.bAffectDistanceFieldLighting)
	{
		Piece->SetAffectDistanceFieldLighting(Cut.bAffectDistanceFieldLighting);
	}
	UE_LOG(LogCodexTactics, Verbose, TEXT("[RoofCutaway] restored %s.%s"), Piece->GetOwner() ? *Piece->GetOwner()->GetName() : TEXT("-"), *Piece->GetName());
	OnCutawayChanged.Broadcast(Piece, false);
}

void URoofCutawaySubsystem::RefreshOptInActors(double Now)
{
	if (Now - OptInRefreshSeconds < 1.0)
	{
		return;
	}
	OptInRefreshSeconds = Now;
	OptInActors.Reset();
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(RoofCutawayRules::OptInTag()) && !It->ActorHasTag(RoofCutawayRules::OptOutTag()))
		{
			OptInActors.Add(*It);
		}
	}
}
