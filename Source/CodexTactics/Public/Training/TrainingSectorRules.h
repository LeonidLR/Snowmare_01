#pragma once

#include "AI/PmcEncounterRules.h"
#include "CoreMinimal.h"

class FJsonObject;

struct CODEXTACTICS_API FTrainingSectorStart
{
	FVector Position = FVector::ZeroVector;
	double Yaw = 0.0;

	FTransform Transform() const { return FTransform(FRotator(0.0, Yaw, 0.0), Position); }
};

struct CODEXTACTICS_API FTrainingSector
{
	int32 Id = 0;
	FBox2D Area{ForceInit};
	FVector2D Heights = FVector2D::ZeroVector;
	TArray<FTrainingSectorStart> Starts;
	TArray<FPmcEncounterPost> Posts;
	FPmcEncounterSpec Squad;
};

namespace TrainingSectorRules
{
	inline constexpr const TCHAR* MapName = TEXT("L_Gen_Depot_1_X6");
	inline constexpr double MinSizeCm = 6000.0;

	// Returns valid sectors only. Duplicate ids and malformed entries are reported.
	CODEXTACTICS_API TArray<FTrainingSector> ParseSectors(const FJsonObject& Json, TArray<FString>& Errors);
	CODEXTACTICS_API TOptional<int32> ParseId(const FString& Text);
	CODEXTACTICS_API int32 SavedId(const FJsonObject& Json);
	CODEXTACTICS_API FVector Clamp(const FBox2D& Box, const FVector& Point, double Inset = 0.0);
	CODEXTACTICS_API bool Contains(const FBox2D& Box, const FVector& Point);
	CODEXTACTICS_API void Pick(const FTrainingSector& Sector, int32 Seed, FTrainingSectorStart& Start, FPmcEncounterSpec& Squad);
}
