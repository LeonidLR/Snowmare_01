#pragma once

#include "CoreMinimal.h"
#include "Data/CombatTypes.h"

/**
 * Pure rules of the sudden horde start (user decisions 2026-10-11; UE-only, no Godot reference — Godot played a cutscene
 * and a 60 s preparation before every wave). Timeline:
 *  1. the wave spawns at once; the fight runs in real time (optional light slow-down PreTriggerTimeDilation), the camera
 *     stays on the squad — the enemies start their run;
 *  2. once the horde has become a running mass (MassMovingFraction of the live enemies run at the squad faster than
 *     MassMinSpeed for MassHoldSeconds, not before MinDelaySeconds; MaxWaitSeconds at the latest) the free tactical pause
 *     opens, the big «HORDE INCOMING» alert shows and the camera flies over the horde in dramatic slow motion
 *     (FlyoverTimeDilation, not the 0.02 pause): a wide shot of the running mass seen from in front, then a close tracking
 *     shot of a running front-runner of every type not yet shown in this fight, then back to the leader;
 *  3. the world settles into the free pause (0.02, no charge, no timer).
 * Esc / a click skip the flyover (camera back on the leader, free pause). Applied by UHordeIntroSubsystem; tested in
 * CodexTactics.Combat.HordeIntro.*; console overrides Codex.HordeIntro.*.
 */
struct CODEXTACTICS_API FHordeIntroConfig
{
	/** The flyover runs at all (without it the free pause opens at the trigger). */
	bool bEnabled = true;
	/** World time dilation between the spawn and the trigger (1 = real time). */
	float PreTriggerTimeDilation = 1.f;
	/** Share of the live enemies that must run at the squad for the trigger. */
	float MassMovingFraction = 0.6f;
	/** Running = at least this ground speed towards the squad, cm/s. */
	float MassMinSpeed = 250.f;
	/** The mass must keep running this long (game s) to count as formed. */
	float MassHoldSeconds = 0.4f;
	/** Earliest trigger after the spawn, game s. */
	float MinDelaySeconds = 2.5f;
	/** Latest trigger after the spawn (fallback when the horde never forms a mass), game s. */
	float MaxWaitSeconds = 6.f;
	/** World time dilation during the flyover: the horde visibly runs in slow motion. */
	float FlyoverTimeDilation = 0.2f;
	/** Real seconds of the wide shot of the running mass. */
	float WideShotSeconds = 3.f;
	/** Real seconds of the wide shot of a wave whose types were all shown before in this fight. */
	float RepeatWideShotSeconds = 2.5f;
	/** Real seconds of each per-type tracking shot. */
	float TypeShotSeconds = 2.5f;
	/** The camera glides into each shot over this many real seconds (part of the shot's time). */
	float ShotBlendSeconds = 0.7f;
	/** Real seconds of the glide back to the leader at the end. */
	float ReturnBlendSeconds = 1.f;
	/** Wide shot: focus between the squad centre (0) and the running mass's centre (1). */
	float WideFocusBias = 0.8f;
	/** Wide shot distance = clamp(span * WideSpanScale + WidePaddingCm, WideMinDistance, WideMaxDistance), cm. */
	float WideSpanScale = 0.9f;
	float WidePaddingCm = 800.f;
	float WideMinDistance = 2000.f;
	float WideMaxDistance = 4000.f;
	/** Shots look at the horde from the squad's side (the onslaught comes at the camera), turned this much aside, deg. */
	float ViewSideDegrees = 25.f;
	/** Camera distance of a per-type tracking shot (close, the runner fills the frame), cm. */
	float TypeShotDistance = 700.f;
	/** At most this many per-type shots in one flyover. */
	int32 MaxTypeShots = 6;
	/** The «HORDE INCOMING» alert stays at least this long (real s), fading in / out over AlertFadeSeconds. */
	float AlertSeconds = 3.f;
	float AlertFadeSeconds = 0.5f;
};

/** One shot of the flyover. */
enum class EHordeIntroShotKind : uint8
{
	/** The running mass charging the squad. */
	Wide,
	/** One running representative of an enemy type, tracked. */
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

/** Where a flyover stands some real time after it began (HordeIntroRules::Evaluate). */
struct FHordeIntroCursor
{
	/** All shots are over (or there are none). */
	bool bFinished = false;
	/** Running shot, -1 when finished. */
	int32 ShotIndex = INDEX_NONE;
	/** Real seconds into the running shot. */
	float ShotSeconds = 0.f;
};

/** An enemy as the trigger / the representative pick sees it. */
struct FHordeMover
{
	FVector Location = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
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
	 * The flyover of a wave: a wide shot of the running mass (WideShotSeconds, or RepeatWideShotSeconds when no type is
	 * new), one Type shot per type of PresentTypes not in AlreadyShown (MaxTypeShots at most), then Return. Nothing
	 * without enemies or with the flyover off.
	 */
	CODEXTACTICS_API TArray<FHordeIntroShot> BuildShotList(const TArray<EEnemyArchetype>& PresentTypes,
		const TSet<EEnemyArchetype>& AlreadyShown, const FHordeIntroConfig& Config);

	/** Real seconds of all shots. */
	CODEXTACTICS_API float GetShotsSeconds(const TArray<FHordeIntroShot>& Shots);

	/** The flyover state RealSecondsSinceStart real seconds after it began. */
	CODEXTACTICS_API FHordeIntroCursor Evaluate(const TArray<FHordeIntroShot>& Shots, float RealSecondsSinceStart);

	/** The mover runs at the squad: ground speed >= MinSpeed, heading within 60 deg of the squad centre. */
	CODEXTACTICS_API bool IsRunningAtSquad(const FHordeMover& Mover, const FVector& SquadCentre, float MinSpeed);

	/** Share (0..1) of Movers running at the squad; 0 without movers. */
	CODEXTACTICS_API float RunningFraction(const TArray<FHordeMover>& Movers, const FVector& SquadCentre, float MinSpeed);

	/** The running mass has formed: MassMovingFraction ran at the squad for MassHoldSeconds (game s) without a break. */
	CODEXTACTICS_API bool IsMassFormed(float MassRunningSeconds, const FHordeIntroConfig& Config);

	/**
	 * The flyover starts now: the mass has formed (latched — packs fanning out to flank later do not undo it) and
	 * MinDelaySeconds have passed since the spawn, or MaxWaitSeconds are over (game seconds since the spawn).
	 */
	CODEXTACTICS_API bool ShouldTrigger(float SecondsSinceSpawn, bool bMassFormed, const FHordeIntroConfig& Config);

	/** Wide shot focus: between the squad centre and the running mass's centre (WideFocusBias). */
	CODEXTACTICS_API FVector WideFocus(const FVector& SquadCentre, const FVector& HordeCentre, const FHordeIntroConfig& Config);

	/** Wide shot distance that keeps every Point of the mass around Focus in view, cm. */
	CODEXTACTICS_API float WideDistance(const FVector& Focus, const TArray<FVector>& Points, const FHordeIntroConfig& Config);

	/**
	 * Camera yaw that looks from the squad's side at Target (the horde runs at the camera): the view direction
	 * Squad -> Target turned SideDegrees aside, deg.
	 */
	CODEXTACTICS_API float ViewYawAt(const FVector& SquadCentre, const FVector& Target, float SideDegrees);

	/**
	 * The representative of a type: the front-runner — the candidate nearest to the squad among those running at it;
	 * without such runners the nearest one that moves at all (faster than a fifth of MinSpeed, flankers included); else
	 * the nearest one; -1 without candidates.
	 */
	CODEXTACTICS_API int32 PickRunningRepresentative(const TArray<FHordeMover>& Candidates, const FVector& SquadCentre, float MinSpeed);

	/** Alert opacity Seconds after it appeared while it stays ShowSeconds (fades in / out over AlertFadeSeconds). */
	CODEXTACTICS_API float AlertAlpha(float Seconds, float ShowSeconds, const FHordeIntroConfig& Config);

	/** Log / smoke label of a shot: "wide", "type:FrostHound", "return". */
	CODEXTACTICS_API FString ShotLabel(const FHordeIntroShot& Shot);
}
