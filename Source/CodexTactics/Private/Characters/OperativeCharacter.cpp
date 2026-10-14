#include "Characters/OperativeCharacter.h"
#include "Characters/OperativeCharacterInternal.h"
#include "Vehicles/VehicleSubsystem.h"
#include "Characters/OperativeHitRules.h"
#include "Combat/KnockdownComponent.h"
#include "Combat/KnockdownRules.h"
#include "Telemetry/RunTelemetrySubsystem.h"
#include "Interactables/RadiusRingSubsystem.h"
#include "Subsystems/CodexEventBus.h"
#include "Core/PlayerCommands.h"
#include "UI/FloatingTextSubsystem.h"
#include "Characters/OperativeAIController.h"
#include "Characters/PanicComponent.h"
#include "Characters/RageComponent.h"
#include "Characters/VaultRules.h"
#include "Interactables/VaultNavigation.h"
#include "Level/CrawlGapActor.h"
#include "Interactables/TripwireActor.h"
#include "Interactables/TripwireRules.h"
#include "Combat/GrenadeSubsystem.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/CombatFeedbackSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
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

#include "Combat/EnemyGhostActor.h"
#include "Combat/SightRules.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Combat/VisionConeComponent.h"
#include "Combat/VisionConeRules.h"
#include "Data/WeaponDataAsset.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Characters/FacingRules.h"
#include "Characters/OperativeAnimInstance.h"
#include "Animation/AnimSequence.h"
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
#include "NavigationSystem.h"
#include "Survival/ColdSurvivalComponent.h"
#include "Data/TacticalHeatTuning.h"
#include "Survival/TacticalHeatSubsystem.h"
#include "Tactics/CoverRules.h"
#include "Tactics/CoverTraceRules.h"
#include "Tactics/CoverDecisionRules.h"
#include "Tactics/CoverFacingRules.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "UI/GameMessageSubsystem.h"
#include "UObject/ConstructorHelpers.h"

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

int32 AOperativeCharacter::GetItemCount(EPersonalItem Item) const
{
	switch (Item)
	{
	case EPersonalItem::Medkit: return MedkitsCount;
	case EPersonalItem::CannedFood: return CannedFoodCount;
	case EPersonalItem::Bread: return BreadCount;
	case EPersonalItem::HeatPack: return HeatPacksCount;
	default: return ChocolateCount;
	}
}

bool AOperativeCharacter::UsePersonalItem(EPersonalItem Item)
{
	if (Item == EPersonalItem::HeatPack)
	{
		// Sprint 15: on himself through the heat subsystem (turn-based AP, feed line, buffer).
		UTacticalHeatSubsystem* Heat = GetWorld() ? GetWorld()->GetSubsystem<UTacticalHeatSubsystem>() : nullptr;
		return Heat && Heat->UseHeatPack(this, this);
	}
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
	case EDeployableType::Mine: return MinesCount;
	case EDeployableType::PortableStove: return PortableStovesCount;
	default: return TacticalHeatRules::GetCraftableCampfires(TacticalHeatTuning::Get(), MatchesCount, FirewoodCount, GetReserve(TEXT("flamethrower")));
	}
}

void AOperativeCharacter::AddDeployable(EDeployableType Type, int32 Delta)
{
	if (Type == EDeployableType::Campfire)
	{
		// Sprint 15: a campfire is its materials.
		const FTacticalHeatConfig& Heat = TacticalHeatTuning::Get();
		if (Delta > 0)
		{
			MatchesCount += Delta * Heat.CampfireMatchesCost;
			FirewoodCount += Delta * FMath::Max(1, Heat.CampfireFirewoodCost);
		}
		for (int32 Index = 0; Index < -Delta; ++Index)
		{
			int32 Fuel = GetReserve(TEXT("flamethrower"));
			const int32 FuelBefore = Fuel;
			if (!TacticalHeatRules::ConsumeCampfireMaterials(Heat, MatchesCount, FirewoodCount, Fuel))
			{
				break;
			}
			if (Fuel < FuelBefore)
			{
				TakeReserve(TEXT("flamethrower"), FuelBefore - Fuel);
			}
		}
		return;
	}
	int32& Count = Type == EDeployableType::Turret ? TurretsCount : (Type == EDeployableType::Barricade ? BarricadesCount
		: (Type == EDeployableType::PortableStove ? PortableStovesCount : MinesCount));
	Count = FMath::Clamp(Count + Delta, 0, DeployableRules::GetMaxCarried(Type));
	// A stove's gas (StoveGasSeconds) moves with it: APortableStoveActor::OnDeployedBy takes it, MoveDeployableTo hands it over.
}

bool AOperativeCharacter::MoveDeployableTo(AOperativeCharacter& To, EDeployableType Type)
{
	if (&To == this || GetDeployableCount(Type) <= 0 || To.GetDeployableCount(Type) >= DeployableRules::GetMaxCarried(Type))
	{
		return false;
	}
	const float Gas = StoveGasSeconds;
	AddDeployable(Type, -1);
	To.AddDeployable(Type, 1);
	if (Type == EDeployableType::PortableStove)
	{
		To.StoveGasSeconds = Gas; // max one stove each: the recipient had none
		StoveGasSeconds = GetDeployableCount(Type) > 0 ? StoveGasSeconds : 0.f;
	}
	return true;
}

EDeployableType AOperativeCharacter::CycleDeployableType()
{
	for (int32 Step = 1; Step <= DeployableTypeCount; ++Step)
	{
		const EDeployableType Next = static_cast<EDeployableType>((static_cast<int32>(SelectedDeployType) + Step) % DeployableTypeCount);
		if (GetDeployableCount(Next) > 0 || Step == DeployableTypeCount)
		{
			SelectedDeployType = Next;
			break;
		}
	}
	return SelectedDeployType;
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
	}
	UpdateCoverEntryBlend(DeltaTime);
	UpdateSilhouette(DeltaTime);
	UpdateSelectionRing();
	UpdateVisionCone(DeltaTime);
}

float AOperativeCharacter::GetVisionConeRange() const
{
	if (!bVisionConeRangeFromWeapon || !CurrentWeapon)
	{
		return VisionConeRangeCm;
	}
	// What FindBestCombatTarget can reach on flat ground (Godot: the cone range was the M16 range, 14 m).
	return CurrentWeapon->AttackRangeCm * SquadFireRules::GetPostureRangeMultiplier(Stance);
}

bool AOperativeCharacter::IsEnemySpottedInCone() const
{
	if (GetCurrentCombatTarget() || GetManualPriorityTarget())
	{
		return true;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	const float Range = GetVisionConeRange();
	const FVector Origin = GetActorLocation();
	const FVector Forward = GetActorForwardVector();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VisionConeSpot), false, this);
	if (const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>())
	{
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Params.AddIgnoredActor(Member);
		}
	}
	for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
	{
		const AEnemyCharacter* Enemy = *It;
		if (Enemy->IsDying() || !Enemy->GetHealthComponent() || !Enemy->GetHealthComponent()->IsAlive()
			|| !VisionConeRules::IsInsideCone(Origin, Forward, Enemy->GetActorLocation(), Range, VisionConeHalfAngleDeg))
		{
			continue;
		}
		// Godot: a ray from the eyes to the enemy's body; a barricade still counts as seen unless prone.
		FHitResult Hit;
		const FVector Eye = UTacticalSightSubsystem::EyePoint(*this);
		const FVector Body = Enemy->GetActorLocation() + FVector(0.f, 0.f, 60.f);
		if (!World->LineTraceSingleByChannel(Hit, Eye, Body, ECC_Visibility, Params) || Hit.GetActor() == Enemy
			|| Cast<APawn>(Hit.GetActor()) || (Hit.GetActor() && Hit.GetActor()->IsA<ABarricadeActor>() && Stance != EOperativeStance::Prone))
		{
			return true;
		}
	}
	return false;
}

void AOperativeCharacter::UpdateVisionCone(float DeltaTime)
{
	if (!VisionCone)
	{
		return;
	}
	const bool bAlive = HealthComponent && HealthComponent->IsAlive();
	const bool bVisible = VisionConeRules::ResolveVisible(UVisionConeComponent::GetGlobalMode(), bShowVisionCone, bAlive);
	if (bVisible)
	{
		VisionConeCheckTimer -= DeltaTime;
		if (VisionConeCheckTimer <= 0.f)
		{
			VisionConeCheckTimer = 0.2f;
			bVisionConeSpotted = IsEnemySpottedInCone();
		}
	}
	const FLinearColor Color = VisionConeRules::GetStateColor(bVisionConeSpotted ? EVisionConeState::Spotted : EVisionConeState::Clear);
	VisionCone->UpdateCone(bVisible, GetVisionConeRange(), VisionConeHalfAngleDeg, Color, DeltaTime);
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

float AOperativeCharacter::TakeHit(float Amount, const FString& Attacker, bool bCrit, bool bBypassAvoidance, AActor* AttackerActor,
	float CritMultiplierApplied)
{
	return ResolveIncomingHit(bBypassAvoidance ? EOperativeHitKind::Unavoidable : EOperativeHitKind::Attack, Amount, Attacker, bCrit,
		AttackerActor, CritMultiplierApplied);
}

float AOperativeCharacter::TakeGridBlast(float Amount, const FString& Source)
{
	return ResolveIncomingHit(EOperativeHitKind::GridBlast, Amount, Source, false, nullptr, 1.f);
}

float AOperativeCharacter::ResolveIncomingHit(EOperativeHitKind Kind, float Amount, const FString& Attacker, bool bCrit, AActor* AttackerActor,
	float CritMultiplierApplied)
{
	if (!HealthComponent || !HealthComponent->IsAlive() || VehicleQueries::IsMounted(this)) // APC P1: the crew inside takes no hits
	{
		return 0.f;
	}
	LastAttackedTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0; // a threat: the weather idle poses end (UWarmIdleSubsystem)
	// Fire posture: an attack (dodged or not) provokes a Defensive operative to fight back (user request 2026-10-06).
	bProvokedThisFight = true;
	if (USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr)
	{
		Squad->NotifyMemberAttacked(this);
	}
	// 1. Dodge on luck (Godot: luck 25 -> 10 %).
	const float DodgeRoll = ForcedDodgeRollForTesting >= 0.f ? (ForcedDodgeRollForTesting >= 1.f ? -1.f : 101.f) : FMath::FRand() * 100.f;
	ForcedDodgeRollForTesting = -1.f;
	if (OperativeHitRules::IsDodged(Kind, DodgeRoll, Luck))
	{
		UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("💨 DODGE!"), FLinearColor(0.3f, 0.9f, 1.f));
		return 0.f;
	}
	// 2. Stance defense and fortitude cut (Godot: 15 fortitude = 22.5 %, at most 50 %), each once per kind (OperativeHitRules).
	const float Fortitude = ColdSurvival ? ColdSurvival->Fortitude : 15.f;
	// Sprint 12 cover (user decision 2026-10-06): a hit from the wall's frontal arc is absorbed 90 % by high cover (35 %
	// crouched / 90 % prone behind low cover), flanking hits pass fully; while the head stays down no hit is a headshot
	// (crit). Blasts / traps (bBypassAvoidance) are not stopped by a wall at the back.
	float CoverAbsorb = 0.f;
	if (bInCover && OperativeHitRules::UsesCover(Kind) && AttackerActor)
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
	// Lying after a knockdown: any blow (shot, bite, blast, trap; real time and the turn-based grid hits, which come in
	// with bBypassAvoidance) lands x1.33 (user decision 2026-10-11, KnockdownRules::DownedDamageMultiplier). The
	// bypass path keeps whole numbers (grid damage is integer).
	const float KnockdownScale = KnockdownComponent ? KnockdownComponent->GetDamageMultiplier() : 1.f;
	const float Final = OperativeHitRules::FinalDamage(Kind, CoverRules::ApplyAbsorb(Amount, CoverAbsorb), HealthComponent->GetDefenseMultiplier(),
		Fortitude, KnockdownScale);
	RecentIncomingDamage += Final;
	bHasLastHitSource = true; // before the health loss: the hit reaction / death fall read it
	LastHitSource = AttackerActor ? AttackerActor->GetActorLocation() : GetActorLocation() + GetActorForwardVector() * 100.f;
	if (OperativeCharacterInternal::IsRangedEnemyActor(AttackerActor))
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
	if (KnockdownComponent && HealthComponent->IsAlive() && OperativeHitRules::AllowsHeavyHitKnockdown(Kind))
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

