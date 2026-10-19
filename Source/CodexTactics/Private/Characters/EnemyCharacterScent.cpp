// AEnemyCharacter: the hound scent - walls to the target, scent perception, sniff / follow behaviour.
// Split out of EnemyCharacter.cpp verbatim (cleanup 2026-10-19; map: docs/port/REFACTOR_ENEMY_2026-10.md).
// UE-only (hound scent 2026-10-18; no Godot reference). Pure rules: AI/ScentRules.

#include "Characters/EnemyCharacter.h"
#include "Vehicles/VehicleSubsystem.h"
#include "CodexTactics.h"
#include "UI/FloatingTextSubsystem.h"
#include "UI/GameMessageSubsystem.h"
#include "AIController.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Engine/World.h"

int32 AEnemyCharacter::CountScentWallsTo(const AActor& Target) const
{
	// From the nose (inside its own capsule, so never inside a wall) to his body, over a low obstacle and to either side of him:
	// the clearest trace counts, so an open doorway / a gap near the line lets the scent through (ScentRules::CombineTraceWalls).
	const FVector Body = Target.GetActorLocation();
	const FVector Nose = GetActorLocation() + GetActorForwardVector().GetSafeNormal2D() * GetSimpleCollisionRadius() * 0.5f;
	const FVector Side = FVector::CrossProduct(FVector::UpVector, (Body - Nose).GetSafeNormal2D()) * 50.f;
	const FVector Ends[] = { Body, Body + FVector(0.f, 0.f, 80.f), Body + Side, Body - Side };
	TArray<int32> Walls;
	for (const FVector& End : Ends)
	{
		const int32 Count = CountOccludersBetween(Nose, End, PerceptionRules::MaxHearingOccluders);
		Walls.Add(Count);
		if (Count == 0)
		{
			break; // open air: nothing clearer to find
		}
	}
	return ScentRules::CombineTraceWalls(Walls);
}

bool AEnemyCharacter::TickScentPerception(const FEnemyPerceptionParams& Params, float StepSeconds)
{
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	FHoundScentSense Sense;
	Sense.bSkipSniff = IsInvestigatingScent(); // already on the trail: no second sniff
	const AOperativeCharacter* Source = nullptr;
	float SourceDistance = TNumericLimits<float>::Max();
	int32 SourceWalls = 0;
	for (AOperativeCharacter* Member : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
	{
		if (!Member || !Member->HealthComponent || !Member->HealthComponent->IsAlive() || VehicleQueries::IsMounted(Member))
		{
			continue;
		}
		const float Distance = FVector::Dist(GetActorLocation(), Member->GetActorLocation());
		// The walls are traced only when the open-air radius reaches him (and he is closer than the best so far).
		if (Distance > Params.SmellRadiusCm || Distance >= SourceDistance)
		{
			continue;
		}
		const int32 Walls = CountScentWallsTo(*Member);
		if (!ScentRules::Smells(Params, Distance, Walls))
		{
			continue;
		}
		Source = Member;
		SourceDistance = Distance;
		SourceWalls = Walls;
		Sense.bSmells = true;
		Sense.Point = Member->GetActorLocation();
		Sense.bClose = ScentRules::IsCloseDetection(Params, Distance, Walls);
	}
	if (bScentHeldByNoise && !Sense.bClose)
	{
		Sense.bSmells = false; // a noise took it off a scent: on its way there only the close open-air nose detects
	}
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	switch (ScentRules::Step(Params, ScentState, Sense, StepSeconds))
	{
	case EHoundScentAction::StartSniff:
	{
		// Stops, nose up: the player sees the scent marker and gets the feed line - time to back off.
		if (AAIController* AIC = Cast<AAIController>(GetController()))
		{
			AIC->StopMovement();
		}
		bScentMoveIssued = false;
		ScentStuckTime = 0.f;
		UE_LOG(LogCodexTactics, Display, TEXT("[Stealth] scent caught by %s at %.1f s (%.0f cm, %d walls)"), *GetName(), Now, SourceDistance, SourceWalls);
		UFloatingTextSubsystem::SpawnAboveEnemy(this, TEXT("SNIFF..."), FLinearColor(0.65f, 1.f, 0.55f));
		if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
		{
			Messages->PostMessage(Source ? Source->DisplayName : FText::FromString(EnemyDisplayName), ScentRules::GetCaughtScentLine());
		}
		return false;
	}
	case EHoundScentAction::StartFollow:
		bScentMoveIssued = false;
		ScentStuckTime = 0.f;
		UE_LOG(LogCodexTactics, Display, TEXT("[Stealth] scent followed by %s at %.1f s towards %s"), *GetName(), Now, *ScentState.ScentPoint.ToCompactString());
		return false;
	case EHoundScentAction::Detect:
		UE_LOG(LogCodexTactics, Display, TEXT("[Stealth] scent close by %s at %.1f s (%.0f cm, open air)"), *GetName(), Now, SourceDistance);
		BreakPatrol(EPatrolAlertCause::Smell, Sense.Point);
		return true;
	case EHoundScentAction::LoseScent:
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Stealth] scent lost by %s at %.1f s, investigates %s"), *GetName(), Now, *ScentState.ScentPoint.ToCompactString());
		bScentMoveIssued = false;
		FPatrolSearchSpec Spec;
		Spec.bScent = true;
		Spec.DurationSeconds = Params.ScentSearchSeconds;
		Spec.SweepRadiusCm = Params.ScentSweepRadiusCm;
		StartPatrolSearchWith(ScentState.ScentPoint, Spec);
		return false;
	}
	default:
		return false;
	}
}

bool AEnemyCharacter::TickScentBehavior(float DeltaTime)
{
	if (!IsOnScent())
	{
		return false;
	}
	AAIController* AIC = Cast<AAIController>(GetController());
	if (ScentState.Phase == EHoundScentPhase::Sniffing)
	{
		// Stands and sniffs (no turn: it does not look for him yet).
		if (AIC && (bScentMoveIssued || GetVelocity().Size2D() > 10.f))
		{
			AIC->StopMovement();
		}
		bScentMoveIssued = false;
		return true;
	}
	// Following: a cautious walk to where it smells him (the point moves with him while it still smells him).
	const FVector Goal = ScentState.ScentPoint;
	if (FVector::Dist2D(GetActorLocation(), Goal) <= 120.f)
	{
		if (AIC && bScentMoveIssued)
		{
			AIC->StopMovement();
		}
		bScentMoveIssued = false;
		return true; // at the point: the nose decides (close in open air = detected; nothing = a lost scent)
	}
	ScentStuckTime = GetVelocity().Size2D() < 10.f ? ScentStuckTime + DeltaTime : 0.f;
	if (!bScentMoveIssued || FVector::Dist2D(Goal, ScentMoveGoal) > 100.f || ScentStuckTime > 1.f)
	{
		ScentStuckTime = 0.f;
		ScentMoveGoal = Goal;
		bScentMoveIssued = true;
		IssuePatrolMove(Goal, ScentRules::GetFollowSpeed(PatrolWalkSpeed, BaseWalkSpeed, GetPatrolPerception().ScentFollowSpeedFraction));
	}
	return true;
}

void AEnemyCharacter::ResetScent()
{
	bScentHeldByNoise = false;
	ScentState = FHoundScentState();
	bScentMoveIssued = false;
	ScentStuckTime = 0.f;
}
