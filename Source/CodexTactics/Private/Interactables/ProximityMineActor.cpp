#include "Interactables/ProximityMineActor.h"
#include "Characters/EnemyCharacter.h"
#include "Subsystems/CodexEventBus.h"
#include "UI/FloatingTextSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Interactables/RelocationSubsystem.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "ProximityMineActor"

AProximityMineActor::AProximityMineActor()
{
	PrimaryActorTick.bCanEverTick = true;
	DeployableType = EDeployableType::Mine;
	DisplayName = LOCTEXT("Name", "Anti-personnel Mine");
	bTrapped = true; // Godot is_armed / is_trapped
	InteractionDistance = 110.f; // Godot interaction_distance 1.1 m from the trigger sphere

	// The box is the trigger area (Godot sphere 1.6 m): clickable, not blocking, no NavMesh hole.
	Box->SetBoxExtent(FVector(160.f, 160.f, 30.f));
	Box->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionResponseToAllChannels(ECR_Ignore);
	Box->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Box->SetCanEverAffectNavigation(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BaseMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (CylinderMesh.Succeeded())
	{
		Mesh->SetStaticMesh(CylinderMesh.Object);
	}
	if (BaseMaterial.Succeeded())
	{
		Mesh->SetMaterial(0, BaseMaterial.Object);
	}
	// Godot disk: radius 0.55 m, 8 cm high, on the ground (set here too: placement ghosts copy the class default).
	Mesh->SetRelativeScale3D(FVector(1.1f, 1.1f, 0.08f));
	Mesh->SetRelativeLocation(FVector(0.f, 0.f, -26.f));
}

void AProximityMineActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// Godot disk: radius 0.55 m, 8 cm high, on the ground (the box is the larger trigger area).
	Mesh->SetRelativeScale3D(FVector(1.1f, 1.1f, 0.08f));
	Mesh->SetRelativeLocation(FVector(0.f, 0.f, -Box->GetUnscaledBoxExtent().Z + 4.f));
	UpdateVisuals();
}

void AProximityMineActor::BeginPlay()
{
	Super::BeginPlay();
	UpdateVisuals();
}

void AProximityMineActor::SetPlacedBySquad()
{
	bPlacedBySquad = true;
	bTrapped = true;
	bRevealed = true;
	ArmingTimeLeft = ArmingDelay;
	UpdateVisuals();
}

void AProximityMineActor::Reveal()
{
	bRevealed = true;
	UpdateVisuals();
}

void AProximityMineActor::UpdateVisuals()
{
	Mesh->SetVisibility(bRevealed);
	// Hidden mines cannot be clicked until spotted.
	Box->SetCollisionResponseToChannel(ECC_Visibility, bRevealed ? ECR_Block : ECR_Ignore);
	if (UMaterialInstanceDynamic* Material = Mesh->CreateAndSetMaterialInstanceDynamic(0))
	{
		Material->SetVectorParameterValue(TEXT("Color"), bPlacedBySquad ? SquadColor : HostileColor);
	}
}

void AProximityMineActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bDetonated)
	{
		return;
	}
	if (ArmingTimeLeft > 0.f)
	{
		ArmingTimeLeft -= DeltaSeconds;
		if (ArmingTimeLeft <= 0.f)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("%s armed"), *GetName());
			UFloatingTextSubsystem::SpawnAboveMine(this, TEXT("⚠️ ARMED!"), FLinearColor(1.f, 0.3f, 0.2f)); // Godot _on_mine_armed_feedback
		}
		return;
	}
	if (!bTrapped)
	{
		return;
	}
	if (!bRevealed)
	{
		ScanForSpotters();
	}

	const FVector Location = GetActorLocation();
	// 1. Enemies always set it off.
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (!It->ActorHasTag(FName(TEXT("Enemy"))))
		{
			continue;
		}
		const UHealthComponent* EnemyHealth = It->FindComponentByClass<UHealthComponent>();
		if (EnemyHealth && EnemyHealth->IsAlive() && FVector::Dist2D(Location, It->GetActorLocation()) <= TriggerRadius)
		{
			Detonate();
			return;
		}
	}
	// 2. Operatives, unless it is a friendly-safe squad mine; the defuser walking up is ignored.
	if (!bPlacedBySquad || bFriendlyFire)
	{
		if (const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>())
		{
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member == ApproachingDefuser.Get() || !Member->HealthComponent || !Member->HealthComponent->IsAlive())
				{
					continue;
				}
				if (FVector::Dist2D(Location, Member->GetActorLocation()) <= TriggerRadius)
				{
					Detonate();
					return;
				}
			}
		}
	}
}

void AProximityMineActor::ScanForSpotters()
{
	// Godot: operatives scan outside combat (the turn-based grid takes over in combat).
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	if (Flow && Flow->GetPhase() == ECodexGamePhase::WaveCombat)
	{
		return;
	}
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	if (!Squad || (bPlacedBySquad && !bFriendlyFire))
	{
		return;
	}
	for (AOperativeCharacter* Member : Squad->GetMembers())
	{
		const float Range = DetectionRange + (Member->SquadRole == EOperativeRole::MedicSapper ? SapperDetectionBonus : 0.f);
		if (FVector::Dist(GetActorLocation(), Member->GetActorLocation()) <= Range)
		{
			HandleSpotted(Member);
			return;
		}
	}
}

void AProximityMineActor::HandleSpotted(AOperativeCharacter* Spotter)
{
	Reveal();
	// Godot _on_mine_spotted: the whole squad stops, the spotter faces the mine and warns on the radio.
	if (const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>())
	{
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->StopOperative();
		}
	}
	const FRotator Facing = (GetActorLocation() - Spotter->GetActorLocation()).Rotation();
	Spotter->SetActorRotation(FRotator(0.f, Facing.Yaw, 0.f));
	UFloatingTextSubsystem::SpawnAboveOperative(Spotter, TEXT("⚠️ MINE SPOTTED!"), FLinearColor(1.f, 0.85f, 0.1f));
	PostLine(Spotter->DisplayName, LOCTEXT("Spotted", "⚠️ Mine! Everyone hold position!"));
	if (UCodexEventBus* Bus = UCodexEventBus::Get(this))
	{
		Bus->OnMineSpotted.Broadcast(this, Spotter);
	}
	if (URelocationSubsystem* Relocation = GetWorld()->GetSubsystem<URelocationSubsystem>())
	{
		Relocation->DropAllForMine();
	}
}

void AProximityMineActor::DetonateTrap(bool bByShot, const FText& InstigatorName)
{
	Detonate();
}

void AProximityMineActor::Detonate()
{
	if (bDetonated)
	{
		return;
	}
	bDetonated = true;
	bTrapped = false;
	UE_LOG(LogCodexTactics, Display, TEXT("%s detonated"), *GetName());
	ApplyBlast(ExplosionDamage, ExplosionDamage * DeployableRules::SquadDamageScale, ExplosionRadius, 0.50f, EDamageType::Explosive,
		LOCTEXT("Source", "Mine"), LOCTEXT("SquadHit", "💥 Argh! Hit by the mine blast (-{0} HP)!"));
	// Sprint 11: the blast is heard — patrols within 20 m break off.
	AEnemyCharacter::AlertPatrolsNearTrap(GetWorld(), GetActorLocation());
	Destroy();
}

void AProximityMineActor::DescribeForMenu(const AOperativeCharacter* Leader, FText& OutTitle, FText& OutDescription, FText& OutConfirm,
	bool& bOutDisabled) const
{
	int32 Count = 0;
	int32 Max = 0;
	GetLeaderSupply(Leader, Count, Max);
	const bool bFull = Count >= Max;
	const FText LeaderName = Leader ? Leader->DisplayName : LOCTEXT("Soldier", "Soldier");
	const FText DamageInfo = FText::Format(LOCTEXT("DamageInfo", " (Damage: {0})"), FMath::FloorToInt(ExplosionDamage));

	OutTitle = LOCTEXT("Title", "💣 Anti-personnel Mine");
	OutConfirm = bTrapped ? LOCTEXT("Defuse", "Defuse") : (bDeployable ? LOCTEXT("PickUp", "Pick up") : LOCTEXT("CannotPickUp", "Cannot pick up"));
	if (bFull && bDeployable)
	{
		OutConfirm = FText::Format(LOCTEXT("Full", "Inventory full ({0}/{1})"), Count, Max);
	}
	if (bTrapped)
	{
		OutDescription = FText::Format(LOCTEXT("DescArmed", "Armed anti-personnel mine{0}.\n{1}\n({2} mines: {3}/{4})."),
			DamageInfo, DescribeDefusal(Leader), LeaderName, Count, Max);
	}
	else if (bDeployable)
	{
		OutDescription = FText::Format(LOCTEXT("DescPickUp", "Disarm and take the mine{0}?\nIt will be added to personal supplies ({1}: {2}/{3})."),
			DamageInfo, LeaderName, Count, Max);
	}
	else
	{
		OutDescription = FText::Format(LOCTEXT("DescFixed", "Stationary mine{0}. Deployable is off: it cannot be stowed in the inventory."),
			DamageInfo);
	}
	bOutDisabled = (bFull && bDeployable) || (!bDeployable && !bTrapped);
}

#undef LOCTEXT_NAMESPACE
