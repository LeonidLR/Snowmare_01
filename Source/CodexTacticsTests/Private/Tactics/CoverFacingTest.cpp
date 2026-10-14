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
	TestEqual(TEXT("right (his own right) = clip 0 (pack _L)"), ClipIndex(Facing), 0);
	TestEqual(TEXT("left = clip 1 (pack _R)"), ClipIndex(ECoverFacing::Left), 1);

	// Shimmy towards the threat side = forward clip, away from it = backwards (still facing the threat).
	TestTrue(TEXT("facing right, shimmy right: forward"), IsShimmyForward(ECoverFacing::Right, 1.f));
	TestFalse(TEXT("facing right, shimmy left: backwards"), IsShimmyForward(ECoverFacing::Right, -1.f));
	TestTrue(TEXT("facing left, shimmy left: forward"), IsShimmyForward(ECoverFacing::Left, -1.f));
	TestFalse(TEXT("facing left, shimmy right: backwards"), IsShimmyForward(ECoverFacing::Left, 1.f));

	// Clips (measured 2026-10-06): the pack names its sides facing the wall, so pack *_L = index 0 = his own RIGHT as he
	// stands back to the wall (facing Right), *_R = index 1 = his left.
	TestEqual(TEXT("left clip index (pack _R)"), ClipIndex(ECoverFacing::Left), 1);
	TestEqual(TEXT("right clip index (pack _L)"), ClipIndex(ECoverFacing::Right), 0);
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
	// Clips: unknown threat, moving to his left = fwd_loop_R (index 1), to his right = fwd_loop_L (index 0).
	TestEqual(TEXT("unknown, his left: fwd_loop_R"), ShimmyClipIndex(FacingForShimmy(ECoverFacing::Right, -1.f, false), true), 1);
	TestEqual(TEXT("unknown, his right: fwd_loop_L"), ShimmyClipIndex(FacingForShimmy(ECoverFacing::Left, 1.f, false), true), 0);
	return true;
}

COVER_FACING_TEST(FCoverShimmyClipByMovementTest, "ShimmyClipByMovementDirection")
bool FCoverShimmyClipByMovementTest::RunTest(const FString& Parameters)
{
	using namespace CoverFacingRules;
	// M4 walk clips: the suffix is the MOVEMENT direction in the pack's naming (0 = _L = towards his own right, 1 = _R =
	// towards his left; measured from the planted-foot slide). Threat on his right: right = fwd_L, left = bwd_R.
	TestEqual(TEXT("threat right, move right: fwd_loop_L"), ShimmyClipIndex(ECoverFacing::Right, true), 0);
	TestEqual(TEXT("threat right, move left: bwd_loop_R"), ShimmyClipIndex(ECoverFacing::Right, false), 1);
	// Threat on his left: left = fwd_R, right = bwd_L.
	TestEqual(TEXT("threat left, move left: fwd_loop_R"), ShimmyClipIndex(ECoverFacing::Left, true), 1);
	TestEqual(TEXT("threat left, move right: bwd_loop_L"), ShimmyClipIndex(ECoverFacing::Left, false), 0);
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
	// Edge 70 cm away on the facing side (right: pack _L, stand-off 57 cm): at the corner, within the snap tolerance.
	const FCoverSlot Near = CoverFacingTest::MakeSlot(false, 0.f, true, 70.f);
	float Shift = -1.f;
	TestTrue(TEXT("edge 0.7 m on the facing side: corner pose"), IsAtCorner(Near, ECoverFacing::Right, Config));
	TestFalse(TEXT("... no snap needed (13 cm over the stand-off)"), ShouldSnapToCorner(Near, ECoverFacing::Right, Shift, Config));
	TestFalse(TEXT("facing the other (closed) side: no corner pose"), IsAtCorner(Near, ECoverFacing::Left, Config));
	// Edge 1.6 m away: walk towards it, stopping the pack _L stand-off (57 cm) short of the measured edge.
	const FCoverSlot Mid = CoverFacingTest::MakeSlot(false, 0.f, true, 160.f);
	TestFalse(TEXT("edge 1.6 m away: not yet at the corner"), IsAtCorner(Mid, ECoverFacing::Right, Config));
	TestTrue(TEXT("edge 1.6 m away: snap"), ShouldSnapToCorner(Mid, ECoverFacing::Right, Shift, Config));
	TestEqual(TEXT("snap 1.03 m"), Shift, 160.f - Config.StandStandOffPackLCm, 0.01f);
	// Too far / not exposed / the other side: no snap.
	TestFalse(TEXT("no exposed edge on the facing side: no snap"), ShouldSnapToCorner(Mid, ECoverFacing::Left, Shift, Config));
	const FCoverSlot Far = CoverFacingTest::MakeSlot(false, 0.f, true, 260.f);
	TestFalse(TEXT("edge 2.6 m away: no snap"), ShouldSnapToCorner(Far, ECoverFacing::Right, Shift, Config));
	TestEqual(TEXT("edge distance of a closed side"), EdgeDistance(Mid, ECoverFacing::Left), -1.f);
	return true;
}

COVER_FACING_TEST(FCoverCornerPeekEdgeHitTest, "CornerPeekEdgeHit")
bool FCoverCornerPeekEdgeHitTest::RunTest(const FString& Parameters)
{
	// Manual corner peek by click (user request 2026-10-13, rev 5c): which clicks mean "the edge of my wall".
	using namespace CoverFacingRules;
	const FCoverSlot Slot = CoverFacingTest::MakeSlot(true, 150.f, true, 120.f); // left edge 1.5 m (-Y), right edge 1.2 m (+Y)
	ECoverFacing Side = ECoverFacing::Left;
	float Edge = 0.f;
	TestTrue(TEXT("the wall face 20 cm in from the right edge"), FindPeekEdge(Slot, FVector(-45.f, 100.f, 90.f), nullptr, Side, Edge)
		&& Side == ECoverFacing::Right && FMath::IsNearlyEqual(Edge, 120.f));
	TestTrue(TEXT("the end face of the wall (behind the face plane, at the edge)"), FindPeekEdge(Slot, FVector(-60.f, 120.f, 90.f), nullptr, Side, Edge)
		&& Side == ECoverFacing::Right);
	TestTrue(TEXT("the top of the wall near the right edge"), FindPeekEdge(Slot, FVector(-65.f, 110.f, 300.f), nullptr, Side, Edge) && Side == ECoverFacing::Right);
	TestTrue(TEXT("40 cm past the edge (the cursor slipped off the corner)"), FindPeekEdge(Slot, FVector(-45.f, 160.f, 90.f), nullptr, Side, Edge)
		&& Side == ECoverFacing::Right);
	TestFalse(TEXT("1 m past the edge: not the wall any more"), FindPeekEdge(Slot, FVector(-45.f, 220.f, 90.f), nullptr, Side, Edge));
	TestFalse(TEXT("the middle of the wall: a shimmy, no peek"), FindPeekEdge(Slot, FVector(-45.f, 0.f, 90.f), nullptr, Side, Edge));
	TestFalse(TEXT("80 cm in from the edge: still a shimmy"), FindPeekEdge(Slot, FVector(-45.f, 40.f, 90.f), nullptr, Side, Edge));
	TestTrue(TEXT("near the left edge: the left corner"), FindPeekEdge(Slot, FVector(-45.f, -120.f, 90.f), nullptr, Side, Edge)
		&& Side == ECoverFacing::Left && FMath::IsNearlyEqual(Edge, -150.f));
	TestFalse(TEXT("2 m in front of the wall (a ground click)"), FindPeekEdge(Slot, FVector(155.f, 110.f, 0.f), nullptr, Side, Edge));
	TestFalse(TEXT("far behind the wall"), FindPeekEdge(Slot, FVector(-400.f, 110.f, 90.f), nullptr, Side, Edge));
	TestFalse(TEXT("a closed side (the wall goes on) has no edge to peek round"),
		FindPeekEdge(CoverFacingTest::MakeSlot(), FVector(-45.f, 110.f, 90.f), nullptr, Side, Edge));
	FCoverSlot Low = Slot;
	Low.Height = ECoverHeight::LowCover;
	TestTrue(TEXT("rev 5e: low cover peeks too (the crouched corner stance round its end)"), FindPeekEdge(Low, FVector(-45.f, 100.f, 90.f), nullptr, Side, Edge)
		&& Side == ECoverFacing::Right);
	// A long wall: the end lies beyond his slot's corner probes; a slot traced near the click knows it.
	const FCoverSlot Long = CoverFacingTest::MakeSlot();
	FCoverSlot Probe = CoverFacingTest::MakeSlot(false, 0.f, true, 30.f);
	Probe.WorldLocation = FVector(0.f, 400.f, 0.f);
	Probe.WallPoint = FVector(-45.f, 400.f, 90.f);
	TestTrue(TEXT("long wall: the probed slot's edge 4.3 m along"), FindPeekEdge(Long, FVector(-45.f, 420.f, 90.f), &Probe, Side, Edge)
		&& Side == ECoverFacing::Right && FMath::IsNearlyEqual(Edge, 430.f));
	FCoverSlot Other = Probe;
	Other.WallNormal = FVector(0.f, 1.f, 0.f);
	TestFalse(TEXT("a probe on another wall (round the corner) is ignored"), FindPeekEdge(Long, FVector(-45.f, 420.f, 90.f), &Other, Side, Edge));
	// Where he stands for the peek: the clip side's stand-off short of the edge.
	TestEqual(TEXT("right edge at 120, stand-off 57: stand at 63"), CornerPeekStandAlong(ECoverFacing::Right, 120.f, 57.f), 63.f, 0.01f);
	TestEqual(TEXT("left edge at -150, stand-off 28: stand at -122"), CornerPeekStandAlong(ECoverFacing::Left, -150.f, 28.f), -122.f, 0.01f);
	return true;
}

COVER_FACING_TEST(FCoverCornerPeekNearEdgeTest, "CornerPeekNearEdge")
bool FCoverCornerPeekNearEdgeTest::RunTest(const FString& Parameters)
{
	// Rev 5d (user bug 2026-10-14): he hugs a high wall at its left end; hovering that end showed the peek ghost, the click did
	// nothing (the proximity "click near an operative picks him" rule ate it). The far end worked.
	using namespace CoverFacingRules;
	const FCoverFacingConfig Config;
	// Click precedence: the edge of his own wall wins over the 120 cm proximity selection; his body still selects.
	TestTrue(TEXT("near-edge click on his wall: the peek, not a selection"), CornerPeekClickWins(true, false));
	TestFalse(TEXT("a click on an operative's body: a selection"), CornerPeekClickWins(true, true));
	TestFalse(TEXT("not a peek edge: the other click rules"), CornerPeekClickWins(false, false));

	// Every end / facing / stance: snapped to the corner (the stand point) -> the aim starts in place, real time and turn-based.
	struct FCase
	{
		ECoverFacing Side;
		bool bCrouched;
		const TCHAR* Name;
	};
	for (const FCase& Case : { FCase{ ECoverFacing::Left, false, TEXT("left end, standing") }, FCase{ ECoverFacing::Right, false, TEXT("right end, standing") },
		FCase{ ECoverFacing::Left, true, TEXT("left end, crouched") }, FCase{ ECoverFacing::Right, true, TEXT("right end, crouched") } })
	{
		const float StandOff = CornerStandOff(Case.Side, Case.bCrouched, Config);
		const float Edge = Case.Side == ECoverFacing::Right ? StandOff : -StandOff; // the slot (body at 0) sits at the stand-off
		const float Stand = CornerPeekStandAlong(Case.Side, Edge, StandOff);
		TestEqual(FString::Printf(TEXT("%s: the stand point is where he is"), Case.Name), Stand, 0.f, 0.01f);
		const FCoverSlot Slot = CoverFacingTest::MakeSlot(Case.Side == ECoverFacing::Left, StandOff, Case.Side == ECoverFacing::Right, StandOff);
		const bool bAtCorner = IsAtCorner(Slot, Case.Side, Config);
		TestTrue(FString::Printf(TEXT("%s: his slot is at that corner"), Case.Name), bAtCorner);
		TestTrue(FString::Printf(TEXT("%s: real time -> pose in place"), Case.Name),
			DecideCornerPeekOrder(Stand, 0.f, false, bAtCorner, false, Config) == ECornerPeekOrder::PoseInPlace);
		TestTrue(FString::Printf(TEXT("%s: turn-based -> pose in place (no AP)"), Case.Name),
			DecideCornerPeekOrder(Stand, 0.f, false, bAtCorner, true, Config) == ECornerPeekOrder::PoseInPlace);
		// Near but not at the stand point (40 cm off, the edge 40 cm further): real time shimmies the few cm, turn-based poses
		// where he is (at that corner), a body far from the corner is refused in turn-based.
		const float Off = Case.Side == ECoverFacing::Right ? -40.f : 40.f;
		TestTrue(FString::Printf(TEXT("%s: 40 cm short -> shimmy then pose"), Case.Name),
			DecideCornerPeekOrder(Stand, Off, false, true, false, Config) == ECornerPeekOrder::ShimmyThenPose);
		TestTrue(FString::Printf(TEXT("%s: 40 cm short, turn-based at that corner -> pose in place"), Case.Name),
			DecideCornerPeekOrder(Stand, Off, false, true, true, Config) == ECornerPeekOrder::PoseInPlace);
		TestTrue(FString::Printf(TEXT("%s: 3 m away, turn-based -> refused"), Case.Name),
			DecideCornerPeekOrder(Stand, Off * 7.5f, false, false, true, Config) == ECornerPeekOrder::Refused);
	}
	// Within the tolerance (stand-off + 15 + 10 cm): in place; a running shimmy is never "in place".
	TestTrue(TEXT("20 cm off the stand point: in place"), DecideCornerPeekOrder(0.f, 20.f, false, true, false, Config) == ECornerPeekOrder::PoseInPlace);
	TestTrue(TEXT("30 cm off: a short shimmy"), DecideCornerPeekOrder(0.f, 30.f, false, true, false, Config) == ECornerPeekOrder::ShimmyThenPose);
	TestTrue(TEXT("still shimmying: shimmy again (real time)"), DecideCornerPeekOrder(0.f, 0.f, true, true, false, Config) == ECornerPeekOrder::ShimmyThenPose);
	TestTrue(TEXT("still shimmying, turn-based: refused"), DecideCornerPeekOrder(0.f, 0.f, true, true, true, Config) == ECornerPeekOrder::Refused);
	return true;
}

COVER_FACING_TEST(FCoverCornerPeekLowCoverTest, "CornerPeekLowCover")
bool FCoverCornerPeekLowCoverTest::RunTest(const FString& Parameters)
{
	// Rev 5e (game designer 2026-10-14): crouched behind a LOW wall / barricade, a click on its end = the crouched corner peek.
	using namespace CoverFacingRules;
	const FCoverFacingConfig Config;
	FCoverSlot Low = CoverFacingTest::MakeSlot(true, 140.f, true, 110.f);
	Low.Height = ECoverHeight::LowCover;
	Low.WallPoint.Z = 50.f;
	ECoverFacing Side = ECoverFacing::Left;
	float Edge = 0.f;
	TestTrue(TEXT("the low wall's face near its right end"), FindPeekEdge(Low, FVector(-45.f, 80.f, 50.f), nullptr, Side, Edge)
		&& Side == ECoverFacing::Right && FMath::IsNearlyEqual(Edge, 110.f));
	TestTrue(TEXT("the TOP of the low wall at its left end (behind the face plane)"), FindPeekEdge(Low, FVector(-60.f, -125.f, 100.f), nullptr, Side, Edge)
		&& Side == ECoverFacing::Left && FMath::IsNearlyEqual(Edge, -140.f));
	TestFalse(TEXT("the middle of the low wall: no peek (a shimmy / over the top)"), FindPeekEdge(Low, FVector(-45.f, 0.f, 50.f), nullptr, Side, Edge));
	TestFalse(TEXT("a ground click in front of it: no peek"), FindPeekEdge(Low, FVector(160.f, 100.f, 0.f), nullptr, Side, Edge));
	// A long low wall: a probe slot of the same height gives the far end; a high probe (another obstacle) is ignored.
	const FCoverSlot LongLow = [] { FCoverSlot Slot = CoverFacingTest::MakeSlot(); Slot.Height = ECoverHeight::LowCover; return Slot; }();
	FCoverSlot Probe = CoverFacingTest::MakeSlot(false, 0.f, true, 30.f);
	Probe.Height = ECoverHeight::LowCover;
	Probe.WorldLocation = FVector(0.f, 400.f, 0.f);
	TestTrue(TEXT("long low wall: the low probe's edge"), FindPeekEdge(LongLow, FVector(-45.f, 420.f, 50.f), &Probe, Side, Edge)
		&& Side == ECoverFacing::Right && FMath::IsNearlyEqual(Edge, 430.f));
	Probe.Height = ECoverHeight::HighCover;
	TestFalse(TEXT("a probe of another height is not his wall"), FindPeekEdge(LongLow, FVector(-45.f, 420.f, 50.f), &Probe, Side, Edge));
	// Low cover = crouched: the crouched stand-offs (pack _L 65, _R 49) and the near-edge rule at both ends.
	for (const ECoverFacing End : { ECoverFacing::Left, ECoverFacing::Right })
	{
		const float StandOff = CornerStandOff(End, true, Config);
		TestEqual(TEXT("crouched stand-off"), StandOff, End == ECoverFacing::Right ? 65.f : 49.f, 0.01f);
		FCoverSlot AtEnd = CoverFacingTest::MakeSlot(End == ECoverFacing::Left, StandOff, End == ECoverFacing::Right, StandOff);
		AtEnd.Height = ECoverHeight::LowCover;
		const float EdgeAlong = End == ECoverFacing::Right ? StandOff : -StandOff;
		TestTrue(TEXT("crouched at the low wall's end: the near-edge click poses in place"),
			DecideCornerPeekOrder(CornerPeekStandAlong(End, EdgeAlong, StandOff), 0.f, false, IsAtCorner(AtEnd, End, Config), false, Config) == ECornerPeekOrder::PoseInPlace);
		TestTrue(TEXT("... also in turn-based (no AP)"),
			DecideCornerPeekOrder(CornerPeekStandAlong(End, EdgeAlong, StandOff), 0.f, false, IsAtCorner(AtEnd, End, Config), true, Config) == ECornerPeekOrder::PoseInPlace);
	}
	TestTrue(TEXT("the near-edge click on his own low wall wins over the proximity selection"), CornerPeekClickWins(true, false));
	return true;
}

COVER_FACING_TEST(FCoverCornerStandOffPerClipSideTest, "CornerStandOffPerClipSide")
bool FCoverCornerStandOffPerClipSideTest::RunTest(const FString& Parameters)
{
	using namespace CoverFacingRules;
	const FCoverFacingConfig Config;
	// Measured 2026-10-06: the pack _L peek (his right corner) steps the body ~69 cm out standing / 77 crouched, the _R
	// one (his left corner) ~40 / 61 cm; the stand-off is that minus a 12 cm margin.
	TestEqual(TEXT("his right corner, standing (pack _L)"), CornerStandOff(ECoverFacing::Right, false, Config), 57.f, 0.01f);
	TestEqual(TEXT("his left corner, standing (pack _R)"), CornerStandOff(ECoverFacing::Left, false, Config), 28.f, 0.01f);
	TestEqual(TEXT("his right corner, crouched (pack _L)"), CornerStandOff(ECoverFacing::Right, true, Config), 65.f, 0.01f);
	TestEqual(TEXT("his left corner, crouched (pack _R)"), CornerStandOff(ECoverFacing::Left, true, Config), 49.f, 0.01f);
	TestTrue(TEXT("each stand-off leaves the body past the measured edge"), 69.f - CornerStandOff(ECoverFacing::Right, false, Config) >= 10.f
		&& 40.f - CornerStandOff(ECoverFacing::Left, false, Config) >= 10.f && 77.f - CornerStandOff(ECoverFacing::Right, true, Config) >= 10.f
		&& 61.f - CornerStandOff(ECoverFacing::Left, true, Config) >= 10.f);

	// The same edge 80 cm away on either side: the short-stepping _R side walks closer, the _L side stays.
	float Shift = -1.f;
	const FCoverSlot Both = CoverFacingTest::MakeSlot(true, 80.f, true, 80.f);
	TestTrue(TEXT("right edge 0.8 m: steps 23 cm closer"), ShouldSnapToCorner(Both, ECoverFacing::Right, Shift, Config, false));
	TestEqual(TEXT("... by edge - 57"), Shift, 23.f, 0.01f);
	TestTrue(TEXT("left edge 0.8 m (stand-off 28): steps 52 cm closer"), ShouldSnapToCorner(Both, ECoverFacing::Left, Shift, Config, false));
	TestEqual(TEXT("... by edge - 28"), Shift, 52.f, 0.01f);
	TestTrue(TEXT("left edge 0.8 m crouched (stand-off 49): 31 cm"), ShouldSnapToCorner(Both, ECoverFacing::Left, Shift, Config, true));
	TestEqual(TEXT("... by edge - 49"), Shift, 31.f, 0.01f);
	// Already nearer than the stand-off: never walks away from the edge.
	const FCoverSlot Close = CoverFacingTest::MakeSlot(true, 40.f, true, 40.f);
	TestFalse(TEXT("edge 0.4 m, stand-off 57: no walk away"), ShouldSnapToCorner(Close, ECoverFacing::Right, Shift, Config, false));
	TestEqual(TEXT("... shift 0"), Shift, 0.f, 0.01f);
	return true;
}

COVER_FACING_TEST(FCoverCornerShotOnlyBehindWallTest, "CornerShotOnlyBehindWall")
bool FCoverCornerShotOnlyBehindWallTest::RunTest(const FString& Parameters)
{
	using namespace CoverFacingRules;
	const FCoverFacingConfig Config;
	// Wall face at X = -45 (normal +X), the slot at the origin, his right corner 80 cm away (+Y), the left side closed.
	const FCoverSlot Slot = CoverFacingTest::MakeSlot(false, 0.f, true, 80.f);
	TestEqual(TEXT("depth of the slot itself: 45 cm in front of the face"), DepthInFrontOfWall(Slot, FVector::ZeroVector), 45.f, 0.01f);
	TestTrue(TEXT("behind the wall, straight back: cover shot"), ShouldCornerShot(Slot, FVector(-600.f, 0.f, 0.f), Config));
	TestTrue(TEXT("behind the wall, diagonally past the corner: cover shot"), ShouldCornerShot(Slot, FVector(-400.f, 500.f, 0.f), Config));
	TestTrue(TEXT("beside the wall end, level with its line (around the corner): cover shot"), ShouldCornerShot(Slot, FVector(30.f, 400.f, 0.f), Config));
	TestFalse(TEXT("out in front of the wall: normal shot"), ShouldCornerShot(Slot, FVector(700.f, 0.f, 0.f), Config));
	TestFalse(TEXT("in front, off to the corner side but well out: normal shot"), ShouldCornerShot(Slot, FVector(600.f, 400.f, 0.f), Config));
	TestFalse(TEXT("near the wall line but on the closed side (no exposed edge there): normal shot"), ShouldCornerShot(Slot, FVector(30.f, -400.f, 0.f), Config));
	TestFalse(TEXT("near the wall line but within the wall's span: normal shot"), ShouldCornerShot(Slot, FVector(30.f, 50.f, 0.f), Config));
	// A low cover (barricade): over the top only at targets behind it.
	FCoverSlot Low = Slot;
	Low.Height = ECoverHeight::LowCover;
	TestTrue(TEXT("low cover, target behind it: over the top"), ShouldCornerShot(Low, FVector(-500.f, 0.f, 0.f), Config));
	TestFalse(TEXT("low cover, target on the open side beside it: normal shot"), ShouldCornerShot(Low, FVector(30.f, 400.f, 0.f), Config));
	return true;
}

COVER_FACING_TEST(FCoverFacingKeepEngagedTest, "Facing.KeepWhileEngaged")
bool FCoverFacingKeepEngagedTest::RunTest(const FString& Parameters)
{
	// User PIE video 2026-10-07: hounds swarming in front of the corner flipped the threat side every shot or two,
	// so the fire stance jumped between fire_idle_L and fire_idle_R. While engaged the stance keeps its side.
	using namespace CoverFacingRules;
	const FCoverSlot Corner = CoverFacingTest::MakeSlot(false, 0.f, true, 30.f); // exposed edge on his Right only
	TestEqual(TEXT("engaged at the exposed corner: the threat side flip is ignored"),
		KeepEngagedFacing(Corner, ECoverFacing::Right, ECoverFacing::Left, true), ECoverFacing::Right);
	TestEqual(TEXT("not engaged: he follows the threat"),
		KeepEngagedFacing(Corner, ECoverFacing::Right, ECoverFacing::Left, false), ECoverFacing::Left);
	TestEqual(TEXT("engaged, facing the closed side: he turns to the exposed corner"),
		KeepEngagedFacing(Corner, ECoverFacing::Left, ECoverFacing::Right, true), ECoverFacing::Right);
	TestEqual(TEXT("engaged, no change wanted: unchanged"),
		KeepEngagedFacing(Corner, ECoverFacing::Right, ECoverFacing::Right, true), ECoverFacing::Right);
	const FCoverSlot Flat = CoverFacingTest::MakeSlot();
	TestEqual(TEXT("engaged along a wall with no exposed edge: he keeps his side"),
		KeepEngagedFacing(Flat, ECoverFacing::Left, ECoverFacing::Right, true), ECoverFacing::Left);
	const FCoverSlot Both = CoverFacingTest::MakeSlot(true, 30.f, true, 30.f);
	TestEqual(TEXT("engaged with both edges exposed: he keeps the edge he fights from"),
		KeepEngagedFacing(Both, ECoverFacing::Left, ECoverFacing::Right, true), ECoverFacing::Left);
	return true;
}

COVER_FACING_TEST(FCoverFacingCornerHoldTargetTest, "Facing.CornerHoldTarget")
bool FCoverFacingCornerHoldTargetTest::RunTest(const FString& Parameters)
{
	// User decision 2026-10-07 (PIE video CoverBug_02, 58 s): at the edge he holds the corner stance and fires from it only
	// at what the stance can aim at; a flank rush in front of the wall is no corner shot.
	using namespace CoverFacingRules;
	const FCoverSlot Corner = CoverFacingTest::MakeSlot(false, 0.f, true, 30.f); // exposed edge on his Right (+Y)
	const FVector Straight = CornerAimDirection(Corner, ECoverFacing::Right, 0.f);
	TestTrue(TEXT("0 deg outward: along the wall to his right"), Straight.Equals(FVector(0.f, 1.f, 0.f), 0.001f));
	const FVector Round = CornerAimDirection(Corner, ECoverFacing::Right, 45.f);
	TestTrue(TEXT("45 deg outward: half round the corner, behind the wall"), Round.Equals(FVector(-0.7071f, 0.7071f, 0.f), 0.001f));
	TestTrue(TEXT("the left side mirrors"), CornerAimDirection(Corner, ECoverFacing::Left, 45.f).Equals(FVector(-0.7071f, -0.7071f, 0.f), 0.001f));
	const FVector Origin(0.f, 100.f, 140.f); // stepped out past the edge
	TestTrue(TEXT("behind the wall round the corner: corner shot"),
		IsCornerHoldTarget(Corner, ECoverFacing::Right, Origin, FVector(-400.f, 400.f, 0.f), 45.f, 60.f));
	TestTrue(TEXT("along the wall line past the edge, within reach: corner shot"),
		IsCornerHoldTarget(Corner, ECoverFacing::Right, Origin, FVector(50.f, 900.f, 0.f), 45.f, 60.f));
	TestFalse(TEXT("flank rush in front, past the edge but outside the reach: no corner shot"),
		IsCornerHoldTarget(Corner, ECoverFacing::Right, Origin, FVector(400.f, 300.f, 0.f), 45.f, 60.f));
	TestFalse(TEXT("in front within the wall's span: open side"),
		IsCornerHoldTarget(Corner, ECoverFacing::Right, Origin, FVector(300.f, 0.f, 0.f), 45.f, 60.f));
	TestFalse(TEXT("behind the wall on the other side: out of the stance's reach"),
		IsCornerHoldTarget(Corner, ECoverFacing::Right, Origin, FVector(-400.f, -600.f, 0.f), 45.f, 60.f));
	TestFalse(TEXT("the facing edge is closed: no corner hold"),
		IsCornerHoldTarget(Corner, ECoverFacing::Left, FVector(0.f, -100.f, 140.f), FVector(-400.f, -400.f, 0.f), 45.f, 60.f));
	return true;
}

COVER_FACING_TEST(FCoverCornerHoldOnlyWhenEngagedTest, "Facing.CornerHoldOnlyWhenEngaged")
bool FCoverCornerHoldOnlyWhenEngagedTest::RunTest(const FString&)
{
	using namespace CoverFacingRules;
	// User bug 2026-10-11: at the first wall of the level (exploration, no enemy known) he stepped out past the edge into
	// the corner fire stance instead of hugging the wall. The hold needs the edge AND a fight or a fresh known threat.
	TestFalse(TEXT("exploring, nothing known: he hugs the wall at the edge"), WantsCornerHold(true, false, false));
	TestTrue(TEXT("a fight: he holds the corner (user decision 2026-10-07)"), WantsCornerHold(true, true, false));
	TestTrue(TEXT("exploring with a fresh known threat: he holds the corner"), WantsCornerHold(true, false, true));
	TestFalse(TEXT("away from the exposed edge: never"), WantsCornerHold(false, true, true));
	// With no threat the corner pose is the look-around idle (2026-10-06 clip rule), never the fire-ready one.
	const FCoverSlot Edge = CoverFacingTest::MakeSlot(true, 60.f, false, 0.f);
	TestTrue(TEXT("the edge 60 cm away is the corner"), IsAtCorner(Edge, ECoverFacing::Left));
	TestFalse(TEXT("no threat: no fire-ready pose at the corner"), IsFireReady(true, false, false, 0.f));
	// No threat known: a shimmy either way along the wall is a forward walk (never the backward clip).
	for (const float Direction : { -1.f, 1.f })
	{
		const ECoverFacing Facing = FacingForShimmy(ECoverFacing::Left, Direction, false);
		TestTrue(FString::Printf(TEXT("no threat, shimmy %+.0f: forward"), Direction), IsShimmyForward(Facing, Direction, false));
		TestEqual(FString::Printf(TEXT("no threat, shimmy %+.0f: the forward clip index of the move side"), Direction),
			ShimmyClipIndex(Facing, true), Direction > 0.f ? 0 : 1);
	}
	return true;
}

COVER_FACING_TEST(FCoverEntryHeldYawTest, "Facing.EntryHeldYawNeverSticks")
bool FCoverEntryHeldYawTest::RunTest(const FString&)
{
	using namespace CoverFacingRules;
	// User bug 2026-10-11: running into a wall near its edge, the auto-snap shimmy cut the enter clip and the ~180 deg entry
	// turn stayed on the mesh for good (weight -1 read as 0): turned round, "walking backwards" along the wall.
	TestEqual(TEXT("first frames, the clip not started yet: the old yaw is held"), EntryHeldYawFraction(-1.f, false, 0.f), 1.f);
	TestEqual(TEXT("blending in at 0.4: 60 % held"), EntryHeldYawFraction(0.4f, true, 0.1f), 0.6f, 1.e-4f);
	TestEqual(TEXT("fully in: nothing held"), EntryHeldYawFraction(1.f, true, 0.3f), 0.f);
	TestTrue(TEXT("cut short by a shimmy after it played: hand over to the timed ease-out"), EntryHeldYawFraction(-1.f, true, 0.2f) < 0.f);
	TestTrue(TEXT("never came: hand over after the wait"), EntryHeldYawFraction(-1.f, false, 0.3f) < 0.f);
	return true;
}

COVER_FACING_TEST(FCoverWallSpanTest, "Facing.ClearanceOnlyWithinWallSpan")
bool FCoverWallSpanTest::RunTest(const FString&)
{
	using namespace CoverFacingRules;
	// The wall along Y at X = -45; his Right = +Y. Left edge exposed 26 cm away, the right side closed.
	const FCoverSlot Slot = CoverFacingTest::MakeSlot(true, 26.f, false, 0.f);
	TestTrue(TEXT("behind the slot: within the wall"), IsWithinWallSpan(Slot, FVector(-60.f, 0.f, 90.f)));
	TestTrue(TEXT("20 cm to the left, before the edge: within"), IsWithinWallSpan(Slot, FVector(-60.f, -20.f, 90.f)));
	TestFalse(TEXT("the corner fire stance's hand 60 cm past the left edge: no wall there (no push)"), IsWithinWallSpan(Slot, FVector(-60.f, -86.f, 90.f)));
	TestTrue(TEXT("3 cm past the edge with a 6 cm margin: still counted"), IsWithinWallSpan(Slot, FVector(-60.f, -29.f, 90.f), 6.f));
	TestTrue(TEXT("the closed right side is unbounded"), IsWithinWallSpan(Slot, FVector(-60.f, 900.f, 90.f)));
	return true;
}

#undef COVER_FACING_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
