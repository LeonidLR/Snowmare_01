#include "Characters/PersonalItemRules.h"

#define LOCTEXT_NAMESPACE "PersonalItemRules"

bool PersonalItemRules::IsFood(EPersonalItem Item)
{
	return Item == EPersonalItem::CannedFood || Item == EPersonalItem::Bread || Item == EPersonalItem::Chocolate;
}

FPersonalItemEffect PersonalItemRules::GetEffect(EPersonalItem Item)
{
	switch (Item)
	{
	case EPersonalItem::Medkit: return { 80.f, 0.f };
	case EPersonalItem::CannedFood: return { 45.f, 25.f };
	case EPersonalItem::Bread: return { 30.f, 15.f };
	case EPersonalItem::HeatPack: return { 0.f, 0.f }; // Sprint 15: TacticalHeatRules::ApplyHeatPack
	default: return { 20.f, 10.f };
	}
}

FPersonalItemEffect PersonalItemRules::GetEffectiveEffect(EPersonalItem Item, CriticalWoundRules::EWoundTier WoundTier)
{
	const FPersonalItemEffect Base = GetEffect(Item);
	if (IsFood(Item) && (WoundTier == CriticalWoundRules::EWoundTier::Heavy || WoundTier == CriticalWoundRules::EWoundTier::Critical))
	{
		return { 0.f, Base.Warmth };
	}
	return Base;
}

bool PersonalItemRules::CanUse(float Health, float MaxHealth, float Cold, EPersonalItem Item, CriticalWoundRules::EWoundTier WoundTier)
{
	if (IsFood(Item) && (WoundTier == CriticalWoundRules::EWoundTier::Heavy || WoundTier == CriticalWoundRules::EWoundTier::Critical))
	{
		return Cold > 0.f;
	}
	return !(Health >= MaxHealth && Cold <= 0.f);
}

FText PersonalItemRules::GetName(EPersonalItem Item)
{
	switch (Item)
	{
	case EPersonalItem::Medkit: return LOCTEXT("Medkit", "Medkit");
	case EPersonalItem::CannedFood: return LOCTEXT("Can", "Canned food");
	case EPersonalItem::Bread: return LOCTEXT("Bread", "Bread");
	case EPersonalItem::HeatPack: return LOCTEXT("HeatPack", "Heat pack");
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
	case EPersonalItem::HeatPack: return LOCTEXT("NoHeatPack", "heat packs");
	default: return LOCTEXT("NoChocolate", "chocolate");
	}
}

const TCHAR* PersonalItemRules::GetEventId(EPersonalItem Item)
{
	switch (Item)
	{
	case EPersonalItem::Medkit: return TEXT("MEDKIT");
	case EPersonalItem::CannedFood: return TEXT("CANNED_FOOD");
	case EPersonalItem::Bread: return TEXT("BREAD");
	case EPersonalItem::Chocolate: return TEXT("CHOCOLATE");
	default: return TEXT("HEAT_PACK");
	}
}

#undef LOCTEXT_NAMESPACE
