#include "Characters/OperativeCharacter.h"
#include "UI/FloatingTextSubsystem.h"
#include "Characters/OperativeAIController.h"
#include "Characters/RageComponent.h"
#include "Combat/GrenadeSubsystem.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/CombatFeedbackSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Core/MissionSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Data/WeaponDataAsset.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Interactables/BarricadeActor.h"
#include "Interactables/BarrelActor.h"
#include "Interactables/LootCrateActor.h"
#include "Interactables/ProximityMineActor.h"
#include "Interactables/TurretActor.h"
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
	RageComponent = CreateDefaultSubobject<URageComponent>(TEXT("RageComponent"));
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
		HealthComponent->OnHealthChanged.AddDynamic(this, &AOperativeCharacter::HandleHealthChanged);
	}
	CaptureProgressionBases();

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->MaxAcceleration = MovementConfig.Acceleration;
	Movement->BrakingDecelerationWalking = MovementConfig.Deceleration;
	Movement->GetNavMovementProperties()->bUseFixedBrakingDistanceForPaths = true;
	Movement->GetNavMovementProperties()->FixedPathBrakingDistance = MovementConfig.PathBrakingDistance;
	ApplyMovementParams();

	ApplyBodyColor();

	USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	if (Squad && bRecruited)
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

bool AOperativeCharacter::IsRaging() const
{
	return RageComponent && RageComponent->IsRaging();
}

EOperativeOrderResult AOperativeCharacter::OrderMoveTo(const FVector& Destination, bool bSprint)
{
	if (IsRaging())
	{
		UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("⚠️ В ЯРОСТИ! НЕ ПОДЧИНЯЕТСЯ!"), FLinearColor(1.f, 0.4f, 0.1f));
		return EOperativeOrderResult::Refused;
	}
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
	if (RageComponent)
	{
		RageComponent->ExitRage(TEXT("Погиб"));
	}
	// Godot _check_squad_vital_signs: any squad member down = mission failed (HQ line, time stop, failed screen).
	if (UMissionSubsystem* Mission = GetWorld()->GetSubsystem<UMissionSubsystem>())
	{
		Mission->TriggerMissionFailed(this);
	}
	else if (UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>())
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

	// Godot _on_movement_destination_reached: behind a barricade in combat (not frostbitten) the operative takes cover.
	const UGameFlowSubsystem* Flow = GetWorld() ? GetWorld()->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	const bool bCombat = (Flow && Flow->GetPhase() != ECodexGamePhase::Exploration) || CurrentCombatTarget.IsValid();
	if (!bCombat || Stance == EOperativeStance::Crouching || (ColdSurvival && ColdSurvival->IsFrostbitten()) || !IsBehindBarricade())
	{
		return;
	}
	SetStance(EOperativeStance::Crouching);
	UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("🛡️ В УКРЫТИИ (-35% урона)"), FLinearColor(0.3f, 0.9f, 1.f));
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now - LastCoverChatterTime >= 5.0)
	{
		LastCoverChatterTime = Now;
		const TCHAR* Callouts[] = { TEXT("🛡️ Занял укрытие!"), TEXT("🛡️ В укрытии, сектор держу!"),
			TEXT("🛡️ Укрылся за баррикадой, готов к бою!"), TEXT("🛡️ На позиции за щитом, веду наблюдение!") };
		if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
		{
			Messages->PostMessage(DisplayName, FText::FromString(Callouts[FMath::RandRange(0, 3)]));
		}
	}
}

bool AOperativeCharacter::IsBehindBarricade() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	const FVector Centre = GetActorLocation() - FVector(0.f, 0.f, GetSimpleCollisionHalfHeight() - 100.f);
	for (TActorIterator<ABarricadeActor> It(World); It; ++It)
	{
		const UHealthComponent* Health = It->FindComponentByClass<UHealthComponent>();
		if (!IsValid(*It) || (Health && !Health->IsAlive()))
		{
			continue;
		}
		FVector Origin;
		FVector Extent;
		It->GetActorBounds(true, Origin, Extent);
		if (FVector::Dist(Centre, FVector(Origin.X, Origin.Y, Origin.Z - Extent.Z)) <= 220.f)
		{
			return true;
		}
	}
	return false;
}

void AOperativeCharacter::SetFacingPoint(const FVector& Point)
{
	StopOperative();
	const FVector Direction = (Point - GetActorLocation()).GetSafeNormal2D();
	if (!Direction.IsNearlyZero())
	{
		SetActorRotation(FRotator(0.f, Direction.Rotation().Yaw, 0.f));
	}
}

int32 AOperativeCharacter::GetItemCount(EPersonalItem Item) const
{
	switch (Item)
	{
	case EPersonalItem::Medkit: return MedkitsCount;
	case EPersonalItem::CannedFood: return CannedFoodCount;
	case EPersonalItem::Bread: return BreadCount;
	default: return ChocolateCount;
	}
}

bool AOperativeCharacter::UsePersonalItem(EPersonalItem Item)
{
	if (GetItemCount(Item) <= 0 || !HealthComponent
		|| !PersonalItemRules::CanUse(HealthComponent->GetCurrentHealth(), HealthComponent->GetMaxHealth(), ColdLevel))
	{
		return false;
	}
	const FPersonalItemEffect Effect = PersonalItemRules::GetEffect(Item);
	const float HealthBefore = HealthComponent->GetCurrentHealth();
	HealthComponent->Heal(Effect.Heal);
	const int32 Gained = FMath::FloorToInt(HealthComponent->GetCurrentHealth() - HealthBefore);
	if (UFloatingTextSubsystem* Floating = Gained > 0 && GetWorld() ? GetWorld()->GetSubsystem<UFloatingTextSubsystem>() : nullptr)
	{
		Floating->Spawn(GetActorLocation() - FVector(0.f, 0.f, GetSimpleCollisionHalfHeight() - 210.f), FString::Printf(TEXT("+%d HP"), Gained),
			FLinearColor(0.2f, 1.f, 0.4f), 0.9f, 70.f);
	}
	ColdLevel = FMath::Max(0.f, ColdLevel - Effect.Warmth);
	int32& Count = Item == EPersonalItem::Medkit ? MedkitsCount
		: (Item == EPersonalItem::CannedFood ? CannedFoodCount : (Item == EPersonalItem::Bread ? BreadCount : ChocolateCount));
	--Count;
	return true;
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

bool AOperativeCharacter::IsWounded() const
{
	// Godot is_wounded: below the wounded threshold of max health (bWounded forces it, e.g. in tests).
	return bWounded || (HealthComponent && HealthComponent->GetMaxHealth() > 0.f
		&& HealthComponent->GetCurrentHealth() / HealthComponent->GetMaxHealth() < MovementConfig.WoundedHealthThreshold);
}

bool AOperativeCharacter::CanSprint() const
{
	return !bCarrying && OperativeMovementRules::CanSprint(MovementConfig, Stance, ColdLevel, IsWounded());
}

float AOperativeCharacter::GetMaxSpeed() const
{
	return OperativeMovementRules::ComputeMaxSpeed(MovementConfig, Stance, bSprinting, IsWounded(), bCarrying) * ColdSpeedMultiplier;
}

void AOperativeCharacter::HandleHealthChanged(float NewHealth, float MaxHealth, float Delta)
{
	if (bSprinting && !CanSprint())
	{
		bSprinting = false;
	}
	ApplyMovementParams(); // the wounded speed follows the health
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

void AOperativeCharacter::InitArsenal(const TArray<UWeaponDataAsset*>& Weapons, int32 RifleReserve)
{
	AvailableWeapons.Reset();
	AmmoInventory.Reset();
	for (UWeaponDataAsset* Weapon : Weapons)
	{
		if (!Weapon)
		{
			continue;
		}
		AvailableWeapons.Add(Weapon);
		FWeaponAmmoState& Ammo = AmmoInventory.Add(Weapon->WeaponId);
		if (Weapon->WeaponId == TEXT("grenade"))
		{
			Ammo.Clip = GrenadesCount > 0 ? 1 : 0;
			Ammo.Reserve = FMath::Max(0, GrenadesCount - 1);
		}
		else
		{
			Ammo.Clip = Weapon->MaxClipSize;
			Ammo.Reserve = Weapon->WeaponId == TEXT("m16") ? RifleReserve : (Weapon->WeaponId == TEXT("pistol") ? 24 : 0);
		}
	}
	// Rounds picked up before the arsenal existed (loot) join their weapon.
	for (auto It = ExtraAmmo.CreateIterator(); It; ++It)
	{
		if (FWeaponAmmoState* Ammo = AmmoInventory.Find(It.Key().ToString()))
		{
			Ammo->Reserve += It.Value();
			It.RemoveCurrent();
		}
	}
	CurrentWeapon = nullptr;
	if (!AvailableWeapons.IsEmpty())
	{
		SwitchToWeaponById(AvailableWeapons[0]->WeaponId);
	}
}

bool AOperativeCharacter::SwitchToWeaponById(const FString& WeaponId)
{
	const TObjectPtr<UWeaponDataAsset>* Found = AvailableWeapons.FindByPredicate([&WeaponId](const UWeaponDataAsset* Weapon)
	{
		return Weapon && Weapon->WeaponId == WeaponId;
	});
	if (!Found)
	{
		return false;
	}
	if (CurrentWeapon)
	{
		AmmoInventory.FindOrAdd(CurrentWeapon->WeaponId) = { CurrentClip, ReserveAmmo };
	}
	CurrentWeapon = *Found;
	FWeaponAmmoState Ammo = AmmoInventory.FindRef(WeaponId);
	if (WeaponId == TEXT("grenade"))
	{
		// Grenades are also spent on traps: the count is the truth.
		Ammo = { GrenadesCount > 0 ? 1 : 0, FMath::Max(0, GrenadesCount - 1) };
	}
	CurrentClip = Ammo.Clip;
	ReserveAmmo = Ammo.Reserve;
	bIsReloading = false;
	ReloadTimer = 0.0f;
	ShootTimer = 0.0f;
	return true;
}

FWeaponAmmoState AOperativeCharacter::GetAmmoState(const FString& WeaponId) const
{
	if (CurrentWeapon && CurrentWeapon->WeaponId == WeaponId)
	{
		return { CurrentClip, ReserveAmmo };
	}
	return AmmoInventory.FindRef(WeaponId);
}

void AOperativeCharacter::AddAmmo(const FString& WeaponId, int32 Count)
{
	if (CurrentWeapon && CurrentWeapon->WeaponId == WeaponId)
	{
		ReserveAmmo += Count;
	}
	else if (FWeaponAmmoState* Ammo = AmmoInventory.Find(WeaponId))
	{
		Ammo->Reserve += Count;
	}
	else
	{
		ExtraAmmo.FindOrAdd(FName(*WeaponId)) += Count;
	}
}

int32 AOperativeCharacter::GetReserve(const FString& WeaponId) const
{
	if (CurrentWeapon && CurrentWeapon->WeaponId == WeaponId)
	{
		return ReserveAmmo;
	}
	if (const FWeaponAmmoState* Ammo = AmmoInventory.Find(WeaponId))
	{
		return Ammo->Reserve;
	}
	return ExtraAmmo.FindRef(FName(*WeaponId));
}

int32 AOperativeCharacter::TakeReserve(const FString& WeaponId, int32 Max)
{
	int32* Reserve = nullptr;
	if (CurrentWeapon && CurrentWeapon->WeaponId == WeaponId)
	{
		Reserve = &ReserveAmmo;
	}
	else if (FWeaponAmmoState* Ammo = AmmoInventory.Find(WeaponId))
	{
		Reserve = &Ammo->Reserve;
	}
	else
	{
		Reserve = ExtraAmmo.Find(FName(*WeaponId));
	}
	if (!Reserve)
	{
		return 0;
	}
	const int32 Taken = FMath::Clamp(Max, 0, *Reserve);
	*Reserve -= Taken;
	return Taken;
}

bool AOperativeCharacter::UsesAmmo() const
{
	return !CurrentWeapon || CurrentWeapon->bUsesAmmo;
}

void AOperativeCharacter::StartReload()
{
	if (bIsReloading || ReserveAmmo <= 0 || !UsesAmmo())
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
	if (bSprinting || bCarrying || (bIsReloading && !IsRaging())) // Godot: rage ignores the reload
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

namespace
{
	FVector ShotFeet(const AActor* Actor)
	{
		const ACharacter* Character = Cast<ACharacter>(Actor);
		return Actor->GetActorLocation() - FVector(0.f, 0.f, Character ? Character->GetSimpleCollisionHalfHeight() : 0.f);
	}

	bool IsLiveEnemy(const AActor* Actor)
	{
		if (!IsValid(Actor) || !Actor->ActorHasTag(FName(TEXT("Enemy"))) || Actor->IsHidden())
		{
			return false;
		}
		const UHealthComponent* Health = Actor->FindComponentByClass<UHealthComponent>();
		return !Health || Health->IsAlive();
	}
}

bool AOperativeCharacter::EvaluateShotLine(AActor* Enemy, bool bKeepTarget, FShootCandidate& Out, bool& bOutBarricadeBlocked) const
{
	bOutBarricadeBlocked = false;
	UWorld* World = GetWorld();
	const FVector MyFeet = ShotFeet(this);
	const FVector EnemyFeet = ShotFeet(Enemy);
	if (!World || SquadFireRules::IsInDeadZone(MyFeet, EnemyFeet))
	{
		return false;
	}
	const FElevationAdvantage Elevation = SquadFireRules::GetElevationAdvantage(MyFeet.Z, EnemyFeet.Z);
	const float Range = (CurrentWeapon ? CurrentWeapon->AttackRangeCm : 1400.f) * SquadFireRules::GetPostureRangeMultiplier(Stance)
		* (Elevation.bElevated ? Elevation.RangeMultiplier : 1.f);
	// Godot: from the operative's centre (1 m above the feet) to the enemy's feet.
	const float Distance = FVector::Dist(MyFeet + FVector(0.f, 0.f, 100.f), EnemyFeet);
	if (Distance > Range)
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SquadShotLine), false, this);
	for (TActorIterator<AOperativeCharacter> It(World); It; ++It)
	{
		Params.AddIgnoredActor(*It); // the squad
	}
	for (TActorIterator<ATurretActor> It(World); It; ++It)
	{
		Params.AddIgnoredActor(*It); // Godot "allies"
	}
	EShotLineHit Kind = EShotLineHit::Clear;
	AActor* HitEnemy = Enemy;
	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, GetMuzzleLocation(), EnemyFeet + FVector(0.f, 0.f, 80.f), ECC_Visibility, Params))
	{
		AActor* Blocker = Hit.GetActor();
		if (Blocker && Blocker != Enemy)
		{
			if (Blocker->ActorHasTag(FName(TEXT("Enemy"))))
			{
				HitEnemy = bKeepTarget ? Enemy : Blocker;
			}
			else
			{
				Kind = Blocker->IsA<ABarricadeActor>() ? EShotLineHit::Barricade : EShotLineHit::Blocked;
			}
		}
	}
	const FShotLineVerdict Verdict = SquadFireRules::JudgeLine(Kind, Stance, Elevation);
	bOutBarricadeBlocked = Verdict.bBarricadeBlocked;
	if (!Verdict.bCanHit)
	{
		return false;
	}
	Out.Enemy = HitEnemy;
	Out.Distance = Distance;
	Out.Cover = Verdict.Cover;
	return true;
}

AActor* AOperativeCharacter::FindBestCombatTarget() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	FShootCandidate Candidate;
	bool bBlocked = false;
	if (AActor* Priority = ManualPriorityTarget.Get())
	{
		if (!IsLiveEnemy(Priority))
		{
			ManualPriorityTarget.Reset();
		}
		else if (EvaluateShotLine(Priority, true, Candidate, bBlocked))
		{
			return Priority;
		}
	}
	AActor* Best = nullptr;
	float BestDistance = TNumericLimits<float>::Max();
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (IsLiveEnemy(*It) && EvaluateShotLine(*It, false, Candidate, bBlocked) && Candidate.Distance < BestDistance)
		{
			BestDistance = Candidate.Distance;
			Best = Candidate.Enemy;
		}
	}
	return Best;
}

FShootCandidate AOperativeCharacter::FindShootTarget(float DeltaTime)
{
	UWorld* World = GetWorld();
	FShootCandidate Candidate;
	bool bBlocked = false;
	// 1. The manual priority target (Ctrl + click) while it can be hit.
	if (AActor* Priority = ManualPriorityTarget.Get())
	{
		if (!IsLiveEnemy(Priority))
		{
			ManualPriorityTarget.Reset();
		}
		else if (EvaluateShotLine(Priority, true, Candidate, bBlocked))
		{
			CurrentCombatTarget = Priority;
			PendingFlankTarget.Reset();
			TargetSwitchTimer = 0.f;
			return Candidate;
		}
		else if (bBlocked)
		{
			NotifyBarricadeBlocked();
		}
	}
	// 2. Visible enemies, closest first.
	TArray<FShootCandidate> Visible;
	int32 BarricadeBlocked = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (!IsLiveEnemy(*It))
		{
			continue;
		}
		if (EvaluateShotLine(*It, false, Candidate, bBlocked))
		{
			Visible.Add(Candidate);
		}
		else if (bBlocked)
		{
			++BarricadeBlocked;
		}
	}
	if (Visible.IsEmpty())
	{
		CurrentCombatTarget.Reset();
		PendingFlankTarget.Reset();
		TargetSwitchTimer = 0.f;
		if (Stance == EOperativeStance::Prone && BarricadeBlocked > 0)
		{
			NotifyBarricadeBlocked();
		}
		return FShootCandidate();
	}
	Visible.Sort([](const FShootCandidate& A, const FShootCandidate& B) { return A.Distance < B.Distance; });
	const FShootCandidate& Closest = Visible[0];
	const FShootCandidate* Current = IsLiveEnemy(CurrentCombatTarget.Get())
		? Visible.FindByPredicate([this](const FShootCandidate& Entry) { return Entry.Enemy == CurrentCombatTarget.Get(); }) : nullptr;
	if (!Current)
	{
		CurrentCombatTarget = Closest.Enemy;
		PendingFlankTarget.Reset();
		TargetSwitchTimer = 0.f;
		return Closest;
	}
	// A much closer enemy (flank) takes over after the stance's reaction delay.
	const int32 StanceIndex = Stance == EOperativeStance::Prone ? 2 : (Stance == EOperativeStance::Crouching ? 1 : 0);
	if (Closest.Enemy != CurrentCombatTarget.Get() && SquadFireRules::IsSignificantlyCloser(Closest.Distance, Current->Distance, FireConfig.SwitchRatio[StanceIndex]))
	{
		if (PendingFlankTarget.Get() != Closest.Enemy)
		{
			PendingFlankTarget = Closest.Enemy;
			TargetSwitchTimer = FireConfig.SwitchDelay[StanceIndex];
		}
		TargetSwitchTimer -= DeltaTime;
		if (TargetSwitchTimer <= 0.f)
		{
			CurrentCombatTarget = PendingFlankTarget;
			PendingFlankTarget.Reset();
			TargetSwitchTimer = 0.f;
			return Closest;
		}
	}
	else
	{
		PendingFlankTarget.Reset();
		TargetSwitchTimer = 0.f;
	}
	return *Current;
}

void AOperativeCharacter::NotifyBarricadeBlocked()
{
	if (BarricadeBlockNotifyTimer > 0.f)
	{
		return;
	}
	BarricadeBlockNotifyTimer = 3.5f;
	UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("🚫 Баррикада блокирует огонь (нужно сесть)!"), FLinearColor(1.f, 0.75f, 0.2f));
}

void AOperativeCharacter::ProcessCombatShooting(float DeltaTime)
{
	if (!bRecruited || bTacticalCeaseFire || !HealthComponent || !HealthComponent->IsAlive()) // Godot can_shoot = false
	{
		return;
	}

	if (MisfireCooldownTimer > 0.0f)
	{
		MisfireCooldownTimer -= DeltaTime;
	}

	const bool bRaging = IsRaging();
	if (bIsReloading && !bRaging) // Godot: no reloading while raging
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
	WeaponFreezeNotifyTimer = FMath::Max(0.f, WeaponFreezeNotifyTimer - DeltaTime);
	BarricadeBlockNotifyTimer = FMath::Max(0.f, BarricadeBlockNotifyTimer - DeltaTime);
	AIGrenadeCooldown = FMath::Max(0.f, AIGrenadeCooldown - DeltaTime);

	if (!CanShoot())
	{
		// Godot _can_fire: a frozen weapon says so while there is something to shoot at.
		const UGameFlowSubsystem* FrozenFlow = GetWorld() ? GetWorld()->GetSubsystem<UGameFlowSubsystem>() : nullptr;
		if (ColdSurvival && ColdSurvival->IsWeaponFrozen() && WeaponFreezeNotifyTimer <= 0.f && FrozenFlow
			&& FrozenFlow->GetPhase() == ECodexGamePhase::WaveCombat && FrozenFlow->GetCombatMode() == ECodexCombatMode::RealTime
			&& FindBestCombatTarget())
		{
			NotifyWeaponFrozen();
		}
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

	// Godot: a clustered pack gets a grenade before the rifle (not while raging or reloading).
	if (!bRaging && !bIsReloading && GrenadesCount > 0 && AIGrenadeCooldown <= 0.f && TryAIGrenadeThrow())
	{
		ShootTimer = 1.f;
		return;
	}

	const bool bInfiniteRageAmmo = bRaging && RageComponent->Config.bInfiniteAmmo;
	if (UsesAmmo() && CurrentClip <= 0 && !bInfiniteRageAmmo)
	{
		if (ReserveAmmo > 0)
		{
			StartReload();
		}
		else
		{
			AutoSwitchOnEmpty();
		}
		return;
	}

	// Raging: a random enemy in reach is sprayed (Godot rage_comp.get_chaotic_target, cover 1).
	FShootCandidate Shot;
	if (AActor* Chaotic = bRaging ? RageComponent->GetChaoticTarget() : nullptr)
	{
		Shot.Enemy = Chaotic;
	}
	else
	{
		Shot = FindShootTarget(DeltaTime);
	}
	AActor* Target = Shot.Enemy;
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
		ShootAtTarget(Target, Shot.Cover);
	}
}

bool AOperativeCharacter::ShootAtTarget(AActor* Target, float Cover)
{
	if (!Target || (UsesAmmo() && CurrentClip <= 0))
	{
		return false;
	}

	const bool bRagingShot = IsRaging();
	if (UsesAmmo() && !(bRagingShot && RageComponent->Config.bInfiniteAmmo))
	{
		CurrentClip--;
	}

	// Cold misfire (Godot player.gd `_shoot_at_target`, balance.tres: >= 60 %, linear up to 30 %, never near heat).
	bool bMisfire = bForceMisfireForTesting;
	if (!bMisfire && ColdSurvival && FMath::FRand() < ColdSurvival->GetMisfireChance())
	{
		bMisfire = true;
	}

	if (bMisfire)
	{
		MisfireCooldownTimer = ColdSurvival ? ColdSurvival->Config.MisfireDelay : 0.45f;
		UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("❄️ ОСЕЧКА! (Затвор заклинил)"), FLinearColor(0.4f, 0.85f, 1.f));
		OnWeaponMisfired.Broadcast(this);
		OnWeaponMisfiredNative.Broadcast(this);
		return false;
	}

	ShootTimer = FMath::Max(0.08f, (CurrentWeapon ? CurrentWeapon->FireRate : 0.65f) * (bRagingShot ? RageComponent->Config.FireRateMultiplier : 1.f));

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
	if (!bHit)
	{
		UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("ПРОМАХ!"), FLinearColor(0.75f, 0.75f, 0.75f));
	}
	OnWeaponFired.Broadcast(this, Target, bHit);
	OnWeaponFiredNative.Broadcast(this, Target, bHit);

	// Godot _spawn_muzzle_tracer: to the target, or deflected next to it on a miss.
	if (UCombatFeedbackSubsystem* Feedback = GetWorld() ? GetWorld()->GetSubsystem<UCombatFeedbackSubsystem>() : nullptr)
	{
		FVector End = Target->GetActorLocation();
		if (!bHit)
		{
			FVector Offset(FMath::FRandRange(-140.f, 140.f), FMath::FRandRange(-140.f, 140.f), FMath::FRandRange(20.f, 120.f));
			if (Offset.SizeSquared() < 50.f * 50.f)
			{
				Offset = FVector(100.f, 0.f, 50.f);
			}
			End += Offset;
		}
		Feedback->SpawnTracer(GetMuzzleLocation(), End, CurrentWeapon ? CurrentWeapon->TracerColor : UCombatFeedbackSubsystem::DefaultTracerColor(),
			CurrentWeapon ? CurrentWeapon->DamageType : EDamageType::Kinetic);
	}

	if (bHit)
	{
		// Godot _shoot_at_target: crit on luck (x2), elevation (+15 %), stance, cover and distance factors.
		const float CritRoll = ForcedCritRollForTesting >= 0.f ? (ForcedCritRollForTesting >= 1.f ? 0.f : 1.f) : FMath::FRand();
		ForcedCritRollForTesting = -1.f;
		const bool bCrit = SquadFireRules::IsCrit(Luck + (bRagingShot ? 30.f : 0.f), CritRoll); // rage: +30 luck
		const FElevationAdvantage Elevation = SquadFireRules::GetElevationAdvantage(
			GetActorLocation().Z - GetSimpleCollisionHalfHeight(), Target->GetActorLocation().Z - Target->GetSimpleCollisionHalfHeight());
		float DistanceMultiplier = 1.f;
		if (CurrentWeapon && !CurrentWeapon->DistanceDamageMultipliers.IsEmpty())
		{
			DistanceMultiplier = CurrentWeapon->DistanceDamageMultipliers[FMath::Clamp(DistCells - 1, 0, CurrentWeapon->DistanceDamageMultipliers.Num() - 1)];
		}
		if (bCrit)
		{
			UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("🎯 КРИТ x2!"), FLinearColor(1.f, 0.85f, 0.1f));
		}
		else if (Elevation.bElevated)
		{
			UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("⛰️ +15% ВЫСОТА"), FLinearColor(0.35f, 0.9f, 1.f));
		}

		FDamageSpec Spec;
		Spec.Amount = SquadFireRules::ComputeShotDamage(CurrentWeapon ? CurrentWeapon->BaseDamage : 18.0f, Stance, Cover, bCrit, Elevation, DistanceMultiplier)
			* (bRagingShot ? RageComponent->Config.DamageMultiplier : 1.f);
		Spec.bIsCritical = bCrit;
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
		// Cryo weapons chill the shooter, fire weapons warm him (Godot self_cold / self_warmth_generation).
		if (CurrentWeapon && CurrentWeapon->SelfColdGeneration > 0.f)
		{
			ColdLevel = FMath::Min(100.f, ColdLevel + CurrentWeapon->SelfColdGeneration);
		}
		if (CurrentWeapon && CurrentWeapon->SelfWarmthGeneration > 0.f)
		{
			ColdLevel = FMath::Max(0.f, ColdLevel - CurrentWeapon->SelfWarmthGeneration);
		}
	}

	return bHit;
}

// --- Ctrl + click targeted shots ---------------------------------------------------------------------------------

#define LOCTEXT_NAMESPACE "OperativeTargetedShots"

namespace
{
	void OperativeShotLine(const AOperativeCharacter& Operative, const FText& Text)
	{
		UWorld* World = Operative.GetWorld();
		if (UGameMessageSubsystem* Messages = World ? World->GetSubsystem<UGameMessageSubsystem>() : nullptr)
		{
			Messages->PostMessage(Operative.DisplayName, Text);
		}
	}
}

ETargetedShotKind AOperativeCharacter::ClassifyShotTarget(const AActor* Target)
{
	if (!IsValid(Target))
	{
		return ETargetedShotKind::None;
	}
	if (Target->ActorHasTag(FName(TEXT("Enemy"))))
	{
		const UHealthComponent* Health = Target->FindComponentByClass<UHealthComponent>();
		return Health && Health->IsAlive() ? ETargetedShotKind::Enemy : ETargetedShotKind::None;
	}
	if (Target->IsA<ABarrelActor>())
	{
		return ETargetedShotKind::Barrel;
	}
	if (Target->IsA<AProximityMineActor>())
	{
		return ETargetedShotKind::Mine;
	}
	if (Target->IsA<ALootCrateActor>())
	{
		return ETargetedShotKind::Crate;
	}
	const AInteractableActor* Interactable = Cast<AInteractableActor>(Target);
	return Interactable && Interactable->bTrapped ? ETargetedShotKind::TrappedObject : ETargetedShotKind::None;
}

FVector AOperativeCharacter::GetMuzzleLocation() const
{
	const float Height = Stance == EOperativeStance::Prone ? 25.f : (Stance == EOperativeStance::Crouching ? 85.f : 140.f);
	return GetActorLocation() - FVector(0.f, 0.f, GetSimpleCollisionHalfHeight() - Height);
}

bool AOperativeCharacter::CanBeginWeaponShot()
{
	if (!HealthComponent || !HealthComponent->IsAlive() || bIsReloading || MisfireCooldownTimer > 0.f)
	{
		return false;
	}
	if (ColdSurvival && ColdSurvival->IsWeaponFrozen())
	{
		if (WeaponFreezeNotifyTimer <= 0.f)
		{
			NotifyWeaponFrozen();
		}
		return false;
	}
	if (CurrentClip > 0 || !UsesAmmo())
	{
		return true;
	}
	StartReload();
	return false;
}

void AOperativeCharacter::ConsumeAmmoAfterShot()
{
	if (!UsesAmmo())
	{
		return;
	}
	CurrentClip = FMath::Max(0, CurrentClip - 1);
	if (CurrentClip <= 0)
	{
		StartReload();
	}
}

void AOperativeCharacter::StopAndFace(const FVector& Location)
{
	SetSprinting(false);
	StopOperative();
	const FVector Direction = (Location - GetActorLocation()).GetSafeNormal2D();
	if (!Direction.IsNearlyZero())
	{
		SetActorRotation(FRotator(0.f, Direction.Rotation().Yaw, 0.f));
	}
}

bool AOperativeCharacter::ShootAtObject(AActor* Target)
{
	const ETargetedShotKind Kind = ClassifyShotTarget(Target);
	if (Kind == ETargetedShotKind::None || Kind == ETargetedShotKind::Enemy || !CanBeginWeaponShot())
	{
		return false;
	}
	StopAndFace(Target->GetActorLocation());

	bool bHit = true;
	FMineShotChance MineShot;
	float DistanceM = 0.f;
	if (Kind == ETargetedShotKind::Mine)
	{
		DistanceM = FVector::Dist(GetActorLocation(), Target->GetActorLocation()) / 100.f;
		MineShot = TargetedShotRules::ComputeMineShotChance(Accuracy, ColdLevel, Stance, DistanceM);
		bHit = bForceHitForTesting || FMath::FRand() * 100.f <= MineShot.Chance;
	}
	OnWeaponFired.Broadcast(this, Target, bHit);
	OnWeaponFiredNative.Broadcast(this, Target, bHit);

	// Godot tracers: barrel = default green, explosive targets = orange, mine miss = grey into the snow nearby.
	if (UCombatFeedbackSubsystem* Feedback = GetWorld() ? GetWorld()->GetSubsystem<UCombatFeedbackSubsystem>() : nullptr)
	{
		FVector End = Target->GetActorLocation();
		FLinearColor Color(1.f, 0.45f, 0.15f);
		EDamageType Style = EDamageType::Explosive;
		if (Kind == ETargetedShotKind::Barrel)
		{
			Color = UCombatFeedbackSubsystem::DefaultTracerColor();
			Style = EDamageType::Kinetic;
		}
		else if (Kind == ETargetedShotKind::Mine && bHit)
		{
			Color = FLinearColor(1.f, 0.5f, 0.15f);
		}
		else if (Kind == ETargetedShotKind::Mine)
		{
			const float Angle = FMath::FRandRange(0.f, 2.f * PI);
			End += FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * FMath::FRandRange(80.f, 180.f);
			Color = FLinearColor(0.7f, 0.7f, 0.8f, 0.65f);
			Style = EDamageType::Kinetic;
		}
		Feedback->SpawnTracer(GetMuzzleLocation(), End, Color, Style);
	}

	switch (Kind)
	{
	case ETargetedShotKind::Barrel:
		CastChecked<ABarrelActor>(Target)->Explode(DisplayName);
		break;
	case ETargetedShotKind::Mine:
	{
		const FText Chance = FText::AsNumber(FMath::TruncToInt(MineShot.Chance));
		const FText Distance = FText::FromString(FString::Printf(TEXT("%.1f"), DistanceM));
		if (bHit)
		{
			OperativeShotLine(*this, FText::Format(LOCTEXT("MineHit",
				"💥 {0}: «Меткий выстрел (Шанс: {1}%, {2}, {3}м)! Мина ликвидирована дистанционно!»"),
				DisplayName, Chance, MineShot.StanceName, Distance));
			CastChecked<AProximityMineActor>(Target)->Detonate();
		}
		else
		{
			OperativeShotLine(*this, FText::Format(LOCTEXT("MineMiss",
				"💨 {0}: «Промах! (Шанс был {1}%: дист. {2}м, {3}). {4} — присядьте или подойдите ближе!»"),
				DisplayName, Chance, Distance, MineShot.StanceName, TargetedShotRules::GetMineMissReason(Stance, DistanceM, ColdLevel)));
		}
		break;
	}
	case ETargetedShotKind::Crate:
	{
		ALootCrateActor* Crate = CastChecked<ALootCrateActor>(Target);
		if (Crate->bTrapped)
		{
			OperativeShotLine(*this, FText::Format(LOCTEXT("CrateTrapShot", "💥 {0}: «Выстрел по ловушке ящика! Дистанционный подрыв!»"), DisplayName));
			Crate->DetonateTrap(true, DisplayName);
		}
		else
		{
			// Only a trap blows up (user decision 2026-09-28: Godot also detonating an untrapped crate is a bug).
			OperativeShotLine(*this, LOCTEXT("CratePierced", "💥 Пуля пробила ящик снабжения."));
		}
		break;
	}
	case ETargetedShotKind::TrappedObject:
		OperativeShotLine(*this, FText::Format(LOCTEXT("TrappedShot", "💥 {0}: «Выстрел по растяжке на объекте! Дистанционная детонация!»"), DisplayName));
		CastChecked<AInteractableActor>(Target)->DetonateTrap(true, DisplayName);
		break;
	default:
		break;
	}
	ConsumeAmmoAfterShot();
	return true;
}

void AOperativeCharacter::SetManualPriorityTarget(AActor* Enemy)
{
	if (IsRaging())
	{
		UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("⚠️ В ЯРОСТИ! НЕ ПОДЧИНЯЕТСЯ!"), FLinearColor(1.f, 0.4f, 0.1f));
		return;
	}
	ManualPriorityTarget = Enemy;
	if (Enemy)
	{
		const FVector Direction = (Enemy->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
		if (!Direction.IsNearlyZero())
		{
			SetActorRotation(FRotator(0.f, Direction.Rotation().Yaw, 0.f));
		}
	}
}

void AOperativeCharacter::PlanTargetedShot(AActor* Target)
{
	const ETargetedShotKind Kind = ClassifyShotTarget(Target);
	if (Kind != ETargetedShotKind::None)
	{
		PlannedShots.Add(Kind, Target);
	}
}

void AOperativeCharacter::ExecutePlannedTargetedShots()
{
	TMap<ETargetedShotKind, TWeakObjectPtr<AActor>> Plans = MoveTemp(PlannedShots);
	PlannedShots.Reset();
	for (const ETargetedShotKind Kind : { ETargetedShotKind::Barrel, ETargetedShotKind::Mine, ETargetedShotKind::Crate, ETargetedShotKind::TrappedObject })
	{
		const TWeakObjectPtr<AActor>* Target = Plans.Find(Kind);
		if (Target && Target->IsValid())
		{
			ShootAtObject(Target->Get());
		}
	}
	const TWeakObjectPtr<AActor>* Enemy = Plans.Find(ETargetedShotKind::Enemy);
	if (Enemy && Enemy->IsValid())
	{
		SetManualPriorityTarget(Enemy->Get());
	}
}

#undef LOCTEXT_NAMESPACE

void AOperativeCharacter::NotifyWeaponFrozen()
{
	WeaponFreezeNotifyTimer = 2.5f;
	UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("🥶 ОРУЖИЕ ЗАМЁРЗЛО! Нужен источник тепла!"), FLinearColor(0.4f, 0.85f, 1.f));
}

float AOperativeCharacter::TakeHit(float Amount, const FString& Attacker, bool bCrit, bool bBypassAvoidance, AActor* AttackerActor)
{
	if (!HealthComponent || !HealthComponent->IsAlive())
	{
		return 0.f;
	}
	// 1. Dodge on luck (Godot: luck 25 -> 10 %).
	const float DodgeRoll = ForcedDodgeRollForTesting >= 0.f ? (ForcedDodgeRollForTesting >= 1.f ? -1.f : 101.f) : FMath::FRand() * 100.f;
	ForcedDodgeRollForTesting = -1.f;
	if (!bBypassAvoidance && DodgeRoll < Luck * 0.4f)
	{
		UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("💨 УКЛОНЕНИЕ!"), FLinearColor(0.3f, 0.9f, 1.f));
		return 0.f;
	}
	// 2. Stance defense and fortitude cut (Godot: 15 fortitude = 22.5 %, at most 50 %).
	const float Fortitude = ColdSurvival ? ColdSurvival->Fortitude : 15.f;
	const float FortitudeCut = FMath::Clamp(Fortitude * 0.015f, 0.f, 0.5f);
	const float Final = bBypassAvoidance ? FMath::Max(1.f, Amount)
		: FMath::Max(1.f, Amount * HealthComponent->GetDefenseMultiplier() * (1.f - FortitudeCut));
	HealthComponent->ApplyDirectHealthLoss(Final, Attacker);
	UFloatingTextSubsystem::SpawnAboveOperative(this, bCrit ? FString::Printf(TEXT("💥 КРИТИЧЕСКИЙ УДАР! -%d"), FMath::FloorToInt(Final))
		: FString::Printf(TEXT("-%d"), FMath::FloorToInt(Final)), bCrit ? FLinearColor(1.f, 0.25f, 0.1f) : FLinearColor(1.f, 0.3f, 0.3f));
	// Godot rage_comp.on_incoming_hit(attacker_node, is_crit, final_incoming).
	if (RageComponent && HealthComponent->IsAlive())
	{
		RageComponent->OnIncomingHit(AttackerActor, bCrit);
	}
	return Final;
}

bool AOperativeCharacter::TryAIGrenadeThrow()
{
	UWorld* World = GetWorld();
	UGrenadeSubsystem* Grenades = World ? World->GetSubsystem<UGrenadeSubsystem>() : nullptr;
	const UGameFlowSubsystem* Flow = World ? World->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	if (!Grenades || GrenadesCount <= 0 || AIGrenadeCooldown > 0.f || !HealthComponent || !HealthComponent->IsAlive()
		|| Grenades->IsAiming() || (Flow && Flow->GetCombatMode() == ECodexCombatMode::TurnBased))
	{
		return false;
	}
	// Godot global_position: operatives at their centre (1 m above the feet), enemies at their feet.
	auto Centre = [](const AActor* Actor) { return Actor->GetActorLocation() - FVector(0.f, 0.f, Actor->GetSimpleCollisionHalfHeight() - 100.f); };
	TArray<FVector> Enemies;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (IsLiveEnemy(*It))
		{
			Enemies.Add(ShotFeet(*It));
		}
	}
	TArray<FVector> Allies;
	if (const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>())
	{
		for (const AOperativeCharacter* Member : Squad->GetMembers())
		{
			if (Member->HealthComponent && Member->HealthComponent->IsAlive())
			{
				Allies.Add(Centre(Member));
			}
		}
	}
	for (TActorIterator<ATurretActor> It(World); It; ++It)
	{
		Allies.Add(It->GetActorLocation()); // Godot "allies"
	}
	const FAIGrenadeOpportunity Opportunity = AIGrenadeRules::Evaluate(AIGrenadeConfig, Centre(this), Stance, GrenadeThrowRange,
		GrenadeEffectRadius, Enemies, Allies);
	if (!Opportunity.bCanThrow)
	{
		return false;
	}
	AIGrenadeCooldown = AIGrenadeConfig.Cooldown;
	AGrenadeActor* Grenade = Grenades->ThrowAt(this, Opportunity.Target);
	if (UGameMessageSubsystem* Messages = World->GetSubsystem<UGameMessageSubsystem>())
	{
		const TCHAR* Callouts[] = { TEXT("💣 Бросаю гранату! Ложись!"), TEXT("🧨 Враги скучились! Ловите подарок!"),
			TEXT("💣 Граната пошла! Пригнитесь!"), TEXT("🧨 Лови гранату, гады!") };
		Messages->PostMessage(DisplayName, FText::FromString(Callouts[FMath::RandRange(0, 3)]));
	}
	return Grenade != nullptr;
}

void AOperativeCharacter::AutoSwitchOnEmpty()
{
	auto Usable = [this](const TCHAR* Id)
	{
		const bool bOwned = AvailableWeapons.ContainsByPredicate([Id](const UWeaponDataAsset* Weapon) { return Weapon && Weapon->WeaponId == Id; });
		const FWeaponAmmoState Ammo = GetAmmoState(Id);
		return bOwned && (Ammo.Clip > 0 || Ammo.Reserve > 0 || Ammo.Reserve == -1);
	};
	if (Usable(TEXT("m16")))
	{
		SwitchToWeaponById(TEXT("m16"));
		return;
	}
	if (Usable(TEXT("pistol")))
	{
		SwitchToWeaponById(TEXT("pistol"));
		return;
	}
	if (GrenadesCount > 0 && AIGrenadeCooldown <= 0.f)
	{
		TryAIGrenadeThrow();
	}
	SwitchToWeaponById(TEXT("knife")); // Godot _switch_to_melee_knife: the last-chance weapon
}

int32 AOperativeCharacter::GetNextLevelExp() const
{
	return ProgressionRules::NextLevelExp(Level);
}

void AOperativeCharacter::AddExp(int32 Amount)
{
	const int32 FirstLevel = Level;
	const int32 Gained = ProgressionRules::AddExp(Level, CurrentExp, Amount);
	for (int32 NewLevel = FirstLevel + 1; NewLevel <= FirstLevel + Gained; ++NewLevel)
	{
		// Godot _on_level_up: +3 points, full heal, floating «⭐ УРОВЕНЬ N!», radio line.
		UnspentStatPoints += ProgressionRules::PointsPerLevel;
		if (HealthComponent)
		{
			HealthComponent->Heal(HealthComponent->GetMaxHealth() - HealthComponent->GetCurrentHealth());
		}
		UFloatingTextSubsystem::SpawnAboveOperative(this, FString::Printf(TEXT("⭐ УРОВЕНЬ %d!"), NewLevel), FLinearColor(1.f, 0.85f, 0.1f));
		if (UGameMessageSubsystem* Messages = GetWorld() ? GetWorld()->GetSubsystem<UGameMessageSubsystem>() : nullptr)
		{
			Messages->PostMessage(DisplayName, FText::FromString(FString::Printf(
				TEXT("⭐ НОВЫЙ УРОВЕНЬ %d! Доступно +3 очка характеристик для распределения!"), NewLevel)));
		}
		UE_LOG(LogCodexTactics, Log, TEXT("%s reached level %d"), *DisplayName.ToString(), NewLevel);
	}
}

float AOperativeCharacter::GetStatValue(EProgressStat Stat) const
{
	switch (Stat)
	{
	case EProgressStat::Health: return HealthComponent ? HealthComponent->GetMaxHealth() : 100.f;
	case EProgressStat::Luck: return Luck;
	case EProgressStat::Accuracy: return Accuracy;
	default: return ColdSurvival ? ColdSurvival->Fortitude : 15.f;
	}
}

bool AOperativeCharacter::CanIncreaseStat(EProgressStat Stat) const
{
	return ProgressionRules::CanIncrease(Stat, GetStatValue(Stat), UnspentStatPoints);
}

bool AOperativeCharacter::CanDecreaseStat(EProgressStat Stat) const
{
	const float Base = Stat == EProgressStat::Health ? InitialBaseHealth
		: (Stat == EProgressStat::Luck ? InitialBaseLuck : (Stat == EProgressStat::Accuracy ? InitialBaseAccuracy : InitialBaseFortitude));
	return ProgressionRules::CanDecrease(Stat, GetStatValue(Stat), Base);
}

bool AOperativeCharacter::IncreaseStat(EProgressStat Stat)
{
	if (!CanIncreaseStat(Stat))
	{
		return false;
	}
	const float Step = ProgressionRules::StatStep(Stat);
	switch (Stat)
	{
	case EProgressStat::Health:
		if (HealthComponent)
		{
			HealthComponent->SetMaxHealth(HealthComponent->GetMaxHealth() + Step, false);
			HealthComponent->Heal(Step);
		}
		break;
	case EProgressStat::Luck: Luck += Step; break;
	case EProgressStat::Accuracy: Accuracy += Step; break;
	default:
		if (ColdSurvival)
		{
			ColdSurvival->Fortitude += Step;
		}
		break;
	}
	--UnspentStatPoints;
	return true;
}

bool AOperativeCharacter::DecreaseStat(EProgressStat Stat)
{
	if (!CanDecreaseStat(Stat))
	{
		return false;
	}
	const float Step = ProgressionRules::StatStep(Stat);
	switch (Stat)
	{
	case EProgressStat::Health:
		if (HealthComponent)
		{
			HealthComponent->SetMaxHealth(HealthComponent->GetMaxHealth() - Step, false);
		}
		break;
	case EProgressStat::Luck: Luck -= Step; break;
	case EProgressStat::Accuracy: Accuracy -= Step; break;
	default:
		if (ColdSurvival)
		{
			ColdSurvival->Fortitude -= Step;
		}
		break;
	}
	++UnspentStatPoints;
	return true;
}

void AOperativeCharacter::CaptureProgressionBases()
{
	InitialBaseHealth = GetStatValue(EProgressStat::Health);
	InitialBaseLuck = Luck;
	InitialBaseAccuracy = Accuracy;
	InitialBaseFortitude = GetStatValue(EProgressStat::Fortitude);
}
