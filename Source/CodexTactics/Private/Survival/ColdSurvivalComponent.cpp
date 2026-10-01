#include "Survival/ColdSurvivalComponent.h"
#include "UI/FloatingTextSubsystem.h"
#include "Combat/WaveSubsystem.h"
#include "Camera/CameraZoneVolume.h"
#include "Characters/OperativeCharacter.h"
#include "Combat/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Interactables/HeatSourceComponent.h"
#include "UI/GameMessageSubsystem.h"

#define LOCTEXT_NAMESPACE "ColdSurvival"

namespace
{
	/** Godot: warm regeneration boost per fortitude point. */
	constexpr float RegenFortitudeBoostPerPoint = 0.015f;
	/** Godot: frostbite clears in a warm zone once cold drops to this level from the Freezing tier. */
	constexpr float FrostbiteWarmClearCold = 75.f;
}

UColdSurvivalComponent::UColdSurvivalComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UColdSurvivalComponent::BeginPlay()
{
	Super::BeginPlay();
	Operative = Cast<AOperativeCharacter>(GetOwner());
	if (Operative.IsValid())
	{
		Tier = ColdRules::GetTier(Operative->ColdLevel);
		ApplyTierEffects(Tier);
	}
}

void UColdSurvivalComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Godot stops the operatives' physics loop (and with it the real-time cold) during turn-based combat.
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	if (Flow && Flow->GetCombatMode() == ECodexCombatMode::TurnBased)
	{
		return;
	}
	StepCold(DeltaTime);
}

bool UColdSurvivalComponent::IsNearHeatSource() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}
	for (const TWeakObjectPtr<UHeatSourceComponent>& Source : UHeatSourceComponent::GetAllSources())
	{
		if (Source.IsValid() && Source->GetWorld() == GetWorld() && Source->IsLocationWarm(Owner->GetActorLocation()))
		{
			return true;
		}
	}
	return false;
}

float UColdSurvivalComponent::GetMisfireChance() const
{
	return Operative.IsValid() ? ColdRules::GetMisfireChance(Config, Operative->ColdLevel, IsNearHeatSource()) : 0.f;
}

float UColdSurvivalComponent::GetAimPenalty() const
{
	return Operative.IsValid() ? ColdRules::GetAimPenalty(Config, Operative->ColdLevel) : 0.f;
}

FColdEnvironment UColdSurvivalComponent::GatherEnvironment() const
{
	FColdEnvironment Environment;
	Environment.bWarm = IsNearHeatSource();

	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	Environment.bPreparation = Flow && Flow->GetPhase() == ECodexGamePhase::Preparation;

	// Godot cold_rate_modifier: the level wave's cold_drain_mult, overridden inside a camera zone.
	if (const UWaveSubsystem* Waves = GetWorld()->GetSubsystem<UWaveSubsystem>())
	{
		Environment.ZoneMultiplier = Waves->GetColdDrainMultiplier();
	}
	const FVector Location = Operative->GetActorLocation();
	for (TActorIterator<ACameraZoneVolume> It(GetWorld()); It; ++It)
	{
		if (It->ContainsLocation(Location))
		{
			Environment.ZoneMultiplier = It->GetColdMultiplier();
			break;
		}
	}

	const float FeetZ = Location.Z - Operative->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	Environment.bElevated = FeetZ >= Config.ElevatedHeight;
	Environment.bSprinting = Operative->IsSprinting() && Operative->GetVelocity().Size2D() > 150.f;
	return Environment;
}

void UColdSurvivalComponent::StepCold(float DeltaSeconds)
{
	AOperativeCharacter* Owner = Operative.Get();
	UHealthComponent* Health = Owner ? Owner->HealthComponent.Get() : nullptr;
	if (!Owner || (Health && !Health->IsAlive()))
	{
		return;
	}

	const FColdEnvironment Environment = GatherEnvironment();
	Owner->ColdLevel = ColdRules::StepCold(Config, Owner->ColdLevel, DeltaSeconds, Environment, Owner->GetStance(), Fortitude);
	const float Cold = Owner->ColdLevel;

	if (Health)
	{
		// Warm regeneration (Godot: in a warm zone with cold <= 20 %).
		if (Environment.bWarm && Cold <= Config.RegenMaxCold && Health->GetCurrentHealth() < Health->GetMaxHealth())
		{
			Health->Heal(Config.WarmHealthRegenPerSecond * (1.f + Fortitude * RegenFortitudeBoostPerPoint) * DeltaSeconds);
		}
		// Freezing damage at 100 % (not during preparation), halved in combat.
		if (Cold >= 100.f && !Environment.bPreparation)
		{
			const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
			const bool bCombat = Flow && Flow->GetPhase() == ECodexGamePhase::WaveCombat;
			const float Multiplier = bCombat ? Config.CombatFreezeDamageMultiplier : 1.f;
			Health->ApplyDirectHealthLoss(Config.FreezeDamagePerSecond * Multiplier * DeltaSeconds, TEXT("Холод"));
		}
	}

	const EColdTier NewTier = ColdRules::GetTier(Cold);

	// Frostbite: collapse prone at 100 %; recovers per Godot tier rules.
	if (NewTier == EColdTier::Frostbite && !bFrostbitten)
	{
		bFrostbitten = true;
		Owner->SetStance(EOperativeStance::Prone);
		// Godot floats these over the operative (no radio line).
		UFloatingTextSubsystem::SpawnAboveOperative(Owner, TEXT("❄️ ОБМОРОЖЕНИЕ! ПАДАЕТ НА СНЕГ!"), FLinearColor(0.4f, 0.8f, 1.f));
	}
	else if (bFrostbitten
		&& (NewTier == EColdTier::Normal
			|| (NewTier == EColdTier::Chills && Environment.bWarm)
			|| (NewTier == EColdTier::Freezing && Environment.bWarm && Cold <= FrostbiteWarmClearCold)))
	{
		bFrostbitten = false;
	}

	const bool bWasFrozen = bWeaponFrozen;
	bWeaponFrozen = ColdRules::UpdateWeaponFrozen(Config, bWeaponFrozen, Cold, Environment.bWarm);
	if (bWeaponFrozen != bWasFrozen)
	{
		UFloatingTextSubsystem::SpawnAboveOperative(Owner, bWeaponFrozen ? TEXT("🥶 ОРУЖИЕ ЗАМЁРЗЛО!") : TEXT("🔥 ОРУЖИЕ ОТОГРЕЛОСЬ!"),
			bWeaponFrozen ? FLinearColor(0.4f, 0.85f, 1.f) : FLinearColor(1.f, 0.6f, 0.2f));
	}

	if (NewTier != Tier)
	{
		Tier = NewTier;
		ApplyTierEffects(Tier);
	}
}

void UColdSurvivalComponent::ApplyTierEffects(EColdTier NewTier)
{
	if (AOperativeCharacter* Owner = Operative.Get())
	{
		Owner->SetColdSpeedMultiplier(ColdRules::GetSpeedMultiplier(NewTier));
		OnTierChanged.Broadcast(Owner, NewTier);
	}
}

#undef LOCTEXT_NAMESPACE
