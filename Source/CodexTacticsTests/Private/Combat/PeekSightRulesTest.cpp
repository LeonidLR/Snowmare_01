// CodexTactics.Combat.Sight.Peek* - peek sight (game designer 2026-10-19: "peeking out of cover he sees NOTHING"): the leaned-out
// eye point round high / low cover corners and over a low wall, the limited peek cone (PeekSightRules, CoverRules::CornerLeanPoint).

#include "Combat/PeekSightRules.h"
#include "Combat/SightRules.h"
#include "Misc/AutomationTest.h"
#include "Tactics/CoverRules.h"

namespace PeekSightRulesTestLocal
{
	/**
	 * The test wall: 4 m long along Y (-200..200), 30 cm thick (X 185..215); the operative's slot at X 140 (45 cm off its face),
	 * 50 cm in from its -Y end. The wall's outward normal points at him (-X): his right tangent is -Y, so -Y is his RIGHT corner.
	 */
	FCoverSlot MakeSlot(ECoverHeight Height, float WallTopCm)
	{
		FCoverSlot Slot;
		Slot.WorldLocation = FVector(140.f, -150.f, 0.f);
		Slot.WallPoint = FVector(185.f, -150.f, 90.f);
		Slot.WallNormal = FVector(-1.f, 0.f, 0.f);
		Slot.Height = Height;
		Slot.WallTopCm = WallTopCm;
		Slot.bRightEdgeExposed = true;
		Slot.RightEdgeDistanceCm = 50.f;
		Slot.bLeftEdgeExposed = true;
		Slot.LeftEdgeDistanceCm = 350.f;
		return Slot;
	}

	/** The segment From -> To crosses the test wall's box (X 185..215, Y -200..200) below its top, sampled every 2 cm. */
	bool PeekSightTestSegmentHitsWall(const FVector& From, const FVector& To, float WallTopCm)
	{
		const int32 Steps = FMath::Max(1, FMath::CeilToInt(FVector::Dist(From, To) / 2.f));
		for (int32 Index = 0; Index <= Steps; ++Index)
		{
			const FVector P = FMath::Lerp(From, To, static_cast<float>(Index) / Steps);
			if (P.X >= 185.f && P.X <= 215.f && P.Y >= -200.f && P.Y <= 200.f && P.Z <= WallTopCm)
			{
				return true;
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPeekSightPoseTest, "CodexTactics.Combat.Sight.PeekPose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPeekSightPoseTest::RunTest(const FString& Parameters)
{
	using namespace PeekSightRules;
	const EOperativeStance Stand = EOperativeStance::Standing;
	const EOperativeStance Crouch = EOperativeStance::Crouching;
	TestTrue(TEXT("out of cover: no peek"), DecidePose(ECoverHeight::None, Stand, true, true) == EPeekSightPose::None);
	TestTrue(TEXT("hugging a high wall at its corner (no aim): his own eyes only"), DecidePose(ECoverHeight::HighCover, Stand, false, true) == EPeekSightPose::None);
	TestTrue(TEXT("corner aim / hold / manual peek at a high corner: leaned out"), DecidePose(ECoverHeight::HighCover, Stand, true, true) == EPeekSightPose::CornerLean);
	TestTrue(TEXT("a high wall away from its corner shows nothing"), DecidePose(ECoverHeight::HighCover, Stand, true, false) == EPeekSightPose::None);
	TestTrue(TEXT("crouched corner lean of a low wall (manual peek)"), DecidePose(ECoverHeight::LowCover, Crouch, true, true) == EPeekSightPose::CornerLean);
	TestTrue(TEXT("aiming over a low wall: over the top"), DecidePose(ECoverHeight::LowCover, Crouch, true, false) == EPeekSightPose::OverTop);
	TestTrue(TEXT("crouched behind a low wall, no aim: nothing"), DecidePose(ECoverHeight::LowCover, Crouch, false, false) == EPeekSightPose::None);
	TestTrue(TEXT("prone: never a peek (prone behind a wall sees nothing)"), DecidePose(ECoverHeight::LowCover, EOperativeStance::Prone, true, true) == EPeekSightPose::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPeekSightLeanPointTest, "CodexTactics.Combat.Sight.PeekLeanPoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPeekSightLeanPointTest::RunTest(const FString& Parameters)
{
	using namespace PeekSightRulesTestLocal;
	const FCoverSlot High = MakeSlot(ECoverHeight::HighCover, 250.f);
	const FVector StandEye = High.WorldLocation + FVector(0.f, 0.f, SightRules::EyeHeight(EOperativeStance::Standing));
	// Right corner (-Y): the edge 50 cm away -> leaned max(60, 50 + 20) = 70 cm out, 20 cm past the edge, at his eye height.
	const FVector Right = CoverRules::CornerLeanPoint(High, ECoverFacing::Right, StandEye, 60.f);
	TestTrue(TEXT("right corner: 70 cm along -Y, 20 cm past the edge"), Right.Equals(FVector(140.f, -220.f, 160.f), 0.01f));
	// Left corner (+Y): 350 cm away -> 370 cm along +Y.
	const FVector Left = CoverRules::CornerLeanPoint(High, ECoverFacing::Left, StandEye, 60.f);
	TestTrue(TEXT("left corner: 370 cm along +Y, 20 cm past that edge"), Left.Equals(FVector(140.f, 220.f, 160.f), 0.01f));
	// A corner right at his shoulder: never less than the minimum lean.
	FCoverSlot AtEdge = High;
	AtEdge.RightEdgeDistanceCm = 10.f;
	TestTrue(TEXT("edge at his shoulder: the 60 cm minimum lean"), CoverRules::CornerLeanPoint(AtEdge, ECoverFacing::Right, StandEye, 60.f).Equals(FVector(140.f, -210.f, 160.f), 0.01f));
	// The facing's corner closed: the only exposed one.
	FCoverSlot OnlyLeft = High;
	OnlyLeft.bRightEdgeExposed = false;
	TestTrue(TEXT("facing a closed corner: leans round the exposed one"), CoverRules::CornerLeanPoint(OnlyLeft, ECoverFacing::Right, StandEye, 60.f).Equals(Left, 0.01f));
	// Low cover crouched lean: the same round the end, at the crouched eyes (95 cm).
	const FCoverSlot Low = MakeSlot(ECoverHeight::LowCover, 120.f);
	const FVector CrouchEye = Low.WorldLocation + FVector(0.f, 0.f, SightRules::EyeHeight(EOperativeStance::Crouching));
	TestTrue(TEXT("low cover: the crouched lean round the end at 95 cm"), CoverRules::CornerLeanPoint(Low, ECoverFacing::Right, CrouchEye, 60.f).Equals(FVector(140.f, -220.f, 95.f), 0.01f));
	// The corner shot's muzzle uses the same point (no drift between the shot and the sight).
	TestTrue(TEXT("same as the corner muzzle offset"), Right.Equals(CoverRules::CornerMuzzle(High, ECoverFacing::Right, StandEye, 70.f), 0.01f));

	// Sight lines: an enemy round the right corner, 4 m behind the wall, 1.5 m past its end (X 615, Y -350, profile 150).
	const FVector Enemy(615.f, -350.f, 150.f);
	TestTrue(TEXT("hugging (eyes at the capsule): the wall hides the enemy round the corner"), PeekSightTestSegmentHitsWall(StandEye, Enemy, 250.f));
	TestFalse(TEXT("leaned out: the line passes the corner"), PeekSightTestSegmentHitsWall(Right, Enemy, 250.f));
	// Low wall 120 cm: crouched (95) behind it the line to a man 6 m behind the wall is blocked; over the top it is clear.
	FPeekSightConfig Config;
	const FVector Feet = Low.WorldLocation;
	const FVector Across(515.f, -100.f, 150.f);
	TestTrue(TEXT("crouched behind a 1.2 m wall: blocked"), PeekSightTestSegmentHitsWall(CrouchEye, Across, 120.f));
	const FVector OverTop = PeekSightRules::OverTopEyePoint(Config, Feet, Low.WallNormal, 95.f, 120.f);
	TestTrue(TEXT("over the top: eyes 135 cm, leaned 25 cm to the wall"), OverTop.Equals(FVector(165.f, -150.f, 135.f), 0.01f));
	TestFalse(TEXT("over the top: the line clears the 1.2 m wall"), PeekSightTestSegmentHitsWall(OverTop, Across, 120.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPeekSightOverTopTest, "CodexTactics.Combat.Sight.PeekOverTopHeight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPeekSightOverTopTest::RunTest(const FString& Parameters)
{
	using namespace PeekSightRules;
	const FPeekSightConfig Config;
	TestEqual(TEXT("a 60 cm barricade: the crouched eyes already see over it"), OverTopEyeHeight(Config, 95.f, 60.f), 95.f);
	TestEqual(TEXT("a 1.2 m wall: wall top + 15"), OverTopEyeHeight(Config, 95.f, 120.f), 135.f);
	TestEqual(TEXT("a 1.75 m 'low' wall: capped at 170"), OverTopEyeHeight(Config, 95.f, 175.f), 170.f);
	TestEqual(TEXT("standing eyes stay when higher"), OverTopEyeHeight(Config, 160.f, 120.f), 160.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPeekSightConeTest, "CodexTactics.Combat.Sight.PeekCone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPeekSightConeTest::RunTest(const FString& Parameters)
{
	using namespace PeekSightRules;
	const FPeekSightConfig Config;
	TestEqual(TEXT("data: 90 degree cone"), Config.HalfAngleDeg * 2.f, 90.f);
	TestEqual(TEXT("data: 18 m"), Config.RangeCm, 1800.f);
	const FVector Eye(0.f, 0.f, 160.f);
	const FVector Aim(1.f, 0.f, 0.f);
	auto At = [](float YawDeg, float DistanceCm) { return FRotator(0.f, YawDeg, 0.f).Vector() * DistanceCm + FVector(0.f, 0.f, 150.f); };
	TestTrue(TEXT("straight ahead 10 m"), IsInCone(Config, Eye, Aim, At(0.f, 1000.f)));
	TestTrue(TEXT("40 deg off, 15 m"), IsInCone(Config, Eye, Aim, At(40.f, 1500.f)));
	TestTrue(TEXT("-44 deg off, 17.9 m"), IsInCone(Config, Eye, Aim, At(-44.f, 1790.f)));
	TestFalse(TEXT("50 deg off: outside the cone"), IsInCone(Config, Eye, Aim, At(50.f, 1000.f)));
	TestFalse(TEXT("behind him"), IsInCone(Config, Eye, Aim, At(180.f, 500.f)));
	TestFalse(TEXT("19 m: beyond the peek range"), IsInCone(Config, Eye, Aim, At(0.f, 1900.f)));
	FPeekSightConfig Off;
	Off.bEnabled = false;
	TestFalse(TEXT("disabled: nothing"), IsInCone(Off, Eye, Aim, At(0.f, 1000.f)));
	// The upper-body twist turns the cone: 50 deg off is inside once he twists 20 deg to it.
	const FVector Twisted = TwistedDirection(Aim, 20.f);
	TestTrue(TEXT("twist 20 deg: unit planar"), FMath::IsNearlyEqual(static_cast<float>(Twisted.Size()), 1.f, 0.001f) && FMath::IsNearlyZero(static_cast<float>(Twisted.Z)));
	TestTrue(TEXT("twisted 20 deg: 50 deg off is inside"), IsInCone(Config, Eye, Twisted, At(50.f, 1000.f)));
	TestFalse(TEXT("twisted 20 deg: -30 deg off is outside"), IsInCone(Config, Eye, Twisted, At(-30.f, 1000.f)));
	return true;
}
