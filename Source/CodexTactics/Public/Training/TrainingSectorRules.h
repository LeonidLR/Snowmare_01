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

enum class ESectorAmmoMode : uint8
{
	Real,
	Resupply
};

enum class ESectorRoundEnd : uint8
{
	None,
	Victory,
	Defeat,
	Draw
};

enum class ESectorRoundPhase : uint8
{
	Idle,
	Waiting,
	Fighting,
	Intermission
};

struct CODEXTACTICS_API FSectorFightConfig
{
	float RoundRestartDelayS = 8.f;
	float RoundTimeLimitS = 600.f;
	float ResupplyDelayS = 20.f;
	float MinStartPostDistanceM = 40.f;
	float RespawnGraceS = 3.f;
	float NoContactRelocateS = 90.f;
	float RelocateDistanceM = 40.f;
	int32 RelocateMaxPerRound = 3;
	int32 LauncherRocketsPerRound = 2;
	ESectorAmmoMode AmmoMode = ESectorAmmoMode::Resupply;
};

struct CODEXTACTICS_API FSectorRelocateCandidate
{
	FVector Location = FVector::ZeroVector;
	float DistToCentroidM = 0.f;
	float MinPlayerDistM = 0.f;
	bool bHasLos = true;
	bool bNearCover = false;
};

struct CODEXTACTICS_API FSectorRelocatePick
{
	bool bValid = false;
	int32 CandidateIndex = INDEX_NONE;
	FVector Location = FVector::ZeroVector;
	float DistToCentroidM = 0.f;
	bool bHasLos = false;
	bool bNearCover = false;
	bool bFallback = false;
};

struct CODEXTACTICS_API FSectorPair
{
	int32 StartIndex = INDEX_NONE;
	int32 PostIndex = INDEX_NONE;

	bool IsValid() const { return StartIndex != INDEX_NONE && PostIndex != INDEX_NONE; }
	bool operator==(const FSectorPair& Other) const { return StartIndex == Other.StartIndex && PostIndex == Other.PostIndex; }
};

struct CODEXTACTICS_API FSectorSideCount
{
	int32 Fighting = 0;
	int32 Members = 0;
};

struct CODEXTACTICS_API FSectorResupplyQuery
{
	bool bUsesAmmo = false;
	bool bHasWeapon = false;
	bool bLauncher = false;
	int32 Reserve = 0;
	int32 Clip = 0;
	int32 Magazine = 0;
	int32 WeaponReserve = 0;
	double SecondsQuiet = 0.0;
};

struct CODEXTACTICS_API FSectorRoundView
{
	ESectorRoundPhase Phase = ESectorRoundPhase::Idle;
	ESectorRoundEnd LastResult = ESectorRoundEnd::None;
	int32 Number = 0;
	int32 Seed = 0;
	int32 ScoreVictory = 0;
	int32 ScoreDefeat = 0;
	int32 ScoreDraw = 0;
	int32 KillsPlayer = 0;
	int32 KillsPmc = 0;
	FSectorPair Pair;
};

namespace TrainingSectorRules
{
	inline constexpr const TCHAR* MapName = TEXT("L_Gen_Depot_1_X6");
	inline constexpr double MinSizeCm = 6000.0;
	inline constexpr double BannerSeconds = 3.0;

	// Returns valid sectors only. Duplicate ids and malformed entries are reported.
	CODEXTACTICS_API TArray<FTrainingSector> ParseSectors(const FJsonObject& Json, TArray<FString>& Errors);
	CODEXTACTICS_API TOptional<int32> ParseId(const FString& Text);
	CODEXTACTICS_API int32 SavedId(const FJsonObject& Json);
	CODEXTACTICS_API FVector Clamp(const FBox2D& Box, const FVector& Point, double Inset = 0.0);
	CODEXTACTICS_API bool Contains(const FBox2D& Box, const FVector& Point);
	CODEXTACTICS_API void Pick(const FTrainingSector& Sector, int32 Seed, FTrainingSectorStart& Start, FPmcEncounterSpec& Squad);

	/** Top-level fight fields, then the matching sector's fields. Unknown ammo_mode stays resupply. */
	CODEXTACTICS_API FSectorFightConfig FightConfig(const FJsonObject& Json, int32 SectorId);
	CODEXTACTICS_API ESectorRoundEnd EvaluateRound(const FSectorSideCount& Player, const FSectorSideCount& Pmc, double ElapsedS, double LimitS);
	CODEXTACTICS_API FSectorPair PickRoundPair(int32 StartCount, int32 PostCount, int32 Seed, int32 RoundIndex, const FSectorPair& Previous);
	CODEXTACTICS_API FSectorPair PickSafeRoundPair(const FTrainingSector& Sector, int32 Seed, int32 RoundIndex, const FSectorPair& Previous,
		float MinDistanceM, TFunctionRef<bool(const FSectorPair&)> HasLosFn, bool* bOutFallback = nullptr);
	CODEXTACTICS_API void ApplyPair(const FTrainingSector& Sector, const FSectorPair& Pair, int32 Seed, FTrainingSectorStart& Start, FPmcEncounterSpec& Squad);
	CODEXTACTICS_API FSectorPair PairOf(const FTrainingSector& Sector, const FTrainingSectorStart& Start, const FPmcEncounterSpec& Squad);
	CODEXTACTICS_API bool ShouldRefillReserve(const FSectorResupplyQuery& Query, ESectorAmmoMode Mode, double DelayS);
	CODEXTACTICS_API int32 RefilledReserve(int32 Magazine, int32 WeaponReserve);
	CODEXTACTICS_API double QuietSeconds(double Now, double EmptySince, double LastShotTime);
	CODEXTACTICS_API FString RoundBanner(int32 Round, ESectorRoundEnd Result);
	CODEXTACTICS_API const TCHAR* RoundEndName(ESectorRoundEnd Result);
	CODEXTACTICS_API bool IsFighterDown(bool bAlive, bool bIncapacitated);
	CODEXTACTICS_API bool ResupplyTouchesGrenades();
	CODEXTACTICS_API bool ShouldRelocateNoContact(double Now, double LastContactAt, float DelayS, int32 RelocatedCount, int32 MaxPerRound, bool bInGrace);
	CODEXTACTICS_API FSectorRelocatePick PickRelocatePoint(TConstArrayView<FSectorRelocateCandidate> Candidates, float TargetDistM, float BandHalfM,
		float MinPlayerDistM, int32 Seed);
}
