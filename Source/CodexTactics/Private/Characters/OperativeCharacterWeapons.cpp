// AOperativeCharacter: Arsenal, ammo, reload, weapon switching, autonomous grenades.
// Split out of OperativeCharacter.cpp verbatim (cleanup day 2026-10-14; map: docs/port/REFACTOR_OPERATIVE_2026-10.md).
// Godot reference: Scenes/movements/player.gd (_init_weapons, switch_to_weapon_by_id, ammo_inventory, start_reload,
// _can_begin_weapon_shot, _consume_ammo_after_shot, _auto_switch_on_empty, _evaluate_ai_grenade_opportunity).

#include "Characters/OperativeCharacter.h"

#include "Combat/CombatCheats.h"

#include "Characters/OperativeCharacterInternal.h"
#include "Characters/EnemyCharacter.h"
#include "CodexTactics.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "Combat/GrenadeActor.h"
#include "Combat/GrenadeSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/WeaponBallisticsRules.h"
#include "Data/WeaponDataAsset.h"
#include "Data/WeaponRoster.h"
#include "Data/WeaponLoadoutRules.h"
#include "Combat/SidearmRules.h"
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
		CurrentClip = CurrentWeapon->Stats.Ammo.MagazineSize;
		ReserveAmmo = CurrentWeapon->Stats.Ammo.ReserveAmmo;
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
		if (!Weapon || WeaponRoster::IsOutOfPlay(Weapon->WeaponId))
		{
			continue; // out of play (the designer's roster, 2026-11-08): never in an arsenal
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
			Ammo.Clip = Weapon->Stats.Ammo.MagazineSize;
			// The M16 takes the level's rifle reserve, the pistol its Godot 24; any other weapon its own data (the D-1: 40 shells; was 0).
			Ammo.Reserve = Weapon->WeaponId == TEXT("m16") ? RifleReserve
				: (Weapon->WeaponId == TEXT("pistol") ? 24 : FMath::Max(0, Weapon->Stats.Ammo.ReserveAmmo));
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
	DropOutOfPlayWeapons(); // their loot ammo / bonus items too
	if (!AvailableWeapons.IsEmpty())
	{
		SwitchToWeaponById(AvailableWeapons[0]->WeaponId);
	}
}

FString AOperativeCharacter::GetPrimaryWeaponId() const
{
	for (const UWeaponDataAsset* Weapon : AvailableWeapons)
	{
		if (Weapon && Weapon->WeaponId != TEXT("grenade"))
		{
			return Weapon->WeaponId; // the loadout's first weapon (WeaponLoadoutRules), the one he spawns with in hands
		}
	}
	return FString();
}

bool AOperativeCharacter::TakePrimaryWeapon()
{
	const FString Primary = GetPrimaryWeaponId();
	return !Primary.IsEmpty() && SwitchToWeaponById(Primary);
}

void AOperativeCharacter::EquipRestoredWeapon(const FString& SavedWeaponId)
{
	TArray<FString> Arsenal;
	for (const UWeaponDataAsset* Weapon : AvailableWeapons)
	{
		if (Weapon)
		{
			Arsenal.Add(Weapon->WeaponId);
		}
	}
	// An old save of the Engineer holds the M16 he no longer carries: his primary (the D-1) instead, never empty hands.
	const FString Held = WeaponLoadoutRules::ResolveHeldWeapon(SavedWeaponId, Arsenal);
	if (!Held.IsEmpty())
	{
		SwitchToWeaponById(Held);
	}
}

void AOperativeCharacter::DropOutOfPlayWeapons()
{
	// The designer's roster (2026-11-08): no out-of-play weapon in hands, in the arsenal, in the ammo inventory or among the bonus items.
	AvailableWeapons.RemoveAll([](const UWeaponDataAsset* Weapon) { return Weapon && WeaponRoster::IsOutOfPlay(Weapon->WeaponId); });
	for (auto It = AmmoInventory.CreateIterator(); It; ++It)
	{
		if (WeaponRoster::IsOutOfPlay(It.Key()))
		{
			It.RemoveCurrent();
		}
	}
	for (auto It = ExtraAmmo.CreateIterator(); It; ++It)
	{
		if (WeaponRoster::IsOutOfPlay(It.Key().ToString()))
		{
			It.RemoveCurrent();
		}
	}
	BonusItems.RemoveAll([](const FString& Item) { return WeaponRoster::IsOutOfPlay(Item); });
	if (CurrentWeapon && WeaponRoster::IsOutOfPlay(CurrentWeapon->WeaponId))
	{
		CurrentWeapon = nullptr;
		if (!SwitchToWeaponById(WeaponRoster::FallbackWeaponId()) && !AvailableWeapons.IsEmpty())
		{
			SwitchToWeaponById(AvailableWeapons[0]->WeaponId);
		}
	}
}

bool AOperativeCharacter::SwitchToWeaponById(const FString& WeaponId)
{
	if (WeaponRoster::IsOutOfPlay(WeaponId))
	{
		return false; // the designer's roster (2026-11-08)
	}
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
	bSidearmDrawnByRule = false; // any switch ends the rule's draw (TryDrawSidearm sets it again after its own switch)
	SidearmNoTargetSeconds = 0.f;
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
	return !CurrentWeapon || CurrentWeapon->Stats.Ammo.bUsesAmmo;
}

void AOperativeCharacter::StartReload()
{
	if (bIsReloading || ReserveAmmo <= 0 || !UsesAmmo())
	{
		return;
	}

	const int32 MaxClip = CurrentWeapon ? CurrentWeapon->Stats.Ammo.MagazineSize : 30;
	if (CurrentClip >= MaxClip)
	{
		return;
	}

	bIsReloading = true;
	// Per-shell weapons (decision 3, 2026-11-08: the shotgun) time one shell, the rest a whole magazine (WeaponBallisticsRules).
	ReloadTimer = CurrentWeapon ? WeaponBallisticsRules::ReloadStepSeconds(CurrentWeapon->Stats.Ammo) : 2.0f;

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
	RoundsSpent += UsesAmmo() ? 1 : 0; // the bot report's rounds, the free ones included
	if (!UsesAmmo() || CombatCheats::IsInfiniteAmmo(GetWorld())) // combat lab cheat (CombatCheats): the clip stays full
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
	for (AActor* Enemy : UEnemyRegistrySubsystem::GetTaggedEnemies(World))
	{
		if (OperativeCharacterInternal::IsLiveEnemy(Enemy))
		{
			Enemies.Add(OperativeCharacterInternal::ShotFeet(Enemy));
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
	// The primary weapon first (the M16; the Engineer's D-1 shotgun, 2026-11-08), then the pistol.
	const FString Primary = GetPrimaryWeaponId();
	if (!Primary.IsEmpty() && Primary != TEXT("pistol") && Usable(*Primary))
	{
		SwitchToWeaponById(Primary);
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
	// Godot _switch_to_melee_knife (the last-chance weapon) is gone: the knife is out of play (the designer's roster, 2026-11-08).
}

bool AOperativeCharacter::TryDrawSidearm()
{
	const FSidearmConfig& Config = SidearmRules::Get();
	if (!CurrentWeapon || !Config.bEnabled)
	{
		return false;
	}
	const bool bOwned = AvailableWeapons.ContainsByPredicate([&Config](const UWeaponDataAsset* Weapon) { return Weapon && Weapon->WeaponId == Config.WeaponId; });
	const FWeaponAmmoState Sidearm = GetAmmoState(Config.WeaponId);
	const bool bUsable = bOwned && !WeaponRoster::IsOutOfPlay(Config.WeaponId) && (Sidearm.Clip > 0 || Sidearm.Reserve > 0 || Sidearm.Reserve == -1);
	// The threat: the nearest live enemy - his combat target, any monster, any fighter of another team (an empty-clip moment only, rare).
	float Threat = -1.f;
	auto Consider = [this, &Threat](const AActor* Actor)
	{
		const UHealthComponent* Health = IsValid(Actor) ? Actor->FindComponentByClass<UHealthComponent>() : nullptr;
		if (IsValid(Actor) && (!Health || Health->IsAlive()))
		{
			const float Distance = static_cast<float>(FVector::Dist(GetActorLocation(), Actor->GetActorLocation()));
			Threat = Threat < 0.f ? Distance : FMath::Min(Threat, Distance);
		}
	};
	Consider(CurrentCombatTarget.Get());
	if (UWorld* World = GetWorld())
	{
		for (const AEnemyCharacter* Enemy : UEnemyRegistrySubsystem::GetEnemyCharacters(World))
		{
			Consider(Enemy);
		}
		for (TActorIterator<AOperativeCharacter> It(World); It; ++It)
		{
			if (*It != this && It->TeamId != TeamId)
			{
				Consider(*It);
			}
		}
	}
	// In cover (the corner peek / its fire pose included) he reloads behind it (reload_in_cover; CoverSmoke / CornerHoldSmoke 2026-11-09).
	if (!SidearmRules::ShouldDrawSidearm(Config, CurrentWeapon->WeaponId == Config.WeaponId, UsesAmmo() && CurrentClip <= 0, ReserveAmmo > 0, bUsable, Threat, IsRaging(),
		bInCover))
	{
		return false;
	}
	const FString From = CurrentWeapon->WeaponId;
	if (!SwitchToWeaponById(Config.WeaponId)) // drops the reload; the primary keeps its empty clip + reserve
	{
		return false;
	}
	bSidearmDrawnByRule = true;
	SidearmNoTargetSeconds = 0.f;
	ShootTimer = FMath::Max(ShootTimer, Config.DrawSeconds);
	++SidearmDraws;
	UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("🔫 Sidearm!"), FLinearColor(1.f, 0.8f, 0.35f));
	UE_LOG(LogCodexTactics, Display, TEXT("[Sidearm] %s: %s empty, enemy %.1f m away -> %s (draw %.2f s)"), *DisplayName.ToString(), *From, Threat / 100.f,
		*Config.WeaponId, Config.DrawSeconds);
	return true;
}

void AOperativeCharacter::UpdateSidearmReturn(float DeltaTime)
{
	const FSidearmConfig& Config = SidearmRules::Get();
	if (!bSidearmDrawnByRule || !CurrentWeapon || CurrentWeapon->WeaponId != Config.WeaponId)
	{
		bSidearmDrawnByRule = false;
		return;
	}
	const AActor* Target = CurrentCombatTarget.Get();
	const UHealthComponent* Health = IsValid(Target) ? Target->FindComponentByClass<UHealthComponent>() : nullptr;
	const bool bTarget = IsValid(Target) && (!Health || Health->IsAlive());
	SidearmNoTargetSeconds = bTarget ? 0.f : SidearmNoTargetSeconds + DeltaTime;
	const bool bDry = UsesAmmo() && CurrentClip <= 0 && ReserveAmmo == 0;
	if (!SidearmRules::ShouldReturnToPrimary(Config, true, SidearmNoTargetSeconds, bDry))
	{
		return;
	}
	if (TakePrimaryWeapon())
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Sidearm] %s: back to %s (%s)"), *DisplayName.ToString(), *GetPrimaryWeaponId(), bDry ? TEXT("sidearm dry") : TEXT("no target"));
		if (UsesAmmo() && CurrentClip <= 0)
		{
			StartReload();
		}
	}
	bSidearmDrawnByRule = false;
}

