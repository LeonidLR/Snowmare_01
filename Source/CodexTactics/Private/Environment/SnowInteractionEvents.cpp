// USnowInteractionSubsystem, the event stamps (interactive snow phase 3, docs/systems/environment-weather.md §14, docs/design/INTERACTIVE_SNOW.md
// §5): explosion craters (GameplayEvents::Explosion on the event bus) and bullet dents (UCombatFeedbackSubsystem::OnTracerSpawned). The gameplay
// only broadcasts; nothing here feeds back into it (read-only traces, stamps into the RT). UE-only, no Godot reference.

#include "Environment/SnowInteractionSubsystem.h"

#include "CodexTactics.h"
#include "CollisionQueryParams.h"
#include "Combat/CombatFeedbackSubsystem.h"
#include "Engine/World.h"
#include "Environment/SnowInteractionRules.h"
#include "SnowInteractionStats.h"

namespace SnowInteractionEvents
{
	/** A dent trace starts this far before the tracer's end (a tracer ending just inside the snow still finds it). */
	constexpr float ShotBackCm = 30.f;
	/** The explosion's ground trace starts this far above the blast centre. */
	constexpr float CraterTraceUpCm = 100.f;

	/** Floors / walls / props: what a crater or a bullet can land on (pawns never: a hit is not a dent). */
	FCollisionObjectQueryParams GroundObjects()
	{
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		return Objects;
	}
}

void USnowInteractionSubsystem::BindEventSources(UWorld& InWorld)
{
	if (UCombatFeedbackSubsystem* Feedback = InWorld.GetSubsystem<UCombatFeedbackSubsystem>())
	{
		TracerHandle = Feedback->OnTracerSpawned.AddUObject(this, &USnowInteractionSubsystem::HandleTracerSpawned);
		TracerSource = Feedback;
	}
}

void USnowInteractionSubsystem::UnbindEventSources()
{
	if (UCombatFeedbackSubsystem* Feedback = TracerSource.Get())
	{
		Feedback->OnTracerSpawned.Remove(TracerHandle);
	}
	TracerSource.Reset();
	TracerHandle.Reset();
}

bool USnowInteractionSubsystem::HandleExplosion(FName Kind, const FVector& Location, float RadiusCm, const AActor* Source)
{
	UWorld* World = GetWorld();
	if (!bActive || !World || Location.IsNearlyZero())
	{
		return false;
	}
	++Metrics.Explosions;
	// The snow under the blast: one trace down (a blast high above the ground, on a roof or indoors leaves no crater).
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SnowInteractionCrater), false, Source);
	const FVector From = Location + FVector(0., 0., SnowInteractionEvents::CraterTraceUpCm);
	const FVector To = Location - FVector(0., 0., FMath::Max(Tuning.CraterMaxDropCm, 1.f));
	if (!World->LineTraceSingleByObjectType(Hit, From, To, SnowInteractionEvents::GroundObjects(), Params) || !IsSnowGroundHit(Hit))
	{
		UE_LOG(LogCodexTactics, Verbose, TEXT("SnowInteraction: explosion %s at %s: no snow ground below"), *Kind.ToString(), *Location.ToCompactString());
		return false;
	}
	const FSnowCraterParams Crater = SnowInteractionRules::ComputeCrater(Kind, RadiusCm, Stamps, Tuning);
	SnowInteractionRules::MakeCraterStamps(Crater, Stamps, FVector2D(Hit.ImpactPoint.X, Hit.ImpactPoint.Y), PendingStamps);
	++Metrics.Craters;
	return true;
}

void USnowInteractionSubsystem::HandleTracerSpawned(const FVector& Start, const FVector& End, bool bHit)
{
	if (!bActive || bHit)
	{
		return;
	}
	FSnowShot Shot;
	Shot.Start = Start;
	Shot.End = End;
	Metrics.ShotsDropped += SnowInteractionRules::EnqueueShot(PendingShots, Shot, Tuning.ShotQueueMax);
	++Metrics.ShotsQueued;
}

void USnowInteractionSubsystem::ProcessShots()
{
	UWorld* World = GetWorld();
	const int32 Budget = SnowInteractionRules::GetShotBudget(PendingShots.Num(), Tuning.ShotTracesPerFrame);
	if (!World || Budget <= 0)
	{
		return;
	}
	SCOPE_CYCLE_COUNTER(STAT_SnowInteraction_Poll);
	const FCollisionObjectQueryParams Objects = SnowInteractionEvents::GroundObjects();
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(SnowInteractionShot), false);
	const FCollisionShape HitProbe = FCollisionShape::MakeSphere(FMath::Max(Tuning.ShotHitRadiusCm, 1.f));
	int32 Traced = 0;
	for (int32 Index = 0; Index < Budget; ++Index)
	{
		const FSnowShot& Shot = PendingShots[Index];
		FVector From;
		FVector To;
		if (!SnowInteractionRules::ComputeShotRay(Shot.Start, Shot.End, Tuning.ShotExtendCm, SnowInteractionEvents::ShotBackCm, From, To))
		{
			continue;
		}
		// The tracer ends on a body = it hit (SpawnTracer does not know): no dent.
		if (Tuning.ShotHitRadiusCm > 0.f
			&& World->OverlapAnyTestByObjectType(Shot.End, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn), HitProbe, Params))
		{
			++Metrics.ShotsOnTarget;
			continue;
		}
		++Traced;
		FHitResult Hit;
		if (World->LineTraceSingleByObjectType(Hit, From, To, Objects, Params) && IsSnowGroundHit(Hit))
		{
			AddStamp(SnowInteractionRules::MakeDent(Stamps.Dent, Hit.ImpactPoint, To - From, Hit.ImpactNormal, Tuning.DentMaxStretch));
			++Metrics.Dents;
		}
	}
	PendingShots.RemoveAt(0, Budget, EAllowShrinking::No);
	Metrics.ShotTraces += Traced;
	Metrics.MaxShotTracesInFrame = FMath::Max(Metrics.MaxShotTracesInFrame, Traced);
}
