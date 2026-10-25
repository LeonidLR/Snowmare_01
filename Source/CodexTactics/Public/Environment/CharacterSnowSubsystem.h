#pragma once

#include "CoreMinimal.h"
#include "Environment/EnvironmentTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "CharacterSnowSubsystem.generated.h"

class ACameraZoneVolume;
class ACharacter;
class AWeatherPresenterActor;

/** Cumulative cost / activity of UCharacterSnowSubsystem (perf probes, the charsnow shots). */
struct FCharacterSnowMetrics
{
	/** Game-thread seconds spent in Tick (everything, including the frames that only accumulate time). */
	double CpuSeconds = 0.0;
	int32 Ticks = 0;
	/** Pocket steps (every ~0.25 s). */
	int32 Steps = 0;
	/** SetCustomPrimitiveDataFloatArray calls. */
	int32 Pushes = 0;
	/** Characters known / processed in the last step (the nearest Codex.Env.CharacterSnowMax). */
	int32 Tracked = 0;
	int32 Processed = 0;
};

/**
 * Snow on characters (UE-only, no Godot reference; docs/systems/environment-weather.md section 15, user decisions 2026-10-23):
 * operatives and enemies get covered while it snows and melt in the sun. Per character five pockets (FDirectionalSnow) in the BODY
 * mesh's LOCAL axes (ACharacter::GetMesh: the snow stays on the back when the character turns): every ~0.25 s the world wind (UEnvironmentSubsystem::
 * GetWindDirection) is turned into the body frame, the top / windward sides fill at the snow cover rates outdoors, every pocket melts in
 * Clear daylight (the objects' melt rate) and clears fast near an active UHeatSourceComponent, inside a Closed / Shelter ACameraZoneVolume
 * or while an operative is hidden (in the APC; enemies hidden by the fog of war keep snowing); an enemy appearing while it snows starts with half the objects' cover (CharacterSnowRules::MakeSpawnCover).
 * Operatives start each level bare (per world: nothing is carried or saved). The pockets go to the body mesh as Custom Primitive Data
 * (CharacterSnowRules::FirstCustomDataIndex 8..12), at most ~4 Hz per character and only on a change > 0.01; the snow layer of
 * M_CharacterMaster* reads them. Visual only: no gameplay reads it.
 * Characters come from the registries (USquadSubsystem::GetMembers, UEnemyRegistrySubsystem::GetEnemyCharacters - hounds are enemies),
 * never from an actor scan; only the Codex.Env.CharacterSnowMax (40) nearest the camera are processed, the others keep their snow frozen.
 * The time runs with the environment's clock rules (frozen in the tactical pause / turn-based / AI pause, x Codex.Env.ClockScale), but also
 * on a locked level (the Demo 01 lake snows on the squad). Not created without a renderer (-nullrhi smokes and bot runs are untouched) or on
 * a dedicated server; inactive (every touched body back to 0) with Codex.Env.CharacterSnow 0 or while the weather presenter is not applying.
 */
UCLASS()
class CODEXTACTICS_API UCharacterSnowSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableWhenPaused() const override { return false; }

	/** The subsystem of Context's world (null without one, e.g. -nullrhi). */
	static UCharacterSnowSubsystem* Get(const UObject* Context);

	/** Codex.Env.CharacterSnow (1 = on). */
	static bool IsEnabled();

	/** The pockets of Character (false when it was never processed). */
	bool GetPockets(const ACharacter* Character, FDirectionalSnow& OutPockets) const;

	/** Debug / captures: sets Character's pockets (clamped); pushed at the next step. */
	void SetPockets(ACharacter* Character, const FDirectionalSnow& Pockets);

	/** Debug / captures: every known character's pockets to 0 (pushed at the next step). */
	void ClearAll();

	/** Debug / captures: the next Tick steps at once instead of waiting for the ~0.25 s interval. */
	void RequestStep() { bStepRequested = true; }

	const FCharacterSnowMetrics& GetMetrics() const { return Metrics; }

private:
	struct FEntry
	{
		TWeakObjectPtr<ACharacter> Character;
		FDirectionalSnow Pockets;
		FDirectionalSnow Pushed;
		float SincePush = 1000.f;
		bool bEverPushed = false;
	};

	void Step(float DeltaSeconds);
	/** Every touched body back to 0 (the layer renders exactly as without snow). */
	void ZeroAll();
	void Push(FEntry& Entry, const FDirectionalSnow& Values);
	void RefreshShelters();
	bool IsSheltered(const FVector& Location) const;
	bool IsNearHeat(const FVector& Location) const;
	AWeatherPresenterActor* FindPresenter();

	TMap<TWeakObjectPtr<ACharacter>, FEntry> Entries;
	TArray<TWeakObjectPtr<ACameraZoneVolume>> Shelters;
	TWeakObjectPtr<AWeatherPresenterActor> Presenter;
	FCharacterSnowMetrics Metrics;
	float SinceStep = 0.f;
	float SinceShelterRefresh = 1000.f;
	double NextPresenterSearch = 0.0;
	bool bZeroed = true;
	bool bStepRequested = false;
};
