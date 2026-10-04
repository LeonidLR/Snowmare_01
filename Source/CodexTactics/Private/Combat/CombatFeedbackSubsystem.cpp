#include "Combat/CombatFeedbackSubsystem.h"
#include "Characters/EnemyCharacter.h"
#include "Combat/CombatFeedbackActor.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"

namespace
{
	/** Godot OmniLight3D light_energy -> point light intensity (unitless). */
	constexpr float FeedbackLightPerEnergy = 1500.f;
	/** Tracer beam thickness, cm (Godot draws 1 px lines). */
	constexpr float FeedbackTracerThickness = 3.f;
}

void UCombatFeedbackSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (UGameFlowSubsystem* Flow = InWorld.GetSubsystem<UGameFlowSubsystem>())
	{
		Flow->OnGameFlowChanged.AddDynamic(this, &UCombatFeedbackSubsystem::HandleGameFlowChanged);
	}
}

void UCombatFeedbackSubsystem::HandleGameFlowChanged(ECodexGamePhase Phase, ECodexCombatMode CombatMode)
{
	// Godot clears the markers when the pause starts and when it ends.
	ClearPlannedMarkers();
}

ACombatFeedbackActor* UCombatFeedbackSubsystem::SpawnFeedback(const FVector& Location) const
{
	UWorld* World = GetWorld();
	if (!World || World->bIsTearingDown)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<ACombatFeedbackActor>(Location, FRotator::ZeroRotator, Params);
}

void UCombatFeedbackSubsystem::SpawnTracer(const FVector& Muzzle, const FVector& End, const FLinearColor& Color, EDamageType DamageType)
{
	ACombatFeedbackActor* Tracer = SpawnFeedback(Muzzle);
	if (!Tracer)
	{
		return;
	}
	const float Glow = DamageType == EDamageType::Energy ? 8.f : (DamageType == EDamageType::Fire ? 6.f : 5.f);
	const float Fade = (DamageType == EDamageType::Fire || DamageType == EDamageType::Cryo) ? 0.2f : 0.1f;
	// Godot fades the albedo alpha; the additive glow fades its intensity, scaled by the colour alpha.
	Tracer->SetupBeam(Muzzle, End, FeedbackTracerThickness, Color, Glow * Color.A, Fade);
	Tracer->SetupLight(Color, 4.f * FeedbackLightPerEnergy, 400.f, 0.08f);
}

void UCombatFeedbackSubsystem::SpawnTurretTracer(const FVector& Muzzle, const FVector& End)
{
	ACombatFeedbackActor* Tracer = SpawnFeedback(Muzzle);
	if (!Tracer)
	{
		return;
	}
	const FLinearColor TurretColor(0.25f, 1.f, 0.45f);
	Tracer->SetupBeam(Muzzle, End, FeedbackTracerThickness, TurretColor, 8.f, 0.1f);
	Tracer->SetupLight(TurretColor, 3.f * FeedbackLightPerEnergy, 250.f, 0.05f);
}

void UCombatFeedbackSubsystem::SpawnWaypointMarker(const FVector& GroundLocation)
{
	const FVector Location = GroundLocation + FVector(0.f, 0.f, 5.f);
	if (ACombatFeedbackActor* Marker = SpawnFeedback(Location))
	{
		Marker->SetupDisc(Location, 35.f, 5.f, FLinearColor(0.2f, 0.9f, 1.f), 3.f * 0.75f, 0.f);
		PlannedMarkers.Add(Marker);
	}
}

void UCombatFeedbackSubsystem::SpawnMovePing(const FVector& GroundLocation, bool bClearPrevious, float Duration)
{
	if (bClearPrevious)
	{
		for (const TWeakObjectPtr<ACombatFeedbackActor>& Ping : MovePings)
		{
			if (Ping.IsValid())
			{
				Ping->Destroy();
			}
		}
		MovePings.Reset();
	}
	MovePings.RemoveAll([](const TWeakObjectPtr<ACombatFeedbackActor>& Ping) { return !Ping.IsValid(); });
	const FVector Location = GroundLocation + FVector(0.f, 0.f, 5.f);
	if (ACombatFeedbackActor* Ping = SpawnFeedback(Location))
	{
		Ping->SetupDisc(Location, 45.f, 4.f, FLinearColor(0.2f, 0.9f, 1.f), 3.f, Duration);
		MovePings.Add(Ping);
	}
}

int32 UCombatFeedbackSubsystem::GetMovePingCount() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<ACombatFeedbackActor>& Ping : MovePings)
	{
		Count += Ping.IsValid() ? 1 : 0;
	}
	return Count;
}

void UCombatFeedbackSubsystem::ClearPlannedMarkers()
{
	for (const TWeakObjectPtr<ACombatFeedbackActor>& Marker : PlannedMarkers)
	{
		if (Marker.IsValid())
		{
			Marker->Destroy();
		}
	}
	PlannedMarkers.Reset();
}

int32 UCombatFeedbackSubsystem::GetPlannedMarkerCount() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<ACombatFeedbackActor>& Marker : PlannedMarkers)
	{
		Count += Marker.IsValid() ? 1 : 0;
	}
	return Count;
}

void UCombatFeedbackSubsystem::SpawnDamageFlash(AActor* Operative)
{
	if (!IsValid(Operative))
	{
		return;
	}
	// Godot global_position is the operative's centre (1 m above the feet); UE's capsule centre is close to it.
	if (ACombatFeedbackActor* Flash = SpawnFeedback(Operative->GetActorLocation() + FVector(0.f, 0.f, 120.f)))
	{
		Flash->SetupLight(FLinearColor(1.f, 0.15f, 0.1f), 3.5f * FeedbackLightPerEnergy, 350.f, 0.12f);
		++DamageFlashCount;
	}
}

void UCombatFeedbackSubsystem::FlashEnemyHit(AActor* Enemy)
{
	if (!IsValid(Enemy))
	{
		return;
	}
	if (ACombatFeedbackActor* Flash = SpawnFeedback(Enemy->GetActorLocation()))
	{
		Flash->SetupOverlayFlash(Enemy, FLinearColor(1.f, 0.2f, 0.2f), 5.f, 0.08f);
		++DamageFlashCount;
	}
}

void UCombatFeedbackSubsystem::HighlightTarget(AActor* Target)
{
	if (!IsValid(Target))
	{
		return;
	}
	if (ACombatFeedbackActor* Flash = SpawnFeedback(Target->GetActorLocation() + FVector(0.f, 0.f, 120.f)))
	{
		Flash->SetupLight(FLinearColor(1.f, 0.25f, 0.2f), 6.f * FeedbackLightPerEnergy, 450.f, 0.45f);
		// Godot _apply_enemy_target_fresnel: an enemy gets the red see-through fresnel edge (flash_alpha 1 -> 0 in 0.4 s).
		UMaterialInterface* Fresnel = Target->IsA<AEnemyCharacter>()
			? LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/VFX/Materials/M_TargetFresnel.M_TargetFresnel")) : nullptr;
		if (Fresnel)
		{
			FLinearColor Red = FLinearColor::FromSRGBColor(FColor(255, 38, 26));
			Red.A = 0.95f;
			Flash->SetupOverlayFlash(Target, Red, 1.f, 0.4f, Fresnel);
		}
		else
		{
			Flash->SetupOverlayFlash(Target, FLinearColor(1.f, 0.25f, 0.15f), 4.f, 0.4f);
		}
	}
}
