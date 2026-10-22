#pragma once

#include "CoreMinimal.h"
#include "Interactables/DeployableRules.h"
#include "CampaignCarry.generated.h"

class AOperativeCharacter;

/** Clip / reserve of one carried weapon. */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FCarriedAmmo
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Campaign")
	FString WeaponId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Campaign")
	int32 Clip = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Campaign")
	int32 Reserve = 0;
};

/**
 * One operative as he left the previous level (2026-10-22 b, the campaign carry-over): progression (level, EXP, unspent points, the
 * four stats with the points already spent in them) and the kit (items, ammo, the weapon in hands). Health is NOT carried: he
 * arrives at full health of his carried maximum; cold, frostbite, orders, cover, postures start fresh (a new level).
 */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FCarriedOperative
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Campaign")
	EOperativeRole Role = EOperativeRole::Commander;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Campaign")
	int32 SquadIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Campaign")
	FString DisplayName;

	/** Fell on the previous level: he does not come along (the roster row is not spawned). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Campaign")
	bool bKilledInAction = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Campaign")
	int32 Level = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Campaign")
	int32 CurrentExp = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Campaign")
	int32 UnspentStatPoints = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Campaign")
	float MaxHealth = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Campaign")
	float Luck = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Campaign")
	float Accuracy = 90.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Campaign")
	float Fortitude = 15.f;

	/** Item counts by the save system's keys (matches_count, medkits_count, ... - CampaignCarry::ItemKeys). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Campaign")
	TMap<FString, int32> Items;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Campaign")
	float StoveGasSeconds = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Campaign")
	TArray<FString> BonusItems;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Campaign")
	TArray<FCarriedAmmo> Ammo;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Campaign")
	TMap<FName, int32> ExtraAmmo;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Campaign")
	FString CurrentWeaponId;
};

/** The squad handed from one campaign level to the next. */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FCampaignSquadCarry
{
	GENERATED_BODY()

	/** Short map names (CampaignRules::ShortMapName). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Campaign")
	FString FromMap;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Campaign")
	FString ToMap;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Campaign")
	TArray<FCarriedOperative> Members;

	bool IsSet() const { return !ToMap.IsEmpty(); }
};

/**
 * Capture / apply of the campaign carry-over (UCampaignSubsystem holds it between the levels). Rules (2026-10-22 b, no Godot
 * reference - the archive had no level-to-level flow; the save system's per-operative keys are the model): carried = level, EXP,
 * unspent points, max health / luck / accuracy / fortitude, every item count, bonus items, clip + reserve per weapon, extra ammo,
 * the weapon in hands; NOT carried = current health (full on arrival), cold / frostbite / frozen weapon, cover, orders, postures,
 * guard. The killed in action stay behind. Members are matched by role (one per role in the roster), else by squad index.
 */
namespace CampaignCarry
{
	/** The item keys an FCarriedOperative::Items map uses (the save system's names). */
	CODEXTACTICS_API TArray<FString> ItemKeys();

	/** Snapshot of one operative as he leaves the level. */
	CODEXTACTICS_API FCarriedOperative Capture(const AOperativeCharacter& Operative);

	/**
	 * Puts a carried record on a freshly spawned operative of the new level (after his arsenal is set up): progression, kit, the weapon
	 * in hands; full health of the carried maximum, no cold; his progression bases are re-captured (the "-" buttons stop at the
	 * arrival values: points spent on an earlier level stay spent).
	 */
	CODEXTACTICS_API void Apply(AOperativeCharacter& Operative, const FCarriedOperative& Carried);

	/** The record for a roster member: same role first (unique roles), else the same squad index; nullptr = none (spawn fresh). */
	CODEXTACTICS_API const FCarriedOperative* FindFor(const FCampaignSquadCarry& Carry, EOperativeRole Role, int32 SquadIndex);

	/** A roster member is spawned on the new level: always without a carry; with one, unless his record says killed in action. */
	CODEXTACTICS_API bool ShouldSpawn(const FCampaignSquadCarry& Carry, EOperativeRole Role, int32 SquadIndex);
}
