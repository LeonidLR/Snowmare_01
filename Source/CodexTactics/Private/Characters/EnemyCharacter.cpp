#include "Characters/EnemyCharacter.h"
#include "UI/OverheadLabel.h"
#include "Characters/EnemyAIController.h"
#include "UI/FloatingTextSubsystem.h"
#include "Interactables/TurretActor.h"
#include "Interactables/HeatSourceComponent.h"
#include "Interactables/BarricadeActor.h"
#include "Interactables/BarrelActor.h"
#include "EngineUtils.h"
#include "Combat/CombatFeedbackSubsystem.h"
#include "AIController.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/ProgressionRules.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Core/CodexTacticsGameMode.h"
#include "Data/GodotBalanceAsset.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
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
	Movement->bOrientRotationToMovement = true;
	Movement->bUseControllerDesiredRotation = false;
	Movement->MaxWalkSpeed = 300.f;

	HealthComponent = CreateDefaultSubobject<UHealthComponent>(TEXT("HealthComponent"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(GetCapsuleComponent());
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BodyMesh->SetRelativeScale3D(FVector(0.7f, 0.7f, 1.8f));
	if (CylinderMesh.Succeeded())
	{
		BodyMesh->SetStaticMesh(CylinderMesh.Object);
	}

	Tags.Add(FName(TEXT("Enemy")));
}

void AEnemyCharacter::BeginPlay()
{
	Super::BeginPlay();

	Tags.AddUnique(FName(TEXT("Enemy")));

	if (HealthComponent)
	{
		HealthComponent->OnDiedNative.AddUObject(this, &AEnemyCharacter::HandleDied);
		HealthComponent->OnDamaged.AddDynamic(this, &AEnemyCharacter::HandleDamaged);
	}

	ApplyArchetypeDefaults();
}

void AEnemyCharacter::InitializeArchetype(EEnemyArchetype InArchetype)
{
	Archetype = InArchetype;
	ApplyArchetypeDefaults();
}

void AEnemyCharacter::ApplyArchetypeDefaults()
{
	if (!HealthComponent)
	{
		return;
	}

	FLinearColor TintColor = FLinearColor(0.6f, 0.6f, 0.7f);
	float MeshScale = 1.0f;

	switch (Archetype)
	{
	case EEnemyArchetype::Cutter:
		// Godot enemy_cutter.gd _ready: 75 HP, 6.2 m/s, 18 damage, 2 m, 1.1 s, crit 0.25 x1.75.
		EnemyDisplayName = TEXT("Механо-гончая Cutter");
		HealthComponent->SetMaxHealth(75.0f);
		HealthComponent->SetArmorTier(EArmorTier::Light);
		GetCharacterMovement()->MaxWalkSpeed = 620.0f;
		AttackDamage = 18.0f;
		AttackRange = 200.0f;
		AttackCooldown = 1.1f;
		CritChance = 0.25f;
		CritMultiplier = 1.75f;
		bFearsFire = true;
		TintColor = FLinearColor(0.35f, 0.4f, 0.45f);
		MeshScale = 0.85f;
		GetCapsuleComponent()->SetCapsuleSize(35.f, 60.f);
		if (const UGodotBalanceAsset* Jump = LoadObject<UGodotBalanceAsset>(nullptr, TEXT("/Game/Data/Enemies/DA_EnemyAnim_cutter.DA_EnemyAnim_cutter")))
		{
			bJumpAttackEnabled = Jump->GetNumber(TEXT("enable_jump_attack"), 0.f) > 0.5f;
			JumpAttackSpeed = Jump->GetNumber(TEXT("jump_attack_speed"), JumpAttackSpeed);
			JumpMinDistance = Jump->GetNumber(TEXT("jump_min_distance"), JumpMinDistance / 100.f) * 100.f;
			JumpMaxDistance = Jump->GetNumber(TEXT("jump_max_distance"), JumpMaxDistance / 100.f) * 100.f;
			JumpCooldown = Jump->GetNumber(TEXT("jump_cooldown"), JumpCooldown);
			JumpAttackDamage = Jump->GetNumber(TEXT("jump_attack_damage"), JumpAttackDamage);
			JumpDamageRadius = Jump->GetNumber(TEXT("jump_damage_radius"), JumpDamageRadius / 100.f) * 100.f;
		}
		break;

	case EEnemyArchetype::FrostHound:
		EnemyDisplayName = TEXT("Ледяная гончая");
		HealthComponent->SetMaxHealth(45.0f);
		HealthComponent->SetArmorTier(EArmorTier::Light);
		HealthComponent->SetBaseArmorReduction(0.10f);
		GetCharacterMovement()->MaxWalkSpeed = 540.0f; // 5.4 m/s
		AttackDamage = 12.0f;
		AttackRange = 180.0f; // 1.8 m
		AttackCooldown = 1.0f;
		bFearsFire = true;
		TintColor = FLinearColor(0.2f, 0.65f, 0.95f);
		MeshScale = 0.8f;
		GetCapsuleComponent()->SetCapsuleSize(35.f, 60.f);
		break;

	case EEnemyArchetype::Spitter:
	case EEnemyArchetype::CryoDrone:
		EnemyDisplayName = TEXT("Ледяной стрелок");
		HealthComponent->SetMaxHealth(70.0f);
		HealthComponent->SetArmorTier(EArmorTier::Medium);
		HealthComponent->SetBaseArmorReduction(0.40f);
		GetCharacterMovement()->MaxWalkSpeed = 320.0f; // 3.2 m/s
		AttackDamage = 18.0f;
		AttackRange = 1500.0f; // 15.0 m
		AttackCooldown = 2.2f;
		bFearsFire = false;
		TintColor = FLinearColor(0.85f, 0.95f, 1.0f);
		MeshScale = 1.0f;
		GetCapsuleComponent()->SetCapsuleSize(40.f, 85.f);
		break;

	case EEnemyArchetype::Brute:
		EnemyDisplayName = TEXT("Ледяной громила");
		HealthComponent->SetMaxHealth(220.0f);
		HealthComponent->SetArmorTier(EArmorTier::Heavy);
		HealthComponent->SetBaseArmorReduction(0.75f);
		GetCharacterMovement()->MaxWalkSpeed = 180.0f; // 1.8 m/s
		AttackDamage = 35.0f;
		AttackRange = 240.0f; // 2.4 m
		AttackCooldown = 2.0f;
		bFearsFire = false;
		TintColor = FLinearColor(0.15f, 0.35f, 0.65f);
		MeshScale = 1.4f;
		GetCapsuleComponent()->SetCapsuleSize(60.f, 120.f);
		break;

	case EEnemyArchetype::Frostbitten:
	default:
		EnemyDisplayName = TEXT("Промёрзший");
		HealthComponent->SetMaxHealth(60.0f);
		HealthComponent->SetArmorTier(EArmorTier::Light);
		HealthComponent->SetBaseArmorReduction(0.10f);
		GetCharacterMovement()->MaxWalkSpeed = 280.0f; // 2.8 m/s
		AttackDamage = 15.0f;
		AttackRange = 180.0f; // 1.8 m
		AttackCooldown = 1.5f;
		bFearsFire = true;
		TintColor = FLinearColor(0.6f, 0.6f, 0.7f);
		MeshScale = 1.0f;
		GetCapsuleComponent()->SetCapsuleSize(40.f, 90.f);
		break;
	}

	if (BodyMesh)
	{
		BodyMesh->SetRelativeScale3D(FVector(0.7f * MeshScale, 0.7f * MeshScale, 1.8f * MeshScale));
		if (!BodyMaterial)
		{
			BodyMaterial = BodyMesh->CreateDynamicMaterialInstance(0);
		}
		if (BodyMaterial)
		{
			BodyMaterial->SetVectorParameterValue(TEXT("Color"), TintColor);
		}
	}

	// Godot per-type elemental_affinities / base_armor_reduction (enemy_frost_*.gd, enemy_cutter.gd, enemy_cryo_drone.gd).
	HealthComponent->ElementalAffinities = EnemyAIRules::GetAffinities(Archetype);
	HealthComponent->SetBaseArmorReduction(EnemyAIRules::GetBaseArmor(Archetype));

	KillExpReward = ProgressionRules::KillReward(Archetype, nullptr);
	// Godot enemies re-read their stats from game_balance_config.tres (imported DA_GameBalanceConfig).
	if (const ACodexTacticsGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ACodexTacticsGameMode>() : nullptr)
	{
		if (const UGodotBalanceAsset* Config = GameMode->GameBalanceConfig.LoadSynchronous())
		{
			ApplyBalance(*Config);
			AIConfig = EnemyAIRules::ConfigFromBalance(Config);
			KillExpReward = ProgressionRules::KillReward(Archetype, Config);
		}
	}
	BaseWalkSpeed = GetCharacterMovement()->MaxWalkSpeed;
}

void AEnemyCharacter::ApplyWaveModifiers(float HpMult, float DamageMult, float SpeedMult, float CustomHealth)
{
	const float BaseHealth = CustomHealth > 0.f ? CustomHealth : HealthComponent->GetMaxHealth();
	HealthComponent->SetMaxHealth(BaseHealth * HpMult, /*bResetCurrent*/ true);
	AttackDamage *= DamageMult;
	GetCharacterMovement()->MaxWalkSpeed *= SpeedMult;
	BaseWalkSpeed = GetCharacterMovement()->MaxWalkSpeed;
}

void AEnemyCharacter::ApplyBalance(const UGodotBalanceAsset& Config)
{
	CritChance = Config.GetNumber(TEXT("enemy_crit_chance"), CritChance);
	CritMultiplier = Config.GetNumber(TEXT("enemy_crit_multiplier"), CritMultiplier);
	const TCHAR* Prefix = nullptr;
	switch (Archetype)
	{
	case EEnemyArchetype::FrostHound: Prefix = TEXT("hound_"); break;
	case EEnemyArchetype::Spitter: Prefix = TEXT("spitter_"); break;
	case EEnemyArchetype::Brute: Prefix = TEXT("brute_"); break;
	case EEnemyArchetype::Frostbitten:
		CritChance = Config.GetNumber(TEXT("frostbitten_crit_chance"), CritChance);
		return;
	case EEnemyArchetype::Cutter:
		CritChance = Config.GetNumber(TEXT("hound_crit_chance"), CritChance); // Godot enemy_cutter.gd
		return;
	default:
		return;
	}
	auto Key = [Prefix](const TCHAR* Name) { return FName(FString(Prefix) + Name); };
	CritChance = Config.GetNumber(Key(TEXT("crit_chance")), CritChance);
	if (HealthComponent)
	{
		HealthComponent->SetMaxHealth(Config.GetNumber(Key(TEXT("health")), HealthComponent->GetMaxHealth()), true);
	}
	GetCharacterMovement()->MaxWalkSpeed = Config.GetNumber(Key(TEXT("speed")), GetCharacterMovement()->MaxWalkSpeed / 100.f) * 100.f;
	AttackDamage = Config.GetNumber(Key(TEXT("damage")), AttackDamage);
	AttackRange = Config.GetNumber(Key(TEXT("attack_range")), AttackRange / 100.f) * 100.f;
	AttackCooldown = Config.GetNumber(Archetype == EEnemyArchetype::Spitter ? Key(TEXT("shoot_interval")) : Key(TEXT("attack_cooldown")), AttackCooldown);
}

void AEnemyCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bIsDying || !HealthComponent || !HealthComponent->IsAlive())
	{
		return;
	}
	AAIController* AIC = Cast<AAIController>(GetController());

	if (JumpCooldownTimer > 0.f)
	{
		JumpCooldownTimer -= DeltaTime;
	}
	if (IsJumpAttacking())
	{
		TickJumpAttack(DeltaTime);
		return;
	}

	// Godot: stagger freezes the enemy.
	if (HealthComponent->HasStatusEffect(EStatusEffect::Stagger))
	{
		if (AIC)
		{
			AIC->StopMovement();
		}
		return;
	}
	if (AttackTimer > 0.0f)
	{
		AttackTimer -= DeltaTime;
	}
	// Godot current_max_speed: frost halves the speed; fleeing from fire speeds it up.
	GetCharacterMovement()->MaxWalkSpeed = BaseWalkSpeed * (HealthComponent->HasStatusEffect(EStatusEffect::Frozen) ? 0.5f : 1.f)
		* (bFleeingFire ? AIConfig.FireFearFleeSpeedMultiplier : 1.f);

	// 0. Panic fear of fire (hounds, cutters, frostbitten).
	FVector Fire;
	if (bFearsFire && AIConfig.bFireFearEnabled && FindNearestFire(Fire))
	{
		const AActor* Victim = CurrentTarget.Get();
		const FVector VictimLocation = Victim ? Victim->GetActorLocation() : FVector::ZeroVector;
		const FVector Direction = EnemyAIRules::FleeDirection(GetActorLocation(), Fire, Victim ? &VictimLocation : nullptr);
		if (AIC)
		{
			AIC->MoveToLocation(GetActorLocation() + Direction * 300.f, 50.f, false, true);
		}
		if (!bFleeingFire)
		{
			bFleeingFire = true;
			UFloatingTextSubsystem::SpawnAboveEnemy(this, TEXT("🔥😱 СТРАХ ОГНЯ!"), FLinearColor(1.f, 0.45f, 0.1f));
		}
		return;
	}
	bFleeingFire = false;

	if (Archetype == EEnemyArchetype::Spitter || Archetype == EEnemyArchetype::CryoDrone)
	{
		TickSpitter(DeltaTime);
		return;
	}

	AActor* Target = FindTarget();
	CurrentTarget = Target;
	if (!Target)
	{
		if (AIC)
		{
			AIC->StopMovement();
		}
		return;
	}

	auto Face = [this, DeltaTime](const AActor* Actor)
	{
		FRotator LookRot = (Actor->GetActorLocation() - GetActorLocation()).Rotation();
		SetActorRotation(FMath::RInterpTo(GetActorRotation(), FRotator(0.f, LookRot.Yaw, 0.f), DeltaTime, 10.0f));
	};

	// Godot enemy_cutter.gd _process_enemy_behavior: a pounce at 3.5-9 m (at most 2.5 m of height difference).
	if (Archetype == EEnemyArchetype::Cutter && bJumpAttackEnabled && JumpCooldownTimer <= 0.f)
	{
		const FVector JumpFeet = GetActorLocation() - FVector(0.f, 0.f, GetSimpleCollisionHalfHeight());
		const FVector JumpTargetPosition = GodotPosition(Target);
		const float JumpDistance = FVector::Dist(JumpFeet, JumpTargetPosition);
		if (JumpDistance >= JumpMinDistance && JumpDistance <= JumpMaxDistance && FMath::Abs(JumpTargetPosition.Z - JumpFeet.Z) <= 250.f
			&& StartJumpAttack(Target))
		{
			return;
		}
	}

	// 1. A barricade / turret in the way is smashed first.
	if (AActor* Obstacle = FindBlockingObstacle(Target))
	{
		if (AIC)
		{
			AIC->StopMovement();
		}
		Face(Obstacle);
		if (AttackTimer <= 0.f)
		{
			AttackObject(Obstacle);
		}
		return;
	}

	// 2. Melee reach (Godot: attack range and at most 1.2 m of height difference), else close in.
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetSimpleCollisionHalfHeight());
	const FVector TargetPosition = GodotPosition(Target);
	if (EnemyAIRules::CanMelee(FVector::Dist(Feet, TargetPosition), FMath::Abs(TargetPosition.Z - Feet.Z), AttackRange))
	{
		if (AIC)
		{
			AIC->StopMovement();
		}
		Face(Target);
		if (AttackTimer <= 0.0f)
		{
			if (Target->IsA<AOperativeCharacter>())
			{
				AttackTarget(Target);
			}
			else
			{
				AttackObject(Target);
			}
		}
	}
	else if (AIC)
	{
		AIC->MoveToActor(Target, AttackRange * 0.5f);
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

AActor* AEnemyCharacter::FindTarget() const
{
	UWorld* World = GetWorld();
	const USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
	if (!Squad)
	{
		return nullptr;
	}
	TArray<AActor*> Actors;
	TArray<FEnemyTargetCandidate> Candidates;
	for (AOperativeCharacter* Member : Squad->GetMembers())
	{
		if (Member->HealthComponent && Member->HealthComponent->IsAlive())
		{
			Actors.Add(Member);
			Candidates.Add({ EEnemyTargetKind::Operative, GodotPosition(Member), true });
		}
	}
	for (TActorIterator<ATurretActor> It(World); It; ++It)
	{
		const UHealthComponent* TurretHealth = It->FindComponentByClass<UHealthComponent>();
		Actors.Add(*It);
		Candidates.Add({ EEnemyTargetKind::Turret, GodotPosition(*It), !It->IsBroken() && TurretHealth && TurretHealth->IsAlive() });
	}
	for (TActorIterator<AInteractableActor> It(World); It; ++It)
	{
		if (It->ObjectType == EInteractableType::Generator)
		{
			Actors.Add(*It);
			Candidates.Add({ EEnemyTargetKind::Generator, GodotPosition(*It), !It->bGeneratorBroken && It->GeneratorHealth > 0.f });
		}
	}
	const bool bTurretHit = LastAttackerSource.Contains(TEXT("Турель")) || LastAttackerSource.Contains(TEXT("Turret"));
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetSimpleCollisionHalfHeight());
	const int32 Index = EnemyAIRules::SelectTarget(AIConfig, EnemyAIRules::IsSmallEnemy(Archetype), Feet, Candidates, bTurretHit);
	return Actors.IsValidIndex(Index) ? Actors[Index] : nullptr;
}

bool AEnemyCharacter::FindNearestFire(FVector& OutFire) const
{
	UWorld* World = GetWorld();
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetSimpleCollisionHalfHeight());
	float MinDistance = AIConfig.FireFearRadius;
	bool bFound = false;
	for (TActorIterator<ABarrelActor> It(World); It; ++It)
	{
		const float Distance = FVector::Dist(Feet, It->GetActorLocation());
		if (It->IsBurning() && Distance < MinDistance)
		{
			MinDistance = Distance;
			OutFire = It->GetActorLocation();
			bFound = true;
		}
	}
	for (const TWeakObjectPtr<UHeatSourceComponent>& Source : UHeatSourceComponent::GetAllSources())
	{
		if (!Source.IsValid() || Source->GetWorld() != World || !Source->IsHeatActive())
		{
			continue;
		}
		const float Distance = FVector::Dist(Feet, Source->GetComponentLocation());
		if (Distance < FMath::Max(AIConfig.FireFearRadius, Source->Radius) && Distance < MinDistance)
		{
			MinDistance = Distance;
			OutFire = Source->GetComponentLocation();
			bFound = true;
		}
	}
	return bFound;
}

AActor* AEnemyCharacter::FindBlockingObstacle(const AActor* Target) const
{
	UWorld* World = GetWorld();
	TArray<AActor*> Actors;
	TArray<FVector> Obstacles;
	for (TActorIterator<ABarricadeActor> It(World); It; ++It)
	{
		const UHealthComponent* BarricadeHealth = It->FindComponentByClass<UHealthComponent>();
		if (BarricadeHealth && BarricadeHealth->IsAlive())
		{
			Actors.Add(*It);
			Obstacles.Add(GodotPosition(*It));
		}
	}
	if (AIConfig.bCanTargetTurrets)
	{
		for (TActorIterator<ATurretActor> It(World); It; ++It)
		{
			const UHealthComponent* TurretHealth = It->FindComponentByClass<UHealthComponent>();
			if (*It != Target && TurretHealth && TurretHealth->IsAlive())
			{
				Actors.Add(*It);
				Obstacles.Add(GodotPosition(*It));
			}
		}
	}
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetSimpleCollisionHalfHeight());
	const FVector TargetPosition = Target ? GodotPosition(Target) : FVector::ZeroVector;
	const int32 Index = EnemyAIRules::SelectBlockingObstacle(Feet, Target ? &TargetPosition : nullptr, Obstacles);
	return Actors.IsValidIndex(Index) ? Actors[Index] : nullptr;
}

void AEnemyCharacter::AttackObject(AActor* Object)
{
	AttackTimer = AttackCooldown;
	if (AInteractableActor* Interactable = Cast<AInteractableActor>(Object);
		Interactable && Interactable->ObjectType == EInteractableType::Generator && !Object->IsA<ADeployableActor>())
	{
		Interactable->TakeGeneratorDamage(AttackDamage);
		return;
	}
	UHealthComponent* ObjectHealth = Object->FindComponentByClass<UHealthComponent>();
	if (!ObjectHealth)
	{
		return;
	}
	// Godot barricade / turret take_damage: raw damage; an enemy blow sets off a trap. Brutes hit barricades twice.
	const bool bBarricade = Object->IsA<ABarricadeActor>();
	ObjectHealth->ApplyDirectHealthLoss(AttackDamage * (bBarricade && Archetype == EEnemyArchetype::Brute ? 2.f : 1.f), EnemyDisplayName);
	if (AInteractableActor* Trapped = Cast<AInteractableActor>(Object); Trapped && Trapped->bTrapped && IsValid(Trapped))
	{
		Trapped->DetonateTrap(false, NSLOCTEXT("EnemyCharacter", "EnemyBlow", "Удар противника"));
	}
}

void AEnemyCharacter::TickSpitter(float DeltaTime)
{
	UWorld* World = GetWorld();
	AAIController* AIC = Cast<AAIController>(GetController());
	const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
	TArray<AOperativeCharacter*> Members;
	TArray<FVector> Positions;
	TArray<bool> Elevated;
	for (AOperativeCharacter* Member : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
	{
		if (Member->HealthComponent && Member->HealthComponent->IsAlive())
		{
			Members.Add(Member);
			Positions.Add(GodotPosition(Member));
			Elevated.Add(GodotPosition(Member).Z >= 240.f); // Godot: y >= 2.4 m counts as elevated ground
		}
	}
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetSimpleCollisionHalfHeight());
	const int32 Index = EnemyAIRules::SelectSpitterTarget(Feet, Positions, Elevated);
	AOperativeCharacter* Target = Members.IsValidIndex(Index) ? Members[Index] : Cast<AOperativeCharacter>(FindClosestSquadMember());
	CurrentTarget = Target;
	if (!Target)
	{
		if (AIC)
		{
			AIC->StopMovement();
		}
		return;
	}
	// Line of fire: from 0.4 m above the feet to the target's stance height (Godot _check_line_of_sight).
	const FVector Start = Feet + FVector(0.f, 0.f, 40.f);
	const float TargetHeight = Target->GetStance() == EOperativeStance::Prone ? 30.f : (Target->GetStance() == EOperativeStance::Crouching ? 90.f : 150.f);
	const FVector End = GodotPosition(Target) - FVector(0.f, 0.f, 100.f - TargetHeight);
	FSpitterLine Line;
	if (End.Z - Feet.Z >= 180.f && FVector::Dist2D(End, Feet) <= 220.f)
	{
		Line.bHasLos = false; // under the platform
	}
	else
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(SpitterLine), false, this);
		Params.AddIgnoredActor(Target);
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			Params.AddIgnoredActor(*It); // Godot mask: walls and barricades only
		}
		FHitResult Hit;
		EShotLineHit Kind = EShotLineHit::Clear;
		if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params) && Hit.GetActor() && !Hit.GetActor()->IsA<AOperativeCharacter>())
		{
			Kind = Hit.GetActor()->IsA<ABarricadeActor>() ? EShotLineHit::Barricade : EShotLineHit::Blocked;
		}
		Line = EnemyAIRules::JudgeSpitterLine(Kind, Target->GetStance(), AIConfig.CrouchCoverReduction);
	}
	const float Distance = FVector::Dist(Feet, GodotPosition(Target));
	FRotator LookRot = (Target->GetActorLocation() - GetActorLocation()).Rotation();
	SetActorRotation(FMath::RInterpTo(GetActorRotation(), FRotator(0.f, LookRot.Yaw, 0.f), DeltaTime, 8.0f));
	switch (EnemyAIRules::SpitterMove(Line.bHasLos, Distance, AIConfig.SpitterPreferredRange))
	{
	case ESpitterMove::Approach:
		if (AIC)
		{
			AIC->MoveToActor(Target, AIConfig.SpitterPreferredRange * 0.5f);
		}
		break;
	case ESpitterMove::Retreat:
		if (AIC)
		{
			const FVector Back = (GetActorLocation() - Target->GetActorLocation()).GetSafeNormal2D();
			AIC->MoveToLocation(GetActorLocation() + Back * 300.f, 50.f, false, true);
		}
		break;
	default:
		if (AIC)
		{
			AIC->StopMovement();
		}
		break;
	}
	// Fires only with a clear line (Godot _shoot_at_target: crit, cover, red tracer).
	if (Line.bHasLos && Distance <= AttackRange && AttackTimer <= 0.f)
	{
		AttackTimer = AttackCooldown;
		const bool bIsCrit = FMath::FRand() < CritChance;
		Target->TakeHit(AttackDamage * (bIsCrit ? CritMultiplier : 1.f) * Line.Cover, EnemyDisplayName, bIsCrit, false, this);
		if (UCombatFeedbackSubsystem* Feedback = World->GetSubsystem<UCombatFeedbackSubsystem>())
		{
			Feedback->SpawnTracer(Feet + FVector(0.f, 0.f, 90.f), End, FLinearColor(1.f, 0.2f, 0.2f));
		}
	}
}

void AEnemyCharacter::HandleDamaged(const FDamageSpec& Spec, float FinalDamage)
{
	if (!Spec.AttackerSource.IsEmpty())
	{
		LastAttackerSource = Spec.AttackerSource;
	}
}

AActor* AEnemyCharacter::FindClosestSquadMember() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
	if (!Squad)
	{
		return nullptr;
	}

	AActor* Closest = nullptr;
	float MinDistSq = TNumericLimits<float>::Max();
	const FVector MyLoc = GetActorLocation();

	for (AOperativeCharacter* Member : Squad->GetMembers())
	{
		if (!Member || !Member->HealthComponent || !Member->HealthComponent->IsAlive())
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(MyLoc, Member->GetActorLocation());
		if (DistSq < MinDistSq)
		{
			MinDistSq = DistSq;
			Closest = Member;
		}
	}

	return Closest;
}

void AEnemyCharacter::AttackTarget(AActor* Target)
{
	if (!Target || bIsDying)
	{
		return;
	}

	AttackTimer = AttackCooldown;

	const bool bIsCrit = (FMath::FRand() < CritChance);
	const float FinalDamage = AttackDamage * (bIsCrit ? CritMultiplier : 1.0f);

	FDamageSpec Spec;
	Spec.Amount = FinalDamage;
	Spec.DamageType = EDamageType::Kinetic;
	Spec.AttackerSource = EnemyDisplayName;

	// Godot _attack_target -> player.gd take_damage (dodge, stance, fortitude) for operatives.
	if (AOperativeCharacter* Operative = Cast<AOperativeCharacter>(Target))
	{
		Operative->TakeHit(FinalDamage, EnemyDisplayName, bIsCrit, false, this);
	}
	else if (UHealthComponent* TargetHealth = Target->FindComponentByClass<UHealthComponent>())
	{
		TargetHealth->ApplyDamage(Spec, this);
	}
}

void AEnemyCharacter::HandleDied(AActor* Victim, const FString& AttackerSource)
{
	if (bIsDying)
	{
		return;
	}

	bIsDying = true;
	Tags.Remove(FName(TEXT("Enemy")));

	// Godot enemy_cutter.gd _start_airborne_death: shot down mid-leap it keeps flying and crashes on the ground.
	if (Archetype == EEnemyArchetype::Cutter && (IsJumpAttacking() || GetCharacterMovement()->IsFalling()))
	{
		bAirborneDeath = true;
		JumpPhase = ECutterJumpPhase::None;
		GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Ignore);
		GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
		UFloatingTextSubsystem::SpawnAboveEnemy(this, TEXT("💀 СБИТ В ВОЗДУХЕ"), FLinearColor(1.f, 0.25f, 0.25f));
	}
	else
	{
		GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (AController* C = GetController())
	{
		C->StopMovement();
	}

	// Godot enemy_base.gd / enemy_cutter.gd death: every squad member gets the kill EXP.
	if (USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr)
	{
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->AddExp(KillExpReward);
		}
	}

	OnEnemyDied.Broadcast(this);
	OnEnemyDiedNative.Broadcast(this);

	SetLifeSpan(2.0f);
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
		Status += TEXT(" ГОРИТ");
	}
	if (HealthComponent->HasStatusEffect(EStatusEffect::Frozen))
	{
		Status += TEXT(" ЛЁД");
	}
	if (HealthComponent->HasStatusEffect(EStatusEffect::Stagger))
	{
		Status += TEXT(" ОГЛУШЁН");
	}
	if (HealthComponent->HasStatusEffect(EStatusEffect::ArmorShred))
	{
		Status += TEXT(" БРОНЯ-");
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

bool AEnemyCharacter::StartJumpAttack(AActor* Target)
{
	if (bIsDying || IsJumpAttacking() || !IsValid(Target))
	{
		return false;
	}
	JumpTarget = Target;
	JumpPhase = ECutterJumpPhase::Windup;
	JumpPhaseTimer = 0.f;
	bJumpDamageDealt = false;
	if (AAIController* AIC = Cast<AAIController>(GetController()))
	{
		AIC->StopMovement();
	}
	OnJumpAttackStarted();
	return true;
}

void AEnemyCharacter::TickJumpAttack(float DeltaTime)
{
	JumpPhaseTimer += DeltaTime;
	AActor* Target = JumpTarget.Get();
	switch (JumpPhase)
	{
	case ECutterJumpPhase::Windup:
	{
		if (Target)
		{
			const FRotator Look = (Target->GetActorLocation() - GetActorLocation()).Rotation();
			SetActorRotation(FMath::RInterpTo(GetActorRotation(), FRotator(0.f, Look.Yaw, 0.f), DeltaTime, 24.f));
		}
		if (JumpPhaseTimer < 0.4f / JumpAttackSpeed) // Godot JUMP_ORIG_WINDUP_TIME
		{
			return;
		}
		// Godot _launch_jump_flight: a ballistic arc landing on the target within 1.3 s / speed, at most JumpMaxDistance.
		JumpPhase = ECutterJumpPhase::Airborne;
		JumpPhaseTimer = 0.f;
		JumpFlightDuration = 1.3f / JumpAttackSpeed;
		const FVector Start = GetActorLocation();
		FVector Landing = Target ? GodotPosition(Target) + FVector(0.f, 0.f, GetSimpleCollisionHalfHeight()) : Start + GetActorForwardVector() * 500.f;
		FVector Horizontal(Landing.X - Start.X, Landing.Y - Start.Y, 0.f);
		if (Horizontal.Size() > JumpMaxDistance)
		{
			Horizontal = Horizontal.GetSafeNormal() * JumpMaxDistance;
		}
		const float Gravity = -GetCharacterMovement()->GetGravityZ();
		const float DeltaZ = Landing.Z - Start.Z;
		const FVector Velocity = Horizontal / FMath::Max(0.05f, JumpFlightDuration)
			+ FVector(0.f, 0.f, (DeltaZ + 0.5f * Gravity * JumpFlightDuration * JumpFlightDuration) / FMath::Max(0.05f, JumpFlightDuration));
		LaunchCharacter(Velocity, true, true);
		return;
	}
	case ECutterJumpPhase::Airborne:
		// Impact at the end of the flight, or on touching the ground in its second half.
		if (JumpPhaseTimer >= JumpFlightDuration || (JumpPhaseTimer >= JumpFlightDuration * 0.7f && !GetCharacterMovement()->IsFalling()))
		{
			JumpPhase = ECutterJumpPhase::Impact;
			JumpPhaseTimer = 0.f;
			GetCharacterMovement()->StopMovementImmediately();
			if (!bJumpDamageDealt)
			{
				bJumpDamageDealt = true;
				ApplyJumpImpactDamage();
			}
			OnJumpAttackImpact();
		}
		return;
	case ECutterJumpPhase::Impact:
		if (JumpPhaseTimer >= 0.75f / JumpAttackSpeed) // Godot JUMP_ORIG_RECOVERY_TIME
		{
			JumpPhase = ECutterJumpPhase::None;
			JumpPhaseTimer = 0.f;
			JumpCooldownTimer = JumpCooldown;
			AttackTimer = AttackCooldown;
		}
		return;
	default:
		return;
	}
}

void AEnemyCharacter::ApplyJumpImpactDamage()
{
	UWorld* World = GetWorld();
	const bool bIsCrit = FMath::FRand() < CritChance;
	const float Damage = JumpAttackDamage * (bIsCrit ? CritMultiplier : 1.f);
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetSimpleCollisionHalfHeight());
	bool bHitAny = false;
	if (const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>())
	{
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			const FVector Position = GodotPosition(Member);
			if (FVector::Dist(Feet, Position) <= JumpDamageRadius && FMath::Abs(Feet.Z - Position.Z) <= 150.f)
			{
				Member->TakeHit(Damage, EnemyDisplayName, bIsCrit, false, this);
				bHitAny = true;
			}
		}
	}
	for (TActorIterator<ABarricadeActor> It(World); It; ++It)
	{
		UHealthComponent* BarricadeHealth = It->FindComponentByClass<UHealthComponent>();
		if (BarricadeHealth && BarricadeHealth->IsAlive() && FVector::Dist(Feet, GodotPosition(*It)) <= JumpDamageRadius)
		{
			BarricadeHealth->ApplyDirectHealthLoss(Damage, EnemyDisplayName);
			bHitAny = true;
		}
	}
	if (bHitAny)
	{
		UFloatingTextSubsystem::SpawnAboveEnemy(this, FString::Printf(TEXT("💥 НАЛЁТ %d"), FMath::FloorToInt(Damage)), FLinearColor(1.f, 0.35f, 0.1f));
	}
}

void AEnemyCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);
	if (bAirborneDeath)
	{
		// Godot _process_airborne_death: crash landing.
		bAirborneDeath = false;
		GetCharacterMovement()->StopMovementImmediately();
		UFloatingTextSubsystem::SpawnAboveEnemy(this, TEXT("💥 КРАХ"), FLinearColor(0.9f, 0.5f, 0.2f));
	}
}
