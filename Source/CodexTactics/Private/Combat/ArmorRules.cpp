#include "Combat/ArmorRules.h"

#include "Data/WeaponStatsRules.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace ArmorRulesInternal
{
	static TMap<FString, FBodyArmor>& BodyArmorTable()
	{
		static TMap<FString, FBodyArmor> Table;
		return Table;
	}
}

namespace ArmorRules
{
	bool PenetrationApplies(EArmorClass WeaponClass, EArmorClass TargetClass)
	{
		return static_cast<uint8>(WeaponClass) >= static_cast<uint8>(TargetClass);
	}

	float EffectiveReduction(float BaseReduction, bool bShredded, float Penetration, EArmorClass WeaponClass, EArmorClass TargetClass)
	{
		float EffectiveArmor = BaseReduction;
		if (bShredded)
		{
			EffectiveArmor = FMath::Max(0.0f, EffectiveArmor * 0.3f);
		}
		if (PenetrationApplies(WeaponClass, TargetClass))
		{
			EffectiveArmor *= (1.0f - FMath::Clamp(Penetration, 0.0f, 1.0f));
		}
		return EffectiveArmor;
	}

	EArmorClass ClassFromTier(EArmorTier Tier)
	{
		switch (Tier)
		{
		case EArmorTier::Medium: return EArmorClass::Medium;
		case EArmorTier::Heavy: return EArmorClass::Heavy;
		default: return EArmorClass::Light;
		}
	}

	FString BodyKey(EEnemyArchetype Archetype)
	{
		switch (Archetype)
		{
		case EEnemyArchetype::FrostHound: return TEXT("frost_hound");
		case EEnemyArchetype::Spitter: return TEXT("spitter");
		case EEnemyArchetype::Brute: return TEXT("brute");
		case EEnemyArchetype::Frostbitten: return TEXT("frostbitten");
		case EEnemyArchetype::Cutter: return TEXT("cutter");
		case EEnemyArchetype::CryoDrone: return TEXT("cryo_drone");
		case EEnemyArchetype::Marksman: return TEXT("marksman");
		default: return TEXT("base");
		}
	}

	int32 ParseBodies(const FJsonObject& ArmorBlock, TMap<FString, FBodyArmor>& OutBodies, TArray<FString>* OutErrors)
	{
		const TSharedPtr<FJsonObject>* Bodies = nullptr;
		if (!ArmorBlock.TryGetObjectField(TEXT("bodies"), Bodies) || !Bodies || !Bodies->IsValid())
		{
			return 0;
		}
		int32 Read = 0;
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Bodies)->Values)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			FString ClassName;
			EArmorClass Class = EArmorClass::Light;
			const TSharedPtr<FJsonValue>* Reduction = nullptr;
			if (!Entry.Value.IsValid() || !Entry.Value->TryGetObject(Object) || !Object || !(*Object)->TryGetStringField(TEXT("class"), ClassName)
				|| !WeaponStatsRules::ParseArmorClass(ClassName, Class) || !(Reduction = (*Object)->Values.Find(TEXT("reduction")))
				|| !Reduction->IsValid() || (*Reduction)->Type != EJson::Number)
			{
				if (OutErrors)
				{
					OutErrors->Add(FString::Printf(TEXT("armor.bodies.%s: expected {\"class\": none / light / medium / heavy / vehicle, \"reduction\": number}"), *Entry.Key));
				}
				continue;
			}
			const float Value = static_cast<float>((*Reduction)->AsNumber());
			if (Value < 0.f || Value >= 1.f)
			{
				if (OutErrors)
				{
					OutErrors->Add(FString::Printf(TEXT("armor.bodies.%s: reduction %.2f is outside [0, 1)"), *Entry.Key, Value));
				}
				continue;
			}
			OutBodies.Add(Entry.Key, FBodyArmor{ Class, Value });
			++Read;
		}
		return Read;
	}

	void SetBodies(const TMap<FString, FBodyArmor>& Bodies)
	{
		ArmorRulesInternal::BodyArmorTable() = Bodies;
	}

	const TMap<FString, FBodyArmor>& GetBodies()
	{
		return ArmorRulesInternal::BodyArmorTable();
	}

	bool ApplyBody(const FString& Key, FBodyArmor& InOut)
	{
		if (const FBodyArmor* Found = ArmorRulesInternal::BodyArmorTable().Find(Key))
		{
			InOut = *Found;
			return true;
		}
		return false;
	}
}
