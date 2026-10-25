#include "Camera/RoofCutawaySubsystem.h"
#include "Camera/CameraZoneVolume.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "CollisionQueryParams.h"
#include "Combat/HealthComponent.h"
#include "Components/CapsuleComponent.h"
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

	bool ShouldRestore(double LastCoveredSeconds, double NowSeconds, float RestoreDelaySeconds)
	{
		return NowSeconds - LastCoveredSeconds >= RestoreDelaySeconds;
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
	TArray<ACameraZoneVolume*> ShownZones;
	for (TActorIterator<ACameraZoneVolume> It(World); It; ++It)
	{
		if (It->IsZoneCameraShown())
		{
			ShownZones.Add(*It);
		}
	}
	TSet<UPrimitiveComponent*> Covered;
	if (const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>())
	{
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			if (Member && !Member->IsHidden() && Member->HealthComponent && Member->HealthComponent->IsAlive() && !VehicleQueries::IsMounted(Member))
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

void URoofCutawaySubsystem::CollectRoofPieces(const AOperativeCharacter& Operative, const TArray<ACameraZoneVolume*>& ShownZones,
	TSet<UPrimitiveComponent*>& OutPieces) const
{
	// On a ladder he counts on the floor he left until he is off it (LADDERS.md 7.3: no cutaway flicker mid-ladder).
	const FVector Location = Operative.GetFloorReferenceLocation();
	const bool bInShownZone = ShownZones.ContainsByPredicate([&Location](const ACameraZoneVolume* Zone) { return Zone->ContainsLocation(Location); });
	if (!RoofCutawayRules::ShouldCutForOperative(bInShownZone))
	{
		return;
	}
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
	Cut.LastCoveredSeconds = Now;
	// Not drawn (main + depth pass), still casting its shadow; clicks and the silhouette trace pass through.
	Piece.SetRenderInMainPass(false);
	Piece.SetRenderInDepthPass(false);
	Piece.SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
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
