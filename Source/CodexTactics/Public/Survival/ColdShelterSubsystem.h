#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Survival/ColdShelterRules.h"
#include "ColdShelterSubsystem.generated.h"

/** A building's leak changed because one of its openings did (door kicked, glass shot, breach blown, door shut again). */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnColdShelterLeakChanged, const FString& /*BuildingId*/, float /*OldLeak*/, float /*NewLeak*/);

/**
 * The cold shelter of a generated map (T-021; docs/systems/cold-survival.md). Loads the map's levelgen layout ONCE per level (the
 * map's own Content/Data/LevelJson/<Map>.json "levelgen_layout", parsed by PmcDefenceRules::ParseLayout) plus
 * Content/Data/Survival/cold_shelter.json, keeps a live open / sealed flag per opening, and answers "how cold is it where this
 * operative stands" for UColdSurvivalComponent::GatherEnvironment, the campfire and the blizzard walk layer. A map with no layout
 * (hand-made maps) answers Outside everywhere: nothing changes there, their camera zones keep doing the job.
 *
 * LIVE state, event-driven: whatever breaks / opens / shuts / breaches an opening calls SetOpeningOpen(AtPosition); only that
 * building's leak is recomputed and OnLeakChanged fires. There is no per-tick scan: a query is BuildingAt + the open openings of one
 * building. As of T-021 no door, window or breach actor exists at runtime (the generated doors are gaps, the windows holes), so
 * nothing calls the setters yet except the smoke; the layout's "state" fields are the initial state.
 */
UCLASS()
class CODEXTACTICS_API UColdShelterSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	/** The indoor model at Feet (world cm, feet not the capsule centre). Outside (multiplier 1) when the map has no layout. */
	FColdShelterSample GetShelterAt(const FVector& Feet) const;

	/** Inside a building of the layout (a camera zone check is NOT included, see IsShelteredLocation). */
	bool IsInsideBuilding(const FVector& Location) const { return GetShelterAt(Location).IsIndoors(); }

	/**
	 * The indoor rule of the campfire / the blizzard walk layer on any map: a Closed / Shelter camera zone
	 * (ACameraZoneVolume::IsShelteredLocation) or a building of the generated layout.
	 */
	static bool IsShelteredLocation(const UWorld* World, const FVector& Location);

	/**
	 * An opening of a building opens (bOpen: kicked out, glass broken, breach, door open) or seals again. False when the building or
	 * the opening id is unknown. Recomputes that building's leak and fires OnLeakChanged when it changed.
	 */
	bool SetOpeningOpen(const FString& BuildingId, const FString& OpeningId, bool bOpen);

	/**
	 * Same, for a runtime actor that only knows where it stands: the opening of the building (planar) nearest to WorldXY on that storey
	 * within RadiusCm. OutBuildingId / OutOpeningId say which one. False when none is near.
	 */
	bool SetOpeningOpenAt(const FVector2D& WorldXY, int32 Storey, float RadiusCm, bool bOpen, FString* OutBuildingId = nullptr, FString* OutOpeningId = nullptr);

	/** The building's current leak (0..1); negative when the building is unknown. */
	float GetBuildingLeak(const FString& BuildingId) const;

	/** The opening is currently open; false when unknown. */
	bool IsOpeningOpen(const FString& BuildingId, const FString& OpeningId) const;

	/** Fired when a building's leak changes. */
	FOnColdShelterLeakChanged OnLeakChanged;

	/** True when the map's layout was found and parsed (smokes). */
	bool HasLayout() const;

	/** The parsed layout (null when none). */
	const FPmcLayoutAccess* GetLayout() const;

	/** The numbers in force (cold_shelter.json, loaded with the layout). Changing them in a smoke / test recomputes the leaks. */
	const FColdShelterConfig& GetConfig() const;
	void SetConfig(const FColdShelterConfig& InConfig);

	/** Replaces the layout (tests / smokes) and stops the lazy load. */
	void SetLayoutForTest(const FPmcLayoutAccess& InLayout);

private:
	struct FBuildingState
	{
		TArray<bool> Open;
		float Leak = 0.f;
	};

	void EnsureLoaded() const;
	void BuildStates() const;
	int32 FindBuilding(const FString& BuildingId) const;
	bool ApplyOpening(int32 BuildingIndex, int32 OpeningIndex, bool bOpen);

	mutable bool bTried = false;
	mutable TOptional<FPmcLayoutAccess> Layout;
	mutable TArray<FBuildingState> States;
	mutable FColdShelterConfig Config;
};
