// CodexTactics.Formation.Runtime.WallHug.* - the squad hugging a wall in the formation wall layout (the game designer's play-test bug
// 2026-10-27 in the Demo 01 lake yard: "at one wall it hugs, at the other it doesn't - and look at the distance between the squad and the
// wall"): the wall the formation's traces read (FormationRuntime::NearestWallHit - never an overlap-only trigger box) and the wall pose's
// glide onto the exact slot (FormationRuntime::WallSlotGlide), whose face offset is the stand-off of one operative in cover.
// In-game check: FormationWallHugSmoke (the real lake yard). UE-only (no Godot reference).

#include "Misc/AutomationTest.h"
#include "Formation/FormationRuntimeRules.h"
#include "Formation/FormationTuning.h"
#include "Formation/FormationWallRules.h"
#include "Tactics/CoverTraceRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define FORMATION_WALL_HUG_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Formation.Runtime.WallHug." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace FormationWallHugTestUtil
{
	FormationRuntime::FWallTraceHit MakeWallHugHit(float Time, bool bBlocksPawn, bool bPawn = false)
	{
		FormationRuntime::FWallTraceHit Hit;
		Hit.Time = Time;
		Hit.bBlocksPawn = bBlocksPawn;
		Hit.bPawn = bPawn;
		return Hit;
	}
}

FORMATION_WALL_HUG_TEST(FFormationWallHugTraceTest, "TriggerBoxIsNoWall")
bool FFormationWallHugTraceTest::RunTest(const FString&)
{
	using FormationWallHugTestUtil::MakeWallHugHit;
	// From inside the yard's TutorialTriggerVolume (OverlapAllDynamic): the box at distance 0, a team-mate on the way, the building's wall,
	// a crate behind it. The old single object trace returned the box (the wall read AT the clicked slot).
	const TArray<FormationRuntime::FWallTraceHit> Yard = { MakeWallHugHit(0.f, false), MakeWallHugHit(0.1f, true, true), MakeWallHugHit(0.6f, true),
		MakeWallHugHit(0.4f, true) };
	TestEqual(TEXT("the nearest object that blocks pawns and is no pawn - the wall at 0.4, not the trigger box at 0 / the pawn at 0.1"),
		FormationRuntime::NearestWallHit(Yard), 3);
	TestEqual(TEXT("only overlap boxes / pawns along the ray: no wall"),
		FormationRuntime::NearestWallHit({ MakeWallHugHit(0.f, false), MakeWallHugHit(0.3f, true, true) }), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("nothing along the ray: no wall"), FormationRuntime::NearestWallHit({}), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("a wall alone is the wall"), FormationRuntime::NearestWallHit({ MakeWallHugHit(0.25f, true) }), 0);
	return true;
}

FORMATION_WALL_HUG_TEST(FFormationWallHugGlideTest, "GlideOntoTheSlot")
bool FFormationWallHugGlideTest::RunTest(const FString&)
{
	// The face along x = 0, the slot 45 cm out; he stopped 25 cm short of it (the formation's stop radius) - a hand-span off the wall.
	const FVector2D Slot(45.0, 100.0);
	FVector2D Here(70.0, 100.0);
	const float Dt = 1.f / 60.f;
	int32 Frames = 0;
	double Closest = TNumericLimits<double>::Max();
	while (const TOptional<FVector2D> Move = FormationRuntime::WallSlotGlide(Here, Slot, 60.f, Dt))
	{
		TestTrue(TEXT("a step never exceeds the glide speed"), Move->Size() <= FormationRuntime::WallSlotGlideSpeedCm * Dt + 1e-3);
		Here += Move.GetValue();
		Closest = FMath::Min(Closest, FVector2D::Distance(Here, Slot));
		TestTrue(TEXT("never past the slot (never into the wall)"), Here.X >= Slot.X - 1e-3);
		if (++Frames > 100)
		{
			break;
		}
	}
	TestTrue(FString::Printf(TEXT("on the slot (%.2f cm off it after %d frames)"), FVector2D::Distance(Here, Slot), Frames),
		FVector2D::Distance(Here, Slot) <= FormationRuntime::WallSlotGlideDoneCm);
	TestTrue(TEXT("25 cm at 150 cm/s: within ~0.2 s"), Frames >= 9 && Frames <= 12);
	TestFalse(TEXT("farther than MaxCm: not at his slot, the walk brings him (no glide)"), FormationRuntime::WallSlotGlide(FVector2D(200.0, 100.0), Slot, 60.f, Dt).IsSet());
	TestFalse(TEXT("on the slot: nothing to do"), FormationRuntime::WallSlotGlide(FVector2D(45.5, 100.0), Slot, 60.f, Dt).IsSet());
	TestFalse(TEXT("no time step: no move"), FormationRuntime::WallSlotGlide(FVector2D(70.0, 100.0), Slot, 60.f, 0.f).IsSet());
	return true;
}

FORMATION_WALL_HUG_TEST(FFormationWallHugFaceOffsetTest, "FaceOffsetIsTheSoloCoverStandOff")
bool FFormationWallHugFaceOffsetTest::RunTest(const FString&)
{
	// The user: the squad tight to the wall like one operative hugging cover. The wall layout's face offset (wall.face_offset) is the solo
	// cover slot's (FCoverTraceConfig::SlotOffsetCm) - by default and as tuned in formation_config.json.
	const float Solo = FCoverTraceConfig().SlotOffsetCm;
	TestEqual(TEXT("FFormationConfig default wall.face_offset = the solo cover stand-off"), FFormationConfig().WallFaceOffset, Solo, 0.01f);
	TestEqual(TEXT("the tuned wall.face_offset = the solo cover stand-off"), FormationTuning::Get().WallFaceOffset, Solo, 0.01f);
	// The face line x = 500 seen from the origin (FormationRuntime::WallAlong: thickness 0): every place Solo off the face.
	const FFormationWall Wall = FormationRuntime::WallAlong(FVector2D(500.0, 0.0), 180.f, 400.f, 400.f);
	const TArray<FFormationSlot> Slots = FormationWallRules::Layout(Wall, FVector2D(455.0, 0.0), 3, 400.f, 400.f, FormationTuning::Get());
	TestEqual(TEXT("3 places"), Slots.Num(), 3);
	for (const FFormationSlot& Slot : Slots)
	{
		TestEqual(FString::Printf(TEXT("%s stands the solo stand-off off the face"), FormationNames::ToString(Slot.Role)), static_cast<float>(500.0 - Slot.Position.X), Solo, 0.5f);
	}
	return true;
}

FORMATION_WALL_HUG_TEST(FFormationWallHugSlideClearTest, "SlideLeadMenClearOfTheLeader")
bool FFormationWallHugSlideClearTest::RunTest(const FString&)
{
	// A 12 m wall along y = 0 (the squad on +y), the triad hugging it (the face offset off its line), a slide 5 m to -x.
	const FFormationConfig Config;
	FFormationWall Wall;
	Wall.A = FVector2D(-600.0, 0.0);
	Wall.B = FVector2D(600.0, 0.0);
	Wall.Thickness = 0.f;
	const TArray<FFormationSlot> Start = FormationWallRules::Layout(Wall, FVector2D(100.0, Config.WallFaceOffset), 3, 600.f, 600.f, Config);
	TestEqual(TEXT("3 places, K first"), Start.Num(), 3);
	if (Start.Num() != 3 || Start[0].Role != EFormationRole::Commander)
	{
		return false;
	}
	const FVector2D K0 = Start[0].Position;
	const FVector2D Destination(-400.0, 100.0);
	const FVector2D Leader = K0 + FVector2D(-120.0, 0.0); // he walked 1.2 m of it
	const FFormationSlideResult Base = FormationWallRules::Slide(Wall, Start, Leader, Destination, 0.f, Config);
	TestTrue(TEXT("the move along the wall is a slide"), Base.bActive);
	const FVector2D Probe = FormationRuntime::SlideLeaderAhead(Leader, K0, Base.LeaderTarget);
	TestEqual(TEXT("the probe is the leader 40 cm further along his travel"), FVector2D::Distance(Probe, Leader), static_cast<double>(FormationRuntime::SlideAheadClearCm), 0.01);
	TestTrue(TEXT("... towards his target"), FVector2D::Distance(Probe, Base.LeaderTarget) < FVector2D::Distance(Leader, Base.LeaderTarget));
	TestTrue(TEXT("never past his target"), FVector2D::Distance(FormationRuntime::SlideLeaderAhead(Base.LeaderTarget - FVector2D(-10.0, 0.0), K0, Base.LeaderTarget),
		Base.LeaderTarget) < 0.01);
	const FFormationSlideResult Ahead = FormationWallRules::Slide(Wall, Start, Probe, Destination, 0.f, Config);
	const TArray<FFormationSlot> Places = FormationRuntime::SlideTargetsClearOfLeader(Start, Base.Targets, Ahead.Targets, Base.LeaderTarget);
	TestEqual(TEXT("one place per member"), Places.Num(), 3);
	for (int32 Index = 0; Index < Places.Num() && Index < Start.Num(); ++Index)
	{
		const double StartAlong = Start[Index].Position.X - K0.X; // the travel is -x: ahead = smaller x
		if (Start[Index].Role == EFormationRole::Commander || StartAlong >= 0.0)
		{
			TestTrue(FString::Printf(TEXT("%s (the leader / behind him) keeps the slide's place"), FormationNames::ToString(Start[Index].Role)),
				Places[Index].Position.Equals(Base.Targets[Index].Position, 0.01));
		}
		else
		{
			TestEqual(FString::Printf(TEXT("%s (ahead of him) stays spacing + %.0f cm ahead of the walking leader"), FormationNames::ToString(Start[Index].Role),
				FormationRuntime::SlideAheadClearCm), Leader.X - Places[Index].Position.X, static_cast<double>(Config.WallSpacing + FormationRuntime::SlideAheadClearCm), 0.5);
			TestEqual(TEXT("... on the wall line"), Places[Index].Position.Y, Start[Index].Position.Y, 0.01);
		}
	}
	// At the end of the slide everyone is at his final place (the lead margin is capped by the leader's target).
	const FFormationSlideResult EndBase = FormationWallRules::Slide(Wall, Start, Base.LeaderTarget, Destination, 0.f, Config);
	const FFormationSlideResult EndAhead = FormationWallRules::Slide(Wall, Start, FormationRuntime::SlideLeaderAhead(Base.LeaderTarget, K0, Base.LeaderTarget),
		Destination, 0.f, Config);
	const TArray<FFormationSlot> EndPlaces = FormationRuntime::SlideTargetsClearOfLeader(Start, EndBase.Targets, EndAhead.Targets, Base.LeaderTarget);
	for (int32 Index = 0; Index < EndPlaces.Num() && Index < EndBase.Targets.Num(); ++Index)
	{
		TestTrue(FString::Printf(TEXT("%s ends at the slide's final place"), FormationNames::ToString(EndPlaces[Index].Role)),
			EndPlaces[Index].Position.Equals(EndBase.Targets[Index].Position, 0.01));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
