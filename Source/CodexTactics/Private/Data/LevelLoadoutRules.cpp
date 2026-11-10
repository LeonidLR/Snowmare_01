#include "Data/LevelLoadoutRules.h"
#include "Characters/OperativeCharacter.h"
#include "Data/WeaponLoadoutRules.h"

FString LevelLoadoutRules::NormalizeKey(const FString& Key)
{
	return Key.TrimStartAndEnd().ToLower();
}

FString LevelLoadoutRules::MakeOperativeId(int32 SquadIndex)
{
	return SquadIndex >= 0 ? FString::Printf(TEXT("slot_%d"), SquadIndex) : FString();
}

FString LevelLoadoutRules::OperativeIdFor(const AOperativeCharacter& Operative)
{
	return MakeOperativeId(Operative.SquadIndex);
}

FString LevelLoadoutRules::RoleKeyFor(const AOperativeCharacter& Operative)
{
	const UEnum* RoleEnum = StaticEnum<EOperativeRole>();
	return RoleEnum ? NormalizeKey(WeaponLoadoutRules::RoleKey(RoleEnum->GetNameStringByValue(static_cast<int64>(Operative.SquadRole)))) : FString();
}

bool LevelLoadoutRules::FilterKnownWeapons(const TArray<FString>& LevelIds, TFunctionRef<bool(const FString&)> IsKnown,
	TArray<FString>& OutKnown, TArray<FString>& OutUnknown)
{
	OutKnown.Reset();
	OutUnknown.Reset();
	for (const FString& Raw : LevelIds)
	{
		const FString Id = NormalizeKey(Raw);
		if (Id.IsEmpty())
		{
			continue;
		}
		if (IsKnown(Id))
		{
			OutKnown.AddUnique(Id);
		}
		else
		{
			OutUnknown.AddUnique(Id);
		}
	}
	return OutKnown.Num() > 0;
}

int32 LevelLoadoutRules::ApplyKitCount(int32 Current, int32 Wanted, int32 MaxValue)
{
	return Wanted < 0 ? Current : FMath::Clamp(Wanted, 0, FMath::Max(MaxValue, 0));
}
