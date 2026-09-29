#include "Characters/PersonalItemRules.h"

#define LOCTEXT_NAMESPACE "PersonalItemRules"

FPersonalItemEffect PersonalItemRules::GetEffect(EPersonalItem Item)
{
	switch (Item)
	{
	case EPersonalItem::Medkit: return { 80.f, 0.f };
	case EPersonalItem::CannedFood: return { 45.f, 25.f };
	case EPersonalItem::Bread: return { 30.f, 15.f };
	default: return { 20.f, 10.f };
	}
}

bool PersonalItemRules::CanUse(float Health, float MaxHealth, float Cold)
{
	return !(Health >= MaxHealth && Cold <= 0.f);
}

FText PersonalItemRules::GetName(EPersonalItem Item)
{
	switch (Item)
	{
	case EPersonalItem::Medkit: return LOCTEXT("Medkit", "Аптечка");
	case EPersonalItem::CannedFood: return LOCTEXT("Can", "Консервы");
	case EPersonalItem::Bread: return LOCTEXT("Bread", "Хлеб");
	default: return LOCTEXT("Chocolate", "Шоколад");
	}
}

FText PersonalItemRules::GetMissingName(EPersonalItem Item)
{
	switch (Item)
	{
	case EPersonalItem::Medkit: return LOCTEXT("NoMedkit", "аптечек");
	case EPersonalItem::CannedFood: return LOCTEXT("NoCan", "консервов");
	case EPersonalItem::Bread: return LOCTEXT("NoBread", "хлеба");
	default: return LOCTEXT("NoChocolate", "шоколада");
	}
}

#undef LOCTEXT_NAMESPACE
