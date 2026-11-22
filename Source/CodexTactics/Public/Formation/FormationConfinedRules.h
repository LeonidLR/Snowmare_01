#pragma once

#include "CoreMinimal.h"

class FJsonObject;

/**
 * The formation compresses in a confined space (the game designer's decision 2026-11-21: "the whole squad must enter" - every squad member
 * gets into a ~5 m2 khrushchevka flat hall and the 2.5 m gable-vestibule corridors; UE-only, no Godot reference). Before it the trail column
 * parked its men 330 / 480 cm back along the leader's trail - through the hall door, on the landing - and the column / wall layout slots of a
 * small room lay outside it (LevelGenBuildingsWalkSmoke flat-entry squad legs, docs/design/levelgen/STORY_SHOWCASE_2026-11-22.md).
 *
 * The rule (pure; the runtime probes the navmesh, USquadSubsystem::ApplyFormationConfined, SquadSubsystemFormationConfined.cpp):
 * - The POCKET: when the leader arrives (his move destination within ArriveCm, on his level) or stands, ProbeRays navmesh rays from the
 *   destination (else from where he stands), world-aligned (ray 0 = +X, counter-clockwise in UE's X / Y), each ProbeCm long: Free[i] = the
 *   free navmesh distance (the navmesh is already eroded by the agent radius).
 * - CONFINED when the median of Free <= ConfinedMedianCm (a door's or a corridor's few long rays do not lift the median); open ground is never
 *   confined - the formation there is unchanged.
 * - The ENTRY point E = where the squad comes in (the runtime: where the leader first walked within the pocket radius - at or just outside the door);
 *   unset = unknown (he stood there).
 * - A follower's formation slot STAYS when it lies inside the pocket (within the pocket radius = max(median, MinSpacingCm, MinRadiusCm) and
 *   WallMarginCm short of the free distance toward it), off the leader's remaining way (WayHalfWidthCm round the segment from where he walks
 *   to the centre while he is farther than WayMinCm from it), off the entry line (WayHalfWidthCm round the segment from the centre toward E,
 *   at least the pocket radius long: nobody parks in the way in of the man behind), not past the leader, and MinSpacingCm from the centre and
 *   every place given out before.
 * - Every other slot gets a CANDIDATE place: on the probe rays at MinSpacingCm + k x RingStepCm and 2 x MinSpacingCm, up to the pocket radius
 *   and WallMarginCm short of the ray's free distance, off the leader's way, never PAST THE LEADER (IsPastLeader: a hall narrower than
 *   2 x PassFreeCm through the centre is split by the standing leader - nobody is sent to its far half from E). The men nearest the centre
 *   choose first, each the cheapest place that keeps MinSpacingCm, by tiers: reachable (his straight line keeps PassClearCm from every place
 *   given out he is not already beside) and off the entry line; reachable; off the entry line; any. Cost = his walk - DepthWeight x the place's
 *   distance from E: the first man in goes deepest, so the next never has to pass him. No spaced place: the place farthest from everyone given
 *   out if that keeps half MinSpacingCm, else his slot stays (no room inside: he waits outside rather than on a team-mate); no candidate at
 *   all: the slot stays.
 * - Line tests (FFormationConfinedLineTests, the runtime's navmesh): every place (kept or new) must be seen from the entry point on a clear line
 *   that keeps JambClearCm from the navmesh edge (no place round a door jamb's corner - none such: the filter is dropped); "reachable" also
 *   needs his own straight navmesh line to it, unless he still stands farther out than the entry point (he walks in through the door); no
 *   place beyond a throat on the line from the centre (InRoom). A man keeps the place he was given last frame (Held) while it is still valid.
 *
 * - ENTRY ORDER (2026-11-22, bEntryQueue; FFormationConfinedQueue): with the squad's navmesh way in known (EntryOnPath: the inside entry point
 *   just past the door throat, the door side EntryBackCm back along the way) the places are dealt as a depth queue - QueueSlots: a man through
 *   the door keeps his held place; the others, nearest the door first (QueueOrder, hysteresis), take the deepest free place from the inside
 *   entry point that keeps the spacing, lies off every earlier man's way in and has his own way clear (pools: seen from the door + in the room,
 *   in the room, any; the last tier the shallowest); a detected block (BlockedBy) swaps two held places. The runtime walks a man round a
 *   standing body head on his way (StepRoundBody). No queue input / entry_queue 0: the cost rule above.
 * Content/Data/AI/squad_confined.json (snake_case keys, metres where the key says _m; a missing key keeps the default), read on the first
 * FormationConfinedTuning::Get(); console Codex.Formation.Confined (-1 = the file's "enabled", 0 = off - the old slots, 1 = on).
 * Tests: CodexTactics.Formation.Confined.*; in-game: LevelGenBuildingsWalkSmoke flat-entry squad legs (hall legs).
 */
struct CODEXTACTICS_API FFormationConfinedTuning
{
	/** The rule is on (enabled, 0 / 1). */
	bool bEnabled = true;
	/** Navmesh rays round the pocket centre (probe_rays, 8..64). */
	int32 ProbeRays = 16;
	/** Each ray's length, cm (probe_m, metres in the file). */
	float ProbeCm = 450.f;
	/**
	 * The pocket is confined when the median free distance is at most this, cm (confined_median_m, metres in the file). 1.7 m (Lead review round
	 * 2): flat halls (median 0.5-1.5 m) and 2.5 m corridors compress, a 3.5 x 5 m living room (1.86 m, grey S09 r_f0_c17) does not.
	 */
	float ConfinedMedianCm = 170.f;
	/** Two bodies' centres (the operative capsule is 40 cm): spacing between the places and from the pocket centre, cm (min_spacing_cm). */
	float MinSpacingCm = 80.f;
	/** A place stays this short of the free (already agent-eroded) navmesh distance toward it, cm (wall_margin_cm). */
	float WallMarginCm = 10.f;
	/** The candidate rings' step outward from MinSpacingCm, cm (ring_step_cm). */
	float RingStepCm = 15.f;
	/** The pocket radius is at least this (two bodies out along a narrow hall's long axis - a column of three fits), cm (min_radius_cm). */
	float MinRadiusCm = 160.f;
	/** The leader's destination within this of him (and on his level) is the pocket centre - the squad compresses as he arrives, cm (arrive_m). */
	float ArriveCm = 200.f;
	/** The destination counts on his level within this height, cm (level_dz_cm). */
	float LevelDzCm = 120.f;
	/** A held pocket is probed again once its wanted centre moved this far, cm (recenter_cm). */
	float RecenterCm = 60.f;
	/** ... or after this long (a door opened / closed), s (refresh_s). */
	float RefreshSeconds = 1.f;
	/** No place within this of the leader's remaining way or of the entry line, cm (way_half_width_cm). */
	float WayHalfWidthCm = 60.f;
	/** The leader's way counts while he is farther than this from the pocket centre, cm (way_min_cm). */
	float WayMinCm = 40.f;
	/**
	 * A man's straight line to a place must pass every place already given out (the leader's too) at least this far, unless he already stands
	 * beside it, cm (pass_clear_cm).
	 */
	float PassClearCm = 60.f;
	/** A place's cost = his walk - this x its distance from the entry point: the first man in goes deepest (depth_weight). */
	float DepthWeight = 1.5f;
	/** A man passes beside the standing leader only where the pocket through the centre is at least 2 x this wide, cm (pass_free_cm). */
	float PassFreeCm = 80.f;
	/** Walking to his compressed place a follower faces his walk (no formation look, no backward walk) until this near it, cm (face_walk_cm). */
	float FaceWalkCm = 100.f;
	/**
	 * Lead review 2026-11-22 (the follower's 40 cm capsule snagged the door jamb on a place round the corner behind the door; the navmesh agent
	 * radius is 35 cm and stays - a project decision): a place must be in a clear navmesh line from the entry point whose every sample (but the
	 * last EndSkip) keeps this from the navmesh edge - with the 35 cm erosion a 45 cm clearance from the geometry, more than the capsule, cm
	 * (jamb_clear_cm; 0 = the line only).
	 */
	float JambClearCm = 10.f;
	/** The entry line's clearance samples are this far apart, cm (jamb_sample_cm). */
	float JambSampleCm = 20.f;
	/**
	 * Lead review 2026-11-22 (grey showcase S03: a man stood in the hall's door, a place 160 cm out along the one ray through it): a ray longer
	 * than this x both its neighbours is a GAP (a door seen through by one ray); unless its neighbours are themselves long (> this x the median -
	 * a corridor's axis) its places reach only as far as the shorter neighbour - inside the room, not in the door (gap_factor; 0 = off).
	 */
	float GapFactor = 1.5f;
	/**
	 * Lead review round 2: a THROAT on the line from the centre to a place - the navmesh edge nearer than this (a door) while the centre keeps
	 * more than ThroatClearCm / 0.7 - puts the place out of the room (FFormationConfinedLineTests::InRoom), cm (throat_clear_cm; 0 = off).
	 */
	float ThroatClearCm = 30.f;
	/** Lead review round 3: a follower keeps the place he was given last frame while it stays valid (FFormationConfinedSlot::Held) (hold_places, 0 / 1). */
	bool bHoldPlaces = true;
	/**
	 * Entry order 2026-11-22 (Lead: the STUCK legs were a follower held at the door by his team-mate's body on his place by the door): the
	 * DEPTH QUEUE (entry_queue, 0 / 1). With an entry point inside the doorway known (FFormationConfinedQueue, from the navmesh way in) the
	 * places are ranked by their distance from it, the man first through the door takes the deepest, the next the next, a later (shallower)
	 * place never lies on an earlier man's way in; a man through the door keeps his place, places are re-dealt only on a detected block.
	 * 0 = the round-3 cost rule (A/B).
	 */
	bool bEntryQueue = true;
	/** The line tests' entry point (the door side) lies this far back along the way in from the inside entry point, cm (entry_back_cm). */
	float EntryBackCm = 60.f;
	/** The way in is sampled this often to find where it enters the pocket, cm (entry_step_cm). */
	float EntryStepCm = 10.f;
	/** Two men's queue order flips only when the later one is this much nearer the door than the earlier, cm (queue_hysteresis_cm). */
	float QueueHysteresisCm = 40.f;
	/** A follower slower than this while away from his place counts as standing, cm/s (block_speed_cm_s). */
	float BlockSpeedCmS = 20.f;
	/** ... for this long with a team-mate on his way = blocked, s (block_s). */
	float BlockSeconds = 0.6f;
	/** The team-mate on his way stands within this of him (two bodies touching: 80 cm), cm (block_radius_cm). */
	float BlockRadiusCm = 95.f;
	/** After a block swap the same man is not swapped again for this long, s (block_cooldown_s). */
	float BlockCooldownSeconds = 2.f;
	/**
	 * Entry order: a follower's straight way to his confined place passing a standing body (the leader, a team-mate) closer than this - head
	 * on, not a graze the capsules slide past - goes by a step-round point beside it first, MinSpacingCm + StepRoundMarginCm out from it (the
	 * navmesh ignores bodies, the followers' crowd avoidance is off while compressed: the first man in pushed into the leader standing 60 cm
	 * inside the door; at 80 a 76 cm graze in the doorway sent him round the leader's far side), cm (body_clear_cm; 0 = off).
	 */
	float BodyClearCm = 55.f;
};

/**
 * Entry order (FFormationConfinedTuning::bEntryQueue): where the squad's navmesh way in enters the pocket. Inside = the first point of the way's
 * last run of points inside the pocket and in the room (just inside the doorway); Outside = EntryBackCm back along the way (the door side: the
 * line tests' and the entry line's point).
 */
struct FFormationConfinedEntry
{
	FVector2D Inside = FVector2D::ZeroVector;
	FVector2D Outside = FVector2D::ZeroVector;
};

/** The depth queue's input to CompressSlots (unset / nullptr = the round-3 cost rule). */
struct FFormationConfinedQueue
{
	/** The inside entry point (FFormationConfinedEntry::Inside): places rank by their distance from it, men walk in from it. */
	FVector2D EntryInside = FVector2D::ZeroVector;
};

/** A probed pocket: its centre and the free navmesh distance along each world-aligned ray (FormationConfinedRules::RayDir). */
struct FFormationConfinedPocket
{
	FVector2D Center = FVector2D::ZeroVector;
	/** Free[i] = the free distance along RayDir(i, Free.Num()), cm (at most the probe length). */
	TArray<float> Free;
};

/**
 * The runtime's navmesh line tests for CompressSlots (Lead review 2026-11-22; unset functions = no test). FromEntry(Place): a clear navmesh line
 * from the entry point to Place keeping JambClearCm from the navmesh edge (no place round a jamb corner behind the door). Straight(From, To):
 * a clear navmesh line (a follower's own straight way to a place).
 */
struct FFormationConfinedLineTests
{
	TFunction<bool(const FVector2D& Place)> FromEntry;
	TFunction<bool(const FVector2D& From, const FVector2D& To)> Straight;
	/**
	 * Lead review round 2 (2026-11-22): InRoom(Place) - the line from the pocket centre to Place passes no THROAT (a door: the navmesh narrows
	 * below ThroatClearCm on the way while the centre itself is roomier) - a place out in a doorway seen through by two rays is no place.
	 */
	TFunction<bool(const FVector2D& Place)> InRoom;
};

/** One follower slot to compress: the formation's slot and (when known) where its man stands now. */
struct FFormationConfinedSlot
{
	FVector2D Desired = FVector2D::ZeroVector;
	TOptional<FVector2D> Occupant;
	/**
	 * Lead review round 2 (2026-11-22): the place this rule gave him last frame - kept while it is still a valid place (the walk smoke's men
	 * swapped places frame by frame as they moved, across the hall and back).
	 */
	TOptional<FVector2D> Held;
	/** Depth queue: his navmesh way to the inside entry point, cm (unset: the straight distance). */
	TOptional<float> ToEntryCm;
	/** Depth queue: his rank last frame (INDEX_NONE: none) - the order flips only past QueueHysteresisCm. */
	int32 PrevRank = INDEX_NONE;
	/** Depth queue: the slot index of the team-mate standing on his way while he stands (the runtime's block detection): their places swap. */
	int32 BlockedBy = INDEX_NONE;
};

/** CompressSlots' result: each slot's place and whether the rule moved it. */
struct FFormationConfinedResult
{
	bool bConfined = false;
	TArray<FVector2D> Places;
	TArray<bool> Replaced;
	int32 ReplacedCount = 0;
	/** The depth queue dealt the places. */
	bool bQueued = false;
	/** Depth queue: each slot's rank (0 = the first through the door, the deepest place; INDEX_NONE = not queued: his slot stayed). */
	TArray<int32> Rank;
	/** Depth queue: the man is through the door and keeps his place. */
	TArray<bool> Locked;
	/** Depth queue: a block swap was applied (BlockedBy) for this slot. */
	TArray<bool> Swapped;
};

namespace FormationConfinedRules
{
	/** The rule in force: console Codex.Formation.Confined (0 / 1) over the tuning's bEnabled (-1, the default). */
	CODEXTACTICS_API bool IsEnabled(const FFormationConfinedTuning& Tuning);

	/** The depth queue in force: console Codex.Formation.ConfinedQueue (0 / 1) over the tuning's bEntryQueue (-1, the default). */
	CODEXTACTICS_API bool IsQueueEnabled(const FFormationConfinedTuning& Tuning);

	/** Unit direction of ray Index of Count: world-aligned, ray 0 = +X, then counter-clockwise (+Y) in 360 / Count steps. */
	CODEXTACTICS_API FVector2D RayDir(int32 Index, int32 Count);

	/** The median of Values (the mean of the two middle ones for an even count); 0 when empty. */
	CODEXTACTICS_API float Median(TArrayView<const float> Values);

	/** Confined: at least 4 rays and their median free distance <= ConfinedMedianCm. */
	CODEXTACTICS_API bool IsConfined(const FFormationConfinedPocket& Pocket, const FFormationConfinedTuning& Tuning);

	/** The pocket radius: max(median free distance, MinSpacingCm, MinRadiusCm). */
	CODEXTACTICS_API float PocketRadius(const FFormationConfinedPocket& Pocket, const FFormationConfinedTuning& Tuning);

	/** The free distance toward Dir (any length): the smaller of the two rays bracketing it (conservative); 0 for an empty pocket. */
	CODEXTACTICS_API float FreeToward(const FFormationConfinedPocket& Pocket, const FVector2D& Dir);

	/** How far places reach along ray Ray: min(PocketRadius, its free distance - WallMarginCm, a gap ray's shorter neighbour - WallMarginCm) (GapFactor). */
	CODEXTACTICS_API float RayReach(const FFormationConfinedPocket& Pocket, int32 Ray, const FFormationConfinedTuning& Tuning);

	/** Point lies inside the pocket: within PocketRadius of the centre and WallMarginCm short of FreeToward its direction, not beyond a gap ray's reach (the centre itself: true). */
	CODEXTACTICS_API bool IsInside(const FFormationConfinedPocket& Pocket, const FVector2D& Point, const FFormationConfinedTuning& Tuning);

	/**
	 * Point lies within WayHalfWidthCm of the entry line: the segment from the centre toward EntryFrom, max(its distance, PocketRadius) long (the
	 * door the squad came through). EntryFrom unset or within 1 cm of the centre: false.
	 */
	CODEXTACTICS_API bool IsOnEntry(const FFormationConfinedPocket& Pocket, const TOptional<FVector2D>& EntryFrom, const FVector2D& Point,
		const FFormationConfinedTuning& Tuning);

	/**
	 * Point lies past the standing leader: the pocket's narrowest width through the centre (Free[i] + Free[opposite], an even ray count) is under
	 * 2 x PassFreeCm, so he splits it along the perpendicular long axis; Point lies on the far half from EntryFrom by more than a quarter of
	 * MinSpacingCm. EntryFrom unset or square to the long axis: false.
	 */
	CODEXTACTICS_API bool IsPastLeader(const FFormationConfinedPocket& Pocket, const TOptional<FVector2D>& EntryFrom, const FVector2D& Point,
		const FFormationConfinedTuning& Tuning);

	/** 2D distance from Point to the segment A-B. */
	CODEXTACTICS_API float DistanceToSegment(const FVector2D& Point, const FVector2D& A, const FVector2D& B);

	/** The candidate places: on each ray at MinSpacingCm + k x RingStepCm and 2 x MinSpacingCm up to PocketRadius, each inside the pocket; ray-major, inner first. */
	CODEXTACTICS_API TArray<FVector2D> Candidates(const FFormationConfinedPocket& Pocket, const FFormationConfinedTuning& Tuning);

	/**
	 * The slots in a confined pocket (see the header comment). LeaderAt = where the leader walks now (his remaining way runs from there to the
	 * pocket centre while he is farther than WayMinCm from it); unset = he stands. EntryFrom = the entry point E (unset = unknown). Not confined:
	 * every slot as Desired, nothing replaced.
	 */
	CODEXTACTICS_API FFormationConfinedResult CompressSlots(const FFormationConfinedPocket& Pocket, TArrayView<const FFormationConfinedSlot> Slots,
		const TOptional<FVector2D>& LeaderAt, const TOptional<FVector2D>& EntryFrom, const FFormationConfinedTuning& Tuning,
		const FFormationConfinedLineTests* Lines = nullptr, const FFormationConfinedQueue* Queue = nullptr);

	/**
	 * Entry order: where the way in (Path: polyline from outside toward the pocket centre, e.g. the leader's navmesh path) enters the pocket -
	 * sampled every EntryStepCm, the first point of its LAST run of points inside the pocket (IsInside) and in the room (InRoom, unset = no
	 * test); Outside = EntryBackCm back along the way (at most its start). Unset: the way starts inside (no door crossed) or never gets in.
	 */
	CODEXTACTICS_API TOptional<FFormationConfinedEntry> EntryOnPath(const FFormationConfinedPocket& Pocket, TArrayView<const FVector2D> Path,
		const FFormationConfinedTuning& Tuning, const TFunction<bool(const FVector2D&)>& InRoom = nullptr);

	/**
	 * Block detection (the depth queue's re-deal): a man at Here walking to Place stands blocked by a team-mate at Other when Other lies within
	 * BlockRadiusCm of him, ahead toward Place (forward of him) and within PassClearCm of his straight line to Place.
	 */
	CODEXTACTICS_API bool IsBlockedBy(const FVector2D& Here, const FVector2D& Place, const FVector2D& Other, const FFormationConfinedTuning& Tuning);

	/**
	 * The depth queue's order of the men not yet locked: Keys[i] = how far man i still is from the door (lower = earlier); PrevRank[i] his last
	 * rank (INDEX_NONE: new). Start from the last order (new men by key behind), then two neighbours flip only when the later one's key is lower
	 * by more than HysteresisCm. Returns the indices into Keys in queue order.
	 */
	CODEXTACTICS_API TArray<int32> QueueOrder(TArrayView<const float> Keys, TArrayView<const int32> PrevRank, float HysteresisCm);

	/**
	 * Entry order: the step-round point for a man at Here walking straight to Place past standing Bodies - the first body ahead on the way
	 * (projected strictly between Here and Place) closer than BodyClearCm to it: the point MinSpacingCm + StepRoundMarginCm out from that body
	 * on the way's side (the other side when IsWalkable refuses it; IsWalkable unset = any). Unset: the way is clear, no walkable side, or off.
	 */
	CODEXTACTICS_API TOptional<FVector2D> StepRoundBody(const FVector2D& Here, const FVector2D& Place, TArrayView<const FVector2D> Bodies,
		const FFormationConfinedTuning& Tuning, const TFunction<bool(const FVector2D&)>& IsWalkable = nullptr);

	/** StepRoundBody's margin beyond the body clearance, cm. */
	inline constexpr float StepRoundMarginCm = 5.f;
}

namespace FormationConfinedTuning
{
	/** Content/Data/AI/squad_confined.json. */
	CODEXTACTICS_API FString GetDefaultPath();
	/** The tuning in force (the file on the first call, else the defaults). */
	CODEXTACTICS_API const FFormationConfinedTuning& Get();
	/** Parameters of Json over Base (clamped to sane ranges). */
	CODEXTACTICS_API FFormationConfinedTuning FromJson(const FJsonObject& Json, const FFormationConfinedTuning& Base = FFormationConfinedTuning());
	/** Re-reads the file into Get(); false (the defaults kept) when it is missing or unreadable. */
	CODEXTACTICS_API bool Reload();
}
