// AEnemyCharacter: spawn archetype scope, placed-archetype resolution, archetype defaults (stats, tint, capsule), knockdown
// configuration, wave modifiers / spawn entries, Godot balance.
// Split out of EnemyCharacter.cpp verbatim (cleanup 2026-10-19; map: docs/port/REFACTOR_ENEMY_2026-10.md).
// Godot reference: Scenes/movements/enemy_*.gd _ready (per-archetype stats); placed archetype / knockdown UE-only.

#include "Characters/EnemyCharacter.h"
#include "Combat/KnockdownComponent.h"
#include "CodexTactics.h"
#include "Characters/ProgressionRules.h"
#include "Combat/HealthComponent.h"
#include "Core/CodexTacticsGameMode.h"
#include "Data/GodotBalanceAsset.h"
#include "Data/WaveConfigTypes.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace EnemyCharacterLocal
{
	/** The archetype a spawner is spawning now (FScopedSpawnArchetype); unset = a placed enemy. Game thread only. */
	TOptional<EEnemyArchetype> SpawnArchetype;
}

AEnemyCharacter::FScopedSpawnArchetype::FScopedSpawnArchetype(EEnemyArchetype InArchetype)
	: Previous(EnemyCharacterLocal::SpawnArchetype)
{
	EnemyCharacterLocal::SpawnArchetype = InArchetype;
}

AEnemyCharacter::FScopedSpawnArchetype::~FScopedSpawnArchetype()
{
	EnemyCharacterLocal::SpawnArchetype = Previous;
}

void AEnemyCharacter::ResolvePlacedArchetype()
{
	if (EnemyCharacterLocal::SpawnArchetype.IsSet())
	{
		Archetype = EnemyCharacterLocal::SpawnArchetype.GetValue();
		bPlacedInLevel = false;
		return;
	}
	bPlacedInLevel = true;
	const AEnemyCharacter* Defaults = GetClass()->GetDefaultObject<AEnemyCharacter>();
	TArray<FString> Chain;
	for (const UClass* Class = GetClass(); Class && Class != AEnemyCharacter::StaticClass()->GetSuperClass(); Class = Class->GetSuperClass())
	{
		Chain.Add(Class->GetPathName());
	}
	TArray<TPair<EEnemyArchetype, FString>> Mapping;
	if (const ACodexTacticsGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ACodexTacticsGameMode>() : nullptr)
	{
		for (const TPair<EEnemyArchetype, TSoftClassPtr<AEnemyCharacter>>& Entry : GameMode->EnemyClasses)
		{
			Mapping.Emplace(Entry.Key, Entry.Value.ToSoftObjectPath().ToString());
		}
	}
	FString Source;
	const EEnemyArchetype Resolved = EnemyAIRules::ResolvePlacedArchetype(Archetype, Defaults ? Defaults->Archetype : Archetype, Chain, Mapping, Source);
	UE_LOG(LogCodexTactics, Display, TEXT("[Enemy] placed %s (%s): archetype %s from the %s"), *GetName(), *GetClass()->GetName(),
		*StaticEnum<EEnemyArchetype>()->GetNameStringByValue(static_cast<int64>(Resolved)), *Source);
	Archetype = Resolved;
}

void AEnemyCharacter::InitializeArchetype(EEnemyArchetype InArchetype)
{
	Archetype = InArchetype;
	ApplyArchetypeDefaults();
	ConfigureKnockdown();
}

void AEnemyCharacter::ConfigureKnockdown()
{
	if (!KnockdownComponent)
	{
		return;
	}
	// Humanoid UE4-mannequin rigs with their own copies of the clips (Scripts/Editor/import_knockdown_animations.py). The
	// hound (dog rig) and the cutter (it pounces itself) never fall; the marksman keeps its prone / kiting logic.
	switch (Archetype)
	{
	case EEnemyArchetype::Frostbitten:
		KnockdownComponent->bCanBeKnockedDown = true;
		KnockdownComponent->bHeavyPoise = false;
		KnockdownComponent->SetClipFolder(TEXT("Frostbitten"), false);
		break;
	case EEnemyArchetype::Brute:
		KnockdownComponent->bCanBeKnockedDown = true;
		KnockdownComponent->bHeavyPoise = true; // spec: only blasts and critical heavy hits floor a Frost Brute
		KnockdownComponent->SetClipFolder(TEXT("Brute"), false);
		break;
	default:
		KnockdownComponent->bCanBeKnockedDown = false;
		break;
	}
	// A variant body on another skeleton (BP_Enemy_FrozenPMC_Fighter: a Frostbitten on the UE5 Manny) brings its own clips.
	if (bOverrideKnockdownClipFolder && KnockdownComponent->bCanBeKnockedDown)
	{
		KnockdownComponent->SetClipFolder(EnemyAIRules::NormalizeKnockdownClipFolder(KnockdownClipFolder), false);
	}
}

void AEnemyCharacter::ApplyArchetypeDefaults()
{
	if (!HealthComponent)
	{
		return;
	}

	FLinearColor TintColor = FLinearColor(0.6f, 0.6f, 0.7f);
	float MeshScale = 1.0f;
	FVector2D Capsule(40.f, 90.f); // radius, half height

	switch (Archetype)
	{
	case EEnemyArchetype::Cutter:
		// Godot enemy_cutter.gd _ready: 75 HP, 6.2 m/s, 18 damage, 2 m, 1.1 s, crit 0.25 x1.75.
		EnemyDisplayName = TEXT("Cutter Mech-Hound");
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
		Capsule = FVector2D(35.f, 60.f);
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
		EnemyDisplayName = TEXT("Frost Hound");
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
		Capsule = FVector2D(35.f, 60.f);
		break;

	case EEnemyArchetype::Spitter:
	case EEnemyArchetype::CryoDrone:
		EnemyDisplayName = TEXT("Frost Spitter");
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
		Capsule = FVector2D(40.f, 85.f);
		break;

	case EEnemyArchetype::Brute:
		EnemyDisplayName = TEXT("Frost Brute");
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
		Capsule = FVector2D(60.f, 120.f);
		break;

	case EEnemyArchetype::Marksman:
		// UE-only archetype (TANDEM request 3): the shot itself is tuned in AMarksmanEnemyCharacter::MarksmanConfig.
		EnemyDisplayName = TEXT("Marksman");
		HealthComponent->SetMaxHealth(80.0f);
		HealthComponent->SetArmorTier(EArmorTier::Medium);
		GetCharacterMovement()->MaxWalkSpeed = 320.0f;
		AttackDamage = 45.0f;
		AttackRange = 3500.0f;
		AttackCooldown = 2.5f;
		CritChance = 0.25f;
		CritMultiplier = 2.0f;
		bFearsFire = false;
		TintColor = FLinearColor(0.35f, 0.4f, 0.25f);
		MeshScale = 1.0f;
		Capsule = FVector2D(40.f, 90.f);
		break;

	case EEnemyArchetype::Frostbitten:
	default:
		EnemyDisplayName = TEXT("Frostbitten");
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
		Capsule = FVector2D(40.f, 90.f);
		break;
	}
	EnemyDisplayName = EnemyAIRules::ResolveDisplayName(EnemyDisplayName, DisplayNameOverride);

	// An art Blueprint (skeletal mesh set, e.g. BP_Enemy_Hound) owns its look: capsule, mesh offset / rotation / scale
	// are authored in the Blueprint (Scripts/Editor/setup_enemy_animation.py filled them once with these per-type
	// values) and are left alone here. The C++ placeholder body gets the per-type capsule and size.
	if (GetMesh() && GetMesh()->GetSkeletalMeshAsset())
	{
		if (BodyMesh)
		{
			BodyMesh->SetVisibility(false);
		}
	}
	else
	{
		GetCapsuleComponent()->SetCapsuleSize(Capsule.X, Capsule.Y);
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

void AEnemyCharacter::ApplySpawnEntry(const FWaveModifiers& Mods, const FEnemySpawnEntry& Entry)
{
	// Godot _spawn_custom_json_wave: custom_stats replace the type's value (health / damage / speed times the wave
	// multiplier; range and cooldown as given), the multipliers alone apply otherwise.
	ApplyWaveModifiers(Mods.EnemyHpMult, Mods.EnemyDamageMult, Mods.EnemySpeedMult, Entry.CustomHealth);
	if (Entry.CustomDamage > 0.f)
	{
		AttackDamage = Entry.CustomDamage * Mods.EnemyDamageMult;
	}
	if (Entry.CustomSpeed > 0.f)
	{
		GetCharacterMovement()->MaxWalkSpeed = Entry.CustomSpeed * 100.f * Mods.EnemySpeedMult;
		BaseWalkSpeed = GetCharacterMovement()->MaxWalkSpeed;
	}
	if (Entry.CustomAttackRange > 0.f)
	{
		AttackRange = Entry.CustomAttackRange * 100.f;
	}
	if (Entry.CustomAttackCooldown > 0.f)
	{
		AttackCooldown = Entry.CustomAttackCooldown;
	}
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
