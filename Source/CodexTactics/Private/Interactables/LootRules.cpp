#include "Interactables/LootRules.h"

#define LOCTEXT_NAMESPACE "LootRules"

namespace
{
	struct FLootItemInfo
	{
		const TCHAR* Icon;
		FText Name;
		bool bUnits; // «ед.» instead of «шт.»
	};

	FLootItemInfo GetInfo(ELootItem Item)
	{
		switch (Item)
		{
		case ELootItem::Medkit: return { TEXT("🩹"), LOCTEXT("Medkit", "Аптечка"), false };
		case ELootItem::CannedFood: return { TEXT("🥫"), LOCTEXT("CannedFood", "Консервы"), false };
		case ELootItem::Bread: return { TEXT("🍞"), LOCTEXT("Bread", "Хлеб"), false };
		case ELootItem::Chocolate: return { TEXT("🍫"), LOCTEXT("Chocolate", "Шоколад"), false };
		case ELootItem::Matches: return { TEXT("🪵"), LOCTEXT("Matches", "Спички"), false };
		case ELootItem::RifleAmmo: return { TEXT("🔫"), LOCTEXT("RifleAmmo", "Патроны 5.56 мм (M16)"), false };
		case ELootItem::PistolAmmo: return { TEXT("🔫"), LOCTEXT("PistolAmmo", "Патроны к пистолету (9мм)"), false };
		case ELootItem::ShotgunAmmo: return { TEXT("💥"), LOCTEXT("ShotgunAmmo", "Дробь 12k (Remington)"), false };
		case ELootItem::FlameFuel: return { TEXT("🔥"), LOCTEXT("FlameFuel", "Топливо огнемёта"), true };
		case ELootItem::CryoAmmo: return { TEXT("❄️"), LOCTEXT("CryoAmmo", "Хладагент крио"), true };
		case ELootItem::PlasmaAmmo: return { TEXT("⚡"), LOCTEXT("PlasmaAmmo", "Батареи плазмы"), true };
		case ELootItem::Turret: return { TEXT("🎯"), LOCTEXT("Turret", "Боевая автотурель"), false };
		case ELootItem::Barricade: return { TEXT("🧱"), LOCTEXT("Barricade", "Тактическая бронебаррикада"), false };
		case ELootItem::Mine: return { TEXT("💣"), LOCTEXT("Mine", "Противопехотная мина"), false };
		case ELootItem::BonusWeapon: return { TEXT("⭐"), LOCTEXT("BonusWeapon", "Оружие ({0})"), false };
		default: return { TEXT("🧥"), LOCTEXT("BonusClothing", "Экипировка ({0})"), false };
		}
	}

	constexpr ELootItem AllItems[] = {
		ELootItem::Medkit, ELootItem::CannedFood, ELootItem::Bread, ELootItem::Chocolate, ELootItem::Matches,
		ELootItem::RifleAmmo, ELootItem::PistolAmmo, ELootItem::ShotgunAmmo, ELootItem::FlameFuel, ELootItem::CryoAmmo,
		ELootItem::PlasmaAmmo, ELootItem::Turret, ELootItem::Barricade, ELootItem::Mine, ELootItem::BonusWeapon,
		ELootItem::BonusClothing };
}

FText FLootEntry::GetLabel() const
{
	return FText::Format(LOCTEXT("EntryLabel", "{0} {1}: {2} {3}"), FText::FromString(Icon), Name, Count, Unit);
}

int32 FLootContents::GetCount(ELootItem Item) const
{
	switch (Item)
	{
	case ELootItem::Medkit: return Medkits;
	case ELootItem::CannedFood: return CannedFood;
	case ELootItem::Bread: return Bread;
	case ELootItem::Chocolate: return Chocolate;
	case ELootItem::Matches: return Matches;
	case ELootItem::RifleAmmo: return RifleAmmo;
	case ELootItem::PistolAmmo: return PistolAmmo;
	case ELootItem::ShotgunAmmo: return ShotgunAmmo;
	case ELootItem::FlameFuel: return FlameFuel;
	case ELootItem::CryoAmmo: return CryoAmmo;
	case ELootItem::PlasmaAmmo: return PlasmaAmmo;
	case ELootItem::Turret: return Turrets;
	case ELootItem::Barricade: return Barricades;
	case ELootItem::Mine: return Mines;
	case ELootItem::BonusWeapon: return BonusWeaponId.IsEmpty() ? 0 : 1;
	default: return BonusClothingId.IsEmpty() ? 0 : 1;
	}
}

TArray<FLootEntry> FLootContents::GetItems() const
{
	TArray<FLootEntry> Items;
	for (ELootItem Item : AllItems)
	{
		const int32 Count = GetCount(Item);
		if (Count <= 0)
		{
			continue;
		}
		const FLootItemInfo Info = GetInfo(Item);
		FLootEntry& Entry = Items.AddDefaulted_GetRef();
		Entry.Item = Item;
		Entry.Icon = Info.Icon;
		Entry.Count = Count;
		Entry.Unit = Info.bUnits ? LOCTEXT("UnitUnits", "ед.") : LOCTEXT("UnitPieces", "шт.");
		Entry.Name = Item == ELootItem::BonusWeapon ? FText::Format(Info.Name, FText::FromString(BonusWeaponId))
			: (Item == ELootItem::BonusClothing ? FText::Format(Info.Name, FText::FromString(BonusClothingId)) : Info.Name);
	}
	return Items;
}

bool FLootContents::IsEmpty() const
{
	for (ELootItem Item : AllItems)
	{
		// Godot is_empty_crate ignores pistol ammo.
		if (Item != ELootItem::PistolAmmo && GetCount(Item) > 0)
		{
			return false;
		}
	}
	return true;
}

void FLootContents::AddCount(ELootItem Item, int32 Count)
{
	switch (Item)
	{
	case ELootItem::Medkit: Medkits += Count; break;
	case ELootItem::CannedFood: CannedFood += Count; break;
	case ELootItem::Bread: Bread += Count; break;
	case ELootItem::Chocolate: Chocolate += Count; break;
	case ELootItem::Matches: Matches += Count; break;
	case ELootItem::RifleAmmo: RifleAmmo += Count; break;
	case ELootItem::PistolAmmo: PistolAmmo += Count; break;
	case ELootItem::ShotgunAmmo: ShotgunAmmo += Count; break;
	case ELootItem::FlameFuel: FlameFuel += Count; break;
	case ELootItem::CryoAmmo: CryoAmmo += Count; break;
	case ELootItem::PlasmaAmmo: PlasmaAmmo += Count; break;
	case ELootItem::Turret: Turrets += Count; break;
	case ELootItem::Barricade: Barricades += Count; break;
	case ELootItem::Mine: Mines += Count; break;
	default: break;
	}
}

int32 FLootContents::Take(ELootItem Item)
{
	const int32 Count = GetCount(Item);
	switch (Item)
	{
	case ELootItem::Medkit: Medkits = 0; break;
	case ELootItem::CannedFood: CannedFood = 0; break;
	case ELootItem::Bread: Bread = 0; break;
	case ELootItem::Chocolate: Chocolate = 0; break;
	case ELootItem::Matches: Matches = 0; break;
	case ELootItem::RifleAmmo: RifleAmmo = 0; break;
	case ELootItem::PistolAmmo: PistolAmmo = 0; break;
	case ELootItem::ShotgunAmmo: ShotgunAmmo = 0; break;
	case ELootItem::FlameFuel: FlameFuel = 0; break;
	case ELootItem::CryoAmmo: CryoAmmo = 0; break;
	case ELootItem::PlasmaAmmo: PlasmaAmmo = 0; break;
	case ELootItem::Turret: Turrets = 0; break;
	case ELootItem::Barricade: Barricades = 0; break;
	case ELootItem::Mine: Mines = 0; break;
	case ELootItem::BonusWeapon: BonusWeaponId.Reset(); break;
	default: BonusClothingId.Reset(); break;
	}
	return Count;
}

void FLootContents::DestroyAll()
{
	for (ELootItem Item : AllItems)
	{
		Take(Item);
	}
}

#undef LOCTEXT_NAMESPACE
