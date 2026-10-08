#include "Characters/TransferRules.h"
#include "Characters/OperativeCharacter.h"
#include "Interactables/DeployableRules.h"
#include "Interactables/ItemStashComponent.h"

namespace
{
	int32* TransferCountField(AOperativeCharacter& Operative, ETransferItem Item)
	{
		switch (Item)
		{
		case ETransferItem::Medkit: return &Operative.MedkitsCount;
		case ETransferItem::CannedFood: return &Operative.CannedFoodCount;
		case ETransferItem::Bread: return &Operative.BreadCount;
		case ETransferItem::Chocolate: return &Operative.ChocolateCount;
		case ETransferItem::Matches: return &Operative.MatchesCount;
		default: return nullptr;
		}
	}

	bool TransferDeployableType(ETransferItem Item, EDeployableType& OutType)
	{
		switch (Item)
		{
		case ETransferItem::Turret: OutType = EDeployableType::Turret; return true;
		case ETransferItem::Barricade: OutType = EDeployableType::Barricade; return true;
		case ETransferItem::Mine: OutType = EDeployableType::Mine; return true;
		default: return false;
		}
	}

	const TCHAR* TransferFeedbackName(ETransferItem Item)
	{
		switch (Item)
		{
		case ETransferItem::Turret: return TEXT("Turret");
		case ETransferItem::Barricade: return TEXT("Barricade");
		case ETransferItem::Mine: return TEXT("Mine");
		case ETransferItem::Medkit: return TEXT("Medkit");
		case ETransferItem::CannedFood: return TEXT("Canned food");
		case ETransferItem::Bread: return TEXT("Bread");
		case ETransferItem::Chocolate: return TEXT("Chocolate");
		case ETransferItem::Matches: return TEXT("Matches");
		case ETransferItem::RifleAmmo: return TEXT("M16 rounds");
		case ETransferItem::PistolAmmo: return TEXT("9mm rounds");
		case ETransferItem::ShotgunAmmo: return TEXT("12g shells");
		case ETransferItem::FlameFuel: return TEXT("Fuel");
		case ETransferItem::CryoAmmo: return TEXT("Coolant");
		default: return TEXT("Plasma");
		}
	}
}

FString TransferRules::GetAmmoWeaponId(ETransferItem Item)
{
	switch (Item)
	{
	case ETransferItem::RifleAmmo: return TEXT("m16");
	case ETransferItem::PistolAmmo: return TEXT("pistol");
	case ETransferItem::ShotgunAmmo: return TEXT("shotgun");
	case ETransferItem::FlameFuel: return TEXT("flamethrower");
	case ETransferItem::CryoAmmo: return TEXT("cryo_emitter");
	case ETransferItem::PlasmaAmmo: return TEXT("plasma_carbine");
	default: return FString();
	}
}

int32 TransferRules::GetAmmoPack(ETransferItem Item)
{
	switch (Item)
	{
	case ETransferItem::RifleAmmo: return 30;
	case ETransferItem::PistolAmmo: return 12;
	case ETransferItem::ShotgunAmmo: return 8;
	case ETransferItem::FlameFuel: return 25;
	case ETransferItem::CryoAmmo: return 15;
	case ETransferItem::PlasmaAmmo: return 10;
	default: return 0;
	}
}

int32 TransferRules::GetAvailable(const AOperativeCharacter& Sender, ETransferItem Item)
{
	EDeployableType Type;
	if (TransferDeployableType(Item, Type))
	{
		return Sender.GetDeployableCount(Type);
	}
	const FString WeaponId = GetAmmoWeaponId(Item);
	if (!WeaponId.IsEmpty())
	{
		return Sender.GetReserve(WeaponId);
	}
	const int32* Count = TransferCountField(const_cast<AOperativeCharacter&>(Sender), Item);
	return Count ? *Count : 0;
}

FTransferResult TransferRules::Transfer(AOperativeCharacter& Sender, AOperativeCharacter& Recipient, ETransferItem Item)
{
	FTransferResult Result;
	EDeployableType Type;
	if (TransferDeployableType(Item, Type))
	{
		if (Sender.GetDeployableCount(Type) <= 0)
		{
			return Result;
		}
		if (Recipient.GetDeployableCount(Type) >= DeployableRules::GetMaxCarried(Type))
		{
			Result.bRecipientFull = true;
			return Result;
		}
		Sender.AddDeployable(Type, -1);
		Recipient.AddDeployable(Type, 1);
		Result.bDone = true;
		Result.Moved = 1;
		Result.Feedback = FString::Printf(TEXT("+1 %s"), TransferFeedbackName(Item));
		return Result;
	}
	const FString WeaponId = GetAmmoWeaponId(Item);
	if (!WeaponId.IsEmpty())
	{
		const int32 Taken = Sender.TakeReserve(WeaponId, GetAmmoPack(Item));
		if (Taken > 0)
		{
			Recipient.AddAmmo(WeaponId, Taken);
			Result.bDone = true;
			Result.Moved = Taken;
			Result.Feedback = FString::Printf(TEXT("+%d %s"), Taken, TransferFeedbackName(Item));
		}
		return Result;
	}
	int32* From = TransferCountField(Sender, Item);
	int32* To = TransferCountField(Recipient, Item);
	if (From && To && *From > 0)
	{
		--*From;
		++*To;
		Result.bDone = true;
		Result.Moved = 1;
		Result.Feedback = FString::Printf(TEXT("+1 %s"), TransferFeedbackName(Item));
	}
	return Result;
}

FString TransferRules::GetPromptName(ETransferItem Item)
{
	switch (Item)
	{
	case ETransferItem::Medkit: return TEXT("Medkit");
	case ETransferItem::RifleAmmo: return TEXT("M16 rounds (x30)");
	case ETransferItem::PistolAmmo: return TEXT("9mm rounds (x12)");
	case ETransferItem::ShotgunAmmo: return TEXT("12g shells (x8)");
	case ETransferItem::FlameFuel: return TEXT("Flamethrower fuel (25 u)");
	case ETransferItem::CryoAmmo: return TEXT("Cryo coolant (15 u)");
	case ETransferItem::PlasmaAmmo: return TEXT("Plasma cells (10 u)");
	default: return TransferFeedbackName(Item);
	}
}

bool TransferRules::IsAmmoItem(ETransferItem Item)
{
	return !GetAmmoWeaponId(Item).IsEmpty();
}

int32 TransferRules::GetItemQuantityStep(ETransferItem Item)
{
	return IsAmmoItem(Item) ? 5 : 1;
}

int32 TransferRules::GetMinQuantity(ETransferItem Item, int32 MaxQuantity)
{
	return MaxQuantity <= 0 ? 0 : FMath::Min(GetItemQuantityStep(Item), MaxQuantity);
}

int32 TransferRules::QuantizeQuantity(ETransferItem Item, int32 Desired, int32 MaxQuantity)
{
	if (MaxQuantity <= 0)
	{
		return 0;
	}
	if (Desired >= MaxQuantity)
	{
		return MaxQuantity;
	}
	const int32 Step = GetItemQuantityStep(Item);
	return FMath::Max(GetMinQuantity(Item, MaxQuantity), (FMath::Max(Desired, 0) / Step) * Step);
}

int32 TransferRules::StepQuantity(ETransferItem Item, int32 Current, int32 Direction, int32 MaxQuantity)
{
	const int32 Step = GetItemQuantityStep(Item);
	if (Direction > 0)
	{
		return QuantizeQuantity(Item, (FMath::Max(Current, 0) / Step + 1) * Step, MaxQuantity);
	}
	if (Direction < 0)
	{
		// From an off-grid value (the whole stack) the first [-] lands on the grid below it.
		const int32 Down = Current % Step != 0 ? (Current / Step) * Step : Current - Step;
		return QuantizeQuantity(Item, Down, MaxQuantity);
	}
	return QuantizeQuantity(Item, Current, MaxQuantity);
}

bool TransferRules::NeedsQuantityDialog(ETransferItem Item, int32 MaxQuantity)
{
	return MaxQuantity > GetMinQuantity(Item, MaxQuantity);
}

int32 TransferRules::GetRecipientCapacity(const AOperativeCharacter& Recipient, ETransferItem Item)
{
	EDeployableType Type;
	if (TransferDeployableType(Item, Type))
	{
		return FMath::Max(0, DeployableRules::GetMaxCarried(Type) - Recipient.GetDeployableCount(Type));
	}
	return MAX_int32;
}

int32 TransferRules::GetMaxTransferQuantity(const AOperativeCharacter& Sender, const AOperativeCharacter& Recipient, ETransferItem Item)
{
	return FMath::Max(0, FMath::Min(GetAvailable(Sender, Item), GetRecipientCapacity(Recipient, Item)));
}

bool TransferRules::IsWithinTransferRange(const FVector& SenderLocation, const FVector& RecipientLocation)
{
	return FVector::Dist2D(SenderLocation, RecipientLocation) <= MaxTransferDistance;
}

bool TransferRules::CanTransferTo(const AOperativeCharacter& Sender, const AOperativeCharacter& Recipient)
{
	return &Sender != &Recipient && IsWithinTransferRange(Sender.GetActorLocation(), Recipient.GetActorLocation());
}

ETransferRangeDecision TransferRules::DecideRange(const FTransferRangeContext& Context)
{
	if (Context.Distance <= MaxTransferDistance)
	{
		return ETransferRangeDecision::InRange;
	}
	const bool bBlocked = Context.bInCombat || Context.bTurnBased || Context.bUnderFire || !Context.bCanMove;
	return bBlocked ? ETransferRangeDecision::Blocked : ETransferRangeDecision::Approach;
}

int32 TransferRules::RemoveFromOperative(AOperativeCharacter& Operative, ETransferItem Item, int32 Max)
{
	const int32 Count = FMath::Clamp(Max, 0, GetAvailable(Operative, Item));
	if (Count <= 0)
	{
		return 0;
	}
	EDeployableType Type;
	const FString WeaponId = GetAmmoWeaponId(Item);
	if (TransferDeployableType(Item, Type))
	{
		Operative.AddDeployable(Type, -Count);
		return Count;
	}
	if (!WeaponId.IsEmpty())
	{
		return Operative.TakeReserve(WeaponId, Count);
	}
	int32* Field = TransferCountField(Operative, Item);
	if (!Field)
	{
		return 0;
	}
	*Field -= Count;
	return Count;
}

void TransferRules::AddToOperative(AOperativeCharacter& Operative, ETransferItem Item, int32 Count)
{
	if (Count <= 0)
	{
		return;
	}
	EDeployableType Type;
	const FString WeaponId = GetAmmoWeaponId(Item);
	if (TransferDeployableType(Item, Type))
	{
		Operative.AddDeployable(Type, Count);
	}
	else if (!WeaponId.IsEmpty())
	{
		Operative.AddAmmo(WeaponId, Count);
	}
	else if (int32* Field = TransferCountField(Operative, Item))
	{
		*Field += Count;
	}
}

FTransferResult TransferRules::TransferQuantity(AOperativeCharacter& Sender, AOperativeCharacter& Recipient, ETransferItem Item, int32 Quantity)
{
	FTransferResult Result;
	const int32 Available = GetAvailable(Sender, Item);
	if (Quantity <= 0 || Available <= 0)
	{
		return Result;
	}
	const int32 Capacity = GetRecipientCapacity(Recipient, Item);
	if (Capacity <= 0)
	{
		Result.bRecipientFull = true;
		return Result;
	}
	const int32 Wanted = FMath::Min(Quantity, Available);
	const int32 Count = FMath::Min(Wanted, Capacity);
	Result.bClampedByCapacity = Count < Wanted;
	Result.Moved = RemoveFromOperative(Sender, Item, Count);
	AddToOperative(Recipient, Item, Result.Moved);
	Result.bDone = Result.Moved > 0;
	if (Result.bDone)
	{
		Result.Feedback = FString::Printf(TEXT("+%d %s"), Result.Moved, TransferFeedbackName(Item));
	}
	return Result;
}

FTransferResult TransferRules::StoreInStash(AOperativeCharacter& Sender, UItemStashComponent& Stash, ETransferItem Item, int32 Quantity)
{
	FTransferResult Result;
	const int32 Wanted = FMath::Min(Quantity, GetAvailable(Sender, Item));
	if (Wanted <= 0)
	{
		return Result;
	}
	const int32 Free = Stash.GetFreeSpace();
	if (Free <= 0)
	{
		Result.bRecipientFull = true;
		return Result;
	}
	const int32 Count = FMath::Min(Wanted, Free);
	Result.bClampedByCapacity = Count < Wanted;
	const int32 Removed = RemoveFromOperative(Sender, Item, Count);
	Result.Moved = Stash.Add(Item, Removed, true);
	Result.bDone = Result.Moved > 0;
	if (Result.bDone)
	{
		Result.Feedback = FString::Printf(TEXT("%d %s"), Result.Moved, TransferFeedbackName(Item));
	}
	return Result;
}

FTransferResult TransferRules::TakeFromStash(UItemStashComponent& Stash, AOperativeCharacter& Recipient, ETransferItem Item, int32 Quantity)
{
	FTransferResult Result;
	const int32 Wanted = FMath::Min(Quantity, Stash.GetCount(Item));
	if (Wanted <= 0)
	{
		return Result;
	}
	const int32 Capacity = GetRecipientCapacity(Recipient, Item);
	if (Capacity <= 0)
	{
		Result.bRecipientFull = true;
		return Result;
	}
	const int32 Count = FMath::Min(Wanted, Capacity);
	Result.bClampedByCapacity = Count < Wanted;
	Result.Moved = Stash.Take(Item, Count);
	AddToOperative(Recipient, Item, Result.Moved);
	Result.bDone = Result.Moved > 0;
	if (Result.bDone)
	{
		Result.Feedback = FString::Printf(TEXT("%d %s"), Result.Moved, TransferFeedbackName(Item));
	}
	return Result;
}

FString TransferRules::GetItemName(ETransferItem Item)
{
	return TransferFeedbackName(Item);
}
