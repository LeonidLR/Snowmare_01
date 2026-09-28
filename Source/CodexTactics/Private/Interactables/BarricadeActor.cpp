#include "Interactables/BarricadeActor.h"
#include "Characters/OperativeCharacter.h"
#include "Combat/HealthComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "BarricadeActor"

ABarricadeActor::ABarricadeActor()
{
	PrimaryActorTick.bCanEverTick = true;
	DeployableType = EDeployableType::Barricade;
	DisplayName = LOCTEXT("Name", "Тактическая баррикада");

	// Godot box 3 x 1 x 0.6 m (X along the wall).
	Box->SetBoxExtent(FVector(150.f, 30.f, 50.f));
	Mesh->SetRelativeScale3D(FVector(3.f, 0.6f, 1.f)); // also on the class default: placement ghosts copy it
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BaseMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (BaseMaterial.Succeeded())
	{
		Mesh->SetMaterial(0, BaseMaterial.Object);
	}

	Health = CreateDefaultSubobject<UHealthComponent>(TEXT("Health"));
	Health->MaxHealth = 200.f;
}

void ABarricadeActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (UMaterialInstanceDynamic* Material = Mesh->CreateAndSetMaterialInstanceDynamic(0))
	{
		Material->SetVectorParameterValue(TEXT("Color"), BodyColor);
	}
}

void ABarricadeActor::BeginPlay()
{
	Super::BeginPlay();
	Health->OnDied.AddDynamic(this, &ABarricadeActor::HandleDestroyed);
}

void ABarricadeActor::HandleDestroyed(AActor* Victim, const FString& AttackerSource)
{
	Destroy();
}

void ABarricadeActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bTrapped)
	{
		return;
	}
	// Godot _check_enemy_trap_contact: any living enemy within 1.8 m sets the wire off.
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
			return;
		}
	}
}

void ABarricadeActor::DetonateTrap(bool bByShot, const FText& InstigatorName)
{
	if (!bTrapped && !bByShot)
	{
		return;
	}
	bTrapped = false;
	PostLine(bByShot ? (InstigatorName.IsEmpty() ? LOCTEXT("Sniper", "Снайпер") : InstigatorName) : LOCTEXT("Blast", "ВЗРЫВ"),
		bByShot ? LOCTEXT("ShotBoom", "💥 Взрыв ловушки на баррикаде от меткого выстрела!") : LOCTEXT("TrapBoom", "💥 Растяжка на баррикаде сдетонировала!"));
	ApplyBlast(TrapDamage, TrapDamage * DeployableRules::SquadDamageScale, TrapRadius, 0.45f, EDamageType::Explosive,
		LOCTEXT("Source", "Ловушка баррикады"), LOCTEXT("SquadHit", "💥 Задело взрывом растяжки баррикады (-{0} HP)!"));
	// The charge sits on the barricade itself (Godot: max(trap_damage * 1.4, 130)).
	Health->ApplyDirectHealthLoss(FMath::Max(TrapDamage * 1.4f, 130.f), LOCTEXT("Source", "Ловушка баррикады").ToString());
}

void ABarricadeActor::DescribeForMenu(const AOperativeCharacter* Leader, FText& OutTitle, FText& OutDescription, FText& OutConfirm,
	bool& bOutDisabled) const
{
	int32 Count = 0;
	int32 Max = 0;
	GetLeaderSupply(Leader, Count, Max);
	const bool bFull = Count >= Max;
	const FText LeaderName = Leader ? Leader->DisplayName : LOCTEXT("Soldier", "Боец");
	const FText HealthInfo = FText::Format(LOCTEXT("HealthInfo", " (HP: {0}/{1})"), FMath::FloorToInt(Health->GetCurrentHealth()),
		FMath::FloorToInt(Health->GetMaxHealth()));

	OutTitle = bTrapped ? LOCTEXT("TitleTrapped", "🧱 Тактическая баррикада [ЗАМИНИРОВАНА]") : LOCTEXT("Title", "🧱 Тактическая баррикада");
	OutConfirm = bTrapped ? LOCTEXT("Defuse", "Разминировать") : (bDeployable ? LOCTEXT("PickUp", "Подобрать") : LOCTEXT("CannotPickUp", "Нельзя подобрать"));
	if (bFull && bDeployable)
	{
		OutConfirm = FText::Format(LOCTEXT("Full", "Инвентарь полон ({0}/{1})"), Count, Max);
	}
	if (bTrapped)
	{
		OutDescription = FText::Format(LOCTEXT("DescTrapped", "⚠️ ВНИМАНИЕ: Баррикада заминирована взрывной растяжкой!\n{0}\n(У {1} баррикад: {2}/{3})."),
			DescribeDefusal(Leader), LeaderName, Count, Max);
	}
	else if (bDeployable)
	{
		OutDescription = FText::Format(LOCTEXT("DescPickUp", "Разобрать защитную бронебаррикаду{0}?\nОбъект будет добавлен в личный запас (У {1}: {2}/{3})."),
			HealthInfo, LeaderName, Count, Max);
	}
	else
	{
		OutDescription = FText::Format(LOCTEXT("DescFixed", "Стационарная баррикада{0}. Параметр deployable отключён: объект нельзя убрать в инвентарь."),
			HealthInfo);
	}
	bOutDisabled = (bFull && bDeployable) || (!bDeployable && !bTrapped);
}

#undef LOCTEXT_NAMESPACE
