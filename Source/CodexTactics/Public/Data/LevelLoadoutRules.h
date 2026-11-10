#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

class AOperativeCharacter;

/**
 * Pure rules of the per-level weapon loadouts / starting kit (level JSON "loadouts" / "kit", Training Ground pass 1, 2026-11-09).
 * Key resolution: the operative id "slot_<n>" (0-based roster SquadIndex) first, then the role key (WeaponLoadoutRules::RoleKey), else none.
 * UE-only (no Godot reference). Tested in CodexTactics.Data.LevelJson.Tg*.
 */
namespace LevelLoadoutRules
{
	/** A JSON key / weapon id as stored and looked up: trimmed, lower case. */
	CODEXTACTICS_API FString NormalizeKey(const FString& Key);

	/** The operative id of a roster slot: "slot_<SquadIndex>"; empty for a negative index. */
	CODEXTACTICS_API FString MakeOperativeId(int32 SquadIndex);

	/** MakeOperativeId(Operative.SquadIndex). */
	CODEXTACTICS_API FString OperativeIdFor(const AOperativeCharacter& Operative);

	/** The normalized role key of the operative's SquadRole ("commander", "engineer", "medic_sapper", "recruit"). */
	CODEXTACTICS_API FString RoleKeyFor(const AOperativeCharacter& Operative);

	/** The entry for an operative: OperativeId's entry, else RoleKey's, else nullptr (= today's behaviour). */
	template <typename ValueType>
	const ValueType* FindPerOperative(const TMap<FString, ValueType>& Entries, const FString& OperativeId, const FString& RoleKey)
	{
		if (const ValueType* ById = OperativeId.IsEmpty() ? nullptr : Entries.Find(NormalizeKey(OperativeId)))
		{
			return ById;
		}
		return RoleKey.IsEmpty() ? nullptr : Entries.Find(NormalizeKey(RoleKey));
	}

	/**
	 * Splits a level loadout into the weapon ids IsKnown accepts (order kept, duplicates dropped) and the unknown ones (to warn about).
	 * True when at least one id is known; false = fall back to the global loadout.
	 */
	CODEXTACTICS_API bool FilterKnownWeapons(const TArray<FString>& LevelIds, TFunctionRef<bool(const FString&)> IsKnown,
		TArray<FString>& OutKnown, TArray<FString>& OutUnknown);

	/** A kit count onto the current value: Wanted < 0 keeps Current, else Wanted clamped to [0, MaxValue]. */
	CODEXTACTICS_API int32 ApplyKitCount(int32 Current, int32 Wanted, int32 MaxValue);
}
