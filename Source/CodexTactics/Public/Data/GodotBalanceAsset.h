#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GodotBalanceAsset.generated.h"

/**
 * Every numeric / bool export of a Godot GameBalanceConfig resource, by its Godot name (bools as 0 / 1). Filled by
 * Scripts/Editor/import_balance.py from resources/balance.tres (DA_Balance — what the turn-based manager loads) or
 * resources/game_balance_config.tres (DA_GameBalanceConfig — camera, enemies, deployables); a field missing in the
 * .tres carries the game_balance_config.gd default. Systems read their values with GetNumber, never hand-typed copies.
 * The two game balance assets are UGameBalanceConfig (typed fields per Godot export); GetNumber reads a numeric / bool
 * field of the asset's class with that name first, then Numbers (keys without a field, enemy anim configs).
 * Godot reference: resources/game_balance_config.gd.
 */
UCLASS(BlueprintType)
class CODEXTACTICS_API UGodotBalanceAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Godot file the values came from (res:// path). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance")
	FString SourceFile;

	/** Values without a typed field of their own (by Godot name). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance")
	TMap<FName, float> Numbers;

	/** Value of a Godot export, or Fallback when the resource does not have it. */
	UFUNCTION(BlueprintPure, Category = "Balance")
	float GetNumber(FName Key, float Fallback) const;

	/** Writes a value: the typed field with that name when the class has one, Numbers otherwise. */
	UFUNCTION(BlueprintCallable, Category = "Balance")
	void SetNumber(FName Key, float Value);

	/** True when the asset's class has a typed numeric / bool field for Key. */
	UFUNCTION(BlueprintPure, Category = "Balance")
	bool HasField(FName Key) const;

	int32 GetInt(FName Key, int32 Fallback) const { return FMath::RoundToInt(GetNumber(Key, static_cast<float>(Fallback))); }
};
