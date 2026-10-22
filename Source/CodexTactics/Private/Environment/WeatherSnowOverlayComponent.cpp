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
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"

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

	/** Seconds between automatic re-scans while the overlay is shown (the presenter may call RefreshTargets sooner). */
	constexpr double AutoRefreshSeconds = 3.0;
	/** A re-scan also happens once the view focus moved this share of MaxDistance. */
	constexpr float RefocusShare = 0.2f;
	/** The upward cover trace: how high above a mesh top something counts as a roof, cm. */
	constexpr float CoverTraceHeightCm = 4000.f;

	const TCHAR* DefaultMaterialPath = TEXT("/Game/Environment/Weather/Snow/Materials/MI_SnowOverlay.MI_SnowOverlay");

	bool ClassNameContains(const UObject* Object, const TCHAR* Text)
	{
		return Object && Object->GetClass()->GetName().Contains(Text);
	}

	bool HasTag(const UActorComponent& Component, FName Tag)
	{
		const AActor* Owner = Component.GetOwner();
		return Component.ComponentHasTag(Tag) || (Owner && Owner->ActorHasTag(Tag));
	}

	bool HasAnyOverlay(const UStaticMeshComponent& Mesh, const UMaterialInterface* Ours)
	{
		const UMaterialInterface* Overlay = Mesh.GetOverlayMaterial();
		if (Overlay && Overlay != Ours)
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
}

UWeatherSnowOverlayComponent::UWeatherSnowOverlayComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	OverlayMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(WeatherSnowOverlay::DefaultMaterialPath));
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

void UWeatherSnowOverlayComponent::ApplySnowCover(float SnowAmount, float FrostAmount)
{
	LastSnow = SnowAmount;
	LastFrost = FrostAmount;
	if (!CanDraw())
	{
		return;
	}
	const bool bWant = bVisuals && WeatherSnowOverlay::GEnabled != 0 && WeatherSnowOverlay::GMaxMeshes > 0
		&& SnowOverlayRules::ShouldShowOverlay(SnowAmount, FrostAmount);
	if (!bWant)
	{
		if (bOverlayShown)
		{
			RemoveOverlays();
		}
		return;
	}
	const double Now = GetWorld()->GetRealTimeSeconds();
	const bool bRefocus = MaxDistance > 0.f && FVector::DistSquared(GetViewFocus(), LastRefreshFocus) > FMath::Square(MaxDistance * WeatherSnowOverlay::RefocusShare);
	if (!bScanned || Now - LastRefreshSeconds > WeatherSnowOverlay::AutoRefreshSeconds || bRefocus || LastMaxMeshes != WeatherSnowOverlay::GMaxMeshes)
	{
		bOverlayShown = true;
		RefreshTargets();
	}
	else if (!bOverlayShown)
	{
		bOverlayShown = true;
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
	UMaterialInterface* Material = ResolveOverlayMaterial();
	bScanned = true;
	LastRefreshSeconds = World->GetRealTimeSeconds();
	LastRefreshFocus = GetViewFocus();
	LastMaxMeshes = WeatherSnowOverlay::GMaxMeshes;
	if (!Material)
	{
		Targets.Reset();
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

	// 1. Cheap filter over every static mesh in the world.
	TArray<UStaticMeshComponent*> Pool;
	TArray<SnowOverlayRules::FSnowCandidate> Candidates;
	int32 VerdictCounts[static_cast<int32>(SnowOverlayRules::ESnowTargetVerdict::Covered) + 1] = {};
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
			Traits.bHasOverlay = WeatherSnowOverlay::HasAnyOverlay(*Mesh, Material);
			Traits.bOptOut = WeatherSnowOverlay::HasTag(*Mesh, SnowOverlayRules::OptOutTag());
			Traits.bTranslucentOnly = WeatherSnowOverlay::IsTranslucentOnly(*Mesh);
			Traits.bNanite = NaniteForced.Contains(Mesh) || WeatherSnowOverlay::IsDrawnByNanite(*Mesh);
			const FBoxSphereBounds Bounds = Mesh->Bounds;
			Traits.BoundsRadiusCm = Bounds.SphereRadius;
			Traits.FootprintCm = 2.f * FMath::Max(Bounds.BoxExtent.X, Bounds.BoxExtent.Y);
			Traits.HeightCm = 2.f * Bounds.BoxExtent.Z;
			const SnowOverlayRules::ESnowTargetVerdict Verdict = SnowOverlayRules::Classify(Traits, Config);
			if (Verdict != SnowOverlayRules::ESnowTargetVerdict::Accept)
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
	int32 OutOfRange = Candidates.Num() - Order.Num();
	int32 OverCap = 0;
	Targets.Reset();
	FCollisionObjectQueryParams CoverObjects(ECC_WorldStatic);
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
			const FVector Start(Box.GetCenter().X, Box.GetCenter().Y, Box.Max.Z + 5.f);
			const FVector End = Start + FVector(0.f, 0.f, WeatherSnowOverlay::CoverTraceHeightCm);
			FCollisionQueryParams Params(SCENE_QUERY_STAT(SnowOverlayCover), false, Mesh->GetOwner());
			TArray<FHitResult> Hits;
			World->LineTraceMultiByObjectType(Hits, Start, End, CoverObjects, Params);
			for (const FHitResult& Hit : Hits)
			{
				const UPrimitiveComponent* Above = Hit.GetComponent();
				const AActor* AboveOwner = Hit.GetActor();
				if (!Above || (AboveOwner && AboveOwner->IsA<APawn>()) || WeatherSnowOverlay::ClassNameContains(AboveOwner, TEXT("Landscape"))
					|| WeatherSnowOverlay::ClassNameContains(Above, TEXT("Landscape")) || !Above->IsVisible() || (AboveOwner && AboveOwner->IsHidden()))
				{
					continue;
				}
				const FVector Extent = Above->Bounds.BoxExtent;
				if (2.f * FMath::Max(Extent.X, Extent.Y) > Config.MaxFootprintCm)
				{
					continue;
				}
				Traits.bCovered = true;
				break;
			}
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
	SyncOverlays();

	const int32 Summary = Targets.Num() * 7919 + VerdictCounts[static_cast<int32>(SnowOverlayRules::ESnowTargetVerdict::Covered)] * 31
		+ VerdictCounts[static_cast<int32>(SnowOverlayRules::ESnowTargetVerdict::Interior)];
	const double Ms = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
	if (Summary != LastLoggedSummary)
	{
		LastLoggedSummary = Summary;
		FString Rejected;
		for (int32 Index = 1; Index < UE_ARRAY_COUNT(VerdictCounts); ++Index)
		{
			if (VerdictCounts[Index] > 0)
			{
				Rejected += FString::Printf(TEXT(" %s %d"), SnowOverlayRules::VerdictToString(static_cast<SnowOverlayRules::ESnowTargetVerdict>(Index)),
					VerdictCounts[Index]);
			}
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[Env] snow overlay: %d mesh(es) (cap %d, radius %.0f m, %s); out of range %d, over cap %d; rejected:%s; scan %.2f ms"),
			Targets.Num(), WeatherSnowOverlay::GMaxMeshes, MaxDistance / 100.f, bOverlayShown ? TEXT("shown") : TEXT("hidden"), OutOfRange, OverCap,
			Rejected.IsEmpty() ? TEXT(" none") : *Rejected, Ms);
	}
}

void UWeatherSnowOverlayComponent::SyncOverlays()
{
	UMaterialInterface* Material = LoadedOverlay;
	const bool bShow = bOverlayShown && Material != nullptr;
	// Remove from the components that are no target any more (or all when hidden).
	for (int32 Index = Touched.Num() - 1; Index >= 0; --Index)
	{
		UMeshComponent* Mesh = Touched[Index].Get();
		const bool bKeep = bShow && Mesh && Targets.Contains(Touched[Index]);
		if (!bKeep)
		{
			if (Mesh && Mesh->GetOverlayMaterial() == Material)
			{
				Mesh->SetOverlayMaterial(nullptr);
			}
			RestoreNanite(Mesh);
			Touched.RemoveAtSwap(Index);
		}
	}
	if (!bShow)
	{
		return;
	}
	for (const TWeakObjectPtr<UMeshComponent>& Weak : Targets)
	{
		UMeshComponent* Mesh = Weak.Get();
		if (Mesh && !Touched.Contains(Weak))
		{
			UStaticMeshComponent* StaticMesh = Cast<UStaticMeshComponent>(Mesh);
			if (StaticMesh && WeatherSnowOverlay::GNaniteFallback != 0 && WeatherSnowOverlay::IsDrawnByNanite(*StaticMesh))
			{
				// Nanite draws no overlay: this mesh shows its fallback mesh while it holds the snow.
				StaticMesh->SetForceDisableNanite(true);
				NaniteForced.Add(StaticMesh);
			}
			Mesh->SetOverlayMaterial(Material);
			Touched.Add(Weak);
		}
	}
}

void UWeatherSnowOverlayComponent::RemoveOverlays()
{
	bOverlayShown = false;
	for (const TWeakObjectPtr<UMeshComponent>& Weak : Touched)
	{
		UMeshComponent* Mesh = Weak.Get();
		if (Mesh && LoadedOverlay && Mesh->GetOverlayMaterial() == LoadedOverlay)
		{
			Mesh->SetOverlayMaterial(nullptr);
		}
		RestoreNanite(Mesh);
	}
	Touched.Reset();
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
	Super::EndPlay(EndPlayReason);
}
