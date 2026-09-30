#include "Interactables/BarricadeActor.h"
#include "UI/OverheadLabel.h"
#include "Characters/OperativeCharacter.h"
#include "Combat/HealthComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Interactables/VaultNavigation.h"
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
	if (bVaultable)
	{
		VaultNavigation::MakeVaultable(this);
	}
}

void ABarricadeActor::HandleDestroyed(AActor* Victim, const FString& AttackerSource)
{
	Destroy();
}

void ABarricadeActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Godot _tick_contact_area_damage: every contact_tick_interval, enemies within 2.2 m take half the contact damage.
	if (ContactType != EBarricadeContact::None && ContactDamage > 0.f && (ContactTimer -= DeltaSeconds) <= 0.f)
	{
		ContactTimer = ContactTickInterval;
		TArray<AActor*> Touching;
		for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		{
			const UHealthComponent* EnemyHealth = It->ActorHasTag(FName(TEXT("Enemy"))) ? It->FindComponentByClass<UHealthComponent>() : nullptr;
			if (EnemyHealth && EnemyHealth->IsAlive() && FVector::Dist(GetActorLocation(), It->GetActorLocation()) <= 220.f)
			{
				Touching.Add(*It);
			}
		}
		for (AActor* Enemy : Touching)
		{
			ApplyContactTo(Enemy, ContactDamage * 0.5f);
		}
	}
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

void ABarricadeActor::RetaliateAgainst(AActor* Attacker)
{
	if (!bTrapped && ContactType != EBarricadeContact::None && ContactDamage > 0.f)
	{
		ApplyContactTo(Attacker, ContactDamage);
	}
}

void ABarricadeActor::ApplyContactTo(AActor* Enemy, float Damage)
{
	UHealthComponent* EnemyHealth = IsValid(Enemy) ? Enemy->FindComponentByClass<UHealthComponent>() : nullptr;
	if (!EnemyHealth || !EnemyHealth->IsAlive())
	{
		return;
	}
	// Godot _apply_contact_effect_to_enemy: take_damage(dmg, type, 0.15 armor pen, «Баррикада», status...).
	FDamageSpec Spec;
	Spec.Amount = Damage;
	Spec.ArmorPenetration = 0.15f;
	Spec.AttackerSource = TEXT("Баррикада");
	switch (ContactType)
	{
	case EBarricadeContact::Physical:
		Spec.DamageType = EDamageType::Melee;
		break;
	case EBarricadeContact::Fire:
		Spec.DamageType = EDamageType::Fire;
		Spec.StatusEffect = EStatusEffect::Burning;
		Spec.StatusDuration = 3.f;
		Spec.StatusTickDamage = FMath::Max(1.f, Damage * 0.25f);
		break;
	case EBarricadeContact::Cryo:
		Spec.DamageType = EDamageType::Cryo;
		Spec.StatusEffect = EStatusEffect::Frozen;
		Spec.StatusDuration = 2.5f;
		break;
	case EBarricadeContact::Energy:
		Spec.DamageType = EDamageType::Energy;
		Spec.StatusEffect = EStatusEffect::Stagger;
		Spec.StatusDuration = 1.f;
		break;
	default:
		return;
	}
	EnemyHealth->TakeDamage(Spec);
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

bool ABarricadeActor::GetOverheadLabel(FOverheadLabel& OutLabel) const
{
	const UHealthComponent* BarricadeHealth = FindComponentByClass<UHealthComponent>();
	if (!BarricadeHealth || !BarricadeHealth->IsAlive())
	{
		return false;
	}
	// Godot type icon 🗡️ / 🔥 / ❄️ / ⚡ (the HUD font has no emoji: a word).
	const TCHAR* Contact = ContactDamage <= 0.f ? TEXT("")
		: ContactType == EBarricadeContact::Physical ? TEXT(" [шипы]")
		: ContactType == EBarricadeContact::Fire ? TEXT(" [огонь]")
		: ContactType == EBarricadeContact::Cryo ? TEXT(" [холод]")
		: ContactType == EBarricadeContact::Energy ? TEXT(" [ток]") : TEXT("");
	OutLabel.Text = FString::Printf(TEXT("🧱 Баррикада%s%s: %d/%d"), Contact, bTrapped ? TEXT(" [⚠️ ЛОВУШКА]") : TEXT(""),
		FMath::FloorToInt(FMath::Max(0.f, BarricadeHealth->GetCurrentHealth())), FMath::FloorToInt(BarricadeHealth->GetMaxHealth()));
	OutLabel.Color = FLinearColor(0.9f, 0.75f, 0.3f);
	OutLabel.HeightCm = 135.f; // obstacle 1 m + 0.35
	return true;
}
