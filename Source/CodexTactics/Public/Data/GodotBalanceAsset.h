#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GodotBalanceAsset.generated.h"

/**
 * Every numeric / bool export of a Godot GameBalanceConfig resource, by its Godot name (bools as 0 / 1). Filled by
 * Scripts/Editor/import_balance.py from resources/balance.tres (DA_Balance — what the turn-based manager loads) or
 * resources/game_balance_config.tres (DA_GameBalanceConfig — camera, enemies, deployables); a field missing in the
 * .tres carries the game_balance_config.gd default. Systems read their values with GetNumber, never hand-typed copies.
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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Balance")
	TMap<FName, float> Numbers;

	/** Value of a Godot export, or Fallback when the resource does not have it. */
	UFUNCTION(BlueprintPure, Category = "Balance")
	float GetNumber(FName Key, float Fallback) const
	{
		const float* Value = Numbers.Find(Key);
		return Value ? *Value : Fallback;
	}

	int32 GetInt(FName Key, int32 Fallback) const { return FMath::RoundToInt(GetNumber(Key, static_cast<float>(Fallback))); }
};
