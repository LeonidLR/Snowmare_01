#pragma once

#include "CoreMinimal.h"
#include "Data/CombatTypes.h"

/**
 * Pure rules of the sudden horde start (user decision 2026-10-11; UE-only, no Godot reference — Godot played a cutscene
 * and a 60 s preparation before every wave). A horde wave spawns at once and the fight opens in a FREE tactical pause
 * (FGameFlowStateMachine::IsFreePause); FlyoverDelaySeconds after the spawn the camera flies over the horde on real
 * time while the world stays at the pause dilation: a WIDE shot of the horde advancing on the squad, then one medium
 * shot per enemy TYPE not yet shown in this fight (on one representative, following it), then back to the leader.
 * Esc / a click skips it; releasing the pause (Space) ends it too. Applied by UHordeIntroSubsystem; tested in
 * CodexTactics.Combat.HordeIntro.*; console overrides Codex.HordeIntro.*.
 */
struct CODEXTACTICS_API FHordeIntroConfig
{
	/** The flyover runs at all (the sudden start and its free pause do not depend on it). */
	bool bEnabled = true;
	/** Real seconds from the spawn to the first shot (the enemies already move, slowed). */
	float FlyoverDelaySeconds = 1.75f;
	/** Real seconds of the wide shot when it opens a flyover with new types (the first wave). */
	float WideShotSeconds = 3.f;
	/**
	 * Real seconds of the wide shot of a wave whose types were all shown before in this fight (no type shots follow);
	 * 0 = no flyover at all for such a wave (the free pause still opens).
	 */
	float RepeatWideShotSeconds = 2.f;
	/** Real seconds of each per-type shot. */
	float TypeShotSeconds = 2.5f;
	/** The camera glides into each shot over this many real seconds (part of the shot's time). */
	float ShotBlendSeconds = 0.8f;
	/** Real seconds of the glide back to the leader at the end. */
	float ReturnBlendSeconds = 1.f;
	/** Wide shot: focus between the squad centre (0) and the horde centre (1). */
	float WideFocusBias = 0.6f;
	/** Wide shot distance = clamp(span * WideSpanScale + WidePaddingCm, WideMinDistance, WideMaxDistance), cm. */
	float WideSpanScale = 0.9f;
	float WidePaddingCm = 1000.f;
	float WideMinDistance = 2600.f;
	float WideMaxDistance = 4500.f;
	/** Camera distance of a per-type shot (medium framing, not too close), cm. */
	float TypeShotDistance = 1300.f;
	/** At most this many per-type shots in one flyover. */
	int32 MaxTypeShots = 6;
};

/** One shot of the flyover. */
enum class EHordeIntroShotKind : uint8
{
	/** The whole horde advancing on the squad. */
	Wide,
	/** One representative of an enemy type, followed. */
	Type,
	/** Glide back to the leader (the normal tactical camera). */
	Return
};

struct FHordeIntroShot
{
	EHordeIntroShotKind Kind = EHordeIntroShotKind::Wide;
	EEnemyArchetype Archetype = EEnemyArchetype::Base;
	float Seconds = 0.f;

	bool operator==(const FHordeIntroShot& Other) const
	{
		return Kind == Other.Kind && Archetype == Other.Archetype && FMath::IsNearlyEqual(Seconds, Other.Seconds);
	}
};

/** Where a flyover stands at some real time after the spawn (HordeIntroRules::Evaluate). */
struct FHordeIntroCursor
{
	/** Still in the delay before the first shot. */
	bool bWaiting = true;
	/** All shots are over (or there are none). */
	bool bFinished = false;
	/** Running shot, -1 while waiting / finished. */
	int32 ShotIndex = INDEX_NONE;
	/** Real seconds into the running shot. */
	float ShotSeconds = 0.f;
};

namespace HordeIntroRules
{
	/** Config with the Codex.HordeIntro.* console overrides applied. */
	CODEXTACTICS_API FHordeIntroConfig GetConfig();

	/** Codex.HordeIntro.SuddenStart: -1 the data decides (default), 0 every wave has the old prepared start, 1 all sudden. */
	CODEXTACTICS_API int32 GetSuddenStartOverride();

	/** The distinct types of Spawned in first-appearance order (the level's spawn order: hounds, then frostbitten, ...). */
	CODEXTACTICS_API TArray<EEnemyArchetype> UniqueTypesInOrder(const TArray<EEnemyArchetype>& Spawned);

	/**
	 * The flyover of a wave: PresentTypes (unique, in order) minus AlreadyShown in this fight. New types: Wide
	 * (WideShotSeconds) + one Type shot each (MaxTypeShots at most) + Return. No new type: a short Wide
	 * (RepeatWideShotSeconds) + Return, or nothing when that is 0. Nothing without enemies or with the flyover off.
	 */
	CODEXTACTICS_API TArray<FHordeIntroShot> BuildShotList(const TArray<EEnemyArchetype>& PresentTypes,
		const TSet<EEnemyArchetype>& AlreadyShown, const FHordeIntroConfig& Config);

	/** Real seconds of all shots (without the delay). */
	CODEXTACTICS_API float GetShotsSeconds(const TArray<FHordeIntroShot>& Shots);

	/** The flyover state RealSecondsSinceSpawn real seconds after the spawn. */
	CODEXTACTICS_API FHordeIntroCursor Evaluate(const TArray<FHordeIntroShot>& Shots, const FHordeIntroConfig& Config,
		float RealSecondsSinceSpawn);

	/** Wide shot focus: between the squad centre and the horde centre (WideFocusBias). */
	CODEXTACTICS_API FVector WideFocus(const FVector& SquadCentre, const FVector& HordeCentre, const FHordeIntroConfig& Config);

	/** Wide shot distance that keeps every Point (enemies and the squad) around Focus in view, cm. */
	CODEXTACTICS_API float WideDistance(const FVector& Focus, const TArray<FVector>& Points, const FHordeIntroConfig& Config);

	/** The representative of a type: the candidate nearest to the squad (the first threat), -1 without candidates. */
	CODEXTACTICS_API int32 PickRepresentative(const TArray<FVector>& Candidates, const FVector& SquadCentre);

	/** Log / smoke label of a shot: "wide", "type:FrostHound", "return". */
	CODEXTACTICS_API FString ShotLabel(const FHordeIntroShot& Shot);
}
