#include "Interactables/TurretActor.h"
#include "UI/OverheadLabel.h"
#include "UI/FloatingTextSubsystem.h"
#include "Combat/CombatFeedbackSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Combat/HealthComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Interactables/BarricadeActor.h"
#include "Interactables/HeatSourceComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "TurretActor"

namespace
{
	/** Muzzle and aim heights, cm (Godot +0.7 m / +0.8 m). */
	constexpr float MuzzleHeight = 70.f;
	constexpr float AimHeight = 80.f;
	/** Godot generic heat source radius 4.5 m (+0.5 m tolerance). */
	constexpr float TurretWarmthRadius = 450.f;
}

ATurretActor::ATurretActor()
{
	PrimaryActorTick.bCanEverTick = true;
	DeployableType = EDeployableType::Turret;
	DisplayName = LOCTEXT("Name", "Автоматическая турель");
	TrapDamage = 90.f; // Godot turret.gd trap_damage

	// Godot ghost: cylinder r 0.4-0.5 m, 0.8 m high.
	Box->SetBoxExtent(FVector(50.f, 50.f, 40.f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BaseMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (CylinderMesh.Succeeded())
	{
		Mesh->SetStaticMesh(CylinderMesh.Object);
	}
	Mesh->SetRelativeScale3D(FVector(1.f, 1.f, 0.6f));
	Mesh->SetRelativeLocation(FVector(0.f, 0.f, -10.f));

	Head = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Head"));
	Head->SetupAttachment(Box);
	Head->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Head->SetRelativeLocation(FVector(0.f, 0.f, 30.f));
	Head->SetRelativeScale3D(FVector(0.7f, 0.35f, 0.25f));
	if (CubeMesh.Succeeded())
	{
		Head->SetStaticMesh(CubeMesh.Object);
	}
	if (BaseMaterial.Succeeded())
	{
		Mesh->SetMaterial(0, BaseMaterial.Object);
		Head->SetMaterial(0, BaseMaterial.Object);
	}

	Health = CreateDefaultSubobject<UHealthComponent>(TEXT("Health"));
	Health->MaxHealth = 120.f;

	HeatSource->Radius = TurretWarmthRadius;
	HeatSource->bHeatActive = true;
}

void ATurretActor::BeginPlay()
{
	Super::BeginPlay();
	Health->OnDied.AddDynamic(this, &ATurretActor::HandleBroken);
	UpdateState();
}

void ATurretActor::UpdateState()
{
	HeatSource->SetHeatActive(bPowered && !bBroken);
	// Placeholder tint: green working, amber unpowered, red broken (Godot overhead label colours).
	const FColor Tint = bBroken ? FColor(255, 64, 64) : (bPowered ? FColor(51, 230, 102) : FColor(255, 191, 51));
	if (UMaterialInstanceDynamic* Material = Head->CreateAndSetMaterialInstanceDynamic(0))
	{
		Material->SetVectorParameterValue(TEXT("Color"), FLinearColor::FromSRGBColor(Tint));
	}
}

void ATurretActor::SetPowered(bool bNewPowered)
{
	bPowered = bNewPowered;
	UpdateState();
}

void ATurretActor::SetAllPowered(UWorld* World, bool bNewPowered)
{
	for (TActorIterator<ATurretActor> It(World); It; ++It)
	{
		It->SetPowered(bNewPowered);
	}
}

void ATurretActor::HandleBroken(AActor* Victim, const FString& AttackerSource)
{
	bBroken = true;
	UpdateState();
}

void ATurretActor::Repair()
{
	Health->SetMaxHealth(Health->GetMaxHealth(), true);
	bBroken = false;
	UpdateState();
}

float ATurretActor::GetRepairSeconds(const AOperativeCharacter* Operative) const
{
	return Operative && Operative->SquadRole == EOperativeRole::Engineer ? 2.f : 4.f;
}

void ATurretActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bTrapped && !bBroken)
	{
		for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		{
			if (!It->ActorHasTag(FName(TEXT("Enemy"))))
			{
				continue;
			}
			const UHealthComponent* EnemyHealth = It->FindComponentByClass<UHealthComponent>();
			if (EnemyHealth && EnemyHealth->IsAlive() && FVector::Dist(GetActorLocation(), It->GetActorLocation()) <= TrapContactDistance)
			{
				DetonateTrap(false, LOCTEXT("EnemyContact", "Контакт с противником"));
				break;
			}
		}
	}
	if (ShotCooldown > 0.f)
	{
		ShotCooldown -= DeltaSeconds;
	}
	if (!bPowered || bBroken)
	{
		return;
	}
	float Cover = 1.f;
	FVector Aim = FVector::ZeroVector;
	if (AActor* Target = FindTarget(Cover, Aim))
	{
		const FRotator Facing = (Target->GetActorLocation() - GetActorLocation()).Rotation();
		Head->SetWorldRotation(FRotator(0.f, Facing.Yaw, 0.f));
		if (ShotCooldown <= 0.f)
		{
			ShotCooldown = FireInterval;
			Fire(Target, Cover, Aim);
		}
	}
}

AActor* ATurretActor::FindTarget(float& OutCover, FVector& OutAim) const
{
	const FVector Start = GetActorLocation() + FVector(0.f, 0.f, MuzzleHeight - Box->GetScaledBoxExtent().Z);
	float Best = AttackRange;
	AActor* BestTarget = nullptr;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* Enemy = *It;
		if (!Enemy->ActorHasTag(FName(TEXT("Enemy"))))
		{
			continue;
		}
		const UHealthComponent* EnemyHealth = Enemy->FindComponentByClass<UHealthComponent>();
		const float Distance = FVector::Dist(GetActorLocation(), Enemy->GetActorLocation());
		if (!EnemyHealth || !EnemyHealth->IsAlive() || Distance > Best)
		{
			continue;
		}
		// Godot: line of fire; a barricade in between is cover (60 %), anything else (walls) blocks.
		const FVector End = Enemy->GetActorLocation() + FVector(0.f, 0.f, AimHeight - Enemy->GetSimpleCollisionHalfHeight());
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TurretLineOfFire), false, this);
		float Cover = 1.f;
		if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params) && Hit.GetActor() != Enemy)
		{
			if (!Cast<ABarricadeActor>(Hit.GetActor()))
			{
				continue;
			}
			Cover = BarricadeCoverMultiplier;
		}
		Best = Distance;
		BestTarget = Enemy;
		OutCover = Cover;
		OutAim = End;
	}
	return BestTarget;
}

void ATurretActor::Fire(AActor* Target, float Cover, const FVector& Aim)
{
	if (UHealthComponent* TargetHealth = Target->FindComponentByClass<UHealthComponent>())
	{
		FDamageSpec Spec;
		Spec.Amount = Damage * Cover;
		Spec.DamageType = EDamageType::Kinetic;
		Spec.ArmorPenetration = 0.20f;
		Spec.AttackerSource = TEXT("Турель");
		TargetHealth->TakeDamage(Spec);
	}
	// Godot turret.gd _spawn_muzzle_tracer: green tracer from 0.7 m above the turret, short flash.
	if (UCombatFeedbackSubsystem* Feedback = GetWorld() ? GetWorld()->GetSubsystem<UCombatFeedbackSubsystem>() : nullptr)
	{
		Feedback->SpawnTurretTracer(GetActorLocation() + FVector(0.f, 0.f, 70.f), Aim);
	}
	OnFired.Broadcast(this, Target, Aim);
	ReceiveFired(Target, Aim);
}

void ATurretActor::ExecuteAction(AOperativeCharacter* User)
{
	if (!User)
	{
		return;
	}
	// Godot _on_action_confirmed 1.5: a broken / damaged turret is repaired before anything else.
	if (bBroken || Health->GetCurrentHealth() < Health->GetMaxHealth())
	{
		const float Seconds = GetRepairSeconds(User);
		const FRotator Facing = (GetActorLocation() - User->GetActorLocation()).Rotation();
		User->StopOperative();
		User->SetActorRotation(FRotator(0.f, Facing.Yaw, 0.f));
		User->SetStance(EOperativeStance::Crouching);
		UFloatingTextSubsystem::SpawnAboveOperative(User, TEXT("🔧 РЕМОНТ ТУРЕЛИ..."), FLinearColor(0.2f, 0.9f, 0.4f));
		PostLine(User->DisplayName, FText::Format(LOCTEXT("Repairing", "🔧 {0}: «Чистим контакты и восстанавливаем сервоприводы турели ({1}с)...»"),
			User->DisplayName, FText::AsNumber(Seconds, &FNumberFormattingOptions().SetMinimumFractionalDigits(1).SetMaximumFractionalDigits(1))));
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateUObject(this, &ATurretActor::FinishRepair,
			TWeakObjectPtr<AOperativeCharacter>(User)), Seconds, false);
		return;
	}
	Super::ExecuteAction(User);
}

FActionMenuRequest ATurretActor::BuildActionMenu(const AOperativeCharacter* Leader) const
{
	FActionMenuRequest Request = Super::BuildActionMenu(Leader);
	if (Request.bOpenMenu && !bBroken && !bPowered && Health->GetCurrentHealth() >= Health->GetMaxHealth())
	{
		Request.Menu.CancelText = LOCTEXT("Close", "Закрыть");
	}
	return Request;
}

void ATurretActor::FinishRepair(TWeakObjectPtr<AOperativeCharacter> WeakUser)
{
	Repair();
	PostLine(WeakUser.IsValid() ? WeakUser->DisplayName : LOCTEXT("Soldier", "Боец"),
		LOCTEXT("Repaired", "✅ Турель полностью отремонтирована и готова к бою!"));
}

void ATurretActor::DetonateTrap(bool bByShot, const FText& InstigatorName)
{
	if (!bTrapped && !bByShot)
	{
		return;
	}
	bTrapped = false;
	PostLine(bByShot ? (InstigatorName.IsEmpty() ? LOCTEXT("Sniper", "Снайпер") : InstigatorName) : LOCTEXT("Blast", "ВЗРЫВ"),
		bByShot ? LOCTEXT("ShotBoom", "💥 Взрыв ловушки на турели от точного выстрела!") : LOCTEXT("TrapBoom", "💥 Растяжка на турели сдетонировала!"));
	ApplyBlast(TrapDamage, TrapDamage * DeployableRules::SquadDamageScale, TrapRadius, 0.45f, EDamageType::Explosive,
		LOCTEXT("Source", "Ловушка турели"), LOCTEXT("SquadHit", "💥 Задело взрывом растяжки турели (-{0} HP)!"));
	// The charge sits on the turret (Godot: max(trap_damage * 1.3, 110)).
	Health->ApplyDirectHealthLoss(FMath::Max(TrapDamage * 1.3f, 110.f), LOCTEXT("Source", "Ловушка турели").ToString());
}

void ATurretActor::DescribeForMenu(const AOperativeCharacter* Leader, FText& OutTitle, FText& OutDescription, FText& OutConfirm,
	bool& bOutDisabled) const
{
	int32 Count = 0;
	int32 Max = 0;
	GetLeaderSupply(Leader, Count, Max);
	const bool bFull = Count >= Max;
	const FText LeaderName = Leader ? Leader->DisplayName : LOCTEXT("Soldier", "Боец");
	const int32 Current = FMath::FloorToInt(Health->GetCurrentHealth());
	const int32 MaxHp = FMath::FloorToInt(Health->GetMaxHealth());
	bOutDisabled = false;

	if (bBroken || Current < MaxHp)
	{
		const bool bEngineer = Leader && Leader->SquadRole == EOperativeRole::Engineer;
		OutTitle = bBroken ? LOCTEXT("TitleBroken", "🎯 Автоматическая турель [СЛОМАНА]") : LOCTEXT("TitleDamaged", "🎯 Автоматическая турель [ПОВРЕЖДЕНА]");
		FText State = FText::GetEmpty();
		if (bBroken)
		{
			State = LOCTEXT("StateBroken", "\nСИСТЕМА СЛОМАНА: Огонь не ведётся, требуется ремонт.");
		}
		else if (!bPowered)
		{
			State = LOCTEXT("StateUnpowered", "\nВНИМАНИЕ: Турель обесточена (генератор отключен).");
		}
		OutDescription = FText::Format(LOCTEXT("DescRepair", "⚠️ Автоматическая турель повреждена ({0}/{1} HP)!{2}\nИсполнитель: {3} ({4}, ремонт: {5}с)."),
			Current, MaxHp, State, LeaderName,
			bEngineer ? LOCTEXT("Engineer", "🛠️ Инженер (в 2 раза быстрее)") : LOCTEXT("Regular", "Обычный боец"),
			FText::AsNumber(GetRepairSeconds(Leader), &FNumberFormattingOptions().SetMinimumFractionalDigits(1).SetMaximumFractionalDigits(1)));
		OutConfirm = LOCTEXT("Repair", "🔧 Починить");
		return;
	}
	if (!bPowered)
	{
		OutTitle = LOCTEXT("TitleUnpowered", "🎯 Автоматическая турель [ОБЕСТОЧЕНА]");
		OutDescription = FText::Format(LOCTEXT("DescUnpowered", "Автоматическая турель исправна ({0}/{1} HP), но обесточена!\nДля работы турели запустите или почините резервный генератор."),
			Current, MaxHp);
		OutConfirm = LOCTEXT("Unpowered", "⚡ Обесточена");
		bOutDisabled = true;
		return;
	}
	if (bTrapped)
	{
		OutTitle = LOCTEXT("TitleTrapped", "🎯 Автоматическая турель [ЗАМИНИРОВАНА]");
		OutDescription = FText::Format(LOCTEXT("DescTrapped", "⚠️ ВНИМАНИЕ: Турель заминирована взрывной растяжкой!\n{0}\n(У {1} турелей: {2}/{3})."),
			DescribeDefusal(Leader), LeaderName, Count, Max);
		OutConfirm = LOCTEXT("Defuse", "Разминировать");
		return;
	}
	OutTitle = LOCTEXT("Title", "🎯 Автоматическая турель");
	const FText HealthInfo = FText::Format(LOCTEXT("HealthInfo", " (HP: {0}/{1}, Урон: {2})"), Current, MaxHp, FMath::FloorToInt(Damage));
	if (bDeployable)
	{
		OutConfirm = bFull ? FText::Format(LOCTEXT("Full", "Инвентарь полон ({0}/{1})"), Count, Max) : LOCTEXT("PickUp", "Подобрать");
		OutDescription = FText::Format(LOCTEXT("DescPickUp", "Подобрать стационарную турель{0}?\nОбъект будет добавлен в личный запас (У {1}: {2}/{3})."),
			HealthInfo, LeaderName, Count, Max);
		bOutDisabled = bFull;
	}
	else
	{
		OutConfirm = LOCTEXT("CannotPickUp", "Нельзя подобрать");
		OutDescription = FText::Format(LOCTEXT("DescFixed", "Стационарная турель{0}. Параметр deployable отключён: объект нельзя убрать в инвентарь."), HealthInfo);
		bOutDisabled = true;
	}
}

#undef LOCTEXT_NAMESPACE

bool ATurretActor::GetOverheadLabel(FOverheadLabel& OutLabel) const
{
	const UHealthComponent* TurretHealth = FindComponentByClass<UHealthComponent>();
	const float Max = TurretHealth ? TurretHealth->GetMaxHealth() : 0.f;
	const float Current = TurretHealth ? TurretHealth->GetCurrentHealth() : 0.f;
	const TCHAR* Trap = bTrapped ? TEXT(" [⚠️ ЛОВУШКА]") : TEXT("");
	OutLabel.HeightCm = 160.f;
	if (bBroken || Current <= 0.f)
	{
		OutLabel.Text = FString::Printf(TEXT("⚠️ Турель%s: СЛОМАНА [0/%d HP]\n(Нужен ремонт)"), Trap, FMath::FloorToInt(Max));
		OutLabel.Color = FLinearColor(1.f, 0.25f, 0.25f);
	}
	else if (!bPowered)
	{
		OutLabel.Text = FString::Printf(TEXT("⚡ Турель%s: ОБЕСТОЧЕНА [%d/%d HP]\n(Запустите генератор)"), Trap, FMath::FloorToInt(Current), FMath::FloorToInt(Max));
		OutLabel.Color = FLinearColor(1.f, 0.75f, 0.2f);
	}
	else
	{
		OutLabel.Text = FString::Printf(TEXT("🎯 Турель%s: %d/%d HP"), Trap, FMath::FloorToInt(Current), FMath::FloorToInt(Max));
		OutLabel.Color = FLinearColor(0.2f, 0.9f, 0.4f);
	}
	return true;
}
