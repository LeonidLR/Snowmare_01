#pragma once

#include "CoreMinimal.h"
#include "Engine/HitResult.h"

/**
 * Mantling (climbing onto / over obstacles), slice 1 (docs/design/MANTLING.md section 11, designer answers 11.6): the pure rules - which
 * height class an obstacle is, whether the operative ends on top or on the far side, whether the landing has headroom, which clip plays
 * (standing / running, male / female, round-robin between variants) and why a climb is refused. No UObject: unit tested in
 * CodexTactics.Characters.Mantle.*. The runtime (AOperativeCharacter, OperativeCharacterMantle.cpp) measures the obstacle with traces
 * (FMantleProbe) and asks Decide.
 *
 * Data: the height classes (nominal +- tolerance) come from Content/Data/LevelGen/obstacle_heights.json (the level generator's height
 * standard - one source for the generator and the runtime); the mantle's own parameters, the per-class action and the clip table from
 * Content/Data/Movement/mantle.json. The defaults below mirror both files (test CodexTactics.Characters.Mantle.TuningFileMirrorsDefaults).
 * UE-only (Godot had no mantling).
 */

/** Where the climb ends. */
enum class EMantleDestination : uint8
{
	/** On the obstacle's top (low / high mantle, tall ledge, very tall). */
	Top,
	/** Over it, on the far side's ground (low vault, high fence over). */
	Over
};

/** The approach the clip is authored for. */
enum class EMantleGait : uint8
{
	Standing,
	Running
};

/** The retargeted clip set (the operatives' two skeletons; a clip of one never plays on the other). */
enum class EMantleBody : uint8
{
	Male,
	Female
};

/** Why no climb happens (MantleRules::RefusalName for the logs / smokes). */
enum class EMantleRefusal : uint8
{
	None,
	/** No obstacle face in front / an invalid measurement. */
	NoObstacle,
	/** The obstacle is not a mantle target (an explosive barrel, a building wall without a mantle edge mark, a silo ...). */
	NotEligible,
	/** Lower than every class with this destination: a step (the movement walks up) or nothing to climb. */
	TooLow,
	/** A low vault: the existing vault (OperativeCharacterVault.cpp) takes it. */
	VaultInstead,
	/** Between two class bands (no clip fits this height). */
	BetweenClasses,
	/** Above the traversal ceiling (wall_no_traverse): no climb, ever. */
	Wall,
	/** The class exists but is switched off in mantle.json. */
	ClassDisabled,
	/** The class needs a marked mantle edge (the one-storey roofs) and this one has none. */
	NotMarked,
	/** Neither a top to stand on (deep enough) nor thin enough to climb over. */
	DepthMismatch,
	/** No room to stand up on the landing (a roof / beam over it). */
	NoHeadroom,
	/** No free landing: the top's footprint or the far side's ground does not take the capsule (or is occupied). */
	NoLanding,
	/** The far side's ground lies too far below / above the feet for an over clip. */
	LandingStep,
	/** No clip of this class for this body. */
	MissingClip,
	/** The clip would have to be stretched / squeezed beyond the warp limits. */
	WarpTooLarge,
	Count
};

/** Why a running climb ended early (AOperativeCharacter::CancelMantle). */
enum class EMantleCancel : uint8
{
	None,
	/** A knockdown: he drops back to the base, then the knockdown plays there (designer Q5). */
	Knockdown,
	/** A hit of at least heavy_hit_damage: he drops back to the base. */
	HeavyHit,
	/** Killed mid-climb: everything released, the body falls (the corpse fall). */
	Death,
	/** The actor leaves play. */
	EndPlay,
	/** The montage never played / stopped by someone else, or the clip ran past its length + timeout_pad_s. */
	Interrupted
};

/** One height class (obstacle_heights.json "classes.<id>" + mantle.json "classes.<id>"). */
struct FMantleHeightClass
{
	FName Id;
	float NominalCm = 0.f;
	float ToleranceCm = 0.f;
	EMantleDestination Destination = EMantleDestination::Top;
	/** mantle.json "enabled": false keeps the class out of the climb (refused ClassDisabled). */
	bool bEnabled = true;
	/** mantle.json "marked_edge_only": only an obstacle tagged as a marked mantle edge (MantleRules::MarkedEdgeTag) may be climbed. */
	bool bMarkedEdgeOnly = false;
	/** mantle.json "handled_by": "vault" - the existing vault takes it (refused VaultInstead). */
	bool bHandledByVault = false;

	float MinCm() const { return NominalCm - ToleranceCm; }
	float MaxCm() const { return NominalCm + ToleranceCm; }
};

/** One clip (mantle.json "clips"); the measurements are the retargeted copy's root track (Scripts/Editor/setup_mantle_animation.py). */
struct FMantleClip
{
	FName ClassId;
	EMantleGait Gait = EMantleGait::Standing;
	EMantleBody Body = EMantleBody::Male;
	/** Object path of the retargeted root-motion sequence. */
	FString Path;
	float LengthSeconds = 0.f;
	/** Top: the root's end rise; Over: its peak rise (cm). */
	float RiseCm = 0.f;
	/** The root's forward travel over the whole clip (cm). */
	float ForwardCm = 0.f;
	/** Over clips: the face's distance from the start (cm; 0 = the config's over.start_cm). */
	float StartCm = 0.f;
};

/** Everything the rules read (the defaults mirror the two JSON files). */
struct FMantleConfig
{
	TArray<FMantleHeightClass> Classes;
	TArray<FMantleClip> Clips;
	/** Above this (cm over the feet) every obstacle is a wall (obstacle_heights.json wall_no_traverse.min_cm). "params.ceiling_cm" */
	float CeilingCm = 320.f;
	/** A top at least this deep along the climb is stood on; thinner is climbed over (if a class allows it), cm. "params.top_min_depth_cm" */
	float TopMinDepthCm = 90.f;
	/** At most this thick: climbed over (a fence); between this and TopMinDepthCm: DepthMismatch, cm. "params.over_max_depth_cm" */
	float OverMaxDepthCm = 60.f;
	/** The landing on a top lies this far past the edge (the capsule centre), cm. "params.top_inset_cm" */
	float TopInsetCm = 50.f;
	/** The capsule centre never starts closer to the face than this, cm. "params.min_face_gap_cm" */
	float MinFaceGapCm = 45.f;
	/** Over clips: the face's distance from the start when a clip has none, cm. "params.over_start_cm" */
	float OverStartCm = 110.f;
	/** The far side's ground may differ from the feet by this much for an over clip, cm. "params.over_max_step_cm" */
	float OverMaxStepCm = 40.f;
	/** Free room needed over the landing on top of the standing capsule height, cm. "params.headroom_margin_cm" */
	float HeadroomMarginCm = 10.f;
	/** The clip's rise may be warped by at most this fraction (|height / rise - 1|). "params.max_vertical_warp" */
	float MaxVerticalWarp = 0.25f;
	/** The clip's forward travel may be warped by at most this fraction. "params.max_forward_warp" */
	float MaxForwardWarp = 0.35f;
	/** A standing climb starts when the face is at most the clip's start distance + this, cm (else he walks closer). "params.standing_slack_cm" */
	float StandingSlackCm = 40.f;
	/** He counts as running above this fraction of his walk speed (or sprinting). "params.run_speed_fraction" */
	float RunSpeedFraction = 1.1f;
	/** Seconds after a climb before the next one may start (no chained re-trigger). "params.cooldown_s" */
	float CooldownSeconds = 0.8f;
	/** The climb is abandoned (state restored) this long after the clip's own length, s. "params.timeout_pad_s" */
	float TimeoutPadSeconds = 1.5f;
	/** Montage blend in / out, s. "params.blend_in_s" / "params.blend_out_s" */
	float BlendInSeconds = 0.12f;
	float BlendOutSeconds = 0.2f;
	/** A hit of at least this much damage mid-climb drops him to the base (a knockdown always does), HP. "params.heavy_hit_damage" */
	float HeavyHitDamage = 35.f;
	/** End-of-clip correction onto the planned landing: larger errors are logged as a warning, cm. "params.landing_snap_warn_cm" */
	float LandingSnapWarnCm = 25.f;
	/** Path links: spacing along a face, cm. "links.spacing_cm" */
	float LinkSpacingCm = 150.f;
	/** Path links: a face shorter than this gets one link at its middle, cm. "links.min_face_cm" */
	float LinkMinFaceCm = 90.f;
	/** Path links: at most this many per obstacle face. "links.max_per_face" */
	float LinkMaxPerFace = 4.f;
	/** Path links: the mantle area's entering cost (cm of walking it is worth) - only a real short cut climbs. "links.entering_cost_cm" */
	float LinkEnteringCostCm = 600.f;
	/** Path links: at most this many on a map (a runaway layout); slice 1 had a fixed 900, which a Depot already filled - the generated X4 / X6
	 *  maps then had links over a corner of the map only (batch 2 item 9). "links.max_total" */
	float LinkMaxTotal = 12000.f;
	/** Path links: a top under this height also gets a link back down (the drop links start at falls.json link.min_drop_cm), cm. "links.down_below_cm" */
	float LinkDownBelowCm = 150.f;
	/** Click order: the clicked top must lie within this distance of the operative to be taken as a climb order, cm. "order.max_distance_cm" */
	float OrderMaxDistanceCm = 3000.f;
	/** Click order on an obstacle's side face (batch 2): the top is looked for this far inside the clicked face, cm. "order.face_inset_cm" */
	float OrderFaceInsetCm = 15.f;
	/** A second climb onto a landing another man holds must be at least this far from it, cm. "reserve.radius_cm" */
	float ReserveRadiusCm = 150.f;
	/** A clip's authored motion is sampled at this rate for the contact alignment (GetMantleClipTrack), Hz. "align.sample_hz" */
	float AlignSampleHz = 30.f;
	/** Acceptance (MantleAlignSmoke): the nearer hand comes at least this close to the ledge's lip during the climb, cm. "align.hand_edge_tolerance_cm" */
	float AlignHandEdgeToleranceCm = 15.f;
	/** Acceptance: the body (lowest foot / capsule) never rises more than this over the final standing height (no pop), cm. "align.pop_tolerance_cm" */
	float AlignPopToleranceCm = 15.f;
	/** Acceptance: the body stands still at most this long between the climb and moving on (no freeze at the handoff), s. "align.freeze_limit_s" */
	float AlignFreezeLimitSeconds = 0.35f;
	/** A bone slower than this counts as still (the freeze measure and the clip's settle time), cm/s. "align.still_speed_cm_s" */
	float AlignStillSpeedCmPerSecond = 20.f;
	/** A hand slower than this in the clip's own space (it rests on something fixed) is planted, cm/s. "align.plant_speed_cm_s" */
	float AlignPlantSpeedCmPerSecond = 80.f;
	/** ... after it reached out at least this far from where it hung at frame 0 (not the idle arm), cm. "align.plant_reach_cm" */
	float AlignPlantReachCm = 25.f;
	/** ... and at least this high over the frame-0 feet as a fraction of the clip's rise (on the ledge, not on a knee). "align.plant_min_rise_fraction" */
	float AlignPlantMinRiseFraction = 0.6f;
	/** ... staying planted at least this long, s. "align.plant_hold_s" */
	float AlignPlantHoldSeconds = 0.1f;
	/**
	 * B17-3: a clip whose hands never rest on the ledge (the run clips: RunHighClimb's hands slide along the top at ~100-150 cm/s) takes its
	 * slowest hand frame at the ledge's height (within align.hand_edge_tolerance_cm) as the contact, when that hand is no faster than this,
	 * cm/s. "align.slide_speed_cm_s"
	 */
	float AlignSlideSpeedCmPerSecond = 250.f;
	/** ... and no faster than this fraction of the pelvis then (the body climbs past the hand; a hand hanging on the body moves with it). "align.slide_body_ratio" */
	float AlignSlideBodyRatio = 0.5f;
	/** B17-2 (designer option A): the hover ghost shows the clip's frame this far through the contact hand's hold on the lip (0 = the plant). "ghost.pose_fraction" */
	float GhostPoseFraction = 0.5f;
	/** A hand resting once the root is within this distance of its last spot is the end pose, not a plant, cm. "align.exit_root_cm" */
	float AlignExitRootCm = 3.f;
	/** A planted wrist sits this far past the lip and this high over the top (the hand's point is put there, not on the lip), cm.
	 *  "align.grip_forward_cm" / "align.grip_up_cm" */
	float AlignGripForwardCm = 8.f;
	float AlignGripUpCm = 6.f;
	/** Acceptance (batch 2, MantlingProblem_03 / 05): a torso joint (pelvis, spine, head) below the top stays at least this far outside the
	 *  obstacle's footprint while he climbs up or comes down (no clipping into the mesh), cm. "align.min_body_gap_cm" */
	float AlignMinBodyGapCm = 10.f;
	/** ... a joint higher than this under the top is over it (the body comes over the lip there), cm. "align.gap_above_margin_cm" */
	float AlignGapAboveMarginCm = 20.f;
	/** The climb hands over once every bone of the clip (root, pelvis, hands, feet) stays within this of its final pose to the end (the rest
	 *  is a slow settle the blend-out absorbs; MantlingProblem_04 "hangs ~0.5 s in a strange pose"); the acceptance's quiet-tail measure, cm.
	 *  "align.exit_pose_cm" */
	float AlignExitPoseCm = 8.f;
	/** The wall guard: while a torso joint (pelvis, spine, head) is below the top it is kept at least this far in front of the face (the
	 *  capsule is pushed back by the deficit; MantlingProblem_03 / 04 "sinks into the obstacle"), cm. "align.guard_gap_cm" */
	float AlignGuardGapCm = 14.f;
	/** ... pushing him back at most this much in all per climb (then the clip runs unguarded: no far snap at the landing), cm.
	 *  "align.guard_max_push_cm" */
	float AlignGuardMaxPushCm = 30.f;
	/** Acceptance (MantlingProblem_04): the pelvis settles at its final height over the feet within this of its last height, cm. "align.settle_pelvis_cm" */
	float AlignSettlePelvisCm = 5.f;
	/** ... at most this long after the climb ended (the montage blend-out + the stance change; no hold in the clip's pose), s. "align.handoff_limit_s" */
	float AlignHandoffLimitSeconds = 0.6f;
	/** Acceptance (MantleDescentSmoke): a move order from a top reaches its ground goal within this long, s. "descent.arrive_limit_s" */
	float DescentArriveLimitSeconds = 15.f;
	/** ... within this distance of the goal (2D), cm. "descent.arrive_radius_cm" */
	float DescentArriveRadiusCm = 120.f;
	/** ... and the capsule never moves faster than this between two frames (a teleport would), cm/s. "descent.max_speed_cm_s" */
	float DescentMaxSpeedCmPerSecond = 1500.f;
	/** The way down off a top (batch 2): he walks off its edge and falls (the fall states play); the landing he aims at lies this far past
	 *  the face beyond his capsule radius (clear of the face all the way down), cm. "descent.land_out_cm" */
	float DescentLandOutCm = 40.f;
	/** A face counts as a way down when the ground past it lies at least this far below the top ... cm. "descent.min_drop_cm" */
	float DescentMinDropCm = 30.f;
	/** ... and at most this far (a harmless fall: falls.json damage.safe_height_cm is 320), cm. "descent.max_drop_cm" */
	float DescentMaxDropCm = 250.f;
	/** Prone / crouched on a top: he stands up first, waiting for the stance clip at most this long, s. "descent.stand_up_max_s" */
	float DescentStandUpMaxSeconds = 1.5f;
	/** He has to be off the top this long after the walk to the edge started, else the old hop takes him down (logged), s. "descent.walk_timeout_s" */
	float DescentWalkTimeoutSeconds = 3.f;
	/** On a top with a navmesh path down, the walk-off over another face is taken when its way to the goal is at least this much shorter
	 *  (MantlingProblem_03: the path's only down link was on the climbed face), cm. "descent.prefer_margin_cm" */
	float DescentPreferMarginCm = 100.f;
	/**
	 * R17-1 (designer 2026-11-17, MANTLING.md 6.4): the squad climbs in turns - one climbs, the others wait at the base and cover. Each man climbs
	 * his own lane of the face, this far from the next lane (no man up stands on the next one's landing), cm. "squad.lane_spacing_cm"
	 */
	float SquadLaneSpacingCm = 100.f;
	/** Two lanes' landings at least this far apart (2D; two capsules of 34 cm + 2), cm. "squad.min_landing_gap_cm" */
	float SquadMinLandingGapCm = 70.f;
	/**
	 * The waiters' spots: the ladder queue's rule (LADDERS.md 7.1, ULadderClimbComponent QueueFirstSpotCm / QueueSpacingCm, TTraversalQueue::
	 * SpotDistanceCm) - the next one this far out from his lane's base, cm ... "squad.queue_first_cm"
	 */
	float SquadQueueFirstCm = 90.f;
	/** ... each one after him this much further, cm. "squad.queue_spacing_cm" */
	float SquadQueueSpacingCm = 75.f;
	/** Acceptance (FormationMantleSmoke): a waiter stands within this of the climbed face while he waits, cm. "squad.base_radius_cm" */
	float SquadBaseRadiusCm = 500.f;
	/** Only members within this of the first climber join the squad's climb (the rest keep their orders), cm. "squad.join_radius_cm" */
	float SquadJoinRadiusCm = 2500.f;
	/** One man's turn (his walk to the base + the climb) gives up after this long: he stays at the base, the next one goes, s. "squad.turn_timeout_s" */
	float SquadTurnTimeoutSeconds = 30.f;
	/** The next man starts this long after the previous one is up (he has crouched and turned to cover), s. "squad.next_delay_s" */
	float SquadNextDelaySeconds = 0.4f;
	/** The waiters fan out from the outward look by this much per place (0 = straight out / at the threat, 1 = +fan, 2 = -fan ...), deg. "squad.wait_fan_deg" */
	float SquadWaitFanDeg = 45.f;
	/** ... never more than this off the outward look (nobody covers into the obstacle), deg. "squad.wait_max_off_deg" */
	float SquadWaitMaxOffDeg = 80.f;
};

/**
 * One clip's authored motion as a body plays it (AOperativeCharacter::GetMantleClipTrack): bones sampled every SampleSeconds in the clip's
 * own space - the component space with the root's track left in (the clip's origin is fixed in the world while it plays), turned so that
 * X = the climb's forward and Z = up; cm.
 */
struct FMantleClipTrack
{
	float SampleSeconds = 0.f;
	TArray<FVector> Root;
	TArray<FVector> Pelvis;
	TArray<FVector> HandL;
	TArray<FVector> HandR;
	TArray<FVector> FootL;
	TArray<FVector> FootR;

	int32 Num() const { return Root.Num(); }
	float TimeAt(int32 Index) const { return SampleSeconds * static_cast<float>(Index); }
};

/** Where and when a clip's hands take the ledge and when its motion is over (MantleRules::AnalyseClipTrack; clip space, cm, s). */
struct FMantleClipContact
{
	/** A hand plant was found (else the climb keeps one warp window over the whole clip). */
	bool bValid = false;
	/** B17-3: no hand rests on the ledge - the contact is the slowest hand frame at the ledge's height (align.slide_speed_cm_s). */
	bool bSlide = false;
	/** The first hand plant: its time and the wrist's position then. */
	float ContactSeconds = 0.f;
	FVector HandPoint = FVector::ZeroVector;
	/** The root at the plant (the warp's first window ends there). */
	FVector RootAtContact = FVector::ZeroVector;
	/** The root at frame 0 (the mesh draws the root there for the whole clip: root lock = the first frame). */
	FVector RootStart = FVector::ZeroVector;
	/** The motion is over (every bone within align.exit_pose_cm of its final pose from here on): the climb hands over to the locomotion here. */
	float ExitSeconds = 0.f;
	/** The exit before batch 2 (the root at its last spot and every bone slower than align.still_speed_cm_s; Codex.Mantle.Batch2 0). */
	float StillExitSeconds = 0.f;
};

/** What the traces measured at one obstacle (the runtime fills it; the rules never trace). */
struct FMantleProbe
{
	/** The ledge top over the feet, cm. */
	float HeightCm = 0.f;
	/** The top's depth along the climb (how far the top surface runs on past the edge), cm. */
	float DepthCm = 0.f;
	/** The obstacle may be climbed at all (MantleRules::IsEligibleTags). */
	bool bEligible = true;
	/** It carries the marked-edge tag (MantleRules::MarkedEdgeTag). */
	bool bMarkedEdge = false;
	/** Free height over the landing (top / far side), cm; < 0 = not measured (taken as free). */
	float FreeHeadroomCm = -1.f;
	/** The landing takes the capsule (a free top footprint / far-side ground). */
	bool bLandingFree = true;
	/** Over: the far side's ground relative to the feet, cm. */
	float LandingStepCm = 0.f;
	/** The horizontal distance of the capsule centre from the face, cm. */
	float FaceDistanceCm = 0.f;
	/** He is running now (sprint or above RunSpeedFraction x walk speed). */
	bool bRunning = false;
	EMantleBody Body = EMantleBody::Male;
	/** The standing capsule height (2 x half height), cm. */
	float CapsuleHeightCm = 180.f;
	/** The operative's own round-robin counter (variants of one class / gait in turn). */
	int32 Rotation = 0;
};

/** The verdict (MantleRules::Decide). */
struct FMantleDecision
{
	EMantleRefusal Refusal = EMantleRefusal::NoObstacle;
	int32 ClassIndex = INDEX_NONE;
	int32 ClipIndex = INDEX_NONE;
	EMantleGait Gait = EMantleGait::Standing;
	/** He was running but the run-up did not fit a run clip (or none exists): he stops and the standing clip plays (designer Q2 = A). */
	bool bStopFirst = false;
	/** The face is still too far for this clip: walk closer first (to ApproachCm from the face). */
	bool bApproachFirst = false;
	/** The capsule centre's distance from the face where the clip starts, cm. */
	float ApproachCm = 0.f;
	/** height / clip rise and the forward travel needed / clip forward travel. */
	float VerticalWarp = 1.f;
	float ForwardWarp = 1.f;

	bool IsGo() const { return Refusal == EMantleRefusal::None; }
};

/** A click / hover planned as a climb (AOperativeCharacter::PlanMantleClick, batch 2). */
struct FMantleClickPlan
{
	/** The base point he walks to (feet) and the climb's direction (into the obstacle). */
	FVector Base = FVector::ZeroVector;
	FVector Dir = FVector::ForwardVector;
	/** The climb's landing (feet): on the top, or past a fence. */
	FVector Landing = FVector::ZeroVector;
	bool bTop = true;
	float HeightCm = 0.f;
	/** Batch 2: the climbed face (a point on it at knee height) and the clip's start distance from it (FMantleDecision::ApproachCm), cm. */
	FVector FacePoint = FVector::ZeroVector;
	float ApproachCm = 0.f;
	/** Batch 2: the clip the click would play (its config index), the lip on the face (the top's height) and the top's depth (B17-2: the ghost's pose). */
	int32 ClipIndex = INDEX_NONE;
	FVector EdgeTop = FVector::ZeroVector;
	float DepthCm = 0.f;
	/** The obstacle's top as clicked (a face click: the top just inside the face). */
	FHitResult TopHit;
	const class AActor* Obstacle = nullptr;
};

/** The walk-off descent off a mantle top (AOperativeCharacter::StartMantleDescent): standing up, walking off the edge, in the air. */
enum class EMantleDescentPhase : uint8
{
	None,
	StandUp,
	Walk,
	Air,
};

/** One way down off a mantle top (AOperativeCharacter::TryMantleStepDown, batch 2): over one face of the top onto the ground past it. */
struct FMantleDescentOption
{
	/** Where he walks off (on the face, his feet's height) and where he comes down (feet on the ground past the face). */
	FVector Exit = FVector::ZeroVector;
	FVector Landing = FVector::ZeroVector;
	/** His walk on the top to the exit, cm. */
	float WalkCm = 0.f;
	/** The navmesh path from the landing to the goal (a partial one: to its end), cm. */
	float PathCm = 0.f;
	/** The path from the landing reaches the goal (not partial). */
	bool bPathComplete = false;
	/** Ground within descent.min/max_drop_cm, navmesh and a free capsule at the landing, a path from it. */
	bool bValid = false;
};

namespace MantleRules
{
	/** Actor tag of a marked mantle edge (the level builder's mark: a pipe / annex / crate next to a one-storey roof). */
	CODEXTACTICS_API extern const FName MarkedEdgeTag;
	/** Actor tag that makes any obstacle a mantle target (test obstacles, hand-placed props). */
	CODEXTACTICS_API extern const FName MantleTag;
	/** Actor tag that keeps an obstacle out of mantling whatever its kind. */
	CODEXTACTICS_API extern const FName NoMantleTag;

	/** The built-in defaults (they mirror obstacle_heights.json + mantle.json). */
	CODEXTACTICS_API FMantleConfig MakeDefaultConfig();

	/** Applies obstacle_heights.json's "classes" (nominal_cm / tolerance_cm; wall_no_traverse.min_cm = the ceiling). False when unreadable. */
	CODEXTACTICS_API bool ApplyHeightsJson(const FString& JsonText, FMantleConfig& InOut, FString* OutError = nullptr);
	/** Applies mantle.json ("params", "classes", "clips"); unknown params are listed in OutUnknown. False when unreadable / without "params". */
	CODEXTACTICS_API bool ApplyMantleJson(const FString& JsonText, FMantleConfig& InOut, TArray<FString>* OutUnknown = nullptr, FString* OutError = nullptr);
	/** The "params" keys of mantle.json. */
	CODEXTACTICS_API TArray<FString> KnownKeys();
	CODEXTACTICS_API bool GetValue(const FMantleConfig& Config, const FString& Key, float& OutValue);

	/** The values in force (loaded on the first call from the two files; the defaults when missing). */
	CODEXTACTICS_API const FMantleConfig& GetConfig();
	/** Replaces the values in force (tests / smokes). */
	CODEXTACTICS_API void SetConfig(const FMantleConfig& Config);
	/** Reads both files again; false when one is missing / unreadable (its part keeps the defaults). */
	CODEXTACTICS_API bool ReloadConfig();
	CODEXTACTICS_API FString GetDefaultPath();
	CODEXTACTICS_API FString GetHeightsPath();

	CODEXTACTICS_API int32 FindClass(const FMantleConfig& Config, FName Id);
	CODEXTACTICS_API const TCHAR* RefusalName(EMantleRefusal Refusal);

	/**
	 * Eligibility by the obstacle's tags: NoMantleTag never; MantleTag / MarkedEdgeTag always; else one of the eligible level-generator kinds
	 * (crate, wreck, boiler, cut_wall, fence, low_wall, cover). Barrels (explosive, designer 2026-11-15), silos, chimneys, building walls and
	 * parapets are not. OutMarked: the marked-edge tag is there.
	 */
	CODEXTACTICS_API bool IsEligibleTags(const TArray<FName>& Tags, bool& OutMarked);

	/** Destination by depth: Top (deep enough to stand on), Over (thin), else none (DepthMismatch). */
	CODEXTACTICS_API TOptional<EMantleDestination> DestinationForDepth(const FMantleConfig& Config, float DepthCm);

	/**
	 * The class of an obstacle HeightCm tall with this destination: the band (nominal +- tolerance) that holds it - the nearest nominal
	 * when two overlap. INDEX_NONE with OutRefusal = TooLow / BetweenClasses / Wall.
	 */
	CODEXTACTICS_API int32 ClassifyHeight(const FMantleConfig& Config, float HeightCm, EMantleDestination Destination, EMantleRefusal& OutRefusal);

	/** The capsule centre's distance from the face where Clip starts (Top: forward - inset, never under MinFaceGapCm; Over: its StartCm). */
	CODEXTACTICS_API float ClipStartCm(const FMantleConfig& Config, const FMantleClip& Clip);

	/** The forward warp of Clip started FaceDistanceCm from the face: the travel needed / the clip's own travel (Top only; Over: 1). */
	CODEXTACTICS_API float ForwardWarpFor(const FMantleConfig& Config, const FMantleClip& Clip, float FaceDistanceCm);

	/**
	 * Picks the clip (designer Q2 = A): running and the face within the run clip's warp window -> the run clip; running but too close for it
	 * (or no run clip) -> the standing clip after a stop (bStopFirst); walking / standing -> the standing clip. Variants of the same class /
	 * gait / body rotate by Rotation; IsAvailable(index) false = the asset is missing (skipped). INDEX_NONE: no clip for this body.
	 */
	CODEXTACTICS_API int32 ChooseClip(const FMantleConfig& Config, FName ClassId, EMantleBody Body, bool bRunning, float FaceDistanceCm, int32 Rotation,
		TFunctionRef<bool(int32)> IsAvailable, EMantleGait& OutGait, bool& OutStopFirst);

	/** Free room over the landing for the standing capsule: FreeHeadroomCm < 0 (not measured) or >= capsule height + margin. */
	CODEXTACTICS_API bool HasHeadroom(const FMantleConfig& Config, float FreeHeadroomCm, float CapsuleHeightCm);

	/** The whole decision for one probe. */
	CODEXTACTICS_API FMantleDecision Decide(const FMantleConfig& Config, const FMantleProbe& Probe, TFunctionRef<bool(int32)> IsAvailable);
	/** Decide with every clip available. */
	CODEXTACTICS_API FMantleDecision Decide(const FMantleConfig& Config, const FMantleProbe& Probe);

	/**
	 * A clip's contact and exit from its sampled motion: the first hand that reached out (align.plant_reach_cm from its frame-0 spot), rose to
	 * align.plant_min_rise_fraction x RiseCm over the frame-0 feet and then rests (slower than align.plant_speed_cm_s for align.plant_hold_s)
	 * while the root is still on its way (farther than align.exit_root_cm from its last spot) is the plant; the exit is the first moment after
	 * the plant from which every bone stays within align.exit_pose_cm of its final pose (the clip's slow settle and idle tail are skipped,
	 * batch 2). bValid false without a plant; ExitSeconds = the clip's last sample when even that moves more.
	 */
	CODEXTACTICS_API FMantleClipContact AnalyseClipTrack(const FMantleConfig& Config, const FMantleClipTrack& Track, float RiseCm);

	/**
	 * The face distance (capsule centre, cm) a standing climb starts at so that the clip's planted wrist lands on the lip + align.grip_forward_cm
	 * without any forward warp (the mesh drawn root-relative: the wrist is HandPoint - RootStart ahead of his start), never under
	 * min_face_gap_cm. Negative without a plant.
	 */
	CODEXTACTICS_API float ContactStartCm(const FMantleConfig& Config, const FMantleClipContact& Contact);

	/**
	 * B17-2 (designer option A, 2026-11-17: the hover ghost = a frame from the middle of the climb, hands on the lip): the track sample
	 * ghost.pose_fraction through the contact hand's hold - from the contact to the last sample (not past the exit) the hand stays within
	 * align.plant_reach_cm of its contact point. INDEX_NONE without a contact. bOutLeftHand: the contact hand.
	 */
	CODEXTACTICS_API int32 GhostPoseSample(const FMantleConfig& Config, const FMantleClipTrack& Track, const FMantleClipContact& Contact, bool& bOutLeftHand);

	/** A heavy hit (FinalDamage >= HeavyHitDamage) cancels a running climb. */
	CODEXTACTICS_API bool IsHeavyHit(const FMantleConfig& Config, float FinalDamage);

	/**
	 * A low top also gets a path link back down (the drop links only start at LinkDownBelowCm): a man climbed onto a crate / wreck is never
	 * stranded on it.
	 */
	CODEXTACTICS_API bool NeedsDownLink(const FMantleConfig& Config, float HeightCm);

	/** Sample offsets (cm from the face's middle, along it) of the path links of a face FaceLengthCm long, keeping MarginCm off its ends. */
	CODEXTACTICS_API TArray<float> LinkOffsets(const FMantleConfig& Config, float FaceLengthCm, float MarginCm);

	/**
	 * The way down to take (batch 2, item 5 "always jumps down on the side he climbed from"): the valid option with the shortest total (the
	 * walk on the top to its exit + the path from its landing to the goal); one whose path reaches the goal beats every partial one.
	 * INDEX_NONE when none is valid.
	 */
	CODEXTACTICS_API int32 ChooseDescent(const TArray<FMantleDescentOption>& Options);

	/**
	 * The exit point on one face of a top (2D; the face runs from A to B, Z kept from A): the goal's projection onto the face, MarginCm off its
	 * ends (a face shorter than 2 x MarginCm: its middle).
	 */
	CODEXTACTICS_API FVector DescentExitOnFace(const FVector& A, const FVector& B, const FVector& Goal, float MarginCm);
}
