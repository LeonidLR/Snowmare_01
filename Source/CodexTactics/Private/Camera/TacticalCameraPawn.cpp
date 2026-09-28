#include "Camera/TacticalCameraPawn.h"
#include "Camera/CameraComponent.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/SpringArmComponent.h"
#include "Misc/App.h"

ATacticalCameraPawn::ATacticalCameraPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(RootComponent);
	SpringArm->bDoCollisionTest = false;
	SpringArm->bUsePawnControlRotation = false;
	SpringArm->bInheritPitch = false;
	SpringArm->bInheritYaw = false;
	SpringArm->bInheritRoll = false;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
}

void ATacticalCameraPawn::BeginPlay()
{
	Super::BeginPlay();

	SpringArm->TargetArmLength = ArmLength;
	SpringArm->SetRelativeRotation(FRotator(Pitch, Yaw, 0.f));

	if (USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>())
	{
		Squad->OnLeaderChanged.AddDynamic(this, &ATacticalCameraPawn::HandleLeaderChanged);
		SetFollowTarget(Squad->GetLeader());
	}
}

void ATacticalCameraPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!FollowTarget.IsValid())
	{
		// The squad may spawn after the camera; pick up the leader once it exists.
		if (USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>())
		{
			SetFollowTarget(Squad->GetLeader());
		}
		if (!FollowTarget.IsValid())
		{
			return;
		}
	}

	const float RealDelta = static_cast<float>(FApp::GetDeltaTime());
	const FVector NewLocation = FMath::VInterpTo(GetActorLocation(), FollowTarget->GetActorLocation(), RealDelta, FollowSpeed);
	SetActorLocation(NewLocation);
}

void ATacticalCameraPawn::SetFollowTarget(AActor* NewTarget)
{
	FollowTarget = NewTarget;
}

void ATacticalCameraPawn::HandleLeaderChanged(AOperativeCharacter* NewLeader)
{
	SetFollowTarget(NewLeader);
}
