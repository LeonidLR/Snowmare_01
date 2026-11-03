#pragma once

#include "CoreMinimal.h"
#include "Combat/KnockdownTypes.h"

/**
 * Pure rules of falls from heights, running drops off an edge and knockback falls (game designer items 6 / 10 / 11a, 2026-11-03;
 * docs/systems/stances-and-movement.md "Falls and drops"). No UObject: unit tested in CodexTactics.Falls.Rules.*. UE-only system (no Godot
 * reference). The values are tuned in Content/Data/Movement/falls.json (FFallConfig mirrors it, test TuningFileMirrorsDefaults).
 *
 * The fall damage reuses the ladder-fall curve (LadderRules::FallDamage: clamp((H - Safe) * PerCm, 0, Max)) with one level free (Safe 320 cm)
 * below 9 m; 9-12 m is a heavy wound (60-80 HP and the knockdown pose on landing), 12 m and more kills (user decision 2026-11-03); drop links where a
 * walkable lower surface is 1.5-6.5 m below and at most 2.5 m beyond the edge; drops cost the pathfinder extra (UNavArea_Drop / _DropHurt) so a
 * stair / ramp / ladder of similar length is preferred; knockback falls only from the existing knockdowns, never over a waist-high parapet.
 */
namespace FallRules
{
	/** falls.json "params" (the comments name the keys). Distances cm, speeds cm/s, times s. */
	struct FFallConfig
	{
		/** damage.safe_height_cm: a fall up to this height is free (one 3 m level + the slab). */
		float SafeHeightCm = 320.f;
		/** damage.per_cm: HP per cm above SafeHeightCm (the ladder-fall curve). */
		float DamagePerCm = 0.1f;
		/** damage.max: the damage cap of the curve. */
		float MaxDamage = 40.f;
		/**
		 * damage.heavy_height_cm: from this height (up to the lethal one) a fall is a HEAVY WOUND (user decision 2026-11-03: 9-12 m): the damage
		 * runs from damage.heavy_min at this height to damage.heavy_max at the lethal height (an operative at full health, 100 HP, survives it)
		 * and he stays down in the knockdown pose for the knockdown's downed time before he gets up. 0 = off (the curve up to the lethal height).
		 */
		float HeavyHeightCm = 900.f;
		float HeavyDamageMin = 60.f;
		float HeavyDamageMax = 80.f;
		/**
		 * damage.heavy_max_health_fraction: a heavy wound never takes more than this share of the body's max health (anyone at full health
		 * survives 9-12 m, as the user decided - a 60 HP Frostbitten loses at most 51, a 45 HP hound 38; the operatives' 100 HP: 60-80 as is).
		 */
		float HeavyMaxHealthFraction = 0.85f;
		/** damage.lethal_height_cm: a fall at least this high kills (user decision 2026-11-03: 12 m, operatives and enemies); 0 = off. */
		float LethalHeightCm = 1200.f;
		/** track.min_fall_cm: a fall shorter than this is not logged / counted (steps, spawn settles). */
		float MinTrackedFallCm = 50.f;

		/** link.min_drop_cm / link.max_drop_cm: a drop link's lower surface lies this far below the edge. */
		float LinkMinDropCm = 150.f;
		float LinkMaxDropCm = 650.f;
		/** link.max_gap_cm: the lower surface starts at most this far beyond the edge (the running drop). */
		float LinkMaxGapCm = 250.f;
		/** link.spacing_cm: one link per this much of an edge. */
		float LinkSpacingCm = 150.f;
		/** link.landing_margin_cm: the landing aims this far past the lower surface's near border (and before its far border). */
		float LandingMarginCm = 40.f;
		/** link.reference_speed_cms: a link is made only when a run-up at this speed reaches its landing (a healthy sprinter: 725). */
		float ReferenceSpeedCms = 600.f;
		/** link.min_lower_depth_cm: the lower surface must be at least this deep along the jump. */
		float MinLowerDepthCm = 120.f;
		/** link.probe_step_cm: the edge / gap / landing profile's sample step. */
		float ProbeStepCm = 15.f;
		/** link.step_tolerance_cm: a floor within this height counts as the same surface. */
		float StepToleranceCm = 40.f;
		/** link.edge_search_cm: the edge must be this close to the link start (the navmesh boundary). */
		float EdgeSearchCm = 120.f;
		/** link.max_links: the generator stops at this many links (a log line says so). */
		float MaxLinks = 800.f;

		/** takeoff.min_speed_cms: the slowest step off an edge. */
		float TakeoffMinSpeedCms = 150.f;
		/** takeoff.speed_margin: the needed run-up speed times this. */
		float TakeoffSpeedMargin = 1.1f;
		/** takeoff.max_speed_cms: the run-up cap of a body without its own sprint speed (an enemy). */
		float TakeoffMaxSpeedCms = 725.f;
		/** takeoff.runup_timeout_s: the run-up to the edge gives up after this long (blocked). */
		float RunUpTimeoutSeconds = 1.5f;
		/**
		 * air.gravity_scale: gravity x this while a body is in a drop / knockback throw / any other ledge fall (user 2026-11-03: the falls looked
		 * like gliding - 2.25 = the fall time / 1.5, sqrt(2.25) = 1.5). The run-up plan, the link generation's reach and the HUD's "TOO FAR" use the
		 * same gravity (AirGravity); a knockback throw's speeds are x sqrt(scale), so its arc - and reach - stay and only play 1.5x faster.
		 */
		float AirGravityScale = 2.25f;
		/** air.timeout_s: a fall that never lands is ended after this long. */
		float AirTimeoutSeconds = 6.f;
		/** land.settle_s: after landing he stands this long (the land clip) before the path goes on. */
		float LandSettleSeconds = 0.35f;
		/** lease.radius_cm: a drop whose landing is this close to another man's running drop waits (one at a time). */
		float LeaseRadiusCm = 220.f;
		/** lease.hold_after_landing_s: the landing spot stays taken this long after he landed (he walks off it). */
		float LeaseHoldAfterLandingSeconds = 0.6f;

		/** knockback.reach_cm: a knockdown throws him over an edge at most this far away. */
		float KnockbackReachCm = 120.f;
		/** knockback.speed_cms / knockback.up_cms: the throw's horizontal / upward speed. */
		float KnockbackSpeedCms = 420.f;
		float KnockbackUpCms = 200.f;
		/** knockback.parapet_height_cm: an obstacle this tall at the edge (a waist-high railing / parapet) stops the throw. */
		float ParapetHeightCm = 80.f;
		/** knockback.min_fall_cm: a knockback fall needs at least this drop beyond the edge. */
		float KnockbackMinFallCm = 150.f;

		/** anim.fall_min_s: a body falls this long (movement mode Falling) before the operative ABP's Falling state takes over (a step down never shows it). */
		float AnimFallMinSeconds = 0.25f;
		/** anim.land_s: after a fall the ABP's Landing state (the land clip) plays this long before locomotion comes back. */
		float AnimLandSeconds = 0.45f;

		/**
		 * enemy_drops.<archetype>: which generated drop links an enemy type paths through (user decision 2026-11-03; EEnemyDropUse as a number:
		 * 0 none, 1 free drops (no damage; a hurting one only when there is no other way), 2 jumper (every drop at its normal cost, a faster
		 * run-up). Defaults: the hound / the cutter jump; the zombies, the PMC fighters (Frostbitten bodies) and the Marksman drop; the Brute, the
		 * Spitter and the Cryo Drone never. A Blueprint may override it (AEnemyCharacter::DropLinkUseOverride).
		 */
		float EnemyDropsBase = 1.f;
		float EnemyDropsFrostHound = 2.f;
		float EnemyDropsSpitter = 0.f;
		float EnemyDropsBrute = 0.f;
		float EnemyDropsFrostbitten = 1.f;
		float EnemyDropsCutter = 2.f;
		float EnemyDropsCryoDrone = 0.f;
		float EnemyDropsMarksman = 1.f;
		/** enemy_drops.hurt_entering_cm: a free-drop enemy's extra path cost of a hurting drop (cm of path; it takes one only when there is no other way). */
		float EnemyHurtDropEnteringCm = 6000.f;
		/** enemy_drops.jumper_max_speed_cms: a jumper's run-up cap off an edge (a pouncing body; the others: takeoff.max_speed_cms). */
		float JumperTakeoffMaxSpeedCms = 900.f;
	};

	/** What a fall did to the body (FallRules::JudgeFall). */
	enum class EFallOutcome : uint8
	{
		/** Up to damage.safe_height_cm: nothing. */
		Unhurt,
		/** The ladder curve's damage. */
		Hurt,
		/** damage.heavy_height_cm .. damage.lethal_height_cm: big damage and the knockdown on landing. */
		HeavyWound,
		/** damage.lethal_height_cm and more: dead. */
		Lethal
	};

	/** The landing's verdict: the damage, the outcome band and whether he goes down in the knockdown pose. */
	struct FFallVerdict
	{
		float Damage = 0.f;
		EFallOutcome Outcome = EFallOutcome::Unhurt;
		bool bKnockdown = false;
	};

	/** Which drop links an enemy type may path through (enemy_drops.<archetype>). */
	enum class EEnemyDropUse : uint8
	{
		None,
		/** No-damage drops; a hurting one costs enemy_drops.hurt_entering_cm extra (only when there is no other way). */
		Free,
		/** Every drop at its normal cost and a pouncing run-up (enemy_drops.jumper_max_speed_cms). */
		Jumper
	};

	/** The operative ABP's fall states (UOperativeAnimInstance bFallAirborne / bFallLanding / bFallGrounded). */
	enum class EFallAnimPhase : uint8
	{
		Grounded,
		Falling,
		Landing
	};

	// --- Damage ----------------------------------------------------------------------------------------------------------

	/**
	 * The landing of a fall of HeightCm (user decision 2026-11-03): below damage.heavy_height_cm the ladder-fall curve (one level free); from it
	 * to damage.lethal_height_cm a heavy wound (heavy_min .. heavy_max, linear, and the knockdown); from the lethal height dead (LethalDamage).
	 * A band whose height is 0 is off.
	 */
	CODEXTACTICS_API FFallVerdict JudgeFall(float HeightCm, const FFallConfig& Config);

	/** JudgeFall for a body of MaxHealth: a heavy wound's damage capped at damage.heavy_max_health_fraction of it (MaxHealth <= 0 = no cap). */
	CODEXTACTICS_API FFallVerdict JudgeFallForBody(float HeightCm, float MaxHealth, const FFallConfig& Config);

	/** JudgeFall(HeightCm).Damage. */
	CODEXTACTICS_API float FallDamage(float HeightCm, const FFallConfig& Config);

	/** The enemy type's drop use: enemy_drops.<archetype> (ArchetypeIndex = EEnemyArchetype as a number; unknown = None). */
	CODEXTACTICS_API EEnemyDropUse EnemyDropUse(int32 ArchetypeIndex, const FFallConfig& Config);

	/**
	 * The ABP fall phase this frame: Falling while a drop / knockback is in the air (bDropAirborne) or the movement has been Falling for at
	 * least anim.fall_min_s; Landing for anim.land_s after a Falling phase ended (SecondsSinceFallEnded); Grounded else.
	 */
	CODEXTACTICS_API EFallAnimPhase FallAnimPhase(bool bDropAirborne, float FallingSeconds, float SecondsSinceFallEnded, const FFallConfig& Config);

	/** What FallDamage returns for a lethal fall (more than any body's health). */
	constexpr float LethalDamage = 100000.f;

	/** A drop that hurts (FallDamage > 0): its link gets the dearer area (UNavArea_DropHurt). */
	CODEXTACTICS_API bool IsHurtingDrop(float DropCm, const FFallConfig& Config);

	// --- Ballistics ------------------------------------------------------------------------------------------------------

	/** The gravity of a ledge fall (cm/s2): WorldGravityCmS2 x air.gravity_scale (at least x 0.1). */
	CODEXTACTICS_API float AirGravity(float WorldGravityCmS2, const FFallConfig& Config);

	/** A knockback throw's speeds x this (sqrt of air.gravity_scale): the same arc under the stronger gravity, played faster. */
	CODEXTACTICS_API float KnockbackLaunchScale(const FFallConfig& Config);

	/** Seconds to fall DropCm from rest (vertically): sqrt(2 H / g); 0 for H <= 0 or g <= 0. */
	CODEXTACTICS_API float FallSeconds(float DropCm, float GravityCmS2);

	/** The run-up off an edge (a running drop keeps its horizontal speed while it falls). */
	struct FTakeoffPlan
	{
		/** The horizontal speed he leaves the edge with, cm/s. */
		float SpeedCms = 0.f;
		/** The fall time to the lower surface, s. */
		float Seconds = 0.f;
		/** Where he comes down, horizontally from the link start, cm. */
		float LandingCm = 0.f;
		/** The landing lies inside the lower surface's window (near + margin / 2 .. far - margin / 2). */
		bool bReaches = false;
		/** He could not run fast enough (the speed was capped below the needed one): he falls short. */
		bool bTooSlow = false;
	};

	/**
	 * The run-up for a drop link: EdgeCm / NearCm / FarCm are the edge, the lower surface's near and far border horizontally from the link
	 * start (FarCm <= 0 = open), DropCm the height. He keeps ApproachSpeed when it lands him inside the window, speeds up to the needed one
	 * (x TakeoffSpeedMargin, at least TakeoffMinSpeedCms) and checks his momentum so he does not sail past a narrow lower roof - never faster
	 * than CapSpeed (his sprint, slowed by wounds / cold / a load): then he falls short (bTooSlow).
	 */
	CODEXTACTICS_API FTakeoffPlan PlanTakeoff(float ApproachSpeed, float CapSpeed, float EdgeCm, float NearCm, float FarCm, float DropCm, float GravityCmS2,
		const FFallConfig& Config);

	// --- Drop link profile -----------------------------------------------------------------------------------------------

	/** One down-probe of the profile beyond a navmesh boundary point: at DistCm along the outward normal, the floor DropCm below the start floor. */
	struct FProfileSample
	{
		float DistCm = 0.f;
		bool bHit = false;
		/** Positive = below the start floor. */
		float DropCm = 0.f;
	};

	/** Why a profile gives no link (logs / tests). */
	enum class EProfileVerdict : uint8
	{
		Ok,
		NoEdge,
		Blocked,
		LowStep,
		GapTooWide,
		NoLowerSurface,
		TooShallow,
		/** The generator's own checks after the profile (UDropLinkSubsystem): no reachable landing / no navmesh there / the arc hits something. */
		Unreachable,
		NoLanding,
		ArcBlocked,
		Count
	};

	/** The analysed profile: the edge, the lower surface's borders and its drop. */
	struct FDropProfile
	{
		EProfileVerdict Verdict = EProfileVerdict::NoEdge;
		float EdgeCm = 0.f;
		float NearCm = 0.f;
		/** <= 0: the lower surface runs past the last sample (open). */
		float FarCm = 0.f;
		float DropCm = 0.f;
		bool IsOk() const { return Verdict == EProfileVerdict::Ok; }
	};

	/**
	 * Reads the samples (increasing DistCm): the start floor runs on (within StepToleranceCm) up to the EDGE (no floor / lower by more); a
	 * floor above the start floor first = Blocked (a wall); in the gap a floor LinkMinDropCm..LinkMaxDropCm below starts the lower surface
	 * (NEAR) - a lower step than that = LowStep, a gap wider than LinkMaxGapCm = GapTooWide; the lower surface runs while it stays within the
	 * tolerance of its height (FAR = the first sample off it) - a higher surface still within the gap replaces it (IsHigherLanding: the lower roof
	 * beyond the gap's ground); shallower than MinLowerDepthCm = TooShallow. No edge within EdgeSearchCm = NoEdge.
	 */
	CODEXTACTICS_API FDropProfile AnalyseProfile(const TArray<FProfileSample>& Samples, const FFallConfig& Config);

	/**
	 * While on a lower surface: Sample starts a HIGHER one (a drop of at least LinkMinDropCm, more than the tolerance above the current) still
	 * within the gap - the lower roof beyond the ground of the gap is the landing, not the ground.
	 */
	CODEXTACTICS_API bool IsHigherLanding(const FProfileSample& Sample, const FDropProfile& Current, const FFallConfig& Config);

	/** Samples along an edge segment of Length: max(1, round(Length / Spacing)) at (i + 0.5) / n of it (fractions 0..1). */
	CODEXTACTICS_API TArray<float> EdgeSampleFractions(float LengthCm, float SpacingCm);

	/** One link per spacing: a new start within 0.8 x spacing of a kept one landing on the same level (|dz| < 100 cm) is a duplicate. */
	CODEXTACTICS_API bool IsDuplicateStart(const FVector& Start, float LandZ, const TArray<FVector>& KeptStarts, const TArray<float>& KeptLandZ, float SpacingCm);

	/** Two drops' landings this close (2D) are one landing spot for the one-at-a-time rule. */
	CODEXTACTICS_API bool LandingsConflict(const FVector& LandingA, const FVector& LandingB, float RadiusCm);

	/**
	 * The order path's drop marker text (the HUD): "DROP 3.0 m", with the expected damage "DROP 6.0 m  -28 HP", a lethal one "DROP 9.0 m  LETHAL",
	 * a run-up that falls short "DROP 3.0 m  TOO FAR" (then the damage of the fall to wherever he comes down is not known: no number).
	 */
	CODEXTACTICS_API FString DropMarkerLabel(float DropCm, float Damage, bool bFallsShort);

	// --- Knockback falls -------------------------------------------------------------------------------------------------

	/**
	 * The throw of a knockdown: away from the blow's source (Victim - Source, 2D) when known, else by the fall side - on the back = backwards
	 * (-Forward), on the face = forwards. Unit, 2D.
	 */
	CODEXTACTICS_API FVector KnockbackDirection(EKnockdownDirection Side, const FVector& Forward, bool bHasSource, const FVector& VictimLocation, const FVector& SourceLocation);

	/** How far the throw carries him before he would come down on his own level again: Speed x 2 Up / g. */
	CODEXTACTICS_API float KnockbackFlightCm(float SpeedCms, float UpCms, float GravityCmS2);

	/**
	 * The throw takes him over the edge: no parapet / railing (bObstacle) before it, the edge within KnockbackReachCm and inside the flight
	 * (EdgeCm + CapsuleRadius / 2 <= flight), and at least KnockbackMinFallCm of drop beyond it.
	 */
	CODEXTACTICS_API bool KnockbackGoesOver(float EdgeCm, bool bObstacle, float DropCm, float CapsuleRadiusCm, float GravityCmS2, const FFallConfig& Config);

	// --- Tuning file (Content/Data/Movement/falls.json) ------------------------------------------------------------------

	CODEXTACTICS_API FString GetDefaultPath();
	CODEXTACTICS_API TArray<FString> KnownKeys();
	CODEXTACTICS_API bool GetValue(const FFallConfig& Config, const FString& Key, float& OutValue);
	/** Applies the "params" object of JsonText onto InOut; false when unreadable / no params; unknown or non-numeric keys go to OutUnknown. */
	CODEXTACTICS_API bool ApplyJson(const FString& JsonText, FFallConfig& InOut, TArray<FString>* OutUnknown = nullptr);
	/** The values in force (loaded from the file on the first call). */
	CODEXTACTICS_API const FFallConfig& GetConfig();
	CODEXTACTICS_API void SetConfig(const FFallConfig& Config);
	CODEXTACTICS_API bool ReloadConfig();
}
