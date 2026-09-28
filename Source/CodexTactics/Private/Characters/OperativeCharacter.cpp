#include "Characters/OperativeCharacter.h"
#include "Characters/OperativeAIController.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/WeaponDataAsset.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Navigation/PathFollowingComponent.h"
#include "UI/GameMessageSubsystem.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** Speed below which an operative without a move order counts as standing still, cm/s. */
	constexpr float MovingSpeedThreshold = 15.f;
}

AOperativeCharacter::AOperativeCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

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

	HealthComponent = CreateDefaultSubobject<UHealthComponent>(TEXT("HealthComponent"));
	HealthComponent->MaxHealth = 100.0f;
	HealthComponent->BaseArmorReduction = 0.10f;
}

void AOperativeCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (HealthComponent)
	{
		HealthComponent->OnDied.AddDynamic(this, &AOperativeCharacter::HandleDied);
	}

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

	if (HealthComponent)
	{
		float DefMult = 1.0f;
		if (Stance == EOperativeStance::Crouching)
		{
			DefMult = 0.75f;
		}
		else if (Stance == EOperativeStance::Prone)
		{
			DefMult = 0.50f;
		}
		HealthComponent->SetDefenseMultiplier(DefMult);
	}

	ApplyMovementParams();
}

void AOperativeCharacter::HandleDied(AActor* Victim, const FString& AttackerSource)
{
	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(FText::FromString(TEXT("ШТАБ")), FText::Format(FText::FromString(TEXT("{0} погиб в бою!")), DisplayName));
	}
	if (UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>())
	{
		Flow->TriggerGameOver();
	}
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

void AOperativeCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	ProcessCombatShooting(DeltaTime);
}

void AOperativeCharacter::EquipWeapon(UWeaponDataAsset* NewWeapon)
{
	CurrentWeapon = NewWeapon;
	if (CurrentWeapon)
	{
		CurrentClip = CurrentWeapon->MaxClipSize;
		ReserveAmmo = CurrentWeapon->DefaultReserveAmmo;
	}
	bIsReloading = false;
	ReloadTimer = 0.0f;
	ShootTimer = 0.0f;
}

void AOperativeCharacter::StartReload()
{
	if (bIsReloading || ReserveAmmo <= 0)
	{
		return;
	}

	const int32 MaxClip = CurrentWeapon ? CurrentWeapon->MaxClipSize : 30;
	if (CurrentClip >= MaxClip)
	{
		return;
	}

	bIsReloading = true;
	ReloadTimer = CurrentWeapon ? CurrentWeapon->ReloadTime : 2.0f;
}

bool AOperativeCharacter::CanShoot() const
{
	if (!HealthComponent || !HealthComponent->IsAlive())
	{
		return false;
	}
	if (bSprinting || bCarrying || bIsReloading)
	{
		return false;
	}
	return true;
}

AActor* AOperativeCharacter::FindBestCombatTarget() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	const float MaxRange = CurrentWeapon ? CurrentWeapon->AttackRangeCm : 1400.0f;
	const FVector MyLoc = GetActorLocation();

	AActor* BestTarget = nullptr;
	float MinDistSq = MaxRange * MaxRange;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Candidate = *It;
		if (!Candidate || !Candidate->ActorHasTag(FName(TEXT("Enemy"))) || Candidate->IsHidden())
		{
			continue;
		}

		if (UHealthComponent* HC = Candidate->FindComponentByClass<UHealthComponent>())
		{
			if (!HC->IsAlive())
			{
				continue;
			}
		}

		const float DistSq = FVector::DistSquared2D(MyLoc, Candidate->GetActorLocation());
		if (DistSq < MinDistSq)
		{
			MinDistSq = DistSq;
			BestTarget = Candidate;
		}
	}

	return BestTarget;
}

void AOperativeCharacter::ProcessCombatShooting(float DeltaTime)
{
	if (!HealthComponent || !HealthComponent->IsAlive())
	{
		return;
	}

	if (MisfireCooldownTimer > 0.0f)
	{
		MisfireCooldownTimer -= DeltaTime;
	}

	if (bIsReloading)
	{
		ReloadTimer -= DeltaTime;
		if (ReloadTimer <= 0.0f)
		{
			bIsReloading = false;
			const int32 MaxClip = CurrentWeapon ? CurrentWeapon->MaxClipSize : 30;
			const int32 Needed = MaxClip - CurrentClip;
			const int32 Added = FMath::Min(Needed, ReserveAmmo);
			CurrentClip += Added;
			ReserveAmmo -= Added;
		}
		return;
	}

	if (ShootTimer > 0.0f)
	{
		ShootTimer -= DeltaTime;
	}

	if (!CanShoot())
	{
		return;
	}

	// Only auto-shoot when in WaveCombat real-time mode (or if no game flow subsystem exists, e.g. standalone test)
	if (UWorld* World = GetWorld())
	{
		if (UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>())
		{
			if (Flow->GetPhase() != ECodexGamePhase::WaveCombat || Flow->GetCombatMode() != ECodexCombatMode::RealTime)
			{
				return;
			}
		}
	}

	if (CurrentClip <= 0)
	{
		if (ReserveAmmo > 0)
		{
			StartReload();
		}
		return;
	}

	AActor* Target = FindBestCombatTarget();
	if (!Target)
	{
		return;
	}

	// Turn towards target smoothly
	FRotator LookRot = (Target->GetActorLocation() - GetActorLocation()).Rotation();
	LookRot.Pitch = 0.f;
	LookRot.Roll = 0.f;
	SetActorRotation(FMath::RInterpTo(GetActorRotation(), LookRot, DeltaTime, 12.0f));

	if (ShootTimer <= 0.0f && MisfireCooldownTimer <= 0.0f)
	{
		ShootAtTarget(Target);
	}
}

bool AOperativeCharacter::ShootAtTarget(AActor* Target)
{
	if (!Target || CurrentClip <= 0)
	{
		return false;
	}

	CurrentClip--;

	// Check cold misfire (Godot parity: >= 60% cold, max 35% chance)
	bool bMisfire = bForceMisfireForTesting;
	if (!bMisfire && ColdLevel >= 60.0f)
	{
		const float Denom = FMath::Max(1.0f, 100.0f - 60.0f);
		const float MisfireT = FMath::Clamp((ColdLevel - 60.0f) / Denom, 0.0f, 1.0f);
		const float MisfireChance = 0.35f * MisfireT;
		if (FMath::FRand() < MisfireChance)
		{
			bMisfire = true;
		}
	}

	if (bMisfire)
	{
		MisfireCooldownTimer = 1.5f;
		OnWeaponMisfired.Broadcast(this);
		OnWeaponMisfiredNative.Broadcast(this);
		return false;
	}

	ShootTimer = CurrentWeapon ? CurrentWeapon->FireRate : 0.65f;

	const float Dist = FVector::Dist2D(GetActorLocation(), Target->GetActorLocation());
	const float DistM = Dist / 100.0f;
	const int32 DistCells = FMath::Max(1, FMath::RoundToInt(DistM / 1.5f));

	float HitChance = CurrentWeapon ? CurrentWeapon->GetHitChanceForDistance(DistCells) : 0.85f;
	if (ColdLevel > 50.0f)
	{
		const float ColdPenalty = 0.40f * ((ColdLevel - 50.0f) / 50.0f);
		HitChance = FMath::Clamp(HitChance - ColdPenalty, 0.05f, 0.99f);
	}

	const bool bHit = bForceHitForTesting || (FMath::FRand() <= HitChance);
	OnWeaponFired.Broadcast(this, Target, bHit);
	OnWeaponFiredNative.Broadcast(this, Target, bHit);

	if (bHit)
	{
		FDamageSpec Spec;
		Spec.Amount = CurrentWeapon ? CurrentWeapon->BaseDamage : 18.0f;
		Spec.DamageType = CurrentWeapon ? CurrentWeapon->DamageType : EDamageType::Kinetic;
		Spec.ArmorPenetration = CurrentWeapon ? CurrentWeapon->ArmorPenetration : 0.20f;
		Spec.AttackerSource = DisplayName.ToString();
		if (CurrentWeapon)
		{
			Spec.StatusEffect = CurrentWeapon->StatusEffect;
			Spec.StatusDuration = CurrentWeapon->StatusDuration;
			Spec.StatusTickDamage = CurrentWeapon->StatusTickDamage;
		}

		if (UHealthComponent* TargetHealth = Target->FindComponentByClass<UHealthComponent>())
		{
			TargetHealth->TakeDamage(Spec);
		}
	}

	return bHit;
}
