// CodexTactics.Tactics.Cover.* — facing along the wall in cover (user design rule 2026-10-06; UE-only, no Godot
// reference): the back against the wall, the body along it towards the last known threat; shimmy forward towards it,
// backwards away from it; threat side with hysteresis; corner pose / snap at the exposed edge on that side.

#include "Misc/AutomationTest.h"
#include "Tactics/CoverFacingRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define COVER_FACING_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Tactics.Cover." TestPath, EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace CoverFacingTest
{
	/** A wall along Y at X = -45 cm; the slot at the origin facing out along +X: his Right = +Y. */
	FCoverSlot MakeSlot(bool bLeftEdge = false, float LeftCm = 0.f, bool bRightEdge = false, float RightCm = 0.f)
	{
		FCoverSlot Slot;
		Slot.WorldLocation = FVector::ZeroVector;
		Slot.WallPoint = FVector(-45.f, 0.f, 90.f);
		Slot.WallNormal = FVector(1.f, 0.f, 0.f);
		Slot.Height = ECoverHeight::HighCover;
		Slot.bLeftEdgeExposed = bLeftEdge;
		Slot.LeftEdgeDistanceCm = LeftCm;
		Slot.bRightEdgeExposed = bRightEdge;
		Slot.RightEdgeDistanceCm = RightCm;
		return Slot;
	}
}

COVER_FACING_TEST(FCoverShimmyFacesThreatTest, "ShimmyFacesThreat")
bool FCoverShimmyFacesThreatTest::RunTest(const FString& Parameters)
{
	using namespace CoverFacingRules;
	const FCoverSlot Slot = CoverFacingTest::MakeSlot();
	TestTrue(TEXT("right tangent = +Y"), Slot.RightTangent().Equals(FVector(0.f, 1.f, 0.f), 0.001f));

	// Threat out in front, to his right (+Y): he faces right along the wall (+Y, yaw 90), not out of the wall (yaw 0).
	const FVector ThreatRight(800.f, 600.f, 0.f);
	TestEqual(TEXT("threat 6 m along to the right"), ThreatAlongWall(Slot, ThreatRight), 600.f, 0.01f);
	const ECoverFacing Facing = ResolveThreatSide(Slot, ThreatRight, ECoverFacing::Left);
	TestEqual(TEXT("faces the threat side (right)"), static_cast<int32>(Facing), static_cast<int32>(ECoverFacing::Right));
	const float NormalYaw = static_cast<float>(FRotator::NormalizeAxis(Slot.WallNormal.Rotation().Yaw));
	TestEqual(TEXT("actor yaw = wall normal (back to the wall)"), static_cast<float>(FRotator::NormalizeAxis(FacingYaw(Slot, Facing))), NormalYaw, 0.01f);
	TestEqual(TEXT("left side: same yaw, only the clip differs"), static_cast<float>(FRotator::NormalizeAxis(FacingYaw(Slot, ECoverFacing::Left))), NormalYaw, 0.01f);
	TestTrue(TEXT("aligned within 10 deg of the normal"), IsFacingAligned(NormalYaw + 5.f, Slot, Facing, 10.f));
	TestFalse(TEXT("not aligned turned along the wall (+90)"), IsFacingAligned(NormalYaw + 90.f, Slot, Facing, 10.f));
	TestEqual(TEXT("right = clip 1"), ClipIndex(Facing), 1);
	TestEqual(TEXT("left = clip 0"), ClipIndex(ECoverFacing::Left), 0);

	// Shimmy towards the threat side = forward clip, away from it = backwards (still facing the threat).
	TestTrue(TEXT("facing right, shimmy right: forward"), IsShimmyForward(ECoverFacing::Right, 1.f));
	TestFalse(TEXT("facing right, shimmy left: backwards"), IsShimmyForward(ECoverFacing::Right, -1.f));
	TestTrue(TEXT("facing left, shimmy left: forward"), IsShimmyForward(ECoverFacing::Left, -1.f));
	TestFalse(TEXT("facing left, shimmy right: backwards"), IsShimmyForward(ECoverFacing::Left, 1.f));

	// Clips: pack *_L = index 0 (facing left), *_R = index 1 (facing right).
	TestEqual(TEXT("left clip index"), ClipIndex(ECoverFacing::Left), 0);
	TestEqual(TEXT("right clip index"), ClipIndex(ECoverFacing::Right), 1);
	TestTrue(TEXT("along-wall direction right = tangent"), AlongWallDirection(Slot, ECoverFacing::Right).Equals(Slot.RightTangent(), 0.001f));
	TestTrue(TEXT("along-wall direction left = -tangent"), AlongWallDirection(Slot, ECoverFacing::Left).Equals(-Slot.RightTangent(), 0.001f));
	return true;
}

COVER_FACING_TEST(FCoverUnknownThreatAlwaysForwardTest, "UnknownThreatAlwaysForward")
bool FCoverUnknownThreatAlwaysForwardTest::RunTest(const FString& Parameters)
{
	using namespace CoverFacingRules;
	// No threat known: every shimmy is face-forward in its direction, whatever the (default) facing.
	for (const ECoverFacing Facing : { ECoverFacing::Left, ECoverFacing::Right })
	{
		TestTrue(TEXT("unknown threat, moving right: forward"), IsShimmyForward(Facing, 1.f, false));
		TestTrue(TEXT("unknown threat, moving left: forward"), IsShimmyForward(Facing, -1.f, false));
	}
	// The facing after the move follows the movement; a known threat keeps its side.
	TestEqual(TEXT("unknown, moved left: faces left"), static_cast<int32>(FacingForShimmy(ECoverFacing::Right, -1.f, false)), static_cast<int32>(ECoverFacing::Left));
	TestEqual(TEXT("unknown, moved right: faces right"), static_cast<int32>(FacingForShimmy(ECoverFacing::Left, 1.f, false)), static_cast<int32>(ECoverFacing::Right));
	TestEqual(TEXT("known: keeps the threat side"), static_cast<int32>(FacingForShimmy(ECoverFacing::Right, -1.f, true)), static_cast<int32>(ECoverFacing::Right));
	// Clips: unknown threat, moving left = fwd_loop_L (index 0), moving right = fwd_loop_R (index 1).
	TestEqual(TEXT("unknown, left: fwd_loop_L"), ShimmyClipIndex(FacingForShimmy(ECoverFacing::Right, -1.f, false), true), 0);
	TestEqual(TEXT("unknown, right: fwd_loop_R"), ShimmyClipIndex(FacingForShimmy(ECoverFacing::Left, 1.f, false), true), 1);
	return true;
}

COVER_FACING_TEST(FCoverShimmyClipByMovementTest, "ShimmyClipByMovementDirection")
bool FCoverShimmyClipByMovementTest::RunTest(const FString& Parameters)
{
	using namespace CoverFacingRules;
	// M4 walk clips: the suffix is the MOVEMENT direction (0 = _L, 1 = _R). Threat right: right = fwd_R, left = bwd_L.
	TestEqual(TEXT("threat right, move right: fwd_loop_R"), ShimmyClipIndex(ECoverFacing::Right, true), 1);
	TestEqual(TEXT("threat right, move left: bwd_loop_L"), ShimmyClipIndex(ECoverFacing::Right, false), 0);
	// Threat left: left = fwd_L, right = bwd_R.
	TestEqual(TEXT("threat left, move left: fwd_loop_L"), ShimmyClipIndex(ECoverFacing::Left, true), 0);
	TestEqual(TEXT("threat left, move right: bwd_loop_R"), ShimmyClipIndex(ECoverFacing::Left, false), 1);
	return true;
}

COVER_FACING_TEST(FCoverFireReadyRuleTest, "FireReadyCornerPose")
bool FCoverFireReadyRuleTest::RunTest(const FString& Parameters)
{
	using namespace CoverFacingRules;
	const float Hold = FCoverFacingConfig().FireReadyHoldSeconds;
	TestTrue(TEXT("corner + fresh threat: fire-ready"), IsFireReady(true, false, true, 0.5f, Hold));
	TestFalse(TEXT("no known threat: look-around idle"), IsFireReady(true, false, false, 0.f, Hold));
	TestFalse(TEXT("threat older than the hold: relaxes"), IsFireReady(true, false, true, Hold + 0.1f, Hold));
	TestFalse(TEXT("shimmying off the edge: not ready"), IsFireReady(true, true, true, 0.f, Hold));
	TestFalse(TEXT("not at the corner: not ready"), IsFireReady(false, false, true, 0.f, Hold));
	return true;
}

COVER_FACING_TEST(FCoverThreatSideFlipHysteresisTest, "ThreatSideFlipHysteresis")
bool FCoverThreatSideFlipHysteresisTest::RunTest(const FString& Parameters)
{
	using namespace CoverFacingRules;
	const FCoverSlot Slot = CoverFacingTest::MakeSlot();
	const float Margin = FCoverFacingConfig().ThreatSideHysteresisCm;
	// Facing right: a threat drifting just past the middle to the left does not flip him.
	TestEqual(TEXT("right, threat 0.5 m to the left: keeps right"),
		static_cast<int32>(ResolveThreatSide(Slot, FVector(900.f, -50.f, 0.f), ECoverFacing::Right, Margin)), static_cast<int32>(ECoverFacing::Right));
	TestEqual(TEXT("right, threat straight ahead: keeps right"),
		static_cast<int32>(ResolveThreatSide(Slot, FVector(900.f, 0.f, 0.f), ECoverFacing::Right, Margin)), static_cast<int32>(ECoverFacing::Right));
	// The enemy flanks from the left: he turns round.
	TestEqual(TEXT("right, threat 3 m to the left: turns left"),
		static_cast<int32>(ResolveThreatSide(Slot, FVector(900.f, -300.f, 0.f), ECoverFacing::Right, Margin)), static_cast<int32>(ECoverFacing::Left));
	// And back: symmetric margin.
	TestEqual(TEXT("left, threat 0.5 m to the right: keeps left"),
		static_cast<int32>(ResolveThreatSide(Slot, FVector(900.f, 50.f, 0.f), ECoverFacing::Left, Margin)), static_cast<int32>(ECoverFacing::Left));
	TestEqual(TEXT("left, threat 1 m to the right: turns right"),
		static_cast<int32>(ResolveThreatSide(Slot, FVector(900.f, 100.f, 0.f), ECoverFacing::Left, Margin)), static_cast<int32>(ECoverFacing::Right));
	// A threat jittering around the middle never flickers.
	ECoverFacing Facing = ECoverFacing::Right;
	int32 Flips = 0;
	for (int32 Step = 0; Step < 40; ++Step)
	{
		const float Jitter = (Step % 2 == 0 ? 1.f : -1.f) * Margin * 0.8f;
		const ECoverFacing Next = ResolveThreatSide(Slot, FVector(900.f, Jitter, 0.f), Facing, Margin);
		Flips += Next != Facing ? 1 : 0;
		Facing = Next;
	}
	TestEqual(TEXT("no flicker for a threat jittering +-0.8 x margin"), Flips, 0);
	// Without hysteresis the sign decides.
	TestEqual(TEXT("margin 0: sign decides"),
		static_cast<int32>(ResolveThreatSide(Slot, FVector(900.f, -1.f, 0.f), ECoverFacing::Right, 0.f)), static_cast<int32>(ECoverFacing::Left));
	return true;
}

COVER_FACING_TEST(FCoverThreatPriorityTest, "ThreatPriorityAndDefaultSide")
bool FCoverThreatPriorityTest::RunTest(const FString& Parameters)
{
	using namespace CoverFacingRules;
	TArray<FCoverThreatCandidate> Candidates;
	TestEqual(TEXT("nothing known"), PickThreat(Candidates), static_cast<int32>(INDEX_NONE));
	Candidates.Add({ FVector(500.f, 0.f, 0.f), ECoverThreatSource::Heard, 500.f });
	Candidates.Add({ FVector(2000.f, 0.f, 0.f), ECoverThreatSource::Visible, 2000.f });
	Candidates.Add({ FVector(1200.f, 0.f, 0.f), ECoverThreatSource::Visible, 1200.f });
	TestEqual(TEXT("a visible enemy beats a nearer heard one; the nearest visible"), PickThreat(Candidates), 2);
	Candidates.Add({ FVector(3000.f, 0.f, 0.f), ECoverThreatSource::PriorityTarget, 3000.f });
	TestEqual(TEXT("the priority target beats everything"), PickThreat(Candidates), 3);
	TArray<FCoverThreatCandidate> Heard = { { FVector(900.f, 0.f, 0.f), ECoverThreatSource::Heard, 900.f }, { FVector(400.f, 0.f, 0.f), ECoverThreatSource::Heard, 400.f } };
	TestEqual(TEXT("only heard: the nearest"), PickThreat(Heard), 1);

	// None known: the nearest exposed edge; none exposed: unchanged.
	TestEqual(TEXT("only the left edge exposed"), static_cast<int32>(DefaultSide(CoverFacingTest::MakeSlot(true, 120.f), ECoverFacing::Right)),
		static_cast<int32>(ECoverFacing::Left));
	TestEqual(TEXT("both exposed: the nearer (right 80 cm)"), static_cast<int32>(DefaultSide(CoverFacingTest::MakeSlot(true, 160.f, true, 80.f), ECoverFacing::Left)),
		static_cast<int32>(ECoverFacing::Right));
	TestEqual(TEXT("no edge: keeps the facing"), static_cast<int32>(DefaultSide(CoverFacingTest::MakeSlot(), ECoverFacing::Left)), static_cast<int32>(ECoverFacing::Left));
	return true;
}

COVER_FACING_TEST(FCoverCornerPoseSnapTest, "CornerPoseAndSnap")
bool FCoverCornerPoseSnapTest::RunTest(const FString& Parameters)
{
	using namespace CoverFacingRules;
	const FCoverFacingConfig Config;
	// Edge 80 cm away on the facing side: at the corner, no walk.
	const FCoverSlot Near = CoverFacingTest::MakeSlot(false, 0.f, true, 80.f);
	float Shift = -1.f;
	TestTrue(TEXT("edge 0.8 m on the facing side: corner pose"), IsAtCorner(Near, ECoverFacing::Right, Config));
	TestFalse(TEXT("... no snap needed"), ShouldSnapToCorner(Near, ECoverFacing::Right, Shift, Config));
	TestFalse(TEXT("facing the other (closed) side: no corner pose"), IsAtCorner(Near, ECoverFacing::Left, Config));
	// Edge 1.6 m away: walk 1.0 m towards it (60 cm short of the measured edge).
	const FCoverSlot Mid = CoverFacingTest::MakeSlot(false, 0.f, true, 160.f);
	TestFalse(TEXT("edge 1.6 m away: not yet at the corner"), IsAtCorner(Mid, ECoverFacing::Right, Config));
	TestTrue(TEXT("edge 1.6 m away: snap"), ShouldSnapToCorner(Mid, ECoverFacing::Right, Shift, Config));
	TestEqual(TEXT("snap 1.0 m"), Shift, 100.f, 0.01f);
	// Too far / not exposed / the other side: no snap.
	TestFalse(TEXT("no exposed edge on the facing side: no snap"), ShouldSnapToCorner(Mid, ECoverFacing::Left, Shift, Config));
	const FCoverSlot Far = CoverFacingTest::MakeSlot(false, 0.f, true, 260.f);
	TestFalse(TEXT("edge 2.6 m away: no snap"), ShouldSnapToCorner(Far, ECoverFacing::Right, Shift, Config));
	TestEqual(TEXT("edge distance of a closed side"), EdgeDistance(Mid, ECoverFacing::Left), -1.f);
	return true;
}

#undef COVER_FACING_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
