#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LandscapeAuthoringLibrary.generated.h"

class ALandscape;
class UMaterialInterface;
class UWorld;

/**
 * Editor helpers for the landscape material work (environment §13, docs/systems/environment-weather.md): UE 5.8 has no Python API that
 * creates a landscape with a height field, so Scripts/Editor/create_snow_terrain_test_map.py builds the test terrain through
 * CreateLandscapeFromHeights; Scripts/Editor/landscape_snow_patch.py reads the shader platform its stats are compiled for.
 * No Godot reference (new Unreal-only tooling).
 */
UCLASS()
class ULandscapeAuthoringLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Spawns a landscape in World with the height field HeightsCm (world centimetres relative to Location.Z, row-major, X fastest) of
	 * (ComponentsX * QuadsPerComponent + 1) x (ComponentsY * QuadsPerComponent + 1) vertices, one section per component. Location is the
	 * landscape corner (vertex 0, 0); Scale is the actor scale (X / Y = vertex spacing in cm, Z = height scale, 100 = +-256 m).
	 * Returns nullptr (with a log error) when the input does not match. QuadsPerComponent must be a valid section size (7, 15, 31, 63, 127, 255).
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Landscape")
	static ALandscape* CreateLandscapeFromHeights(UWorld* World, FVector Location, FVector Scale, int32 ComponentsX, int32 ComponentsY,
		int32 QuadsPerComponent, const TArray<float>& HeightsCm, UMaterialInterface* Material);

	/** The landscape height value (uint16) that stores HeightCm at a Z scale of ScaleZ (32768 = 0; clamped to the 16-bit range). */
	static uint16 EncodeHeight(float HeightCm, float ScaleZ);

	/** Name of GMaxRHIShaderPlatform ("PCD3D_SM6" with a D3D12 RHI): the platform UMaterialEditingLibrary compiles stats / errors for. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Materials")
	static FString GetMaxShaderPlatformName();
};
