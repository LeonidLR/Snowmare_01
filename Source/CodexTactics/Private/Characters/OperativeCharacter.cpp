#include "Characters/OperativeCharacter.h"
#include "Characters/OperativeAIController.h"
#include "Characters/SquadSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Navigation/PathFollowingComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** Speed below which an operative without a move order counts as standing still, cm/s. */
	constexpr float MovingSpeedThreshold = 15.f;
}

AOperativeCharacter::AOperativeCharacter()
{
	PrimaryActorTick.bCanEverTick = false;

	// Godot capsule: radius 0.4 m, height 1.8 m.
	GetCapsuleComponent()->InitCapsuleSize(40.f, 90.f);

	AIControllerClass = AOperativeAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	bUseControllerRotationYaw = false;

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = true;
	Movement->bUseControllerDesiredRotation = false;
	Movement->GetNavAgentPropertiesRef().bCanCrouch = true;
	Movement->GetNavAgentPropertiesRef().bCanJump = false;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(GetCapsuleComponent());
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BodyMesh->SetRelativeScale3D(FVector(0.7f, 0.7f, 1.8f));
	if (CylinderMesh.Succeeded())
	{
		BodyMesh->SetStaticMesh(CylinderMesh.Object);
	}

	FacingMarker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FacingMarker"));
	FacingMarker->SetupAttachment(GetCapsuleComponent());
	FacingMarker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FacingMarker->SetRelativeLocation(FVector(40.f, 0.f, 50.f));
	FacingMarker->SetRelativeScale3D(FVector(0.3f, 0.15f, 0.15f));
	if (CubeMesh.Succeeded())
	{
		FacingMarker->SetStaticMesh(CubeMesh.Object);
	}
}

void AOperativeCharacter::BeginPlay()
{
	Super::BeginPlay();

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->MaxAcceleration = MovementConfig.Acceleration;
	Movement->BrakingDecelerationWalking = MovementConfig.Deceleration;
	Movement->GetNavMovementProperties()->bUseFixedBrakingDistanceForPaths = true;
	Movement->GetNavMovementProperties()->FixedPathBrakingDistance = MovementConfig.PathBrakingDistance;
	ApplyMovementParams();

	if (BodyMesh->GetMaterial(0))
	{
		BodyMaterial = BodyMesh->CreateAndSetMaterialInstanceDynamic(0);
		BodyMaterial->SetVectorParameterValue(TEXT("Color"), BodyColor);
		FacingMarker->SetMaterial(0, BodyMaterial);
	}

	if (USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>())
	{
		Squad->RegisterOperative(this);
	}
}

void AOperativeCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>())
		{
			Squad->UnregisterOperative(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

EOperativeOrderResult AOperativeCharacter::OrderMoveTo(const FVector& Destination, bool bSprint)
{
	if (bSprint && CanSprint())
	{
		// A sprint order stands a crouching operative up (Godot set_target).
		if (Stance == EOperativeStance::Crouching)
		{
			Stance = EOperativeStance::Standing;
		}
		bSprinting = true;
	}
	else
	{
		bSprinting = false;
	}
	ApplyMovementParams();
	return RequestMove(Destination);
}

EOperativeOrderResult AOperativeCharacter::FollowTo(const FVector& Destination, float Speed)
{
	ApplyMovementParams(Speed);
	return RequestMove(Destination);
}

EOperativeOrderResult AOperativeCharacter::RequestMove(const FVector& Destination)
{
	AOperativeAIController* AIController = Cast<AOperativeAIController>(GetController());
	if (!AIController)
	{
		return EOperativeOrderResult::NoController;
	}
	const EPathFollowingRequestResult::Type Result = AIController->MoveToLocation(
		Destination, MovementConfig.AcceptanceRadius, /*bStopOnOverlap*/ false, /*bUsePathfinding*/ true,
		/*bProjectDestinationToNavigation*/ true, /*bCanStrafe*/ false);
	if (Result == EPathFollowingRequestResult::Failed)
	{
		bHasMoveOrder = false;
		return EOperativeOrderResult::Unreachable;
	}
	bHasMoveOrder = Result == EPathFollowingRequestResult::RequestSuccessful;
	return EOperativeOrderResult::Accepted;
}

void AOperativeCharacter::StopOperative()
{
	if (AController* OwnerController = GetController())
	{
		OwnerController->StopMovement();
	}
	GetCharacterMovement()->StopMovementImmediately();
	bHasMoveOrder = false;
	bSprinting = false;
	ApplyMovementParams();
}

void AOperativeCharacter::SetStance(EOperativeStance NewStance)
{
	if (Stance == NewStance)
	{
		return;
	}
	Stance = NewStance;
	if (Stance != EOperativeStance::Standing)
	{
		bSprinting = false;
	}
	ApplyMovementParams();
}

void AOperativeCharacter::SetSprinting(bool bNewSprinting)
{
	const bool bAllowed = bNewSprinting && CanSprint() && Stance == EOperativeStance::Standing;
	if (bSprinting != bAllowed)
	{
		bSprinting = bAllowed;
		ApplyMovementParams();
	}
}

void AOperativeCharacter::HandleMoveFinished()
{
	bHasMoveOrder = false;
	bSprinting = false;
	ApplyMovementParams();
}

bool AOperativeCharacter::CanSprint() const
{
	return OperativeMovementRules::CanSprint(MovementConfig, Stance, ColdLevel, bWounded);
}

float AOperativeCharacter::GetMaxSpeed() const
{
	return OperativeMovementRules::ComputeMaxSpeed(MovementConfig, Stance, bSprinting, bWounded, bCarrying);
}

bool AOperativeCharacter::IsMoving() const
{
	return bHasMoveOrder || GetVelocity().Size2D() > MovingSpeedThreshold;
}

void AOperativeCharacter::ApplyMovementParams(float SpeedOverride)
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->MaxWalkSpeed = SpeedOverride >= 0.f ? SpeedOverride : GetMaxSpeed();
	Movement->RotationRate = FRotator(0.f, OperativeMovementRules::GetTurnRate(MovementConfig, Stance), 0.f);
}
