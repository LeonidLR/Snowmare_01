#include "Environment/WeatherSnowOverlayComponent.h"

#include "Camera/CameraZoneVolume.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/TacticalCameraPawn.h"
#include "CodexTactics.h"
#include "CollisionQueryParams.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Environment/SnowOverlayRules.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "MaterialShared.h"
#include "PrimitiveSceneProxy.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Misc/PackageName.h"

namespace WeatherSnowOverlay
{
	int32 GMaxMeshes = 250;
	static FAutoConsoleVariableRef CVarMaxMeshes(TEXT("Codex.Env.SnowOverlayMax"), GMaxMeshes,
		TEXT("Max static meshes that get the snow overlay material (an extra draw pass each), nearest to the view first. 0 = none."));

	int32 GEnabled = 1;
	static FAutoConsoleVariableRef CVarEnabled(TEXT("Codex.Env.SnowOverlay"), GEnabled,
		TEXT("1 = the gradual snow cover overlay is on (environment phase 4c), 0 = removed from every mesh (perf A/B)."));

	int32 GNaniteFallback = 0;
	static FAutoConsoleVariableRef CVarNaniteFallback(TEXT("Codex.Env.SnowOverlayNanite"), GNaniteFallback,
		TEXT("1 = Nanite meshes get the snow overlay too, drawn through their non-Nanite fallback mesh while they hold it (UE 5.8 Nanite never draws an overlay material); 0 = Nanite meshes are skipped."));

	int32 GGroundEnabled = 1;
	static FAutoConsoleVariableRef CVarGroundEnabled(TEXT("Codex.Env.SnowGround"), GGroundEnabled,
		TEXT("1 = the ground slabs (floor templates, big flat floors) get the snow ground overlay M_SnowGroundOverlay (environment phase 4d), 0 = removed (perf A/B)."));

	int32 GGroundMax = 32;
	static FAutoConsoleVariableRef CVarGroundMax(TEXT("Codex.Env.SnowGroundMax"), GGroundMax,
		TEXT("Max ground slabs that get the snow ground overlay (one extra translucent pass over the visible floor each), nearest edge first. 0 = none."));

	/** Seconds between automatic re-scans while the overlay is shown (the presenter may call RefreshTargets sooner). */
	constexpr double AutoRefreshSeconds = 3.0;
	/** A re-scan also happens once the view focus moved this share of MaxDistance. */
	constexpr float RefocusShare = 0.2f;
	/** The upward cover trace: how high above a mesh top something counts as a roof, cm. */
	constexpr float CoverTraceHeightCm = 4000.f;

	const TCHAR* DefaultMaterialPath = TEXT("/Game/Environment/Weather/Snow/Materials/MI_SnowOverlay.MI_SnowOverlay");
	const TCHAR* DefaultGroundMaterialPath = TEXT("/Game/Environment/Weather/Snow/Materials/MI_SnowGroundOverlay.MI_SnowGroundOverlay");
	// Section 18: the wet look of temperate levels.
	const TCHAR* DefaultWetMaterialPath = TEXT("/Game/Environment/Weather/Rain/Materials/MI_WetOverlay.MI_WetOverlay");
	const TCHAR* DefaultWetGroundMaterialPath = TEXT("/Game/Environment/Weather/Rain/Materials/MI_WetGroundOverlay.MI_WetGroundOverlay");

	/** The soft reference names an existing package (a missing wet material is skipped without load warnings). */
	bool PackageExists(const FSoftObjectPath& Path)
	{
		return !Path.IsNull() && FPackageName::DoesPackageExist(Path.GetLongPackageName());
	}

	bool ClassNameContains(const UObject* Object, const TCHAR* Text)
	{
		return Object && Object->GetClass()->GetName().Contains(Text);
	}

	bool HasTag(const UActorComponent& Component, FName Tag)
	{
		const AActor* Owner = Component.GetOwner();
		return Component.ComponentHasTag(Tag) || (Owner && Owner->ActorHasTag(Tag));
	}

	/** An overlay material that is not one of ours (the object overlay or the ground instance). */
	bool HasAnyOverlay(const UStaticMeshComponent& Mesh, const UMaterialInterface* Ours, const UMaterialInterface* OursGround)
	{
		const UMaterialInterface* Overlay = Mesh.GetOverlayMaterial();
		if (Overlay && Overlay != Ours && Overlay != OursGround)
		{
			return true;
		}
		TArray<TObjectPtr<UMaterialInterface>> Slots;
		Mesh.GetMaterialSlotsOverlayMaterial(Slots);
		return Slots.ContainsByPredicate([](const TObjectPtr<UMaterialInterface>& Slot) { return Slot != nullptr; });
	}

	/** The mesh is rendered by a Nanite proxy now (Nanite draws no overlay material in UE 5.8). */
	bool IsDrawnByNanite(const UStaticMeshComponent& Mesh)
	{
		const FPrimitiveSceneProxy* Proxy = Mesh.GetSceneProxy();
		return Proxy && Proxy->IsNaniteMesh();
	}

	bool IsTranslucentOnly(const UStaticMeshComponent& Mesh)
	{
		const int32 Count = Mesh.GetNumMaterials();
		if (Count == 0)
		{
			return false;
		}
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const UMaterialInterface* Material = Mesh.GetMaterial(Index);
			if (!Material || !IsTranslucentBlendMode(*Material))
			{
				return false;
			}
		}
		return true;
	}

	/** Something static is straight above Start (a roof, a ceiling, an overhang): no pawn / terrain / hidden / sky-sized mesh. */
	bool IsCoveredAt(UWorld& World, const FVector& Start, const AActor* Ignore, float MaxFootprintCm)
	{
		const FVector End = Start + FVector(0.f, 0.f, CoverTraceHeightCm);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(SnowOverlayCover), false, Ignore);
		TArray<FHitResult> Hits;
		World.LineTraceMultiByObjectType(Hits, Start, End, FCollisionObjectQueryParams(ECC_WorldStatic), Params);
		for (const FHitResult& Hit : Hits)
		{
			const UPrimitiveComponent* Above = Hit.GetComponent();
			const AActor* AboveOwner = Hit.GetActor();
			if (!Above || (AboveOwner && AboveOwner->IsA<APawn>()) || ClassNameContains(AboveOwner, TEXT("Landscape"))
				|| ClassNameContains(Above, TEXT("Landscape")) || !Above->IsVisible() || (AboveOwner && AboveOwner->IsHidden()))
			{
				continue;
			}
			const FVector Extent = Above->Bounds.BoxExtent;
			if (2.f * FMath::Max(Extent.X, Extent.Y) > MaxFootprintCm)
			{
				continue;
			}
			return true;
		}
		return false;
	}
}

UWeatherSnowOverlayComponent::UWeatherSnowOverlayComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	OverlayMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(WeatherSnowOverlay::DefaultMaterialPath));
	GroundMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(WeatherSnowOverlay::DefaultGroundMaterialPath));
	WetOverlayMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(WeatherSnowOverlay::DefaultWetMaterialPath));
	WetGroundMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(WeatherSnowOverlay::DefaultWetGroundMaterialPath));
	// The tactical view: ~60 m around its focus covers the screen with margin (orchestrator cap, 2026-10-22).
	MaxDistance = 6000.f;
}

bool UWeatherSnowOverlayComponent::CanDraw() const
{
	const UWorld* World = GetWorld();
	return FApp::CanEverRender() && World && World->IsGameWorld() && World->GetNetMode() != NM_DedicatedServer;
}

UMaterialInterface* UWeatherSnowOverlayComponent::ResolveOverlayMaterial()
{
	if (!bMaterialResolved)
	{
		bMaterialResolved = true;
		LoadedOverlay = OverlayMaterial.IsNull() ? nullptr : OverlayMaterial.LoadSynchronous();
		if (!LoadedOverlay)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[Env] snow overlay: material %s not found - no snow cover on objects"), *OverlayMaterial.ToString());
		}
	}
	return LoadedOverlay;
}

UMaterialInstanceDynamic* UWeatherSnowOverlayComponent::ResolveGroundMaterial()
{
	if (!bGroundResolved)
	{
		bGroundResolved = true;
		UMaterialInterface* Base = GroundMaterial.IsNull() ? nullptr : GroundMaterial.LoadSynchronous();
		GroundInstance = Base ? UMaterialInstanceDynamic::Create(Base, this, TEXT("SnowGroundOverlay")) : nullptr;
		if (!GroundInstance)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[Env] snow overlay: ground material %s not found - no snow on the ground slabs"), *GroundMaterial.ToString());
		}
	}
	return GroundInstance;
}

UMaterialInterface* UWeatherSnowOverlayComponent::ResolveWetOverlayMaterial()
{
	if (!bWetResolved)
	{
		bWetResolved = true;
		LoadedWetOverlay = WeatherSnowOverlay::PackageExists(WetOverlayMaterial.ToSoftObjectPath()) ? WetOverlayMaterial.LoadSynchronous() : nullptr;
		if (!LoadedWetOverlay)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[Env] wet overlay: material %s not found - no wet look on objects"), *WetOverlayMaterial.ToString());
		}
	}
	return LoadedWetOverlay;
}

UMaterialInstanceDynamic* UWeatherSnowOverlayComponent::ResolveWetGroundMaterial()
{
	if (!bWetGroundResolved)
	{
		bWetGroundResolved = true;
		UMaterialInterface* Base = WeatherSnowOverlay::PackageExists(WetGroundMaterial.ToSoftObjectPath()) ? WetGroundMaterial.LoadSynchronous() : nullptr;
		WetGroundInstance = Base ? UMaterialInstanceDynamic::Create(Base, this, TEXT("WetGroundOverlay")) : nullptr;
		if (!WetGroundInstance)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[Env] wet overlay: ground material %s not found - no wet look on the ground slabs"), *WetGroundMaterial.ToString());
		}
	}
	return WetGroundInstance;
}

UMaterialInterface* UWeatherSnowOverlayComponent::ActiveOverlay() const
{
	return bWetMode ? LoadedWetOverlay.Get() : LoadedOverlay.Get();
}

UMaterialInstanceDynamic* UWeatherSnowOverlayComponent::ActiveGround() const
{
	return bWetMode ? WetGroundInstance.Get() : GroundInstance.Get();
}

void UWeatherSnowOverlayComponent::SetWetMode(bool bWet)
{
	if (bWetMode == bWet)
	{
		return;
	}
	// The previous mode's materials come off first (RemoveOverlays compares against the materials of the mode in force).
	RemoveOverlays();
	bWetMode = bWet;
	bScanned = false;
	LastLoggedSummary = -1;
}

UMaterialInterface* UWeatherSnowOverlayComponent::GetGroundOverlayMaterial() const
{
	return ActiveGround();
}

void UWeatherSnowOverlayComponent::UpdateGroundZones(const TArray<TPair<FTransform, FVector>>& Zones)
{
	UMaterialInstanceDynamic* Ground = ActiveGround();
	if (!Ground)
	{
		return;
	}
	for (int32 Index = 0; Index < SnowOverlayRules::GroundZoneCount; ++Index)
	{
		FLinearColor A(0.f, 0.f, 0.f, 0.f);
		FLinearColor B(1.f, 0.f, 0.f, 0.f);
		if (Zones.IsValidIndex(Index))
		{
			SnowOverlayRules::MakeGroundZoneParams(Zones[Index].Key, Zones[Index].Value, A, B);
		}
		Ground->SetVectorParameterValue(*FString::Printf(TEXT("Zone%dA"), Index), A);
		Ground->SetVectorParameterValue(*FString::Printf(TEXT("Zone%dB"), Index), B);
	}
}

void UWeatherSnowOverlayComponent::ApplyWetCover(float Wetness, float PuddleAmount)
{
	LastWetness = Wetness;
	LastPuddles = PuddleAmount;
	if (!CanDraw())
	{
		return;
	}
	SetWetMode(true);
	ApplyShown(bVisuals && SnowOverlayRules::ShouldShowWetOverlay(Wetness, PuddleAmount));
}

void UWeatherSnowOverlayComponent::ApplySnowCover(float SnowAmount, float FrostAmount)
{
	LastSnow = SnowAmount;
	LastFrost = FrostAmount;
	if (!CanDraw())
	{
		return;
	}
	SetWetMode(false);
	ApplyShown(bVisuals && SnowOverlayRules::ShouldShowOverlay(SnowAmount, FrostAmount));
}

void UWeatherSnowOverlayComponent::ApplyShown(bool bCanShow)
{
	const bool bWantObjects = bCanShow && WeatherSnowOverlay::GEnabled != 0 && WeatherSnowOverlay::GMaxMeshes > 0;
	const bool bWantGround = bCanShow && WeatherSnowOverlay::GGroundEnabled != 0 && WeatherSnowOverlay::GGroundMax > 0;
	if (!bWantObjects && !bWantGround)
	{
		if (bOverlayShown || bGroundShown || Touched.Num() > 0 || GroundTouched.Num() > 0)
		{
			RemoveOverlays();
		}
		return;
	}
	const bool bChanged = bWantObjects != bOverlayShown || bWantGround != bGroundShown;
	bOverlayShown = bWantObjects;
	bGroundShown = bWantGround;
	const double Now = GetWorld()->GetRealTimeSeconds();
	const bool bRefocus = MaxDistance > 0.f && FVector::DistSquared(GetViewFocus(), LastRefreshFocus) > FMath::Square(MaxDistance * WeatherSnowOverlay::RefocusShare);
	if (!bScanned || Now - LastRefreshSeconds > WeatherSnowOverlay::AutoRefreshSeconds || bRefocus || LastMaxMeshes != WeatherSnowOverlay::GMaxMeshes
		|| LastGroundMax != WeatherSnowOverlay::GGroundMax)
	{
		RefreshTargets();
	}
	else if (bChanged)
	{
		SyncOverlays();
	}
}

void UWeatherSnowOverlayComponent::SetVisualsEnabled(bool bEnabled)
{
	bVisuals = bEnabled;
	if (!bEnabled)
	{
		RemoveOverlays();
	}
	else if (bWetMode)
	{
		ApplyWetCover(LastWetness, LastPuddles);
	}
	else
	{
		ApplySnowCover(LastSnow, LastFrost);
	}
}

FVector UWeatherSnowOverlayComponent::GetViewFocus() const
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	if (!PC)
	{
		return FVector::ZeroVector;
	}
	if (const ATacticalCameraPawn* Tactical = Cast<ATacticalCameraPawn>(PC->GetViewTarget()))
	{
		return Tactical->GetFocus();
	}
	if (!PC->PlayerCameraManager)
	{
		return PC->GetViewTarget() ? PC->GetViewTarget()->GetActorLocation() : FVector::ZeroVector;
	}
	// Any other view (a level / cinematic camera): the ground point it looks at.
	const FVector Eye = PC->PlayerCameraManager->GetCameraLocation();
	const FVector End = Eye + PC->PlayerCameraManager->GetCameraRotation().Vector() * 20000.f;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SnowOverlayFocus), false, PC->GetPawn());
	if (World->LineTraceSingleByObjectType(Hit, Eye, End, FCollisionObjectQueryParams(ECC_WorldStatic), Params))
	{
		return Hit.ImpactPoint;
	}
	return Eye;
}

void UWeatherSnowOverlayComponent::RefreshTargets()
{
	UWorld* World = GetWorld();
	if (!CanDraw() || !World)
	{
		return;
	}
	const double StartSeconds = FPlatformTime::Seconds();
	// Section 18: the wet materials on a temperate level, the snow ones otherwise (same targets).
	UMaterialInterface* Material = bWetMode ? ResolveWetOverlayMaterial() : ResolveOverlayMaterial();
	UMaterialInstanceDynamic* Ground = bWetMode ? ResolveWetGroundMaterial() : ResolveGroundMaterial();
	bScanned = true;
	LastRefreshSeconds = World->GetRealTimeSeconds();
	LastRefreshFocus = GetViewFocus();
	LastMaxMeshes = WeatherSnowOverlay::GMaxMeshes;
	LastGroundMax = WeatherSnowOverlay::GGroundMax;
	if (!Material && !Ground)
	{
		Targets.Reset();
		GroundTargets.Reset();
		SyncOverlays();
		return;
	}

	SnowOverlayRules::FSnowTargetConfig Config;
	Config.bAllowNaniteFallback = WeatherSnowOverlay::GNaniteFallback != 0;
	// Interior zones: closed rooms and half-open shelters (ACameraZoneVolume, the cold system's indoor volumes).
	struct FZoneBox
	{
		FTransform Transform;
		FVector HalfExtent;
		/** The highest top of the zone's own meshes (its roof), filled after the cheap pass. */
		double RoofTopZ = -UE_BIG_NUMBER;
	};
	TArray<FZoneBox> Interiors;
	for (TActorIterator<ACameraZoneVolume> It(World); It; ++It)
	{
		if (It->Environment == ECameraZoneEnvironment::Closed || It->Environment == ECameraZoneEnvironment::Shelter)
		{
			Interiors.Add({ It->GetZoneTransform(), It->GetZoneHalfExtent() });
		}
	}

	// 1. Cheap filter over every static mesh in the world: objects (M_SnowOverlay) and ground slabs (M_SnowGroundOverlay).
	constexpr int32 VerdictCount = static_cast<int32>(SnowOverlayRules::ESnowTargetVerdict::NotGround) + 1;
	TArray<UStaticMeshComponent*> Pool;
	TArray<SnowOverlayRules::FSnowCandidate> Candidates;
	TArray<UStaticMeshComponent*> GroundPool;
	TArray<SnowOverlayRules::FSnowCandidate> GroundCandidates;
	int32 VerdictCounts[VerdictCount] = {};
	int32 GroundVerdictCounts[VerdictCount] = {};
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor || Actor == GetOwner())
		{
			continue;
		}
		const bool bPawn = Actor->IsA<APawn>();
		const bool bLandscapeActor = WeatherSnowOverlay::ClassNameContains(Actor, TEXT("Landscape"));
		const bool bFoliageActor = WeatherSnowOverlay::ClassNameContains(Actor, TEXT("InstancedFoliageActor"));
		TInlineComponentArray<UStaticMeshComponent*> Meshes(Actor);
		for (UStaticMeshComponent* Mesh : Meshes)
		{
			if (!Mesh || !Mesh->IsRegistered() || !Mesh->GetStaticMesh())
			{
				continue;
			}
			SnowOverlayRules::FSnowTargetTraits Traits;
			Traits.bStaticMesh = true;
			Traits.bInstanced = bFoliageActor || Mesh->IsA<UInstancedStaticMeshComponent>();
			Traits.bPawn = bPawn;
			Traits.bLandscape = bLandscapeActor || WeatherSnowOverlay::ClassNameContains(Mesh, TEXT("Landscape"));
			// A roof cut away by the roof cutaway (not in the main pass) keeps its snow for when it comes back.
			Traits.bVisible = Mesh->IsVisible() && !Actor->IsHidden();
			Traits.bHasOverlay = WeatherSnowOverlay::HasAnyOverlay(*Mesh, Material, Ground);
			Traits.bOptOut = WeatherSnowOverlay::HasTag(*Mesh, SnowOverlayRules::OptOutTag());
			Traits.bTranslucentOnly = WeatherSnowOverlay::IsTranslucentOnly(*Mesh);
			Traits.bNanite = NaniteForced.Contains(Mesh) || WeatherSnowOverlay::IsDrawnByNanite(*Mesh);
			Traits.bFloorTemplate = SnowOverlayRules::IsFloorTemplateMeshName(Mesh->GetStaticMesh()->GetName());
			const FBoxSphereBounds Bounds = Mesh->Bounds;
			Traits.BoundsRadiusCm = Bounds.SphereRadius;
			Traits.FootprintCm = 2.f * FMath::Max(Bounds.BoxExtent.X, Bounds.BoxExtent.Y);
			Traits.MinWidthCm = 2.f * FMath::Min(Bounds.BoxExtent.X, Bounds.BoxExtent.Y);
			Traits.HeightCm = 2.f * Bounds.BoxExtent.Z;
			const SnowOverlayRules::ESnowTargetVerdict Verdict = SnowOverlayRules::Classify(Traits, Config);
			if (Verdict == SnowOverlayRules::ESnowTargetVerdict::Ground)
			{
				// A ground slab: the ground overlay's candidate (ordered by its nearest edge - a slab is big).
				++VerdictCounts[static_cast<int32>(Verdict)];
				if (Ground)
				{
					const FBox Box = Bounds.GetBox();
					SnowOverlayRules::FSnowCandidate Candidate;
					Candidate.Index = GroundPool.Add(Mesh);
					Candidate.DistanceSq = SnowOverlayRules::DistanceSqToBox2D(LastRefreshFocus, Box.Min, Box.Max);
					GroundCandidates.Add(Candidate);
				}
				continue;
			}
			if (Verdict != SnowOverlayRules::ESnowTargetVerdict::Accept || !Material)
			{
				++VerdictCounts[static_cast<int32>(Verdict)];
				continue;
			}
			SnowOverlayRules::FSnowCandidate Candidate;
			Candidate.Index = Pool.Add(Mesh);
			Candidate.DistanceSq = FVector::DistSquared(Bounds.Origin, LastRefreshFocus);
			Candidates.Add(Candidate);
		}
	}

	// The roof of each interior: the highest mesh top among the candidates inside it (the roof itself stays outdoor).
	for (FZoneBox& Zone : Interiors)
	{
		for (const UStaticMeshComponent* Mesh : Pool)
		{
			const FBox Box = Mesh->Bounds.GetBox();
			if (SnowOverlayRules::IsInsideInterior(Box.Min, Box.Max, Zone.Transform, Zone.HalfExtent, UE_BIG_NUMBER))
			{
				Zone.RoofTopZ = FMath::Max(Zone.RoofTopZ, Box.Max.Z);
			}
		}
	}

	// 2. Nearest first within MaxDistance; the world queries (interior, cover) only until the cap is full.
	const TArray<int32> Order = SnowOverlayRules::OrderCandidates(Candidates, MaxDistance, WeatherSnowOverlay::GMaxMeshes, /*bKeepAll*/ true);
	const int32 OutOfRange = Candidates.Num() - Order.Num();
	int32 OverCap = 0;
	Targets.Reset();
	for (const int32 Index : Order)
	{
		if (Targets.Num() >= WeatherSnowOverlay::GMaxMeshes)
		{
			++OverCap;
			continue;
		}
		UStaticMeshComponent* Mesh = Pool[Index];
		const FBox Box = Mesh->Bounds.GetBox();
		SnowOverlayRules::FSnowTargetTraits Traits;
		Traits.bStaticMesh = true;
		Traits.BoundsRadiusCm = Mesh->Bounds.SphereRadius;
		Traits.bInterior = Interiors.ContainsByPredicate([&Box](const FZoneBox& Zone)
		{
			return SnowOverlayRules::IsInsideInterior(Box.Min, Box.Max, Zone.Transform, Zone.HalfExtent, UE_BIG_NUMBER)
				&& SnowOverlayRules::IsBelowInteriorRoof(Box.Max.Z, Zone.RoofTopZ);
		});
		if (!Traits.bInterior)
		{
			// Under cover: the first static thing straight above the top centre that is no pawn / terrain / sky-sized mesh.
			Traits.bCovered = WeatherSnowOverlay::IsCoveredAt(*World, FVector(Box.GetCenter().X, Box.GetCenter().Y, Box.Max.Z + 5.f), Mesh->GetOwner(),
				Config.MaxFootprintCm);
		}
		const SnowOverlayRules::ESnowTargetVerdict Verdict = SnowOverlayRules::Classify(Traits, Config);
		if (Verdict != SnowOverlayRules::ESnowTargetVerdict::Accept)
		{
			UE_LOG(LogCodexTactics, Verbose, TEXT("[Env] snow overlay: %s.%s %s"), *GetNameSafe(Mesh->GetOwner()), *Mesh->GetName(), SnowOverlayRules::VerdictToString(Verdict));
			++VerdictCounts[static_cast<int32>(Verdict)];
			continue;
		}
		Targets.Add(Mesh);
	}

	// 3. Ground slabs: nearest edge first within MaxDistance, own cap; interior = the slab inside a zone, covered = most of its sample
	// points under a roof. Interior zones ON a slab are kept bare per pixel by the ground material (the nearest GroundZoneCount zones).
	const TArray<int32> GroundOrder = SnowOverlayRules::OrderCandidates(GroundCandidates, MaxDistance, WeatherSnowOverlay::GGroundMax, /*bKeepAll*/ true);
	const int32 GroundOutOfRange = GroundCandidates.Num() - GroundOrder.Num();
	int32 GroundOverCap = 0;
	GroundTargets.Reset();
	for (const int32 Index : GroundOrder)
	{
		if (GroundTargets.Num() >= WeatherSnowOverlay::GGroundMax)
		{
			++GroundOverCap;
			continue;
		}
		UStaticMeshComponent* Mesh = GroundPool[Index];
		const FBox Box = Mesh->Bounds.GetBox();
		SnowOverlayRules::FSnowTargetTraits Traits;
		Traits.bStaticMesh = true;
		Traits.BoundsRadiusCm = Mesh->Bounds.SphereRadius;
		Traits.FootprintCm = 2.f * FMath::Max(Mesh->Bounds.BoxExtent.X, Mesh->Bounds.BoxExtent.Y);
		Traits.MinWidthCm = 2.f * FMath::Min(Mesh->Bounds.BoxExtent.X, Mesh->Bounds.BoxExtent.Y);
		Traits.HeightCm = 2.f * Mesh->Bounds.BoxExtent.Z;
		Traits.bFloorTemplate = SnowOverlayRules::IsFloorTemplateMeshName(Mesh->GetStaticMesh()->GetName());
		Traits.bInterior = Interiors.ContainsByPredicate([&Box](const FZoneBox& Zone)
		{
			return SnowOverlayRules::IsInsideInterior(Box.Min, Box.Max, Zone.Transform, Zone.HalfExtent);
		});
		if (!Traits.bInterior)
		{
			const TArray<FVector2D> Points = SnowOverlayRules::GroundCoverSamplePoints(FVector2D(Box.Min), FVector2D(Box.Max));
			int32 CoveredPoints = 0;
			for (const FVector2D& Point : Points)
			{
				CoveredPoints += WeatherSnowOverlay::IsCoveredAt(*World, FVector(Point.X, Point.Y, Box.Max.Z + 5.f), Mesh->GetOwner(), Config.MaxFootprintCm) ? 1 : 0;
			}
			Traits.bCovered = SnowOverlayRules::IsGroundCovered(CoveredPoints, Points.Num());
		}
		const SnowOverlayRules::ESnowTargetVerdict Verdict = SnowOverlayRules::ClassifyGround(Traits, Config);
		if (Verdict != SnowOverlayRules::ESnowTargetVerdict::Accept)
		{
			UE_LOG(LogCodexTactics, Verbose, TEXT("[Env] snow ground: %s.%s %s"), *GetNameSafe(Mesh->GetOwner()), *Mesh->GetName(), SnowOverlayRules::VerdictToString(Verdict));
			++GroundVerdictCounts[static_cast<int32>(Verdict)];
			continue;
		}
		GroundTargets.Add(Mesh);
	}
	if (Ground)
	{
		// The interior zones nearest to the view (the ground material keeps them bare).
		TArray<SnowOverlayRules::FSnowCandidate> ZoneCandidates;
		for (int32 Index = 0; Index < Interiors.Num(); ++Index)
		{
			SnowOverlayRules::FSnowCandidate Candidate;
			Candidate.Index = Index;
			Candidate.DistanceSq = FVector::DistSquared2D(Interiors[Index].Transform.GetLocation(), LastRefreshFocus);
			ZoneCandidates.Add(Candidate);
		}
		TArray<TPair<FTransform, FVector>> Zones;
		for (const int32 Index : SnowOverlayRules::OrderCandidates(ZoneCandidates, 0.f, SnowOverlayRules::GroundZoneCount))
		{
			Zones.Emplace(Interiors[Index].Transform, Interiors[Index].HalfExtent);
		}
		UpdateGroundZones(Zones);
	}
	SyncOverlays();

	const int32 Summary = Targets.Num() * 7919 + VerdictCounts[static_cast<int32>(SnowOverlayRules::ESnowTargetVerdict::Covered)] * 31
		+ VerdictCounts[static_cast<int32>(SnowOverlayRules::ESnowTargetVerdict::Interior)] + GroundTargets.Num() * 104729;
	const double Ms = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
	if (Summary != LastLoggedSummary)
	{
		LastLoggedSummary = Summary;
		auto Describe = [](const int32* Counts)
		{
			FString Text;
			for (int32 Index = 1; Index < VerdictCount; ++Index)
			{
				if (Counts[Index] > 0)
				{
					Text += FString::Printf(TEXT(" %s %d"), SnowOverlayRules::VerdictToString(static_cast<SnowOverlayRules::ESnowTargetVerdict>(Index)), Counts[Index]);
				}
			}
			return Text.IsEmpty() ? FString(TEXT(" none")) : Text;
		};
		UE_LOG(LogCodexTactics, Display, TEXT("[Env] %s overlay: %d mesh(es) (cap %d, radius %.0f m, %s); out of range %d, over cap %d; rejected:%s; scan %.2f ms"),
			bWetMode ? TEXT("wet") : TEXT("snow"), Targets.Num(), WeatherSnowOverlay::GMaxMeshes, MaxDistance / 100.f, bOverlayShown ? TEXT("shown") : TEXT("hidden"), OutOfRange, OverCap,
			*Describe(VerdictCounts), Ms);
		UE_LOG(LogCodexTactics, Display, TEXT("[Env] %s ground: %d slab(s) of %d ground candidate(s) (cap %d, %s); out of range %d, over cap %d; rejected:%s"),
			bWetMode ? TEXT("wet") : TEXT("snow"), GroundTargets.Num(), GroundCandidates.Num(), WeatherSnowOverlay::GGroundMax, bGroundShown ? TEXT("shown") : TEXT("hidden"), GroundOutOfRange,
			GroundOverCap, *Describe(GroundVerdictCounts));
	}
}

void UWeatherSnowOverlayComponent::SyncList(const TArray<TWeakObjectPtr<UMeshComponent>>& Wanted, TArray<TWeakObjectPtr<UMeshComponent>>& InOutTouched,
	UMaterialInterface* Material, bool bShow)
{
	bShow = bShow && Material != nullptr;
	// Remove from the components that are no target any more (or all when hidden).
	for (int32 Index = InOutTouched.Num() - 1; Index >= 0; --Index)
	{
		UMeshComponent* Mesh = InOutTouched[Index].Get();
		const bool bKeep = bShow && Mesh && Wanted.Contains(InOutTouched[Index]);
		if (!bKeep)
		{
			if (Mesh && Material && Mesh->GetOverlayMaterial() == Material)
			{
				Mesh->SetOverlayMaterial(nullptr);
			}
			RestoreNanite(Mesh);
			InOutTouched.RemoveAtSwap(Index);
		}
	}
	if (!bShow)
	{
		return;
	}
	for (const TWeakObjectPtr<UMeshComponent>& Weak : Wanted)
	{
		UMeshComponent* Mesh = Weak.Get();
		if (Mesh && !InOutTouched.Contains(Weak))
		{
			UStaticMeshComponent* StaticMesh = Cast<UStaticMeshComponent>(Mesh);
			if (StaticMesh && WeatherSnowOverlay::GNaniteFallback != 0 && WeatherSnowOverlay::IsDrawnByNanite(*StaticMesh))
			{
				// Nanite draws no overlay: this mesh shows its fallback mesh while it holds the snow.
				StaticMesh->SetForceDisableNanite(true);
				NaniteForced.Add(StaticMesh);
			}
			Mesh->SetOverlayMaterial(Material);
			InOutTouched.Add(Weak);
		}
	}
}

void UWeatherSnowOverlayComponent::SyncOverlays()
{
	SyncList(Targets, Touched, ActiveOverlay(), bOverlayShown);
	SyncList(GroundTargets, GroundTouched, ActiveGround(), bGroundShown);
}

void UWeatherSnowOverlayComponent::RemoveOverlays()
{
	bOverlayShown = false;
	bGroundShown = false;
	auto Remove = [this](TArray<TWeakObjectPtr<UMeshComponent>>& List, const UMaterialInterface* Material)
	{
		for (const TWeakObjectPtr<UMeshComponent>& Weak : List)
		{
			UMeshComponent* Mesh = Weak.Get();
			if (Mesh && Material && Mesh->GetOverlayMaterial() == Material)
			{
				Mesh->SetOverlayMaterial(nullptr);
			}
			RestoreNanite(Mesh);
		}
		List.Reset();
	};
	Remove(Touched, ActiveOverlay());
	Remove(GroundTouched, ActiveGround());
	NaniteForced.Reset();
}

void UWeatherSnowOverlayComponent::RestoreNanite(UMeshComponent* Mesh)
{
	UStaticMeshComponent* StaticMesh = Cast<UStaticMeshComponent>(Mesh);
	if (StaticMesh && NaniteForced.RemoveSwap(StaticMesh) > 0)
	{
		StaticMesh->SetForceDisableNanite(false);
	}
}

void UWeatherSnowOverlayComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RemoveOverlays();
	Targets.Reset();
	GroundTargets.Reset();
	Super::EndPlay(EndPlayReason);
}
