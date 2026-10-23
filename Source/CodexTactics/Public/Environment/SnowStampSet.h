#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Environment/SnowInteractionTypes.h"
#include "SnowStampSet.generated.h"

/**
 * The stamp tuning of the interactive snow (phase 1, UE-only; docs/systems/environment-weather.md §14):
 * /Game/Environment/Weather/Snow/Tracks/DA_SnowStamps (created with the C++ defaults by Scripts/Editor/create_snow_interaction_assets.py,
 * then tuned by hand). USnowInteractionSubsystem reads it at world begin; without the asset it uses the same C++ defaults (tests never
 * need the asset). Visual only: nothing in the game reads it.
 */
UCLASS(BlueprintType)
class CODEXTACTICS_API USnowStampSet : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Shape / size / depth / berm / spacing of every stamp kind. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Snow")
	FSnowStampTable Stamps;

	/** Window, fade, polling. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Snow")
	FSnowInteractionTuning Tuning;
};
