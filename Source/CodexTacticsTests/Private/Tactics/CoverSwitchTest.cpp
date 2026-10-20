// CodexTactics.Tactics.CoverSwitch.* - the wall-edge cover switch (game designer 2026-10-20, Tactics/CoverSwitchRules): in cover at a
// wall EDGE, a double-click on ANOTHER cover within range dashes there and arrives through the M4 pack's run-in transition
// (std_jog_fwd[_L|_R]_to_cvr_*); the clip and its start come from the geometry. UE-only (no Godot reference).

#include "Misc/AutomationTest.h"
#include "Core/PlayerCommands.h"
#include "Tactics/CoverSwitchRules.h"
#include "Tutorial/TutorialRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define COVER_SWITCH_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Tactics.CoverSwitch." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace CoverSwitchTestUtil
{
	/** A high-cover slot at Location, the wall behind it facing +X (his right = +Y), with the given exposed edges (cm; < 0 = closed). */
	FCoverSlot MakeSwitchSlot(const FVector& Location, float LeftEdgeCm, float RightEdgeCm, ECoverHeight Height = ECoverHeight::HighCover)
	{
		FCoverSlot Slot;
		Slot.WorldLocation = Location;
		Slot.WallPoint = Location - FVector(45.f, 0.f, 0.f);
		Slot.WallNormal = FVector::ForwardVector;
		Slot.Height = Height;
		Slot.bLeftEdgeExposed = LeftEdgeCm >= 0.f;
		Slot.LeftEdgeDistanceCm = FMath::Max(LeftEdgeCm, 0.f);
		Slot.bRightEdgeExposed = RightEdgeCm >= 0.f;
		Slot.RightEdgeDistanceCm = FMath::Max(RightEdgeCm, 0.f);
		return Slot;
	}

	/** A straight synthetic run-in track: the clip body from Start (offset from the slot) to the slot over Seconds, then still. */
	FCoverSwitchTrack MakeSwitchTrack(const FVector2D& Start, float Seconds, float HoldSeconds = 0.3f)
	{
		FCoverSwitchTrack Track;
		const int32 Steps = 30;
		for (int32 Index = 0; Index <= Steps; ++Index)
		{
			const float Alpha = static_cast<float>(Index) / Steps;
			Track.Times.Add(Seconds * Alpha);
			Track.Offsets.Add(Start * (1.f - Alpha));
		}
		Track.Times.Add(Seconds + HoldSeconds);
		Track.Offsets.Add(FVector2D::ZeroVector);
		return Track;
	}
}

COVER_SWITCH_TEST(FCoverSwitchEdgeEligibilityTest, "EdgeEligibility")
bool FCoverSwitchEdgeEligibilityTest::RunTest(const FString&)
{
	using namespace CoverSwitchTestUtil;
	const FCoverSwitchConfig Config;
	const FCoverSlot Target = MakeSwitchSlot(FVector(150.f, -500.f, 0.f), 60.f, 250.f);
	// His left (-Y, the wall facing +X) edge 60 cm away: at the edge.
	const FCoverSlot AtLeftEnd = MakeSwitchSlot(FVector::ZeroVector, 60.f, 340.f);
	ECoverFacing Edge = ECoverFacing::Right;
	TestTrue(TEXT("60 cm from his left edge: at a wall edge"), CoverSwitchRules::FindSwitchEdge(AtLeftEnd, Target.WorldLocation, Config.EdgeReachCm, Edge));
	TestEqual(TEXT("... the left one"), static_cast<int32>(Edge), static_cast<int32>(ECoverFacing::Left));
	TestEqual(TEXT("At the edge, another wall 5 m away: a switch"),
		static_cast<int32>(CoverSwitchRules::Evaluate(true, false, AtLeftEnd, Target, false, Config)), static_cast<int32>(ECoverSwitchVerdict::Switch));
	// The middle of a long wall: no edge within the reach.
	const FCoverSlot Middle = MakeSwitchSlot(FVector::ZeroVector, 200.f, 200.f);
	TestEqual(TEXT("The middle of the wall (edges 2 m away): not at an edge"),
		static_cast<int32>(CoverSwitchRules::Evaluate(true, false, Middle, Target, false, Config)), static_cast<int32>(ECoverSwitchVerdict::NotAtEdge));
	// A closed edge (the wall goes on into another one) never counts.
	const FCoverSlot ClosedEnd = MakeSwitchSlot(FVector::ZeroVector, -1.f, 300.f);
	TestEqual(TEXT("At a closed end: not at an edge"), static_cast<int32>(CoverSwitchRules::Evaluate(true, false, ClosedEnd, Target, false, Config)), static_cast<int32>(ECoverSwitchVerdict::NotAtEdge));
	// A short wall (both edges close): the edge towards the target.
	const FCoverSlot Short = MakeSwitchSlot(FVector::ZeroVector, 70.f, 70.f);
	TestTrue(TEXT("Short wall, target on his right (+Y): the right edge"),
		CoverSwitchRules::FindSwitchEdge(Short, FVector(100.f, 600.f, 0.f), Config.EdgeReachCm, Edge) && Edge == ECoverFacing::Right);
	TestTrue(TEXT("Short wall, target on his left (-Y): the left edge"),
		CoverSwitchRules::FindSwitchEdge(Short, FVector(100.f, -600.f, 0.f), Config.EdgeReachCm, Edge) && Edge == ECoverFacing::Left);
	// The other rules, in order.
	TestEqual(TEXT("Not in cover"), static_cast<int32>(CoverSwitchRules::Evaluate(false, false, AtLeftEnd, Target, false, Config)), static_cast<int32>(ECoverSwitchVerdict::NotInCover));
	TestEqual(TEXT("Side-stepping along the wall"), static_cast<int32>(CoverSwitchRules::Evaluate(true, true, AtLeftEnd, Target, false, Config)), static_cast<int32>(ECoverSwitchVerdict::Shimmying));
	TestEqual(TEXT("A click on his own wall: a shimmy / peek, never a switch"),
		static_cast<int32>(CoverSwitchRules::Evaluate(true, false, AtLeftEnd, Target, true, Config)), static_cast<int32>(ECoverSwitchVerdict::SameWall));
	TestEqual(TEXT("No target slot"), static_cast<int32>(CoverSwitchRules::Evaluate(true, false, AtLeftEnd, FCoverSlot(), false, Config)), static_cast<int32>(ECoverSwitchVerdict::NoTarget));
	FCoverSwitchConfig Off = Config;
	Off.bEnabled = false;
	TestEqual(TEXT("Switched off"), static_cast<int32>(CoverSwitchRules::Evaluate(true, false, AtLeftEnd, Target, false, Off)), static_cast<int32>(ECoverSwitchVerdict::Disabled));
	// Low cover counts too (the crouched run-in clips).
	const FCoverSlot LowEnd = MakeSwitchSlot(FVector::ZeroVector, 50.f, 300.f, ECoverHeight::LowCover);
	TestEqual(TEXT("Low cover at its end: a switch"), static_cast<int32>(CoverSwitchRules::Evaluate(true, false, LowEnd, Target, false, Config)), static_cast<int32>(ECoverSwitchVerdict::Switch));
	return true;
}

COVER_SWITCH_TEST(FCoverSwitchTargetRangeTest, "TargetRange")
bool FCoverSwitchTargetRangeTest::RunTest(const FString&)
{
	using namespace CoverSwitchTestUtil;
	const FCoverSwitchConfig Config;
	TestEqual(TEXT("Default range 8 m (data-driven: AOperativeCharacter::CoverSwitchMaxRangeCm)"), Config.MaxRangeCm, 800.f);
	const FCoverSlot Here = MakeSwitchSlot(FVector::ZeroVector, 50.f, 300.f);
	auto At = [&](float Distance, const FCoverSwitchConfig& WithConfig)
	{
		return CoverSwitchRules::Evaluate(true, false, Here, MakeSwitchSlot(FVector(0.f, -Distance, 0.f), 50.f, 50.f), false, WithConfig);
	};
	TestEqual(TEXT("5 m: in range"), static_cast<int32>(At(500.f, Config)), static_cast<int32>(ECoverSwitchVerdict::Switch));
	TestEqual(TEXT("7.9 m: in range"), static_cast<int32>(At(790.f, Config)), static_cast<int32>(ECoverSwitchVerdict::Switch));
	TestEqual(TEXT("8.5 m: out of range -> the ordinary cover order"), static_cast<int32>(At(850.f, Config)), static_cast<int32>(ECoverSwitchVerdict::OutOfRange));
	TestEqual(TEXT("1 m: too close -> the ordinary cover order"), static_cast<int32>(At(100.f, Config)), static_cast<int32>(ECoverSwitchVerdict::TooClose));
	FCoverSwitchConfig Short = Config;
	Short.MaxRangeCm = 300.f;
	TestEqual(TEXT("Range tuned down to 3 m: 5 m is out of range"), static_cast<int32>(At(500.f, Short)), static_cast<int32>(ECoverSwitchVerdict::OutOfRange));
	return true;
}

COVER_SWITCH_TEST(FCoverSwitchClipChoiceTest, "ClipChoiceFromGeometry")
bool FCoverSwitchClipChoiceTest::RunTest(const FString&)
{
	using namespace CoverSwitchTestUtil;
	// The target wall faces +X: out in front = +X, his own right (pack _L side) = +Y.
	const FCoverSlot Target = MakeSwitchSlot(FVector::ZeroVector, 60.f, 300.f);
	TestEqual(TEXT("Straight in from the front: jog_fwd_to_cvr"), static_cast<int32>(CoverSwitchRules::ClassifyApproach(Target, FVector(400.f, 50.f, 0.f))), static_cast<int32>(ECoverSwitchApproach::Front));
	TestEqual(TEXT("Along the wall from his right: jog_fwd_L_to_cvr"), static_cast<int32>(CoverSwitchRules::ClassifyApproach(Target, FVector(80.f, 450.f, 0.f))),
		static_cast<int32>(ECoverSwitchApproach::FromPackL));
	TestEqual(TEXT("Along the wall from his left: jog_fwd_R_to_cvr"), static_cast<int32>(CoverSwitchRules::ClassifyApproach(Target, FVector(80.f, -450.f, 0.f))),
		static_cast<int32>(ECoverSwitchApproach::FromPackR));
	TestEqual(TEXT("Round the wall from behind it, his right: from the side"), static_cast<int32>(CoverSwitchRules::ClassifyApproach(Target, FVector(-200.f, 300.f, 0.f))),
		static_cast<int32>(ECoverSwitchApproach::FromPackL));
	TestEqual(TEXT("The 45 deg diagonal counts as the front"), static_cast<int32>(CoverSwitchRules::ClassifyApproach(Target, FVector(300.f, 300.f, 0.f))), static_cast<int32>(ECoverSwitchApproach::Front));
	// Clip names / array indices: _L suffix = facing his own right (CoverFacingRules::ClipIndex).
	TestEqual(TEXT("From his right, facing back the way he came"), CoverSwitchRules::ClipName(ECoverSwitchApproach::FromPackL, ECoverFacing::Right, false),
		FString(TEXT("anim_M4_std_jog_fwd_L_to_cvr_std_idle_L")));
	TestEqual(TEXT("From his left, facing his left"), CoverSwitchRules::ClipName(ECoverSwitchApproach::FromPackR, ECoverFacing::Left, false),
		FString(TEXT("anim_M4_std_jog_fwd_R_to_cvr_std_idle_R")));
	TestEqual(TEXT("Approach and facing differ: L_to_R"), CoverSwitchRules::ClipName(ECoverSwitchApproach::FromPackL, ECoverFacing::Left, false),
		FString(TEXT("anim_M4_std_jog_fwd_L_to_cvr_std_idle_R")));
	TestEqual(TEXT("Low cover: the crouched ending"), CoverSwitchRules::ClipName(ECoverSwitchApproach::FromPackR, ECoverFacing::Right, true),
		FString(TEXT("anim_M4_std_jog_fwd_R_to_cvr_crch_idle_L")));
	TestEqual(TEXT("Straight in, facing his right"), CoverSwitchRules::ClipName(ECoverSwitchApproach::Front, ECoverFacing::Right, false),
		FString(TEXT("anim_M4_std_jog_fwd_to_cvr_std_idle_L")));
	TestEqual(TEXT("Index: front / right = 0"), CoverSwitchRules::ClipArrayIndex(ECoverSwitchApproach::Front, ECoverFacing::Right), 0);
	TestEqual(TEXT("Index: from pack L / right = 2"), CoverSwitchRules::ClipArrayIndex(ECoverSwitchApproach::FromPackL, ECoverFacing::Right), 2);
	TestEqual(TEXT("Index: from pack R / left = 5"), CoverSwitchRules::ClipArrayIndex(ECoverSwitchApproach::FromPackR, ECoverFacing::Left), 5);
	return true;
}

COVER_SWITCH_TEST(FCoverSwitchClipStartTest, "ClipStartFromTracks")
bool FCoverSwitchClipStartTest::RunTest(const FString&)
{
	using namespace CoverSwitchTestUtil;
	// Tracks shaped like the measured pack clips (world XY offsets from the slot; the wall faces +X, his right = +Y).
	TArray<FCoverSwitchTrack> Tracks;
	Tracks.Add(MakeSwitchTrack(FVector2D(465.f, 0.f), 1.2f));  // Front: straight out in front
	Tracks.Add(MakeSwitchTrack(FVector2D(0.f, 520.f), 1.2f));  // From pack L: along the wall from his right
	Tracks.Add(MakeSwitchTrack(FVector2D(0.f, -440.f), 1.2f)); // From pack R: from his left
	TestEqual(TEXT("OffsetAt interpolates"), static_cast<float>(CoverSwitchRules::OffsetAt(Tracks[0], 0.6f).X), 232.5f, 1.f);
	TestEqual(TEXT("OffsetAt clamps past the end"), static_cast<float>(CoverSwitchRules::OffsetAt(Tracks[0], 9.f).X), 0.f, 0.01f);

	// Running in along the wall from his right, 2.1 m out: the pack-L clip from where its body is ~2.1 m out.
	const FCoverSwitchClipStart Side = CoverSwitchRules::ChooseClipStart(Tracks, FVector2D(10.f, 210.f), 0.6f);
	TestEqual(TEXT("From his right: the _L run-in"), Side.Approach, static_cast<int32>(ECoverSwitchApproach::FromPackL));
	TestTrue(TEXT("... its body meets him (< 20 cm)"), Side.MismatchCm < 20.f);
	TestTrue(TEXT("... started mid-clip (the last metres only)"), Side.StartSeconds > 0.5f && Side.StartSeconds < 0.8f);
	TestTrue(TEXT("... the clip offset is where its body stands"), FVector2D::Distance(Side.ClipOffset, FVector2D(0.f, 210.f)) < 20.f);
	// Straight at the wall: the front clip.
	const FCoverSwitchClipStart Front = CoverSwitchRules::ChooseClipStart(Tracks, FVector2D(220.f, -15.f), 0.6f);
	TestEqual(TEXT("Straight in: the front run-in"), Front.Approach, static_cast<int32>(ECoverSwitchApproach::Front));
	// From his left.
	const FCoverSwitchClipStart Left = CoverSwitchRules::ChooseClipStart(Tracks, FVector2D(20.f, -200.f), 0.6f);
	TestEqual(TEXT("From his left: the _R run-in"), Left.Approach, static_cast<int32>(ECoverSwitchApproach::FromPackR));
	// Already right at the slot: at least MinClipLeftSeconds still play (the turn into the wall), however close he is.
	const FCoverSwitchClipStart Close = CoverSwitchRules::ChooseClipStart(Tracks, FVector2D(0.f, 20.f), 0.6f);
	TestTrue(TEXT("Close in: the start leaves >= 0.6 s of the clip"), Close.StartSeconds <= Tracks[Close.Approach].Length() - 0.6f + 0.01f);
	TestTrue(TEXT("... so the body is drawn further out (a glide eases it in)"), Close.MismatchCm > 50.f);
	// Level with the slot, running in from his RIGHT (moving -Y): the pack-R clip's body (coming from his left, moving +Y) may be the
	// nearest point, but it runs the other way - never chosen; the pack-L one is.
	const FCoverSwitchClipStart Level = CoverSwitchRules::ChooseClipStart(Tracks, FVector2D(5.f, -80.f), 0.6f, FVector2D(0.f, -1.f));
	TestEqual(TEXT("Heading: a clip from the other side is never taken"), Level.Approach, static_cast<int32>(ECoverSwitchApproach::FromPackL));
	const FCoverSwitchClipStart NoHeading = CoverSwitchRules::ChooseClipStart(Tracks, FVector2D(5.f, -80.f), 0.6f);
	TestEqual(TEXT("... (without the heading the nearest body would be the pack-R clip's)"), NoHeading.Approach,
		static_cast<int32>(ECoverSwitchApproach::FromPackR));
	TestEqual(TEXT("Running straight at the wall (-X): only the front clip moves that way"),
		CoverSwitchRules::ChooseClipStart(Tracks, FVector2D(150.f, 60.f), 0.6f, FVector2D(-1.f, 0.f)).Approach, static_cast<int32>(ECoverSwitchApproach::Front));
	// A missing clip is skipped; none at all -> no choice.
	TArray<FCoverSwitchTrack> OnlySide;
	OnlySide.SetNum(3);
	OnlySide[1] = Tracks[1];
	TestEqual(TEXT("Only the pack-L clip exists: it is taken"), CoverSwitchRules::ChooseClipStart(OnlySide, FVector2D(220.f, 0.f), 0.6f).Approach,
		static_cast<int32>(ECoverSwitchApproach::FromPackL));
	TArray<FCoverSwitchTrack> None;
	None.SetNum(3);
	TestEqual(TEXT("No clips: no choice"), CoverSwitchRules::ChooseClipStart(None, FVector2D(220.f, 0.f), 0.6f).Approach, static_cast<int32>(INDEX_NONE));
	return true;
}

COVER_SWITCH_TEST(FCoverSwitchClickRulesTest, "ClickRules")
bool FCoverSwitchClickRulesTest::RunTest(const FString&)
{
	// Double-click at a wall edge = the switch; everything else = the ordinary cover order (preview -> confirm, unchanged).
	TestTrue(TEXT("Double-click, switch verdict: the switch"), CoverSwitchRules::IsSwitchConfirm(true, ECoverSwitchVerdict::Switch));
	TestFalse(TEXT("A slow second click: the ordinary cover order"), CoverSwitchRules::IsSwitchConfirm(false, ECoverSwitchVerdict::Switch));
	TestFalse(TEXT("Double-click, not at an edge: the ordinary (running) cover order"), CoverSwitchRules::IsSwitchConfirm(true, ECoverSwitchVerdict::NotAtEdge));
	TestFalse(TEXT("Double-click, out of range: the ordinary cover order"), CoverSwitchRules::IsSwitchConfirm(true, ECoverSwitchVerdict::OutOfRange));
	TestFalse(TEXT("Double-click, the tutorial still locks it: the ordinary cover order"),
		CoverSwitchRules::IsSwitchConfirm(true, ECoverSwitchVerdict::Switch, /*bLocked*/ true));
	// The tutorial lock degrades the switch to the cover order taught before it (as a locked sprint walks).
	TestEqual(TEXT("Command id"), PlayerCommands::CoverSwitch, FName(TEXT("Cover.Switch")));
	TestEqual(TEXT("Locked switch -> Cover.Enter"), TutorialRules::ResolveLockedCommand({ TEXT("Cover.Switch") }, PlayerCommands::CoverSwitch),
		PlayerCommands::CoverEnter);
	TestEqual(TEXT("All cover locked -> refused"), TutorialRules::ResolveLockedCommand({ TEXT("Cover.*") }, PlayerCommands::CoverSwitch), FName(NAME_None));
	TestEqual(TEXT("Cover.* unlocked but the switch kept locked (A1 / A2): the cover order is free"),
		TutorialRules::ResolveLockedCommand({ TEXT("Cover.Switch") }, PlayerCommands::CoverEnter), PlayerCommands::CoverEnter);
	TestEqual(TEXT("Unlocked: the switch itself"), TutorialRules::ResolveLockedCommand({}, PlayerCommands::CoverSwitch), PlayerCommands::CoverSwitch);
	return true;
}

COVER_SWITCH_TEST(FCoverSwitchCornerToCornerTest, "CornerToCornerMirrored")
bool FCoverSwitchCornerToCornerTest::RunTest(const FString&)
{
	// The designer's layout (2026-10-20, second pass): block A (X -300..300, Y 0..100), its front face at Y = 0 facing -Y; block B
	// (X 550..850, Y 150..250) beyond the gap, a little further back. He stands at A's +X end, back to the front face: his right (+RightTangent of
	// the normal (0, -1)) is +X, so he faces Right, towards the gap. Edges of a box footprint: 0 front (-Y), 1 +X side, 2 back (+Y), 3 -X side.
	const FCoverFootprint B = CoverWrapRules::MakeBoxFootprint(FVector2D(700.f, 200.f), FVector2D(1.f, 0.f), 150.f, 50.f);
	const FVector2D From(270.f, -45.f);
	const FVector2D Protected(0.f, -1.f); // his wall's normal: the threat is on the other side
	auto StandOff = [](ECoverFacing Facing) { return Facing == ECoverFacing::Right ? 57.f : 28.f; };
	FCoverSwitchCorner Corner;
	// The click lands on B's side face towards him (edge 3): still B's FRONT face (the protected side), its nearest end.
	TestTrue(TEXT("A corner is found"), CoverSwitchRules::ChooseSwitchCorner(B, From, Protected, 3, 45.f, StandOff, Corner));
	TestEqual(TEXT("... on B's front face (the protected side), not the clicked side face"), Corner.Edge, 0);
	TestTrue(TEXT("... at its end nearest to him (X 550)"), FVector2D::Distance(Corner.Corner, FVector2D(550.f, 150.f)) < 0.1f);
	TestEqual(TEXT("... he faces that corner: Left (-X, back over the gap)"), static_cast<int32>(Corner.Facing), static_cast<int32>(ECoverFacing::Left));
	TestEqual(TEXT("... the mirrored pose of his Right at A"), static_cast<int32>(CoverSwitchRules::MirrorFacing(ECoverFacing::Right)), static_cast<int32>(Corner.Facing));
	TestTrue(TEXT("... the slot: 45 cm off the face, the Left stand-off (28 cm) in from the corner"), FVector2D::Distance(Corner.SlotPoint, FVector2D(578.f, 105.f)) < 0.5f);
	// The diagonal dash: across the gap and forward at once (not perpendicular to B's face).
	const FVector2D Dash = Corner.SlotPoint - From;
	TestTrue(TEXT("The dash runs diagonally (both along and across the faces)"), FMath::Abs(Dash.X) > 200.f && FMath::Abs(Dash.Y) > 100.f);
	// A click on B's far end (front face, edge 0) gives the same corner.
	FCoverSwitchCorner FarClick;
	CoverSwitchRules::ChooseSwitchCorner(B, From, Protected, 0, 45.f, StandOff, FarClick);
	TestTrue(TEXT("A click on B's far end: the same nearest corner"), FarClick.Edge == 0 && FVector2D::Distance(FarClick.SlotPoint, Corner.SlotPoint) < 0.5f);
	// Mirror image (he stands at A's -X end, B beyond it on that side): the other end, facing Right, the Right stand-off.
	const FCoverFootprint BLeft = CoverWrapRules::MakeBoxFootprint(FVector2D(-700.f, 200.f), FVector2D(1.f, 0.f), 150.f, 50.f);
	FCoverSwitchCorner Mirror;
	CoverSwitchRules::ChooseSwitchCorner(BLeft, FVector2D(-272.f, -45.f), Protected, 1, 45.f, StandOff, Mirror);
	TestTrue(TEXT("Mirror layout: B's front face, its +X end, facing Right"), Mirror.Edge == 0 && Mirror.Facing == ECoverFacing::Right
		&& FVector2D::Distance(Mirror.SlotPoint, FVector2D(-607.f, 105.f)) < 0.5f);
	// No face on the protected side (the other object turned 90 deg away from it): the clicked face.
	FCoverSwitchCorner Fallback;
	TestTrue(TEXT("No protected face: the clicked face's nearest end"), CoverSwitchRules::ChooseSwitchCorner(B, From, FVector2D(0.f, 0.f), 3, 45.f, StandOff, Fallback)
		&& Fallback.Edge == 3);
	// The arrival side from a traced slot: the nearest exposed edge within the reach.
	FCoverSlot Arrived;
	Arrived.Height = ECoverHeight::HighCover;
	Arrived.bLeftEdgeExposed = true;
	Arrived.LeftEdgeDistanceCm = 28.f;
	Arrived.bRightEdgeExposed = true;
	Arrived.RightEdgeDistanceCm = 90.f;
	TestEqual(TEXT("Arrival facing: the nearer exposed edge"), static_cast<int32>(CoverSwitchRules::ArrivalFacing(Arrived, 100.f, ECoverFacing::Right)),
		static_cast<int32>(ECoverFacing::Left));
	Arrived.bLeftEdgeExposed = false;
	Arrived.RightEdgeDistanceCm = 150.f;
	TestEqual(TEXT("Arrival facing: no edge within the reach -> the fallback"), static_cast<int32>(CoverSwitchRules::ArrivalFacing(Arrived, 100.f, ECoverFacing::Left)),
		static_cast<int32>(ECoverFacing::Left));
	return true;
}

#undef COVER_SWITCH_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
