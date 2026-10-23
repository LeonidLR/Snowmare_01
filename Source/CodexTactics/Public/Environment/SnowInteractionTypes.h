#pragma once

#include "CoreMinimal.h"
#include "SnowInteractionTypes.generated.h"

/**
 * Interactive snow, phase 1 (UE-only, no Godot reference; docs/systems/environment-weather.md §14, docs/design/INTERACTIVE_SNOW.md).
 * The procedural stamp shapes of M_SnowStamp (the id travels in the vertex colour, SnowInteractionRules::EncodeStampColor).
 */
UENUM(BlueprintType)
enum class ESnowStampShape : uint8
{
	/** Boot print: toe + heel ellipses, x = along the walk. */
	Boot = 0,
	/** Paw: a main pad and four toes. */
	Paw = 1,
	/** Body imprint: a rounded box (knockdown / death). */
	Body = 2,
	/** Explosion crater with a raised rim (phase 3; Depth 0 = only the rim: the thrown-snow ring). */
	Crater = 3,
	/** Bullet dent (phase 3; Depth 0 = a small lump of thrown snow). */
	Dent = 4,
	/** Trail cross-section, constant along the segment: furrows, ruts, drag trails. */
	Segment = 5
};

/** One kind of stamp (DA_SnowStamps row; C++ defaults in FSnowStampTable). Visual only. */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FSnowStampSpec
{
	GENERATED_BODY()

	FSnowStampSpec() = default;
	FSnowStampSpec(ESnowStampShape InShape, float InLength, float InWidth, float InDepth, float InBerm, float InSpacing, float InSide,
		float InRandomYaw = 0.f, float InRandomScale = 0.f, bool bInAlign = true)
		: Shape(InShape), LengthCm(InLength), WidthCm(InWidth), Depth(InDepth), Berm(InBerm), SpacingCm(InSpacing), SideOffsetCm(InSide)
		, RandomYawDeg(InRandomYaw), RandomScale(InRandomScale), bAlignToMotion(bInAlign)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stamp")
	ESnowStampShape Shape = ESnowStampShape::Boot;

	/** Print length along the motion (cm). A Segment's length is the distance walked; this is unused for it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stamp", meta = (ClampMin = "1"))
	float LengthCm = 30.f;

	/** Print width across the motion / the furrow width (cm). The berm lies just outside it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stamp", meta = (ClampMin = "1"))
	float WidthCm = 12.f;

	/** Compaction added to RT channel R at the centre (1 = the reader's full MaxDepthCm; the RT holds up to 4). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stamp", meta = (ClampMin = "0", ClampMax = "4"))
	float Depth = 0.9f;

	/** Berm added to RT channel G on the rim (1 = the reader's MaxBermCm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stamp", meta = (ClampMin = "0", ClampMax = "4"))
	float Berm = 0.3f;

	/** Prints: the stride (cm between two prints). Trails: the shortest segment drawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stamp", meta = (ClampMin = "1"))
	float SpacingCm = 70.f;

	/** Prints alternate this far left / right of the path (feet); trails: unused (ruts take FSnowInteractionTuning::RutHalfSpacingCm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stamp", meta = (ClampMin = "0"))
	float SideOffsetCm = 12.f;

	/** +- random yaw (degrees), from a hash of the position (deterministic: the game's random stream is never touched). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stamp", meta = (ClampMin = "0", ClampMax = "180"))
	float RandomYawDeg = 0.f;

	/** +- random size fraction (same hash). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stamp", meta = (ClampMin = "0", ClampMax = "0.5"))
	float RandomScale = 0.f;

	/** Prints / imprints turn with the motion (false: world-aligned, e.g. craters). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stamp")
	bool bAlignToMotion = true;
};

/** Every stamp kind of phase 1 + the phase 3 event stamps (craters, dents). The C++ defaults ARE the tuning when DA_SnowStamps is missing. */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FSnowStampTable
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Operatives")
	FSnowStampSpec OperativeBoot = FSnowStampSpec(ESnowStampShape::Boot, 30.f, 12.f, 0.9f, 0.3f, 70.f, 12.f, 5.f, 0.06f);

	/** Prone / crawl / prone dive: a wide body furrow. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Operatives")
	FSnowStampSpec OperativeCrawl = FSnowStampSpec(ESnowStampShape::Segment, 0.f, 60.f, 0.8f, 0.5f, 25.f, 0.f);

	/** Knockdown (rising edge). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Operatives")
	FSnowStampSpec OperativeImprint = FSnowStampSpec(ESnowStampShape::Body, 170.f, 60.f, 0.9f, 0.6f, 0.f, 0.f, 0.f, 0.05f);

	/** UCodexEventBus::OnSoldierDowned. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Operatives")
	FSnowStampSpec OperativeDeath = FSnowStampSpec(ESnowStampShape::Body, 180.f, 65.f, 1.f, 0.6f, 0.f, 0.f, 0.f, 0.05f);

	/** Base / Spitter / Marksman. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemies")
	FSnowStampSpec EnemyBoot = FSnowStampSpec(ESnowStampShape::Boot, 30.f, 12.f, 0.9f, 0.3f, 72.f, 13.f, 8.f, 0.08f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemies")
	FSnowStampSpec BruteBoot = FSnowStampSpec(ESnowStampShape::Boot, 52.f, 24.f, 1.4f, 0.6f, 120.f, 22.f, 6.f, 0.06f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemies")
	FSnowStampSpec HoundPaw = FSnowStampSpec(ESnowStampShape::Paw, 13.f, 12.f, 0.7f, 0.2f, 45.f, 9.f, 10.f, 0.08f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemies")
	FSnowStampSpec CutterPaw = FSnowStampSpec(ESnowStampShape::Paw, 15.f, 14.f, 0.8f, 0.25f, 52.f, 10.f, 10.f, 0.08f);

	/** Frostbitten: one dragged foot = a narrow furrow. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemies")
	FSnowStampSpec FrostbittenDrag = FSnowStampSpec(ESnowStampShape::Segment, 0.f, 28.f, 0.6f, 0.35f, 25.f, 0.f);

	/** Humanoid enemy knockdown / death. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemies")
	FSnowStampSpec EnemyImprint = FSnowStampSpec(ESnowStampShape::Body, 170.f, 60.f, 0.9f, 0.6f, 0.f, 0.f, 0.f, 0.05f);

	/** Hound / Cutter knockdown / death. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemies")
	FSnowStampSpec BeastImprint = FSnowStampSpec(ESnowStampShape::Body, 110.f, 45.f, 0.8f, 0.5f, 0.f, 0.f, 0.f, 0.05f);

	/** Brute knockdown / death. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemies")
	FSnowStampSpec BruteImprint = FSnowStampSpec(ESnowStampShape::Body, 230.f, 95.f, 1.3f, 0.8f, 0.f, 0.f, 0.f, 0.05f);

	/** The APC: two of these, FSnowInteractionTuning::RutHalfSpacingCm either side of its centre. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicles")
	FSnowStampSpec VehicleRut = FSnowStampSpec(ESnowStampShape::Segment, 0.f, 45.f, 1.2f, 0.7f, 35.f, 0.f);

	/**
	 * A barrel / barricade dragged: a turn-based relocation (UTurnBasedCombatSubsystem::GetRelocatingObject) or a real-time push
	 * (URelocationSubsystem::GetMovingObjects, phase 3).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Objects")
	FSnowStampSpec DragTrail = FSnowStampSpec(ESnowStampShape::Segment, 0.f, 55.f, 0.7f, 0.45f, 25.f, 0.f);

	/**
	 * Explosion crater of a trap / unknown kind (GameplayEvents::Explosion "Trap"). Every crater spec: Length = the diameter when the
	 * event has no radius (else FSnowInteractionTuning::CraterDiameterPerRadius), Depth = the bowl, Berm = the rim.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Events")
	FSnowStampSpec Crater = FSnowStampSpec(ESnowStampShape::Crater, 320.f, 320.f, 1.6f, 1.2f, 0.f, 0.f, 0.f, 0.1f, false);

	/** A grenade (and a tripwire: two grenades on a wire). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Events")
	FSnowStampSpec GrenadeCrater = FSnowStampSpec(ESnowStampShape::Crater, 220.f, 220.f, 1.5f, 1.1f, 0.f, 0.f, 0.f, 0.08f, false);

	/** A fuel barrel: a wide, shallow scorch bowl (the blast goes up, not down). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Events")
	FSnowStampSpec BarrelCrater = FSnowStampSpec(ESnowStampShape::Crater, 280.f, 280.f, 0.9f, 1.0f, 0.f, 0.f, 0.f, 0.08f, false);

	/** A mine: buried, so the deepest bowl and the highest rim. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Events")
	FSnowStampSpec MineCrater = FSnowStampSpec(ESnowStampShape::Crater, 240.f, 240.f, 2.0f, 1.4f, 0.f, 0.f, 0.f, 0.08f, false);

	/** An APC cannon shell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Events")
	FSnowStampSpec CannonCrater = FSnowStampSpec(ESnowStampShape::Crater, 180.f, 180.f, 1.3f, 1.0f, 0.f, 0.f, 0.f, 0.08f, false);

	/** The thrown-snow ring around every crater: a Crater shape with no bowl (Depth 0), FSnowInteractionTuning::CraterSprayScale x the crater (its Length / Width are unused). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Events")
	FSnowStampSpec CraterSpray = FSnowStampSpec(ESnowStampShape::Crater, 1.f, 1.f, 0.f, 0.55f, 0.f, 0.f, 0.f, 0.f, false);

	/** Clods of thrown snow round a crater (FSnowInteractionTuning::CraterClods of them): the small pit + rim a falling chunk leaves. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Events")
	FSnowStampSpec CraterClod = FSnowStampSpec(ESnowStampShape::Dent, 36.f, 28.f, 0.35f, 0.8f, 0.f, 0.f, 180.f, 0.35f, true);

	/** A bullet that missed and hit the snow (UCombatFeedbackSubsystem::OnTracerSpawned + one capped trace). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Events")
	FSnowStampSpec Dent = FSnowStampSpec(ESnowStampShape::Dent, 20.f, 20.f, 0.9f, 0.35f, 0.f, 0.f, 0.f, 0.1f, false);
};

/** System tuning (window, fade, polling); in DA_SnowStamps with the stamps. Visual only. */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FSnowInteractionTuning
{
	GENERATED_BODY()

	/** Walker polls per second (operatives, enemies, vehicles, dragged objects). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Polling", meta = (ClampMin = "1", ClampMax = "60"))
	float PollHz = 20.f;

	/** At most this many stamps per frame; beyond it the farthest from the view are dropped first. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Polling", meta = (ClampMin = "1"))
	int32 MaxStampsPerFlush = 512;

	/** A walker that moved farther than this in one poll was teleported: its tracker restarts without a stamp. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Polling", meta = (ClampMin = "50"))
	float MaxJumpCm = 400.f;

	/** A dragged object may jump this far in one poll (the relocation moves it at once) and still leave its trail. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Polling", meta = (ClampMin = "50"))
	float MaxDragCm = 1500.f;

	/** An object stays tracked this long after it stopped being the relocating object (it moves after the order). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Polling", meta = (ClampMin = "0"))
	float DragTrackSeconds = 10.f;

	/** The APC's ruts lie this far either side of its centre. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicles", meta = (ClampMin = "0"))
	float RutHalfSpacingCm = 105.f;

	/** The window centre leads the view focus by this much along the view yaw (more of the visible ground is covered). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Window", meta = (ClampMin = "0"))
	float WindowLeadCm = 800.f;

	/** Fade passes per second (world time: they pause with the game). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fade", meta = (ClampMin = "0.5", ClampMax = "30"))
	float FadeHz = 5.f;

	/** Tracks fade by exp(-rate x dt), rate = FadeBaseRate x (1 + FadeSnowBoost x MPC SnowRefill). Base: e-fold in 15 min. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fade", meta = (ClampMin = "0"))
	float FadeBaseRate = 1.f / 900.f;

	/** Snowfall refills the tracks: a blizzard (refill 1) fades 13x faster (e-fold ~70 s), Snowfall 0.6 ~8x (~110 s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fade", meta = (ClampMin = "0"))
	float FadeSnowBoost = 12.f;

	/** The berm (G) fades at this multiple of the compaction's rate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fade", meta = (ClampMin = "0"))
	float BermFadeScale = 1.f;

	/** Seconds between clamp passes (additive stamps pile up; the shift pass clamps both channels to 4). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fade", meta = (ClampMin = "1"))
	float ClampPassSeconds = 10.f;

	/** Explosions (phase 3): crater diameter = the event's blast radius x this (clamped to CraterMinCm..CraterMaxCm); no radius = the spec's Length. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Events", meta = (ClampMin = "0"))
	float CraterDiameterPerRadius = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Events", meta = (ClampMin = "10"))
	float CraterMinCm = 120.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Events", meta = (ClampMin = "10"))
	float CraterMaxCm = 380.f;

	/** The thrown-snow ring (FSnowStampTable::CraterSpray) is this many times the crater's size. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Events", meta = (ClampMin = "1"))
	float CraterSprayScale = 1.7f;

	/** Clods of thrown snow round a crater (0 = none). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Events", meta = (ClampMin = "0", ClampMax = "16"))
	int32 CraterClods = 7;

	/** No crater when the ground is farther than this below the blast centre (an air burst, a blast on a roof / a wall). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Events", meta = (ClampMin = "10"))
	float CraterMaxDropCm = 300.f;

	/** Bullet dents: a tracer's ray is extended this far past its end to find the snow a miss hits. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Events", meta = (ClampMin = "0"))
	float ShotExtendCm = 1500.f;

	/** At most this many dent traces a frame (one line trace each); the rest wait in the queue. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Events", meta = (ClampMin = "0", ClampMax = "64"))
	int32 ShotTracesPerFrame = 8;

	/** Tracers waiting for their trace; beyond this the oldest are dropped (a long MG burst). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Events", meta = (ClampMin = "1"))
	int32 ShotQueueMax = 32;

	/** A pawn within this distance of the tracer's end = the shot hit it: no dent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Events", meta = (ClampMin = "0"))
	float ShotHitRadiusCm = 30.f;

	/** A grazing bullet stretches its dent along the ray, at most this many times its length. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Events", meta = (ClampMin = "1", ClampMax = "4"))
	float DentMaxStretch = 2.f;

	/** MPC FreshSnow rises 0 -> 1 in this many seconds of full snowfall... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fresh snow", meta = (ClampMin = "1"))
	float FreshSnowRiseSeconds = 120.f;

	/** ...and falls 1 -> 0 in this many seconds once it stopped (visual only). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fresh snow", meta = (ClampMin = "1"))
	float FreshSnowFadeSeconds = 600.f;
};

/** One stamp to draw: an oriented quad in world XY (cm). Prints / imprints / segments alike. */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FSnowStamp
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Stamp") FVector2D Centre = FVector2D::ZeroVector;
	/** Direction of the stamp's length axis (degrees, world yaw). */
	UPROPERTY(BlueprintReadOnly, Category = "Stamp") float YawDeg = 0.f;
	/** Print length / segment length (cm). */
	UPROPERTY(BlueprintReadOnly, Category = "Stamp") float LengthCm = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Stamp") float WidthCm = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Stamp") float Depth = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Stamp") float Berm = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Stamp") ESnowStampShape Shape = ESnowStampShape::Boot;
};

/** A tracer waiting for its dent trace (phase 3). */
struct FSnowShot
{
	FVector Start = FVector::ZeroVector;
	FVector End = FVector::ZeroVector;
};

/** The crater of one explosion (SnowInteractionRules::ComputeCrater; phase 3). */
struct FSnowCraterParams
{
	float DiameterCm = 0.f;
	float Depth = 0.f;
	float Berm = 0.f;
	float SprayDiameterCm = 0.f;
	float SprayBerm = 0.f;
	int32 Clods = 0;
};

/** What a walker stands on (SnowInteractionRules::IsSnowGround). */
enum class ESnowGroundKind : uint8
{
	/** Not on the ground (falling, climbing, no floor). */
	None,
	Landscape,
	/** A static / stationary static mesh (the current levels' floors). */
	StaticMesh,
	/** Anything else (a movable prop, a pawn, a physics body). */
	Other
};

/** Per walker: the print stride. */
struct FSnowStrideState
{
	FVector2D Last = FVector2D::ZeroVector;
	float Distance = 0.f;
	bool bLeft = false;
	bool bValid = false;
};

/** Per trail: the last point a segment ended at. */
struct FSnowTrailState
{
	FVector2D Last = FVector2D::ZeroVector;
	bool bValid = false;
};

/** The scrolling window of the interaction RT (world cm). */
struct FSnowTrackWindow
{
	FVector2D Origin = FVector2D::ZeroVector;
	double SizeCm = 6400.;
	int32 Resolution = 2048;
	bool bValid = false;
};
