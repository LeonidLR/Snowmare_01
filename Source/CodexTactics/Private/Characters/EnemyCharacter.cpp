#include "Characters/EnemyCharacter.h"
#include "Characters/EnemyAIController.h"
#include "Characters/OperativeCharacter.h"
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
	case EEnemyArchetype::FrostHound:
	case EEnemyArchetype::Cutter:
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

	// Godot enemies re-read their stats from game_balance_config.tres (imported DA_GameBalanceConfig).
	if (const ACodexTacticsGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ACodexTacticsGameMode>() : nullptr)
	{
		if (const UGodotBalanceAsset* Config = GameMode->GameBalanceConfig.LoadSynchronous())
		{
			ApplyBalance(*Config);
		}
	}
}

void AEnemyCharacter::ApplyWaveModifiers(float HpMult, float DamageMult, float SpeedMult, float CustomHealth)
{
	const float BaseHealth = CustomHealth > 0.f ? CustomHealth : HealthComponent->GetMaxHealth();
	HealthComponent->SetMaxHealth(BaseHealth * HpMult, /*bResetCurrent*/ true);
	AttackDamage *= DamageMult;
	GetCharacterMovement()->MaxWalkSpeed *= SpeedMult;
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

	if (bIsDying)
	{
		return;
	}

	if (AttackTimer > 0.0f)
	{
		AttackTimer -= DeltaTime;
	}

	AActor* Target = FindClosestSquadMember();
	if (!Target)
	{
		return;
	}

	const float Dist = FVector::Dist2D(GetActorLocation(), Target->GetActorLocation());
	if (Dist <= AttackRange)
	{
		if (AController* C = GetController())
		{
			C->StopMovement();
		}

		FRotator LookRot = (Target->GetActorLocation() - GetActorLocation()).Rotation();
		LookRot.Pitch = 0.f;
		LookRot.Roll = 0.f;
		SetActorRotation(FMath::RInterpTo(GetActorRotation(), LookRot, DeltaTime, 10.0f));

		if (AttackTimer <= 0.0f)
		{
			AttackTarget(Target);
		}
	}
	else
	{
		if (AAIController* AIC = Cast<AAIController>(GetController()))
		{
			AIC->MoveToActor(Target, AttackRange * 0.75f);
		}
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

	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (AController* C = GetController())
	{
		C->StopMovement();
	}

	OnEnemyDied.Broadcast(this);
	OnEnemyDiedNative.Broadcast(this);

	SetLifeSpan(2.0f);
}
