#include "Characters/TransferRules.h"
#include "Characters/OperativeCharacter.h"
#include "Interactables/DeployableRules.h"

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
		case ETransferItem::Turret: return TEXT("Турель");
		case ETransferItem::Barricade: return TEXT("Баррикада");
		case ETransferItem::Mine: return TEXT("Мина");
		case ETransferItem::Medkit: return TEXT("Аптечка");
		case ETransferItem::CannedFood: return TEXT("Консервы");
		case ETransferItem::Bread: return TEXT("Хлеб");
		case ETransferItem::Chocolate: return TEXT("Шоколад");
		case ETransferItem::Matches: return TEXT("Спички");
		case ETransferItem::RifleAmmo: return TEXT("Патроны M16");
		case ETransferItem::PistolAmmo: return TEXT("Патроны 9мм");
		case ETransferItem::ShotgunAmmo: return TEXT("Дробь 12k");
		case ETransferItem::FlameFuel: return TEXT("Топливо");
		case ETransferItem::CryoAmmo: return TEXT("Хладагент");
		default: return TEXT("Плазма");
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
		Result.Feedback = FString::Printf(TEXT("+1 %s"), TransferFeedbackName(Item));
	}
	return Result;
}

FString TransferRules::GetPromptName(ETransferItem Item)
{
	switch (Item)
	{
	case ETransferItem::Medkit: return TEXT("Аптечку");
	case ETransferItem::RifleAmmo: return TEXT("Патроны M16 (30 шт.)");
	case ETransferItem::PistolAmmo: return TEXT("Патроны 9мм (12 шт.)");
	case ETransferItem::ShotgunAmmo: return TEXT("Дробь 12k (8 шт.)");
	case ETransferItem::FlameFuel: return TEXT("Топливо огнемёта (25 ед.)");
	case ETransferItem::CryoAmmo: return TEXT("Хладагент крио (15 ед.)");
	case ETransferItem::PlasmaAmmo: return TEXT("Батареи плазмы (10 ед.)");
	default: return TransferFeedbackName(Item);
	}
}
