#include "Characters/OperativeCharacter.h"
#include "Characters/OperativeAIController.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
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
#include "Survival/ColdSurvivalComponent.h"
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
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

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
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BaseMat(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(GetCapsuleComponent());
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	BodyMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	BodyMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	BodyMesh->SetRelativeScale3D(StandingShape.PlaceholderScale);
	if (CylinderMesh.Succeeded())
	{
		BodyMesh->SetStaticMesh(CylinderMesh.Object);
	}
	if (BaseMat.Succeeded())
	{
		BodyMesh->SetMaterial(0, BaseMat.Object);
	}

	FacingMarker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FacingMarker"));
	FacingMarker->SetupAttachment(GetCapsuleComponent());
	FacingMarker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FacingMarker->SetRelativeLocation(StandingShape.MarkerOffset - FVector(0.f, 0.f, StandingShape.CapsuleHalfHeight));
	FacingMarker->SetRelativeScale3D(FVector(0.3f, 0.15f, 0.15f));
	if (CubeMesh.Succeeded())
	{
		FacingMarker->SetStaticMesh(CubeMesh.Object);
	}
	if (BaseMat.Succeeded())
	{
		FacingMarker->SetMaterial(0, BaseMat.Object);
	}

	// Godot capsule 2.0 / 1.3 / 0.7 m -> 180 / 117 / 63 cm; prone is clamped to the capsule radius.
	CrouchingShape.CapsuleHalfHeight = 58.5f;
	CrouchingShape.PlaceholderScale = FVector(0.7f, 0.7f, 1.17f);
	CrouchingShape.PlaceholderCenterHeight = 58.5f;
	CrouchingShape.MarkerOffset = FVector(40.f, 0.f, 95.f);
	// Placeholder lies along the facing direction so prone reads clearly from the tactical camera.
	ProneShape.CapsuleHalfHeight = 40.f;
	ProneShape.PlaceholderScale = FVector(0.45f, 0.7f, 1.6f);
	ProneShape.PlaceholderRotation = FRotator(90.f, 0.f, 0.f);
	ProneShape.PlaceholderCenterHeight = 22.5f;
	ProneShape.MarkerOffset = FVector(95.f, 0.f, 30.f);

	// Skeletal mesh defaults for UE5-style skeletons (feet at the capsule bottom, +Y forward rigs face +X).
	GetMesh()->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -90.f), FRotator(0.f, -90.f, 0.f));

	WeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
	WeaponMesh->SetupAttachment(GetMesh(), WeaponSocket);
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	ColdSurvival = CreateDefaultSubobject<UColdSurvivalComponent>(TEXT("ColdSurvival"));
	HealthComponent = CreateDefaultSubobject<UHealthComponent>(TEXT("HealthComponent"));
	HealthComponent->MaxHealth = 100.0f;
	HealthComponent->BaseArmorReduction = 0.10f;
}

void AOperativeCharacter::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdatePlaceholderVisibility();
	UpdatePlaceholderPose(1.f);
}

void AOperativeCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (USkeletalMeshComponent* SkeletalMesh = GetMesh())
	{
		MeshBaseZ = SkeletalMesh->GetRelativeLocation().Z;
		bMeshBaseCaptured = true;
		if (WeaponMesh)
		{
			WeaponMesh->AttachToComponent(SkeletalMesh, FAttachmentTransformRules::KeepRelativeTransform, WeaponSocket);
		}
	}
	UpdatePlaceholderVisibility();
	ApplyStanceCapsule();
	UpdatePlaceholderPose(1.f);

	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	if (BodyMesh)
	{
		BodyMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	}

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

	ApplyBodyColor();

	if (USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>())
	{
		Squad->RegisterOperative(this);
	}
}

void AOperativeCharacter::SetSquadIdentity(int32 InSquadIndex, const FText& InName, const FLinearColor& InColor)
{
	SquadIndex = InSquadIndex;
	DisplayName = InName;
	BodyColor = InColor;
	ApplyBodyColor();
}

void AOperativeCharacter::ApplyBodyColor()
{
	// Default color if unassigned: determine from SquadIndex (matching Godot)
	if (BodyColor.IsAlmostBlack() || BodyColor.Equals(FLinearColor(0.2f, 0.5f, 1.f)))
	{
		switch (SquadIndex)
		{
		case 0: // Commander - Military Blue
			BodyColor = FLinearColor::FromSRGBColor(FColor(0x20, 0x80, 0xEC));
			if (DisplayName.IsEmpty()) DisplayName = NSLOCTEXT("CodexTactics", "Commander", "Командир");
			break;
		case 1: // Engineer - Hazard Orange
			BodyColor = FLinearColor::FromSRGBColor(FColor(0xFF, 0x61, 0x0F));
			if (DisplayName.IsEmpty()) DisplayName = NSLOCTEXT("CodexTactics", "Engineer", "Инженер");
			break;
		case 2: // Medic-Sapper - Field Medic Green
			BodyColor = FLinearColor::FromSRGBColor(FColor(0x1F, 0xB3, 0x33));
			if (DisplayName.IsEmpty()) DisplayName = NSLOCTEXT("CodexTactics", "Medic", "Медик-сапёр");
			break;
		default:
			break;
		}
	}

	if (!UsesPlaceholderBody())
	{
		const int32 SlotIndex = GetMesh()->GetMaterialIndex(RoleColorMaterialSlot);
		if (SlotIndex != INDEX_NONE)
		{
			if (UMaterialInstanceDynamic* RoleMaterial = GetMesh()->CreateDynamicMaterialInstance(SlotIndex))
			{
				RoleMaterial->SetVectorParameterValue(RoleColorParameter, BodyColor);
			}
		}
	}

	if (BodyMesh)
	{
		BodyMaterial = BodyMesh->CreateAndSetMaterialInstanceDynamic(0);
		if (BodyMaterial)
		{
			BodyMaterial->SetVectorParameterValue(TEXT("Color"), BodyColor);
		}
	}
	if (FacingMarker)
	{
		UMaterialInstanceDynamic* MarkerMat = FacingMarker->CreateAndSetMaterialInstanceDynamic(0);
		if (MarkerMat)
		{
			MarkerMat->SetVectorParameterValue(TEXT("Color"), BodyColor * 1.4f + FLinearColor(0.15f, 0.15f, 0.15f, 1.0f));
		}
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
	const EOperativeOrderResult Result = RequestMove(Destination);
	UE_LOG(LogCodexTactics, Display, TEXT("%s: move to (%.0f, %.0f)%s -> %s"), *DisplayName.ToString(), Destination.X, Destination.Y,
		bSprinting ? TEXT(" sprint") : TEXT(""), Result == EOperativeOrderResult::Accepted ? TEXT("accepted")
		: Result == EOperativeOrderResult::Unreachable ? TEXT("unreachable") : TEXT("no controller"));
	return Result;
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
	// Godot: a frostbitten operative physically cannot get up from the snow.
	if (ColdSurvival && ColdSurvival->IsFrostbitten() && NewStance != EOperativeStance::Prone)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("%s: frostbitten, cannot leave prone"), *DisplayName.ToString());
		return;
	}
	const EOperativeStance OldStance = Stance;
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

	ApplyStanceCapsule();
	ApplyMovementParams();
	UE_LOG(LogCodexTactics, Display, TEXT("%s: stance %s -> %s"), *DisplayName.ToString(),
		*GetStanceDisplayName(OldStance).ToString(), *GetStanceDisplayName(Stance).ToString());
	OnStanceChanged.Broadcast(this, OldStance, Stance);
	ReceiveStanceChanged(OldStance, Stance);
}

const FOperativeStanceShape& AOperativeCharacter::GetStanceShape(EOperativeStance InStance) const
{
	switch (InStance)
	{
	case EOperativeStance::Crouching: return CrouchingShape;
	case EOperativeStance::Prone: return ProneShape;
	default: return StandingShape;
	}
}

FText AOperativeCharacter::GetStanceDisplayName(EOperativeStance InStance)
{
	switch (InStance)
	{
	case EOperativeStance::Crouching: return NSLOCTEXT("CodexTactics", "StanceCrouching", "СИДЯ");
	case EOperativeStance::Prone: return NSLOCTEXT("CodexTactics", "StanceProne", "ЛЁЖА");
	default: return NSLOCTEXT("CodexTactics", "StanceStanding", "СТОЯ");
	}
}

bool AOperativeCharacter::UsesPlaceholderBody() const
{
	const USkeletalMeshComponent* SkeletalMesh = GetMesh();
	return !SkeletalMesh || !SkeletalMesh->GetSkeletalMeshAsset();
}

void AOperativeCharacter::UpdatePlaceholderVisibility()
{
	const bool bPlaceholder = UsesPlaceholderBody();
	if (BodyMesh)
	{
		BodyMesh->SetVisibility(bPlaceholder);
		BodyMesh->SetCollisionEnabled(bPlaceholder ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	}
	if (FacingMarker)
	{
		FacingMarker->SetVisibility(bPlaceholder);
	}
	if (WeaponMesh)
	{
		WeaponMesh->SetVisibility(!bPlaceholder);
	}
}

void AOperativeCharacter::ApplyStanceCapsule()
{
	UCapsuleComponent* Capsule = GetCapsuleComponent();
	const float OldHalf = Capsule->GetUnscaledCapsuleHalfHeight();
	const float NewHalf = FMath::Max(GetStanceShape(Stance).CapsuleHalfHeight, Capsule->GetUnscaledCapsuleRadius());
	if (!FMath::IsNearlyEqual(OldHalf, NewHalf))
	{
		Capsule->SetCapsuleHalfHeight(NewHalf);
		// Keep the feet where they were.
		AddActorWorldOffset(FVector(0.f, 0.f, (NewHalf - OldHalf) * Capsule->GetShapeScale()), false, nullptr, ETeleportType::TeleportPhysics);
	}
	if (bMeshBaseCaptured)
	{
		FVector MeshLocation = GetMesh()->GetRelativeLocation();
		MeshLocation.Z = MeshBaseZ + (StandingShape.CapsuleHalfHeight - NewHalf);
		GetMesh()->SetRelativeLocation(MeshLocation);
	}
}

void AOperativeCharacter::UpdatePlaceholderPose(float Alpha)
{
	if (!BodyMesh || !FacingMarker || !BodyMesh->IsVisible())
	{
		return;
	}
	const FOperativeStanceShape& Shape = GetStanceShape(Stance);
	const float FeetZ = -GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	const FVector BodyLocation(0.f, 0.f, FeetZ + Shape.PlaceholderCenterHeight);
	const FVector MarkerLocation = Shape.MarkerOffset + FVector(0.f, 0.f, FeetZ);
	const FQuat BodyRotation = FQuat::Slerp(BodyMesh->GetRelativeRotation().Quaternion(), Shape.PlaceholderRotation.Quaternion(), Alpha);
	BodyMesh->SetRelativeLocationAndRotation(FMath::Lerp(BodyMesh->GetRelativeLocation(), BodyLocation, Alpha), BodyRotation);
	BodyMesh->SetRelativeScale3D(FMath::Lerp(BodyMesh->GetRelativeScale3D(), Shape.PlaceholderScale, Alpha));
	FacingMarker->SetRelativeLocation(FMath::Lerp(FacingMarker->GetRelativeLocation(), MarkerLocation, Alpha));
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

int32 AOperativeCharacter::GetDeployableCount(EDeployableType Type) const
{
	switch (Type)
	{
	case EDeployableType::Turret: return TurretsCount;
	case EDeployableType::Barricade: return BarricadesCount;
	default: return MinesCount;
	}
}

void AOperativeCharacter::AddDeployable(EDeployableType Type, int32 Delta)
{
	int32& Count = Type == EDeployableType::Turret ? TurretsCount : (Type == EDeployableType::Barricade ? BarricadesCount : MinesCount);
	Count = FMath::Clamp(Count + Delta, 0, DeployableRules::GetMaxCarried(Type));
}

EDeployableType AOperativeCharacter::CycleDeployableType()
{
	for (int32 Step = 1; Step <= 3; ++Step)
	{
		const EDeployableType Next = static_cast<EDeployableType>((static_cast<int32>(SelectedDeployType) + Step) % 3);
		if (GetDeployableCount(Next) > 0 || Step == 3)
		{
			SelectedDeployType = Next;
			break;
		}
	}
	return SelectedDeployType;
}

void AOperativeCharacter::SetCarrying(bool bNewCarrying)
{
	bCarrying = bNewCarrying;
	if (bCarrying)
	{
		bSprinting = false; // Godot can_sprint: no sprint while carrying
	}
	ApplyMovementParams();
}

bool AOperativeCharacter::CanSprint() const
{
	return !bCarrying && OperativeMovementRules::CanSprint(MovementConfig, Stance, ColdLevel, bWounded);
}

float AOperativeCharacter::GetMaxSpeed() const
{
	return OperativeMovementRules::ComputeMaxSpeed(MovementConfig, Stance, bSprinting, bWounded, bCarrying) * ColdSpeedMultiplier;
}

void AOperativeCharacter::SetColdSpeedMultiplier(float Multiplier)
{
	if (!FMath::IsNearlyEqual(ColdSpeedMultiplier, Multiplier))
	{
		ColdSpeedMultiplier = Multiplier;
		ApplyMovementParams();
	}
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
	UpdatePlaceholderPose(1.f - FMath::Exp(-StanceBlendSpeed * DeltaTime));
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

	// Godot start_reload: radio callout asking for cover.
	static const TCHAR* Callouts[] = {
		TEXT("🔄 Перезаряжаюсь! Прикройте меня!"),
		TEXT("🔄 Пустой магазин! Прикройте сектор!"),
		TEXT("🔄 Меняю обойму, держите их!"),
		TEXT("🔄 Перезарядка! Прикройте спину!") };
	if (UWorld* World = GetWorld())
	{
		if (UGameMessageSubsystem* Messages = World->GetSubsystem<UGameMessageSubsystem>())
		{
			Messages->PostMessage(DisplayName, FText::FromString(Callouts[FMath::RandHelper(UE_ARRAY_COUNT(Callouts))]));
		}
	}
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
	// Godot: a weapon frozen at >= 90 % cold cannot fire until it thaws.
	if (ColdSurvival && ColdSurvival->IsWeaponFrozen())
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

	// Cold misfire (Godot player.gd `_shoot_at_target`, balance.tres: >= 60 %, linear up to 30 %, never near heat).
	bool bMisfire = bForceMisfireForTesting;
	if (!bMisfire && ColdSurvival && FMath::FRand() < ColdSurvival->GetMisfireChance())
	{
		bMisfire = true;
	}

	if (bMisfire)
	{
		MisfireCooldownTimer = ColdSurvival ? ColdSurvival->Config.MisfireDelay : 0.45f;
		OnWeaponMisfired.Broadcast(this);
		OnWeaponMisfiredNative.Broadcast(this);
		return false;
	}

	ShootTimer = CurrentWeapon ? CurrentWeapon->FireRate : 0.65f;

	const float Dist = FVector::Dist2D(GetActorLocation(), Target->GetActorLocation());
	const float DistM = Dist / 100.0f;
	const int32 DistCells = FMath::Max(1, FMath::RoundToInt(DistM / 1.5f));

	float HitChance = CurrentWeapon ? CurrentWeapon->GetHitChanceForDistance(DistCells) : 0.85f;
	// Shivering hands above 50 % cold (Godot realtime_cold_aim_penalty_max 0.30).
	if (ColdSurvival && ColdLevel > 50.0f)
	{
		HitChance = FMath::Clamp(HitChance - ColdSurvival->GetAimPenalty(), 0.05f, 0.99f);
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
