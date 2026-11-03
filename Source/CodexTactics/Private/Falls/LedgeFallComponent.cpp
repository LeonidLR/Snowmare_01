// ULedgeFallComponent: running drops through the generated drop links, knockback falls over an edge and the landing's fall damage
// (game designer items 6 / 10 / 11a, 2026-11-03). UE-only system (no Godot reference).

#include "Falls/LedgeFallComponent.h"

#include "AIController.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/KnockdownComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Falls/DropLinkSubsystem.h"
#include "Falls/FallRules.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Ladders/LadderClimbComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "UI/FloatingTextSubsystem.h"
#include "Vehicles/VehicleSubsystem.h"

namespace LedgeFallInternal
{
	const TCHAR* const FallLoopPath = TEXT("/Game/Characters/Mannequins/Anims/Rifle/Jump/MM_Rifle_Jump_Fall_Loop.MM_Rifle_Jump_Fall_Loop");
	const TCHAR* const LandPath = TEXT("/Game/Characters/Mannequins/Anims/Rifle/Jump/MM_Rifle_Jump_Fall_Land.MM_Rifle_Jump_Fall_Land");

	const TCHAR* KindName(ELedgeFallKind Kind)
	{
		switch (Kind)
		{
		case ELedgeFallKind::DropLink: return TEXT("drop");
		case ELedgeFallKind::Knockback: return TEXT("knockback fall");
		case ELedgeFallKind::Other: return TEXT("fall");
		default: return TEXT("-");
		}
	}

	/** Every pawn ignored (the probes look at the level, never at a body standing near the edge). */
	void IgnorePawns(UWorld& World, FCollisionQueryParams& Params)
	{
		for (TActorIterator<APawn> It(&World); It; ++It)
		{
			Params.AddIgnoredActor(*It);
		}
	}
}

ULedgeFallComponent::ULedgeFallComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	FallLoopClip = TSoftObjectPtr<UAnimSequenceBase>(FSoftObjectPath(LedgeFallInternal::FallLoopPath));
	LandClip = TSoftObjectPtr<UAnimSequenceBase>(FSoftObjectPath(LedgeFallInternal::LandPath));
}

void ULedgeFallComponent::BeginPlay()
{
	Super::BeginPlay();
	if (ACharacter* Character = GetCharacter())
	{
		Character->MovementModeChangedDelegate.AddUniqueDynamic(this, &ULedgeFallComponent::HandleMovementModeChanged);
	}
}

void ULedgeFallComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ACharacter* Character = GetCharacter())
	{
		Character->MovementModeChangedDelegate.RemoveDynamic(this, &ULedgeFallComponent::HandleMovementModeChanged);
	}
	if (UDropLinkSubsystem* Links = GetLinks(); Links && ActiveLink != INDEX_NONE)
	{
		Links->ReleaseLease(ActiveLink, GetOwner(), 0.f);
	}
	Super::EndPlay(EndPlayReason);
}

ACharacter* ULedgeFallComponent::GetCharacter() const
{
	return Cast<ACharacter>(GetOwner());
}

float ULedgeFallComponent::GetHalfHeight() const
{
	const ACharacter* Character = GetCharacter();
	return Character && Character->GetCapsuleComponent() ? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 88.f;
}

float ULedgeFallComponent::FeetZ() const
{
	const AActor* Owner = GetOwner();
	return Owner ? static_cast<float>(Owner->GetActorLocation().Z) - GetHalfHeight() : 0.f;
}

float ULedgeFallComponent::Gravity() const
{
	const ACharacter* Character = GetCharacter();
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	return Movement ? FMath::Abs(Movement->GetGravityZ()) : 980.f;
}

UDropLinkSubsystem* ULedgeFallComponent::GetLinks() const
{
	return GetWorld() ? GetWorld()->GetSubsystem<UDropLinkSubsystem>() : nullptr;
}

float ULedgeFallComponent::GetTakeoffCap() const
{
	const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(GetOwner());
	const float Cap = Operative ? Operative->GetSprintSpeedCap() : FallRules::GetConfig().TakeoffMaxSpeedCms;
	return Cap * FMath::Max(0.f, TakeoffSpeedScale);
}

FallRules::FTakeoffPlan ULedgeFallComponent::PredictDrop(const FDropLink& Link) const
{
	const ACharacter* Character = GetCharacter();
	const float Along = Character ? FMath::Max(0.f, static_cast<float>(FVector::DotProduct(Character->GetActorLocation() - Link.Start, Link.Dir))) : 0.f;
	const float Speed = State == ELedgeFallState::None && Character ? static_cast<float>(Character->GetVelocity().Size2D()) : ApproachSpeed;
	return FallRules::PlanTakeoff(Speed, GetTakeoffCap(), FMath::Max(0.f, Link.EdgeCm - Along), Link.NearCm - Along, Link.FarCm > 0.f ? Link.FarCm - Along : 0.f,
		Link.DropCm, Gravity(), FallRules::GetConfig());
}

FVector ULedgeFallComponent::GetSaveLocation() const
{
	const AActor* Owner = GetOwner();
	const UDropLinkSubsystem* Links = GetLinks();
	const FDropLink* Link = Links && (State == ELedgeFallState::RunUp || State == ELedgeFallState::Airborne) ? Links->GetLink(ActiveLink) : nullptr;
	if (Link)
	{
		return Link->End + FVector(0.f, 0.f, GetHalfHeight() + 2.f);
	}
	return Owner ? Owner->GetActorLocation() : FVector::ZeroVector;
}

void ULedgeFallComponent::EnterState(ELedgeFallState NewState)
{
	State = NewState;
	StateTime = 0.f;
}

// --- Tick ----------------------------------------------------------------------------------------------------------------

void ULedgeFallComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	StateTime += DeltaTime;
	if (PendingLink != INDEX_NONE)
	{
		StartDrop();
	}
	switch (State)
	{
	case ELedgeFallState::None:
		PollLinks(DeltaTime);
		if (bTrackingOtherFall)
		{
			PeakFeetZ = FMath::Max(PeakFeetZ, FeetZ());
		}
		break;
	case ELedgeFallState::Waiting: TickWaiting(DeltaTime); break;
	case ELedgeFallState::RunUp: TickRunUp(DeltaTime); break;
	case ELedgeFallState::Airborne: TickAirborne(DeltaTime); break;
	case ELedgeFallState::Landed: TickLanded(DeltaTime); break;
	default: break;
	}
}

void ULedgeFallComponent::PollLinks(float DeltaTime)
{
	// Backup of the smart link's callback: a path that runs through a drop link and is at its start point.
	PollTimer -= DeltaTime;
	if (PollTimer > 0.f)
	{
		return;
	}
	PollTimer = 0.1f;
	const ACharacter* Character = GetCharacter();
	const AAIController* Controller = Character ? Cast<AAIController>(Character->GetController()) : nullptr;
	const UPathFollowingComponent* Following = Controller ? Controller->GetPathFollowingComponent() : nullptr;
	const UDropLinkSubsystem* Links = GetLinks();
	if (!Following || !Links || Links->GetLinks().IsEmpty() || Following->GetStatus() != EPathFollowingStatus::Moving || !Following->GetPath().IsValid()
		|| !Character->GetCharacterMovement()->IsMovingOnGround())
	{
		return;
	}
	TArray<int32> OnPath;
	Links->FindLinksOnPath(*Following->GetPath(), OnPath);
	const FVector Feet = Character->GetActorLocation() - FVector(0.f, 0.f, GetHalfHeight());
	for (const int32 Index : OnPath)
	{
		const FDropLink* Link = Links->GetLink(Index);
		if (Link && FVector::Dist2D(Feet, Link->Start) <= 45.f && FMath::Abs(Feet.Z - Link->Start.Z) <= 60.f)
		{
			NotifyDropLinkReached(Index);
			return;
		}
	}
}

bool ULedgeFallComponent::NotifyDropLinkReached(int32 Index)
{
	if (IsBusy())
	{
		return true;
	}
	const UDropLinkSubsystem* Links = GetLinks();
	if (!Links || !Links->GetLink(Index))
	{
		return false;
	}
	const ACharacter* Character = GetCharacter();
	if (!Character)
	{
		return false;
	}
	ApproachSpeed = static_cast<float>(Character->GetVelocity().Size2D());
	// The path's goal: he walks on there from the landing (like the ladder, LADDERS.md 7.1).
	const AAIController* Controller = Cast<AAIController>(Character->GetController());
	const UPathFollowingComponent* Following = Controller ? Controller->GetPathFollowingComponent() : nullptr;
	const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(Character);
	bHasPendingDestination = false;
	if (Operative && Operative->IsOwnMoveSteered())
	{
		PendingDestination = Operative->GetLastMoveDestination(); // a bypass waypoint's path: the ORDER's destination after the drop
		bHasPendingDestination = true;
	}
	else if (Following && Following->GetPath().IsValid() && Following->GetPath()->IsValid())
	{
		PendingDestination = Following->GetPath()->GetDestinationLocation();
		bHasPendingDestination = true;
	}
	else if (Operative)
	{
		PendingDestination = Operative->GetLastMoveDestination();
		bHasPendingDestination = true;
	}
	bOwnOrderAtEntry = Operative && Operative->HasOwnMoveOrder();
	PendingLink = Index; // the smart link calls back from inside the crowd update: the drop starts on the next tick
	UE_LOG(LogCodexTactics, Display, TEXT("[Fall] %s reached drop link %d at %.0f cm/s"), *GetNameSafe(GetOwner()), Index, ApproachSpeed);
	return true;
}

void ULedgeFallComponent::HandleNewOrder(const FVector& Destination)
{
	PendingDestination = Destination;
	bHasPendingDestination = true;
}

void ULedgeFallComponent::CancelWait()
{
	if (State != ELedgeFallState::Waiting)
	{
		return;
	}
	ActiveLink = INDEX_NONE;
	Kind = ELedgeFallKind::None;
	bHasPendingDestination = false;
	EnterState(ELedgeFallState::None);
}

void ULedgeFallComponent::StopOwnerMove()
{
	ACharacter* Character = GetCharacter();
	if (!Character)
	{
		return;
	}
	const FVector Keep = PendingDestination;
	const bool bKeep = bHasPendingDestination;
	if (AOperativeCharacter* Operative = Cast<AOperativeCharacter>(Character))
	{
		Operative->StopOperative();
	}
	else if (AController* Controller = Character->GetController())
	{
		Controller->StopMovement();
	}
	PendingDestination = Keep;
	bHasPendingDestination = bKeep;
}

void ULedgeFallComponent::Refuse(const FString& Line)
{
	UE_LOG(LogCodexTactics, Display, TEXT("[Fall] %s: drop refused (%s)"), *GetNameSafe(GetOwner()), *Line);
	StopOwnerMove();
	bHasPendingDestination = false;
	if (const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(GetOwner()))
	{
		UFloatingTextSubsystem::SpawnAboveOperative(Operative, Line, FLinearColor(1.f, 0.6f, 0.2f));
	}
	ActiveLink = INDEX_NONE;
	Kind = ELedgeFallKind::None;
	EnterState(ELedgeFallState::None);
}

void ULedgeFallComponent::StartDrop()
{
	const int32 Index = PendingLink;
	PendingLink = INDEX_NONE;
	ACharacter* Character = GetCharacter();
	UDropLinkSubsystem* Links = GetLinks();
	const FDropLink* Link = Links ? Links->GetLink(Index) : nullptr;
	if (!Character || !Link)
	{
		return;
	}
	const UHealthComponent* Health = Character->FindComponentByClass<UHealthComponent>();
	AOperativeCharacter* Operative = Cast<AOperativeCharacter>(Character);
	const UTurnBasedCombatSubsystem* TurnBased = GetWorld() ? GetWorld()->GetSubsystem<UTurnBasedCombatSubsystem>() : nullptr;
	ActiveLink = Index;
	Kind = ELedgeFallKind::DropLink;
	if ((Health && !Health->IsAlive()) || (Operative && Operative->IsKnockedDown()) || VehicleQueries::IsMounted(Character))
	{
		Refuse(TEXT("CAN'T DROP"));
		return;
	}
	if (TurnBased && TurnBased->IsActive())
	{
		Refuse(TEXT("NO DROPS IN TURN-BASED COMBAT"));
		return;
	}
	const bool bMustStand = Operative && (Operative->GetStance() != EOperativeStance::Standing || Operative->GetMoveHoldSeconds() > 0.f);
	if (bMustStand || !Links->TryLease(Index, Character))
	{
		// He waits at the link: another man's drop lands there now (one at a time), or he stands up first.
		if (State != ELedgeFallState::Waiting)
		{
			StopOwnerMove();
			EnterState(ELedgeFallState::Waiting);
			UE_LOG(LogCodexTactics, Display, TEXT("[Fall] %s waits at drop link %d (%s)"), *GetNameSafe(Character), Index, bMustStand ? TEXT("stands up first") : TEXT("the landing is taken"));
		}
		if (bMustStand && Operative && Operative->GetStance() != EOperativeStance::Standing)
		{
			Operative->SetStance(EOperativeStance::Standing);
		}
		RetryTimer = 0.2f;
		return;
	}

	// Take the body over: the path ends here (it resumes at the landing); the run-up off the edge.
	StopOwnerMove();
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	if (!bMovementSaved)
	{
		SavedMaxWalkSpeed = Movement->MaxWalkSpeed;
		bSavedWalkOffLedges = Movement->bCanWalkOffLedges;
		bMovementSaved = true;
	}
	Movement->bCanWalkOffLedges = true;
	StartFeetZ = FeetZ();
	PeakFeetZ = StartFeetZ;
	// The plan from where he stands (he is at the link start, give or take the crowd's arrival radius).
	const float Along = static_cast<float>(FVector::DotProduct(Character->GetActorLocation() - Link->Start, Link->Dir));
	const FallRules::FFallConfig& Config = FallRules::GetConfig();
	const float Edge = FMath::Max(0.f, Link->EdgeCm - Along);
	const FallRules::FTakeoffPlan Plan = FallRules::PlanTakeoff(ApproachSpeed, GetTakeoffCap(), Edge, Link->NearCm - Along,
		Link->FarCm > 0.f ? Link->FarCm - Along : 0.f, Link->DropCm, Gravity(), Config);
	PlannedSpeed = FMath::Max(Plan.SpeedCms, 50.f);
	bPlannedReach = Plan.bReaches;
	// Aim at the landing (the crowd hands the link over up to ~80 cm before its start, maybe a little to the side of it).
	const FVector ToLanding = (Link->End - Character->GetActorLocation()).GetSafeNormal2D();
	RunDir = FVector::DotProduct(ToLanding, Link->Dir) > 0.7f ? ToLanding : Link->Dir;
	Movement->MaxWalkSpeed = PlannedSpeed;
	Movement->Velocity = RunDir * PlannedSpeed;
	Character->SetActorRotation(FRotator(0.f, RunDir.Rotation().Yaw, 0.f));
	// The run-up's time limit: the way to the edge and a body width past it (the capsule rolls over the edge) at the planned speed, at least the tunable.
	RunUpLimitSeconds = FMath::Max(Config.RunUpTimeoutSeconds, (Edge + 2.f * Character->GetCapsuleComponent()->GetScaledCapsuleRadius() + 50.f) / PlannedSpeed + 0.5f);
	++DropCount;
	LastAirSpeed = 0.f;
	bPlayedFallClip = false;
	EnterState(ELedgeFallState::RunUp);
	UE_LOG(LogCodexTactics, Display, TEXT("[Fall] %s drops off link %d (%s -> %s, out %s) from %s: %.0f cm down, edge %.0f / lower surface %.0f..%.0f cm out; approach %.0f cm/s, cap %.0f -> run-up %.0f cm/s, lands at %.0f cm (%s)"),
		*GetNameSafe(Character), Index, *Link->Start.ToCompactString(), *Link->End.ToCompactString(), *Link->Dir.ToCompactString(), *Character->GetActorLocation().ToCompactString(),
		Link->DropCm, Edge, Link->NearCm - Along, Link->FarCm > 0.f ? Link->FarCm - Along : -1.f, ApproachSpeed, GetTakeoffCap(), PlannedSpeed,
		Plan.LandingCm, Plan.bReaches ? TEXT("reaches") : Plan.bTooSlow ? TEXT("TOO SLOW: falls short") : TEXT("overshoots"));
}

void ULedgeFallComponent::TickWaiting(float DeltaTime)
{
	RetryTimer -= DeltaTime;
	if (RetryTimer > 0.f)
	{
		return;
	}
	RetryTimer = 0.2f;
	PendingLink = ActiveLink; // StartDrop re-checks the stance and the lease
	StartDrop();
}

void ULedgeFallComponent::TickRunUp(float DeltaTime)
{
	ACharacter* Character = GetCharacter();
	if (!Character)
	{
		Finish(false);
		return;
	}
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	if (Movement->IsFalling())
	{
		BeginAirborne();
		return;
	}
	if (StateTime > RunUpLimitSeconds)
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("[Fall] %s: the run-up off link %d never left the edge (%.1f s, at %s) - the link is switched off, the path goes on"),
			*GetNameSafe(Character), ActiveLink, StateTime, *Character->GetActorLocation().ToCompactString());
		if (UDropLinkSubsystem* Links = GetLinks())
		{
			Links->DisableLink(ActiveLink, TEXT("the run-up never left the edge"));
		}
		Finish(true);
		return;
	}
	// Keep running straight off the edge at the planned speed (no path following now).
	Movement->MaxWalkSpeed = PlannedSpeed;
	const FVector Run = RunDir * PlannedSpeed;
	Movement->Velocity = FVector(Run.X, Run.Y, Movement->Velocity.Z);
	Movement->RequestDirectMove(Run, false);
	Character->SetActorRotation(FRotator(0.f, RunDir.Rotation().Yaw, 0.f));
}

void ULedgeFallComponent::BeginAirborne()
{
	PeakFeetZ = FMath::Max(PeakFeetZ, FeetZ());
	EnterState(ELedgeFallState::Airborne);
	SetRootMotionIgnored(true);
	PlayFallLoop();
}

void ULedgeFallComponent::TickAirborne(float DeltaTime)
{
	ACharacter* Character = GetCharacter();
	if (!Character)
	{
		Finish(false);
		return;
	}
	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	PeakFeetZ = FMath::Max(PeakFeetZ, FeetZ());
	LastAirSpeed = FMath::Max(LastAirSpeed, static_cast<float>(Movement->Velocity.Size2D()));
	if (!Movement->IsFalling() || StateTime > FallRules::GetConfig().AirTimeoutSeconds)
	{
		if (Movement->IsFalling())
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[Fall] %s: still falling after %.1f s - landed by force"), *GetNameSafe(Character), StateTime);
		}
		Land();
	}
}

void ULedgeFallComponent::Land()
{
	ACharacter* Character = GetCharacter();
	if (!Character)
	{
		Finish(false);
		return;
	}
	const FallRules::FFallConfig& Config = FallRules::GetConfig();
	const float LandFeet = FeetZ();
	LastFallHeight = FMath::Max(0.f, PeakFeetZ - LandFeet);
	LastFallDamage = FallRules::FallDamage(LastFallHeight, Config);
	LastFallKind = Kind;
	LastLandingLocation = Character->GetActorLocation() - FVector(0.f, 0.f, GetHalfHeight());
	++LandingCount;
	UDropLinkSubsystem* Links = GetLinks();
	const FDropLink* Link = Links ? Links->GetLink(ActiveLink) : nullptr;
	bLastLandedOnTarget = Link && FMath::Abs(LandFeet - Link->End.Z) <= 60.f;
	StopFallLoop(0.1f);
	// The landing takes most of the speed (no sliding on across a small roof); his own speeds again.
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	Movement->Velocity = FVector(Movement->Velocity.X * 0.3f, Movement->Velocity.Y * 0.3f, Movement->Velocity.Z);
	RestoreMovement();
	UHealthComponent* Health = Character->FindComponentByClass<UHealthComponent>();
	UE_LOG(LogCodexTactics, Display, TEXT("[Fall] %s landed after a %.0f cm %s: %.1f fall damage%s (run-up %.0f cm/s, air speed %.0f cm/s%s)"), *GetNameSafe(Character),
		LastFallHeight, LedgeFallInternal::KindName(Kind), LastFallDamage, bKnockdownOnLanding ? TEXT(", knocked down") : TEXT(""), PlannedSpeed, LastAirSpeed,
		Link ? (bLastLandedOnTarget ? TEXT(", on the link's lower surface") : TEXT(", MISSED the link's lower surface")) : TEXT(""));
	if (Health && Health->IsAlive() && LastFallDamage > 0.f)
	{
		Health->ApplyDirectHealthLoss(LastFallDamage, TEXT("Fall"));
		if (const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(Character))
		{
			UFloatingTextSubsystem::SpawnAboveOperative(Operative, FString::Printf(TEXT("FALL -%d"), FMath::RoundToInt(FMath::Min(LastFallDamage, 999.f))),
				FLinearColor(1.f, 0.45f, 0.15f));
		}
	}
	if (Links && ActiveLink != INDEX_NONE)
	{
		Links->ReleaseLease(ActiveLink, Character, Config.LeaseHoldAfterLandingSeconds);
	}
	if (bKnockdownOnLanding)
	{
		// The knockdown that threw him plays now (by the normal rules: the immunity, the death while down).
		const EKnockdownDirection Side = KnockdownSide;
		const EKnockdownCause Cause = KnockdownCause;
		bKnockdownOnLanding = false;
		Finish(false);
		if (UKnockdownComponent* Knockdown = Character->FindComponentByClass<UKnockdownComponent>(); Knockdown && (!Health || Health->IsAlive()))
		{
			TGuardValue<bool> NoSecondThrow(bLandingKnockdown, true);
			Knockdown->ForceKnockDown(Side, Cause);
		}
		return;
	}
	PlayLandClip();
	EnterState(ELedgeFallState::Landed);
}

void ULedgeFallComponent::TickLanded(float DeltaTime)
{
	if (StateTime >= FallRules::GetConfig().LandSettleSeconds)
	{
		Finish(true);
	}
}

void ULedgeFallComponent::RestoreMovement()
{
	ACharacter* Character = GetCharacter();
	if (Character && bMovementSaved)
	{
		UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
		Movement->bCanWalkOffLedges = bSavedWalkOffLedges;
		Movement->MaxWalkSpeed = SavedMaxWalkSpeed;
		if (AOperativeCharacter* Operative = Cast<AOperativeCharacter>(Character))
		{
			Operative->ApplyMovementParams();
		}
	}
	bMovementSaved = false;
}

void ULedgeFallComponent::Finish(bool bResume)
{
	RestoreMovement();
	SetRootMotionIgnored(false);
	if (UDropLinkSubsystem* Links = GetLinks(); Links && ActiveLink != INDEX_NONE && State != ELedgeFallState::Landed)
	{
		Links->ReleaseLease(ActiveLink, GetOwner(), 0.f);
	}
	ActiveLink = INDEX_NONE;
	Kind = ELedgeFallKind::None;
	EnterState(ELedgeFallState::None);
	if (bResume)
	{
		ResumePath();
	}
	else
	{
		bHasPendingDestination = false;
	}
}

void ULedgeFallComponent::ResumePath()
{
	const FVector Destination = PendingDestination;
	const bool bHadDestination = bHasPendingDestination;
	bHasPendingDestination = false;
	const UTurnBasedCombatSubsystem* TurnBased = GetWorld() ? GetWorld()->GetSubsystem<UTurnBasedCombatSubsystem>() : nullptr;
	if (TurnBased && TurnBased->IsActive())
	{
		return; // the fight started while he dropped: he stays where he landed
	}
	if (AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(GetOwner()))
	{
		Enemy->ResumeMoveAfterLadder(bHadDestination ? Destination : Enemy->GetActorLocation());
		return;
	}
	AOperativeCharacter* Operative = Cast<AOperativeCharacter>(GetOwner());
	if (bHadDestination && Operative && FVector::Dist2D(Destination, Operative->GetActorLocation()) > 60.f)
	{
		Operative->ResumeMoveAfterLadder(Destination, bOwnOrderAtEntry || Operative->HasOwnMoveOrder());
	}
}

// --- Knockback falls -----------------------------------------------------------------------------------------------------

bool ULedgeFallComponent::TryKnockbackFall(EKnockdownDirection Side, EKnockdownCause Cause, bool bHasSource, const FVector& SourceLocation)
{
	ACharacter* Character = GetCharacter();
	UWorld* World = GetWorld();
	if (!Character || !World || IsBusy() || bLandingKnockdown || !Character->GetCharacterMovement()->IsMovingOnGround() || VehicleQueries::IsMounted(Character))
	{
		return false;
	}
	if (const ULadderClimbComponent* Ladder = Character->FindComponentByClass<ULadderClimbComponent>(); Ladder && Ladder->IsBusy())
	{
		return false;
	}
	if (const UTurnBasedCombatSubsystem* TurnBased = World->GetSubsystem<UTurnBasedCombatSubsystem>(); TurnBased && TurnBased->IsActive())
	{
		return false; // the turn-based grid keeps everybody on his level
	}
	const FallRules::FFallConfig& Config = FallRules::GetConfig();
	const FVector Dir = FallRules::KnockbackDirection(Side, Character->GetActorForwardVector(), bHasSource, Character->GetActorLocation(), SourceLocation);
	const float Radius = Character->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const float Feet = FeetZ();
	const FVector Base(Character->GetActorLocation().X, Character->GetActorLocation().Y, Feet);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LedgeKnockback), false, Character);
	LedgeFallInternal::IgnorePawns(*World, Params);
	// The edge: the first probe along the throw with no floor at his feet' height (+-step tolerance) under it.
	float EdgeCm = -1.f;
	for (float D = 10.f; D <= Config.KnockbackReachCm + Radius; D += 10.f)
	{
		const FVector At = Base + Dir * D;
		FHitResult Hit;
		const bool bFloor = World->LineTraceSingleByChannel(Hit, At + FVector(0.f, 0.f, 30.f), At - FVector(0.f, 0.f, Config.StepToleranceCm), ECC_Pawn, Params);
		if (!bFloor)
		{
			EdgeCm = D;
			break;
		}
	}
	if (EdgeCm < 0.f)
	{
		return false;
	}
	// How deep beyond it.
	FHitResult Below;
	const FVector Beyond = Base + Dir * (EdgeCm + Radius);
	const float DropCm = World->LineTraceSingleByChannel(Below, Beyond + FVector(0.f, 0.f, 20.f), Beyond - FVector(0.f, 0.f, 5000.f), ECC_Pawn, Params)
		? Feet - static_cast<float>(Below.ImpactPoint.Z) : 5000.f;
	// A parapet / railing up to his waist between him and the edge stops the throw.
	bool bObstacle = false;
	for (const float Height : { 30.f, FMath::Max(35.f, Config.ParapetHeightCm - 10.f) })
	{
		FHitResult Wall;
		const FVector From = Base + FVector(0.f, 0.f, Height);
		bObstacle |= World->SweepSingleByChannel(Wall, From, From + Dir * (EdgeCm + 15.f), FQuat::Identity, ECC_Pawn, FCollisionShape::MakeSphere(12.f), Params);
	}
	if (!FallRules::KnockbackGoesOver(EdgeCm, bObstacle, DropCm, Radius, Gravity(), Config))
	{
		UE_LOG(LogCodexTactics, Verbose, TEXT("[Fall] %s: no knockback fall (edge %.0f cm, drop %.0f cm, parapet %d)"), *GetNameSafe(Character), EdgeCm, DropCm, bObstacle ? 1 : 0);
		return false;
	}
	StopOwnerMove();
	bHasPendingDestination = false; // the knockdown buffers his orders
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	if (!bMovementSaved)
	{
		SavedMaxWalkSpeed = Movement->MaxWalkSpeed;
		bSavedWalkOffLedges = Movement->bCanWalkOffLedges;
		bMovementSaved = true;
	}
	Movement->bCanWalkOffLedges = true;
	Kind = ELedgeFallKind::Knockback;
	ActiveLink = INDEX_NONE;
	bKnockdownOnLanding = true;
	KnockdownSide = Side == EKnockdownDirection::None ? EKnockdownDirection::Back : Side;
	KnockdownCause = Cause;
	StartFeetZ = Feet;
	PeakFeetZ = Feet;
	PlannedSpeed = Config.KnockbackSpeedCms;
	bPlannedReach = false;
	LastAirSpeed = 0.f;
	bPlayedFallClip = false;
	++KnockbackFallCount;
	BeginAirborne(); // the state first: the movement mode change below is this component's own fall
	Movement->SetMovementMode(MOVE_Falling);
	Movement->Velocity = Dir * Config.KnockbackSpeedCms + FVector(0.f, 0.f, Config.KnockbackUpCms);
	UE_LOG(LogCodexTactics, Display, TEXT("[Fall] %s is thrown over the edge %.0f cm away (%.0f cm drop, cause %d)"), *GetNameSafe(Character), EdgeCm, DropCm,
		static_cast<int32>(Cause));
	return true;
}

// --- Other falls ---------------------------------------------------------------------------------------------------------

void ULedgeFallComponent::HandleMovementModeChanged(ACharacter* Character, EMovementMode PrevMovementMode, uint8 PreviousCustomMode)
{
	if (!Character || State != ELedgeFallState::None)
	{
		bTrackingOtherFall = false;
		return;
	}
	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	const bool bWasGround = PrevMovementMode == MOVE_Walking || PrevMovementMode == MOVE_NavWalking;
	const ULadderClimbComponent* Ladder = Character->FindComponentByClass<ULadderClimbComponent>();
	const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(Character);
	const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Character);
	const bool bIgnored = (Ladder && Ladder->IsOnLadder()) || (Operative && Operative->IsVaulting()) || (Enemy && Enemy->IsJumpAttacking())
		|| VehicleQueries::IsMounted(Character);
	if (Movement->IsFalling() && bWasGround && !bIgnored)
	{
		bTrackingOtherFall = true;
		StartFeetZ = FeetZ();
		PeakFeetZ = StartFeetZ;
		return;
	}
	if (PrevMovementMode == MOVE_Falling && Movement->IsMovingOnGround() && bTrackingOtherFall)
	{
		bTrackingOtherFall = false;
		const float Height = FMath::Max(0.f, PeakFeetZ - FeetZ());
		const FallRules::FFallConfig& Config = FallRules::GetConfig();
		if (bIgnored || Height < Config.MinTrackedFallCm)
		{
			return;
		}
		LastFallHeight = Height;
		LastFallDamage = FallRules::FallDamage(Height, Config);
		LastFallKind = ELedgeFallKind::Other;
		LastLandingLocation = Character->GetActorLocation() - FVector(0.f, 0.f, GetHalfHeight());
		bLastLandedOnTarget = false;
		++LandingCount;
		UE_LOG(LogCodexTactics, Display, TEXT("[Fall] %s landed after a %.0f cm fall: %.1f fall damage"), *GetNameSafe(Character), Height, LastFallDamage);
		UHealthComponent* Health = Character->FindComponentByClass<UHealthComponent>();
		if (Health && Health->IsAlive() && LastFallDamage > 0.f)
		{
			Health->ApplyDirectHealthLoss(LastFallDamage, TEXT("Fall"));
			if (Operative)
			{
				UFloatingTextSubsystem::SpawnAboveOperative(Operative, FString::Printf(TEXT("FALL -%d"), FMath::RoundToInt(FMath::Min(LastFallDamage, 999.f))),
					FLinearColor(1.f, 0.45f, 0.15f));
			}
		}
	}
}

// --- Clips ---------------------------------------------------------------------------------------------------------------

UAnimSequenceBase* ULedgeFallComponent::LoadClip(const TSoftObjectPtr<UAnimSequenceBase>& Clip) const
{
	return Clip.IsNull() ? nullptr : Clip.LoadSynchronous();
}

void ULedgeFallComponent::SetRootMotionIgnored(bool bIgnored)
{
	const ACharacter* Character = GetCharacter();
	UAnimInstance* Anim = Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
	if (!Anim)
	{
		bRootMotionSaved = false;
		return;
	}
	if (bIgnored && !bRootMotionSaved)
	{
		SavedRootMotionMode = static_cast<uint8>(Anim->RootMotionMode.GetValue());
		bRootMotionSaved = true;
		Anim->RootMotionMode = ERootMotionMode::IgnoreRootMotion; // the fall keeps its ballistic speed (a clip's root motion would replace it)
	}
	else if (!bIgnored && bRootMotionSaved)
	{
		Anim->RootMotionMode = static_cast<ERootMotionMode::Type>(SavedRootMotionMode);
		bRootMotionSaved = false;
	}
}

void ULedgeFallComponent::PlayFallLoop()
{
	const ACharacter* Character = GetCharacter();
	UAnimInstance* Anim = Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
	UAnimSequenceBase* Clip = LoadClip(FallLoopClip);
	if (!Anim || !Clip)
	{
		return;
	}
	FallMontage = Anim->PlaySlotAnimationAsDynamicMontage(Clip, MontageSlot, 0.15f, 0.12f, 1.f, 1000);
	bPlayedFallClip = FallMontage.IsValid();
}

void ULedgeFallComponent::StopFallLoop(float BlendOut)
{
	const ACharacter* Character = GetCharacter();
	UAnimInstance* Anim = Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
	if (Anim && FallMontage.IsValid())
	{
		Anim->Montage_Stop(BlendOut, FallMontage.Get());
	}
	FallMontage.Reset();
}

void ULedgeFallComponent::PlayLandClip()
{
	const ACharacter* Character = GetCharacter();
	UAnimInstance* Anim = Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
	UAnimSequenceBase* Clip = LoadClip(LandClip);
	if (Anim && Clip)
	{
		LandMontage = Anim->PlaySlotAnimationAsDynamicMontage(Clip, MontageSlot, 0.05f, 0.2f, 1.f, 1);
	}
}
