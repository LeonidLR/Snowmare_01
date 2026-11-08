#include "Data/WeaponLoadoutRules.h"

#include "Data/WeaponRoster.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace WeaponLoadoutRulesInternal
{
	static TMap<FString, TArray<FString>>& LoadoutTable()
	{
		static TMap<FString, TArray<FString>> Table;
		return Table;
	}
}

namespace WeaponLoadoutRules
{
	TMap<FString, TArray<FString>> Parse(const FJsonObject& LoadoutsBlock, TArray<FString>* OutErrors)
	{
		TMap<FString, TArray<FString>> Out;
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : LoadoutsBlock.Values)
		{
			const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
			if (Entry.Key == TEXT("comment") || !Entry.Value.IsValid() || !Entry.Value->TryGetArray(Array) || !Array)
			{
				if (Entry.Key != TEXT("comment") && OutErrors)
				{
					OutErrors->Add(FString::Printf(TEXT("loadouts.%s: expected an array of weapon ids"), *Entry.Key));
				}
				continue;
			}
			TArray<FString>& Ids = Out.Add(Entry.Key);
			for (const TSharedPtr<FJsonValue>& Value : *Array)
			{
				FString Id;
				if (Value.IsValid() && Value->Type == EJson::String && Value->TryGetString(Id) && !Id.IsEmpty())
				{
					Ids.AddUnique(Id);
				}
				else if (OutErrors)
				{
					OutErrors->Add(FString::Printf(TEXT("loadouts.%s: every entry must be a weapon id string"), *Entry.Key));
				}
			}
		}
		return Out;
	}

	void Set(const TMap<FString, TArray<FString>>& Loadouts)
	{
		WeaponLoadoutRulesInternal::LoadoutTable() = Loadouts;
	}

	const TMap<FString, TArray<FString>>& Get()
	{
		return WeaponLoadoutRulesInternal::LoadoutTable();
	}

	bool Find(const FString& RoleKey, TArray<FString>& OutWeaponIds)
	{
		const TArray<FString>* Ids = Get().Find(RoleKey);
		if (!Ids)
		{
			return false;
		}
		OutWeaponIds.Reset();
		for (const FString& Id : *Ids)
		{
			if (!WeaponRoster::IsOutOfPlay(Id))
			{
				OutWeaponIds.Add(Id);
			}
		}
		return true;
	}

	FString RoleKey(const FString& RoleEnumName)
	{
		FString Out;
		for (int32 Index = 0; Index < RoleEnumName.Len(); ++Index)
		{
			const TCHAR Char = RoleEnumName[Index];
			if (FChar::IsUpper(Char) && Index > 0)
			{
				Out.AppendChar(TEXT('_'));
			}
			Out.AppendChar(FChar::ToLower(Char));
		}
		return Out;
	}

	FString WeaponAssetPath(const FString& WeaponId)
	{
		return FString::Printf(TEXT("/Game/Data/Weapons/DA_Weapon_%s.DA_Weapon_%s"), *WeaponId, *WeaponId);
	}

	FString ResolveHeldWeapon(const FString& Saved, const TArray<FString>& Arsenal)
	{
		if (!Saved.IsEmpty() && Arsenal.Contains(Saved))
		{
			return Saved;
		}
		for (const FString& Id : Arsenal)
		{
			if (Id != TEXT("grenade"))
			{
				return Id;
			}
		}
		return Arsenal.IsEmpty() ? FString() : Arsenal[0];
	}
}
