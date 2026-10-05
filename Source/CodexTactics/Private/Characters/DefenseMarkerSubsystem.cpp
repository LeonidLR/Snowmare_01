#include "Characters/DefenseMarkerSubsystem.h"

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Components/MeshComponent.h"
#include "Engine/World.h"
#include "Interactables/RadiusRingSubsystem.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"

namespace DefenseMarker
{
	const FLinearColor Green(0.15f, 1.f, 0.4f);
	/** The ring glows additively: a deeper green so it reads green, not white. */
	const FLinearColor RingGreen(0.05f, 1.f, 0.25f);
	/** The ring grows from this share of the radius to the full radius while it fades in. */
	constexpr float RingStartScale = 0.8f;

	float EaseOut(float T)
	{
		return 1.f - FMath::Square(1.f - FMath::Clamp(T, 0.f, 1.f));
	}
}

TStatId UDefenseMarkerSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDefenseMarkerSubsystem, STATGROUP_Tickables);
}

void UDefenseMarkerSubsystem::Deinitialize()
{
	for (FMarker& Marker : Markers)
	{
		RemoveMarker(Marker);
	}
	Markers.Reset();
	Super::Deinitialize();
}

UDefenseMarkerSubsystem::FMarker& UDefenseMarkerSubsystem::FindOrAddMarker(AActor* Actor, const FVector& Location)
{
	for (FMarker& Marker : Markers)
	{
		if (Actor ? Marker.Actor.Get() == Actor : (!Marker.Actor.IsValid() && FVector::Dist2D(Marker.Ground, Location) < 100.f))
		{
			return Marker;
		}
	}
	FMarker& Marker = Markers.AddDefaulted_GetRef();
	Marker.Actor = Actor;
	if (Actor)
	{
		FVector Origin;
		FVector Extent;
		Actor->GetActorBounds(true, Origin, Extent);
		Marker.Ground = FVector(Origin.X, Origin.Y, Origin.Z - Extent.Z);
		Marker.TopZ = Origin.Z + Extent.Z;
		ApplyOverlay(Marker);
	}
	else
	{
		Marker.Ground = Location;
		Marker.TopZ = Location.Z + 120.f;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Marker.Ring = GetWorld()->SpawnActor<ARadiusRingActor>(Marker.Ground, FRotator::ZeroRotator, Params);
	if (ARadiusRingActor* Ring = Marker.Ring.Get())
	{
		Ring->SetIntensity(0.6f); // a soft green band, not a white glow
	}
	return Marker;
}

void UDefenseMarkerSubsystem::ApplyOverlay(FMarker& Marker)
{
	AActor* Actor = Marker.Actor.Get();
	UMaterialInterface* Fresnel = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/VFX/Materials/M_TargetFresnel.M_TargetFresnel"));
	if (!Actor || !Fresnel)
	{
		return;
	}
	UMaterialInstanceDynamic* Overlay = UMaterialInstanceDynamic::Create(Fresnel, this);
	FLinearColor Color = DefenseMarker::Green;
	Color.A = 0.6f;
	Overlay->SetVectorParameterValue(TEXT("Color"), Color);
	Overlay->SetScalarParameterValue(TEXT("Intensity"), 0.f);
	OverlayMaterials.Add(Overlay);
	Marker.Overlay = Overlay;
	TArray<UMeshComponent*> Meshes;
	Actor->GetComponents(Meshes);
	for (UMeshComponent* Mesh : Meshes)
	{
		if (!Mesh->IsVisible())
		{
			continue;
		}
		Marker.Meshes.Add(Mesh);
		Marker.PreviousOverlays.Add(Mesh->GetOverlayMaterial());
		Mesh->SetOverlayMaterial(Overlay);
	}
}

void UDefenseMarkerSubsystem::RemoveMarker(FMarker& Marker)
{
	for (int32 Index = 0; Index < Marker.Meshes.Num(); ++Index)
	{
		UMeshComponent* Mesh = Marker.Meshes[Index].Get();
		// Only our own overlay is taken off (a hit flash running on it keeps its own restore).
		if (Mesh && Mesh->GetOverlayMaterial() == Marker.Overlay.Get())
		{
			Mesh->SetOverlayMaterial(Marker.PreviousOverlays[Index].Get());
		}
	}
	OverlayMaterials.Remove(Marker.Overlay.Get());
	if (ARadiusRingActor* Ring = Marker.Ring.Get())
	{
		Ring->Destroy();
	}
}

void UDefenseMarkerSubsystem::Tick(float DeltaTime)
{
	// Real time: the tactical pause slows the world 50x, the fades must not crawl.
	const float RealDelta = FMath::Clamp(static_cast<float>(FApp::GetDeltaTime()), 0.f, 0.1f);
	Clock += RealDelta;
	for (FMarker& Marker : Markers)
	{
		Marker.bWanted = false;
		Marker.Defenders = 0;
	}
	if (const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>())
	{
		for (const AOperativeCharacter* Member : Squad->GetMembers())
		{
			const FDefenseDirective& Defense = Member->TacticalAnchor.Defense;
			if (!Defense.IsActive() || !Member->HealthComponent || !Member->HealthComponent->IsAlive())
			{
				continue;
			}
			AActor* Object = Defense.DefendedActor.Get();
			if (Defense.DefendedActor.IsStale())
			{
				continue; // the defended object is gone: its marker fades out
			}
			FMarker& Marker = FindOrAddMarker(Object, Defense.DefendedLocation);
			Marker.bWanted = true;
			Marker.RadiusCm = Defense.InterceptRadiusCm;
			++Marker.Defenders;
		}
	}
	for (int32 Index = Markers.Num() - 1; Index >= 0; --Index)
	{
		FMarker& Marker = Markers[Index];
		Marker.Alpha = Marker.bWanted ? FMath::Min(1.f, Marker.Alpha + RealDelta / FadeInSeconds) : FMath::Max(0.f, Marker.Alpha - RealDelta / FadeOutSeconds);
		if (!Marker.bWanted && Marker.Alpha <= 0.f)
		{
			RemoveMarker(Marker);
			Markers.RemoveAt(Index);
			continue;
		}
		const float Shown = DefenseMarker::EaseOut(Marker.Alpha);
		// A slow, soft breath once it is up.
		const float Breath = 0.85f + 0.15f * FMath::Sin(Clock * 2.2f + Index);
		if (ARadiusRingActor* Ring = Marker.Ring.Get())
		{
			const float Radius = Marker.RadiusCm * FMath::Lerp(DefenseMarker::RingStartScale, 1.f, Shown);
			Ring->ShowRing(Marker.Ground, Radius, DefenseMarker::RingGreen * (Shown * Breath), 18.f);
		}
		if (UMaterialInstanceDynamic* Overlay = Marker.Overlay.Get())
		{
			Overlay->SetScalarParameterValue(TEXT("Intensity"), 0.9f * Shown * Breath);
		}
	}
}

TArray<FDefenseMarkerView> UDefenseMarkerSubsystem::GetMarkers() const
{
	TArray<FDefenseMarkerView> Views;
	for (const FMarker& Marker : Markers)
	{
		FDefenseMarkerView& View = Views.AddDefaulted_GetRef();
		const AActor* Actor = Marker.Actor.Get();
		const FVector Base = Actor ? FVector(Actor->GetActorLocation().X, Actor->GetActorLocation().Y, Marker.TopZ) : FVector(Marker.Ground.X, Marker.Ground.Y, Marker.TopZ);
		View.IconLocation = Base + FVector(0.f, 0.f, 190.f + 8.f * FMath::Sin(Clock * 2.f)); // above the object's own label
		View.Alpha = DefenseMarker::EaseOut(Marker.Alpha);
		View.Defenders = Marker.Defenders;
	}
	return Views;
}

float UDefenseMarkerSubsystem::GetMarkerAlpha(const AActor* Actor) const
{
	for (const FMarker& Marker : Markers)
	{
		if (Marker.Actor.Get() == Actor)
		{
			return Marker.Alpha;
		}
	}
	return -1.f;
}
