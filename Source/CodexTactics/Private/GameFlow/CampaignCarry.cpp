#include "GameFlow/CampaignCarry.h"

#include "Characters/OperativeCharacter.h"
#include "Combat/HealthComponent.h"
#include "Data/WeaponDataAsset.h"
#include "Survival/ColdSurvivalComponent.h"

namespace CampaignCarryLocal
{
	struct FItemField
	{
		const TCHAR* Key;
		int32 AOperativeCharacter::* Field;
	};

	/** The save system's item keys (SaveGameSubsystem SaveOperative) and the counts they name. */
	const FItemField ItemFields[] = {
		{ TEXT("matches_count"), &AOperativeCharacter::MatchesCount },
		{ TEXT("turrets_count"), &AOperativeCharacter::TurretsCount },
		{ TEXT("barricades_count"), &AOperativeCharacter::BarricadesCount },
		{ TEXT("mines_count"), &AOperativeCharacter::MinesCount },
		{ TEXT("medkits_count"), &AOperativeCharacter::MedkitsCount },
		{ TEXT("canned_food_count"), &AOperativeCharacter::CannedFoodCount },
		{ TEXT("bread_count"), &AOperativeCharacter::BreadCount },
		{ TEXT("chocolate_count"), &AOperativeCharacter::ChocolateCount },
		{ TEXT("grenades_count"), &AOperativeCharacter::GrenadesCount },
		{ TEXT("heat_packs_count"), &AOperativeCharacter::HeatPacksCount },
		{ TEXT("firewood_count"), &AOperativeCharacter::FirewoodCount },
		{ TEXT("flares_count"), &AOperativeCharacter::FlaresCount },
		{ TEXT("stoves_count"), &AOperativeCharacter::PortableStovesCount },
	};
}

TArray<FString> CampaignCarry::ItemKeys()
{
	TArray<FString> Keys;
	for (const CampaignCarryLocal::FItemField& Item : CampaignCarryLocal::ItemFields)
	{
		Keys.Add(Item.Key);
	}
	return Keys;
}

FCarriedOperative CampaignCarry::Capture(const AOperativeCharacter& Operative)
{
	FCarriedOperative Carried;
	Carried.Role = Operative.SquadRole;
	Carried.SquadIndex = Operative.SquadIndex;
	Carried.DisplayName = Operative.DisplayName.ToString();
	Carried.bKilledInAction = Operative.IsKilledInAction();
	Carried.Level = Operative.Level;
	Carried.CurrentExp = Operative.CurrentExp;
	Carried.UnspentStatPoints = Operative.UnspentStatPoints;
	Carried.MaxHealth = Operative.GetStatValue(EProgressStat::Health);
	Carried.Luck = Operative.Luck;
	Carried.Accuracy = Operative.Accuracy;
	Carried.Fortitude = Operative.GetStatValue(EProgressStat::Fortitude);
	for (const CampaignCarryLocal::FItemField& Item : CampaignCarryLocal::ItemFields)
	{
		Carried.Items.Add(Item.Key, Operative.*(Item.Field));
	}
	Carried.StoveGasSeconds = Operative.StoveGasSeconds;
	Carried.BonusItems = Operative.BonusItems;
	// The weapon in hands keeps its live clip outside the inventory: GetAmmoState reads either (as SaveOperative does).
	for (const UWeaponDataAsset* Weapon : Operative.AvailableWeapons)
	{
		if (Weapon)
		{
			const FWeaponAmmoState State = Operative.GetAmmoState(Weapon->WeaponId);
			Carried.Ammo.Add({ Weapon->WeaponId, State.Clip, State.Reserve });
		}
	}
	Carried.ExtraAmmo = Operative.ExtraAmmo;
	Carried.CurrentWeaponId = Operative.CurrentWeapon ? Operative.CurrentWeapon->WeaponId : FString();
	return Carried;
}

void CampaignCarry::Apply(AOperativeCharacter& Operative, const FCarriedOperative& Carried)
{
	Operative.Level = Carried.Level;
	Operative.CurrentExp = Carried.CurrentExp;
	Operative.UnspentStatPoints = Carried.UnspentStatPoints;
	Operative.Luck = Carried.Luck;
	Operative.Accuracy = Carried.Accuracy;
	if (Operative.ColdSurvival)
	{
		Operative.ColdSurvival->Fortitude = Carried.Fortitude;
	}
	if (UHealthComponent* Health = Operative.HealthComponent)
	{
		const float Max = FMath::Max(1.f, Carried.MaxHealth);
		Health->RestoreHealth(Max, Max); // full health on arrival, no damage / heal events
	}
	Operative.ColdLevel = 0.f;
	for (const CampaignCarryLocal::FItemField& Item : CampaignCarryLocal::ItemFields)
	{
		if (const int32* Count = Carried.Items.Find(Item.Key))
		{
			Operative.*(Item.Field) = *Count;
		}
	}
	Operative.StoveGasSeconds = Carried.StoveGasSeconds;
	Operative.BonusItems = Carried.BonusItems;
	for (const FCarriedAmmo& Entry : Carried.Ammo)
	{
		Operative.AmmoInventory.Add(Entry.WeaponId, { Entry.Clip, Entry.Reserve });
	}
	Operative.ExtraAmmo = Carried.ExtraAmmo;
	if (!Carried.Ammo.IsEmpty())
	{
		// The carried state of the weapon in hands is in the inventory now; take it into the hands from there (SaveGameSubsystem order).
		Operative.CurrentWeapon = nullptr;
		if (Carried.CurrentWeaponId.IsEmpty() || !Operative.SwitchToWeaponById(Carried.CurrentWeaponId))
		{
			Operative.SwitchToWeaponById(TEXT("m16"));
		}
	}
	Operative.CaptureProgressionBases();
}

const FCarriedOperative* CampaignCarry::FindFor(const FCampaignSquadCarry& Carry, EOperativeRole Role, int32 SquadIndex)
{
	const FCarriedOperative* ByRole = Carry.Members.FindByPredicate([Role](const FCarriedOperative& Member) { return Member.Role == Role; });
	if (ByRole)
	{
		return ByRole;
	}
	return Carry.Members.FindByPredicate([SquadIndex](const FCarriedOperative& Member) { return Member.SquadIndex == SquadIndex; });
}

bool CampaignCarry::ShouldSpawn(const FCampaignSquadCarry& Carry, EOperativeRole Role, int32 SquadIndex)
{
	const FCarriedOperative* Carried = Carry.IsSet() ? FindFor(Carry, Role, SquadIndex) : nullptr;
	return !Carried || !Carried->bKilledInAction;
}
