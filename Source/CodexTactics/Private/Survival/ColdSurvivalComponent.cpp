#include "Survival/ColdSurvivalComponent.h"
#include "AI/WorldAIPauseSubsystem.h"
#include "UI/FloatingTextSubsystem.h"
#include "Combat/WaveSubsystem.h"
#include "Core/CodexTacticsGameMode.h"
#include "Data/WaveConfigTypes.h"
#include "Camera/CameraZoneVolume.h"
#include "Characters/OperativeCharacter.h"
#include "Combat/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Interactables/HeatSourceComponent.h"
#include "Data/TacticalHeatTuning.h"
#include "Data/EnvironmentTuning.h"
#include "Environment/EnvironmentRules.h"
#include "Environment/EnvironmentSubsystem.h"
#include "Survival/TacticalHeatRules.h"
#include "Survival/WarmIdleRules.h"
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

	// Godot stops the operatives' physics loop (and with it the real-time cold) during turn-based combat; no cold either
	// while a dialogue / the pre-combat cutscene / a blocker holds the world (user request 2026-10-06).
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	const bool bTurnBased = Flow && Flow->GetCombatMode() == ECodexCombatMode::TurnBased;
	if (!ColdRules::ShouldStepCold(bTurnBased, UWorldAIPauseSubsystem::IsPausedIn(GetWorld())))
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

float UColdSurvivalComponent::GetHeatSourceWarmthRate() const
{
	const AActor* Owner = GetOwner();
	float Best = 0.f;
	if (!Owner)
	{
		return Best;
	}
	for (const TWeakObjectPtr<UHeatSourceComponent>& Source : UHeatSourceComponent::GetAllSources())
	{
		if (Source.IsValid() && Source->GetWorld() == GetWorld() && Source->WarmthRate > Best && Source->IsLocationWarm(Owner->GetActorLocation()))
		{
			Best = Source->WarmthRate;
		}
	}
	return Best;
}

bool UColdSurvivalComponent::ApplyHeatPack(const FTacticalHeatConfig& HeatConfig)
{
	AOperativeCharacter* Owner = Operative.Get();
	const UHealthComponent* Health = Owner ? Owner->HealthComponent.Get() : nullptr;
	if (!Owner || (Health && !Health->IsAlive()) || !TacticalHeatRules::CanUseHeatPack(Owner->ColdLevel, bFrostbitten))
	{
		return false;
	}
	const FHeatPackResult Result = TacticalHeatRules::ApplyHeatPack(HeatConfig, Owner->ColdLevel, bFrostbitten);
	Owner->ColdLevel = Result.NewCold;
	HeatBufferSecondsLeft = Result.BufferSeconds;
	if (Result.bClearsFrostbite)
	{
		// The collapsed operative can move again: he gets up to a crouch (safe in a fight; Z stands him up).
		bFrostbitten = false;
		Owner->SetStance(EOperativeStance::Crouching);
	}
	const bool bWasFrozen = bWeaponFrozen;
	bWeaponFrozen = ColdRules::UpdateWeaponFrozen(Config, bWeaponFrozen, Owner->ColdLevel, IsNearHeatSource());
	if (bWasFrozen && !bWeaponFrozen)
	{
		UFloatingTextSubsystem::SpawnAboveOperative(Owner, TEXT("🔥 WEAPON THAWED!"), FLinearColor(1.f, 0.6f, 0.2f));
	}
	const EColdTier NewTier = ColdRules::GetTier(Owner->ColdLevel);
	if (NewTier != Tier)
	{
		Tier = NewTier;
		ApplyTierEffects(Tier);
	}
	return true;
}

float UColdSurvivalComponent::AddColdShock(float Amount, float MinLevel)
{
	AOperativeCharacter* Owner = Operative.Get();
	const UHealthComponent* Health = Owner ? Owner->HealthComponent.Get() : nullptr;
	if (!Owner || (Health && !Health->IsAlive()))
	{
		return Owner ? Owner->ColdLevel : 0.f;
	}
	Owner->ColdLevel = ColdRules::ApplyColdShock(Owner->ColdLevel, Amount, MinLevel);
	bWeaponFrozen = ColdRules::UpdateWeaponFrozen(Config, bWeaponFrozen, Owner->ColdLevel, IsNearHeatSource());
	const EColdTier NewTier = ColdRules::GetTier(Owner->ColdLevel);
	if (NewTier != Tier)
	{
		Tier = NewTier;
		ApplyTierEffects(Tier);
	}
	return Owner->ColdLevel;
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
	// User request 2026-10-12: warming at a heat source is slower in a wave fight / ambush than while exploring.
	Environment.HeatWarmthMultiplier = WarmIdleRules::GetHeatSourceWarmthMultiplier(TacticalHeatTuning::Get(),
		Flow && WarmIdleRules::IsCombatForWarming(Flow->GetPhase()));

	// Godot cold_rate_modifier: the level wave's cold_drain_mult, overridden inside a camera zone.
	if (const UWaveSubsystem* Waves = GetWorld()->GetSubsystem<UWaveSubsystem>())
	{
		Environment.ZoneMultiplier = Waves->GetColdDrainMultiplier();
	}
	// Level JSON "cold_rate_mult" (user request 2026-10-13): level-wide, on top of the wave / zone multiplier.
	const ACodexTacticsGameMode* GameMode = GetWorld()->GetAuthGameMode<ACodexTacticsGameMode>();
	if (const ULevelConfigAsset* Level = GameMode ? GameMode->GetActiveLevelConfig() : nullptr)
	{
		Environment.LevelMultiplier = Level->Config.ColdRateMult;
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
	// Environment phase 2 (docs/systems/environment-weather.md §4.3): the weather (blended through a transition) and the time of day
	// scale the open-air cold. A locked level (Demo 01 tutorial, cold_rate_mult tuned without weather) keeps 1 / 1 unless
	// bColdOnLockedLevels; no environment (menus, tests) -> 1 / 1.
	const UEnvironmentSubsystem* EnvironmentSubsystem = UEnvironmentSubsystem::Get(this);
	if (EnvironmentSubsystem && EnvironmentSubsystem->IsInitialised())
	{
		const FEnvironmentConfig& EnvironmentConfig = EnvironmentTuning::Get();
		if (EnvironmentRules::ShouldApplyCold(EnvironmentConfig, EnvironmentSubsystem->IsLocked()))
		{
			Environment.WeatherMultiplier = EnvironmentRules::GetBlendedColdWeatherMultiplier(EnvironmentConfig, EnvironmentSubsystem->GetStateRef());
			Environment.PhaseMultiplier = EnvironmentRules::GetColdPhaseMultiplier(EnvironmentConfig, EnvironmentSubsystem->GetPhase());
		}
	}

	const float FeetZ = Location.Z - Operative->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	Environment.bElevated = FeetZ >= Config.ElevatedHeight;
	Environment.bSprinting = Operative->IsSprinting() && Operative->GetVelocity().Size2D() > 150.f;
	Environment.bInCover = Operative->bInCover; // Sprint 12: a wall at the back halves the wind chill
	// Sprint 15: a fire puddle's ring warms faster; a heat pack's buffer halves the accumulation.
	Environment.WarmthRateOverride = Environment.bWarm ? GetHeatSourceWarmthRate() : 0.f;
	Environment.AccumulationMultiplier = TacticalHeatRules::GetBufferAccumulationMultiplier(TacticalHeatTuning::Get(), HeatBufferSecondsLeft);
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
	HeatBufferSecondsLeft = FMath::Max(0.f, HeatBufferSecondsLeft - DeltaSeconds);
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
			const float FreezeDamage = Config.FreezeDamagePerSecond * Multiplier * DeltaSeconds;
			Health->ApplyDirectHealthLoss(FreezeDamage, TEXT("Cold"));
			ColdDamageTaken += FreezeDamage;
			ExtremeColdTimeSec += DeltaSeconds;
		}
	}

	const EColdTier NewTier = ColdRules::GetTier(Cold);

	// Frostbite: collapse prone at 100 %; recovers per Godot tier rules.
	if (NewTier == EColdTier::Frostbite && !bFrostbitten)
	{
		bFrostbitten = true;
		Owner->SetStance(EOperativeStance::Prone);
		// Godot floats these over the operative (no radio line).
		UFloatingTextSubsystem::SpawnAboveOperative(Owner, TEXT("❄️ FROSTBITE! COLLAPSING IN THE SNOW!"), FLinearColor(0.4f, 0.8f, 1.f));
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
		UFloatingTextSubsystem::SpawnAboveOperative(Owner, bWeaponFrozen ? TEXT("🥶 WEAPON FROZEN!") : TEXT("🔥 WEAPON THAWED!"),
			bWeaponFrozen ? FLinearColor(0.4f, 0.85f, 1.f) : FLinearColor(1.f, 0.6f, 0.2f));
	}

	if (NewTier != Tier)
	{
		Tier = NewTier;
		ApplyTierEffects(Tier);
	}
}

void UColdSurvivalComponent::RestoreColdState(bool bInFrostbitten, bool bInWeaponFrozen)
{
	bFrostbitten = bInFrostbitten;
	bWeaponFrozen = bInWeaponFrozen;
	if (const AOperativeCharacter* Owner = Operative.Get())
	{
		// The tier of the restored cold level now (no float text: nothing changed for the player, he loaded the moment as it was).
		Tier = ColdRules::GetTier(Owner->ColdLevel);
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
