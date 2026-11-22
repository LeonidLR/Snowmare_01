#pragma once

#include "CoreMinimal.h"
#include "DoorRules.generated.h"

class FJsonObject;

/**
 * State of a generated door leaf (docs/design/levelgen/gdd/opening-states.md section 1, ADR-0003). The builder writes it as the leaf
 * actor's tag `door_state=<name>`; the door runtime (R1, ADoorActor / UDoorSubsystem) adopts `closed` and `locked` leaves.
 */
UENUM(BlueprintType)
enum class EDoorLeafState : uint8
{
	/** Swung open (or a leafless doorway): passes everyone, sight and fire. */
	Open,
	/** Shut, not locked: blocks sight, fire and bodies; anyone walking through opens it (auto-open delay). */
	Closed,
	/** Shut and locked: blocks like Closed and the navmesh; only a kick (or later a shotgun / charge) opens it. */
	Locked,
	/** Furniture / planks in the frame: blocked (builder pieces), never adopted by R1. */
	Barricaded,
	/** Welded / bricked up: blocked (builder wall fill), never adopted by R1. */
	Sealed
};

/** What somebody does to a door. */
UENUM(BlueprintType)
enum class EDoorAction : uint8
{
	/** An operative's open order (click). */
	Open,
	/** An operative's close order (click on an open door). */
	Close,
	/** A kick that lands (loud; on a locked door only by its chance per attempt): opens a locked or a closed door. */
	Kick,
	/** A pawn walking through a closed door opens it (after the auto-open delay). */
	AutoOpen,
	/** The squad's key: a locked door becomes a closed (unlocked) one. */
	Unlock,
	/** A shot that breaks the lock (its chance by weapon): a locked / closed door bursts open. */
	ShootLock,
	/** An explosion: the leaf breaks, the doorway stays open for good (any state but barricaded / sealed). */
	Blast
};

/** What one kick attempt does (DoorRules::ResolveKick). */
struct CODEXTACTICS_API FDoorKickOutcome
{
	/** The chance this attempt had to open it, 0..1. */
	float OpenChance = 0.f;
	/** The chance it had to hurt the kicker, 0..1. */
	float InjuryChance = 0.f;
	bool bOpens = false;
	bool bInjures = false;
};

/** How the navmesh treats a doorway in a state (ADR-0003 "after R1" column). */
enum class EDoorPassage : uint8
{
	/** No modifier: plain navmesh. */
	Free,
	/** Passable with an entering cost (the open delay): UNavArea_DoorClosed. */
	Priced,
	/** Not passable: NavArea_Null over the doorway. */
	Blocked
};

/** Numbers of the door runtime (Content/Data/Interactables/doors.json; every key is commented there). */
struct CODEXTACTICS_API FDoorTuning
{
	/** "enabled": the R1 switch when the console variable Codex.Doors.Openable is -1 (default 0 = off, the doors stay decorative). */
	bool bEnabled = false;
	/** "open_s": the swing of an opening leaf, s. */
	float OpenSeconds = 0.45f;
	/** "close_s": the swing of a closing leaf, s. */
	float CloseSeconds = 0.6f;
	/** "auto_open_delay_s": a pawn heading through a closed door waits this long before it swings, s. */
	float AutoOpenDelaySeconds = 0.3f;
	/** "zone_depth_cm": a pawn counts as AT the door within this distance of the leaf plane on either side. */
	float ZoneDepthCm = 130.f;
	/** "zone_slack_cm": the zone reaches this far past each jamb along the wall. */
	float ZoneSlackCm = 25.f;
	/** "path_look_ahead_cm": his navmesh path must cross the doorway within this length ahead of him (it is on his way). */
	float PathLookAheadCm = 300.f;
	/** "heading_speed_cm_s": without a path, walking into the leaf faster than this counts as heading through. */
	float HeadingSpeedCmS = 40.f;
	/** "interact_reach_cm": an operative ordered to a door acts once his feet are this close to the doorway centre (planar). */
	float InteractReachCm = 170.f;
	/** "approach_cm": the click walk-up stops this far in front of the leaf, on his side. */
	float ApproachCm = 100.f;
	/** "click_snap_cm": a click on the floor this close to an OPEN leaf means the door (to close it). */
	float ClickSnapCm = 35.f;
	/** "interact_timeout_s": a door order that has not arrived after this long is dropped. */
	float InteractTimeoutSeconds = 20.f;
	/** "single_leaf_max_cm": wider openings get two leaves (kit leaf sizes 80-130 cm single). */
	float SingleLeafMaxCm = 130.f;
	/** "swing_deg": an open leaf stands this far round its hinge. */
	float SwingDegrees = 90.f;
	/** "nav_entering_cost_cm": the navmesh price of a closed door (t_open x walk speed, opening-states F1). */
	float NavEnteringCostCm = 350.f;
	/** "nav_box_depth_cm": depth of the doorway's nav modifier box across the wall. */
	float NavBoxDepthCm = 60.f;
	/** "kick_enabled": the double click kicks (0 = refused with a line). */
	bool bKickEnabled = true;
	/** "kick_s": one kick attempt, from the start to the impact (the kick clip's hit moment when it lands). */
	float KickSeconds = 1.2f;
	/** "kick_noise_m": N_kick (opening-states F1, 15 m): the lure noise radius of every kick attempt. */
	float KickNoiseCm = 1500.f;
	/** "kick_chance_closed" / "kick_chance_locked": the chance one kick opens an unlocked / a locked door. */
	float KickChanceClosed = 1.f;
	float KickChanceLocked = 0.4f;
	/** "kick_heavy_factor": a heavy door's kick chance is multiplied by this; "heavy_min_width_cm": doors this wide are heavy (entrances). */
	float KickHeavyFactor = 0.6f;
	float HeavyMinWidthCm = 140.f;
	/** "kick_max_attempts": one double click kicks up to this many times until it gives. */
	int32 KickMaxAttempts = 4;
	/** "kick_injury_base" + "kick_injury_wounded" (his health below "kick_wounded_below") + "kick_injury_heavy": the chance one kick hurts him. */
	float KickInjuryBase = 0.f;
	float KickInjuryWounded = 0.35f;
	float KickWoundedBelow = 0.5f;
	float KickInjuryHeavy = 0.15f;
	/** "kick_injury_damage": what a bad kick costs him (the health component, kinetic). */
	float KickInjuryDamage = 10.f;
	/** "hand_open_s": the hand on the handle before the leaf swings (single click on an unlocked door). */
	float HandOpenSeconds = 0.5f;
	/** "unlock_s": the key in the lock (single click on a locked door with the squad's key), then it opens by hand. */
	float UnlockSeconds = 1.2f;
	/** "lock_shot_chance": per weapon id (weapons' WeaponId) the chance one shot burst breaks the lock; "default" for the rest. */
	TMap<FString, float> LockShotChance;
	float LockShotChanceDefault = 0.35f;
	/** "lock_shot_range_cm": he shoots the lock from up to this far (with a clear line); farther he walks to "lock_shot_stand_cm" first. */
	float LockShotRangeCm = 800.f;
	float LockShotStandCm = 250.f;
	/** "lock_shot_resolve_s": the burst's time before the lock gives (or holds). */
	float LockShotResolveSeconds = 0.6f;
	/** "blast_reach_cm": an explosion within its radius and this far of the doorway centre breaks the leaf. */
	float BlastReachCm = 300.f;
	/** "start_keys": key ids the squad holds from the start (a building id, an opening id, a door_key tag, or "*"). */
	TArray<FString> StartKeys;
	/** Montage hooks (soft object paths, empty = no clip yet): "hand_open_montage", "kick_montage", "unlock_montage". */
	FString HandOpenMontage;
	FString KickMontage;
	FString UnlockMontage;
};

/**
 * Pure rules of the openable doors (R1, designer 2026-11-21 "generated buildings need doors that can be opened"; roadmap
 * docs/port/ROADMAP_THREE_TRACKS_2026-11-21.md: behind a switch, off by default). State machine, the navmesh and sight rules, the leaf
 * split, the swing side, "is this pawn going through the door". The runtime is ADoorActor + UDoorSubsystem. UE-only (no Godot reference).
 */
namespace DoorRules
{
	/** "open" / "closed" / "locked" / "barricaded" / "sealed" (case-insensitive). False (and Open) for anything else. */
	CODEXTACTICS_API bool ParseState(const FString& Text, EDoorLeafState& OutState);

	/** The tag / layout name of a state ("closed"). */
	CODEXTACTICS_API const TCHAR* StateName(EDoorLeafState State);

	/** R1 adopts this builder leaf (closed / locked): it becomes an ADoorActor. Barricaded / sealed stay the builder's blocking pieces. */
	CODEXTACTICS_API bool IsAdoptable(EDoorLeafState State);

	/**
	 * The state machine: what Action does to a door in Current. True with OutNext when it changes the door, false when it is refused
	 * (opening an open door, a locked door to anything but a kick, closing a closed one, a kick with kicks disabled, anything on
	 * barricaded / sealed). A kicked locked door ends Open (its lock is broken: closing it again makes it Closed, not Locked).
	 */
	CODEXTACTICS_API bool ApplyAction(EDoorLeafState Current, EDoorAction Action, const FDoorTuning& Tuning, EDoorLeafState& OutNext);

	/** The navmesh rule: Open free, Closed priced (open delay), Locked / Barricaded / Sealed blocked. */
	CODEXTACTICS_API EDoorPassage GetPassage(EDoorLeafState State);

	/** The entering cost of a doorway in State (0 unless Priced). */
	CODEXTACTICS_API float GetNavEnteringCost(EDoorLeafState State, const FDoorTuning& Tuning);

	/** Sight, bullets and bodies: every state but Open blocks (a leaf mid-swing open already passes; a closing one blocks once shut). */
	CODEXTACTICS_API bool BlocksSightAndFire(EDoorLeafState State);

	/** One leaf up to SingleLeafMaxCm, two above (each half the width). */
	CODEXTACTICS_API int32 CountLeaves(float WidthCm, const FDoorTuning& Tuning);

	/**
	 * The swing side, a push away from whoever opens it: +1 swings the leaf towards the door's +Y (normal) side, -1 towards -Y.
	 * InstigatorLocalY = his side of the leaf plane (door frame); 0 (unknown / in the plane) pushes towards +Y.
	 */
	CODEXTACTICS_API float SwingSign(float InstigatorLocalY);

	/** A point in the door frame (X along the wall from the doorway centre, Y across) lies in the door's zone. */
	CODEXTACTICS_API bool IsInZone(const FVector2D& Local, float HalfWidthCm, const FDoorTuning& Tuning);

	/**
	 * Does the 2D path (his position first, then the remaining path points; door frame) cross the doorway segment |x| <= HalfWidth on
	 * y = 0 within LookAheadCm of path length from its start?
	 */
	CODEXTACTICS_API bool PathCrossesDoorway(const TArray<FVector2D>& LocalPath, float HalfWidthCm, float LookAheadCm);

	/** Walking towards the leaf plane (door frame position / velocity) faster than HeadingSpeedCmS. */
	CODEXTACTICS_API bool IsHeadingInto(const FVector2D& Local, const FVector2D& LocalVelocity, const FDoorTuning& Tuning);

	/**
	 * The auto-open timer of a door: Held seconds somebody has been heading through it. Returns the new Held (DeltaSeconds added while
	 * bSomeoneHeading, else 0) and sets bOutOpen when a Closed door has been waited on for AutoOpenDelaySeconds.
	 */
	CODEXTACTICS_API float AdvanceAutoOpen(EDoorLeafState State, float Held, bool bSomeoneHeading, float DeltaSeconds,
		const FDoorTuning& Tuning, bool& bOutOpen);

	/** An open door may be shut only with nobody standing in its doorway and its leaf not blown off. */
	CODEXTACTICS_API bool CanClose(EDoorLeafState State, bool bDoorwayOccupied, bool bBroken = false);

	/** A door this wide is heavy (doors.json heavy_min_width_cm): a lower kick chance, a higher injury chance. */
	CODEXTACTICS_API bool IsHeavy(float WidthCm, const FDoorTuning& Tuning);

	/**
	 * One kick attempt on a door in State (closed / locked; anything else: no chance): the open chance (kick_chance_closed / _locked,
	 * x kick_heavy_factor when heavy) and the injury chance (kick_injury_base + kick_injury_wounded below kick_wounded_below health +
	 * kick_injury_heavy when heavy, clamped 0..1), decided by the two rolls in 0..1 (a roll below the chance happens).
	 */
	CODEXTACTICS_API FDoorKickOutcome ResolveKick(EDoorLeafState State, bool bHeavy, float HealthFraction, float RollOpen, float RollInjury,
		const FDoorTuning& Tuning);

	/** The chance one shot burst of WeaponId breaks a lock (lock_shot_chance; "default" for an unlisted id, 0 for an empty id). */
	CODEXTACTICS_API float GetLockShotChance(const FString& WeaponId, const FDoorTuning& Tuning);

	/** Does one of Keys open this door: "*", the opening id, the building id (every locked door of it) or the door's own key tag. */
	CODEXTACTICS_API bool KeyFits(const TSet<FString>& Keys, const FString& BuildingId, const FString& OpeningId, const FString& DoorKeyId);

	/** What a click on a door in State orders: single click / double click / Ctrl + click (bAttack). */
	CODEXTACTICS_API bool ChooseClickAction(EDoorLeafState State, bool bDoubleClick, bool bAttack, bool bHasKey, bool bBroken, EDoorAction& OutAction);

	/** The value of a "key=value" actor tag (false when absent). */
	CODEXTACTICS_API bool FindTagValue(const TArray<FName>& Tags, const TCHAR* Key, FString& OutValue);
}

/** Loading of doors.json and the switch. */
namespace DoorTuning
{
	CODEXTACTICS_API FString GetDefaultPath();

	/** Base with every key of Json that is present applied (clamped). */
	CODEXTACTICS_API FDoorTuning FromJson(const FJsonObject& Json, const FDoorTuning& Base = FDoorTuning());

	/** The tuning in force (doors.json, loaded once; defaults when the file is missing). */
	CODEXTACTICS_API const FDoorTuning& Get();

	/** Reloads doors.json (smokes / tests). False when the file is missing or broken (defaults kept). */
	CODEXTACTICS_API bool Reload();

	/** Replaces the tuning in force (smokes: forced chances). Reload() brings the file back. */
	CODEXTACTICS_API void SetForTesting(const FDoorTuning& Tuning);

	/** The switch rule: Console -1 = the file's "enabled", 0 = off, >= 1 = on. */
	CODEXTACTICS_API bool ResolveSwitch(int32 Console, const FDoorTuning& Tuning);

	/** The switch now: Codex.Doors.Openable (default -1 = doors.json "enabled", default 0). Off = the game exactly as before R1. */
	CODEXTACTICS_API bool IsOpenableOn();
}
