#include "Interactables/ItemStashComponent.h"

UItemStashComponent::UItemStashComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

int32 UItemStashComponent::GetCount(ETransferItem Item) const
{
	const int32* Count = Items.Find(Item);
	return Count ? *Count : 0;
}

int32 UItemStashComponent::GetTotal() const
{
	int64 Total = 0;
	for (const TPair<ETransferItem, int32>& Pair : Items)
	{
		Total += FMath::Max(Pair.Value, 0);
	}
	return static_cast<int32>(FMath::Min<int64>(Total, MAX_int32));
}

int32 UItemStashComponent::GetFreeSpace() const
{
	return Capacity <= 0 ? MAX_int32 : FMath::Max(0, Capacity - GetTotal());
}

int32 UItemStashComponent::Add(ETransferItem Item, int32 Count, bool bIgnoreCapacity)
{
	const int32 Accepted = FMath::Max(0, bIgnoreCapacity ? Count : FMath::Min(Count, GetFreeSpace()));
	if (Accepted > 0)
	{
		Items.FindOrAdd(Item) += Accepted;
		Changed();
	}
	return Accepted;
}

int32 UItemStashComponent::Take(ETransferItem Item, int32 Max)
{
	int32* Count = Items.Find(Item);
	const int32 Taken = Count ? FMath::Clamp(Max, 0, *Count) : 0;
	if (Taken > 0)
	{
		*Count -= Taken;
		if (*Count <= 0)
		{
			Items.Remove(Item);
		}
		Changed();
	}
	return Taken;
}

void UItemStashComponent::Clear()
{
	if (Items.Num() > 0)
	{
		Items.Reset();
		Changed();
	}
}

void UItemStashComponent::SetContents(const TMap<ETransferItem, int32>& InItems)
{
	Items.Reset();
	for (const TPair<ETransferItem, int32>& Pair : InItems)
	{
		if (Pair.Value > 0)
		{
			Items.Add(Pair.Key, Pair.Value);
		}
	}
	Changed();
}

TArray<ETransferItem> UItemStashComponent::GetItemTypes() const
{
	TArray<ETransferItem> Types;
	for (const TPair<ETransferItem, int32>& Pair : Items)
	{
		if (Pair.Value > 0)
		{
			Types.Add(Pair.Key);
		}
	}
	Types.Sort([](ETransferItem A, ETransferItem B) { return static_cast<uint8>(A) < static_cast<uint8>(B); });
	return Types;
}

void UItemStashComponent::Changed()
{
	++Revision;
	OnChanged.Broadcast();
}

ELootItem ItemStash::ToLootItem(ETransferItem Item)
{
	switch (Item)
	{
	case ETransferItem::Turret: return ELootItem::Turret;
	case ETransferItem::Barricade: return ELootItem::Barricade;
	case ETransferItem::Mine: return ELootItem::Mine;
	case ETransferItem::Medkit: return ELootItem::Medkit;
	case ETransferItem::RifleAmmo: return ELootItem::RifleAmmo;
	case ETransferItem::PistolAmmo: return ELootItem::PistolAmmo;
	case ETransferItem::CannedFood: return ELootItem::CannedFood;
	case ETransferItem::ShotgunAmmo: return ELootItem::ShotgunAmmo;
	case ETransferItem::Bread: return ELootItem::Bread;
	case ETransferItem::FlameFuel: return ELootItem::FlameFuel;
	case ETransferItem::Chocolate: return ELootItem::Chocolate;
	case ETransferItem::CryoAmmo: return ELootItem::CryoAmmo;
	case ETransferItem::Matches: return ELootItem::Matches;
	default: return ELootItem::PlasmaAmmo;
	}
}

bool ItemStash::FromLootItem(ELootItem Loot, ETransferItem& OutItem)
{
	switch (Loot)
	{
	case ELootItem::Medkit: OutItem = ETransferItem::Medkit; return true;
	case ELootItem::CannedFood: OutItem = ETransferItem::CannedFood; return true;
	case ELootItem::Bread: OutItem = ETransferItem::Bread; return true;
	case ELootItem::Chocolate: OutItem = ETransferItem::Chocolate; return true;
	case ELootItem::Matches: OutItem = ETransferItem::Matches; return true;
	case ELootItem::RifleAmmo: OutItem = ETransferItem::RifleAmmo; return true;
	case ELootItem::PistolAmmo: OutItem = ETransferItem::PistolAmmo; return true;
	case ELootItem::ShotgunAmmo: OutItem = ETransferItem::ShotgunAmmo; return true;
	case ELootItem::FlameFuel: OutItem = ETransferItem::FlameFuel; return true;
	case ELootItem::CryoAmmo: OutItem = ETransferItem::CryoAmmo; return true;
	case ELootItem::PlasmaAmmo: OutItem = ETransferItem::PlasmaAmmo; return true;
	case ELootItem::Turret: OutItem = ETransferItem::Turret; return true;
	case ELootItem::Barricade: OutItem = ETransferItem::Barricade; return true;
	case ELootItem::Mine: OutItem = ETransferItem::Mine; return true;
	default: return false;
	}
}
