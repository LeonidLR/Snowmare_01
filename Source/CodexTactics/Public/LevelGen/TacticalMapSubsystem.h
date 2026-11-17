#pragma once

#include "CoreMinimal.h"
#include "LevelGen/TacticalMapRules.h"
#include "Subsystems/WorldSubsystem.h"
#include "TacticalMapSubsystem.generated.h"

/**
 * The TACTICAL MAP of a generated level (docs/design/TACTICAL_MAP.md P1, designer 2026-11-17: "bots read the terrain"). At BeginPlay it
 * reads Content/Data/LevelGen/tactical/<Map>.tactical.json (written by the Training Ground «Принять» / «Собрать в Unreal»; parsed by
 * TacticalMapRules) and the query settings Content/Data/LevelGen/tactical_runtime.json. A map without the file has an EMPTY map: every
 * query answers "nothing here" (danger 0, no spots) and nothing else changes. The AI (P2 PMC commander / placement, P3 arena metrics,
 * P5 squad hints) asks it: GetZonesNear, DangerAt, BestAmbushSpots, OverwatchSpots, WindowPositions, DropSpotsAbove, IsChokepoint,
 * SafeDetour, PickLadderOnlyRoofs - see TacticalMapRules for the exact semantics.
 * Debug: Codex.TacticalMap.Draw 1 draws the places / spots round the view; Codex.TacticalMap.Dump logs the counts + top places.
 * UE-only (the Godot archive had no generated maps).
 */
UCLASS()
class CODEXTACTICS_API UTacticalMapSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** A tactical file is loaded (it may still hold no items). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|TacticalMap")
	bool IsLoaded() const { return bLoaded; }

	/** The danger 0..1 for the squad (bSquadSide) or the PMC at a world point. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|TacticalMap")
	float GetDangerAt(const FVector& Point, bool bSquadSide = true) const { return DangerAt(Point, bSquadSide ? ETacticalSide::Squad : ETacticalSide::Pmc).Danger; }

	const FTacticalMapFile& GetFile() const { return File; }
	const FTacticalRuntimeConfig& GetConfig() const { return Config; }
	const FString& GetLoadedPath() const { return LoadedPath; }

	TArray<int32> GetZonesNear(const FVector& Point, float RadiusCm) const { return TacticalMapRules::GetZonesNear(File, Config, Point, RadiusCm); }
	FTacticalDanger DangerAt(const FVector& Point, ETacticalSide Side) const { return TacticalMapRules::DangerAt(File, Config, Point, Side); }
	TArray<FTacticalPick> BestAmbushSpots(const FVector& Start, const FVector& End, int32 N) const { return TacticalMapRules::BestAmbushSpots(File, Config, Start, End, N); }
	TArray<FTacticalPick> OverwatchSpots(const FVector& Target, float RadiusCm, int32 N = 0) const { return TacticalMapRules::OverwatchSpots(File, Config, Target, RadiusCm, N); }
	TArray<FTacticalPick> WindowPositions(const FVector& Target, float RadiusCm) const { return TacticalMapRules::WindowPositions(File, Config, Target, RadiusCm); }
	TArray<FTacticalPick> DropSpotsAbove(const FVector& Center, float RadiusCm) const { return TacticalMapRules::DropSpotsAbove(File, Config, Center, RadiusCm); }
	bool IsChokepoint(TConstArrayView<FVector> Path, int32* OutIndex = nullptr) const { return TacticalMapRules::IsChokepoint(File, Config, Path, OutIndex); }
	FTacticalDetour SafeDetour(const FVector& From, const FVector& To) const { return TacticalMapRules::SafeDetour(File, Config, From, To); }
	TArray<int32> PickLadderOnlyRoofs(int32 Seed, float Factor = -1.f) const { return TacticalMapRules::PickLadderOnlyRoofs(File, Seed, Factor); }

	/** Loads a tactical file instead of the map's (smokes / tools). False (and an empty map) when it is missing or invalid. */
	bool LoadFromFile(const FString& Path);

	/** (Re)reads tactical_runtime.json; the defaults stay when it is missing / invalid. */
	void LoadConfig();

	/** Back to the empty map. */
	void Clear();

	/** Logs the counts, the top places and the danger at the first player's pawn. */
	void Dump() const;

	/** Draws the places / spots within the config's draw radius of ViewLocation for one frame (Codex.TacticalMap.Draw). */
	void DrawDebug(const FVector& ViewLocation) const;

	/** Codex.TacticalMap.Draw != 0. */
	static bool IsDrawEnabled();

private:
	FTacticalMapFile File;
	FTacticalRuntimeConfig Config;
	FString LoadedPath;
	bool bLoaded = false;
};
