// UOccluderFadeSubsystem: a wall that hides the cursor's target fades (game designer play-test 2026-11-01, approved fix c). Godot reference:
// none (UE-only). See the header comment; the roof cutaway (URoofCutawaySubsystem) is the model for the main-pass hide.

#include "Camera/OccluderFadeSubsystem.h"

#include "CodexTactics.h"
#include "CollisionQueryParams.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/InteractableActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace OccluderFadeLocal
{
	TAutoConsoleVariable<int32> CVarOccluderFade(TEXT("Codex.OccluderFade"), 1,
		TEXT("1: a wall between the camera and the cursor's target (the click's cover ghost, the far-side cover spot under the cursor) fades to a dithered see-through look (game designer 2026-11-01, UOccluderFadeSubsystem); 0: off"));

	/** Pieces the camera trace may step through on its way to a target. */
	constexpr int32 OccluderMaxTraceHits = 16;
	/** The fade material's opacity parameter. */
	const FName OpacityParameter(TEXT("Opacity"));

	bool OccluderClassNameContains(const UObject* Object, const TCHAR* Part)
	{
		return Object && Object->GetClass()->GetName().Contains(Part);
	}
}

namespace OccluderFadeRules
{
	FName OptOutTag()
	{
		static const FName Tag(TEXT("NoOccluderFade"));
		return Tag;
	}

	bool IsFadeableKind(const FOccluderTraits& Traits)
	{
		return Traits.bStaticMesh && !Traits.bInstanced && Traits.bVisible && !Traits.bPawn && !Traits.bTerrain && !Traits.bFoliage && !Traits.bOptOut
			&& !Traits.bInteractable;
	}

	bool HidesTarget(float HitDistance, float TargetDistance, float PieceTopZ, float TargetFeetZ, float PieceShortSideCm, const FOccluderFadeConfig& Config)
	{
		return HitDistance < TargetDistance - Config.TargetMarginCm && PieceTopZ >= TargetFeetZ + Config.MinRiseAboveFeetCm
			&& PieceShortSideCm <= Config.MaxShortSideCm;
	}

	float Opacity(float Elapsed, bool bFadingIn, const FOccluderFadeConfig& Config)
	{
		const float Alpha = Config.FadeSeconds <= 0.f ? 1.f : FMath::Clamp(Elapsed / Config.FadeSeconds, 0.f, 1.f);
		const float Faded = FMath::Clamp(Config.FadedOpacity, 0.f, 1.f);
		return bFadingIn ? FMath::Lerp(1.f, Faded, Alpha) : FMath::Lerp(Faded, 1.f, Alpha);
	}

	bool ShouldRestore(double LastHidSeconds, double NowSeconds, const FOccluderFadeConfig& Config)
	{
		return NowSeconds - LastHidSeconds >= Config.RestoreDelaySeconds;
	}
}

bool UOccluderFadeSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UOccluderFadeSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UOccluderFadeSubsystem, STATGROUP_Tickables);
}

void UOccluderFadeSubsystem::Deinitialize()
{
	RestoreAll();
	Super::Deinitialize();
}

void UOccluderFadeSubsystem::SetTargets(const TArray<FVector>& InTargets)
{
	Targets = InTargets;
}

void UOccluderFadeSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (OccluderFadeLocal::CVarOccluderFade.GetValueOnGameThread() == 0)
	{
		RestoreAll();
		return;
	}
	UWorld* World = GetWorld();
	const float RealDelta = World ? World->DeltaRealTimeSeconds : DeltaTime;
	UpdateAccumulator += RealDelta;
	if (UpdateAccumulator >= Config.UpdateIntervalSeconds)
	{
		UpdateAccumulator = 0.f;
		UpdateNow();
	}
	if (World)
	{
		Animate(World->GetRealTimeSeconds());
	}
}

bool UOccluderFadeSubsystem::IsFaded(const UPrimitiveComponent* Piece) const
{
	return Piece && FadedPieces.ContainsByPredicate([Piece](const FFaded& Each) { return Each.Piece.Get() == Piece && !Each.bFadingOut; });
}

TArray<UPrimitiveComponent*> UOccluderFadeSubsystem::GetFadedPieces() const
{
	TArray<UPrimitiveComponent*> Out;
	for (const FFaded& Each : FadedPieces)
	{
		if (UPrimitiveComponent* Piece = Each.Piece.Get(); Piece && !Each.bFadingOut)
		{
			Out.Add(Piece);
		}
	}
	return Out;
}

int32 UOccluderFadeSubsystem::GetFadedCount() const
{
	int32 Count = 0;
	for (const FFaded& Each : FadedPieces)
	{
		Count += Each.Piece.IsValid() && !Each.bFadingOut ? 1 : 0;
	}
	return Count;
}

UStaticMeshComponent* UOccluderFadeSubsystem::GetProxy(const UPrimitiveComponent* Piece) const
{
	const FFaded* Found = FadedPieces.FindByPredicate([Piece](const FFaded& Each) { return Each.Piece.Get() == Piece; });
	return Found ? Found->Proxy.Get() : nullptr;
}

OccluderFadeRules::FOccluderTraits UOccluderFadeSubsystem::GetTraits(const UPrimitiveComponent& Piece)
{
	using namespace OccluderFadeLocal;
	OccluderFadeRules::FOccluderTraits Traits;
	const AActor* Owner = Piece.GetOwner();
	Traits.bPawn = Owner && Owner->IsA<APawn>();
	Traits.bTerrain = OccluderClassNameContains(Owner, TEXT("Landscape")) || OccluderClassNameContains(&Piece, TEXT("Landscape"));
	Traits.bFoliage = OccluderClassNameContains(Owner, TEXT("InstancedFoliageActor")) || OccluderClassNameContains(&Piece, TEXT("Foliage"));
	Traits.bStaticMesh = Piece.IsA<UStaticMeshComponent>();
	Traits.bInstanced = Piece.IsA<UInstancedStaticMeshComponent>();
	// Not drawn in the main pass already (a roof the cutaway took away): left alone, its owner restores it.
	Traits.bVisible = Piece.IsVisible() && Piece.bRenderInMainPass && !(Owner && Owner->IsHidden());
	Traits.bOptOut = Owner && Owner->ActorHasTag(OccluderFadeRules::OptOutTag());
	Traits.bInteractable = Owner && Owner->IsA<AInteractableActor>();
	return Traits;
}

void UOccluderFadeSubsystem::CollectOccluders(const FVector& CameraLocation, const FVector& Target, TSet<UPrimitiveComponent*>& OutPieces) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const float TargetDistance = static_cast<float>(FVector::Dist(CameraLocation, Target));
	// The target is a body centre: its feet ~90 cm lower (a crouched man's centre is lower still - the rise rule has the margin).
	const float TargetFeetZ = static_cast<float>(Target.Z) - 90.f;
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(OccluderFadeTrace), false);
	// Two rays: to the body centre and to just above the feet (a wall may hide the feet while the head shows over it).
	for (const FVector& Aim : { Target, FVector(Target.X, Target.Y, TargetFeetZ + 30.f) })
	{
		TArray<FHitResult> Hits;
		World->LineTraceMultiByObjectType(Hits, CameraLocation, Aim, Objects, Params);
		int32 Seen = 0;
		for (const FHitResult& Hit : Hits)
		{
			UPrimitiveComponent* Piece = Hit.GetComponent();
			if (!Piece || ++Seen > OccluderFadeLocal::OccluderMaxTraceHits)
			{
				continue;
			}
			if (Piece->GetCollisionResponseToChannel(ECC_Visibility) != ECR_Block && Piece->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Block)
			{
				continue; // a trigger volume: nothing to see
			}
			const bool bOurs = FadedPieces.ContainsByPredicate([Piece](const FFaded& Each) { return Each.Piece.Get() == Piece; });
			if (!bOurs && !OccluderFadeRules::IsFadeableKind(GetTraits(*Piece)))
			{
				continue;
			}
			const FBox Bounds = Piece->Bounds.GetBox();
			const FVector Size = Bounds.GetSize();
			if (OccluderFadeRules::HidesTarget(Hit.Distance, FVector::Dist(CameraLocation, Aim) > 0.0 ? static_cast<float>(FVector::Dist(CameraLocation, Aim)) : TargetDistance,
				static_cast<float>(Bounds.Max.Z), TargetFeetZ, static_cast<float>(FMath::Min(Size.X, Size.Y)), Config))
			{
				OutPieces.Add(Piece);
			}
		}
	}
}

void UOccluderFadeSubsystem::UpdateNow()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double Now = World->GetRealTimeSeconds();
	TSet<UPrimitiveComponent*> Hiding;
	const APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
	if (PC && PC->PlayerCameraManager && !Targets.IsEmpty())
	{
		const FVector Camera = PC->PlayerCameraManager->GetCameraLocation();
		for (const FVector& Target : Targets)
		{
			CollectOccluders(Camera, Target, Hiding);
		}
	}
	for (UPrimitiveComponent* Piece : Hiding)
	{
		if (FFaded* Existing = FadedPieces.FindByPredicate([Piece](const FFaded& Each) { return Each.Piece.Get() == Piece; }))
		{
			Existing->LastHidSeconds = Now;
			if (Existing->bFadingOut)
			{
				// Back to faded from where the fade out is now (no pop).
				const float Done = FMath::Clamp(static_cast<float>(Now - Existing->StateSeconds) / FMath::Max(Config.FadeSeconds, 0.01f), 0.f, 1.f);
				Existing->bFadingOut = false;
				Existing->StateSeconds = Now - (1.f - Done) * Config.FadeSeconds;
			}
		}
		else if (GetFadedCount() < Config.MaxFaded)
		{
			Fade(*Piece, Now);
		}
	}
	for (FFaded& Each : FadedPieces)
	{
		if (!Each.bFadingOut && !Hiding.Contains(Each.Piece.Get()) && OccluderFadeRules::ShouldRestore(Each.LastHidSeconds, Now, Config))
		{
			Each.bFadingOut = true;
			Each.StateSeconds = Now;
		}
	}
}

void UOccluderFadeSubsystem::Animate(double Now)
{
	for (int32 Index = FadedPieces.Num() - 1; Index >= 0; --Index)
	{
		FFaded& Each = FadedPieces[Index];
		if (!Each.Piece.IsValid())
		{
			Restore(Each);
			FadedPieces.RemoveAtSwap(Index);
			continue;
		}
		const float Elapsed = static_cast<float>(Now - Each.StateSeconds);
		ApplyOpacity(Each, OccluderFadeRules::Opacity(Elapsed, !Each.bFadingOut, Config));
		if (Each.bFadingOut && Elapsed >= Config.FadeSeconds)
		{
			Restore(Each);
			FadedPieces.RemoveAtSwap(Index);
		}
	}
}

void UOccluderFadeSubsystem::Fade(UPrimitiveComponent& Piece, double Now)
{
	if (!bMaterialLoaded)
	{
		bMaterialLoaded = true;
		FadeMaterial = LoadObject<UMaterialInterface>(nullptr, Config.MaterialPath);
		if (!FadeMaterial.IsValid())
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[OccluderFade] %s not found: occluders are hidden instead of faded (run Scripts/Editor/create_occluder_fade_material.py)"),
				Config.MaterialPath);
		}
	}
	FFaded& Faded = FadedPieces.AddDefaulted_GetRef();
	Faded.Piece = &Piece;
	Faded.bRenderInMainPass = Piece.bRenderInMainPass;
	Faded.bRenderInDepthPass = Piece.bRenderInDepthPass;
	Faded.LastHidSeconds = Now;
	Faded.StateSeconds = Now;
	++FadeCount;
	// The proxy: the same mesh at the same place with the fade material, no collision, no shadow (the original keeps casting it).
	const UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(&Piece);
	AActor* Owner = Piece.GetOwner();
	if (Mesh && Owner && FadeMaterial.IsValid() && Mesh->GetStaticMesh())
	{
		UStaticMeshComponent* Proxy = NewObject<UStaticMeshComponent>(Owner, NAME_None, RF_Transient);
		Proxy->SetMobility(EComponentMobility::Movable);
		Proxy->SetStaticMesh(Mesh->GetStaticMesh());
		Proxy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Proxy->SetCastShadow(false);
		Proxy->SetCanEverAffectNavigation(false);
		Proxy->SetWorldTransform(Mesh->GetComponentTransform());
		Proxy->RegisterComponent();
		for (int32 Slot = 0; Slot < FMath::Max(1, Mesh->GetNumMaterials()); ++Slot)
		{
			UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(FadeMaterial.Get(), Proxy);
			Proxy->SetMaterial(Slot, Instance);
			Faded.Materials.Add(Instance);
		}
		Faded.Proxy = Proxy;
	}
	// Not drawn (main + depth pass), still casting its shadow; its collision stays (the cursor still lands on the wall: the side rules decide).
	Piece.SetRenderInMainPass(false);
	Piece.SetRenderInDepthPass(false);
	ApplyOpacity(Faded, 1.f);
	UE_LOG(LogCodexTactics, Display, TEXT("[OccluderFade] fade %s.%s%s"), Owner ? *Owner->GetName() : TEXT("-"), *Piece.GetName(),
		Faded.Proxy.IsValid() ? TEXT("") : TEXT(" (hidden: no fade material)"));
}

void UOccluderFadeSubsystem::ApplyOpacity(FFaded& Faded, float Opacity)
{
	for (const TWeakObjectPtr<UMaterialInstanceDynamic>& Instance : Faded.Materials)
	{
		if (UMaterialInstanceDynamic* Live = Instance.Get())
		{
			Live->SetScalarParameterValue(OccluderFadeLocal::OpacityParameter, Opacity);
		}
	}
}

void UOccluderFadeSubsystem::Restore(FFaded& Faded)
{
	if (UStaticMeshComponent* Proxy = Faded.Proxy.Get())
	{
		Proxy->DestroyComponent();
	}
	Faded.Proxy.Reset();
	Faded.Materials.Reset();
	if (UPrimitiveComponent* Piece = Faded.Piece.Get(); IsValid(Piece))
	{
		Piece->SetRenderInMainPass(Faded.bRenderInMainPass);
		Piece->SetRenderInDepthPass(Faded.bRenderInDepthPass);
		UE_LOG(LogCodexTactics, Display, TEXT("[OccluderFade] restore %s.%s"), Piece->GetOwner() ? *Piece->GetOwner()->GetName() : TEXT("-"), *Piece->GetName());
	}
}

void UOccluderFadeSubsystem::RestoreAll()
{
	for (FFaded& Each : FadedPieces)
	{
		Restore(Each);
	}
	FadedPieces.Reset();
}
