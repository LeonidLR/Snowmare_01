#include "Combat/HealthComponent.h"
#include "Combat/KnockdownComponent.h"
#include "GameFramework/Actor.h"
#include "UI/FloatingTextSubsystem.h"

namespace
{
	/** Godot enemy_base.gd floats its own numbers; operatives (UOperativeCharacter::TakeHit) and objects do not use these. */
	bool FloatsEnemyNumbers(const AActor* Owner)
	{
		return Owner && Owner->ActorHasTag(FName(TEXT("Enemy")));
	}
}

UHealthComponent::UHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UHealthComponent::BeginPlay()
{
	Super::BeginPlay();
	CurrentHealth = MaxHealth;
	bIsDead = false;
}

void UHealthComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bIsDead)
	{
		ProcessStatusEffects(DeltaTime);
	}
}

void UHealthComponent::ApplyDirectHealthLoss(float Amount, const FString& Source)
{
	if (bIsDead || Amount <= 0.0f)
	{
		return;
	}
	const float OldHealth = CurrentHealth;
	CurrentHealth = FMath::Max(0.0f, CurrentHealth - Amount);
	OnHealthChanged.Broadcast(CurrentHealth, MaxHealth, CurrentHealth - OldHealth);
	if (CurrentHealth <= 0.0f)
	{
		Die(Source);
	}
}

float UHealthComponent::TakeDamage(const FDamageSpec& Spec)
{
	if (bIsDead || Spec.Amount <= 0.0f)
	{
		return 0.0f;
	}

	const float ElementMult = ElementalAffinities.GetMultiplier(Spec.DamageType);
	// Absolute elemental immunity (e.g. Cryo on ice creatures)
	if (ElementMult <= 0.001f)
	{
		if (FloatsEnemyNumbers(GetOwner()))
		{
			UFloatingTextSubsystem::SpawnAboveEnemy(GetOwner(), TEXT("❄️ IMMUNE"), FLinearColor(0.3f, 0.8f, 1.f));
		}
		return 0.0f;
	}

	// Calculate armor reduction with armor shred (70% reduction) and armor penetration
	float EffectiveArmor = BaseArmorReduction;
	if (ArmorShredTimer > 0.0f)
	{
		EffectiveArmor = FMath::Max(0.0f, EffectiveArmor * 0.3f);
	}
	EffectiveArmor *= (1.0f - FMath::Clamp(Spec.ArmorPenetration, 0.0f, 1.0f));

	const float DmgAfterArmor = Spec.Amount * (1.0f - EffectiveArmor);
	// Sprint 14: a knocked-down unit takes shots x0.6 (prone profile) and melee x1.5; blasts are not modified.
	const UKnockdownComponent* Knockdown = GetOwner() ? GetOwner()->FindComponentByClass<UKnockdownComponent>() : nullptr;
	const float KnockdownScale = Knockdown && Spec.DamageType != EDamageType::Explosive ? Knockdown->GetDamageMultiplier(Spec.bMelee) : 1.f;
	const float FinalDamage = FMath::Max(1.0f, DmgAfterArmor * ElementMult * DefenseMultiplier * KnockdownScale);

	const float OldHealth = CurrentHealth;
	CurrentHealth = FMath::Max(0.0f, CurrentHealth - FinalDamage);

	// Godot enemy_base.gd take_damage: the number with the damage type's prefix / colour.
	if (FloatsEnemyNumbers(GetOwner()))
	{
		FString Prefix = TEXT("-");
		FLinearColor Color = FLinearColor::White;
		switch (Spec.DamageType)
		{
		case EDamageType::Fire: Prefix = TEXT("🔥 -"); Color = FLinearColor(1.f, 0.45f, 0.1f); break;
		case EDamageType::Cryo: Prefix = TEXT("❄️ -"); Color = FLinearColor(0.2f, 0.85f, 1.f); break;
		case EDamageType::Energy: Prefix = TEXT("⚡ -"); Color = FLinearColor(0.75f, 0.3f, 1.f); break;
		case EDamageType::Explosive: Prefix = TEXT("💥 -"); Color = FLinearColor(1.f, 0.8f, 0.2f); break;
		default:
			if (ElementMult > 1.2f)
			{
				Prefix = TEXT("🎯 -");
				Color = FLinearColor(1.f, 0.2f, 0.2f);
			}
			else if (EffectiveArmor > 0.4f)
			{
				Prefix = TEXT("🛡️ -");
				Color = FLinearColor(0.7f, 0.75f, 0.8f);
			}
			break;
		}
		UFloatingTextSubsystem::SpawnAboveEnemy(GetOwner(), FString::Printf(TEXT("%s%d"), *Prefix, FMath::FloorToInt(FinalDamage)), Color);
	}

	// Status effects
	switch (Spec.StatusEffect)
	{
	case EStatusEffect::Burning:
		BurningTimer = FMath::Max(BurningTimer, Spec.StatusDuration);
		BurningTickDamage = Spec.StatusTickDamage;
		break;
	case EStatusEffect::Frozen:
		FrozenTimer = FMath::Max(FrozenTimer, Spec.StatusDuration);
		break;
	case EStatusEffect::Stagger:
		StaggerTimer = FMath::Max(StaggerTimer, Spec.StatusDuration);
		break;
	case EStatusEffect::Bleeding:
		BleedingTimer = FMath::Max(BleedingTimer, Spec.StatusDuration);
		BleedingTickDamage = Spec.StatusTickDamage;
		break;
	case EStatusEffect::ArmorShred:
		ArmorShredTimer = FMath::Max(ArmorShredTimer, Spec.StatusDuration > 0.0f ? Spec.StatusDuration : 6.0f);
		break;
	default:
		break;
	}

	// Explosives shred armor (Godot line 295)
	if (Spec.DamageType == EDamageType::Explosive)
	{
		ArmorShredTimer = FMath::Max(ArmorShredTimer, 8.0f);
	}

	OnDamaged.Broadcast(Spec, FinalDamage);
	OnHealthChanged.Broadcast(CurrentHealth, MaxHealth, -FinalDamage);

	if (CurrentHealth <= 0.0f)
	{
		Die(Spec.AttackerSource);
	}

	return FinalDamage;
}

void UHealthComponent::Heal(float Amount)
{
	if (bIsDead || Amount <= 0.0f)
	{
		return;
	}

	const float OldHealth = CurrentHealth;
	CurrentHealth = FMath::Min(MaxHealth, CurrentHealth + Amount);
	const float Delta = CurrentHealth - OldHealth;

	if (Delta > 0.0f)
	{
		OnHealthChanged.Broadcast(CurrentHealth, MaxHealth, Delta);
	}
}

void UHealthComponent::SetMaxHealth(float NewMax, bool bResetCurrent)
{
	MaxHealth = FMath::Max(1.0f, NewMax);
	if (bResetCurrent)
	{
		CurrentHealth = MaxHealth;
		bIsDead = false;
	}
	else
	{
		CurrentHealth = FMath::Min(CurrentHealth, MaxHealth);
	}
	OnHealthChanged.Broadcast(CurrentHealth, MaxHealth, 0.0f);
}

bool UHealthComponent::HasStatusEffect(EStatusEffect Effect) const
{
	switch (Effect)
	{
	case EStatusEffect::Burning: return BurningTimer > 0.0f;
	case EStatusEffect::Frozen: return FrozenTimer > 0.0f;
	case EStatusEffect::Stagger: return StaggerTimer > 0.0f;
	case EStatusEffect::Bleeding: return BleedingTimer > 0.0f;
	case EStatusEffect::ArmorShred: return ArmorShredTimer > 0.0f;
	case EStatusEffect::Knockdown:
	{
		const UKnockdownComponent* Knockdown = GetOwner() ? GetOwner()->FindComponentByClass<UKnockdownComponent>() : nullptr;
		return Knockdown && Knockdown->IsDown();
	}
	default: return false;
	}
}

void UHealthComponent::ProcessStatusEffects(float DeltaSeconds)
{
	if (BurningTimer > 0.0f)
	{
		BurningTimer -= DeltaSeconds;
		BurningTickAccum += DeltaSeconds;
		if (BurningTickAccum >= BurningTickInterval)
		{
			BurningTickAccum = 0.0f;
			CurrentHealth = FMath::Max(0.0f, CurrentHealth - BurningTickDamage);
			if (FloatsEnemyNumbers(GetOwner()))
			{
				UFloatingTextSubsystem::SpawnAboveEnemy(GetOwner(), FString::Printf(TEXT("🔥 -%d"), FMath::FloorToInt(BurningTickDamage)), FLinearColor(1.f, 0.45f, 0.1f));
			}
			OnHealthChanged.Broadcast(CurrentHealth, MaxHealth, -BurningTickDamage);
			if (CurrentHealth <= 0.0f)
			{
				Die(TEXT("Burning"));
				return;
			}
		}
	}

	if (BleedingTimer > 0.0f)
	{
		BleedingTimer -= DeltaSeconds;
		BleedingTickAccum += DeltaSeconds;
		if (BleedingTickAccum >= BleedingTickInterval)
		{
			BleedingTickAccum = 0.0f;
			CurrentHealth = FMath::Max(0.0f, CurrentHealth - BleedingTickDamage);
			if (FloatsEnemyNumbers(GetOwner()))
			{
				UFloatingTextSubsystem::SpawnAboveEnemy(GetOwner(), FString::Printf(TEXT("🩸 -%d"), FMath::FloorToInt(BleedingTickDamage)), FLinearColor(0.9f, 0.1f, 0.1f));
			}
			OnHealthChanged.Broadcast(CurrentHealth, MaxHealth, -BleedingTickDamage);
			if (CurrentHealth <= 0.0f)
			{
				Die(TEXT("Bleeding"));
				return;
			}
		}
	}

	if (FrozenTimer > 0.0f)
	{
		FrozenTimer -= DeltaSeconds;
	}
	if (StaggerTimer > 0.0f)
	{
		StaggerTimer -= DeltaSeconds;
	}
	if (ArmorShredTimer > 0.0f)
	{
		ArmorShredTimer -= DeltaSeconds;
	}
}

void UHealthComponent::Die(const FString& AttackerSource)
{
	if (bIsDead)
	{
		return;
	}
	bIsDead = true;
	CurrentHealth = 0.0f;
	OnDied.Broadcast(GetOwner(), AttackerSource);
	OnDiedNative.Broadcast(GetOwner(), AttackerSource);
}
