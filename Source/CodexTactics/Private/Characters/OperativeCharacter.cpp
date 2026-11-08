// AOperativeCharacter core: construction, BeginPlay / EndPlay, the per-frame Tick that drives every cluster, squad
// identity and body colour. The other members live in one translation unit per cluster (cleanup day 2026-10-14,
// docs/port/REFACTOR_OPERATIVE_2026-10.md): OperativeCharacterMovement / Stance / Vault / Overlays / Inventory /
// Progression / Health / Weapons / Combat / Aim / TargetedShots / Cover / CoverEntry / CornerAim / Sniper.cpp; shared
// file-local helpers in OperativeCharacterInternal.h.
// Godot reference: Scenes/movements/player.gd.

#include "Characters/OperativeCharacter.h"

#include "AI/PmcSquadSubsystem.h"
#include "Characters/OperativeAIController.h"
#include "Characters/PanicComponent.h"
#include "Combat/ArmorRules.h"
#include "Combat/EncounterQueries.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "Characters/RageComponent.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/KnockdownComponent.h"
#include "Combat/VisionConeComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Interactables/RadiusRingSubsystem.h"
#include "Falls/LedgeFallComponent.h"
#include "Ladders/LadderClimbComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Survival/ColdSurvivalComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Vehicles/VehicleSubsystem.h"

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
	// The body is turned by UpdateCombatFacing (Godot lerp_angle), not by the movement component.
	Movement->bOrientRotationToMovement = false;
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
	WeaponSkeletalMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WeaponSkeletalMesh"));
	WeaponSkeletalMesh->SetupAttachment(WeaponMesh);
	WeaponSkeletalMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponSkeletalMesh->SetCastShadow(true);

	ColdSurvival = CreateDefaultSubobject<UColdSurvivalComponent>(TEXT("ColdSurvival"));
	RageComponent = CreateDefaultSubobject<URageComponent>(TEXT("RageComponent"));
	PanicComponent = CreateDefaultSubobject<UPanicComponent>(TEXT("PanicComponent"));
	HealthComponent = CreateDefaultSubobject<UHealthComponent>(TEXT("HealthComponent"));
	KnockdownComponent = CreateDefaultSubobject<UKnockdownComponent>(TEXT("KnockdownComponent")); // AM_* montages on FullBody
	LadderClimb = CreateDefaultSubobject<ULadderClimbComponent>(TEXT("LadderClimb")); // docs/design/LADDERS.md
	LedgeFall = CreateDefaultSubobject<ULedgeFallComponent>(TEXT("LedgeFall")); // falls and drops (2026-11-03)
	HealthComponent->MaxHealth = 100.0f;
	HealthComponent->BaseArmorReduction = 0.10f;

	VisionCone = CreateDefaultSubobject<UVisionConeComponent>(TEXT("VisionCone"));
	VisionCone->SetupAttachment(GetCapsuleComponent());
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
	// Body armour (weapons framework, decision 1 2026-11-08): the squad and the PMC fighters share weapons_tuning.json
	// "armor.bodies.operative" (class + reduction) over the constructor's fallback.
	if (HealthComponent)
	{
		FBodyArmor Armor{ HealthComponent->GetArmorClass(), HealthComponent->GetBaseArmorReduction() };
		if (ArmorRules::ApplyBody(ArmorRules::OperativeBodyKey, Armor))
		{
			HealthComponent->SetBaseArmorReduction(Armor.Reduction);
			HealthComponent->SetArmorClass(Armor.Class);
		}
	}
	// Blueprints made before the switch may still carry the engine's orient-to-movement: the facing code turns the body.
	GetCharacterMovement()->bOrientRotationToMovement = false;
	OnCharacterMovementUpdated.AddUniqueDynamic(this, &AOperativeCharacter::HandleCharacterMovementUpdatedForRing);

	if (USkeletalMeshComponent* SkeletalMesh = GetMesh())
	{
		MeshBaseZ = SkeletalMesh->GetRelativeLocation().Z;
		MeshBaseXY = FVector2D(SkeletalMesh->GetRelativeLocation());
		MeshBaseYaw = SkeletalMesh->GetRelativeRotation().Yaw;
		bMeshBaseCaptured = true;
		if (WeaponMesh)
		{
			WeaponMesh->AttachToComponent(SkeletalMesh, FAttachmentTransformRules::KeepRelativeTransform, WeaponSocket);
		}
	}
	if (WeaponMesh && !bDefaultWeaponMeshCaptured)
	{
		// The Blueprint's model + offset: a weapon without its own HandMesh shows it (ApplyWeaponVisual).
		DefaultWeaponMeshAsset = WeaponMesh->GetStaticMesh();
		DefaultWeaponMeshTransform = WeaponMesh->GetRelativeTransform();
		bDefaultWeaponMeshCaptured = true;
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
	if (KnockdownComponent)
	{
		KnockdownComponent->OnPhaseChanged.AddUObject(this, &AOperativeCharacter::HandleKnockdownPhase);
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
	if (Squad && bRecruited && IsPlayerTeam())
	{
		Squad->RegisterOperative(this);
	}
	else if (!IsPlayerTeam())
	{
		// A hostile PMC fighter (2026-11-03): the "Enemy" tag makes him a target of the player's men (registry, plates, Ctrl + click),
		// UPmcSquadSubsystem is his squad.
		Tags.AddUnique(CombatQueries::EnemyTag);
		if (UEnemyRegistrySubsystem* Registry = GetWorld()->GetSubsystem<UEnemyRegistrySubsystem>())
		{
			Registry->RegisterTaggedActor(this);
		}
		if (UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(GetWorld()))
		{
			Pmc->RegisterOperative(this);
		}
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
			if (DisplayName.IsEmpty()) DisplayName = NSLOCTEXT("CodexTactics", "Commander", "Commander");
			break;
		case 1: // Engineer - Hazard Orange
			BodyColor = FLinearColor::FromSRGBColor(FColor(0xFF, 0x61, 0x0F));
			if (DisplayName.IsEmpty()) DisplayName = NSLOCTEXT("CodexTactics", "Engineer", "Engineer");
			break;
		case 2: // Medic-Sapper - Field Medic Green
			BodyColor = FLinearColor::FromSRGBColor(FColor(0x1F, 0xB3, 0x33));
			if (DisplayName.IsEmpty()) DisplayName = NSLOCTEXT("CodexTactics", "Medic", "Medic-Sapper");
			break;
		default:
			break;
		}
	}

	if (!UsesPlaceholderBody() && !SquadOutfits.IsEmpty())
	{
		for (const TPair<FName, TObjectPtr<UMaterialInterface>>& Entry : SquadOutfits[FMath::Max(SquadIndex, 0) % SquadOutfits.Num()].SlotMaterials)
		{
			const int32 OutfitSlot = GetMesh()->GetMaterialIndex(Entry.Key);
			if (OutfitSlot != INDEX_NONE && Entry.Value)
			{
				GetMesh()->SetMaterial(OutfitSlot, Entry.Value);
			}
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
	if (SelectionRing)
	{
		SelectionRing->Destroy();
		SelectionRing = nullptr;
	}
	if (UWorld* World = GetWorld())
	{
		if (USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>())
		{
			Squad->UnregisterOperative(this);
		}
		if (UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World))
		{
			Pmc->UnregisterOperative(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

bool AOperativeCharacter::IsRaging() const
{
	return RageComponent && RageComponent->IsRaging();
}

bool AOperativeCharacter::IsPanicking() const
{
	return PanicComponent && PanicComponent->IsPanicking();
}

void AOperativeCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UpdatePlaceholderPose(1.f - FMath::Exp(-StanceBlendSpeed * DeltaTime));
	UpdateProneDive(DeltaTime);
	if (bVaulting)
	{
		UpdateVault(DeltaTime);
	}
	else if (IsClimbingLadder())
	{
		// on a ladder: ULadderClimbComponent moves and turns him; no cover, facing or shooting (LADDERS.md 7.2)
	}
	else if (IsDroppingOffLedge())
	{
		// running off an edge / in the air / landing: ULedgeFallComponent moves him; no cover, facing or shooting
	}
	else if (UpdateObstacleStepOff(DeltaTime))
	{
		// jumping down off a barricade / barrel top this frame
	}
	else if (!IsKnockedDown() && !VehicleQueries::IsMounted(this)) // Sprint 14: lying on the ground — no cover, facing or shooting until he is up (APC P1: nor aboard)
	{
		UpdateVaultTrigger(DeltaTime);
		TryEnterCoverFromRun();
		UpdateCover(DeltaTime);
		UpdateCombatFacing(DeltaTime);
		ProcessCombatShooting(DeltaTime);
		UpdateSniperPendingShot(DeltaTime);
		UpdateUnderFireManoeuvre(DeltaTime); // #43: cover / a step for a line / prone under fire
	}
	UpdateCoverEntryBlend(DeltaTime);
	EnforceMeshBaseOffset();
	UpdateSilhouette(DeltaTime);
	UpdateSelectionRing();
	UpdateVisionCone(DeltaTime);
}

