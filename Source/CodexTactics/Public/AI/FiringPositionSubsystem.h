#pragma once

#include "CoreMinimal.h"
#include "AI/FiringPositionRules.h"
#include "Subsystems/WorldSubsystem.h"
#include "FiringPositionSubsystem.generated.h"

class AActor;

/** One muzzle trace of the roof-edge diagnosis (what a real shot from that stance / step would hit). */
struct CODEXTACTICS_API FFiringTraceDiag
{
	EOperativeStance Stance = EOperativeStance::Standing;
	/** Centimetres stepped from the spot toward the rim. */
	float StepCm = 0.f;
	/** Where the walk surface ends from the spot toward the target (the rim), cm. */
	float RimDistanceCm = 0.f;
	/** The stepped point is on the roof's walkable surface (navmesh within a few cm at the roof's level). */
	bool bOnWalkSurface = false;
	FVector Muzzle = FVector::ZeroVector;
	FVector Aim = FVector::ZeroVector;
	bool bBlocked = false;
	FString HitActor;
	FString HitComponent;
	/** Along the line, cm. */
	float HitDistanceCm = 0.f;
	/** The impact point's height above the roof floor, cm. */
	float HitHeightCm = 0.f;
	/** The top of the thing hit (a downward trace on it) above the roof floor, cm; -1: not found. */
	float HitTopCm = -1.f;
};

/** What the last candidate gather cost (logs, the report, the hitch budget). */
struct CODEXTACTICS_API FFiringGatherStats
{
	int32 ScanPoints = 0;
	int32 CoverSlots = 0;
	int32 RoofSpots = 0;
	int32 Windows = 0;
	int32 Valid = 0;
	int32 Traces = 0;
	double Milliseconds = 0.0;
};

/**
 * FIRING POSITION SERVICE (smart enemies T-05; GDD smart-enemies.md section 5 + F2). Answers "where can a man stand to fire at THIS area":
 * PositionsCovering(area, weapon band, N) returns stand point + stance + facing + covered share of corner slots, low-cover slots, roof edges
 * and window stands. A position exists only when line traces from its muzzle at its stance reach the area's samples - the same trace a real
 * shot uses (ECC_Visibility, the operatives and turrets ignored; AOperativeCharacter::EvaluateShotLine) - so a returned position fires.
 * A roof edge steps toward the rim (FFiringPositionConfig::RimStepCm up to RimStepMaxCm, on the walkable surface) until a stance clears the
 * parapet; none clears = dropped and logged as a generator fault, never shot through. Candidates are cached per area for the round
 * (InvalidateCache on a geometry change; the event bus has no door / destruction event). Pure parts: FiringPositionRules.
 * Kinds NOT built: StairHead (no stair-head geometry in the tactical file), ObstacleTop (mantle edges are on the unmerged building-generator branch).
 * Debug: CodexTactics.FiringPositions <radius> [band_min_m band_max_m].
 */
UCLASS()
class CODEXTACTICS_API UFiringPositionSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	static UFiringPositionSubsystem* Get(const UWorld* World);

	/** The "firing_positions" numbers (loaded from pmc_commander.json at first use; SetConfig overrides, smokes / tests). */
	const FFiringPositionConfig& GetConfig() const;
	void SetConfig(const FFiringPositionConfig& InConfig);

	/**
	 * The top N valid positions covering the area (a circle on the floor of AreaCenter), best F2 first with the spacing applied (one man per
	 * cover object, MinSpacingCm between two), skipping the cover objects in ExcludeCoverKeys. BandMinCm / BandMaxCm = the weapon's engagement
	 * band: a sample counts only inside it (BandMaxCm <= 0: no upper limit). N <= 0: all valid positions, unspaced, best first.
	 */
	TArray<FFiringPosition> PositionsCovering(const FVector& AreaCenter, float AreaRadiusCm, float BandMinCm, float BandMaxCm, int32 N,
		const TArray<FString>& ExcludeCoverKeys = TArray<FString>(), uint8 StanceMask = FiringStances::Default);

	/** The cached, scored, valid candidates of an area (best first). The pair picker works on this list. */
	const TArray<FFiringCandidate>& GetCandidates(const FVector& AreaCenter, float AreaRadiusCm, float BandMinCm, float BandMaxCm, uint8 StanceMask = FiringStances::Default);

	/** Two hammers with a crossfire on KillZone (FiringPositionRules::PickCrossfirePair over the area's candidates). False: no pair. */
	bool PickCrossfirePair(const FVector& KillZone, float AreaRadiusCm, float BandMinCm, float BandMaxCm, const TArray<FString>& ExcludeCoverKeys,
		FFiringPosition& OutFirst, FFiringPosition& OutSecond, uint8 StanceMask = FiringStances::Default);

	/**
	 * Re-traces a position's lines to the area's samples from its recorded FireOrigin, uncached: OutClear of OutSamples (the area's standable
	 * samples) are clear. Validation of the validation (FiringPositionSmoke).
	 */
	void RetracePosition(const FFiringPosition& Position, const FVector& AreaCenter, float AreaRadiusCm, float BandMinCm, float BandMaxCm,
		int32& OutClear, int32& OutSamples, FVector* OutClearFloorPoint = nullptr) const;

	/**
	 * The roof diagnosis: traces from the muzzle of every stance at every rim step of one roof spot to Target (a floor point; the aim is
	 * Target + SampleHeightCm) and reports what each line hits and how tall it is. Facing = the yaw of the step direction.
	 */
	TArray<FFiringTraceDiag> DiagnoseRoofSpot(const FVector& SpotFeet, float FacingYaw, const FVector& Target) const;

	/** Where the walk surface ends from Start along Dir (planar), cm: the rim of a roof. 0 when Start is the last walkable point. */
	float FindRimDistance(const FVector& Start, const FVector& Dir, float MaxCm) const;

	/** Forgets every cached area (a round's start, a geometry change). */
	void InvalidateCache();

	const FFiringGatherStats& GetLastStats() const { return LastStats; }
	/** How many areas were gathered (not served from the cache) - the cache check of the smoke. */
	int32 GetGatherCount() const { return GatherCount; }

	/** Logs the valid positions around Centre (the console command). */
	void Dump(const FVector& Centre, float AreaRadiusCm, float BandMinCm, float BandMaxCm, int32 Count);

private:
	struct FFiringScanContext;

	FString AreaKey(const FVector& AreaCenter, float AreaRadiusCm, float BandMinCm, float BandMaxCm, uint8 StanceMask) const;
	void BuildContext(FFiringScanContext& Context, const FVector& AreaCenter, float AreaRadiusCm, float BandMinCm, float BandMaxCm, uint8 StanceMask = FiringStances::Default) const;
	TArray<FFiringCandidate> Gather(const FVector& AreaCenter, float AreaRadiusCm, float BandMinCm, float BandMaxCm, uint8 StanceMask);
	/** Feet on the real floor: a downward trace 40 cm over / under Point (the navmesh floats ~8 cm above the floor; the muzzle is measured from the floor). */
	FVector SnapToFloor(const FVector& Point) const;
	bool ProjectOnNav(const FVector& Point, const FVector& Extent, FVector& Out) const;
	/**
	 * The roof's walk surface: the navmesh within HorizontalCm / VerticalCm of Point, else the floor test (a downward trace 30 cm over / under
	 * Point hits an upward-facing surface; a point inside a parapet starts inside it and fails). Out = the surface point under Point.
	 */
	bool FindWalkSurface(const FVector& Point, float HorizontalCm, float VerticalCm, FVector& Out, bool bSnap = true) const;
	bool LineBlocked(FFiringScanContext& Context, const FVector& From, const FVector& To) const;
	/** How many of the context's samples a muzzle at Origin reaches (inside the band; Sector: bearing within HalfFov of Yaw when HalfFov > 0). */
	int32 CountClear(FFiringScanContext& Context, const FVector& Origin, float SectorYaw, float SectorHalfFovDeg, FVector* OutMeanAim, FVector* OutFirstClearAim = nullptr) const;
	void GatherRoofs(FFiringScanContext& Context, TArray<FFiringCandidate>& Out);
	void GatherWindows(FFiringScanContext& Context, TArray<FFiringCandidate>& Out);
	void GatherCover(FFiringScanContext& Context, TArray<FFiringCandidate>& Out);
	void FinishCandidate(FFiringScanContext& Context, FFiringCandidate& Candidate, float Protect, float Escape) const;

	FFiringPositionConfig Config;
	bool bConfigLoaded = false;
	TMap<FString, TArray<FFiringCandidate>> Cache;
	FFiringGatherStats LastStats;
	int32 GatherCount = 0;
};
