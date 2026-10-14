// AOperativeCharacter: Arsenal, ammo, reload, weapon switching, autonomous grenades.
// Split out of OperativeCharacter.cpp verbatim (cleanup day 2026-10-14; map: docs/port/REFACTOR_OPERATIVE_2026-10.md).
// Godot reference: Scenes/movements/player.gd (_init_weapons, switch_to_weapon_by_id, ammo_inventory, start_reload,
// _can_begin_weapon_shot, _consume_ammo_after_shot, _auto_switch_on_empty, _evaluate_ai_grenade_opportunity).

#include "Characters/OperativeCharacter.h"

#include "Characters/OperativeCharacterInternal.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/GrenadeActor.h"
#include "Combat/GrenadeSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Data/WeaponDataAsset.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Interactables/TurretActor.h"
#include "Survival/ColdSurvivalComponent.h"
#include "UI/FloatingTextSubsystem.h"
#include "UI/GameMessageSubsystem.h"

void AOperativeCharacter::EquipWeapon(UWeaponDataAsset* NewWeapon)
{
	CurrentWeapon = NewWeapon;
	if (CurrentWeapon)
	{
		CurrentClip = CurrentWeapon->MaxClipSize;
		ReserveAmmo = CurrentWeapon->DefaultReserveAmmo;
	}
	bIsReloading = false;
	ReloadTimer = 0.0f;
	ShootTimer = 0.0f;
	ApplyWeaponVisual();
}

void AOperativeCharacter::InitArsenal(const TArray<UWeaponDataAsset*>& Weapons, int32 RifleReserve)
{
	AvailableWeapons.Reset();
	AmmoInventory.Reset();
	for (UWeaponDataAsset* Weapon : Weapons)
	{
		if (!Weapon)
		{
			continue;
		}
		AvailableWeapons.Add(Weapon);
		FWeaponAmmoState& Ammo = AmmoInventory.Add(Weapon->WeaponId);
		if (Weapon->WeaponId == TEXT("grenade"))
		{
			Ammo.Clip = GrenadesCount > 0 ? 1 : 0;
			Ammo.Reserve = FMath::Max(0, GrenadesCount - 1);
		}
		else
		{
			Ammo.Clip = Weapon->MaxClipSize;
			Ammo.Reserve = Weapon->WeaponId == TEXT("m16") ? RifleReserve : (Weapon->WeaponId == TEXT("pistol") ? 24 : 0);
		}
	}
	// Rounds picked up before the arsenal existed (loot) join their weapon.
	for (auto It = ExtraAmmo.CreateIterator(); It; ++It)
	{
		if (FWeaponAmmoState* Ammo = AmmoInventory.Find(It.Key().ToString()))
		{
			Ammo->Reserve += It.Value();
			It.RemoveCurrent();
		}
	}
	CurrentWeapon = nullptr;
	if (!AvailableWeapons.IsEmpty())
	{
		SwitchToWeaponById(AvailableWeapons[0]->WeaponId);
	}
}

bool AOperativeCharacter::SwitchToWeaponById(const FString& WeaponId)
{
	const TObjectPtr<UWeaponDataAsset>* Found = AvailableWeapons.FindByPredicate([&WeaponId](const UWeaponDataAsset* Weapon)
	{
		return Weapon && Weapon->WeaponId == WeaponId;
	});
	if (!Found)
	{
		return false;
	}
	if (CurrentWeapon)
	{
		AmmoInventory.FindOrAdd(CurrentWeapon->WeaponId) = { CurrentClip, ReserveAmmo };
	}
	CurrentWeapon = *Found;
	FWeaponAmmoState Ammo = AmmoInventory.FindRef(WeaponId);
	if (WeaponId == TEXT("grenade"))
	{
		// Grenades are also spent on traps: the count is the truth.
		Ammo = { GrenadesCount > 0 ? 1 : 0, FMath::Max(0, GrenadesCount - 1) };
	}
	CurrentClip = Ammo.Clip;
	ReserveAmmo = Ammo.Reserve;
	bIsReloading = false;
	ReloadTimer = 0.0f;
	ShootTimer = 0.0f;
	SniperPendingObject.Reset();
	ApplyWeaponVisual();
	return true;
}

FWeaponAmmoState AOperativeCharacter::GetAmmoState(const FString& WeaponId) const
{
	if (CurrentWeapon && CurrentWeapon->WeaponId == WeaponId)
	{
		return { CurrentClip, ReserveAmmo };
	}
	return AmmoInventory.FindRef(WeaponId);
}

void AOperativeCharacter::AddAmmo(const FString& WeaponId, int32 Count)
{
	if (CurrentWeapon && CurrentWeapon->WeaponId == WeaponId)
	{
		ReserveAmmo += Count;
	}
	else if (FWeaponAmmoState* Ammo = AmmoInventory.Find(WeaponId))
	{
		Ammo->Reserve += Count;
	}
	else
	{
		ExtraAmmo.FindOrAdd(FName(*WeaponId)) += Count;
	}
}

int32 AOperativeCharacter::GetReserve(const FString& WeaponId) const
{
	if (CurrentWeapon && CurrentWeapon->WeaponId == WeaponId)
	{
		return ReserveAmmo;
	}
	if (const FWeaponAmmoState* Ammo = AmmoInventory.Find(WeaponId))
	{
		return Ammo->Reserve;
	}
	return ExtraAmmo.FindRef(FName(*WeaponId));
}

int32 AOperativeCharacter::TakeReserve(const FString& WeaponId, int32 Max)
{
	int32* Reserve = nullptr;
	if (CurrentWeapon && CurrentWeapon->WeaponId == WeaponId)
	{
		Reserve = &ReserveAmmo;
	}
	else if (FWeaponAmmoState* Ammo = AmmoInventory.Find(WeaponId))
	{
		Reserve = &Ammo->Reserve;
	}
	else
	{
		Reserve = ExtraAmmo.Find(FName(*WeaponId));
	}
	if (!Reserve)
	{
		return 0;
	}
	const int32 Taken = FMath::Clamp(Max, 0, *Reserve);
	*Reserve -= Taken;
	return Taken;
}

bool AOperativeCharacter::UsesAmmo() const
{
	return !CurrentWeapon || CurrentWeapon->bUsesAmmo;
}

void AOperativeCharacter::StartReload()
{
	if (bIsReloading || ReserveAmmo <= 0 || !UsesAmmo())
	{
		return;
	}

	const int32 MaxClip = CurrentWeapon ? CurrentWeapon->MaxClipSize : 30;
	if (CurrentClip >= MaxClip)
	{
		return;
	}

	bIsReloading = true;
	ReloadTimer = CurrentWeapon ? CurrentWeapon->ReloadTime : 2.0f;

	// Godot start_reload: radio callout asking for cover.
	static const TCHAR* Callouts[] = {
		TEXT("🔄 Reloading! Cover me!"),
		TEXT("🔄 Mag empty! Cover the sector!"),
		TEXT("🔄 Changing mags, hold them off!"),
		TEXT("🔄 Reloading! Watch my back!") };
	if (UWorld* World = GetWorld())
	{
		if (UGameMessageSubsystem* Messages = World->GetSubsystem<UGameMessageSubsystem>())
		{
			Messages->PostMessage(DisplayName, FText::FromString(Callouts[FMath::RandHelper(UE_ARRAY_COUNT(Callouts))]));
		}
	}
}

bool AOperativeCharacter::CanShoot() const
{
	if (!HealthComponent || !HealthComponent->IsAlive() || IsKnockedDown() || (IsPanicking() && !IsRaging())) // Godot panic: can_shoot = false
	{
		return false;
	}
	if (bSprinting || bCarrying || (bIsReloading && !IsRaging())) // Godot: rage ignores the reload
	{
		return false;
	}
	// Godot: a weapon frozen at >= 90 % cold cannot fire until it thaws.
	if (ColdSurvival && ColdSurvival->IsWeaponFrozen())
	{
		return false;
	}
	return true;
}

bool AOperativeCharacter::CanBeginWeaponShot()
{
	if (!HealthComponent || !HealthComponent->IsAlive() || bIsReloading || MisfireCooldownTimer > 0.f || IsKnockedDown())
	{
		return false;
	}
	if (ColdSurvival && ColdSurvival->IsWeaponFrozen())
	{
		if (WeaponFreezeNotifyTimer <= 0.f)
		{
			NotifyWeaponFrozen();
		}
		return false;
	}
	if (CurrentClip > 0 || !UsesAmmo())
	{
		return true;
	}
	StartReload();
	return false;
}

void AOperativeCharacter::ConsumeAmmoAfterShot()
{
	if (!UsesAmmo())
	{
		return;
	}
	CurrentClip = FMath::Max(0, CurrentClip - 1);
	if (CurrentClip <= 0)
	{
		StartReload();
	}
}

void AOperativeCharacter::NotifyWeaponFrozen()
{
	WeaponFreezeNotifyTimer = 2.5f;
	UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("🥶 WEAPON FROZEN! Need a heat source!"), FLinearColor(0.4f, 0.85f, 1.f));
}

bool AOperativeCharacter::TryAIGrenadeThrow()
{
	UWorld* World = GetWorld();
	UGrenadeSubsystem* Grenades = World ? World->GetSubsystem<UGrenadeSubsystem>() : nullptr;
	const UGameFlowSubsystem* Flow = World ? World->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	if (!Grenades || GrenadesCount <= 0 || AIGrenadeCooldown > 0.f || !HealthComponent || !HealthComponent->IsAlive()
		|| Grenades->IsAiming() || (Flow && Flow->GetCombatMode() == ECodexCombatMode::TurnBased))
	{
		return false;
	}
	// Godot global_position: operatives at their centre (1 m above the feet), enemies at their feet.
	auto Centre = [](const AActor* Actor) { return Actor->GetActorLocation() - FVector(0.f, 0.f, Actor->GetSimpleCollisionHalfHeight() - 100.f); };
	TArray<FVector> Enemies;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (OperativeCharacterInternal::IsLiveEnemy(*It))
		{
			Enemies.Add(OperativeCharacterInternal::ShotFeet(*It));
		}
	}
	TArray<FVector> Allies;
	if (const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>())
	{
		for (const AOperativeCharacter* Member : Squad->GetMembers())
		{
			if (Member->HealthComponent && Member->HealthComponent->IsAlive())
			{
				Allies.Add(Centre(Member));
			}
		}
	}
	for (TActorIterator<ATurretActor> It(World); It; ++It)
	{
		Allies.Add(It->GetActorLocation()); // Godot "allies"
	}
	const FAIGrenadeOpportunity Opportunity = AIGrenadeRules::Evaluate(AIGrenadeConfig, Centre(this), Stance, GrenadeThrowRange,
		GrenadeEffectRadius, Enemies, Allies);
	if (!Opportunity.bCanThrow)
	{
		return false;
	}
	AIGrenadeCooldown = AIGrenadeConfig.Cooldown;
	AGrenadeActor* Grenade = Grenades->ThrowAt(this, Opportunity.Target);
	if (UGameMessageSubsystem* Messages = World->GetSubsystem<UGameMessageSubsystem>())
	{
		const TCHAR* Callouts[] = { TEXT("💣 Grenade out! Get down!"), TEXT("🧨 They're bunched up! Catch this!"),
			TEXT("💣 Grenade away! Heads down!"), TEXT("🧨 Eat this, bastards!") };
		Messages->PostMessage(DisplayName, FText::FromString(Callouts[FMath::RandRange(0, 3)]));
	}
	return Grenade != nullptr;
}

void AOperativeCharacter::AutoSwitchOnEmpty()
{
	auto Usable = [this](const TCHAR* Id)
	{
		const bool bOwned = AvailableWeapons.ContainsByPredicate([Id](const UWeaponDataAsset* Weapon) { return Weapon && Weapon->WeaponId == Id; });
		const FWeaponAmmoState Ammo = GetAmmoState(Id);
		return bOwned && (Ammo.Clip > 0 || Ammo.Reserve > 0 || Ammo.Reserve == -1);
	};
	if (Usable(TEXT("m16")))
	{
		SwitchToWeaponById(TEXT("m16"));
		return;
	}
	if (Usable(TEXT("pistol")))
	{
		SwitchToWeaponById(TEXT("pistol"));
		return;
	}
	if (GrenadesCount > 0 && AIGrenadeCooldown <= 0.f)
	{
		TryAIGrenadeThrow();
	}
	SwitchToWeaponById(TEXT("knife")); // Godot _switch_to_melee_knife: the last-chance weapon
}
