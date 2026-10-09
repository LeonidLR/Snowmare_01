#include "Characters/OperativeCharacter.h"
#include "Combat/KnockdownComponent.h"
#include "Combat/KnockdownRules.h"
#include "Telemetry/RunTelemetrySubsystem.h"
#include "Interactables/RadiusRingSubsystem.h"
#include "Subsystems/CodexEventBus.h"
#include "UI/FloatingTextSubsystem.h"
#include "Characters/OperativeAIController.h"
#include "Characters/PanicComponent.h"
#include "Characters/RageComponent.h"
#include "Characters/VaultRules.h"
#include "Interactables/VaultNavigation.h"
#include "Combat/GrenadeSubsystem.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/CombatFeedbackSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/MissionSubsystem.h"
#include "Core/MissionRules.h"
#include "Combat/DeathCinematicSubsystem.h"
#include "TimerManager.h"
#include "Components/StaticMeshComponent.h"
#include "Data/SquadROE.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/MarksmanEnemyCharacter.h"
#include "Combat/GrenadeActor.h"
#include "DrawDebugHelpers.h"

namespace
{
	TAutoConsoleVariable<int32> CVarDebugCornerAim(TEXT("Codex.Debug.CornerAim"), 0,
		TEXT("1: the sustained corner aim's decision, its reason and inputs over the operatives in cover."));

	/** Spitters, cryo drones and marksmen shoot from a distance; hounds, cutters, frostbitten and brutes close in. */
	bool IsRangedEnemyActor(const AActor* Actor)
	{
		const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Actor);
		if (!Enemy)
		{
			return false;
		}
		const EEnemyArchetype Type = Enemy->GetArchetype();
		return Type == EEnemyArchetype::Spitter || Type == EEnemyArchetype::CryoDrone || Type == EEnemyArchetype::Marksman;
	}
}
#include "Combat/EnemyGhostActor.h"
#include "Combat/SightRules.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Data/WeaponDataAsset.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Characters/FacingRules.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/AimOffsetRules.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFlow/LevelEncounterSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Interactables/BarricadeActor.h"
#include "Interactables/BarrelActor.h"
#include "Interactables/LootCrateActor.h"
#include "Interactables/ProximityMineActor.h"
#include "Interactables/RelocationSubsystem.h"
#include "Interactables/TurretActor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Navigation/PathFollowingComponent.h"
#include "Survival/ColdSurvivalComponent.h"
#include "Tactics/CoverRules.h"
#include "Tactics/CoverTraceRules.h"
#include "Tactics/CoverDecisionRules.h"
#include "Tactics/CoverFacingRules.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
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

	ColdSurvival = CreateDefaultSubobject<UColdSurvivalComponent>(TEXT("ColdSurvival"));
	RageComponent = CreateDefaultSubobject<URageComponent>(TEXT("RageComponent"));
	PanicComponent = CreateDefaultSubobject<UPanicComponent>(TEXT("PanicComponent"));
	HealthComponent = CreateDefaultSubobject<UHealthComponent>(TEXT("HealthComponent"));
	KnockdownComponent = CreateDefaultSubobject<UKnockdownComponent>(TEXT("KnockdownComponent")); // AM_* montages on FullBody
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
	// Blueprints made before the switch may still carry the engine's orient-to-movement: the facing code turns the body.
	GetCharacterMovement()->bOrientRotationToMovement = false;

	if (USkeletalMeshComponent* SkeletalMesh = GetMesh())
	{
		MeshBaseZ = SkeletalMesh->GetRelativeLocation().Z;
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

EOperativeOrderResult AOperativeCharacter::OrderMoveTo(const FVector& Destination, bool bSprint)
{
	if (IsRaging())
	{
		UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("⚠️ ENRAGED! IGNORING ORDERS!"), FLinearColor(1.f, 0.4f, 0.1f));
		return EOperativeOrderResult::Refused;
	}
	if (IsPanicking())
	{
		UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("⚠️ PANICKING! IGNORING ORDERS!"), FLinearColor(1.f, 0.3f, 0.3f));
		return EOperativeOrderResult::Refused;
	}
	// Sprint 14: knocked down — the order waits and runs once he is up (the latest one wins).
	if (IsKnockedDown())
	{
		TWeakObjectPtr<AOperativeCharacter> WeakThis(this);
		KnockdownComponent->BufferOrder([WeakThis, Destination, bSprint]()
		{
			if (AOperativeCharacter* Self = WeakThis.Get())
			{
				Self->OrderMoveTo(Destination, bSprint);
			}
		});
		UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("KNOCKED DOWN - ORDER QUEUED"), FLinearColor(1.f, 0.7f, 0.2f));
		return EOperativeOrderResult::Accepted;
	}
	// Sprint 12: a move order away from the wall leaves the cover (a cover / shimmy order keeps it; so does the planned
	// walk to the pending slot that the tactical pause releases).
	const bool bToPendingSlot = bHasPendingCover && FVector::Dist2D(Destination, PendingCoverSlot.WorldLocation) <= 60.f;
	if (!bCoverMoveOrder && !bPendingMoveReplay && !bToPendingSlot)
	{
		if (bInCover)
		{
			LeaveCover(TEXT("move order"));
		}
		bHasPendingCover = false;
	}
	// Commander Mode (Sprint 07-B): every player move order pins the anchor the autonomy fights around.
	if (!bAutonomousOrder && !bPendingMoveReplay)
	{
		TacticalAnchor = SquadAutonomyRules::MakeAnchor(SquadROE::Get(), GetActorLocation(), Destination, GetActorRotation());
	}
	// User decision 2026-10-01: a sprint order (double click) stands a prone operative up — he rises in place, then runs;
	// any move ordered while he is getting up starts once he is up.
	UOperativeAnimInstance* StanceAnim = GetMesh() ? Cast<UOperativeAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
	float RiseDelay = 0.f;
	if (bSprint && Stance == EOperativeStance::Prone && !bCarrying
		&& OperativeMovementRules::CanSprint(MovementConfig, EOperativeStance::Standing, ColdLevel, IsWounded()))
	{
		RiseDelay = GetStanceChangeDelay(EOperativeStance::Prone, EOperativeStance::Standing);
		SetStance(EOperativeStance::Standing);
	}
	else if (StanceAnim && StanceAnim->IsPlayingStanceTransition() && !bPendingMoveReplay)
	{
		RiseDelay = StanceAnim->GetStanceTransitionTimeLeft();
	}
	if (RiseDelay > 0.05f && GetWorld())
	{
		TWeakObjectPtr<AOperativeCharacter> WeakThis(this);
		GetWorld()->GetTimerManager().SetTimer(PendingMoveTimer, FTimerDelegate::CreateLambda([WeakThis, Destination, bSprint]()
		{
			if (AOperativeCharacter* Self = WeakThis.Get())
			{
				TGuardValue<bool> Replay(Self->bPendingMoveReplay, true);
				Self->OrderMoveTo(Destination, bSprint);
			}
		}), RiseDelay, false);
		bHasMoveOrder = true;
		return EOperativeOrderResult::Accepted;
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
	if (IsPanicking() || IsKnockedDown())
	{
		return EOperativeOrderResult::Refused; // the panic moves him (Godot _process_panic_movement); knocked down: he lies
	}
	if (bInCover || bHasPendingCover)
	{
		return EOperativeOrderResult::Refused; // Sprint 12: he holds his wall like a guard; the formation does not pull him
	}
	if (IsHoldingForSniperShot())
	{
		return EOperativeOrderResult::Refused; // kneeling for an ordered sniper shot: the formation waits for her
	}
	ApplyMovementParams(Speed);
	return RequestMove(Destination);
}

EOperativeOrderResult AOperativeCharacter::RequestMove(const FVector& Destination)
{
	LastMoveDestination = Destination;
	ClearIdleFacing();
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
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PendingMoveTimer);
	}
	ApplyMovementParams();
}

float AOperativeCharacter::GetStanceChangeDelay(EOperativeStance From, EOperativeStance To) const
{
	const UOperativeAnimInstance* Anim = GetMesh() ? Cast<UOperativeAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
	if (!Anim || From == To)
	{
		return 0.f;
	}
	const UAnimSequenceBase* Clip = From == EOperativeStance::Prone
		? (To == EOperativeStance::Crouching ? Anim->ProneToCrouchAnimation.Get() : Anim->ProneToStandAnimation.Get())
		: To == EOperativeStance::Prone ? (From == EOperativeStance::Crouching ? Anim->CrouchToProneAnimation.Get() : Anim->StandToProneAnimation.Get())
		: To == EOperativeStance::Crouching ? Anim->StandToCrouchAnimation.Get() : Anim->CrouchToStandAnimation.Get();
	return Clip ? FMath::Max(0.f, Clip->GetPlayLength() / FMath::Max(Anim->StanceTransitionPlayRate, 0.1f) - Anim->StanceTransitionBlendTime) : 0.f;
}

void AOperativeCharacter::SetStance(EOperativeStance NewStance)
{
	if (Stance == NewStance || IsKnockedDown())
	{
		return; // knocked down: he gets up standing (the get-up clip)
	}
	// Godot: a frostbitten operative physically cannot get up from the snow.
	if (ColdSurvival && ColdSurvival->IsFrostbitten() && NewStance != EOperativeStance::Prone)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("%s: frostbitten, cannot leave prone"), *DisplayName.ToString());
		return;
	}
	// User decision 2026-10-01: getting up from prone needs a full stop — a crawling operative stops, then rises in place.
	if (Stance == EOperativeStance::Prone && NewStance != EOperativeStance::Prone
		&& (bHasMoveOrder || GetVelocity().SizeSquared2D() > 100.f))
	{
		StopOperative();
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
	case EOperativeStance::Crouching: return NSLOCTEXT("CodexTactics", "StanceCrouching", "CROUCHED");
	case EOperativeStance::Prone: return NSLOCTEXT("CodexTactics", "StanceProne", "PRONE");
	default: return NSLOCTEXT("CodexTactics", "StanceStanding", "STANDING");
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
	if (bInCover)
	{
		LeaveCover(TEXT("died"));
	}
	bHasPendingCover = false;
	if (UCodexEventBus* Bus = UCodexEventBus::Get(this))
	{
		Bus->OnSoldierDowned.Broadcast(this); // Godot EventBus.soldier_downed
	}
	if (RageComponent)
	{
		RageComponent->ExitRage(TEXT("Killed"));
	}
	if (PanicComponent)
	{
		PanicComponent->RecoverFromPanic(TEXT("Killed"), true);
	}
	// User decision 2026-10-08 (replaces Godot _check_squad_vital_signs "any member down = mission failed"): only the
	// COMMANDER's death loses the mission. Everybody else is a permanent loss (Godot _handle_expendable_member_death for
	// the recruit, now for all): he leaves the squad (the leader passes on), stays down as a corpse and his body can be
	// searched for his supplies (Godot corpse_loot). Either way the death cinematic shows him fall first.
	bKilledInAction = true;
	BecomeCorpse();
	USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	const bool bSquadMember = Squad && Squad->GetMembers().Contains(this); // a recruit not yet rescued is not one
	if (Squad)
	{
		Squad->UnregisterOperative(this);
	}
	if (UTurnBasedCombatSubsystem* TurnBased = GetWorld()->GetSubsystem<UTurnBasedCombatSubsystem>())
	{
		TurnBased->NotifyOperativeKilled(this);
	}
	int32 Living = 0;
	for (const AOperativeCharacter* Member : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
	{
		Living += Member && Member != this && Member->HealthComponent && Member->HealthComponent->IsAlive() ? 1 : 0;
	}
	const bool bDefeat = bSquadMember && MissionRules::ShouldFailMission(SquadRole == EOperativeRole::Commander, Living);
	UE_LOG(LogCodexTactics, Display, TEXT("%s killed in action (%s, %d squad members alive)%s"), *DisplayName.ToString(),
		*UEnum::GetValueAsString(SquadRole), Living, bDefeat ? TEXT(" -> mission failed after the death cinematic") : TEXT(""));
	if (!bDefeat)
	{
		if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
		{
			Messages->PostMessage(FText::FromString(TEXT("HQ")), FText::FromString(TEXT("⚠️ ") + MissionRules::GetMemberLostRadio(DisplayName).ToString()));
		}
		SpawnRemainsLoot();
	}
	if (UDeathCinematicSubsystem* DeathCam = GetWorld()->GetSubsystem<UDeathCinematicSubsystem>())
	{
		DeathCam->NotifyOperativeDied(this, bDefeat);
	}
	else if (bDefeat)
	{
		if (UMissionSubsystem* Mission = GetWorld()->GetSubsystem<UMissionSubsystem>())
		{
			Mission->TriggerMissionFailed(this);
		}
	}
}

void AOperativeCharacter::RestoreKilledInAction()
{
	bKilledInAction = true;
	BecomeCorpse();
	if (USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>())
	{
		Squad->UnregisterOperative(this);
	}
	if (UTurnBasedCombatSubsystem* TurnBased = GetWorld()->GetSubsystem<UTurnBasedCombatSubsystem>())
	{
		TurnBased->NotifyOperativeKilled(this);
	}
	if (KnockdownComponent)
	{
		KnockdownComponent->PlayDeathFall(GetActorLocation() + GetActorForwardVector() * 100.f); // on his back, held
	}
}

bool AOperativeCharacter::GetLastHitSource(FVector& OutLocation) const
{
	OutLocation = LastHitSource;
	return bHasLastHitSource;
}

void AOperativeCharacter::BecomeCorpse()
{
	// The body stays where he fell: no walking, the squad / enemies step over him, clicks go to his remains, never to him.
	StopOperative();
	if (bCornerAimActive)
	{
		EndCornerAim(ECornerAimDecision::DuckForSafety, TEXT("died"));
	}
	bIsReloading = false;
	ReloadTimer = 0.f;
	SetSprinting(false);
	if (UGrenadeSubsystem* Grenades = GetWorld()->GetSubsystem<UGrenadeSubsystem>(); Grenades && Grenades->GetThrower() == this)
	{
		Grenades->CancelAim();
	}
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		Capsule->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
		Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	}
	if (BodyMesh)
	{
		BodyMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	}
	SetGroupSelected(false, false);
	UpdateSelectionRing();
}

void AOperativeCharacter::SpawnRemainsLoot()
{
	// Godot corpse_loot / player.gd get_items_list: ammo, medkits, food (grenades / the weapon have no loot stack here).
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetSimpleCollisionHalfHeight());
	ALootCrateActor* Remains = GetWorld()->SpawnActor<ALootCrateActor>(Feet + FVector(0.f, 0.f, 40.f), GetActorRotation(), Params);
	if (!Remains)
	{
		return;
	}
	Remains->CrateName = FText::FromString(FString::Printf(TEXT("Remains: %s"), *DisplayName.ToString()));
	FLootContents Contents;
	auto Ammo = [this](const TCHAR* Id)
	{
		const FWeaponAmmoState* State = AmmoInventory.Find(Id);
		return State ? State->Clip + FMath::Max(0, State->Reserve) : 0;
	};
	Contents.Medkits = MedkitsCount;
	Contents.CannedFood = CannedFoodCount;
	Contents.Bread = BreadCount;
	Contents.Chocolate = ChocolateCount;
	Contents.Matches = 0;
	Contents.RifleAmmo = Ammo(TEXT("m16"));
	Contents.PistolAmmo = Ammo(TEXT("pistol"));
	Contents.ShotgunAmmo = 0;
	Contents.FlameFuel = 0;
	Contents.CryoAmmo = 0;
	Contents.PlasmaAmmo = 0;
	Remains->Contents = Contents;
	Remains->OpenSeconds = 0.6f; // searching a body, no lid
	Remains->SetActorHiddenInGame(true); // the body is what the player sees and clicks; the crate is its hit box
	Remains->Tags.Add(TEXT("CorpseLoot"));
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

	// Sprint 12: arrived at the cover slot ordered -> press against the wall.
	if (bHasPendingCover)
	{
		if (FVector::Dist2D(GetActorLocation(), PendingCoverSlot.WorldLocation) <= 150.f)
		{
			bHasPendingCover = false;
			EnterCover(PendingCoverSlot);
			return;
		}
		UE_LOG(LogCodexTactics, Display, TEXT("%s: cover slot not reached (%.0f cm off), stays in the open"), *DisplayName.ToString(),
			FVector::Dist2D(GetActorLocation(), PendingCoverSlot.WorldLocation));
		bHasPendingCover = false;
		if (bInCover)
		{
			LeaveCover(TEXT("shimmy target not reached"));
		}
	}

	// Godot _on_movement_destination_reached: behind a barricade in combat (not frostbitten) the operative takes cover.
	const UGameFlowSubsystem* Flow = GetWorld() ? GetWorld()->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	const bool bCombat = (Flow && Flow->GetPhase() != ECodexGamePhase::Exploration) || CurrentCombatTarget.IsValid();
	if (!bCombat || Stance == EOperativeStance::Crouching || (ColdSurvival && ColdSurvival->IsFrostbitten()) || !IsBehindBarricade())
	{
		return;
	}
	SetStance(EOperativeStance::Crouching);
	UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("🛡️ IN COVER (-35% damage)"), FLinearColor(0.3f, 0.9f, 1.f));
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now - LastCoverChatterTime >= 5.0)
	{
		LastCoverChatterTime = Now;
		const TCHAR* Callouts[] = { TEXT("🛡️ In cover!"), TEXT("🛡️ In cover, holding the sector!"),
			TEXT("🛡️ Behind the barricade, ready to fight!"), TEXT("🛡️ In position behind the shield, watching!") };
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
	if (bVaulting)
	{
		UpdateVault(DeltaTime);
	}
	else if (UpdateObstacleStepOff(DeltaTime))
	{
		// jumping down off a barricade / barrel top this frame
	}
	else if (!IsKnockedDown()) // Sprint 14: lying on the ground — no cover, facing or shooting until he is up
	{
		UpdateVaultTrigger(DeltaTime);
		TryEnterCoverFromRun();
		UpdateCover(DeltaTime);
		UpdateCombatFacing(DeltaTime);
		ProcessCombatShooting(DeltaTime);
		UpdateSniperPendingShot(DeltaTime);
	}
	UpdateCoverEntryBlend(DeltaTime);
	UpdateSilhouette(DeltaTime);
	UpdateSelectionRing();
}

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

void AOperativeCharacter::SetGroupSelected(bool bSelected, bool bInMultiSelectionIn)
{
	bGroupSelected = bSelected;
	bInMultiSelection = bSelected && bInMultiSelectionIn;
	UpdateSelectionRing();
}

bool AOperativeCharacter::IsSelectionRingShown() const
{
	return SelectionRing && !SelectionRing->IsHidden();
}

void AOperativeCharacter::UpdateSelectionRing()
{
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	const bool bLeader = Squad && Squad->GetLeader() == this;
	// Sprint 06-A (user / architect 2026-10-04; Godot showed it only on group members): the active leader always has
	// his gold ring, every selected operative a cyan one — in exploration, preparation and the fight.
	const bool bShow = (bLeader || bGroupSelected) && bRecruited && HealthComponent && HealthComponent->IsAlive();
	if (!bShow)
	{
		if (SelectionRing)
		{
			SelectionRing->SetActorHiddenInGame(true);
		}
		return;
	}
	if (!SelectionRing)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SelectionRing = GetWorld()->SpawnActor<ARadiusRingActor>(Params);
	}
	if (SelectionRing)
	{
		// Godot torus 0.55..0.65 m, 5 cm above the feet; leader gold (1, 0.85, 0.2), others cyan (0.3, 0.9, 1).
		const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetSimpleCollisionHalfHeight() - 5.f);
		const FLinearColor Color = bLeader ? FLinearColor(1.f, 0.85f, 0.2f) : FLinearColor(0.3f, 0.9f, 1.f);
		SelectionRing->ShowRing(Feet, 60.f, Color, 10.f);
		SelectionRing->SetActorHiddenInGame(false);
	}
}

FLinearColor AOperativeCharacter::GetSilhouetteColor() const
{
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	auto Godot = [](float R, float G, float B, float A)
	{
		FLinearColor Color = FLinearColor::FromSRGBColor(FColor(FMath::RoundToInt(R * 255.f), FMath::RoundToInt(G * 255.f), FMath::RoundToInt(B * 255.f)));
		Color.A = A;
		return Color;
	};
	if (Squad && Squad->GetLeader() == this)
	{
		return Godot(0.2f, 0.85f, 1.f, 0.55f);
	}
	switch (SquadRole)
	{
	case EOperativeRole::Engineer: return Godot(1.f, 0.55f, 0.15f, 0.55f);
	case EOperativeRole::MedicSapper: return Godot(0.2f, 0.9f, 0.35f, 0.55f);
	case EOperativeRole::Recruit: return Godot(0.3f, 0.9f, 0.5f, 0.55f);
	default: return Godot(1.f, 0.45f, 0.18f, 0.5f);
	}
}

void AOperativeCharacter::UpdateSilhouette(float DeltaTime)
{
	if (!bEnableSilhouette || !HealthComponent || !HealthComponent->IsAlive())
	{
		SetSilhouetteVisible(false);
		return;
	}
	if (!bSilhouetteOcclusionOnly)
	{
		SetSilhouetteVisible(true);
		return;
	}
	SilhouetteCheckTimer -= DeltaTime;
	if (SilhouetteCheckTimer > 0.f)
	{
		return;
	}
	SilhouetteCheckTimer = 0.05f;
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC || !PC->PlayerCameraManager)
	{
		return;
	}
	const FVector CameraLocation = PC->PlayerCameraManager->GetCameraLocation();
	// Godot global_position + 1 m: the operative's centre is 1 m above the feet.
	const FVector Centre = GetActorLocation() - FVector(0.f, 0.f, GetSimpleCollisionHalfHeight() - 100.f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(OperativeSilhouette), false, this);
	FHitResult Hit;
	const bool bBlocked = GetWorld()->LineTraceSingleByChannel(Hit, CameraLocation, Centre, ECC_Visibility, Params)
		&& FVector::Dist(CameraLocation, Hit.ImpactPoint) < FVector::Dist(CameraLocation, Centre) - 35.f;
	SetSilhouetteVisible(bBlocked);
	if (bBlocked && SilhouetteMID)
	{
		SilhouetteMID->SetVectorParameterValue(TEXT("Color"), GetSilhouetteColor()); // the leader may change
	}
}

void AOperativeCharacter::SetSilhouetteVisible(bool bVisible)
{
	if (bSilhouetteVisible == bVisible)
	{
		return;
	}
	if (bVisible && !SilhouetteMID)
	{
		UMaterialInterface* Base = SilhouetteMaterial ? SilhouetteMaterial.Get()
			: LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/VFX/Materials/M_Silhouette.M_Silhouette"));
		if (!Base)
		{
			return;
		}
		SilhouetteMID = UMaterialInstanceDynamic::Create(Base, this);
		SilhouetteMID->SetVectorParameterValue(TEXT("Color"), GetSilhouetteColor());
	}
	bSilhouetteVisible = bVisible;
	TInlineComponentArray<UMeshComponent*> Meshes(this);
	for (UMeshComponent* MeshComponent : Meshes)
	{
		if (MeshComponent && MeshComponent->IsVisible())
		{
			MeshComponent->SetOverlayMaterial(bVisible ? SilhouetteMID.Get() : nullptr);
		}
	}
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
	ApplyWeaponVisual();
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
	SniperPendingObject.Reset();
	ApplyWeaponVisual();
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
		TEXT("🔄 Reloading! Cover me!"),
		TEXT("🔄 Mag empty! Cover the sector!"),
		TEXT("🔄 Changing mags, hold them off!"),
		TEXT("🔄 Reloading! Watch my back!") };
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
	if (!HealthComponent || !HealthComponent->IsAlive() || IsKnockedDown() || (IsPanicking() && !IsRaging())) // Godot panic: can_shoot = false
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
	// Sprint 08: an enemy the squad does not see is not aimed at (blind fire at its silhouette is an order).
	if (const UTacticalSightSubsystem* Sight = World ? World->GetSubsystem<UTacticalSightSubsystem>() : nullptr;
		Sight && Enemy && !Sight->IsVisibleToSquad(Enemy))
	{
		return false;
	}
	const FVector MyFeet = ShotFeet(this);
	const FVector EnemyFeet = ShotFeet(Enemy);
	// Sprint 12: nothing to fire round / over from this cover (a full wall without a corner). User decision 2026-10-06:
	// only a target behind the wall / around the corner needs that; one out on the open side gets a normal shot.
	const bool bCornerShot = IsCornerShotTarget(EnemyFeet);
	if (bCornerShot && !CanFireFromCover())
	{
		return false;
	}
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
	if (World->LineTraceSingleByChannel(Hit, bCornerShot ? GetCoverFireOrigin() : GetMuzzleLocation(), EnemyFeet + FVector(0.f, 0.f, 80.f), ECC_Visibility, Params))
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

void AOperativeCharacter::SetBlindFireTarget(AEnemyGhostActor* Ghost)
{
	BlindFireGhost = Ghost;
	BlindFireSource = Ghost ? Ghost->GetSource() : nullptr;
	if (Ghost)
	{
		ManualPriorityTarget.Reset();
	}
}

AEnemyGhostActor* AOperativeCharacter::GetBlindFireTarget() const
{
	return BlindFireGhost.Get();
}

bool AOperativeCharacter::EvaluateBlindLine(const AEnemyGhostActor& Ghost, FShootCandidate& Out) const
{
	UWorld* World = GetWorld();
	const FVector MyFeet = ShotFeet(this);
	const FVector GhostFeet = Ghost.GetLastKnownFeet();
	const bool bCornerShot = IsCornerShotTarget(GhostFeet);
	if (bCornerShot && !CanFireFromCover())
	{
		return false; // Sprint 12 (a silhouette out on the open side: a normal shot, user decision 2026-10-06)
	}
	if (!World || SquadFireRules::IsInDeadZone(MyFeet, GhostFeet))
	{
		return false;
	}
	const FElevationAdvantage Elevation = SquadFireRules::GetElevationAdvantage(MyFeet.Z, GhostFeet.Z);
	const float Range = (CurrentWeapon ? CurrentWeapon->AttackRangeCm : 1400.f) * SquadFireRules::GetPostureRangeMultiplier(Stance)
		* (Elevation.bElevated ? Elevation.RangeMultiplier : 1.f);
	const float Distance = FVector::Dist(MyFeet + FVector(0.f, 0.f, 100.f), GhostFeet);
	if (Distance > Range)
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BlindShotLine), false, this);
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		Params.AddIgnoredActor(*It); // fired into the suspected spot: pawns do not stop the line check
	}
	for (TActorIterator<ATurretActor> It(World); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}
	EShotLineHit Kind = EShotLineHit::Clear;
	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, bCornerShot ? GetCoverFireOrigin() : GetMuzzleLocation(), Ghost.GetAimPoint(), ECC_Visibility, Params) && Hit.GetActor())
	{
		Kind = Hit.GetActor()->IsA<ABarricadeActor>() ? EShotLineHit::Barricade : EShotLineHit::Blocked;
	}
	// Suppression into cover: a barricade at the silhouette does not stop a standing / crouched shooter (JudgeLine).
	const FShotLineVerdict Verdict = SquadFireRules::JudgeLine(Kind, Stance, Elevation);
	if (!Verdict.bCanHit)
	{
		return false;
	}
	Out.Enemy = Ghost.GetSource();
	Out.Distance = Distance;
	Out.Cover = Verdict.Cover;
	Out.bBlind = true;
	Out.AimPoint = Ghost.GetAimPoint();
	return Out.Enemy != nullptr;
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

bool AOperativeCharacter::CanHitEnemy(AActor* Enemy) const
{
	FShootCandidate Candidate;
	bool bBlocked = false;
	return IsLiveEnemy(Enemy) && EvaluateShotLine(Enemy, true, Candidate, bBlocked);
}

EOperativeOrderResult AOperativeCharacter::AutonomousMoveTo(const FVector& Destination)
{
	TGuardValue<bool> Autonomous(bAutonomousOrder, true);
	return OrderMoveTo(Destination, false);
}

bool AOperativeCharacter::HealAlly(AOperativeCharacter& Patient)
{
	UHealthComponent* PatientHealth = Patient.HealthComponent;
	if (MedkitsCount <= 0 || &Patient == this || !PatientHealth || !PatientHealth->IsAlive()
		|| PatientHealth->GetCurrentHealth() >= PatientHealth->GetMaxHealth()
		|| FVector::Dist2D(GetActorLocation(), Patient.GetActorLocation()) > AidReachCm)
	{
		return false;
	}
	const float HealthBefore = PatientHealth->GetCurrentHealth();
	PatientHealth->Heal(PersonalItemRules::GetEffect(EPersonalItem::Medkit).Heal);
	--MedkitsCount;
	const int32 Gained = FMath::FloorToInt(PatientHealth->GetCurrentHealth() - HealthBefore);
	if (UFloatingTextSubsystem* Floating = GetWorld() ? GetWorld()->GetSubsystem<UFloatingTextSubsystem>() : nullptr)
	{
		Floating->Spawn(Patient.GetActorLocation() - FVector(0.f, 0.f, Patient.GetSimpleCollisionHalfHeight() - 210.f),
			FString::Printf(TEXT("🩹 +%d HP"), Gained), FLinearColor(0.2f, 1.f, 0.4f), 0.9f, 70.f);
	}
	UE_LOG(LogCodexTactics, Display, TEXT("%s gives first aid to %s: +%d HP (%d medkits left)"), *DisplayName.ToString(),
		*Patient.DisplayName.ToString(), Gained, MedkitsCount);
	return true;
}

FShootCandidate AOperativeCharacter::FindShootTarget(float DeltaTime, bool bAllowAutoTargets)
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
	// 1a. Blind fire at a silhouette (Sprint 08-F); once the enemy is seen again it becomes the priority target.
	if (BlindFireSource.IsValid() || BlindFireGhost.IsValid())
	{
		AEnemyGhostActor* Ghost = BlindFireGhost.Get();
		AActor* Source = BlindFireSource.Get();
		if (!Ghost)
		{
			if (Source && IsLiveEnemy(Source))
			{
				ManualPriorityTarget = Source;
			}
			BlindFireSource.Reset();
		}
		else if (!Source || !IsLiveEnemy(Source))
		{
			BlindFireGhost.Reset();
			BlindFireSource.Reset();
		}
		else if (EvaluateBlindLine(*Ghost, Candidate))
		{
			CurrentCombatTarget = Source;
			PendingFlankTarget.Reset();
			TargetSwitchTimer = 0.f;
			return Candidate;
		}
	}
	// The fire posture holds the automatic fire (user request 2026-10-06): only the direct orders above count.
	if (!bAllowAutoTargets)
	{
		CurrentCombatTarget.Reset();
		PendingFlankTarget.Reset();
		TargetSwitchTimer = 0.f;
		return FShootCandidate();
	}
	// 1b. Commander Mode: the target the ROE policy picked (Sprint 07-C).
	if (AActor* Chosen = AutonomyTarget.Get())
	{
		if (!IsLiveEnemy(Chosen))
		{
			AutonomyTarget.Reset();
		}
		else if (EvaluateShotLine(Chosen, true, Candidate, bBlocked))
		{
			CurrentCombatTarget = Chosen;
			PendingFlankTarget.Reset();
			TargetSwitchTimer = 0.f;
			return Candidate;
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
	// A melee enemy closing in on him at a cover (a flank rush while he holds the corner, user PIE video CoverBug_02
	// 2026-10-07) takes over at once. Out in the open the Godot stance reaction delay below stays (parity: SquadFireSmoke
	// "flank hound taken after 0.15 s, not at once").
	if ((bInCover || bOpenShotReturnPending) && Current && Closest.Enemy != Current->Enemy && Closest.Distance <= FlankRushBreakCm
		&& !IsRangedEnemyActor(Closest.Enemy))
	{
		CurrentCombatTarget = Closest.Enemy;
		PendingFlankTarget.Reset();
		TargetSwitchTimer = 0.f;
		return Closest;
	}
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

ESquadFirePosture AOperativeCharacter::GetFirePosture() const
{
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	return Squad ? Squad->GetEffectivePosture(this)
		: FirePostureRules::Resolve(FirePostureRules::DefaultPosture, bHasPostureOverride, PostureOverride);
}

bool AOperativeCharacter::MayAutoFireNow() const
{
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	return FirePostureRules::MayAutoFire(GetFirePosture(), bProvokedThisFight, Squad && Squad->IsSquadProvoked(),
		USquadSubsystem::GetPostureConfig());
}

void AOperativeCharacter::NotifyBarricadeBlocked()
{
	if (BarricadeBlockNotifyTimer > 0.f)
	{
		return;
	}
	BarricadeBlockNotifyTimer = 3.5f;
	UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("🚫 Barricade blocks the shot (crouch)!"), FLinearColor(1.f, 0.75f, 0.2f));
}

void AOperativeCharacter::ProcessCombatShooting(float DeltaTime)
{
	// Godot: a panicking soldier neither shoots nor reloads (can_shoot = false, _process_reloading skipped).
	if (!bRecruited || bTacticalCeaseFire || !HealthComponent || !HealthComponent->IsAlive() || (IsPanicking() && !IsRaging()))
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

	// Only auto-shoot when in WaveCombat real-time mode (or if no game flow subsystem exists, e.g. standalone test).
	// Exception (user request 2026-10-06): an Aggressive operative on an ambush level still exploring opens fire on an
	// enemy it sees — a squad attack that starts the ambush fight right here.
	if (UWorld* World = GetWorld())
	{
		if (UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>())
		{
			if (Flow->GetPhase() != ECodexGamePhase::WaveCombat || Flow->GetCombatMode() != ECodexCombatMode::RealTime)
			{
				const ULevelEncounterSubsystem* Encounter = World->GetSubsystem<ULevelEncounterSubsystem>();
				if (!Encounter || bRaging || bIsReloading || ShootTimer > 0.f || (UsesAmmo() && CurrentClip <= 0)
					|| !FirePostureRules::MayAutoFireInExploration(GetFirePosture(), Encounter->IsAmbushCombatStart(), Flow->GetPhase(),
						Flow->IsCombatUnlocked(), USquadSubsystem::GetPostureConfig()))
				{
					return;
				}
				const FShootCandidate Opening = FindShootTarget(DeltaTime, true);
				if (!Opening.Enemy || Opening.bBlind
					|| !ULevelEncounterSubsystem::NotifyHostileContactIn(World, EAmbushTrigger::SquadAutoFire, Opening.Enemy))
				{
					CurrentCombatTarget.Reset();
					return;
				}
				UE_LOG(LogCodexTactics, Display, TEXT("%s opens fire on %s (aggressive posture): the ambush fight starts"),
					*DisplayName.ToString(), *Opening.Enemy->GetName());
				// The fight is on now (WaveCombat / RealTime): the normal fire below shoots this frame.
			}
		}
	}

	// Fire posture (user request 2026-10-06): Passive / unprovoked Defensive operatives open no fire of their own.
	// Sprint 12: Commander Mode may hold the fire behind the cover (direct orders still fire).
	const bool bAutoFire = MayAutoFireNow() && !(bInCover && bCoverHoldFire);

	// Godot: a clustered pack gets a grenade before the rifle (not while raging or reloading).
	if (bAutoFire && !bRaging && !bIsReloading && GrenadesCount > 0 && AIGrenadeCooldown <= 0.f && TryAIGrenadeThrow())
	{
		ShootTimer = 1.f;
		return;
	}

	const bool bInfiniteRageAmmo = bRaging && RageComponent->Config.bInfiniteAmmo;
	if (UsesAmmo() && CurrentClip <= 0 && !bInfiniteRageAmmo)
	{
		if (PanicComponent)
		{
			PanicComponent->OnLowAmmo(); // Godot: the clip ran dry under fire
		}
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
		Shot = FindShootTarget(DeltaTime, bAutoFire);
	}
	AActor* Target = Shot.Enemy;
	if (!Target)
	{
		return;
	}

	// Sniper rifle (user request 2026-10-09, SniperRules): only kneeling / prone and still. A direct order (priority target,
	// blind fire at a silhouette) stops her and she kneels by herself; automatic fire waits while she walks under a move order.
	ESniperFireStep SniperStep = ESniperFireStep::Ready;
	if (IsSniperWeaponEquipped())
	{
		SniperStep = PrepareSniperShot(Target == ManualPriorityTarget.Get() || Shot.bBlind);
		if (SniperStep == ESniperFireStep::WaitForMove)
		{
			return;
		}
	}

	// User decision 2026-10-06: a target out on the open side (in front of the wall) is no corner shot: he steps off the
	// wall and fires normally, then comes back to the slot (UpdateOpenShotReturn).
	if (bInCover && !IsCornerShotTarget(Shot.bBlind ? Shot.AimPoint : Target->GetActorLocation()) && !BeginOpenShotFromCover(Target))
	{
		return; // shimmying: the shot waits for the slot
	}

	// Turn towards target smoothly (the barrel onto it, see UpdateCombatFacing); in cover the back stays to the wall.
	const FVector AimAt = Shot.bBlind ? Shot.AimPoint : Target->GetActorLocation();
	// The arms throw a grenade: no rifle shot until the throw clip is done (CornerHoldSmoke 2026-10-07 caught shots from
	// the throw pose, the barrel 100+ deg off the target).
	if (const UOperativeAnimInstance* ArmsAnim = GetMesh() ? Cast<UOperativeAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
		ArmsAnim && ArmsAnim->IsThrowingGrenade())
	{
		return;
	}
	if (!bInCover)
	{
		const FRotator LookRot(0.f, (AimAt - GetActorLocation()).Rotation().Yaw - BarrelYawOffset, 0.f);
		// Eased, but never slower than AimTurnRateDegPerSec (a quick, visible turn to a flank rush).
		const FRotator Eased = FMath::RInterpTo(GetActorRotation(), LookRot, DeltaTime, 12.0f);
		const FRotator Constant = FMath::RInterpConstantTo(GetActorRotation(), LookRot, DeltaTime, AimTurnRateDegPerSec);
		const float EasedLeft = FMath::Abs(FRotator::NormalizeAxis(LookRot.Yaw - Eased.Yaw));
		const float ConstantLeft = FMath::Abs(FRotator::NormalizeAxis(LookRot.Yaw - Constant.Yaw));
		SetActorRotation(EasedLeft <= ConstantLeft ? Eased : Constant);
		// User rule 2026-10-07: no shot where he does not aim - first the turn, then the shot.
		if (GetShotAimResidualDeg(AimAt) > AimConeDeg)
		{
			return;
		}
	}

	if (bInCover)
	{
		// User design rule 2026-10-06: the target sets the threat side; he turns along the wall to it before the corner
		// shot (lean / pop up / blind) — never a plain shot turned away from the wall.
		CoverShotTarget = Target;
		UpdateCoverFacing(/*bAllowSnap*/ false);
		if (!CoverFacingRules::IsFacingAligned(GetActorRotation().Yaw, CoverSlot, CoverFacing, FCoverFacingConfig().FacingToleranceDeg))
		{
			return;
		}
		// Sustained corner aim (user request 2026-10-07): a corner-shot target in sight keeps him leaned out; after a duck
		// for safety he stays behind the corner for the re-entry delay.
		const UWorld* AimWorld = GetWorld();
		const double Now = AimWorld ? AimWorld->GetTimeSeconds() : 0.0;
		if (bCornerAimActive)
		{
			CornerAimLastTargetTime = Now;
		}
		else if (Now < CornerAimDuckUntil)
		{
			return;
		}
		// Arriving at an edge: no shot until the snap to the corner stand-off is decided / done (CornerEntrySmoke 2026-10-07:
		// a shot fired from the wall while side-stepping to the corner, the rifle not on the target).
		if (bCornerHoldAtEdge && CurrentCoverHeight == ECoverHeight::HighCover && (bCoverSnapPending || (bShimmying && bCoverAutoSnap)))
		{
			return;
		}
		// The corner hold stance fires only once the upper body twisted onto the target (user rule 2026-10-07).
		if (IsCornerHoldSpot() && (!bCornerAimActive || GetShotAimResidualDeg(AimAt) > AimConeDeg))
		{
			return;
		}
	}
	if (ShootTimer <= 0.0f && MisfireCooldownTimer <= 0.0f && SniperStep == ESniperFireStep::Ready)
	{
		if (bInCover)
		{
			BeginCoverShot(); // Sprint 12: lean out, or keep the head down for a blind shot
		}
		TGuardValue<bool> Blind(bBlindShot, Shot.bBlind);
		TGuardValue<FVector> Aim(BlindAimPoint, Shot.AimPoint);
		TGuardValue<bool> CoverBlind(bCoverBlindShot, bInCover && CoverFireMode == ECoverFireMode::BlindFire);
		ShootAtTarget(Target, Shot.Cover);
	}
}

bool AOperativeCharacter::ShootAtTarget(AActor* Target, float Cover)
{
	if (!Target || (UsesAmmo() && CurrentClip <= 0) || !SniperShotAllowed())
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
		UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("❄️ MISFIRE! (Bolt jammed)"), FLinearColor(0.4f, 0.85f, 1.f));
		OnWeaponMisfired.Broadcast(this);
		OnWeaponMisfiredNative.Broadcast(this);
		return false;
	}

	ShootTimer = FMath::Max(0.08f, (CurrentWeapon ? CurrentWeapon->FireRate : 0.65f) * (bRagingShot ? RageComponent->Config.FireRateMultiplier : 1.f));
	// Sprint 08-E: the muzzle flash gives the shooter away for 2 s.
	if (UTacticalSightSubsystem* Sight = GetWorld() ? GetWorld()->GetSubsystem<UTacticalSightSubsystem>() : nullptr)
	{
		Sight->NotifyFired(this);
	}
	// Patrols hear the shot (enemy_perception.json hear_gunshot_m) and engage (user request 2026-10-06).
	AEnemyCharacter::NotifySquadNoise(GetWorld(), GetActorLocation(), ESquadNoise::Gunshot);

	const float Dist = FVector::Dist2D(GetActorLocation(), Target->GetActorLocation());
	const float DistM = Dist / 100.0f;
	const int32 DistCells = FMath::Max(1, FMath::RoundToInt(DistM / 1.5f));

	float HitChance = CurrentWeapon ? CurrentWeapon->GetHitChanceForDistance(DistCells) : 0.85f;
	// Shivering hands above 50 % cold (Godot realtime_cold_aim_penalty_max 0.30).
	if (ColdSurvival && ColdLevel > 50.0f)
	{
		HitChance = FMath::Clamp(HitChance - ColdSurvival->GetAimPenalty(), 0.05f, 0.99f);
	}
	// Blind fire at a silhouette: -80 %, and nothing to hit once the enemy left that spot (Sprint 08-F). Cover blind fire
	// (Sprint 12, head down): -40 %. Both apply multiplied, floored at the tunable minimum (user decision 2026-10-06).
	if (bBlindShot && FVector::Dist2D(ShotFeet(Target), BlindAimPoint) > SightRules::BlindFireHitRadiusCm)
	{
		HitChance = 0.f; // the enemy left the silhouette's spot
	}
	else if (bBlindShot || bCoverBlindShot)
	{
		HitChance = CoverRules::CombinedBlindFireHitChance(CoverRules::GetConfig(), HitChance, bCoverBlindShot, bBlindShot,
			SightRules::BlindFireAccuracyMultiplier);
	}
	if (bInCover)
	{
		(bCoverBlindShot ? CoverBlindShots : CoverLeanShots) += 1;
	}
	else if (bOpenShotReturnPending)
	{
		++CoverOpenShots;
		OpenShotLastTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	}

	const bool bHit = bForceHitForTesting || (FMath::FRand() <= HitChance);
	if (!bHit)
	{
		UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("MISS!"), FLinearColor(0.75f, 0.75f, 0.75f));
	}
	NoteShotAim(Target, bBlindShot ? BlindAimPoint : Target->GetActorLocation(), bBlindShot);
	OnWeaponFired.Broadcast(this, Target, bHit);
	OnWeaponFiredNative.Broadcast(this, Target, bHit);

	// Godot _spawn_muzzle_tracer: to the target, or deflected next to it on a miss.
	if (UCombatFeedbackSubsystem* Feedback = GetWorld() ? GetWorld()->GetSubsystem<UCombatFeedbackSubsystem>() : nullptr)
	{
		FVector End = bBlindShot ? BlindAimPoint : Target->GetActorLocation();
		if (!bHit)
		{
			FVector Offset(FMath::FRandRange(-140.f, 140.f), FMath::FRandRange(-140.f, 140.f), FMath::FRandRange(20.f, 120.f));
			if (Offset.SizeSquared() < 50.f * 50.f)
			{
				Offset = FVector(100.f, 0.f, 50.f);
			}
			End += Offset;
		}
		Feedback->SpawnTracer(GetWeaponMuzzleLocation(), End, CurrentWeapon ? CurrentWeapon->TracerColor : UCombatFeedbackSubsystem::DefaultTracerColor(),
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
			UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("🎯 CRIT x2!"), FLinearColor(1.f, 0.85f, 0.1f));
		}
		else if (Elevation.bElevated)
		{
			UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("⛰️ +15% HEIGHT"), FLinearColor(0.35f, 0.9f, 1.f));
		}

		FDamageSpec Spec;
		Spec.Amount = SquadFireRules::ComputeShotDamage(CurrentWeapon ? CurrentWeapon->BaseDamage : 18.0f, Stance, Cover, bCrit, Elevation, DistanceMultiplier)
			* (bRagingShot ? RageComponent->Config.DamageMultiplier : 1.f);
		Spec.bIsCritical = bCrit;
		Spec.bMelee = !UsesAmmo(); // the knife
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
		// Run telemetry (Godot _record_weapon_shot: hits with their damage, per wave and weapon).
		if (URunTelemetrySubsystem* Telemetry = GetWorld() ? GetWorld()->GetSubsystem<URunTelemetrySubsystem>() : nullptr)
		{
			Telemetry->RecordWeaponHit(this, CurrentWeapon ? CurrentWeapon->WeaponId : FString(TEXT("m16")), Spec.Amount);
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

void AOperativeCharacter::NoteShotAim(AActor* Target, const FVector& Point, bool bBlind)
{
	LastShotTarget = Target;
	LastShotPoint = Point;
	bLastShotBlind = bBlind;
	LastShotTime = GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0e9;
}

bool AOperativeCharacter::GetAimTargetPoint(FVector& OutPoint) const
{
	const UWorld* World = GetWorld();
	if (World && World->GetTimeSeconds() - LastShotTime <= AimTargetHoldSeconds)
	{
		const AActor* Shot = LastShotTarget.Get();
		if (bLastShotBlind || !Shot)
		{
			OutPoint = LastShotPoint;
			return true;
		}
		if (IsValid(Shot))
		{
			OutPoint = Shot->GetActorLocation(); // follows a running target (the tracer's end)
			return true;
		}
	}
	const AActor* Combat = CurrentCombatTarget.Get();
	if (Combat && IsLiveEnemy(Combat))
	{
		OutPoint = Combat->GetActorLocation();
		return true;
	}
	return false;
}

FVector AOperativeCharacter::GetWeaponMuzzleLocation() const
{
	if (WeaponMesh && WeaponMesh->GetStaticMesh() && WeaponMesh->IsVisible())
	{
		static const FName MuzzleSocket(TEXT("Muzzle"));
		const FVector Offset = CurrentWeapon && !CurrentWeapon->HandMesh.IsNull() && !CurrentWeapon->HandMeshMuzzleOffset.IsNearlyZero()
			? CurrentWeapon->HandMeshMuzzleOffset : MuzzleOffset;
		return WeaponMesh->DoesSocketExist(MuzzleSocket) ? WeaponMesh->GetSocketLocation(MuzzleSocket)
			: WeaponMesh->GetComponentTransform().TransformPosition(Offset);
	}
	return GetMuzzleLocation();
}

void AOperativeCharacter::UpdateCombatFacing(float DeltaTime)
{
	// Godot player.gd: combat_facing_direction (_process_combat_shooting) wins over the movement heading
	// (_face_movement_target) while not sprinting, so a single-click move walks sideways / backs off facing the enemy;
	// a sprint turns to the movement and stops the fire.
	bFacingCombatTarget = false;
	if (bInCover)
	{
		// User design rule 2026-10-06: the back stays against the wall (actor yaw = wall normal); the threat side only picks the _L/_R clip
		// (CoverFacingRules); no turn to a target or to the movement (a shimmy away from the threat walks backwards).
		SetActorRotation(FMath::RInterpTo(GetActorRotation(), FRotator(0.f, GetCoverFacingYaw(), 0.f), DeltaTime, 10.f));
		BarrelYawOffset = FMath::FInterpTo(BarrelYawOffset, 0.f, DeltaTime, 6.f);
		return;
	}
	const UGameFlowSubsystem* Flow = GetWorld() ? GetWorld()->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	const bool bRealTimeFight = !Flow || (Flow->GetPhase() == ECodexGamePhase::WaveCombat && Flow->GetCombatMode() == ECodexCombatMode::RealTime);
	AActor* Target = CurrentCombatTarget.Get();
	if (bRealTimeFight && !bSprinting && !bCarrying && !IsPanicking() && Target && IsLiveEnemy(Target) && HealthComponent && HealthComponent->IsAlive())
	{
		bFacingCombatTarget = true;
		const float Yaw = (Target->GetActorLocation() - GetActorLocation()).Rotation().Yaw - BarrelYawOffset;
		SetActorRotation(FMath::RInterpTo(GetActorRotation(), FRotator(0.f, Yaw, 0.f), DeltaTime, 12.f));
	}
	// Godot _face_movement_target / _safe_look_at: otherwise the body turns to the (smoothed) movement above 0.35 m/s
	// with lerp_angle(turn_speed of the stance) and holds still below it — no trembling while braking at the goal.
	SmoothedVelocity = FacingRules::SmoothVelocity(SmoothedVelocity, GetVelocity(), DeltaTime);
	// Turn-based: the grid owns the facing (a shot's FaceAimAt, a step's direction). The parked-follower idle facing from real
	// time used to pull the body back to the leader's old yaw right after every grid shot (user report 2026-10-09).
	const UTurnBasedCombatSubsystem* FacingTurnBased = GetWorld() ? GetWorld()->GetSubsystem<UTurnBasedCombatSubsystem>() : nullptr;
	const bool bGridOwnsFacing = FacingTurnBased && FacingTurnBased->IsActive();
	if (bGridOwnsFacing)
	{
		ClearIdleFacing();
	}
	bool bFacingWall = false;
	// Running into cover (user report 2026-10-09: two pops on entry): the last metres turn the body to face the wall, the
	// pose the enter clip starts in (UpdateCoverEntryBlend takes out what is left of the turn).
	if (!bFacingCombatTarget && bHasPendingCover && !bInCover && SmoothedVelocity.SizeSquared2D() > 35.f * 35.f
		&& FVector::Dist2D(GetActorLocation(), PendingCoverSlot.WorldLocation) < CoverApproachFaceWallCm
		&& FVector::DotProduct(SmoothedVelocity.GetSafeNormal2D(), -PendingCoverSlot.WallNormal.GetSafeNormal2D()) >= 0.5f)
	{
		const float WallYaw = (-PendingCoverSlot.WallNormal).Rotation().Yaw;
		SetActorRotation(FRotator(0.f, FacingRules::StepYaw(GetActorRotation().Yaw, WallYaw, 2.f * OperativeMovementRules::GetTurnRate(MovementConfig, Stance) / 57.2958f, DeltaTime), 0.f));
		bFacingWall = true; // the movement facing below stays out of it this frame
	}
	const float TurnSpeed = OperativeMovementRules::GetTurnRate(MovementConfig, Stance) / 57.2958f; // stored as deg/s, Godot rad/s
	if (!bFacingCombatTarget && !bFacingWall && SmoothedVelocity.SizeSquared2D() > 35.f * 35.f)
	{
		SetActorRotation(FRotator(0.f, FacingRules::StepYaw(GetActorRotation().Yaw, SmoothedVelocity.Rotation().Yaw, TurnSpeed, DeltaTime), 0.f));
	}
	else if (!bFacingCombatTarget && bHasIdleFacing && GetVelocity().SizeSquared2D() < 10.f * 10.f && !bGridOwnsFacing)
	{
		// Every frame (it used to step only with the formation repath, every 0.2 s, and looked jerky).
		SetActorRotation(FRotator(0.f, FacingRules::StepYaw(GetActorRotation().Yaw, IdleFacingYaw, TurnSpeed, DeltaTime), 0.f));
	}

	// Barrel vs body yaw in the current aim pose (the rifle is held across the chest); 0 when not aiming.
	float Offset = 0.f;
	const UOperativeAnimInstance* Anim = GetMesh() ? Cast<UOperativeAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
	// The sniper clips aim straight along the body (measured: the pack's Offset_F holds the rifle on the mesh forward axis).
	if (bAlignBarrelWithTarget && !IsSniperWeaponEquipped() && Anim && Anim->bIsAiming && WeaponMesh && WeaponMesh->GetStaticMesh() && WeaponMesh->IsVisible())
	{
		const FVector Barrel = WeaponMesh->GetComponentTransform().TransformVectorNoScale(MuzzleOffset.GetSafeNormal());
		if (FMath::Abs(Barrel.Z) < 0.7f) // roughly level, i.e. really aimed
		{
			// The pose's own barrel yaw: the 2D aim offset's twist (in these bones since the last evaluation, with the AimYaw
			// still in the anim instance) is taken out - else the facing and AimYaw chase each other (AnimOffset_Bug_01).
			Offset = FMath::Clamp(FRotator::NormalizeAxis(Barrel.Rotation().Yaw - GetActorRotation().Yaw - GetAppliedAimYaw()), -45.f, 45.f);
		}
	}
	BarrelYawOffset = FMath::FInterpTo(BarrelYawOffset, Offset, DeltaTime, 6.f);
}

bool AOperativeCharacter::CanBeginWeaponShot()
{
	if (!HealthComponent || !HealthComponent->IsAlive() || bIsReloading || MisfireCooldownTimer > 0.f || IsKnockedDown())
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
	if (IsSniperWeaponEquipped())
	{
		// Sniper rifle (user request 2026-10-09): she stops / kneels first; the shot waits for her (UpdateSniperPendingShot).
		if (PrepareSniperShot(/*bDirectOrder*/ true) != ESniperFireStep::Ready)
		{
			SniperPendingObject = Target;
			SniperPendingTimer = SniperPendingShotSeconds;
			FaceAimAt(Target->GetActorLocation());
			return false;
		}
		if (!SniperShotAllowed())
		{
			return false;
		}
		if (SniperPendingObject.Get() == Target)
		{
			SniperPendingObject.Reset();
		}
	}
	if (bInCover && !IsCornerShotTarget(Target->GetActorLocation()))
	{
		BeginOpenShotFromCover(Target); // out on the open side: a normal shot off the wall (user decision 2026-10-06)
	}
	if (bInCover)
	{
		// From cover the targeted shot goes round the corner / over the top like any cover shot (user rule 2026-10-06).
		if (!CanFireFromCover())
		{
			return false;
		}
		CoverShotTarget = Target;
		UpdateCoverFacing(/*bAllowSnap*/ false);
		BeginCoverShot();
	}
	else
	{
		StopAndFace(Target->GetActorLocation());
	}

	bool bHit = true;
	FMineShotChance MineShot;
	float DistanceM = 0.f;
	if (Kind == ETargetedShotKind::Mine)
	{
		DistanceM = FVector::Dist(GetActorLocation(), Target->GetActorLocation()) / 100.f;
		MineShot = TargetedShotRules::ComputeMineShotChance(Accuracy, ColdLevel, Stance, DistanceM);
		bHit = bForceHitForTesting || FMath::FRand() * 100.f <= MineShot.Chance;
	}
	NoteShotAim(Target, Target->GetActorLocation(), false);
	OnWeaponFired.Broadcast(this, Target, bHit);
	OnWeaponFiredNative.Broadcast(this, Target, bHit);
	// A targeted shot at a barrel / mine / crate is heard by patrols too (user request 2026-10-06).
	AEnemyCharacter::NotifySquadNoise(GetWorld(), GetActorLocation(), ESquadNoise::Gunshot);

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
		Feedback->SpawnTracer(GetWeaponMuzzleLocation(), End, Color, Style);
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
				"💥 {0}: \"Clean shot (Chance: {1}%, {2}, {3}m)! Mine neutralised remotely!\""),
				DisplayName, Chance, MineShot.StanceName, Distance));
			CastChecked<AProximityMineActor>(Target)->Detonate();
		}
		else
		{
			OperativeShotLine(*this, FText::Format(LOCTEXT("MineMiss",
				"💨 {0}: \"Missed! (Chance was {1}%: range {2}m, {3}). {4} - crouch or move closer!\""),
				DisplayName, Chance, Distance, MineShot.StanceName, TargetedShotRules::GetMineMissReason(Stance, DistanceM, ColdLevel)));
		}
		break;
	}
	case ETargetedShotKind::Crate:
	{
		ALootCrateActor* Crate = CastChecked<ALootCrateActor>(Target);
		if (Crate->bTrapped)
		{
			OperativeShotLine(*this, FText::Format(LOCTEXT("CrateTrapShot", "💥 {0}: \"Shot the crate trap! Remote detonation!\""), DisplayName));
			Crate->DetonateTrap(true, DisplayName);
		}
		else
		{
			// Only a trap blows up (user decision 2026-09-28: Godot also detonating an untrapped crate is a bug).
			OperativeShotLine(*this, LOCTEXT("CratePierced", "💥 The bullet pierced the supply crate."));
		}
		break;
	}
	case ETargetedShotKind::TrappedObject:
		OperativeShotLine(*this, FText::Format(LOCTEXT("TrappedShot", "💥 {0}: \"Shot the tripwire on the object! Remote detonation!\""), DisplayName));
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
		UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("⚠️ ENRAGED! IGNORING ORDERS!"), FLinearColor(1.f, 0.4f, 0.1f));
		return;
	}
	if (IsPanicking())
	{
		UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("⚠️ PANICKING! NOT FIRING!"), FLinearColor(1.f, 0.3f, 0.3f));
		return;
	}
	ManualPriorityTarget = Enemy;
	if (Enemy && !bInCover) // in cover the body stays along the wall (user report 2026-10-09: the order snapped her round, then back)
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

bool AOperativeCharacter::ExecutePlannedTargetedShots()
{
	TMap<ETargetedShotKind, TWeakObjectPtr<AActor>> Plans = MoveTemp(PlannedShots);
	PlannedShots.Reset();
	for (const ETargetedShotKind Kind : { ETargetedShotKind::Barrel, ETargetedShotKind::Mine, ETargetedShotKind::Crate, ETargetedShotKind::TrappedObject })
	{
		const TWeakObjectPtr<AActor>* Target = Plans.Find(Kind);
		if (Target && Target->IsValid() && !ShootAtObject(Target->Get()) && ClassifyShotTarget(Target->Get()) == Kind)
		{
			PlannedShots.Add(Kind, *Target); // could not fire yet: the squad retries it (USquadSubsystem::RetryPlannedShots)
		}
	}
	const TWeakObjectPtr<AActor>* Enemy = Plans.Find(ETargetedShotKind::Enemy);
	if (Enemy && Enemy->IsValid())
	{
		SetManualPriorityTarget(Enemy->Get());
	}
	return PlannedShots.IsEmpty();
}

#undef LOCTEXT_NAMESPACE

void AOperativeCharacter::NotifyWeaponFrozen()
{
	WeaponFreezeNotifyTimer = 2.5f;
	UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("🥶 WEAPON FROZEN! Need a heat source!"), FLinearColor(0.4f, 0.85f, 1.f));
}

float AOperativeCharacter::TakeHit(float Amount, const FString& Attacker, bool bCrit, bool bBypassAvoidance, AActor* AttackerActor,
	float CritMultiplierApplied)
{
	if (!HealthComponent || !HealthComponent->IsAlive())
	{
		return 0.f;
	}
	// Fire posture: an attack (dodged or not) provokes a Defensive operative to fight back (user request 2026-10-06).
	bProvokedThisFight = true;
	if (USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr)
	{
		Squad->NotifyMemberAttacked(this);
	}
	// 1. Dodge on luck (Godot: luck 25 -> 10 %).
	const float DodgeRoll = ForcedDodgeRollForTesting >= 0.f ? (ForcedDodgeRollForTesting >= 1.f ? -1.f : 101.f) : FMath::FRand() * 100.f;
	ForcedDodgeRollForTesting = -1.f;
	if (!bBypassAvoidance && DodgeRoll < Luck * 0.4f)
	{
		UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("💨 DODGE!"), FLinearColor(0.3f, 0.9f, 1.f));
		return 0.f;
	}
	// 2. Stance defense and fortitude cut (Godot: 15 fortitude = 22.5 %, at most 50 %).
	const float Fortitude = ColdSurvival ? ColdSurvival->Fortitude : 15.f;
	const float FortitudeCut = FMath::Clamp(Fortitude * 0.015f, 0.f, 0.5f);
	// Sprint 12 cover (user decision 2026-10-06): a hit from the wall's frontal arc is absorbed 90 % by high cover (35 %
	// crouched / 90 % prone behind low cover), flanking hits pass fully; while the head stays down no hit is a headshot
	// (crit). Blasts / traps (bBypassAvoidance) are not stopped by a wall at the back.
	float CoverAbsorb = 0.f;
	if (bInCover && !bBypassAvoidance && AttackerActor)
	{
		const FCoverCombatConfig& CoverConfig = CoverRules::GetConfig();
		const bool bInArc = CoverRules::IsInFrontalArc(CoverSlot.WallNormal, CoverSlot.WorldLocation, AttackerActor->GetActorLocation(), CoverConfig.FrontalArcDeg);
		CoverAbsorb = CoverRules::AbsorbFraction(CoverConfig, CurrentCoverHeight, Stance, bIsCornerLeaning, bInArc);
		if (bCrit && CoverRules::IsHeadshotImmune(CurrentCoverHeight, bIsCornerLeaning))
		{
			bCrit = false; // the head never showed: the plain hit
			Amount /= FMath::Max(CritMultiplierApplied, 1.f);
			UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("🧱 HEAD DOWN"), FLinearColor(0.6f, 0.85f, 1.f));
		}
		if (CoverAbsorb > 0.f)
		{
			UFloatingTextSubsystem::SpawnAboveOperative(this, FString::Printf(TEXT("🧱 COVER -%d%%"), FMath::RoundToInt(CoverAbsorb * 100.f)),
				FLinearColor(0.5f, 0.8f, 1.f));
		}
	}
	// Sprint 14: lying after a knockdown — ranged hits find a prone profile (x0.6), melee blows a helpless man (x1.5).
	const float KnockdownScale = !bBypassAvoidance && KnockdownComponent && KnockdownComponent->IsDown()
		? KnockdownComponent->GetBlowMultiplier(!AttackerActor ? EKnockdownBlow::Explosion
			: IsRangedEnemyActor(AttackerActor) ? EKnockdownBlow::Ranged : EKnockdownBlow::Melee) : 1.f;
	const float Final = bBypassAvoidance ? FMath::Max(1.f, Amount)
		: FMath::Max(1.f, CoverRules::ApplyAbsorb(Amount, CoverAbsorb) * HealthComponent->GetDefenseMultiplier() * (1.f - FortitudeCut) * KnockdownScale);
	RecentIncomingDamage += Final;
	bHasLastHitSource = true; // before the health loss: the hit reaction / death fall read it
	LastHitSource = AttackerActor ? AttackerActor->GetActorLocation() : GetActorLocation() + GetActorForwardVector() * 100.f;
	if (IsRangedEnemyActor(AttackerActor))
	{
		RecentRangedDamage += Final; // the corner-aim duck counts only what the corner protects from (2026-10-07 horde fix)
	}
	HealthComponent->ApplyDirectHealthLoss(Final, Attacker);
	if (UCombatFeedbackSubsystem* Feedback = GetWorld() ? GetWorld()->GetSubsystem<UCombatFeedbackSubsystem>() : nullptr)
	{
		Feedback->SpawnDamageFlash(this); // Godot _spawn_damage_flash
	}
	// Godot take_damage: a hit makes the carrier drop what the squad is carrying.
	if (bCarrying)
	{
		if (URelocationSubsystem* Relocation = GetWorld() ? GetWorld()->GetSubsystem<URelocationSubsystem>() : nullptr)
		{
			Relocation->DropAllForCombat();
		}
		SetCarrying(false);
	}
	UFloatingTextSubsystem::SpawnAboveOperative(this, bCrit ? FString::Printf(TEXT("💥 CRITICAL HIT! -%d"), FMath::FloorToInt(Final))
		: FString::Printf(TEXT("-%d"), FMath::FloorToInt(Final)), bCrit ? FLinearColor(1.f, 0.25f, 0.1f) : FLinearColor(1.f, 0.3f, 0.3f));
	// Godot rage_comp.on_incoming_hit(attacker_node, is_crit, final_incoming).
	if (RageComponent && HealthComponent->IsAlive())
	{
		RageComponent->OnIncomingHit(AttackerActor, bCrit);
	}
	// Godot panic_comp.on_damage_taken(final_incoming).
	if (PanicComponent && HealthComponent->IsAlive())
	{
		PanicComponent->OnDamageTaken(Final);
	}
	// Sprint 14: a single hit of >= 40 HP knocks him down (blasts / traps go through UKnockdownComponent::NotifyExplosion).
	if (KnockdownComponent && HealthComponent->IsAlive() && !bBypassAvoidance)
	{
		const FVector Source = AttackerActor ? AttackerActor->GetActorLocation() : GetActorLocation() + GetActorForwardVector() * 100.f;
		KnockdownComponent->TryKnockDown(EKnockdownCause::HeavyHit, Source, Final, bCrit);
	}
	return Final;
}

bool AOperativeCharacter::IsKnockedDown() const
{
	return KnockdownComponent && KnockdownComponent->IsDown();
}

void AOperativeCharacter::HandleKnockdownPhase(EKnockdownPhase NewPhase, EKnockdownPhase OldPhase)
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (OldPhase == EKnockdownPhase::None && NewPhase != EKnockdownPhase::None)
	{
		// Interrupts: the walk, sprint, reload, the corner aim / cover (left cleanly), a grenade being aimed.
		StopOperative();
		if (bCornerAimActive)
		{
			EndCornerAim(ECornerAimDecision::DuckForSafety, TEXT("knocked down"));
		}
		if (bInCover)
		{
			LeaveCover(TEXT("knocked down"));
		}
		bHasPendingCover = false;
		bIsReloading = false;
		ReloadTimer = 0.f;
		if (Stance == EOperativeStance::Crouching)
		{
			Stance = EOperativeStance::Standing; // the get-up clip ends standing
			ApplyStanceCapsule();
		}
		if (UGrenadeSubsystem* Grenades = GetWorld() ? GetWorld()->GetSubsystem<UGrenadeSubsystem>() : nullptr; Grenades && Grenades->GetThrower() == this)
		{
			Grenades->CancelAim();
		}
		if (UOperativeAnimInstance* Anim = GetMesh() ? Cast<UOperativeAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr)
		{
			Anim->StopSlotAnimation(0.1f, Anim->UpperBodySlot); // fire / reload / hit / throw clips over the fall
		}
		// Lying on the ground: no walking, the squad and the enemies step over him.
		if (Movement)
		{
			Movement->StopMovementImmediately();
			Movement->DisableMovement();
		}
		if (Capsule)
		{
			KnockdownSavedPawnResponse = Capsule->GetCollisionResponseToChannel(ECC_Pawn);
			Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		}
	}
	else if (NewPhase == EKnockdownPhase::None && OldPhase != EKnockdownPhase::None)
	{
		if (Capsule)
		{
			Capsule->SetCollisionResponseToChannel(ECC_Pawn, KnockdownSavedPawnResponse);
		}
		if (Movement && HealthComponent && HealthComponent->IsAlive())
		{
			Movement->SetMovementMode(MOVE_Walking);
			ApplyMovementParams();
		}
	}
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
		const TCHAR* Callouts[] = { TEXT("💣 Grenade out! Get down!"), TEXT("🧨 They're bunched up! Catch this!"),
			TEXT("💣 Grenade away! Heads down!"), TEXT("🧨 Eat this, bastards!") };
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
		// Godot _on_level_up: +3 points, full heal, floating "⭐ LEVEL N!", radio line.
		UnspentStatPoints += ProgressionRules::PointsPerLevel;
		if (HealthComponent)
		{
			HealthComponent->Heal(HealthComponent->GetMaxHealth() - HealthComponent->GetCurrentHealth());
		}
		UFloatingTextSubsystem::SpawnAboveOperative(this, FString::Printf(TEXT("⭐ LEVEL %d!"), NewLevel), FLinearColor(1.f, 0.85f, 0.1f));
		if (UGameMessageSubsystem* Messages = GetWorld() ? GetWorld()->GetSubsystem<UGameMessageSubsystem>() : nullptr)
		{
			Messages->PostMessage(DisplayName, FText::FromString(FString::Printf(
				TEXT("⭐ NEW LEVEL %d! +3 attribute points to assign!"), NewLevel)));
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

// --- Tactical cover (Sprint 12) ---------------------------------------------------------------------------------

EOperativeOrderResult AOperativeCharacter::OrderTakeCover(const FCoverSlot& Slot, bool bSprint)
{
	if (!Slot.IsValid())
	{
		return EOperativeOrderResult::Unreachable;
	}
	if (IsKnockedDown())
	{
		TWeakObjectPtr<AOperativeCharacter> WeakThis(this);
		KnockdownComponent->BufferOrder([WeakThis, Slot, bSprint]()
		{
			if (AOperativeCharacter* Self = WeakThis.Get())
			{
				Self->OrderTakeCover(Slot, bSprint);
			}
		});
		return EOperativeOrderResult::Accepted;
	}
	if (bInCover && CoverTraceRules::IsSameWall(CoverSlot, Slot))
	{
		return OrderShimmyTo(Slot);
	}
	if (bInCover)
	{
		LeaveCover(TEXT("another cover"));
	}
	PendingCoverSlot = Slot;
	bHasPendingCover = true;
	TGuardValue<bool> CoverOrder(bCoverMoveOrder, true);
	const EOperativeOrderResult Result = OrderMoveTo(Slot.WorldLocation, bSprint);
	if (Result != EOperativeOrderResult::Accepted)
	{
		bHasPendingCover = false;
	}
	else
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Cover] %s: to the %s cover at (%.0f, %.0f)%s"), *DisplayName.ToString(),
			Slot.Height == ECoverHeight::HighCover ? TEXT("high") : TEXT("low"), Slot.WorldLocation.X, Slot.WorldLocation.Y,
			bSprint ? TEXT(" at a sprint") : TEXT(""));
	}
	return Result;
}

void AOperativeCharacter::SetPendingCover(const FCoverSlot& Slot)
{
	PendingCoverSlot = Slot;
	bHasPendingCover = Slot.IsValid();
}

void AOperativeCharacter::EnterCover(const FCoverSlot& Slot)
{
	if (!Slot.IsValid() || !HealthComponent || !HealthComponent->IsAlive())
	{
		return;
	}
	const bool bWasInCover = bInCover;
	const bool bSameWall = bWasInCover && CoverTraceRules::IsSameWall(CoverSlot, Slot);
	StopOperative();
	CoverSlot = Slot;
	bInCover = true;
	CurrentCoverHeight = Slot.Height;
	bShimmying = false;
	ShimmyDirection = 0.f;
	bIsCornerLeaning = false;
	bIsBlindFiring = false;
	LeanTimer = 0.f;
	BlindFireTimer = 0.f;
	bHasPendingCover = false;
	bOpenShotReturnPending = false; // a cover entry ends any open shot
	bCornerAimActive = false;
	bQuietCoverEntry = bQuietCoverEntryRequest; // the return after an open shot: no Cover_Enter clip
	bCoverEntryFromRun = bCoverEntryFromRunRequest;
	bCoverLeftToMove = false;
	// A fresh wall: walk to the threat-side edge on the next update if it is close (a player's shimmy is never undone).
	bCoverSnapPending = !bSameWall;
	bCoverAutoSnap = false;
	if (!bSameWall)
	{
		bHasCoverThreat = false;
		CoverThreatSeenTime = -1.0e9;
		CoverShotTarget.Reset();
		CoverFacing = PredictCoverFacing(Slot);
	}
	// Back to the wall, on the slot (the feet keep their ground height), facing along the wall towards the threat side.
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetSimpleCollisionHalfHeight());
	SetActorLocation(FVector(Slot.WorldLocation.X, Slot.WorldLocation.Y, Feet.Z + GetSimpleCollisionHalfHeight()), true);
	CoverThreatTimer = 0.f;
	UpdateCoverFacing(/*bAllowSnap*/ false);
	if (!bSameWall)
	{
		// The actor turns its back to the wall at once (user report 2026-10-09: pops on entry). With an enter clip the clip's
		// first frame faces the wall - the pose he arrives in (the approach turns him to the wall, UpdateCombatFacing) - and the
		// anim blends it in fast when the turn is large (GetCoverEntryTurnDeg). Without one the body's old yaw is drawn back on
		// the mesh and eased out (UpdateCoverEntryBlend).
		const float OldYaw = GetActorRotation().Yaw;
		SetActorRotation(FRotator(0.f, GetCoverFacingYaw(), 0.f));
		CoverEntryTurnDeg = FMath::Abs(FRotator::NormalizeAxis(OldYaw - GetActorRotation().Yaw));
		const UOperativeAnimInstance* EntryAnim = GetMesh() ? Cast<UOperativeAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
		const auto HasClip = [](const TArray<TObjectPtr<UAnimSequenceBase>>& Clips) { return Clips.ContainsByPredicate([](const TObjectPtr<UAnimSequenceBase>& Clip) { return Clip != nullptr; }); };
		const bool bEnterClip = EntryAnim && EntryAnim->bUseNativeCoverClips && (HasClip(EntryAnim->CoverStandEnter) || HasClip(EntryAnim->CoverCrouchEnter));
		CoverEntryYawOffset = FRotator::NormalizeAxis(OldYaw - GetActorRotation().Yaw);
		CoverEntryYawTime = bQuietCoverEntry || bEnterClip ? -1.f : 0.f;
		bBodyYawFree = false;
		// With an enter clip and a big turn the clip comes in at once (CoverEnterTurnedBlendSeconds), one anim update later:
		// until it plays, the old pose keeps its old yaw on the mesh (no frame of the run pose turned round).
		bCoverEntryAwaitClip = bEnterClip && !bQuietCoverEntry && CoverEntryTurnDeg > 90.f;
	}
	ClearIdleFacing();
	if (!bSameWall)
	{
		// High cover: standing (the wall covers him); low cover: crouched. Prone stays prone (hidden by the 60 cm rule).
		const EOperativeStance Default = CoverRules::DefaultStanceFor(Slot.Height);
		if (Stance != EOperativeStance::Prone && Stance != Default && !(ColdSurvival && ColdSurvival->IsFrostbitten()))
		{
			SetStance(Default);
		}
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Cover] %s: %s %s cover at (%.0f, %.0f), facing %s, corners L %d R %d"), *DisplayName.ToString(),
		bSameWall ? TEXT("shimmied along the") : TEXT("entered the"), Slot.Height == ECoverHeight::HighCover ? TEXT("high") : TEXT("low"),
		Slot.WorldLocation.X, Slot.WorldLocation.Y, CoverFacing == ECoverFacing::Left ? TEXT("left") : TEXT("right"),
		Slot.bLeftEdgeExposed ? 1 : 0, Slot.bRightEdgeExposed ? 1 : 0);
	if (!bWasInCover)
	{
		UFloatingTextSubsystem::SpawnAboveOperative(this, Slot.Height == ECoverHeight::HighCover ? TEXT("🧱 AT THE WALL") : TEXT("🧱 BEHIND COVER"),
			FLinearColor(0.5f, 0.8f, 1.f));
		if (UGameMessageSubsystem* Messages = GetWorld() ? GetWorld()->GetSubsystem<UGameMessageSubsystem>() : nullptr)
		{
			Messages->PostMessage(DisplayName, FText::FromString(Slot.Height == ECoverHeight::HighCover
				? TEXT("🧱 Hugging the wall, holding the corner!") : TEXT("🧱 Behind cover!")));
		}
		ReceiveCoverChanged(true, CurrentCoverHeight);
	}
}

void AOperativeCharacter::LeaveCover(const FString& Reason)
{
	if (!bInCover)
	{
		return;
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[Cover] %s: left the cover (%s)"), *DisplayName.ToString(), *Reason);
	bInCover = false;
	CurrentCoverHeight = ECoverHeight::None;
	bShimmying = false;
	ShimmyDirection = 0.f;
	bIsCornerLeaning = false;
	bIsBlindFiring = false;
	bCoverHoldFire = false;
	LeanTimer = 0.f;
	BlindFireTimer = 0.f;
	bAtCoverCorner = false;
	bHasCoverThreat = false;
	CoverThreatSeenTime = -1.0e9;
	bCoverAutoSnap = false;
	bCoverSnapPending = false;
	bCornerAimActive = false; // the sustained corner aim ends with the cover
	bCoverEntryFromRun = false;
	CoverShotTarget.Reset();
	CoverSlot = FCoverSlot();
	bQuietCoverEntry = false;
	ReceiveCoverChanged(false, ECoverHeight::None);
}

EOperativeOrderResult AOperativeCharacter::OrderShimmyTo(const FCoverSlot& Target)
{
	if (!bInCover || !Target.IsValid() || !CoverTraceRules::IsSameWall(CoverSlot, Target))
	{
		return EOperativeOrderResult::Unreachable;
	}
	if (IsRaging() || IsPanicking())
	{
		return EOperativeOrderResult::Refused;
	}
	const float Along = CoverTraceRules::AlongWallDistance(CoverSlot, Target.WorldLocation);
	if (FMath::Abs(Along) < 10.f)
	{
		return EOperativeOrderResult::Accepted; // already there
	}
	if (bCornerAimActive)
	{
		EndCornerAim(ECornerAimDecision::ReturnNoTargets, TEXT("shimmy ordered"));
	}
	PendingCoverSlot = Target;
	PendingCoverSlot.WallNormal = CoverSlot.WallNormal; // one wall, one facing
	bHasPendingCover = true;
	ShimmyDirection = Along > 0.f ? 1.f : -1.f;
	// No threat known: face forward in the direction of the move (and idle facing that side afterwards).
	CoverFacing = CoverFacingRules::FacingForShimmy(CoverFacing, ShimmyDirection, bHasCoverThreat);
	bShimmying = true;
	bIsCornerLeaning = false;
	bIsBlindFiring = false;
	bCoverSnapPending = false;
	bCoverAutoSnap = false; // TrySnapToCoverCorner sets it after this call
	// Side-step at the crouch-walk pace along the wall, facing the threat side (UpdateCombatFacing holds the facing):
	// forward towards it, backwards away from it (CoverFacingRules::IsShimmyForward).
	TGuardValue<bool> CoverOrder(bCoverMoveOrder, true);
	bSprinting = false;
	ApplyMovementParams(OperativeMovementRules::ComputeMaxSpeed(MovementConfig, EOperativeStance::Crouching, false, IsWounded(), bCarrying) * ColdSpeedMultiplier);
	const EOperativeOrderResult Result = RequestMove(Target.WorldLocation);
	if (Result != EOperativeOrderResult::Accepted)
	{
		bHasPendingCover = false;
		bShimmying = false;
		ShimmyDirection = 0.f;
		ApplyMovementParams();
	}
	else
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Cover] %s: shimmies %.0f cm to the %s (%s, facing %s)"), *DisplayName.ToString(), FMath::Abs(Along),
			Along > 0.f ? TEXT("right") : TEXT("left"), IsShimmyForward() ? TEXT("forward") : TEXT("backwards"),
			CoverFacing == ECoverFacing::Left ? TEXT("left") : TEXT("right"));
	}
	return Result;
}

void AOperativeCharacter::SetCoverFireMode(ECoverFireMode Mode)
{
	CoverFireMode = Mode == ECoverFireMode::Normal ? ECoverFireMode::CornerLean : Mode;
}

ECoverFireMode AOperativeCharacter::ToggleCoverFireMode()
{
	SetCoverFireMode(CoverFireMode == ECoverFireMode::BlindFire ? ECoverFireMode::CornerLean : ECoverFireMode::BlindFire);
	UFloatingTextSubsystem::SpawnAboveOperative(this, CoverFireMode == ECoverFireMode::BlindFire ? TEXT("🙈 BLIND FIRE (-40%)") : TEXT("👁️ CORNER FIRE"),
		FLinearColor(0.9f, 0.85f, 0.4f));
	return CoverFireMode;
}

bool AOperativeCharacter::CanFireFromCover() const
{
	if (!bInCover)
	{
		return true;
	}
	if (CurrentCoverHeight == ECoverHeight::LowCover)
	{
		return true; // over the top
	}
	return CoverSlot.HasExposedEdge();
}

FVector AOperativeCharacter::GetCoverFireOrigin() const
{
	const FVector Muzzle = GetMuzzleLocation();
	if (!bInCover || CurrentCoverHeight != ECoverHeight::HighCover)
	{
		return Muzzle;
	}
	return CornerFireOriginFor(CoverSlot, CoverFacing, Muzzle);
}

FVector AOperativeCharacter::CornerFireOriginFor(const FCoverSlot& Slot, ECoverFacing Facing, const FVector& Muzzle)
{
	// Round the facing's corner (or the only exposed one).
	const ECoverFacing Side = Slot.IsEdgeExposed(Facing) ? Facing : (Slot.bLeftEdgeExposed ? ECoverFacing::Left : ECoverFacing::Right);
	const float EdgeDistance = Side == ECoverFacing::Left ? Slot.LeftEdgeDistanceCm : Slot.RightEdgeDistanceCm;
	return CoverRules::CornerMuzzle(Slot, Side, Muzzle, FMath::Max(CoverRules::GetConfig().CornerPeekOffsetCm, EdgeDistance + 20.f));
}

bool AOperativeCharacter::IsHiddenInCoverFrom(const FVector& ObserverLocation) const
{
	if (!bInCover)
	{
		return false;
	}
	const bool bInArc = CoverRules::IsInFrontalArc(CoverSlot.WallNormal, CoverSlot.WorldLocation, ObserverLocation, CoverRules::GetConfig().FrontalArcDeg);
	return CoverRules::HiddenFromObserver(CurrentCoverHeight, bIsCornerLeaning, bInArc);
}

void AOperativeCharacter::BeginCoverShot()
{
	CoverLastShotTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const FCoverCombatConfig& Config = CoverRules::GetConfig();
	if (CoverFireMode == ECoverFireMode::BlindFire)
	{
		bIsBlindFiring = true;
		bIsCornerLeaning = false;
		BlindFireTimer = Config.BlindFireHoldSeconds;
	}
	else
	{
		bIsCornerLeaning = true;
		bIsBlindFiring = false;
		LeanTimer = Config.LeanHoldSeconds;
		BeginCornerAim();
	}
}

void AOperativeCharacter::UpdateCover(float DeltaTime)
{
	RecentIncomingDamage = CoverDecisionRules::DecayRecentDamage(FCoverDecisionConfig(), RecentIncomingDamage, DeltaTime);
	RecentRangedDamage = CoverDecisionRules::DecayRecentDamage(FCoverDecisionConfig(), RecentRangedDamage, DeltaTime);
	// Grid walks (turn-based) and planned walks end without HandleMoveFinished: enter the pending slot on arrival.
	if (bHasPendingCover && !bHasMoveOrder && GetVelocity().SizeSquared2D() < 4.f
		&& FVector::Dist2D(GetActorLocation(), PendingCoverSlot.WorldLocation) <= 120.f)
	{
		const UTurnBasedCombatSubsystem* TurnBased = GetWorld() ? GetWorld()->GetSubsystem<UTurnBasedCombatSubsystem>() : nullptr;
		if (!TurnBased || TurnBased->GetTacticalMoveSpeed(this) < 0.f)
		{
			bHasPendingCover = false;
			EnterCover(PendingCoverSlot);
		}
	}
	UpdateOpenShotReturn();
	if (!bInCover)
	{
		return;
	}
	UpdateCornerAim(DeltaTime);
	// Corner hold (user decision 2026-10-07): at the exposed edge he steps out into the fire stance at once - also right
	// after a reload - and holds it.
	if (!bCornerAimActive && !bIsReloading && IsCornerHoldSpot() && !(bCornerAutoDuck && GetWorld() && GetWorld()->GetTimeSeconds() < CornerAimDuckUntil))
	{
		BeginCornerAim();
	}
	if (CVarDebugCornerAim.GetValueOnGameThread() > 0 && GetWorld())
	{
		const double Now = GetWorld()->GetTimeSeconds();
		const FString Head = bCornerAimActive ? FString(IsCornerHoldSpot() ? TEXT("CORNER HOLD") : TEXT("CORNER AIM")) : Now < CornerAimDuckUntil
			? FString::Printf(TEXT("DUCKED (%s, %.1fs)"), CoverDecisionRules::CornerAimDecisionName(LastCornerAimBreak), CornerAimDuckUntil - Now)
			: FString(TEXT("in cover"));
		DrawDebugString(GetWorld(), GetActorLocation() + FVector(0.f, 0.f, 120.f), Head + TEXT("\n") + CornerAimLabel, nullptr,
			bCornerAimActive ? FColor::Green : FColor::Orange, 0.f, true, 1.f);
	}
	if (bIsCornerLeaning && !bCornerAimActive)
	{
		LeanTimer -= DeltaTime;
		if (LeanTimer <= 0.f)
		{
			bIsCornerLeaning = false; // back behind the corner
		}
	}
	if (bIsBlindFiring)
	{
		BlindFireTimer -= DeltaTime;
		if (BlindFireTimer <= 0.f)
		{
			bIsBlindFiring = false;
		}
	}
	if (bShimmying && !bHasMoveOrder && GetVelocity().SizeSquared2D() < 4.f)
	{
		bShimmying = false;
		ShimmyDirection = 0.f;
		bCoverAutoSnap = false;
	}
	// Facing along the wall towards the last known threat (user design rule 2026-10-06).
	CoverThreatTimer -= DeltaTime;
	if (CoverThreatTimer <= 0.f)
	{
		CoverThreatTimer = FCoverFacingConfig().ThreatUpdateSeconds;
		UpdateCoverFacing(/*bAllowSnap*/ true);
	}
	if (bCoverSnapPending && !bShimmying && !bHasMoveOrder)
	{
		TrySnapToCoverCorner();
	}
}

float AOperativeCharacter::GetCoverFacingYaw() const
{
	return CoverFacingRules::FacingYaw(CoverSlot, CoverFacing);
}

bool AOperativeCharacter::IsShimmyForward() const
{
	return CoverFacingRules::IsShimmyForward(CoverFacing, ShimmyDirection, bHasCoverThreat);
}

bool AOperativeCharacter::GatherCoverThreat(FVector& OutLocation) const
{
	UWorld* World = GetWorld();
	if (!World || bIgnoreCoverThreatForTesting)
	{
		return false;
	}
	const FCoverFacingConfig Config;
	const FVector Here = GetActorLocation();
	TArray<FCoverThreatCandidate> Candidates;
	auto IsAlive = [](const AActor* Actor)
	{
		const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Actor);
		return IsValid(Actor) && (!Enemy || (!Enemy->IsDying() && Enemy->GetHealthComponent() && Enemy->GetHealthComponent()->IsAlive()));
	};
	auto Add = [&Candidates, &Here](const FVector& Location, ECoverThreatSource Source)
	{
		FCoverThreatCandidate& Candidate = Candidates.AddDefaulted_GetRef();
		Candidate.Location = Location;
		Candidate.Source = Source;
		Candidate.DistanceCm = static_cast<float>(FVector::Dist2D(Here, Location));
	};
	// The enemy he is ordered to / does shoot at (while it is in sight; out of sight its silhouette counts as heard).
	for (const AActor* Priority : { CoverShotTarget.Get(), ManualPriorityTarget.Get(), CurrentCombatTarget.Get() })
	{
		if (Priority && IsAlive(Priority) && !Priority->IsHidden())
		{
			Add(Priority->GetActorLocation(), ECoverThreatSource::PriorityTarget);
			break;
		}
	}
	// Enemies seen now (the tactical sight hides the unseen ones in a fight) and heard / remembered silhouettes.
	for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
	{
		if (IsAlive(*It) && !It->IsHidden() && FVector::Dist2D(Here, It->GetActorLocation()) <= Config.MaxThreatDistanceCm)
		{
			Add(It->GetActorLocation(), ECoverThreatSource::Visible);
		}
	}
	for (TActorIterator<AEnemyGhostActor> It(World); It; ++It)
	{
		if (!It->IsHidden() && FVector::Dist2D(Here, It->GetActorLocation()) <= Config.MaxThreatDistanceCm)
		{
			Add(It->GetActorLocation(), ECoverThreatSource::Heard);
		}
	}
	const int32 Best = CoverFacingRules::PickThreat(Candidates);
	if (Best == INDEX_NONE)
	{
		return false;
	}
	OutLocation = Candidates[Best].Location;
	return true;
}

ECoverFacing AOperativeCharacter::PredictCoverFacing(const FCoverSlot& Slot) const
{
	// Corner hold (user decision 2026-10-07): a slot at the one exposed edge of a high wall faces that edge - he steps out
	// there at once (CornerHoldSmoke: re-entering after an open shot he faced the closed side for 5 s).
	if (bCornerHoldAtEdge && Slot.Height == ECoverHeight::HighCover && Slot.bLeftEdgeExposed != Slot.bRightEdgeExposed)
	{
		const ECoverFacing Edge = Slot.bLeftEdgeExposed ? ECoverFacing::Left : ECoverFacing::Right;
		if (CoverFacingRules::IsAtCorner(Slot, Edge, FCoverFacingConfig()))
		{
			return Edge;
		}
	}
	FVector Threat;
	if (GatherCoverThreat(Threat))
	{
		return CoverFacingRules::ThreatAlongWall(Slot, Threat) < 0.f ? ECoverFacing::Left : ECoverFacing::Right;
	}
	return CoverFacingRules::DefaultSide(Slot, CoverFacing);
}

void AOperativeCharacter::UpdateCoverFacing(bool bAllowSnap)
{
	if (!bInCover)
	{
		return;
	}
	const FCoverFacingConfig Config;
	FVector Threat;
	if (GatherCoverThreat(Threat))
	{
		bHasCoverThreat = true; // remembered: the facing keeps the last known direction when nobody is in sight
		CoverThreatLocation = Threat;
		CoverThreatSeenTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0; // ... the fire-ready pose only while it is fresh
	}
	const ECoverFacing Old = CoverFacing;
	// No threat known: the facing stays (nearest edge from PredictCoverFacing on entry, then the last movement direction).
	CoverFacing = bHasCoverThreat ? CoverFacingRules::ResolveThreatSide(CoverSlot, CoverThreatLocation, CoverFacing, Config.ThreatSideHysteresisCm)
		: CoverFacing;
	// Engaged (leaned out, or a cover shot within CoverEngageHoldSeconds): no flip-flop of the fire stance between the sides.
	{
		const UWorld* FacingWorld = GetWorld();
		// Firing (a shot within CoverEngageHoldSeconds) keeps the side - also at the corner hold, where the stance is held
		// between the shots; a threat that stays on the other side without a shot for that long turns him (the hold ends).
		const bool bEngaged = (bCornerAimActive && bCornerAutoDuck)
			|| (FacingWorld && FacingWorld->GetTimeSeconds() - CoverLastShotTime <= CoverEngageHoldSeconds);
		CoverFacing = CoverFacingRules::KeepEngagedFacing(CoverSlot, Old, CoverFacing, bEngaged && !bShimmying);
	}
	// Corner hold (user decision 2026-10-07: no side flips at that corner): at the one exposed edge of a high wall he faces
	// that edge whatever the threat side - the closed side has nothing to fire round (CornerHoldSmoke 2026-10-07: back at
	// the wall after an open shot he turned to the closed side for a threat there and never leaned out again).
	if (bCornerHoldAtEdge && !bShimmying && CoverSlot.Height == ECoverHeight::HighCover && CoverSlot.bLeftEdgeExposed != CoverSlot.bRightEdgeExposed)
	{
		const ECoverFacing Edge = CoverSlot.bLeftEdgeExposed ? ECoverFacing::Left : ECoverFacing::Right;
		if (CoverFacingRules::IsAtCorner(CoverSlot, Edge, Config))
		{
			CoverFacing = Edge;
		}
	}
	bAtCoverCorner = CoverFacingRules::IsAtCorner(CoverSlot, CoverFacing, Config);
	if (CoverFacing != Old)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Cover] %s: turns along the wall to the %s (threat %s)"), *DisplayName.ToString(),
			CoverFacing == ECoverFacing::Left ? TEXT("left") : TEXT("right"), bHasCoverThreat ? TEXT("known") : TEXT("none, nearest edge"));
		bCoverSnapPending |= bAllowSnap && !bShimmying;
	}
}

bool AOperativeCharacter::IsCoverThreatActive() const
{
	const UWorld* World = GetWorld();
	return bInCover && bHasCoverThreat && World && World->GetTimeSeconds() - CoverThreatSeenTime <= CoverFireReadyHoldSeconds;
}

bool AOperativeCharacter::IsCoverFireReady() const
{
	const UWorld* World = GetWorld();
	if (!bInCover || !World || bIsReloading)
	{
		return false;
	}
	if (bCornerAimActive)
	{
		return true; // sustained corner aim: the fire stance between the shots
	}
	const double Now = World->GetTimeSeconds();
	if (Now < CornerAimDuckUntil || Now - CornerAimEndTime < CoverFireReadyHoldSeconds)
	{
		return false; // ducked back / just relaxed: the plain cover pose
	}
	// User decision 2026-10-07: the fire-ready corner pose only for a threat behind the wall / round the corner (the same
	// rule as the corner shot); a threat out in front keeps the plain cover idle (it gets an open shot).
	// A low cover is fired over anywhere along it (the crouched fire stance pops the rifle over the top): no corner needed.
	const bool bFiringSpot = CoverFacingRules::IsFiringSpot(CurrentCoverHeight, bAtCoverCorner);
	return CoverFacingRules::IsFireReady(bFiringSpot, bShimmying, bHasCoverThreat, static_cast<float>(Now - CoverThreatSeenTime),
		CoverFireReadyHoldSeconds) && IsCornerShotTarget(CoverThreatLocation);
}

void AOperativeCharacter::LeaveCoverToMove(const FString& Reason)
{
	if (!bInCover)
	{
		return;
	}
	LeaveCover(Reason);
	bCoverLeftToMove = true; // cleared by the next cover entry
}

void AOperativeCharacter::TryEnterCoverFromRun()
{
	// User-found bug 2026-10-07: running into cover the run stopped dead at the slot and the enter clip popped the body
	// 76 cm back out (its start pose stands out from the wall facing it). Now the cover is entered where that start pose
	// stands (plus a glide for the run's momentum): the capsule goes to the slot, the mesh is offset so the body stays
	// where it ran and glides on, decelerating, while the clip blends in (UpdateCoverEntryBlend).
	if (!bHasPendingCover || !bHasMoveOrder || bInCover || bShimmying || IsRaging() || IsPanicking())
	{
		return;
	}
	const UWorld* World = GetWorld();
	const UTurnBasedCombatSubsystem* TurnBased = World ? World->GetSubsystem<UTurnBasedCombatSubsystem>() : nullptr;
	if (!World || (TurnBased && TurnBased->IsActive()))
	{
		return; // grid walks end on their cell (UpdateCover enters there)
	}
	const UOperativeAnimInstance* Anim = GetMesh() ? Cast<UOperativeAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
	const EOperativeStance EntryStance = CoverRules::DefaultStanceFor(PendingCoverSlot.Height) == EOperativeStance::Crouching
		|| Stance == EOperativeStance::Crouching ? EOperativeStance::Crouching : EOperativeStance::Standing;
	const bool bCrouchEntry = EntryStance == EOperativeStance::Crouching;
	const TArray<TObjectPtr<UAnimSequenceBase>>* EnterClips = Anim ? (bCrouchEntry ? &Anim->CoverCrouchEnter : &Anim->CoverStandEnter) : nullptr;
	const bool bHasEnterClip = Anim && Anim->bUseNativeCoverClips && EnterClips && EnterClips->ContainsByPredicate([](const TObjectPtr<UAnimSequenceBase>& Clip) { return Clip != nullptr; });
	const float ClipLead = bHasEnterClip ? (bCrouchEntry ? CoverEnterClipLeadCrouchCm : CoverEnterClipLeadStandCm) : 0.f;
	const float Speed = static_cast<float>(GetVelocity().Size2D());
	const float Glide = FMath::Min(Speed * CoverEnterDecelSeconds * 0.5f, CoverEnterMaxGlideCm);
	const FVector Here = GetActorLocation();
	const float Distance = static_cast<float>(FVector::Dist2D(Here, PendingCoverSlot.WorldLocation));
	if (Distance > ClipLead + Glide || Distance < 1.f || Speed < 20.f)
	{
		return; // not there yet (or standing: HandleMoveFinished / UpdateCover enter at the slot)
	}
	// The enter clip starts facing the wall out in front of it: only an approach towards the wall matches it (walking in
	// along the wall it would glide the body sideways; then he enters on arrival as before).
	if (FVector::DotProduct(GetVelocity().GetSafeNormal2D(), -PendingCoverSlot.WallNormal.GetSafeNormal2D()) < 0.5f)
	{
		return;
	}
	const FCoverSlot Slot = PendingCoverSlot;
	bHasPendingCover = false;
	{
		TGuardValue<bool> FromRun(bCoverEntryFromRunRequest, true);
		EnterCover(Slot);
	}
	if (!bInCover)
	{
		return;
	}
	const FVector Delta = Here - GetActorLocation();
	CoverEntryClipOffset = Slot.WallNormal.GetSafeNormal2D() * ClipLead; // where the clip's start pose stands from the slot
	CoverEntryGlide = FVector(Delta.X, Delta.Y, 0.f) - CoverEntryClipOffset;
	CoverEntryBlendTime = 0.f;
	UE_LOG(LogCodexTactics, Display, TEXT("[Cover] %s: runs into the cover (%.0f cm out, speed %.0f, clip lead %.0f, glide %.0f)"), *DisplayName.ToString(),
		Distance, Speed, ClipLead, CoverEntryGlide.Size2D());
}

void AOperativeCharacter::UpdateCoverEntryBlend(float DeltaTime)
{
	FVector Wanted = FVector::ZeroVector;
	if (CoverEntryBlendTime >= 0.f)
	{
		if (!bInCover)
		{
			CoverEntryBlendTime = -1.f; // left again: no offset
		}
		else
		{
			CoverEntryBlendTime += DeltaTime;
			const UOperativeAnimInstance* Anim = GetMesh() ? Cast<UOperativeAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
			const float Weight = Anim ? Anim->GetCoverEnterBlendWeight() : -1.f;
			// The clip's start pose stands CoverEntryClipOffset out: while it blends in (weight w) the rest of the pose (the
			// run) must be drawn there too; the glide (momentum + approach error) eases out over CoverEnterDecelSeconds.
			const float W = Weight >= 0.f ? FMath::Clamp(Weight, 0.f, 1.f) : FMath::Clamp(CoverEntryBlendTime / 0.25f, 0.f, 1.f);
			const float S = FMath::Clamp(CoverEntryBlendTime / FMath::Max(CoverEnterDecelSeconds, 0.05f), 0.f, 1.f);
			const FVector ClipPart = Weight >= 0.f || CoverEntryBlendTime < 0.25f ? (1.f - W) * CoverEntryClipOffset : FVector::ZeroVector;
			Wanted = ClipPart + CoverEntryGlide * FMath::Square(1.f - S);
			if ((W >= 1.f || Weight < 0.f) && S >= 1.f && CoverEntryBlendTime >= 0.25f)
			{
				CoverEntryBlendTime = -1.f;
				Wanted = FVector::ZeroVector;
			}
		}
	}
	// Low cover (user request 2026-10-07): the pack's fire stance is a sideways lean round an edge (crouched _L +73 cm,
	// _R -40 cm); over a low cover he fires over the top where he is, so the clips' sideways root step is cancelled.
	if (bInCover && CurrentCoverHeight == ECoverHeight::LowCover && !bShimmying && GetMesh())
	{
		const FVector RootComponentSpace = GetMesh()->GetSocketTransform(TEXT("root"), RTS_Component).GetLocation();
		const FVector RootWorldOffset = GetMesh()->GetComponentTransform().TransformVector(RootComponentSpace);
		const FVector Tangent = CoverSlot.RightTangent();
		Wanted -= Tangent * FVector::DotProduct(RootWorldOffset, Tangent);
	}
	// The body's yaw on entry: (1 - enter clip weight) of the turn the actor made at once is drawn back on the mesh.
	float WantedYaw = 0.f;
	if (bCoverEntryAwaitClip)
	{
		const UOperativeAnimInstance* AwaitAnim = GetMesh() ? Cast<UOperativeAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
		if (bInCover && AwaitAnim && AwaitAnim->GetCoverEnterBlendWeight() < 0.5f)
		{
			WantedYaw = CoverEntryYawOffset;
		}
		else
		{
			bCoverEntryAwaitClip = false;
		}
	}
	if (CoverEntryYawTime >= 0.f)
	{
		CoverEntryYawTime += DeltaTime;
		const float W = FMath::Clamp(CoverEntryYawTime / FMath::Max(CoverEntryYawBlendSeconds, 0.05f), 0.f, 1.f);
		const bool bKeep = bInCover || bBodyYawFree;
		WantedYaw = bKeep ? CoverEntryYawOffset * (1.f - FMath::SmoothStep(0.f, 1.f, W)) : 0.f;
		if (!bKeep || W >= 1.f || CoverEntryYawTime > 2.f)
		{
			CoverEntryYawTime = -1.f;
			bBodyYawFree = false;
			WantedYaw = 0.f;
		}
	}
	if (!FMath::IsNearlyEqual(WantedYaw, CoverEntryAppliedYaw, 0.01f) && GetMesh())
	{
		GetMesh()->AddRelativeRotation(FRotator(0.f, WantedYaw - CoverEntryAppliedYaw, 0.f));
		CoverEntryAppliedYaw = WantedYaw;
	}
	// Additive on the mesh's relative location (crouch / other systems move it too), tracked in the actor's local frame
	// so a turn during the blend never leaves a residue.
	const FVector WantedLocal = Wanted.IsNearlyZero(0.01) ? FVector::ZeroVector : GetActorRotation().UnrotateVector(Wanted);
	if (!WantedLocal.Equals(CoverEntryAppliedOffset, 0.01) && GetMesh())
	{
		GetMesh()->AddRelativeLocation(WantedLocal - CoverEntryAppliedOffset);
		CoverEntryAppliedOffset = WantedLocal;
	}
}

FCoverDecisionConfig AOperativeCharacter::GetCoverDecisionConfig() const
{
	FCoverDecisionConfig Config;
	Config.AimNoTargetGraceSeconds = CornerAimNoTargetGraceSeconds;
	Config.AimReentryDelaySeconds = CornerAimReentryDelaySeconds;
	return Config;
}

void AOperativeCharacter::BeginCornerAim()
{
	if (!bInCover || CurrentCoverHeight == ECoverHeight::None || bShimmying)
	{
		return; // the high wall's corner, or over the top of a low cover (user request 2026-10-07: crouched too)
	}
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	CornerAimLastTargetTime = Now;
	if (!bCornerAimActive)
	{
		bCornerAimActive = true;
		CornerAimEvalTimer = 0.f;
		UE_LOG(LogCodexTactics, Display, TEXT("[Cover] %s: leans out round the corner and holds the fire stance"), *DisplayName.ToString());
	}
}

void AOperativeCharacter::EndCornerAim(ECornerAimDecision Reason, const TCHAR* Why)
{
	if (!bCornerAimActive)
	{
		return;
	}
	bCornerAimActive = false;
	bIsCornerLeaning = false; // back behind the corner
	LeanTimer = 0.f;
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	CornerAimEndTime = Now;
	if (Reason == ECornerAimDecision::DuckForSafety)
	{
		CornerAimDuckUntil = Now + GetCoverDecisionConfig().AimReentryDelaySeconds;
	}
	LastCornerAimBreak = Reason;
	++CornerAimBreakCounts[static_cast<int32>(Reason)];
	UE_LOG(LogCodexTactics, Display, TEXT("[Cover] %s: breaks the corner aim: %s (%s)"), *DisplayName.ToString(),
		CoverDecisionRules::CornerAimDecisionName(Reason), Why);
	if (Reason == ECornerAimDecision::DuckToReload && !bIsReloading && UsesAmmo() && ReserveAmmo > 0
		&& CurrentClip < (CurrentWeapon ? CurrentWeapon->MaxClipSize : 30))
	{
		StartReload(); // reload behind the corner (the AnimInstance plays the fire -> idle exit, then the reload)
	}
}

FCornerAimSituation AOperativeCharacter::GatherCornerAimSituation() const
{
	const FCoverDecisionConfig Config = GetCoverDecisionConfig();
	FCornerAimSituation Situation;
	UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	const int32 MaxClip = FMath::Max(1, CurrentWeapon ? CurrentWeapon->MaxClipSize : 30);
	Situation.ClipFraction = UsesAmmo() ? static_cast<float>(CurrentClip) / MaxClip : 1.f;
	Situation.bHasReserve = !UsesAmmo() || ReserveAmmo > 0;
	Situation.SecondsWithoutTarget = static_cast<float>(FMath::Max(0.0, Now - CornerAimLastTargetTime));
	const UTurnBasedCombatSubsystem* TurnBased = World ? World->GetSubsystem<UTurnBasedCombatSubsystem>() : nullptr;
	Situation.bHoldWithoutTargets = TurnBased && TurnBased->IsActive(); // in his turn the pose stays between his shots
	Situation.HealthFraction = HealthComponent ? HealthComponent->GetHealthFraction() : 1.f;
	Situation.RecentIncomingDamage = RecentRangedDamage;
	Situation.bRangedThreatPresent = false;
	CornerAimTrace = FCornerAimTrace();
	CornerAimTrace.AllDamage = RecentIncomingDamage;
	if (!World)
	{
		return Situation;
	}
	const FVector Here = GetActorLocation();
	int32 Shooters = 0;
	for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
	{
		const AEnemyCharacter* Enemy = *It;
		if (Enemy->IsDying() || !Enemy->GetHealthComponent() || !Enemy->GetHealthComponent()->IsAlive())
		{
			continue;
		}
		const bool bRanged = IsRangedEnemyActor(Enemy);
		const float Distance = static_cast<float>(FVector::Dist2D(Here, Enemy->GetActorLocation()));
		const bool bOpenSide = !IsCornerShotTarget(Enemy->GetActorLocation());
		if (Enemy->GetCurrentTarget() == this && Distance <= 3000.f)
		{
			++CornerAimTrace.AllAimers; // the old rule counted every enemy targeting him, melee rushers included
			Shooters += bRanged ? 1 : 0;
		}
		if (bRanged && !Enemy->IsHidden() && Distance <= 3000.f)
		{
			Situation.bRangedThreatPresent = true;
		}
		if (!bRanged && Distance <= 500.f)
		{
			++Situation.MeleeRushers;
		}
		if (const AMarksmanEnemyCharacter* Marksman = Cast<AMarksmanEnemyCharacter>(Enemy);
			Marksman && Marksman->IsAimingAtTarget() && Marksman->GetCurrentTarget() == this)
		{
			Situation.bSniperLaserOnMe = true;
		}
		// An enemy on his open side (in front of the wall) close by: the wall does not shield him from it. Only a RANGED
		// one is a reason to duck (2026-10-07 horde fix: melee rushers coming round the corner are the targets).
		if (!Enemy->IsHidden() && Distance <= Config.AimFlankDangerCm && bOpenSide)
		{
			CornerAimTrace.bAnyOpenSideNear = true;
			CornerAimTrace.NearestOpenSideCm = FMath::Min(CornerAimTrace.NearestOpenSideCm, Distance);
			Situation.bFlankEnemyNear |= bRanged;
		}
	}
	Situation.RangedAimers = Shooters;
	Situation.SuppressionPressure = ForcedCornerAimSuppressionForTesting >= 0.f ? ForcedCornerAimSuppressionForTesting
		: CoverDecisionRules::SuppressionFromShooters(Config, Shooters);
	CornerAimTrace.AllPressure = CoverDecisionRules::SuppressionFromShooters(Config, CornerAimTrace.AllAimers);
	for (TActorIterator<AGrenadeActor> It(World); It; ++It)
	{
		// 2026-10-07: only an ENEMY grenade is a reason to duck. The squad's own grenades (all AGrenadeActor today: only
		// operatives throw) sit in his hand before the release (distance 0) or fly at the horde — they made him duck at
		// once. A friendly grenade that has LANDED next to him counts only with bCornerAimDuckForFriendlyGrenades.
		const bool bFriendly = It->GetThrower() != nullptr;
		if (bFriendly && !(bCornerAimDuckForFriendlyGrenades && It->HasLanded()))
		{
			continue;
		}
		const float GrenadeDistance = static_cast<float>(FVector::Dist2D(Here, It->GetActorLocation()));
		CornerAimTrace.NearestGrenadeCm = FMath::Min(CornerAimTrace.NearestGrenadeCm, GrenadeDistance);
		if (GrenadeDistance <= Config.AimGrenadeDangerCm)
		{
			Situation.bGrenadeNearby = true;
		}
	}
	return Situation;
}

void AOperativeCharacter::UpdateCornerAim(float DeltaTime)
{
	if (!bCornerAimActive)
	{
		return;
	}
	if (!bInCover || bShimmying || CurrentCoverHeight == ECoverHeight::None)
	{
		EndCornerAim(ECornerAimDecision::ReturnNoTargets, TEXT("no longer at the corner"));
		return;
	}
	if (bIsReloading)
	{
		EndCornerAim(ECornerAimDecision::DuckToReload, TEXT("reloading"));
		return;
	}
	if (bCornerHoldAtEdge && CurrentCoverHeight == ECoverHeight::HighCover && !IsCornerHoldSpot())
	{
		EndCornerAim(ECornerAimDecision::ReturnNoTargets, TEXT("no longer at the exposed edge"));
		return;
	}
	// Leaned out (sight: exposed) for as long as the aim lasts.
	bIsCornerLeaning = true;
	LeanTimer = FMath::Max(LeanTimer, 0.2f);
	CornerAimEvalTimer -= DeltaTime;
	if (CornerAimEvalTimer > 0.f)
	{
		return;
	}
	CornerAimEvalTimer = 0.1f;
	const FCoverDecisionConfig Config = GetCoverDecisionConfig();
	const FCornerAimSituation Situation = GatherCornerAimSituation();
	const TCHAR* Reason = TEXT("");
	const ECornerAimDecision Decision = CoverDecisionRules::DecideCornerAim(Config, Situation, &Reason);
	// What the pre-fix rule (melee counted as suppression / flank, every hit counted) would have decided â€” the trace shows
	// the horde regression's cause next to the fixed decision.
	FCornerAimSituation Legacy = Situation;
	Legacy.SuppressionPressure = CornerAimTrace.AllPressure;
	Legacy.bFlankEnemyNear = CornerAimTrace.bAnyOpenSideNear;
	Legacy.RecentIncomingDamage = CornerAimTrace.AllDamage;
	Legacy.bRangedThreatPresent = true;
	const TCHAR* LegacyReason = TEXT("");
	const ECornerAimDecision LegacyDecision = CoverDecisionRules::DecideCornerAim(Config, Legacy, &LegacyReason);
	CornerAimLabel = FString::Printf(TEXT("%s: %s | legacy %s: %s | hp %.0f%% rngDmg %.0f allDmg %.0f | press %.2f (rng aimers %d, all %d) | melee<5m %d | laser %d | grenade %.0f | openSide %.0f | noTarget %.1fs | clip %.0f%%"),
		CoverDecisionRules::CornerAimDecisionName(Decision), Reason, CoverDecisionRules::CornerAimDecisionName(LegacyDecision), LegacyReason,
		Situation.HealthFraction * 100.f, Situation.RecentIncomingDamage, CornerAimTrace.AllDamage, Situation.SuppressionPressure,
		Situation.RangedAimers, CornerAimTrace.AllAimers, Situation.MeleeRushers, Situation.bSniperLaserOnMe ? 1 : 0,
		CornerAimTrace.NearestGrenadeCm < 1.0e8f ? CornerAimTrace.NearestGrenadeCm : -1.f,
		CornerAimTrace.NearestOpenSideCm < 1.0e8f ? CornerAimTrace.NearestOpenSideCm : -1.f, Situation.SecondsWithoutTarget,
		Situation.ClipFraction * 100.f);
	if (Decision != LastCornerAimTraceDecision || LegacyDecision != LastCornerAimLegacyDecision || FCString::Strcmp(Reason, *LastCornerAimTraceReason) != 0)
	{
		LastCornerAimTraceDecision = Decision;
		LastCornerAimLegacyDecision = LegacyDecision;
		LastCornerAimTraceReason = Reason;
		UE_LOG(LogCodexTactics, Display, TEXT("[CornerAim] %s: %s"), *DisplayName.ToString(), *CornerAimLabel);
	}
	if (Decision != ECornerAimDecision::StayAndFire && bCornerAutoDuck)
	{
		EndCornerAim(Decision, Reason);
	}
}

void AOperativeCharacter::TrySnapToCoverCorner()
{
	bCoverSnapPending = false;
	if (!bInCover || bShimmying || IsRaging() || IsPanicking())
	{
		return;
	}
	const UTurnBasedCombatSubsystem* TurnBased = GetWorld() ? GetWorld()->GetSubsystem<UTurnBasedCombatSubsystem>() : nullptr;
	if (TurnBased && TurnBased->IsActive())
	{
		return; // grid positions stay on their cells
	}
	if (CurrentCoverHeight == ECoverHeight::LowCover)
	{
		return; // a low cover is fired over the top anywhere: no walk to its end
	}
	float Shift = 0.f;
	if (!CoverFacingRules::ShouldSnapToCorner(CoverSlot, CoverFacing, Shift, GetCoverFacingConfig(), Stance == EOperativeStance::Crouching))
	{
		return;
	}
	const FVector Point = CoverSlot.WallPoint + CoverFacingRules::AlongWallDirection(CoverSlot, CoverFacing) * Shift;
	FCoverSlot Corner;
	if (CoverTraceRules::FindShimmySlot(GetWorld(), CoverSlot, Point, Corner) && OrderShimmyTo(Corner) == EOperativeOrderResult::Accepted && bShimmying)
	{
		bCoverAutoSnap = true;
		UE_LOG(LogCodexTactics, Display, TEXT("[Cover] %s: to the %s corner (%.0f cm)"), *DisplayName.ToString(),
			CoverFacing == ECoverFacing::Left ? TEXT("left") : TEXT("right"), Shift);
	}
}

FCoverFacingConfig AOperativeCharacter::GetCoverFacingConfig() const
{
	FCoverFacingConfig Config;
	Config.StandStandOffPackLCm = CoverStandOffStandPackLCm;
	Config.StandStandOffPackRCm = CoverStandOffStandPackRCm;
	Config.CrouchStandOffPackLCm = CoverStandOffCrouchPackLCm;
	Config.CrouchStandOffPackRCm = CoverStandOffCrouchPackRCm;
	return Config;
}

bool AOperativeCharacter::IsCornerShotTarget(const FVector& TargetLocation) const
{
	if (!bInCover)
	{
		return false;
	}
	// Corner hold (user decision 2026-10-07): from the edge he fires at what the corner stance can aim at - behind the wall
	// or past the edge, within the twist limit + the aim cone. The sticky "everything beyond the edge, at any depth" rule
	// of the horde fix is gone: in CoverBug_02 (58 s) it kept a flank rush in front of the wall as corner targets, so he
	// shot hounds at his side from the corner pose without turning to them.
	if (IsCornerHoldSpot())
	{
		return CoverFacingRules::IsCornerHoldTarget(CoverSlot, CoverFacing, GetCoverFireOrigin(), TargetLocation, CornerAimOutwardDeg,
			GetCornerHoldReachDeg());
	}
	return CoverFacingRules::ShouldCornerShot(CoverSlot, TargetLocation, GetCoverFacingConfig());
}

bool AOperativeCharacter::IsCornerHoldSpot() const
{
	// Not while the walk to the corner stand-off is still due (bCoverSnapPending: set on entry, decided once the move order
	// ended) - CornerEntrySmoke 2026-10-07: entering 90 cm from the edge he leaned out and broke the aim for the snap shimmy
	// in the same instant; with a later snap that is step out -> tuck back -> step out.
	return bCornerHoldAtEdge && bInCover && !bShimmying && !bCoverSnapPending && CurrentCoverHeight == ECoverHeight::HighCover
		&& bAtCoverCorner && CoverSlot.IsEdgeExposed(CoverFacing);
}

FVector AOperativeCharacter::GetStanceAimOrigin() const
{
	return bInCover && bCornerAimActive && CurrentCoverHeight == ECoverHeight::HighCover ? GetCoverFireOrigin() : GetMuzzleLocation();
}

FVector AOperativeCharacter::GetStanceAimBaseDirection() const
{
	if (bInCover && CurrentCoverHeight == ECoverHeight::HighCover && (bCornerAimActive || IsCornerHoldSpot()))
	{
		return CoverFacingRules::CornerAimDirection(CoverSlot, CoverFacing, CornerAimOutwardDeg);
	}
	// Out of cover (and any other pose): the pose's barrel yaw (the rifle is held across the chest). BarrelYawOffset is
	// measured on the weapon with the 2D aim offset's twist taken out (UpdateCombatFacing): a stable reference, the AO never
	// feeds back into its own input (AnimOffset_Bug_01, 2026-10-08).
	return FRotator(0.f, GetActorRotation().Yaw + BarrelYawOffset, 0.f).Vector();
}

float AOperativeCharacter::GetAppliedAimYaw() const
{
	const UOperativeAnimInstance* Anim = GetMesh() ? Cast<UOperativeAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
	return Anim && Anim->bAimOffsetYaw ? Anim->AimYaw * Anim->AimOffsetAlpha : 0.f;
}

float AOperativeCharacter::GetCornerHoldReachDeg() const
{
	// 5 deg inside the shot cone: a target the corner stance claims is one its barrel can be brought onto (the clip's
	// barrel varies a few degrees around CornerAimOutwardDeg) - no stand-off where he neither fires nor steps out.
	return FMath::Max(5.f, GetAimTwistLimitDeg() + AimConeDeg - 5.f);
}

float AOperativeCharacter::GetAimTwistLimitDeg() const
{
	const UOperativeAnimInstance* Anim = GetMesh() ? Cast<UOperativeAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
	return Anim && Anim->bAimOffsetYaw && Anim->bAimOffset && !Anim->bAimOffsetTurnBasedOff ? Anim->AimYawClampDegrees : 0.f;
}

float AOperativeCharacter::GetShotAimResidualDeg(const FVector& TargetLocation) const
{
	// The real barrel when the rifle is out and level (the pose as animated, the 2D aim offset's twist included): the shot
	// goes where the rifle points (user rule 2026-10-07). Else the stance rule (the pose's aim direction + the twist).
	if (bCoverEntryAwaitClip || CoverEntryYawTime >= 0.f)
	{
		return 180.f; // the body is still turning after an instant actor turn (the mesh holds the old yaw): not aimed yet
	}
	if (IsSniperWeaponEquipped() && !bInCover)
	{
		// The sniper clips aim along the body's forward (the M16 stand-in model is held at the pack rifle's angle only
		// roughly): the body's facing decides, not the stand-in's barrel.
		return FMath::Abs(AimOffsetRules::YawToTarget(GetMuzzleLocation(), GetActorForwardVector(), TargetLocation, 180.f));
	}
	const USkeletalMeshComponent* Body = GetMesh();
	const bool bPoseLive = Body && (Body->VisibilityBasedAnimTickOption == EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones
		|| Body->WasRecentlyRendered(0.25f)); // headless (not rendered) the bones keep a stale pose
	if (bPoseLive && WeaponMesh && WeaponMesh->GetStaticMesh() && WeaponMesh->IsVisible() && UsesAmmo())
	{
		const FVector Barrel = WeaponMesh->GetComponentTransform().TransformVectorNoScale(MuzzleOffset.GetSafeNormal());
		if (Barrel.Size2D() < 0.3f)
		{
			return 180.f; // the rifle points up / down (a clip in between): not aimed at anything
		}
		return FMath::Abs(AimOffsetRules::YawToTarget(GetWeaponMuzzleLocation(), Barrel, TargetLocation, 180.f));
	}
	const float Error = AimOffsetRules::YawToTarget(GetStanceAimOrigin(), GetStanceAimBaseDirection(), TargetLocation, 180.f);
	return AimOffsetRules::ResidualAimError(Error, GetAppliedAimYaw());
}

void AOperativeCharacter::FaceAimAt(const FVector& TargetLocation)
{
	const FVector Delta = TargetLocation - GetActorLocation();
	ClearIdleFacing(); // an aimed facing is not undone by the parked-follower facing
	if (!Delta.IsNearlyZero())
	{
		SetActorRotation(FRotator(0.f, Delta.Rotation().Yaw - BarrelYawOffset, 0.f));
	}
}

bool AOperativeCharacter::BeginOpenShotFromCover(const AActor* Target)
{
	if (!bInCover || bShimmying)
	{
		return false;
	}
	const FCoverSlot Slot = CoverSlot;
	bOpenShotReturnToCornerHold = IsCornerHoldSpot();
	OpenShotReturnFacing = CoverFacing;
	UE_LOG(LogCodexTactics, Display, TEXT("[Cover] %s: steps off the wall for an open shot at %s (%s)"), *DisplayName.ToString(),
		Target ? *Target->GetName() : TEXT("-"), bOpenShotReturnToCornerHold ? TEXT("outside the corner stance's reach") : TEXT("in front of the cover"));
	LeaveCover(TEXT("open shot: target in front of the cover"));
	OpenShotReturnSlot = Slot;
	bOpenShotReturnPending = bReturnToCoverAfterOpenShot;
	OpenShotLastTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	OpenShotStartTime = OpenShotLastTime;
	return true;
}

void AOperativeCharacter::UpdateOpenShotReturn()
{
	if (!bOpenShotReturnPending)
	{
		return;
	}
	const UWorld* World = GetWorld();
	const bool bAlive = HealthComponent && HealthComponent->IsAlive();
	// Ordered elsewhere (a move / cover order), back in some cover, down, or moved away: no return.
	if (bInCover || !bAlive || bHasMoveOrder || bHasPendingCover || !World
		|| FVector::Dist2D(GetActorLocation(), OpenShotReturnSlot.WorldLocation) > 150.f)
	{
		bOpenShotReturnPending = false;
		return;
	}
	if (IsRaging() || IsPanicking())
	{
		OpenShotLastTime = World->GetTimeSeconds(); // raging / panicking: the return waits until it passes (it used to be dropped)
		return;
	}
	// Enemies still in sight on the open side (a stream coming at him): he stays out, the calm only starts once none is
	// in sight. Back at a corner hold the stance covers what is round the corner: those do not keep him out.
	const float Reach = GetCornerHoldReachDeg();
	const FVector CornerOrigin = CornerFireOriginFor(OpenShotReturnSlot, OpenShotReturnFacing,
		FVector(OpenShotReturnSlot.WorldLocation.X, OpenShotReturnSlot.WorldLocation.Y, GetMuzzleLocation().Z));
	for (TActorIterator<AEnemyCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsDying() || !It->GetHealthComponent() || !It->GetHealthComponent()->IsAlive() || It->CustomTimeDilation <= 0.f)
		{
			continue;
		}
		const float Distance = static_cast<float>(FVector::Dist2D(It->GetActorLocation(), GetActorLocation()));
		const bool bCoveredByCorner = bOpenShotReturnToCornerHold && CoverFacingRules::IsCornerHoldTarget(OpenShotReturnSlot,
			OpenShotReturnFacing, CornerOrigin, It->GetActorLocation(), CornerAimOutwardDeg, Reach);
		// A rusher close by keeps him facing it; one on the open side keeps him out while in sight within 30 m, or - out of
		// sight for a moment (a pack falling back to flank) - within twice that rush distance (CornerHoldSmoke 2026-10-07:
		// he stepped back to the wall while two flankers circled out of sight, then out again).
		const bool bKeepsOut = Distance <= FlankRushBreakCm
			|| (!bCoveredByCorner && (Distance <= 2.f * FlankRushBreakCm || (!It->IsHidden() && Distance <= 3000.f)));
		if (bKeepsOut)
		{
			OpenShotLastTime = World->GetTimeSeconds();
			break;
		}
	}
	if (World->GetTimeSeconds() - OpenShotLastTime < OpenShotReturnDelaySeconds || World->GetTimeSeconds() - OpenShotStartTime < OpenShotCommitSeconds
		|| GetVelocity().SizeSquared2D() > 4.f || bIsReloading)
	{
		return; // still firing / enemies in sight / settling
	}
	bOpenShotReturnPending = false;
	UE_LOG(LogCodexTactics, Display, TEXT("[Cover] %s: back to the wall after the open shot"), *DisplayName.ToString());
	TGuardValue<bool> Quiet(bQuietCoverEntryRequest, true);
	EnterCover(OpenShotReturnSlot);
}

void AOperativeCharacter::PlayCoverShot(AActor* Target, bool bHit)
{
	if (!bInCover)
	{
		return;
	}
	CoverShotTarget = Target;
	UpdateCoverFacing(/*bAllowSnap*/ false);
	if (const UWorld* World = GetWorld())
	{
		bHasCoverThreat = true; // a shot is a threat contact (keeps / enters the fire-ready pose)
		CoverThreatSeenTime = World->GetTimeSeconds();
	}
	SetActorRotation(FRotator(0.f, GetCoverFacingYaw(), 0.f)); // the grid shot resolves at once
	BeginCoverShot();
	(bIsBlindFiring ? CoverBlindShots : CoverLeanShots) += 1;
	OnWeaponFiredNative.Broadcast(this, Target, bHit); // the AnimInstance plays the cover fire clip
}

void AOperativeCharacter::StartBodyYawBlend(float OldYaw)
{
	// The actor turned at once (a turn-based shot's FaceAimAt): the body keeps OldYaw and turns over CoverEntryYawBlendSeconds.
	if (bInCover || !GetMesh())
	{
		return;
	}
	CoverEntryYawOffset = FRotator::NormalizeAxis(OldYaw - GetActorRotation().Yaw + CoverEntryAppliedYaw);
	CoverEntryYawTime = FMath::Abs(CoverEntryYawOffset) > 1.f ? 0.f : -1.f;
	bBodyYawFree = CoverEntryYawTime >= 0.f;
}
