// AEnemyCharacter: construction, BeginPlay / EndPlay, Tick, body facing, AI move requests, debug state, the turn-based /
// world-AI-pause holds, the overhead label.
// Split out of EnemyCharacter.cpp verbatim (cleanup 2026-10-19; map: docs/port/REFACTOR_ENEMY_2026-10.md).
// Godot reference: Scenes/movements/enemy_base.gd (_ready, _physics_process, facing).

#include "Characters/EnemyCharacter.h"
#include "Combat/KnockdownComponent.h"
#include "AI/WorldAIPauseSubsystem.h"
#include "Interactables/VaultNavigation.h"
#include "Characters/FacingRules.h"
#include "CodexTactics.h"
#include "UI/OverheadLabel.h"
#include "Characters/EnemyAIController.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "Combat/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "Combat/VisionConeComponent.h"
#include "UObject/ConstructorHelpers.h"

AEnemyCharacter::AEnemyCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(40.f, 90.f);
	GetCapsuleComponent()->SetCollisionProfileName(TEXT("Pawn"));

	AIControllerClass = AEnemyAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	bUseControllerRotationYaw = false;

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	// The body is turned by UpdateMovementFacing (Godot lerp_angle), not by the movement component.
	Movement->bOrientRotationToMovement = false;
	Movement->bUseControllerDesiredRotation = false;
	Movement->MaxWalkSpeed = 300.f;

	HealthComponent = CreateDefaultSubobject<UHealthComponent>(TEXT("HealthComponent"));
	KnockdownComponent = CreateDefaultSubobject<UKnockdownComponent>(TEXT("KnockdownComponent"));
	KnockdownComponent->bCanBeKnockedDown = false; // ConfigureKnockdown by archetype

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(GetCapsuleComponent());
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BodyMesh->SetRelativeScale3D(FVector(0.7f, 0.7f, 1.8f));
	if (CylinderMesh.Succeeded())
	{
		BodyMesh->SetStaticMesh(CylinderMesh.Object);
	}

	VisionCone = CreateDefaultSubobject<UVisionConeComponent>(TEXT("VisionCone"));
	VisionCone->SetupAttachment(GetCapsuleComponent());

	Tags.Add(FName(TEXT("Enemy")));
}

void AEnemyCharacter::BeginPlay()
{
	Super::BeginPlay();
	// Blueprints made before the switch may still carry the engine's orient-to-movement: the facing code turns the body.
	GetCharacterMovement()->bOrientRotationToMovement = false;

	Tags.AddUnique(FName(TEXT("Enemy")));
	if (UEnemyRegistrySubsystem* Registry = GetWorld() ? GetWorld()->GetSubsystem<UEnemyRegistrySubsystem>() : nullptr)
	{
		Registry->RegisterEnemy(this); // the per-frame enemy queries read the registry, not a world scan
	}

	if (HealthComponent)
	{
		HealthComponent->OnDiedNative.AddUObject(this, &AEnemyCharacter::HandleDied);
		HealthComponent->OnDamaged.AddDynamic(this, &AEnemyCharacter::HandleDamaged);
	}
	if (KnockdownComponent)
	{
		KnockdownComponent->OnPhaseChanged.AddUObject(this, &AEnemyCharacter::HandleKnockdownPhase);
	}

	ResolvePlacedArchetype();
	ApplyArchetypeDefaults();
	ConfigureKnockdown();
	InitPatrol();
	// User request 2026-10-11: placed in the map without a route -> guards its post until it perceives the squad.
	TryStartGuardPost();
}

void AEnemyCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	bFacedThisTick = false;
	// Landed on a barricade / barrel top (a pounce): off it after 0.3 s, it cannot walk up there (no navmesh).
	ObstacleTopTime = !bIsDying && VaultNavigation::IsStandingOnObstacle(*this) ? ObstacleTopTime + DeltaTime : 0.f;
	FVector StepOff;
	if (ObstacleTopTime >= 0.3f && VaultNavigation::FindStepOffSpot(*this, StepOff))
	{
		ObstacleTopTime = 0.f;
		SetActorLocation(StepOff, false, nullptr, ETeleportType::TeleportPhysics);
		UE_LOG(LogCodexTactics, Display, TEXT("[Vault] %s jumped off an obstacle top"), *GetName());
	}
	// Sprint 14: hound pounce run-up (full speed for a while) and its cooldown.
	const float MaxSpeed = GetCharacterMovement() ? GetCharacterMovement()->MaxWalkSpeed : 0.f;
	RunUpSeconds = MaxSpeed > 0.f && GetVelocity().Size2D() >= MaxSpeed * 0.7f ? RunUpSeconds + DeltaTime : FMath::Max(0.f, RunUpSeconds - DeltaTime * 2.f);
	SecondsSinceHoundPounce += DeltaTime;
	TickBehavior(DeltaTime);
	UpdateMovementFacing(DeltaTime);
	UpdateVisionCone(DeltaTime);
}

void AEnemyCharacter::FaceYaw(float Yaw, float DeltaTime, float InterpSpeed)
{
	SetActorRotation(FMath::RInterpTo(GetActorRotation(), FRotator(0.f, Yaw, 0.f), DeltaTime, InterpSpeed));
	bFacedThisTick = true;
}

void AEnemyCharacter::UpdateMovementFacing(float DeltaTime)
{
	SmoothedVelocity = FacingRules::SmoothVelocity(SmoothedVelocity, GetVelocity(), DeltaTime);
	if (bFacedThisTick || bIsDying || IsJumpAttacking() || SmoothedVelocity.SizeSquared2D() < 20.f * 20.f)
	{
		return;
	}
	// Jostling in a crowd close to its prey (slow, within 6 m): it keeps its eyes on the prey instead of turning with every
	// avoidance nudge (FacingSmoke: a flanker squeezing into the pack weaved its body back and forth).
	float WantedYaw = SmoothedVelocity.Rotation().Yaw;
	if (const AActor* Prey = CurrentTarget.Get(); Prey && SmoothedVelocity.SizeSquared2D() < 200.f * 200.f
		&& FVector::DistSquared2D(Prey->GetActorLocation(), GetActorLocation()) < 600.f * 600.f)
	{
		WantedYaw = (Prey->GetActorLocation() - GetActorLocation()).Rotation().Yaw;
	}
	SetActorRotation(FRotator(0.f, FacingRules::StepYaw(GetActorRotation().Yaw, WantedYaw, TurnSpeed, DeltaTime), 0.f));
}

void AEnemyCharacter::RequestAIMoveToLocation(AAIController* AIC, const FVector& Goal, float AcceptanceRadius)
{
	if (AEnemyAIController* EnemyAIC = Cast<AEnemyAIController>(AIC))
	{
		EnemyAIC->RequestMoveToLocation(Goal, AcceptanceRadius);
	}
	else if (AIC)
	{
		AIC->MoveToLocation(Goal, AcceptanceRadius, false, true);
	}
}

void AEnemyCharacter::RequestAIMoveToActor(AAIController* AIC, AActor* Goal, float AcceptanceRadius)
{
	if (AEnemyAIController* EnemyAIC = Cast<AEnemyAIController>(AIC))
	{
		EnemyAIC->RequestMoveToActor(Goal, AcceptanceRadius);
	}
	else if (AIC)
	{
		AIC->MoveToActor(Goal, AcceptanceRadius);
	}
}

void AEnemyCharacter::ResetAIMoveThrottle()
{
	if (AEnemyAIController* EnemyAIC = Cast<AEnemyAIController>(GetController()))
	{
		EnemyAIC->ResetRepathState();
	}
}

FVector AEnemyCharacter::GodotPosition(const AActor* Actor)
{
	if (const ACharacter* Character = Cast<ACharacter>(Actor))
	{
		return Actor->GetActorLocation() - FVector(0.f, 0.f, Character->GetSimpleCollisionHalfHeight() - 100.f);
	}
	FVector Origin;
	FVector Extent;
	Actor->GetActorBounds(true, Origin, Extent);
	return FVector(Actor->GetActorLocation().X, Actor->GetActorLocation().Y, Origin.Z - Extent.Z);
}

FString AEnemyCharacter::GetDebugState() const
{
	const AAIController* AIC = Cast<AAIController>(GetController());
	const UPathFollowingComponent* Follow = AIC ? AIC->GetPathFollowingComponent() : nullptr;
	int32 DeadEnds = 0;
	for (const TPair<TWeakObjectPtr<AActor>, double>& Entry : UnreachableUntil)
	{
		DeadEnds += Entry.Value > GetWorld()->GetTimeSeconds() ? 1 : 0;
	}
	FVector Fire;
	float Radius = 0.f;
	return FString::Printf(TEXT("ctrl %s, follow %d, progress %.1f s, fear-zone near %d, braving %d, fleeing %d, falling back %d, dead ends %d, stagger %d, attacking %d, mode %d, maxspeed %.0f"),
		AIC ? *AIC->GetClass()->GetName() : TEXT("none"), Follow ? static_cast<int32>(Follow->GetStatus()) : -1, ProgressTimer,
		bFearsFire && FindNearestFireZone(800.f, Fire, Radius) ? 1 : 0, bBravingFire ? 1 : 0, bFleeingFire ? 1 : 0, bFallingBack ? 1 : 0, DeadEnds,
		HealthComponent && HealthComponent->HasStatusEffect(EStatusEffect::Stagger) ? 1 : 0, AttackTimer > 0.f ? 1 : 0,
		static_cast<int32>(GetCharacterMovement()->MovementMode), GetCharacterMovement()->MaxWalkSpeed);
}

void AEnemyCharacter::SetTurnBasedHeld(bool bHeld)
{
	if (bTurnBasedHeld == bHeld)
	{
		return;
	}
	bTurnBasedHeld = bHeld;
	ResetAIMoveThrottle(); // a frozen / released enemy asks for its move afresh
	if (bHeld)
	{
		if (AAIController* AIC = Cast<AAIController>(GetController()))
		{
			AIC->StopMovement();
		}
		GetCharacterMovement()->StopMovementImmediately(); // no velocity carried into the frozen fight
		bPatrolMoveIssued = false;
		bEscortMoving = false;
		bSearchMoving = false;
		bScentMoveIssued = false;
	}
	OnTurnBasedHeldChanged(bHeld);
}

bool AEnemyCharacter::HoldForWorldAIPause()
{
	if (!UWorldAIPauseSubsystem::IsPausedIn(GetWorld()))
	{
		bHeldByAIPause = false;
		return false;
	}
	if (!bHeldByAIPause)
	{
		// Stands still in its idle pose; the walk is re-issued when the dialogue / cutscene is over.
		bHeldByAIPause = true;
		ResetAIMoveThrottle();
		if (AAIController* AIC = Cast<AAIController>(GetController()))
		{
			AIC->StopMovement();
		}
		bPatrolMoveIssued = false;
		bEscortMoving = false;
		bSearchMoving = false;
		bScentMoveIssued = false;
		UE_LOG(LogCodexTactics, Verbose, TEXT("[Patrol] %s held (world AI paused)"), *GetName());
	}
	return true;
}

void AEnemyCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UEnemyRegistrySubsystem* Registry = GetWorld() ? GetWorld()->GetSubsystem<UEnemyRegistrySubsystem>() : nullptr)
	{
		Registry->UnregisterEnemy(this);
	}
	Super::EndPlay(EndPlayReason);
}

bool AEnemyCharacter::GetOverheadLabel(FOverheadLabel& OutLabel) const
{
	if (bIsDying || !HealthComponent || !HealthComponent->IsAlive() || IsHidden())
	{
		return false;
	}
	FString Status;
	if (HealthComponent->HasStatusEffect(EStatusEffect::Burning))
	{
		Status += TEXT(" BURNING");
	}
	if (HealthComponent->HasStatusEffect(EStatusEffect::Frozen))
	{
		Status += TEXT(" ICED");
	}
	if (HealthComponent->HasStatusEffect(EStatusEffect::Stagger))
	{
		Status += TEXT(" STUNNED");
	}
	if (HealthComponent->HasStatusEffect(EStatusEffect::ArmorShred))
	{
		Status += TEXT(" ARMOR-");
	}
	OutLabel.Text = FString::Printf(TEXT("%s%s\n%d/%d"), *EnemyDisplayName, *Status,
		FMath::FloorToInt(FMath::Max(0.f, HealthComponent->GetCurrentHealth())), FMath::FloorToInt(HealthComponent->GetMaxHealth()));
	OutLabel.Color = FLinearColor(1.f, 0.4f, 0.4f);
	OutLabel.HeightCm = Archetype == EEnemyArchetype::FrostHound || Archetype == EEnemyArchetype::Cutter ? 115.f
		: (Archetype == EEnemyArchetype::Brute ? 240.f : 180.f);
	// Godot armor_tier: brute heavy (red), spitter medium (yellow), the rest light (green).
	OutLabel.bHasMarker = true;
	OutLabel.MarkerColor = Archetype == EEnemyArchetype::Brute ? FLinearColor(0.95f, 0.2f, 0.2f)
		: (Archetype == EEnemyArchetype::Spitter ? FLinearColor(0.95f, 0.85f, 0.2f) : FLinearColor(0.25f, 0.9f, 0.3f));
	return true;
}
