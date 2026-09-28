#include "Combat/HealthComponent.h"
#include "GameFramework/Actor.h"

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
	const float FinalDamage = FMath::Max(1.0f, DmgAfterArmor * ElementMult * DefenseMultiplier);

	const float OldHealth = CurrentHealth;
	CurrentHealth = FMath::Max(0.0f, CurrentHealth - FinalDamage);

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
