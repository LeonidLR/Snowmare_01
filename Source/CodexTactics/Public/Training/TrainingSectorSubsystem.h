#pragma once

#include "Containers/Ticker.h"
#include "Subsystems/WorldSubsystem.h"
#include "Training/TrainingSectorRules.h"
#include "TrainingSectorSubsystem.generated.h"

class AOperativeCharacter;

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
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/** Called before gameplay preload and any scheduled encounters. */
	void Prepare();
	bool SelectSector(int32 Id, TOptional<int32> SeedOverride = TOptional<int32>());
	bool HasActiveSector() const { return Active.IsSet(); }
	/** An active sector keeps a wiped player squad in the fight: no death cinematic and no mission failure. */
	bool SuppressesMissionEnd() const { return HasActiveSector(); }
	int32 GetSectorId() const { return Active ? Active->Id : 0; }
	const FBox2D* GetArea() const { return Active ? &Active->Area : nullptr; }
	const FTrainingSector* GetSector() const { return Active ? &Active.GetValue() : nullptr; }
	const FPmcEncounterSpec* GetEncounter() const { return Active ? &Encounter : nullptr; }
	const FSectorFightConfig& GetFightConfig() const { return Fight; }
	FSectorRoundView GetRoundView() const;
	/** Empty once the 3 s banner window has passed. */
	FString GetRoundBanner() const;
	FTransform GetStart() const { return Start.Transform(); }

	FVector PlacePlayer(const FVector& Feet, double HalfHeight) const;
	void MovePlayersToSector(bool bOnlyOutside) const;

private:
	struct FSectorFighterWatch
	{
		bool bSeen = false;
		bool bOut = false;
		double EmptySince = -1.0;
	};

	TOptional<FTrainingSector> Active;
	FTrainingSectorStart Start;
	FPmcEncounterSpec Encounter;
	FSectorFightConfig Fight;
	FSectorPair CurrentPair;
	FSectorPair NextPair;
	TArray<TWeakObjectPtr<AActor>> BoundaryActors;
	TMap<TWeakObjectPtr<AOperativeCharacter>, FSectorFighterWatch> Watched;
	FTSTicker::FDelegateHandle FightTicker;
	FString Banner;
	ESectorRoundPhase Phase = ESectorRoundPhase::Idle;
	ESectorRoundEnd LastResult = ESectorRoundEnd::None;
	int32 FightSeed = 0;
	int32 RoundNumber = 0;
	int32 ScoreVictory = 0;
	int32 ScoreDefeat = 0;
	int32 ScoreDraw = 0;
	int32 RoundKillsPlayer = 0;
	int32 RoundKillsPmc = 0;
	double RoundStartedAt = 0.0;
	double IntermissionUntil = 0.0;
	double BannerUntil = -1.0;
	bool bRespawnApplied = false;

	bool IsTrainingMap() const;
	bool BuildBoundary(const FTrainingSector& Sector, TArray<TWeakObjectPtr<AActor>>& Out) const;
	void BeginFight(int32 Seed);
	void EndFight();
	void EnsureFightTick();
	void StopFightTick();
	bool TickFight(float DeltaSeconds);
	void TryStartRound(double Now);
	void TickFighting(double Now);
	void TickIntermission(double Now);
	bool RespawnRound();
	void NoteKills();
	void RefillReserves(double Now);
	void RecordRound(bool bStart, ESectorRoundEnd Result, double Duration) const;
};
