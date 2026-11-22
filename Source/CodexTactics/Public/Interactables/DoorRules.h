#pragma once

#include "CoreMinimal.h"
#include "DoorRules.generated.h"

class FJsonObject;
enum class EEnemyArchetype : uint8;

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
	Blast,
	/**
	 * A hand on the handle of a locked door the squad does not know is locked (designer 2026-11-22: he walks up, tries it, then says
	 * "Locked" - the door is known-locked from then on). Never changes the door.
	 */
	TryOpen
};

/**
 * How strong a door is (designer 2026-11-22: "one door strength for all methods" - a lock that held a pistol shot must not then give to two
 * kicks). Every way in (a kick, a shot at the lock) is judged against the same strength (DoorRules::BreakChance). doors.json "strengths",
 * picked per door by its tag door_strength=<name>, else its layout role (doors.json "strength_by_role"), else "strength_default".
 */
UENUM(BlueprintType)
enum class EDoorStrength : uint8
{
	/** Easy kick, the lock gives to almost any weapon. */
	Wood,
	/** Hard kick, the shotgun opens the lock, a pistol rarely. */
	Steel,
	/** Cannot be kicked: only the key, a shotgun at the lock or an explosive. */
	Reinforced
};

/** One strength class (doors.json "strengths.<name>"). */
struct CODEXTACTICS_API FDoorStrengthClass
{
	/** "resist" 0..<1: an attack of power P breaks it with (P - resist) / (1 - resist) (clamped 0..1). */
	float Resist = 0.f;
	/** "kickable": 0 = no kick ever opens it (locked or not). */
	bool bKickable = true;
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

/** Which side of him the door's handle / the kicking leg is (R2 door clips: the _L / _R takes of the designer's pack). */
enum class EDoorSide : uint8
{
	Left,
	Right
};

/** The R2 door clips (designer 2026-11-22, /Game/Free_Interaction_Animation/Animations/Door). */
enum class EDoorClip : uint8
{
	/** anim_OpenDoor_L / _R: an unlocked door opened by hand. */
	Open,
	/** anim_OpenDoor_Kick_L / _R: one kick attempt (the legs alternate between attempts). */
	Kick,
	/** anim_OpenDoor_Push_L / _R: the forced push-open of a door that WAS locked (after the key or a shot lock). */
	Push
};

/** What a breaching pawn can do now (DoorRules::NextBreachAction). */
struct CODEXTACTICS_API FDoorBreachAbility
{
	/** He carries a key that fits this door. */
	bool bHasKey = false;
	/** He holds a firearm with rounds (a hostile operative; monsters never shoot locks). */
	bool bCanShoot = false;
	/** He has a grenade to throw (a hostile operative). */
	bool bHasGrenade = false;
	/** He can kick / bash at all. */
	bool bCanKick = true;
};

/** One open kit leaf instance (kit_part=door_leaf_open) on the ground plane: its hinge, its +X (the leaf) and +Y, its width. */
struct CODEXTACTICS_API FDoorKitLeafPiece
{
	FVector2D Hinge = FVector2D::ZeroVector;
	FVector2D Along = FVector2D(1.f, 0.f);
	FVector2D Right = FVector2D(0.f, 1.f);
	float WidthCm = 80.f;
};

/** An open doorway found from kit open leaves (DoorRules::PairKitOpenLeaves): its centre on the wall line, the wall direction, width. */
struct CODEXTACTICS_API FDoorKitDoorway
{
	FVector2D Center = FVector2D::ZeroVector;
	FVector2D Tangent = FVector2D(1.f, 0.f);
	float WidthCm = 80.f;
	int32 LeafA = INDEX_NONE;
	int32 LeafB = INDEX_NONE;
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
	/**
	 * "swing_fixed" (designer 2026-11-22, item 3): every door opens to ONE side chosen at the adoption (DoorRules::ChooseSwing: clear of walls,
	 * other doorways' approach zones and stair mouths, an entrance outward; a limited angle where neither side is free, never below
	 * "swing_min_deg"); 0 = R1 (away from whoever opens it). "swing_zone_depth_cm": a doorway's approach zone reaches this far each side.
	 */
	bool bSwingFixed = true;
	/**
	 * "swing_from_opener" (designer 2026-11-22, after the agents test: every door clip - hand, push, kick - pushes the leaf AWAY): a leaf
	 * swings away from whoever opens it (outside -> in, inside -> out), at that side's free angle against walls (DoorRules::ChooseSwingSides);
	 * where the open leaf lies over another doorway's approach zone / a stair mouth it CLOSES BEHIND once nobody is still to come through
	 * ("close_behind_s" quiet, nobody within "close_behind_radius_cm" on the opener's side or with a path through it). Wins over swing_fixed.
	 */
	bool bSwingFromOpener = true;
	/**
	 * "start_interior_closed" (designer 2026-11-22, agents test: "many doors inside are open - for now every inner door is shut"): at the
	 * adoption every door open from the start whose both sides are under a roof (an inner door: flat / room / hall doors) is shut.
	 */
	bool bStartInteriorClosed = true;
	/**
	 * "start_interior_locked_share" (designer 2026-11-22: "some inner doors, at random, must be locked - kick, shoot or key them; inner doors are
	 * a little weaker than the entrances"): of the inner doors shut at the start this share is LOCKED (a stable roll per door from the doors'
	 * roll seed and its place). Their strength stays the inner one (wood: kick 0.55, pistol 0.6) - weaker than the steel entrances.
	 */
	float StartInteriorLockedShare = 0.25f;
	float CloseBehindSeconds = 1.5f;
	float CloseBehindRadiusCm = 300.f;
	float SwingMinDeg = 30.f;
	float SwingZoneDepthCm = 100.f;
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
	/**
	 * Door strength (designer 2026-11-22, DoorRules::BreakChance): "kick_power" - one kick's power against a LOCKED door (never above the
	 * weakest weapon's "lock_power": the consistency rule); "kick_latch_bonus" - added against an unlocked door (only the latch holds it).
	 */
	float KickPower = 0.55f;
	float KickLatchBonus = 0.3f;
	/** "strengths": wood / steel / reinforced (indexed by EDoorStrength). */
	FDoorStrengthClass Strengths[3] = { { 0.f, true }, { 0.5f, true }, { 0.8f, false } };
	/** "strength_by_role": layout door role ("entry", "unit_entry", "room", ...) -> strength name, or "a/b" = a deterministic mix by the opening id. */
	TMap<FString, FString> StrengthByRole;
	/** "strength_mix_second_share": in an "a/b" mix the share of doors that get b. */
	float StrengthMixSecondShare = 0.5f;
	/** "strength_story_locked": a door a building story locks (layout stories' door_state(<id>,locked) brick); empty = by its role. */
	FString StrengthStoryLocked;
	/** "strength_default": a door without a tag / a known role. */
	EDoorStrength StrengthDefault = EDoorStrength::Wood;
	/** "heavy_min_width_cm": doors this wide are heavy (entrances): a riskier kick (kick_injury_heavy); a steel / reinforced door is heavy too. */
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
	/** "lock_power": per weapon id (weapons' WeaponId) the power 0..1 of one burst at the lock (DoorRules::BreakChance vs the strength); "default" for the rest. */
	TMap<FString, float> LockPower;
	float LockPowerDefault = 0.6f;
	/** "locked_try_s": the hand on the handle of a door not known to be locked, from the contact to "Locked" (TryOpen), s. */
	float LockedTrySeconds = 0.45f;
	/** "lock_shot_range_cm": he shoots the lock from up to this far (with a clear line); farther he walks to "lock_shot_stand_cm" first. */
	float LockShotRangeCm = 800.f;
	float LockShotStandCm = 250.f;
	/** "lock_shot_resolve_s": the burst's time before the lock gives (or holds). */
	float LockShotResolveSeconds = 0.6f;
	/** "blast_reach_cm": an explosion within its radius and this far of the doorway centre breaks the leaf. */
	float BlastReachCm = 300.f;
	/** "start_keys": key ids the squad holds from the start (a building id, an opening id, a door_key tag, or "*"). */
	TArray<FString> StartKeys;
	/** Montage hooks (soft object paths, empty = no clip yet): "hand_open_montage", "kick_montage", "unlock_montage" (R1; the R2 "montages" first). */
	FString HandOpenMontage;
	FString KickMontage;
	FString UnlockMontage;

	// --- R2 (designer 2026-11-22) ---
	/** "lock_shot_push": a shot lock breaks (the door unlocked, still shut) and he walks up and PUSHES it open (push clip); 0 = R1 (bursts open). */
	bool bLockShotPush = true;
	/** "push_s": the forced push-open (after the key / a shot lock) when no push clip plays: hand on the leaf, then the swing, s. */
	float PushOpenSeconds = 0.7f;
	/**
	 * "montages": skeleton name -> clip key ("open_l", "open_r", "kick_l", "kick_r", "push_l", "push_r") -> montage (or anim sequence) path.
	 * "default" serves the operatives whose skeleton is not listed; other pawns play only a set listed for their skeleton.
	 */
	TMap<FString, TMap<FString, FString>> MontageSets;
	/** "montage_contact_s": per clip kind ("open" / "kick" / "push") the seconds from the clip start to the hand / foot on the leaf. */
	TMap<FString, float> MontageContactSeconds;
	/**
	 * "montage_root_cm": per clip kind the root-motion travel of the clip (the designer's pack, root motion baked from the pelvis by
	 * setup_door_animation.py): the capsule moves this far forward over the montage (logged / checked by the smokes).
	 */
	TMap<FString, float> MontageRootCm;
	/**
	 * "montage_approach_cm": per clip kind how far in front of the leaf plane he starts the clip (its start pose): the root motion then brings
	 * him to the leaf at the contact time and leaves him at the doorway, not inside the leaf. A kind not listed: approach_cm.
	 */
	TMap<FString, float> MontageApproachCm;
	/** "montage_approach_slack_cm": a clip starts once his feet are this close to its start point (or he stopped walking near the door). */
	float MontageApproachSlackCm = 45.f;
	/** "montage_swap_lr": 1 = the pack's L / R mean the other side (swap the pick). */
	bool bMontageSwapLR = false;
	/** "breach.enabled": enemies whose path ends at a LOCKED door breach it (their archetype's actions) instead of stopping. */
	bool bBreachEnabled = true;
	/** "breach.detect_cm": his (partial) path ends this close to the locked doorway, the goal behind it. */
	float BreachDetectCm = 260.f;
	/** "breach.goal_door_cm": a locked door at the goal's level this close to the goal (the goal behind it) is breached when the path stops short elsewhere. */
	float BreachGoalDoorCm = 800.f;
	/** "breach.interval_s": how often the paths are checked, s. */
	float BreachIntervalSeconds = 0.25f;
	/** "breach.blast_stand_cm": a grenade at the door is thrown from this far (out of its own blast). */
	float BlastStandCm = 650.f;
	/** "breach.lock_shots": bursts an enemy fires at one lock before his next action. */
	int32 BreachLockShots = 2;
	/** "breach.give_up_s": a pawn that ran out of actions on a door leaves it alone this long, s. */
	float BreachGiveUpSeconds = 30.f;
	/** "breach.archetypes": archetype key ("pmc", "marksman", "brute", ...) -> actions in escalation order ("key", "shoot", "kick", "blast"). */
	TMap<FString, TArray<EDoorAction>> BreachActions;
	/** "breach.default": an archetype not listed. */
	TArray<EDoorAction> BreachDefault;
	/** "roll_seed": the seed of every door roll (kick, injury, lock shot): deterministic per seed, door, pawn and attempt (Codex.Doors.Seed). */
	int32 RollSeed = 0;
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

	/** A door this wide is heavy (doors.json heavy_min_width_cm): a higher injury chance of a kick. */
	CODEXTACTICS_API bool IsHeavy(float WidthCm, const FDoorTuning& Tuning);

	/** "wood" / "steel" / "reinforced" (case-insensitive). */
	CODEXTACTICS_API bool ParseStrength(const FString& Text, EDoorStrength& OutStrength);
	CODEXTACTICS_API const TCHAR* StrengthName(EDoorStrength Strength);

	/**
	 * THE one strength rule (designer 2026-11-22): an attack of Power 0..1 (a kick, a weapon's burst at the lock) breaks a door of Strength with
	 * (Power - resist) / (1 - resist), clamped 0..1 - monotonic in the power and in the resist, so one door ranks every way in the same.
	 */
	CODEXTACTICS_API float BreakChance(float Power, EDoorStrength Strength, const FDoorTuning& Tuning);

	/** The power of a weapon's burst at the lock (lock_power; "default" for an unlisted id, 0 for an empty id - no weapon). */
	CODEXTACTICS_API float GetLockPower(const FString& WeaponId, const FDoorTuning& Tuning);

	/** The weakest weapon power of lock_power (its default included): a kick is never stronger (the consistency rule). */
	CODEXTACTICS_API float GetWeakestLockPower(const FDoorTuning& Tuning);

	/** One kick's power against a LOCKED door: kick_power, capped at the weakest weapon power. */
	CODEXTACTICS_API float GetKickPower(const FDoorTuning& Tuning);

	/** The chance one kick opens a door of Strength in State: 0 unless closed / locked and kickable; locked = BreakChance(kick power), unlocked + the latch bonus. */
	CODEXTACTICS_API float GetKickChance(EDoorLeafState State, EDoorStrength Strength, const FDoorTuning& Tuning);

	/**
	 * One kick attempt on a door of Strength in State (closed / locked; anything else: no chance): the open chance (GetKickChance) and the
	 * injury chance (kick_injury_base + kick_injury_wounded below kick_wounded_below health + kick_injury_heavy when heavy, clamped 0..1),
	 * decided by the two rolls in 0..1 (a roll below the chance happens).
	 */
	CODEXTACTICS_API FDoorKickOutcome ResolveKick(EDoorLeafState State, EDoorStrength Strength, bool bHeavy, float HealthFraction, float RollOpen,
		float RollInjury, const FDoorTuning& Tuning);

	/** The chance one burst of WeaponId breaks the lock of a door of Strength: BreakChance(GetLockPower). */
	CODEXTACTICS_API float GetLockShotChance(const FString& WeaponId, EDoorStrength Strength, const FDoorTuning& Tuning);

	/**
	 * A door's strength: its tag door_strength=<name> (TagValue) wins; then a story lock (bStoryLocked, strength_story_locked); then its layout
	 * Role in strength_by_role ("a/b" = b for strength_mix_second_share of the doors, by a hash of OpeningId - the same door always the same);
	 * else strength_default.
	 */
	CODEXTACTICS_API EDoorStrength ResolveStrength(const FString& TagValue, const FString& Role, bool bStoryLocked, const FString& OpeningId,
		const FDoorTuning& Tuning);

	/** What the layout says about a door opening (DoorRules::ParseLayoutDoors). */
	struct FLayoutDoor
	{
		FString BuildingId;
		FString Role;
		/** The opening's own "strength" field, if any (wins like a tag). */
		FString Strength;
		/** A building story's door_state(<id>, locked) brick names it. */
		bool bStoryLocked = false;
	};

	/** The door openings of a levelgen layout (buildings[].openings + partitions[].openings, kind "door") by opening id. */
	CODEXTACTICS_API TMap<FString, FLayoutDoor> ParseLayoutDoors(const FJsonObject& Layout);

	// --- The open leaf's side (designer 2026-11-22, item 3: "in the entrance tambour the open leaf swung across the neighbouring door") ---

	/** Where a doorway / stair mouth lies, from the layout (world cm; Z = its floor). */
	struct FLayoutPlace
	{
		FString Id;
		FString BuildingId;
		FVector Center = FVector::ZeroVector;
		/** Doorways: the wall normal (opening yaw) and width; stair mouths: Normal unused, Width the flight width. */
		FVector2D Normal = FVector2D(1.f, 0.f);
		float WidthCm = 0.f;
		bool bStair = false;
	};

	/** Every door opening (leafless open ones too) and every stair mouth (flight bottom and top) of a layout; Footprints = building rectangles. */
	CODEXTACTICS_API void ParseLayoutPlaces(const FJsonObject& Layout, TArray<FLayoutPlace>& OutPlaces, TMap<FString, FBox2D>& OutFootprints);

	/** What blocks an open leaf, in the door frame (X along the wall from the doorway centre, Y across): a box, and what it is. */
	struct FDoorSwingObstacle
	{
		FBox2D Box = FBox2D(ForceInit);
		/** 0 a wall / fixed piece, 1 another doorway's approach zone, 2 a stair mouth. */
		uint8 Kind = 0;
	};

	/** The chosen swing: the side (+1 = the leaf ends on the frame's +Y side), the open angle, the free angle of each side (+ / -). */
	struct FDoorSwingChoice
	{
		float Sign = 1.f;
		float OpenDeg = 90.f;
		float FreeDegPlus = 0.f;
		float FreeDegMinus = 0.f;
		/** What stopped the chosen side short of MaxDeg (-1 = nothing). */
		int32 BlockKind = -1;
	};

	/** The open leaves at Deg on side Sign (door frame: hinge A at -X running +X, hinge B at +X running -X for a pair): their segments. */
	CODEXTACTICS_API void OpenLeafSegments(float WidthCm, int32 LeafCount, float Sign, float Deg, TArray<TPair<FVector2D, FVector2D>>& OutSegments);

	/** Segment A-B crosses (or lies in) Box. */
	CODEXTACTICS_API bool SegmentHitsBox(const FVector2D& A, const FVector2D& B, const FBox2D& Box);

	/** How far the leaves swing on side Sign before one of them meets an obstacle (3 deg steps; MaxDeg when nothing is met); OutKind what stopped it. */
	CODEXTACTICS_API float FreeSwingDeg(float WidthCm, int32 LeafCount, float Sign, const TArray<FDoorSwingObstacle>& Obstacles, float MaxDeg, int32* OutKind = nullptr);

	/**
	 * The side a door opens to, for good (designer 2026-11-22): the open leaf must lie clear of walls, other doorways' approach zones and stair
	 * mouths. PreferredSign (an entrance: outward, +1 / -1; 0 none) wins when it swings fully; else the side that swings farther; a tie -> the
	 * preferred one, else +1. The open angle is that side's free angle (never below MinDeg: where no side is free the angle is limited).
	 */
	CODEXTACTICS_API FDoorSwingChoice ChooseSwing(float WidthCm, int32 LeafCount, float PreferredSign, const TArray<FDoorSwingObstacle>& Obstacles, float MaxDeg,
		float MinDeg = 30.f);

	/** Each side's open angle (against walls / fixed pieces only) and whether the leaf open there lies over another doorway / a stair mouth. */
	struct FDoorSwingSides
	{
		float DegPlus = 90.f;
		float DegMinus = 90.f;
		bool bBlocksPlus = false;
		bool bBlocksMinus = false;
	};

	/**
	 * swing_from_opener (designer 2026-11-22): per side, the angle the leaves swing to before a wall (Kind 0) stops them (never below MinDeg),
	 * and "blocks" = a doorway approach zone / stair mouth (Kind 1 / 2) is met before that angle - such a door closes behind the squad.
	 */
	CODEXTACTICS_API FDoorSwingSides ChooseSwingSides(float WidthCm, int32 LeafCount, const TArray<FDoorSwingObstacle>& Obstacles, float MaxDeg, float MinDeg = 30.f);

	/** Close behind: the open side blocks, nobody stands in the doorway or is still to come through, and it has been so for NeedSeconds. */
	CODEXTACTICS_API bool ShouldCloseBehind(bool bSideBlocks, bool bDoorwayOccupied, bool bSomebodyToCome, float QuietSeconds, float NeedSeconds);

	/** Does one of Keys open this door: "*", the opening id, the building id (every locked door of it) or the door's own key tag. */
	CODEXTACTICS_API bool KeyFits(const TSet<FString>& Keys, const FString& BuildingId, const FString& OpeningId, const FString& DoorKeyId);

	/**
	 * What a click on a door in State orders: single click / double click / Ctrl + click (bAttack). A single click on a locked door without a key:
	 * TryOpen (walk up, try the handle, "Locked") unless the squad already knows it is locked (bKnownLocked: refused - the caller says how else).
	 */
	CODEXTACTICS_API bool ChooseClickAction(EDoorLeafState State, bool bDoubleClick, bool bAttack, bool bHasKey, bool bBroken, EDoorAction& OutAction,
		bool bKnownLocked = false);

	/** The value of a "key=value" actor tag (false when absent). */
	CODEXTACTICS_API bool FindTagValue(const TArray<FName>& Tags, const TCHAR* Key, FString& OutValue);

	// --- R2: door clips (designer 2026-11-22) ---

	/**
	 * Which side of him the handle is as he faces the leaf plane from LocalOperative (door frame: X along the wall, Y across); HandleLocalX =
	 * the handle's X (one leaf: its free edge; a pair: 0). Right when it is to his right (or straight ahead); bSwap flips the pick.
	 */
	CODEXTACTICS_API EDoorSide ChooseHandSide(const FVector2D& LocalOperative, float HandleLocalX, bool bSwap = false);

	/** The kicking leg of attempt AttemptIndex (0-based): the first on First, then the legs alternate. */
	CODEXTACTICS_API EDoorSide ChooseKickLeg(EDoorSide First, int32 AttemptIndex);

	/** The forced push after the key / a shot lock (bForced); the plain hand open otherwise. */
	CODEXTACTICS_API EDoorClip ChooseOpenClip(bool bForced);

	/** "open" / "kick" / "push". */
	CODEXTACTICS_API const TCHAR* ClipKindName(EDoorClip Clip);

	/** The montages key of a clip: "open_l", "kick_r", "push_l" ... */
	CODEXTACTICS_API FString ClipKey(EDoorClip Clip, EDoorSide Side);

	/**
	 * The clip path for a skeleton: MontageSets[SkeletonName][key], else (bUseDefault: operatives) MontageSets["default"][key], else the R1
	 * hook (hand_open_montage for open / push, kick_montage for the kick; operatives only). Empty = no clip (the doors.json timings hold).
	 */
	CODEXTACTICS_API FString FindClipPath(const FDoorTuning& Tuning, const FString& SkeletonName, bool bUseDefault, EDoorClip Clip, EDoorSide Side);

	/** Where he starts a clip, cm in front of the leaf plane: montage_approach_cm of its kind, else approach_cm. */
	CODEXTACTICS_API float GetClipApproachCm(EDoorClip Clip, const FDoorTuning& Tuning);

	/** The clip has a measured start pose (montage_approach_cm lists its kind): he walks to it before the clip starts. */
	CODEXTACTICS_API bool HasClipApproach(EDoorClip Clip, const FDoorTuning& Tuning);

	/**
	 * The clip moves the pawn (root motion -> the start pose, the measured travel): operatives only. The default set (the operatives' UE4
	 * copies, root motion baked) always does; a skeleton's own set (the PMC body) only when its clip is ON that skeleton (retargeted onto it)
	 * and has root motion - a clip of another skeleton played by bone name loses its root motion (the PMC pack-clip playback: 0 cm).
	 */
	CODEXTACTICS_API bool ClipMovesPawn(bool bOperative, bool bOwnSet, bool bClipOnPawnSkeleton, bool bClipHasRootMotion);

	/** Seconds from the start of the work to the leaf moving / the kick landing: the clip's contact time when a clip plays, else hand_open_s / kick_s / push_s. */
	CODEXTACTICS_API float GetContactSeconds(EDoorClip Clip, bool bClipPlays, const FDoorTuning& Tuning);

	// --- R2: enemies breach locked doors (designer 2026-11-22) ---

	/** "key" / "shoot" / "kick" / "blast" -> Unlock / ShootLock / Kick / Blast (case-insensitive). */
	CODEXTACTICS_API bool ParseBreachAction(const FString& Text, EDoorAction& OutAction);

	/** The breach list key of a pawn: "pmc" for a hostile operative, else its archetype ("brute", "frost_hound", "frostbitten", "cutter", "marksman", "base"). */
	CODEXTACTICS_API FString GetArchetypeKey(bool bHostileOperative, EEnemyArchetype Archetype);

	/** The actions of an archetype key in escalation order (breach.archetypes, else breach.default). */
	CODEXTACTICS_API const TArray<EDoorAction>& GetBreachActions(const FDoorTuning& Tuning, const FString& ArchetypeKey);

	/**
	 * The next breach action: the first of Actions not in Tried that he can do now (key: bHasKey; shoot: bCanShoot; blast: bHasGrenade;
	 * kick: bCanKick and kicks enabled). False when none is left (he gives up on the door).
	 */
	CODEXTACTICS_API bool NextBreachAction(const TArray<EDoorAction>& Actions, const TSet<EDoorAction>& Tried, const FDoorBreachAbility& Ability,
		const FDoorTuning& Tuning, EDoorAction& OutAction);

	/**
	 * A path stopped by a locked door (door frame): its end lies near the doorway (|x| <= HalfWidth + DetectCm, |y| <= DetectCm) on the pawn's
	 * side and its goal on the other side (|y| >= 30 cm): the path was cut short by the door.
	 */
	CODEXTACTICS_API bool IsPathStoppedByDoor(const FVector2D& LocalPawn, const FVector2D& LocalPathEnd, const FVector2D& LocalGoal, float HalfWidthCm,
		float DetectCm);

	/**
	 * The goal lies right behind this door (door frame): within ReachCm of the doorway (|x| <= HalfWidth + ReachCm, 30 <= |y| <= ReachCm).
	 * A partial path may end far from the door (the navmesh point nearest the goal is a storey below): a locked door next to the goal, at
	 * its level, is the one to breach. Returns the side of the goal (+1 / -1), 0 when not behind it.
	 */
	CODEXTACTICS_API float GoalBehindDoorSide(const FVector2D& LocalGoal, float HalfWidthCm, float ReachCm);

	/**
	 * The doorways of OPEN doors on a kit-dressed map (the builder places no leaf for an open door; kit_dress places open leaves, standing
	 * out of the wall - kit_dress_lib.leaves: the leaf's +X is the wall normal, its doorway lies on its -Y side from the hinge). Two leaves
	 * facing the same way whose hinges are 2 x width apart along -Y are a pair (one doorway, both hinges); the rest are single leaves.
	 */
	CODEXTACTICS_API TArray<FDoorKitDoorway> PairKitOpenLeaves(const TArray<FDoorKitLeafPiece>& Leaves, float ToleranceCm = 15.f);

	/**
	 * Does the cursor ray (door frame of one leaf: the hinge at the origin, the leaf along +X - or -X when bNegativeX - its thickness on Y,
	 * floor at Z 0) pass through that leaf's box (SlackCm round it)? The click on an OPEN leaf (its collision is off).
	 */
	CODEXTACTICS_API bool RayHitsLeaf(const FVector& LocalStart, const FVector& LocalEnd, float LeafWidthCm, float ThicknessCm, float HeightCm, bool bNegativeX,
		float SlackCm);

	/** A floor click at Local (door frame) right in front of / in a SHUT doorway counts as the door (|x| <= half + 10, |y| <= DepthCm). */
	CODEXTACTICS_API bool IsInShutDoorwaySnap(const FVector2D& Local, float HalfWidthCm, float DepthCm);

	/** Degrees between his facing and the way into the door from his side (smokes: every kick attempt faces the leaf). */
	CODEXTACTICS_API float FacingErrorDeg(const FVector2D& Forward, const FVector2D& LocalOperativeSide, const FVector2D& DoorNormal);

	/** A deterministic roll 0..1 of Seed and Salt (the same pair always gives the same value). */
	CODEXTACTICS_API float DeterministicRoll(int32 Seed, uint32 Salt);

	/** The salt of one door roll: the door's id, the pawn's id, the attempt and what is rolled ("kick", "injury", "lock"). */
	CODEXTACTICS_API uint32 RollSalt(const FString& DoorId, const FString& PawnId, int32 Attempt, const TCHAR* What);
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

	/** The roll seed now: Codex.Doors.Seed when >= 0, else -DoorSeed= on the command line, else doors.json roll_seed. */
	CODEXTACTICS_API int32 GetRollSeed();
}
