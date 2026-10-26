// CodexTactics.Formation.WallEndCoverage.* - v2.4 the wall layout at a wall END (FormationWallRules::EndLooks / OpenArcCoverage; user bug report
// 2026-10-26: three operatives crouched at the end face of a thin free-standing wall left large uncovered zones on both sides of the end;
// docs/design/FORMATION_CONTRACT.md 14; mirrors Tools/TrainingGround/src/movement/rulesV24.test.ts). UE-only (no Godot reference).
// The thin wall runs along -X and ends at the origin; the leader's wall slot is 45 cm off its end face on the axis (outward normal +X).

#include "Misc/AutomationTest.h"
#include "Formation/FormationWallGroupRules.h"
#include "Formation/FormationWallRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define FORMATION_WALL_END_COVERAGE_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Formation.WallEndCoverage." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace FormationWallEndCoverageTestUtil
{
	FFormationWall Wall(const FVector2D& A, const FVector2D& B, float Thickness)
	{
		FFormationWall Out;
		Out.A = A;
		Out.B = B;
		Out.Thickness = Thickness;
		return Out;
	}

	TArray<FFormationSlot> LayoutAt(const FFormationWall& InWall, const FVector2D& Click, int32 Count, const FFormationConfig& Config)
	{
		float RoomLeft = 0.f;
		float RoomRight = 0.f;
		FormationWallRules::WallRoom(InWall, Click, RoomLeft, RoomRight);
		return FormationWallRules::Layout(InWall, Click, Count, RoomLeft, RoomRight, Config);
	}

	double Coverage(const TArray<FFormationWall>& Walls, const TArray<FFormationSlot>& Slots, const FFormationConfig& Config)
	{
		TArray<FVector2D> Positions;
		TArray<float> Looks;
		for (const FFormationSlot& Slot : Slots)
		{
			Positions.Add(Slot.Position);
			Looks.Add(Slot.Yaw);
		}
		return FormationWallRules::OpenArcCoverage(Walls, Positions, Looks, Config.SightFov, Config.WallEndProbeDist).Pct;
	}
}

FORMATION_WALL_END_COVERAGE_TEST(FFormationWallEndCoverageBugTest, "FaceLooksLeaveTheEndsFlanksOpen")
bool FFormationWallEndCoverageBugTest::RunTest(const FString&)
{
	using namespace FormationWallEndCoverageTestUtil;
	const FFormationConfig Config;
	const FFormationWall Thin = Wall(FVector2D(-1500.0, 0.0), FVector2D(0.0, 0.0), 40.f);
	for (int32 Count = 3; Count <= 4; ++Count)
	{
		const TArray<FFormationSlot> Slots = LayoutAt(Thin, FVector2D(65.0, 0.0), Count, Config);
		TestEqual(FString::Printf(TEXT("%d: K looks away from the end face"), Count), Slots[0].Yaw, 0.f, 0.01f);
		const double Pct = Coverage({ Thin }, Slots, Config);
		TestTrue(FString::Printf(TEXT("%d: the face looks hold only %.1f %% of the open arc round the end (the user's report)"), Count, Pct), Pct < 75.0);
	}
	return true;
}

FORMATION_WALL_END_COVERAGE_TEST(FFormationWallEndCoverageSpreadTest, "SpreadCoversBothFlanks")
bool FFormationWallEndCoverageSpreadTest::RunTest(const FString&)
{
	using namespace FormationWallEndCoverageTestUtil;
	const FFormationConfig Config;
	for (const float Thickness : { 40.f, 20.f })
	{
		const FFormationWall Thin = Wall(FVector2D(-1500.0, 0.0), FVector2D(0.0, 0.0), Thickness);
		const FVector2D Click(0.5 * Thickness + Config.WallFaceOffset, 0.0);
		for (int32 Count = 3; Count <= 4; ++Count)
		{
			const TArray<FFormationSlot> Slots = LayoutAt(Thin, Click, Count, Config);
			const FFormationWallEndResult Result = FormationWallRules::EndLooks({ Thin }, Slots, Config);
			const FString Case = FString::Printf(TEXT("%.0f cm wall, %d"), Thickness, Count);
			TestTrue(Case + TEXT(": spread"), Result.bSpread);
			TestTrue(FString::Printf(TEXT("%s: %.1f -> %.1f %% (>= 95)"), *Case, Result.BasePct, Result.Pct), Result.BasePct < Config.WallEndCoverPct && Result.Pct >= 95.0);
			TestEqual(Case + TEXT(": K keeps his look"), Result.Slots[0].Yaw, Slots[0].Yaw, 0.01f);
			for (int32 Index = 0; Index < Slots.Num(); ++Index)
			{
				TestTrue(Case + TEXT(": positions and roles unchanged"), Result.Slots[Index].Role == Slots[Index].Role
					&& FVector2D::Distance(Result.Slots[Index].Position, Slots[Index].Position) < 0.01);
				TestFalse(FString::Printf(TEXT("%s: slot %d (look %.0f) does not face the wall"), *Case, Index, Result.Slots[Index].Yaw),
					FormationWallGroup::FacesWall({ Thin }, Result.Slots[Index].Position, Result.Slots[Index].Yaw));
			}
			// Each flank along a face (5 m behind the end, 1.5 m off the face) is inside the cone of the man on that side.
			for (const double Side : { 1.0, -1.0 })
			{
				const FVector2D Target(-500.0, 150.0 * Side);
				bool bHeld = false;
				for (const FFormationSlot& Slot : Result.Slots)
				{
					bHeld |= FMath::Sign(Slot.Position.Y) == Side
						&& FormationMath::AngleBetween(Slot.Yaw, FormationMath::YawOf(Target - Slot.Position)) <= 0.5f * Config.SightFov;
				}
				TestTrue(FString::Printf(TEXT("%s: the %s flank along the face is held"), *Case, Side > 0.0 ? TEXT("+Y") : TEXT("-Y")), bHeld);
			}
		}
	}
	return true;
}

FORMATION_WALL_END_COVERAGE_TEST(FFormationWallEndCoverageFaceTest, "FaceAndPairUnchanged")
bool FFormationWallEndCoverageFaceTest::RunTest(const FString&)
{
	using namespace FormationWallEndCoverageTestUtil;
	const FFormationConfig Config;
	const FFormationWall Face = Wall(FVector2D(0.0, -1500.0), FVector2D(0.0, 1500.0), 40.f);
	const FFormationWall Side = Wall(FVector2D(0.0, -150.0), FVector2D(1500.0, -150.0), 40.f);
	for (int32 Count = 2; Count <= 4; ++Count)
	{
		for (const bool bCorner : { false, true })
		{
			const TArray<FFormationSlot> Slots = LayoutAt(Face, FVector2D(65.0, 0.0), Count, Config);
			TArray<FFormationWall> Walls = { Face };
			if (bCorner)
			{
				Walls.Add(Side);
			}
			const FFormationWallEndResult Result = FormationWallRules::EndLooks(Walls, Slots, Config);
			TestFalse(FString::Printf(TEXT("%d%s: a face keeps the user's wall rule"), Count, bCorner ? TEXT(" (inner corner)") : TEXT("")), Result.bSpread);
			for (int32 Index = 0; Index < Slots.Num(); ++Index)
			{
				TestEqual(TEXT("look unchanged"), Result.Slots[Index].Yaw, Slots[Index].Yaw, 0.001f);
			}
		}
	}
	// The pair at the end face: its looks are a user decision (K 7:30 / partner 4:30) - EndLooks needs 3+.
	const FFormationWall Thin = Wall(FVector2D(-1500.0, 0.0), FVector2D(0.0, 0.0), 40.f);
	TestFalse(TEXT("the pair keeps its looks"), FormationWallRules::EndLooks({ Thin }, LayoutAt(Thin, FVector2D(65.0, 0.0), 2, Config), Config).bSpread);
	return true;
}

FORMATION_WALL_END_COVERAGE_TEST(FFormationWallEndCoverageCoverageTest, "OpenArcCoverage")
bool FFormationWallEndCoverageCoverageTest::RunTest(const FString&)
{
	using namespace FormationWallEndCoverageTestUtil;
	const FFormationConfig Config;
	const FFormationArcCoverage One = FormationWallRules::OpenArcCoverage({}, { FVector2D::ZeroVector }, { 0.f }, 140.f, 600.f);
	TestEqual(TEXT("an open field: 180 ring points open"), One.Open, 180);
	TestEqual(TEXT("one 140 deg cone holds 71 of them (-70..70 every 2 deg)"), One.Covered, 71);
	const FFormationArcCoverage Three = FormationWallRules::OpenArcCoverage({}, { FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector },
		{ 0.f, 120.f, -120.f }, 140.f, 600.f);
	TestEqual(TEXT("three cones 120 apart hold everything"), Three.Pct, 100.0, 1e-9);
	// A zero-thickness box (the runtime's traced faces): nothing behind it is open.
	const TArray<FFormationWall> Box = { Wall(FVector2D(-100.0, -100.0), FVector2D(100.0, -100.0), 0.f), Wall(FVector2D(100.0, -100.0), FVector2D(100.0, 100.0), 0.f),
		Wall(FVector2D(100.0, 100.0), FVector2D(-100.0, 100.0), 0.f), Wall(FVector2D(-100.0, 100.0), FVector2D(-100.0, -100.0), 0.f) };
	const FFormationArcCoverage Boxed = FormationWallRules::OpenArcCoverage(Box, { FVector2D::ZeroVector }, { 0.f }, 140.f, 600.f);
	TestEqual(TEXT("boxed in: nothing open"), Boxed.Open, 0);
	TestEqual(TEXT("boxed in: 100 % (nothing to hold)"), Boxed.Pct, 100.0, 1e-9);
	return true;
}

FORMATION_WALL_END_COVERAGE_TEST(FFormationWallEndCoverageFixedTest, "FixedLooksKept")
bool FFormationWallEndCoverageFixedTest::RunTest(const FString&)
{
	using namespace FormationWallEndCoverageTestUtil;
	// The corner stack (formation/v24-corner-peek) fixes more looks than K's: they stay, only the others are spread round them.
	const FFormationConfig Config;
	const FFormationWall Thin = Wall(FVector2D(-1500.0, 0.0), FVector2D(0.0, 0.0), 40.f);
	const TArray<FFormationSlot> Slots = LayoutAt(Thin, FVector2D(65.0, 0.0), 4, Config);
	const FFormationWallEndResult Result = FormationWallRules::EndLooks({ Thin }, Slots, Config, { Slots[1].Role });
	TestTrue(TEXT("spread round the fixed looks"), Result.bSpread && Result.Pct > Result.BasePct);
	TestEqual(TEXT("K keeps his look"), Result.Slots[0].Yaw, Slots[0].Yaw, 0.01f);
	TestEqual(TEXT("the fixed role keeps his look"), Result.Slots[1].Yaw, Slots[1].Yaw, 0.01f);
	const FFormationWallEndResult AllFixed = FormationWallRules::EndLooks({ Thin }, Slots, Config, { Slots[1].Role, Slots[2].Role, Slots[3].Role });
	TestFalse(TEXT("everyone fixed: nothing to spread"), AllFixed.bSpread);
	return true;
}

#undef FORMATION_WALL_END_COVERAGE_TEST

#endif
