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
	case EPersonalItem::Medkit: return LOCTEXT("Medkit", "Medkit");
	case EPersonalItem::CannedFood: return LOCTEXT("Can", "Canned food");
	case EPersonalItem::Bread: return LOCTEXT("Bread", "Bread");
	default: return LOCTEXT("Chocolate", "Chocolate");
	}
}

FText PersonalItemRules::GetMissingName(EPersonalItem Item)
{
	switch (Item)
	{
	case EPersonalItem::Medkit: return LOCTEXT("NoMedkit", "medkits");
	case EPersonalItem::CannedFood: return LOCTEXT("NoCan", "canned food");
	case EPersonalItem::Bread: return LOCTEXT("NoBread", "bread");
	default: return LOCTEXT("NoChocolate", "chocolate");
	}
}

#undef LOCTEXT_NAMESPACE
