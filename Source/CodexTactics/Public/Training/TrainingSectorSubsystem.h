#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Training/TrainingSectorRules.h"
#include "TrainingSectorSubsystem.generated.h"

/** Optional X6 training arena. Absent / sector zero leaves the normal map unchanged. */
UCLASS()
class CODEXTACTICS_API UTrainingSectorSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UTrainingSectorSubsystem* Get(const UWorld* World);
	static FVector LimitGoal(const UWorld* World, const FVector& Point, double Inset = 0.0);
	static bool Allows(const UWorld* World, const FVector& Point, double Inset = 0.0);

	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;

	/** Called before gameplay preload and any scheduled encounters. */
	void Prepare();
	bool SelectSector(int32 Id);
	bool HasActiveSector() const { return Active.IsSet(); }
	int32 GetSectorId() const { return Active ? Active->Id : 0; }
	const FBox2D* GetArea() const { return Active ? &Active->Area : nullptr; }
	const FPmcEncounterSpec* GetEncounter() const { return Active ? &Encounter : nullptr; }
	FTransform GetStart() const { return Start.Transform(); }

	FVector PlacePlayer(const FVector& Feet, double HalfHeight) const;
	void MovePlayersToSector(bool bOnlyOutside) const;

private:
	TOptional<FTrainingSector> Active;
	FTrainingSectorStart Start;
	FPmcEncounterSpec Encounter;
	TArray<TWeakObjectPtr<AActor>> BoundaryActors;

	bool IsTrainingMap() const;
	bool BuildBoundary(const FTrainingSector& Sector, TArray<TWeakObjectPtr<AActor>>& Out) const;
};
