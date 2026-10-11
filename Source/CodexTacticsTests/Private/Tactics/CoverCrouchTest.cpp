// CodexTactics.Tactics.Cover.* — the crouched cover (user request 2026-10-07; UE-only, no Godot reference): the pack's
// crouch clips keep the standing side convention (measured: crch _L = his own right, the walks move by their suffix the
// same way), the crouched corner stand-offs, and the low cover fired over the top anywhere along it.

#include "Misc/AutomationTest.h"
#include "Tactics/CoverFacingRules.h"
#include "Tactics/CoverTraceRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define COVER_CROUCH_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Tactics.Cover." TestPath, EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace CoverCrouchTest
{
	/** Wall along Y at X = -45 cm, normal +X, his right = +Y. */
	FCoverSlot MakeSlot(ECoverHeight Height, bool bRightEdge, float RightCm)
	{
		FCoverSlot Slot;
		Slot.WorldLocation = FVector::ZeroVector;
		Slot.WallPoint = FVector(-45.f, 0.f, 40.f);
		Slot.WallNormal = FVector(1.f, 0.f, 0.f);
		Slot.Height = Height;
		Slot.bRightEdgeExposed = bRightEdge;
		Slot.RightEdgeDistanceCm = RightCm;
		return Slot;
	}
}

COVER_CROUCH_TEST(FCoverCrouchClipSidesTest, "CrouchClipSides")
bool FCoverCrouchClipSidesTest::RunTest(const FString&)
{
	using namespace CoverFacingRules;
	// Measured 2026-10-07 (crouch/): cvr_crch_idle_L faces his right (+0.91 Y), fire_idle_L / fire_L / idle_to_fire_L step
	// 73 cm to his right, fire_to_idle_L comes back from there, walk_fwd_loop_L / walk_bwd_loop_L travel to his right
	// (facing right / left), crch_idle_fwd_to_cvr_crch_idle_L ends in idle_L, the stance switches keep the side. One index
	// rule for both stances.
	TestEqual(TEXT("crouched, facing his right: pack _L"), ClipIndex(ECoverFacing::Right), 0);
	TestEqual(TEXT("crouched, facing his left: pack _R"), ClipIndex(ECoverFacing::Left), 1);
	TestEqual(TEXT("threat right, crouch-shimmy right: crch walk_fwd_loop_L"), ShimmyClipIndex(ECoverFacing::Right, true), 0);
	TestEqual(TEXT("threat right, crouch-shimmy left: crch walk_bwd_loop_R"), ShimmyClipIndex(ECoverFacing::Right, false), 1);
	TestEqual(TEXT("threat left, crouch-shimmy left: crch walk_fwd_loop_R"), ShimmyClipIndex(ECoverFacing::Left, true), 1);
	TestEqual(TEXT("threat left, crouch-shimmy right: crch walk_bwd_loop_L"), ShimmyClipIndex(ECoverFacing::Left, false), 0);
	// Crouched corner stand-offs = the crouched peek's pelvis step-out (77 / 61 cm) minus 12 cm.
	const FCoverFacingConfig Config;
	TestEqual(TEXT("crouched at his right corner (crch _L, 77 cm out)"), CornerStandOff(ECoverFacing::Right, true, Config), 65.f, 0.01f);
	TestEqual(TEXT("crouched at his left corner (crch _R, 61 cm out)"), CornerStandOff(ECoverFacing::Left, true, Config), 49.f, 0.01f);
	float Shift = 0.f;
	TestTrue(TEXT("crouched, right edge 1.2 m: steps to the crouched stand-off"),
		ShouldSnapToCorner(CoverCrouchTest::MakeSlot(ECoverHeight::HighCover, true, 120.f), ECoverFacing::Right, Shift, Config, true));
	TestEqual(TEXT("... 55 cm"), Shift, 55.f, 0.01f);
	return true;
}

COVER_CROUCH_TEST(FCoverLowCoverPopUpShotTest, "LowCoverPopUpShot")
bool FCoverLowCoverPopUpShotTest::RunTest(const FString&)
{
	using namespace CoverFacingRules;
	// A low cover is fired over the top (the crouched fire stance holds the rifle at ~80-90 cm, over a 60 cm barricade)
	// anywhere along it; a high wall only round its exposed edge.
	TestTrue(TEXT("low cover, middle: a firing spot"), IsFiringSpot(ECoverHeight::LowCover, false));
	TestTrue(TEXT("high wall at the corner: a firing spot"), IsFiringSpot(ECoverHeight::HighCover, true));
	TestFalse(TEXT("high wall, middle: no firing spot"), IsFiringSpot(ECoverHeight::HighCover, false));
	TestFalse(TEXT("no cover"), IsFiringSpot(ECoverHeight::None, true));
	// Targets beyond the low wall count as behind it (the pop-up shot); targets on his side get the open shot.
	const FCoverSlot Low = CoverCrouchTest::MakeSlot(ECoverHeight::LowCover, false, 0.f);
	TestTrue(TEXT("beyond the barricade, straight across: pop-up shot"), ShouldCornerShot(Low, FVector(-700.f, 0.f, 0.f)));
	TestTrue(TEXT("beyond the barricade, far to the side: pop-up shot"), ShouldCornerShot(Low, FVector(-300.f, 900.f, 0.f)));
	TestFalse(TEXT("on his side of the barricade: open shot"), ShouldCornerShot(Low, FVector(600.f, 100.f, 0.f)));
	TestFalse(TEXT("beside its end on his side: open shot (no corner lean at a low cover)"), ShouldCornerShot(Low, FVector(30.f, 400.f, 0.f)));
	return true;
}

COVER_CROUCH_TEST(FCoverShimmyReverseDirectionTest, "ShimmyReverseDirection")
bool FCoverShimmyReverseDirectionTest::RunTest(const FString&)
{
	using namespace CoverTraceRules;
	// User report 2026-10-11: a reversing shimmy order got the OLD direction when its target was still on the old side of
	// the slot the running shimmy started from. The direction is measured from where the body is.
	const FCoverSlot Slot = CoverCrouchTest::MakeSlot(ECoverHeight::HighCover, false, 0.f); // slot at the origin, his right = +Y (normal +X)
	const FVector RightTangent = Slot.RightTangent();
	const FVector Body = Slot.WorldLocation - RightTangent * 300.f; // 3 m to the left of the slot, shimmying left
	const FVector Target = Slot.WorldLocation - RightTangent * 100.f; // 1 m left of the slot = 2 m to his RIGHT from the body
	TestTrue(TEXT("from the slot the target is on the left (the old, wrong measurement)"), AlongWallDistance(Slot, Target) < 0.f);
	TestTrue(TEXT("from the body it is on the right: the shimmy reverses"), AlongWallDistanceFrom(Slot, Body, Target) > 0.f);
	TestTrue(TEXT("... 200 cm"), FMath::IsNearlyEqual(AlongWallDistanceFrom(Slot, Body, Target), 200.f, 0.01f));
	TestTrue(TEXT("a target further left keeps going left"), AlongWallDistanceFrom(Slot, Body, Slot.WorldLocation - RightTangent * 450.f) < 0.f);
	return true;
}

#undef COVER_CROUCH_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
