// CodexTactics.Formation.WallSectors.* - rules v2.7: the wall layout's looks at a wall with two ends (FormationWallSectorRules; the game
// designer's bug report 2026-11-02 item 5 on L_Gen_Depot_1: the squad crouched along a short wall segment, the right-end man looked round
// his corner, the LEFT-end man looked forward into K's sector and the sector round the left corner stayed open). UE-only (no Godot reference).
// The wall runs along X through the origin, the squad on its +Y side (outward normal yaw 90); the tests name the ends "-X" / "+X" (a look
// turned towards -X has a positive offset from the normal: UE yaw grows from +X towards +Y).

#include "Misc/AutomationTest.h"
#include "Formation/FormationWallRules.h"
#include "Formation/FormationWallSectorRules.h"
#include "Data/SquadWallTuning.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

#define FORMATION_WALL_SECTORS_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Formation.WallSectors." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace FormationWallSectorsTestUtil
{
	constexpr float Normal = 90.f;

	FFormationWall SectorTestWall(double FromX, double ToX, double Y = 0.0, float Thickness = 40.f)
	{
		FFormationWall Out;
		Out.A = FVector2D(FromX, Y);
		Out.B = FVector2D(ToX, Y);
		Out.Thickness = Thickness;
		return Out;
	}

	FFormationSlot SectorTestSlot(EFormationRole Role, double X, float Yaw, const FFormationConfig& Config)
	{
		FFormationSlot Out;
		Out.Role = Role;
		Out.Position = FVector2D(X, 20.0 + Config.WallFaceOffset);
		Out.Yaw = Yaw;
		return Out;
	}

	/** The look's offset from the outward normal, deg (positive = turned towards -X here: UE yaw grows from +X towards +Y). */
	double SectorTestRel(const FFormationSlot& Each)
	{
		return FormationMath::NormYaw(static_cast<double>(Each.Yaw) - Normal);
	}

	const FFormationSlot* SectorTestEnd(const TArray<FFormationSlot>& Slots, bool bLowX)
	{
		const FFormationSlot* Best = nullptr;
		for (const FFormationSlot& Each : Slots)
		{
			if (!Best || (bLowX ? Each.Position.X < Best->Position.X : Each.Position.X > Best->Position.X))
			{
				Best = &Each;
			}
		}
		return Best;
	}

	/** The end man looks round HIS corner: more than 90 deg off the normal, turned to his own end (the -X end: a positive offset). */
	bool SectorTestRoundOwnCorner(const FFormationSlot& Each, bool bLowX)
	{
		const double Offset = SectorTestRel(Each);
		return FMath::Abs(Offset) > 90.0 && (bLowX ? Offset > 0.0 : Offset < 0.0);
	}
}

FORMATION_WALL_SECTORS_TEST(FFormationWallSectorsBugTest, "LeftEndManMirrorsTheRightEndMan")
bool FFormationWallSectorsBugTest::RunTest(const FString&)
{
	using namespace FormationWallSectorsTestUtil;
	// The user's screenshot (log 2026-11-02 14:47:53, outward yaw 90, room 0 / 50): K out, the +X end man round his corner (-22 = N - 112),
	// the -X end man (the Engineer) kept his face look 150 = N + 60 - inside K's cone, the sector round the -X corner open.
	const FFormationConfig Config;
	const TArray<FFormationWall> Walls = { SectorTestWall(-120.0, 120.0) };
	const TArray<FFormationSlot> Bug = { SectorTestSlot(EFormationRole::Commander, 0.0, Normal, Config), SectorTestSlot(EFormationRole::Right, 100.0, -22.f, Config),
		SectorTestSlot(EFormationRole::Rear, -100.0, 150.f, Config) };
	TestTrue(TEXT("the bug: a duplicated sector while the -X corner is open"), FormationWallSectorRules::CountRedundantWhileOpen(Walls, Bug, Config) > 0);
	const FFormationWallSectorResult Result = FormationWallSectorRules::Balance(Walls, Bug, Normal, Config);
	TestTrue(TEXT("changed"), Result.bChanged);
	TestEqual(TEXT("K keeps his look"), Result.Slots[0].Yaw, Normal, 0.01f);
	TestEqual(TEXT("the +X end man keeps his look"), Result.Slots[1].Yaw, -22.f, 0.01f);
	TestTrue(TEXT("the -X end man takes the mirror"), Result.Fixes[2] == EFormationWallSectorFix::Mirror);
	TestEqual(TEXT("mirror = 2 N - (-22) = 202 = -158"), static_cast<double>(Result.Slots[2].Yaw), -158.0, 0.01);
	TestTrue(TEXT("round his own corner"), SectorTestRoundOwnCorner(Result.Slots[2], true));
	TestTrue(FString::Printf(TEXT("more held (%.1f -> %.1f %%)"), Result.BasePct, Result.Pct), Result.Pct > Result.BasePct);
	TestEqual(TEXT("no duplicated sector left"), FormationWallSectorRules::CountRedundantWhileOpen(Walls, Result.Slots, Config), 0);
	// Mirrored the other way round: the same answer, both ends alike.
	TArray<FFormationSlot> Flipped = Bug;
	Flipped[1] = SectorTestSlot(EFormationRole::Right, 100.0, 30.f, Config);   // +X end: face look N - 60 (forward-right)
	Flipped[2] = SectorTestSlot(EFormationRole::Rear, -100.0, -158.f, Config); // -X end: round his corner N + 112
	const FFormationWallSectorResult Other = FormationWallSectorRules::Balance(Walls, Flipped, Normal, Config);
	TestTrue(TEXT("the +X end man takes the mirror"), Other.Fixes[1] == EFormationWallSectorFix::Mirror);
	TestEqual(TEXT("mirror of -158 about 90 = -22"), static_cast<double>(Other.Slots[1].Yaw), -22.0, 0.01);
	return true;
}

FORMATION_WALL_SECTORS_TEST(FFormationWallSectorsShortWallTest, "ShortWallBothEndsWatched")
bool FFormationWallSectorsShortWallTest::RunTest(const FString&)
{
	using namespace FormationWallSectorsTestUtil;
	// The whole chain on a short free-standing wall (2.4 m, both ends free) and in a gap between two collinear walls: Layout -> EndLooks
	// -> Balance; both end men look round their own corners, nothing duplicated while something is open.
	const FFormationConfig Config;
	struct FCase
	{
		const TCHAR* Name;
		TArray<FFormationWall> Walls;
	};
	const TArray<FCase> Cases = {
		{ TEXT("short wall"), { SectorTestWall(-120.0, 120.0) } },
		{ TEXT("short wall, 1 m"), { SectorTestWall(-50.0, 50.0) } },
		{ TEXT("a segment between two 4 m gaps"), { SectorTestWall(-120.0, 120.0), SectorTestWall(-1400.0, -520.0), SectorTestWall(520.0, 1400.0) } },
	};
	for (const FCase& Case : Cases)
	{
		const FVector2D Click(0.0, 20.0 + Config.WallFaceOffset);
		float RoomLeft = 0.f;
		float RoomRight = 0.f;
		FormationWallRules::WallRoom(Case.Walls[0], Click, RoomLeft, RoomRight);
		const TArray<FFormationSlot> Layout = FormationWallRules::Layout(Case.Walls[0], Click, 3, RoomLeft, RoomRight, Config);
		const FFormationWallEndResult End = FormationWallRules::EndLooks(Case.Walls, Layout, Config);
		const FFormationWallSectorResult Result = FormationWallSectorRules::Balance(Case.Walls, End.Slots, Normal, Config);
		const FFormationSlot* Low = SectorTestEnd(Result.Slots, true);
		const FFormationSlot* High = SectorTestEnd(Result.Slots, false);
		AddInfo(FString::Printf(TEXT("%s: layout %.0f / %.0f / %.0f -> end %.0f / %.0f / %.0f -> balanced %.0f / %.0f / %.0f (%.1f -> %.1f %%)"), Case.Name,
			Layout[0].Yaw, Layout[1].Yaw, Layout[2].Yaw, End.Slots[0].Yaw, End.Slots[1].Yaw, End.Slots[2].Yaw, Result.Slots[0].Yaw, Result.Slots[1].Yaw,
			Result.Slots[2].Yaw, Result.BasePct, Result.Pct));
		TestEqual(FString::Printf(TEXT("%s: K looks out"), Case.Name), Result.Slots[0].Yaw, Normal, 0.01f);
		TestTrue(FString::Printf(TEXT("%s: the -X end man round his corner (%.0f)"), Case.Name, Low->Yaw), SectorTestRoundOwnCorner(*Low, true));
		TestTrue(FString::Printf(TEXT("%s: the +X end man round his corner (%.0f)"), Case.Name, High->Yaw), SectorTestRoundOwnCorner(*High, false));
		TestTrue(FString::Printf(TEXT("%s: both ends alike (%.0f vs %.0f off the normal)"), Case.Name, SectorTestRel(*Low), SectorTestRel(*High)),
			FMath::Abs(FMath::Abs(SectorTestRel(*Low)) - FMath::Abs(SectorTestRel(*High))) <= 15.0);
		TestEqual(FString::Printf(TEXT("%s: no duplicated sector while one is open"), Case.Name),
			FormationWallSectorRules::CountRedundantWhileOpen(Case.Walls, Result.Slots, Config), 0);
		TestTrue(FString::Printf(TEXT("%s: never less held (%.1f -> %.1f %%)"), Case.Name, Result.BasePct, Result.Pct), Result.Pct >= Result.BasePct - 1e-9);
	}
	return true;
}

FORMATION_WALL_SECTORS_TEST(FFormationWallSectorsFaceTest, "FaceKeepsTheUsersLooks")
bool FFormationWallSectorsFaceTest::RunTest(const FString&)
{
	using namespace FormationWallSectorsTestUtil;
	// A long face (the open half-plane): the user's wall rule L 8 / K 6 / P 4 (and the 4-man layout) stays; an inner corner too.
	const FFormationConfig Config;
	for (int32 Count = 3; Count <= 4; ++Count)
	{
		for (const bool bCorner : { false, true })
		{
			TArray<FFormationWall> Walls = { SectorTestWall(-1500.0, 1500.0) };
			if (bCorner)
			{
				FFormationWall Side;
				Side.A = FVector2D(-350.0, 0.0);
				Side.B = FVector2D(-350.0, 1500.0);
				Side.Thickness = 40.f;
				Walls.Add(Side);
			}
			const TArray<FFormationSlot> Layout = FormationWallRules::Layout(Walls[0], FVector2D(0.0, 20.0 + Config.WallFaceOffset), Count, 1400.f, 1400.f, Config);
			const FFormationWallSectorResult Result = FormationWallSectorRules::Balance(Walls, Layout, Normal, Config);
			TestFalse(FString::Printf(TEXT("%d%s: unchanged"), Count, bCorner ? TEXT(" (inner corner)") : TEXT("")), Result.bChanged);
		}
	}
	return true;
}

FORMATION_WALL_SECTORS_TEST(FFormationWallSectorsFixedTest, "FixedAndPairKept")
bool FFormationWallSectorsFixedTest::RunTest(const FString&)
{
	using namespace FormationWallSectorsTestUtil;
	const FFormationConfig Config;
	const TArray<FFormationWall> Walls = { SectorTestWall(-120.0, 120.0) };
	const TArray<FFormationSlot> Bug = { SectorTestSlot(EFormationRole::Commander, 0.0, Normal, Config), SectorTestSlot(EFormationRole::Right, 100.0, -22.f, Config),
		SectorTestSlot(EFormationRole::Rear, -100.0, 150.f, Config) };
	// The corner stack's fixed roles (the point man, the trailing end) keep their looks.
	const FFormationWallSectorResult Fixed = FormationWallSectorRules::Balance(Walls, Bug, Normal, Config, { EFormationRole::Rear });
	TestEqual(TEXT("a fixed role keeps his look"), Fixed.Slots[2].Yaw, 150.f, 0.01f);
	TestEqual(TEXT("a fixed role is no fault"), FormationWallSectorRules::CountRedundantWhileOpen(Walls, Bug, Config, { EFormationRole::Rear }), 0);
	// The pair (a user decision): unchanged.
	const TArray<FFormationSlot> Pair = { Bug[0], Bug[2] };
	TestFalse(TEXT("the pair keeps its looks"), FormationWallSectorRules::Balance(Walls, Pair, Normal, Config).bChanged);
	return true;
}

FORMATION_WALL_SECTORS_TEST(FFormationWallSectorsCoverageTest, "RingCoverage")
bool FFormationWallSectorsCoverageTest::RunTest(const FString&)
{
	const FFormationConfig Config;
	FFormationSlot One;
	One.Yaw = 0.f;
	const FFormationArcCoverage Open = FormationWallSectorRules::RingCoverage({}, { One }, Config);
	TestEqual(TEXT("an open field: 2 x 180 ring points open"), Open.Open, 360);
	TestEqual(TEXT("one 140 deg cone holds 2 x 71"), Open.Covered, 142);
	return true;
}

FORMATION_WALL_SECTORS_TEST(FFormationWallSectorsFarCornerTest, "CornerFartherAlongKeepsTheFace")
bool FFormationWallSectorsFarCornerTest::RunTest(const FString&)
{
	using namespace FormationWallSectorsTestUtil;
	// FormationLabSmoke R_F_deadend (2026-11-03): a wall going on 3-4 m past the end man (room 400 / 400) is a face for him - the corner far along
	// is no reason to turn round; only a corner beside him (CornerReachCm) is.
	const FFormationConfig Config;
	for (int32 Count = 3; Count <= 4; ++Count)
	{
		const TArray<FFormationWall> Walls = { SectorTestWall(-400.0, 400.0) };
		const TArray<FFormationSlot> Layout = FormationWallRules::Layout(Walls[0], FVector2D(0.0, 20.0 + Config.WallFaceOffset), Count, 400.f, 400.f, Config);
		const FFormationWallSectorResult Result = FormationWallSectorRules::Balance(Walls, Layout, Normal, Config);
		for (int32 Index = 0; Index < Result.Fixes.Num(); ++Index)
		{
			TestTrue(FString::Printf(TEXT("%d: %s not turned round a corner 3 m away"), Count, FormationNames::ToString(Layout[Index].Role)),
				Result.Fixes[Index] != EFormationWallSectorFix::Corner && Result.Fixes[Index] != EFormationWallSectorFix::Mirror);
		}
	}
	return true;
}

FORMATION_WALL_SECTORS_TEST(FFormationWallSectorsTuningTest, "TuningFromJson")
bool FFormationWallSectorsTuningTest::RunTest(const FString&)
{
	const FSquadWallTuning Defaults;
	TestEqual(TEXT("corner look default 112"), Defaults.WallCornerLookDeg, 112.f);
	TestEqual(TEXT("follower cover radius default 6 m"), Defaults.FollowerCoverRadiusCm, 600.f);
	TSharedPtr<FJsonObject> Json;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TEXT(R"({"wall_corner_look_deg": 120, "follower_cover_radius_m": 4.5, "follower_cover_side_deg": 400})")), Json);
	const FSquadWallTuning Read = SquadWallTuning::FromJson(*Json);
	TestEqual(TEXT("corner look read"), Read.WallCornerLookDeg, 120.f);
	TestEqual(TEXT("metres -> cm"), Read.FollowerCoverRadiusCm, 450.f);
	TestEqual(TEXT("clamped to 180"), Read.FollowerCoverSideDeg, 180.f);
	// The corner look is a parameter of the rule: a wider one turns the end men farther round.
	const FFormationConfig Config;
	FFormationWall Short;
	Short.A = FVector2D(-120.0, 0.0);
	Short.B = FVector2D(120.0, 0.0);
	Short.Thickness = 40.f;
	FFormationSlot K;
	K.Position = FVector2D(0.0, 65.0);
	K.Yaw = 90.f;
	FFormationSlot P = K;
	P.Role = EFormationRole::Right;
	P.Position.X = 100.0;
	P.Yaw = 30.f;
	FFormationSlot Z = K;
	Z.Role = EFormationRole::Rear;
	Z.Position.X = -100.0;
	Z.Yaw = 150.f;
	const FFormationWallSectorResult Wide = FormationWallSectorRules::Balance({ Short }, { K, P, Z }, 90.f, Config, {}, 125.f);
	TestEqual(TEXT("+X end at N - 125"), static_cast<double>(Wide.Slots[1].Yaw), FormationMath::NormYaw(90.0 - 125.0), 0.01);
	TestEqual(TEXT("-X end mirrors it at N + 125"), static_cast<double>(Wide.Slots[2].Yaw), FormationMath::NormYaw(90.0 + 125.0), 0.01);
	return true;
}

#undef FORMATION_WALL_SECTORS_TEST

#endif
