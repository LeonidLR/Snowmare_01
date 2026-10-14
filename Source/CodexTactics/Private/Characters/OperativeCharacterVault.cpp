// AOperativeCharacter: Vaulting low obstacles and stepping off obstacle tops.
// Split out of OperativeCharacter.cpp verbatim (cleanup day 2026-10-14; map: docs/port/REFACTOR_OPERATIVE_2026-10.md).
// Godot reference: Scenes/movements/player.gd (try_vault_obstacle, is_vaulting). Pure rules: Characters/VaultRules.

#include "Characters/OperativeCharacter.h"

#include "CodexTactics.h"
#include "Characters/OperativeAIController.h"
#include "Characters/SquadSubsystem.h"
#include "Characters/VaultRules.h"
#include "Combat/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Interactables/VaultNavigation.h"
#include "Navigation/PathFollowingComponent.h"

float AOperativeCharacter::GetVaultSpeed() const
{
	return bVaulting ? FVector::Dist2D(VaultStart, VaultLanding) / FMath::Max(VaultDuration, 0.1f) : 0.f;
}

void AOperativeCharacter::UpdateVaultTrigger(float DeltaTime)
{
	VaultCooldown = FMath::Max(0.f, VaultCooldown - DeltaTime);
	const AOperativeAIController* AIController = Cast<AOperativeAIController>(GetController());
	const UPathFollowingComponent* PathFollowing = AIController ? AIController->GetPathFollowingComponent() : nullptr;
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	if (!PathFollowing || PathFollowing->GetStatus() != EPathFollowingStatus::Moving || Stance == EOperativeStance::Prone
		|| VaultCooldown > 0.f || (Flow && Flow->GetCombatMode() == ECodexCombatMode::TurnBased))
	{
		BlockedTimer = 0.f;
		return;
	}
	// Godot is_blocked: wanting to move but hardly moving.
	BlockedTimer = GetVelocity().Size2D() < 25.f ? BlockedTimer + DeltaTime : 0.f;
	const bool bBlocked = BlockedTimer > 0.2f;
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	const bool bLeader = !Squad || Squad->GetLeader() == this;
	// Followers climb only when their way is blocked (Godot follower_vault_only_when_blocked).
	if (!bLeader && !bBlocked)
	{
		return;
	}
	FVector Direction = PathFollowing->GetCurrentDirection();
	if (Direction.IsNearlyZero())
	{
		Direction = GetActorForwardVector();
	}
	TryVault(Direction, bBlocked);
}

bool AOperativeCharacter::TryVault(const FVector& InDirection, bool bForceWhenBlocked)
{
	UWorld* World = GetWorld();
	if (bVaulting || !World || !HealthComponent || !HealthComponent->IsAlive())
	{
		return false;
	}
	// Godot: no vaulting on the run — a running operative goes round, unless it has come to a stop.
	if (bSprinting && GetVelocity().Size2D() > 20.f && !bForceWhenBlocked)
	{
		return false;
	}
	const FVector Direction = FVector(InDirection.X, InDirection.Y, 0.f).GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(OperativeVault), false, this);
	for (TActorIterator<AOperativeCharacter> It(World); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}
	const FVector Location = GetActorLocation();
	const float HalfHeight = GetSimpleCollisionHalfHeight();
	float GroundZ = Location.Z - HalfHeight;
	FHitResult GroundHit;
	if (World->LineTraceSingleByChannel(GroundHit, Location, Location - FVector(0.f, 0.f, HalfHeight + 250.f), ECC_Visibility, Params))
	{
		GroundZ = GroundHit.ImpactPoint.Z;
	}
	// 1. Something within reach at knee height, marked vaultable.
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	const FVector ProbeStart(Location.X, Location.Y, GroundZ + VaultRules::ProbeHeight);
	FHitResult Front;
	if (!World->LineTraceSingleByObjectType(Front, ProbeStart, ProbeStart + Direction * VaultRules::ApproachDistance, Objects, Params)
		|| !VaultNavigation::IsVaultable(Front.GetActor()))
	{
		return false;
	}
	// 2. Its height.
	const FVector TopProbe = Front.ImpactPoint + Direction * 20.f;
	FHitResult Top;
	if (!World->LineTraceSingleByObjectType(Top, FVector(TopProbe.X, TopProbe.Y, GroundZ + 200.f), FVector(TopProbe.X, TopProbe.Y, GroundZ - 20.f),
		Objects, Params))
	{
		return false;
	}
	const float Height = Top.ImpactPoint.Z - GroundZ;
	// 3. Firm ground behind it, where the capsule fits: further out while the spot is still on / in the obstacle (user
	// report 2026-10-04: vaulting along a 3 m barricade landed inside it, he was pushed up and stuck on top).
	FVector LandingXY = Front.ImpactPoint + Direction * VaultRules::LandingDistance;
	FHitResult Land;
	bool bLanding = false;
	bool bLandingFree = false;
	FCollisionQueryParams FitParams(SCENE_QUERY_STAT(OperativeVaultLanding), false, this);
	for (float Extra = 0.f; Extra <= 240.f && !bLandingFree; Extra += 60.f)
	{
		LandingXY = Front.ImpactPoint + Direction * (VaultRules::LandingDistance + Extra);
		bLanding = World->LineTraceSingleByChannel(Land, FVector(LandingXY.X, LandingXY.Y, GroundZ + 200.f),
			FVector(LandingXY.X, LandingXY.Y, GroundZ - 200.f), ECC_Visibility, Params);
		if (!bLanding || VaultNavigation::IsVaultable(Land.GetActor()) || Land.GetActor() == Front.GetActor())
		{
			continue; // the trace stops on the obstacle itself: still above it
		}
		const FVector Centre(LandingXY.X, LandingXY.Y, Land.ImpactPoint.Z + HalfHeight + 2.f);
		bLandingFree = !World->OverlapAnyTestByChannel(Centre, FQuat::Identity, ECC_Pawn,
			FCollisionShape::MakeCapsule(GetCapsuleComponent()->GetScaledCapsuleRadius(), HalfHeight), FitParams);
	}
	if (!bLandingFree || !VaultRules::CanVault(Height, bLanding, bLanding ? Land.ImpactPoint.Z - GroundZ : 0.f))
	{
		return false;
	}

	// Go: remember where the walk was heading, fly the arc with the collision off.
	bVaultResume = false;
	if (const AOperativeAIController* AIController = Cast<AOperativeAIController>(GetController()))
	{
		if (const UPathFollowingComponent* PathFollowing = AIController->GetPathFollowingComponent(); PathFollowing && PathFollowing->GetPath().IsValid())
		{
			VaultResumeTarget = PathFollowing->GetPath()->GetDestinationLocation();
			bVaultResume = true;
		}
	}
	if (AController* OwnerController = GetController())
	{
		OwnerController->StopMovement();
	}
	const bool bRunning = bSprinting || GetVelocity().Size2D() > MovementConfig.WalkSpeed * 1.1f;
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->SetMovementMode(MOVE_Flying);
	SetActorEnableCollision(false);
	if (bInCover)
	{
		LeaveCover(TEXT("vault"));
	}
	bHasPendingCover = false;
	bVaulting = true;
	VaultTimer = 0.f;
	VaultDuration = VaultRules::Duration(bRunning);
	VaultHeight = FMath::Max(30.f, Height);
	VaultStart = Location;
	VaultLanding = FVector(LandingXY.X, LandingXY.Y, Land.ImpactPoint.Z + HalfHeight);
	SetActorRotation(FRotator(0.f, Direction.Rotation().Yaw, 0.f));
	UE_LOG(LogCodexTactics, Log, TEXT("%s vaults over %s (%.0f cm)"), *DisplayName.ToString(), *Front.GetActor()->GetName(), Height);
	return true;
}

bool AOperativeCharacter::UpdateObstacleStepOff(float DeltaTime)
{
	// Safety net: standing on an obstacle top for 0.3 s -> a short vault arc down to the nearest free ground.
	ObstacleTopTime = VaultNavigation::IsStandingOnObstacle(*this) ? ObstacleTopTime + DeltaTime : 0.f;
	FVector Spot;
	if (ObstacleTopTime < 0.3f || !VaultNavigation::FindStepOffSpot(*this, Spot))
	{
		return false;
	}
	ObstacleTopTime = 0.f;
	bVaultResume = bHasMoveOrder;
	VaultResumeTarget = LastMoveDestination;
	if (AController* OwnerController = GetController())
	{
		OwnerController->StopMovement();
	}
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->SetMovementMode(MOVE_Flying);
	SetActorEnableCollision(false);
	if (bInCover)
	{
		LeaveCover(TEXT("vault"));
	}
	bHasPendingCover = false;
	bVaulting = true;
	VaultTimer = 0.f;
	VaultDuration = 0.5f;
	VaultHeight = 30.f;
	VaultStart = GetActorLocation();
	VaultLanding = Spot;
	UE_LOG(LogCodexTactics, Display, TEXT("[Vault] %s stepped off an obstacle top"), *DisplayName.ToString());
	return true;
}

void AOperativeCharacter::UpdateVault(float DeltaTime)
{
	VaultTimer += DeltaTime;
	const float Alpha = FMath::Clamp(VaultTimer / FMath::Max(VaultDuration, 0.1f), 0.f, 1.f);
	SetActorLocation(VaultRules::Position(VaultStart, VaultLanding, VaultHeight, Alpha));
	if (Alpha < 1.f)
	{
		return;
	}
	bVaulting = false;
	VaultCooldown = VaultRules::Cooldown;
	SetActorLocation(VaultLanding);
	SetActorEnableCollision(true);
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	// Godot vault_resume_target: carry on unless the destination was the landing spot (or behind it).
	const FVector VaultDirection = (VaultLanding - VaultStart).GetSafeNormal2D();
	const FVector ToTarget = VaultResumeTarget - VaultLanding;
	if (bVaultResume && ToTarget.Size2D() > 60.f && FVector::DotProduct(VaultDirection, ToTarget.GetSafeNormal2D()) >= 0.1f)
	{
		ApplyMovementParams();
		RequestMove(VaultResumeTarget);
	}
	else
	{
		bHasMoveOrder = false;
	}
	bVaultResume = false;
}
