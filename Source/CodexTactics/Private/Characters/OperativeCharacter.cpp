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
	const FVector MyFeet = OperativeCharacterInternal::ShotFeet(this);
	const FVector EnemyFeet = OperativeCharacterInternal::ShotFeet(Enemy);
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
	const FVector MyFeet = OperativeCharacterInternal::ShotFeet(this);
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
		if (!OperativeCharacterInternal::IsLiveEnemy(Priority))
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
		if (OperativeCharacterInternal::IsLiveEnemy(*It) && EvaluateShotLine(*It, false, Candidate, bBlocked) && Candidate.Distance < BestDistance)
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
	return OperativeCharacterInternal::IsLiveEnemy(Enemy) && EvaluateShotLine(Enemy, true, Candidate, bBlocked);
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
		if (!OperativeCharacterInternal::IsLiveEnemy(Priority))
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
			if (Source && OperativeCharacterInternal::IsLiveEnemy(Source))
			{
				ManualPriorityTarget = Source;
			}
			BlindFireSource.Reset();
		}
		else if (!Source || !OperativeCharacterInternal::IsLiveEnemy(Source))
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
		if (!OperativeCharacterInternal::IsLiveEnemy(Chosen))
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
		if (!OperativeCharacterInternal::IsLiveEnemy(*It))
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
	const FShootCandidate* Current = OperativeCharacterInternal::IsLiveEnemy(CurrentCombatTarget.Get())
		? Visible.FindByPredicate([this](const FShootCandidate& Entry) { return Entry.Enemy == CurrentCombatTarget.Get(); }) : nullptr;
	// A melee enemy closing in on him at a cover (a flank rush while he holds the corner, user PIE video CoverBug_02
	// 2026-10-07) takes over at once. Out in the open the Godot stance reaction delay below stays (parity: SquadFireSmoke
	// "flank hound taken after 0.15 s, not at once").
	if ((bInCover || bOpenShotReturnPending) && Current && Closest.Enemy != Current->Enemy && Closest.Distance <= FlankRushBreakCm
		&& !OperativeCharacterInternal::IsRangedEnemyActor(Closest.Enemy))
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
	if (bBlindShot && FVector::Dist2D(OperativeCharacterInternal::ShotFeet(Target), BlindAimPoint) > SightRules::BlindFireHitRadiusCm)
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

bool AOperativeCharacter::DetonateShotTarget(AActor* Target, ETargetedShotKind Kind, const FText& ShooterName)
{
	if (!IsValid(Target))
	{
		return false;
	}
	switch (Kind)
	{
	case ETargetedShotKind::Barrel:
		return Cast<ABarrelActor>(Target) && Cast<ABarrelActor>(Target)->Explode(ShooterName);
	case ETargetedShotKind::Mine:
		if (AProximityMineActor* Mine = Cast<AProximityMineActor>(Target))
		{
			Mine->Detonate();
			return true;
		}
		return false;
	case ETargetedShotKind::Crate:
	case ETargetedShotKind::TrappedObject:
		if (AInteractableActor* Object = Cast<AInteractableActor>(Target); Object && Object->bTrapped)
		{
			Object->DetonateTrap(true, ShooterName);
			return true;
		}
		return false;
	default:
		return false;
	}
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
	if (Combat && OperativeCharacterInternal::IsLiveEnemy(Combat))
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
	if (bRealTimeFight && !bSprinting && !bCarrying && !IsPanicking() && Target && OperativeCharacterInternal::IsLiveEnemy(Target) && HealthComponent && HealthComponent->IsAlive())
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
		DetonateShotTarget(Target, Kind, DisplayName);
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
			DetonateShotTarget(Target, Kind, DisplayName);
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
			DetonateShotTarget(Crate, Kind, DisplayName);
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
		DetonateShotTarget(Target, Kind, DisplayName);
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
		if (OperativeCharacterInternal::IsLiveEnemy(*It))
		{
			Enemies.Add(OperativeCharacterInternal::ShotFeet(*It));
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

FVector AOperativeCharacter::GetStanceAimOrigin() const
{
	return bInCover && bCornerAimActive && (CurrentCoverHeight == ECoverHeight::HighCover || IsManualCornerPeekSpot()) ? GetCoverFireOrigin()
		: GetMuzzleLocation();
}

FVector AOperativeCharacter::GetStanceAimBaseDirection() const
{
	if (bInCover && ((CurrentCoverHeight == ECoverHeight::HighCover && (bCornerAimActive || IsCornerHoldSpot()))
		|| (bCornerAimActive && IsManualCornerPeekSpot())))
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

