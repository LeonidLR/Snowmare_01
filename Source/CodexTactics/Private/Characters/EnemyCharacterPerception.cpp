// AEnemyCharacter: perception params, patrol sight and hearing (suspicion meter), the vision cone, squad noise
// (gunshots / explosions).
// Split out of EnemyCharacter.cpp verbatim (cleanup 2026-10-19; map: docs/port/REFACTOR_ENEMY_2026-10.md).
// UE-only (Sprint 11 stealth; no Godot reference). Pure rules: AI/PerceptionRules, Combat/VisionConeRules.

#include "Characters/EnemyCharacter.h"
#include "Vehicles/VehicleSubsystem.h"
#include "AI/WorldAIPauseSubsystem.h"
#include "Data/EnemyPerception.h"
#include "UI/FloatingTextSubsystem.h"
#include "EngineUtils.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Engine/World.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Combat/EnemyGhostActor.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "Combat/VisionConeComponent.h"
#include "Combat/VisionConeRules.h"
#include "Combat/SightRules.h"

FEnemyPerceptionParams AEnemyCharacter::GetPerception() const
{
	return bOverridePerception ? PerceptionRules::Sanitize(Archetype, PerceptionOverride) : EnemyPerception::Get(Archetype);
}

FEnemyPerceptionParams AEnemyCharacter::GetPatrolPerception() const
{
	const FEnemyPerceptionParams Params = GetPerception();
	return bSearching ? PerceptionRules::Scaled(Params, EnemyPerception::GetSearch().PerceptionMultiplier) : Params;
}

AActor* AEnemyCharacter::FindVisibleOperative(const FEnemyPerceptionParams& Params, float& OutDistance, float& OutRange) const
{
	OutDistance = 0.f;
	OutRange = 0.f;
	UWorld* World = GetWorld();
	const USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
	if (!Squad)
	{
		return nullptr;
	}
	// The Sprint 08 trace (UTacticalSightSubsystem::HasClearSight) with its own pawn list: the subsystem rebuilds its
	// ignore list only during a wave fight, a patrol looks out before it. Pawns never block the view.
	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(PatrolSight), false, this);
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		TraceParams.AddIgnoredActor(*It);
	}
	for (TActorIterator<AEnemyGhostActor> It(World); It; ++It)
	{
		TraceParams.AddIgnoredActor(*It); // stasis silhouettes (wave fight) are no obstacles either
	}
	const FVector Eye = UTacticalSightSubsystem::EyePoint(*this);
	const FVector Forward = GetActorForwardVector();
	AActor* Best = nullptr;
	float BestDistance = TNumericLimits<float>::Max();
	for (AOperativeCharacter* Member : Squad->GetMembers())
	{
		if (!Member || !Member->HealthComponent || !Member->HealthComponent->IsAlive() || VehicleQueries::IsMounted(Member))
		{
			continue;
		}
		const float Distance = FVector::Dist(GetActorLocation(), Member->GetActorLocation());
		// Range by stance and the field of view first (cheap), the trace only for candidates. Sprint 12: a man behind a
		// full wall (head down) is unseen from the wall's side.
		if (Distance >= BestDistance || Member->IsHiddenInCoverFrom(GetActorLocation())
			|| !PerceptionRules::CanSee(Params, GetActorLocation(), Forward, Member->GetActorLocation(), Member->GetStance(), /*bLineClear*/ true))
		{
			continue;
		}
		// Sprint 08 heights: a prone operative behind a 60 cm barricade stays hidden.
		FHitResult Hit;
		const bool bBlocked = World->LineTraceSingleByChannel(Hit, Eye, UTacticalSightSubsystem::ProfilePoint(*Member), ECC_Visibility, TraceParams)
			&& Cast<APawn>(Hit.GetActor()) == nullptr;
		if (!bBlocked)
		{
			BestDistance = Distance;
			Best = Member;
			OutDistance = Distance;
			OutRange = PerceptionRules::EffectiveSightRange(Params, Member->GetStance());
		}
	}
	return Best;
}

int32 AEnemyCharacter::CountHearingWalls(const AActor& Target) const
{
	return CountHearingWallsTo(Target.GetActorLocation());
}

int32 AEnemyCharacter::CountOccludersBetween(const FVector& From, const FVector& To, int32 Max) const
{
	UWorld* World = GetWorld();
	if (!World || Max <= 0)
	{
		return 0;
	}
	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(PatrolHearing), false, this);
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		TraceParams.AddIgnoredActor(*It); // bodies do not muffle
	}
	for (TActorIterator<AEnemyGhostActor> It(World); It; ++It)
	{
		TraceParams.AddIgnoredActor(*It);
	}
	// Every blocking surface counts once (its component is skipped after).
	int32 Walls = 0;
	FHitResult Hit;
	while (Walls < Max && World->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, TraceParams))
	{
		++Walls;
		if (!Hit.GetComponent())
		{
			break;
		}
		TraceParams.AddIgnoredComponent(Hit.GetComponent());
	}
	return Walls;
}

int32 AEnemyCharacter::CountHearingWallsTo(const FVector& Point) const
{
	// From the ear to the middle of his body.
	const FVector Ear = GetActorLocation() + FVector(0.f, 0.f, GetSimpleCollisionHalfHeight() * 0.5f);
	return CountOccludersBetween(Ear, Point, PerceptionRules::MaxHearingOccluders);
}

bool AEnemyCharacter::TickPatrolPerception(float DeltaTime)
{
	// Checked every 0.2 s, like the sight system.
	PatrolSightTimer -= DeltaTime;
	if (PatrolSightTimer > 0.f)
	{
		return false;
	}
	const float Step = UTacticalSightSubsystem::UpdateInterval;
	PatrolSightTimer = Step;
	const FEnemyPerceptionParams Params = GetPatrolPerception();

	// Sight: the suspicion meter fills while it sees someone (faster up close), detection at 1.
	float SeenDistance = 0.f;
	float SeenRange = 0.f;
	const AActor* Seen = FindVisibleOperative(Params, SeenDistance, SeenRange);
	const float Before = PatrolSuspicion;
	PatrolSuspicion = PerceptionRules::StepSuspicion(Params, PatrolSuspicion, Step, Seen != nullptr, SeenDistance, SeenRange);
	if (Seen && Before <= 0.f && PatrolSuspicion < 1.f && !bIsDying)
	{
		UFloatingTextSubsystem::SpawnAboveEnemy(this, TEXT("?"), FLinearColor(1.f, 0.85f, 0.3f));
	}
	FPatrolAlertInput Input;
	Input.bSeesOperative = Seen != nullptr && PatrolSuspicion >= 1.f;

	// Hearing (footsteps by gait): no line of sight needed. Smell (hounds) after it: ScentRules, a scent alerts first.
	const AActor* Heard = nullptr;
	const bool bCanSmell = PerceptionRules::CanSmell(Archetype) && Params.SmellRadiusCm > 0.f;
	if (!Input.bSeesOperative)
	{
		const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
		for (AOperativeCharacter* Member : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
		{
			if (!Member || !Member->HealthComponent || !Member->HealthComponent->IsAlive() || VehicleQueries::IsMounted(Member))
			{
				continue;
			}
			const float Distance = FVector::Dist(GetActorLocation(), Member->GetActorLocation());
			const ESquadMovementNoise Noise = PerceptionRules::ClassifyMovement(Member->GetStance(), Member->GetVelocity().Size2D(), Member->IsSprinting());
			// Walls between muffle the footsteps (x HearingOcclusionPerWall each; user report 2026-10-06): traced only when
			// the open-air radius reaches him.
			if (!Heard && PerceptionRules::HearsMovement(Params, Noise, Distance)
				&& PerceptionRules::HearsMovementThroughWalls(Params, Noise, Distance, CountHearingWalls(*Member)))
			{
				Heard = Member;
			}
		}
	}
	Input.bHearsOperative = Heard != nullptr;
	// Smell (game designer 2026-10-18): walls cut the scent, a scent only alerts (sniff -> follow); the nose detects close in open air.
	if (!Input.bSeesOperative && !Input.bHearsOperative && bCanSmell && !bIsDying && TickScentPerception(Params, Step))
	{
		return true;
	}
	if (!PatrolRouteRules::ShouldBreakPatrol(Input))
	{
		return false;
	}
	const AActor* Found = Input.bSeesOperative ? Seen : Heard;
	BreakPatrol(Input.bSeesOperative ? EPatrolAlertCause::Sight : EPatrolAlertCause::Hearing, Found ? Found->GetActorLocation() : GetActorLocation());
	return true;
}

float AEnemyCharacter::GetVisionConeRange() const
{
	return PerceptionRules::EffectiveSightRange(GetPatrolPerception(), EOperativeStance::Standing);
}

float AEnemyCharacter::GetVisionConeHalfAngle() const
{
	return GetPatrolPerception().SightHalfAngleDeg;
}

void AEnemyCharacter::UpdateVisionCone(float DeltaTime)
{
	if (!VisionCone)
	{
		return;
	}
	const bool bAlive = !bIsDying && HealthComponent && HealthComponent->IsAlive();
	const bool bOnPatrol = IsOnPatrol();
	const bool bVisible = VisionConeRules::ResolveVisible(UVisionConeComponent::GetGlobalMode(), bShowVisionCone, bAlive,
		UVisionConeComponent::IsPlayerEnemyConesOn()) && (bOnPatrol || bShowVisionConeWhenEngaged);
	if (!bVisible && !VisionCone->IsConeShown())
	{
		return;
	}
	const EVisionConeState State = VisionConeRules::GetEnemyState(bOnPatrol, bSearching || IsOnScent(), PatrolSuspicion);
	// Occluded cone (game designer 2026-10-19): the sight lines of FindVisibleOperative from its eyes to a standing (full
	// tone + light tone) and a crouched operative's profile (full tone only) at their perception ranges.
	const FEnemyPerceptionParams Params = GetPatrolPerception();
	FVisionConeSightSpec Sight;
	Sight.EyeHeightCm = static_cast<float>(UTacticalSightSubsystem::EyePoint(*this).Z - (GetActorLocation().Z - GetSimpleCollisionHalfHeight()));
	Sight.Levels.Add({ SightRules::ProfileHeight(EOperativeStance::Standing), PerceptionRules::EffectiveSightRange(Params, EOperativeStance::Standing) });
	Sight.Levels.Add({ SightRules::ProfileHeight(EOperativeStance::Crouching), PerceptionRules::EffectiveSightRange(Params, EOperativeStance::Crouching) });
	VisionCone->UpdateCone(bVisible, GetVisionConeRange(), GetVisionConeHalfAngle(),
		VisionConeRules::GetStateColor(State, bSearching ? 1.f : PatrolSuspicion), DeltaTime, &Sight);
}

void AEnemyCharacter::NotifySquadNoise(UWorld* World, const FVector& Location, ESquadNoise Noise)
{
	if (!World || UWorldAIPauseSubsystem::IsPausedIn(World))
	{
		return;
	}
	TArray<AEnemyCharacter*> Listeners;
	for (AEnemyCharacter* Patrol : UEnemyRegistrySubsystem::GetEnemyCharacters(World))
	{
		if (Patrol->IsOnPatrol() && !Patrol->IsDying())
		{
			Listeners.Add(Patrol);
		}
	}
	for (AEnemyCharacter* Enemy : Listeners)
	{
		if (!IsValid(Enemy) || !Enemy->IsOnPatrol())
		{
			continue; // broke off with a partner already
		}
		const FEnemyPerceptionParams Params = Enemy->GetPatrolPerception();
		const float Distance = FVector::Dist(Enemy->GetActorLocation(), Location);
		FPatrolAlertInput Input;
		Input.bHearsOperative = Noise == ESquadNoise::Gunshot ? PerceptionRules::HearsGunshot(Params, Distance)
			: PerceptionRules::HearsExplosion(Params, Distance);
		if (PatrolRouteRules::ShouldBreakPatrol(Input))
		{
			Enemy->BreakPatrol(EPatrolAlertCause::Hearing, Location);
		}
	}
}
