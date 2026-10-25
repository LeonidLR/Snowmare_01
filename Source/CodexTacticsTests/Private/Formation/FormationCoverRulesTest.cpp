// CodexTactics.Formation.{Cover,Slide}.* - v2.2: the triad K / P / Z, the rear guard + coverage allocator with fixed looks, the static
// allocation round a standing group, the wall slide (user decisions 2026-10-25 in docs/design/FORMATION_SECTORS.md, contract
// docs/design/FORMATION_CONTRACT.md 11; mirrors Tools/TrainingGround/src/movement/rulesV22.test.ts). UE-only (no Godot reference).

#include "Misc/AutomationTest.h"
#include "Formation/FormationCoverRules.h"
#include "Formation/FormationShapeRules.h"
#include "Formation/FormationWallRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define FORMATION_COVER_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Formation." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace FormationCoverTestUtil
{
	using CER = EFormationRole;
	using CEK = EFormationKind;

	FFormationWall CoverSeg(double AX, double AY, double BX, double BY, float Thickness = 40.f)
	{
		FFormationWall Out;
		Out.A = FVector2D(AX, AY);
		Out.B = FVector2D(BX, BY);
		Out.Thickness = Thickness;
		return Out;
	}

	/** The formation's places at the origin heading +X (Back = how far behind the leader). */
	TArray<FFormationCoverMember> PlacesOf(CEK Formation, int32 Count, const FFormationConfig& Config)
	{
		TArray<FFormationCoverMember> Out;
		for (const FFormationSlot& Slot : FormationShapeRules::Slots(Formation, Count, FVector2D::ZeroVector, 0.f, Config))
		{
			FFormationCoverMember Member;
			Member.Role = Slot.Role;
			Member.Pos = Slot.Position;
			Member.Back = static_cast<float>(-Slot.Position.X);
			Out.Add(Member);
		}
		return Out;
	}

	float LookOf(const TArray<FFormationRoleYaw>& Looks, CER Role)
	{
		const FFormationRoleYaw* Look = Looks.FindByPredicate([Role](const FFormationRoleYaw& Candidate) { return Candidate.Role == Role; });
		return Look ? Look->Yaw : 1e6f;
	}

	/** Largest arc (deg, 1 deg steps) no cone covers and no wall blocks (walls within look.wall_dist, +-45 deg) - the bot's check 10. */
	int32 LargestGap(const TArray<FFormationWall>& Walls, const TArray<FVector2D>& Positions, const TArray<float>& Yaws, const FFormationConfig& Config)
	{
		TArray<double> Blocked;
		for (const FVector2D& Pos : Positions)
		{
			for (const FFormationWall& Wall : Walls)
			{
				const FVector2D Q = FormationMath::ClosestOnSegment(Pos, Wall.A, Wall.B);
				const double D = FVector2D::Distance(Q, Pos);
				if (D - 0.5 * Wall.Thickness <= Config.LookWallDist && D > 1e-6)
				{
					Blocked.Add(FormationMath::YawOfD(Q - Pos));
				}
			}
		}
		TArray<bool> Open;
		for (int32 Index = 0; Index < 360; ++Index)
		{
			const double A = Index - 179;
			bool bOpen = true;
			for (const float Yaw : Yaws)
			{
				bOpen &= FMath::Abs(FormationMath::NormYaw(A - Yaw)) > 0.5 * Config.SightFov;
			}
			for (const double B : Blocked)
			{
				bOpen &= FMath::Abs(FormationMath::NormYaw(A - B)) >= 45.0;
			}
			Open.Add(bOpen);
		}
		if (!Open.Contains(false))
		{
			return 360;
		}
		const int32 Start = Open.IndexOfByKey(false);
		int32 Run = 0;
		int32 Gap = 0;
		for (int32 Step = 1; Step <= 360; ++Step)
		{
			if (Open[(Start + Step) % 360])
			{
				Gap = FMath::Max(Gap, ++Run);
			}
			else
			{
				Run = 0;
			}
		}
		return Gap;
	}
}

FORMATION_COVER_TEST(FFormationCoverRearGuardTest, "Cover.RearGuardHoldsSix")
bool FFormationCoverRearGuardTest::RunTest(const FString&)
{
	using namespace FormationCoverTestUtil;
	const FFormationConfig C;
	for (const int32 Count : { 3, 4 })
	{
		for (const CEK Formation : { CEK::Diamond, CEK::Wedge, CEK::Column, CEK::EchelonRight, CEK::EchelonLeft, CEK::Line, CEK::Blizzard })
		{
			const TOptional<FFormationCoverResult> Halt = FormationCoverRules::Allocate({}, FVector2D::ZeroVector, 0.f, PlacesOf(Formation, Count, C), EFormationMode::Halt, 0.f, C);
			TestTrue(FString::Printf(TEXT("%s of %d: Z is the rear guard at 6, K at 12"), FormationNames::ToString(Formation), Count), Halt.IsSet()
				&& Halt->RearGuard == TOptional<CER>(CER::Rear) && FMath::IsNearlyEqual(FMath::Abs(LookOf(Halt->Looks, CER::Rear)), 180.f, 1e-3f)
				&& FMath::IsNearlyEqual(LookOf(Halt->Looks, CER::Commander), 0.f, 1e-3f));
		}
	}
	TestFalse(TEXT("The pair keeps its own rule (LookYaw)"), FormationCoverRules::Allocate({}, FVector2D::ZeroVector, 0.f, PlacesOf(CEK::Diamond, 2, C), EFormationMode::Walk, 0.f, C).IsSet());
	// A wall on one side shifts him to the open side (wall left -> 5 o'clock) and 5-7 o'clock stays in his cone at every moment.
	const TArray<FFormationWall> WallLeft = { CoverSeg(-3000.0, -200.0, 3000.0, -200.0) };
	const TArray<FFormationCoverMember> Column = PlacesOf(CEK::Column, 4, C);
	const TOptional<FFormationCoverResult> Halt = FormationCoverRules::Allocate(WallLeft, FVector2D::ZeroVector, 0.f, Column, EFormationMode::Halt, 0.f, C);
	TestTrue(TEXT("Wall on the left: the rear guard holds 180 - shift"), Halt.IsSet() && FMath::IsNearlyEqual(LookOf(Halt->Looks, CER::Rear), 180.f - C.RearWallShiftDeg, 1e-3f));
	bool bRearAlwaysInCone = true;
	for (float Time = 0.f; Time < 8.f; Time += 0.1f)
	{
		const TOptional<FFormationCoverResult> Walk = FormationCoverRules::Allocate(WallLeft, FVector2D::ZeroVector, 0.f, Column, EFormationMode::Walk, Time, C);
		bRearAlwaysInCone &= Walk.IsSet() && FormationMath::AngleBetween(LookOf(Walk->Looks, CER::Rear), 180.f) <= 0.5f * C.SightFov - 30.f + 1e-3f;
	}
	TestTrue(TEXT("5-7 o'clock never leaves the rear guard's cone on the move"), bRearAlwaysInCone);
	return true;
}

FORMATION_COVER_TEST(FFormationCoverTriadTest, "Cover.TriadPSweepsBothFlanks")
bool FFormationCoverTriadTest::RunTest(const FString&)
{
	using namespace FormationCoverTestUtil;
	const FFormationConfig C;
	const TArray<FFormationCoverMember> Triad = PlacesOf(CEK::Wedge, 3, C);
	const TOptional<FFormationCoverResult> Halt = FormationCoverRules::Allocate({}, FVector2D::ZeroVector, 0.f, Triad, EFormationMode::Halt, 0.f, C);
	TestTrue(TEXT("Halt: K 12, P 3, Z 6"), Halt.IsSet() && FMath::RoundToInt(LookOf(Halt->Looks, CER::Commander)) == 0 && FMath::RoundToInt(LookOf(Halt->Looks, CER::Right)) == 90
		&& FMath::RoundToInt(FMath::Abs(LookOf(Halt->Looks, CER::Rear))) == 180);
	const TOptional<FFormationCoverResult> Walk0 = FormationCoverRules::Allocate({}, FVector2D::ZeroVector, 0.f, Triad, EFormationMode::Walk, 0.f, C);
	const FFormationCoverShare* Share = Walk0.IsSet() ? Walk0->Shares.FindByPredicate([](const FFormationCoverShare& S) { return S.Role == CER::Right; }) : nullptr;
	TestTrue(TEXT("On the move one share across the front"), Share && Share->From < -60.f && Share->To > 60.f);
	float MaxP = -1e6f;
	float MinP = 1e6f;
	bool bKAgainstP = true;
	for (float Time = 0.f; Time < 12.f; Time += 0.05f)
	{
		const TOptional<FFormationCoverResult> Walk = FormationCoverRules::Allocate({}, FVector2D::ZeroVector, 0.f, Triad, EFormationMode::Walk, Time, C);
		const float P = LookOf(Walk->Looks, CER::Right);
		const float K = LookOf(Walk->Looks, CER::Commander);
		MaxP = FMath::Max(MaxP, P);
		MinP = FMath::Min(MinP, P);
		if (FMath::Abs(P) > 45.f)
		{
			bKAgainstP &= FMath::Sign(K) != FMath::Sign(P) || FMath::Abs(K) < 1e-3f;
		}
	}
	TestTrue(FString::Printf(TEXT("P sweeps 3 -> 12 -> 9 (%.1f .. %.1f)"), MinP, MaxP), FMath::IsNearlyEqual(MaxP, C.CoverageMaxSweep, 0.5f)
		&& FMath::IsNearlyEqual(MinP, -C.CoverageMaxSweep, 0.5f));
	TestTrue(TEXT("K searches against him (never the same flank)"), bKAgainstP);
	return true;
}

FORMATION_COVER_TEST(FFormationCoverDiamondTest, "Cover.DiamondFlanksAndFixedLooks")
bool FFormationCoverDiamondTest::RunTest(const FString&)
{
	using namespace FormationCoverTestUtil;
	const FFormationConfig C;
	const TArray<FFormationCoverMember> Diamond = PlacesOf(CEK::Diamond, 4, C);
	TArray<FVector2D> Positions;
	for (const FFormationCoverMember& Member : Diamond)
	{
		Positions.Add(Member.Pos);
	}
	bool bSides = true;
	int32 Worst = 0;
	for (float Time = 0.f; Time < 10.f; Time += 0.1f)
	{
		const TOptional<FFormationCoverResult> Walk = FormationCoverRules::Allocate({}, FVector2D::ZeroVector, 0.f, Diamond, EFormationMode::Walk, Time, C);
		bSides &= LookOf(Walk->Looks, CER::Right) > 0.f && LookOf(Walk->Looks, CER::Left) < 0.f;
		TArray<float> Yaws;
		for (const FFormationCoverMember& Member : Diamond)
		{
			Yaws.Add(LookOf(Walk->Looks, Member.Role));
		}
		Worst = FMath::Max(Worst, LargestGap({}, Positions, Yaws, C));
	}
	TestTrue(TEXT("Diamond: P the right flank, L the left"), bSides);
	TestTrue(FString::Printf(TEXT("Diamond: the largest open arc %d <= coverage.max_gap_deg"), Worst), Worst <= C.CoverageMaxGapDeg);

	// A corner watcher (fixed look) counts as his cone; the front he leaves goes to the others.
	const TArray<FFormationCoverMember> Triad = PlacesOf(CEK::Wedge, 3, C);
	const TOptional<FFormationCoverResult> Free = FormationCoverRules::Allocate({}, FVector2D::ZeroVector, 0.f, Triad, EFormationMode::Halt, 0.f, C);
	FFormationRoleYaw Watcher;
	Watcher.Role = CER::Commander;
	Watcher.Yaw = -100.f;
	const TOptional<FFormationCoverResult> Fixed = FormationCoverRules::Allocate({}, FVector2D::ZeroVector, 0.f, Triad, EFormationMode::Halt, 0.f, C, { Watcher });
	TestTrue(TEXT("The fixed look is kept"), FMath::IsNearlyEqual(LookOf(Fixed->Looks, CER::Commander), -100.f, 1e-3f));
	TestTrue(TEXT("P takes over another duty"), FMath::RoundToInt(LookOf(Fixed->Looks, CER::Right)) != FMath::RoundToInt(LookOf(Free->Looks, CER::Right)));
	TArray<FVector2D> TriadPositions;
	TArray<float> TriadYaws;
	for (const FFormationCoverMember& Member : Triad)
	{
		TriadPositions.Add(Member.Pos);
		TriadYaws.Add(LookOf(Fixed->Looks, Member.Role));
	}
	TestTrue(TEXT("With the fixed look the open arc stays within the max gap"), LargestGap({}, TriadPositions, TriadYaws, C) <= C.CoverageMaxGapDeg);
	return true;
}

FORMATION_COVER_TEST(FFormationCoverStaticTest, "Cover.StaticGroupClosesTheGap")
bool FFormationCoverStaticTest::RunTest(const FString&)
{
	using namespace FormationCoverTestUtil;
	const FFormationConfig C;
	// Playtest 2026-10-25: three in cover at a wall end, the leader aiming along the wall past its end (yaw 180).
	const FFormationWall Wall = CoverSeg(0.0, 0.0, 3000.0, 0.0);
	const TArray<FFormationSlot> Slots = FormationWallRules::Layout(Wall, FVector2D(400.0, 300.0), 3, 400.f, 2600.f, C);
	FFormationRoleYaw Aim;
	Aim.Role = CER::Commander;
	Aim.Yaw = 180.f;
	TArray<FVector2D> Positions;
	TArray<float> Before;
	for (const FFormationSlot& Slot : Slots)
	{
		Positions.Add(Slot.Position);
		Before.Add(180.f);
	}
	const TArray<FFormationRoleYaw> Looks = FormationCoverRules::AllocateStatic({ Wall }, Slots, { Aim }, { CER::Right, CER::Rear }, C);
	TArray<float> After;
	for (const FFormationSlot& Slot : Slots)
	{
		After.Add(Slot.Role == CER::Commander ? 180.f : LookOf(Looks, Slot.Role));
	}
	const int32 GapBefore = LargestGap({ Wall }, Positions, Before, C);
	const int32 GapAfter = LargestGap({ Wall }, Positions, After, C);
	TestTrue(FString::Printf(TEXT("Before: a wide gap (%d)"), GapBefore), GapBefore > 120);
	TestTrue(FString::Printf(TEXT("After: within the max gap (%d)"), GapAfter), GapAfter <= C.CoverageMaxGapDeg);
	TestTrue(TEXT("One of them covers the rear (along the wall the other way, +X)"), Looks.ContainsByPredicate([](const FFormationRoleYaw& Look) { return FormationMath::AngleBetween(Look.Yaw, 0.f) <= 30.f; }));
	for (const FFormationRoleYaw& Look : Looks)
	{
		TestTrue(FString::Printf(TEXT("%s does not look into the wall (%.1f)"), FormationNames::ToString(Look.Role), Look.Yaw), FormationMath::AngleBetween(Look.Yaw, -90.f) >= 45.f);
	}
	// No fixed look and no walls: the free members face outward from the group, deterministic.
	TArray<FFormationSlot> Group;
	for (const TPair<CER, FVector2D>& Entry : { TPair<CER, FVector2D>(CER::Commander, FVector2D(0.0, 0.0)), TPair<CER, FVector2D>(CER::Right, FVector2D(-200.0, 150.0)),
		TPair<CER, FVector2D>(CER::Rear, FVector2D(-200.0, -150.0)) })
	{
		FFormationSlot Slot;
		Slot.Role = Entry.Key;
		Slot.Position = Entry.Value;
		Group.Add(Slot);
	}
	const TArray<FFormationRoleYaw> A = FormationCoverRules::AllocateStatic({}, Group, {}, { CER::Commander, CER::Right, CER::Rear }, C);
	const TArray<FFormationRoleYaw> B = FormationCoverRules::AllocateStatic({}, Group, {}, { CER::Commander, CER::Right, CER::Rear }, C);
	bool bSame = A.Num() == B.Num() && A.Num() == 3;
	TArray<FVector2D> GroupPositions;
	TArray<float> GroupYaws;
	for (int32 Index = 0; bSame && Index < A.Num(); ++Index)
	{
		bSame &= A[Index].Role == B[Index].Role && A[Index].Yaw == B[Index].Yaw;
		GroupPositions.Add(Group[Index].Position);
		GroupYaws.Add(LookOf(A, Group[Index].Role));
	}
	TestTrue(TEXT("Deterministic, everybody placed"), bSame);
	TestTrue(TEXT("No gap wider than the max"), LargestGap({}, GroupPositions, GroupYaws, C) <= C.CoverageMaxGapDeg);
	return true;
}

FORMATION_COVER_TEST(FFormationSlideTest, "Slide.AlongTheSameWall")
bool FFormationSlideTest::RunTest(const FString&)
{
	using namespace FormationCoverTestUtil;
	const FFormationConfig C;
	const FFormationWall Wall = CoverSeg(0.0, 0.0, 3000.0, 0.0);
	const TArray<FFormationSlot> Start = FormationWallRules::Layout(Wall, FVector2D(1500.0, 300.0), 4, 1500.f, 1500.f, C);
	const FVector2D K0 = Start[0].Position;
	// 6 m along the wall (+X), 1 m off it: a slide.
	const FVector2D Destination(2100.0, 100.0);
	const FFormationSlideResult Plan = FormationWallRules::Slide(Wall, Start, K0, Destination, 0.f, C);
	TestTrue(TEXT("A slide"), Plan.bActive);
	TestTrue(TEXT("The leader's target: along the wall, his offset kept"), FMath::IsNearlyEqual(Plan.LeaderTarget.X, 2100.0, 1e-3) && FMath::IsNearlyEqual(Plan.LeaderTarget.Y, K0.Y, 1e-3));
	// Halfway: everyone moved 3 m along the wall, the offsets kept.
	const FFormationSlideResult Mid = FormationWallRules::Slide(Wall, Start, FVector2D(K0.X + 300.0, K0.Y), Destination, 1.f, C);
	for (int32 Index = 0; Index < Start.Num(); ++Index)
	{
		TestTrue(TEXT("Halfway: moved 3 m as a block"), FMath::IsNearlyEqual(Mid.Targets[Index].Position.X, Start[Index].Position.X + 300.0, 1e-3)
			&& FMath::IsNearlyEqual(Mid.Targets[Index].Position.Y, Start[Index].Position.Y, 1e-3));
	}
	// The order from the leading end (+X) to the trailing end; the ends look along the wall turned outward; K away from the wall.
	TArray<FFormationSlot> ByX = Start;
	ByX.StableSort([](const FFormationSlot& A, const FFormationSlot& B) { return A.Position.X > B.Position.X; });
	TArray<CER> Expected;
	for (const FFormationSlot& Slot : ByX)
	{
		Expected.Add(Slot.Role);
	}
	TestTrue(TEXT("The order is kept from the leading end"), Plan.Order == Expected);
	const auto Look = [&Plan](CER Role)
	{
		const FFormationSlot* Slot = Plan.Targets.FindByPredicate([Role](const FFormationSlot& Candidate) { return Candidate.Role == Role; });
		return Slot ? Slot->Yaw : 1e6f;
	};
	TestTrue(TEXT("The leading end looks ahead, turned outward"), Plan.Lead.IsSet() && FMath::IsNearlyEqual(Look(Plan.Lead.GetValue()), C.SlideLeadOutDeg, 1e-3f));
	TestTrue(TEXT("The trailing end looks back, turned outward: the rear guard"), Plan.RearGuard.IsSet() && Plan.RearGuard.GetValue() == Expected.Last()
		&& FMath::IsNearlyEqual(FMath::Abs(Look(Plan.RearGuard.GetValue())), 180.f - C.SlideLeadOutDeg, 1e-3f));
	TestTrue(TEXT("K away from the wall"), FormationMath::AngleBetween(Look(CER::Commander), 90.f) <= C.ScanAmpLead + 1e-3f);
	for (const FFormationSlot& Target : Plan.Targets)
	{
		TestTrue(FString::Printf(TEXT("%s does not look into the wall"), FormationNames::ToString(Target.Role)), FormationMath::AngleBetween(Target.Yaw, -90.f) >= 45.f);
	}
	// Not a slide: too far off the wall, the other side, beyond its end, or barely along it.
	const TArray<FFormationSlot> Three = FormationWallRules::Layout(Wall, FVector2D(1500.0, 300.0), 3, 1500.f, 1500.f, C);
	const FVector2D T0 = Three[0].Position;
	for (const FVector2D& Dest : { FVector2D(2100.0, 20.0 + C.SlideMaxOffWall + 50.0), FVector2D(2100.0, -150.0), FVector2D(3600.0, 100.0), FVector2D(T0.X + 30.0, 100.0) })
	{
		const FFormationSlideResult Not = FormationWallRules::Slide(Wall, Three, T0, Dest, 0.f, C);
		TestFalse(FString::Printf(TEXT("Not a slide to (%.0f, %.0f)"), Dest.X, Dest.Y), Not.bActive);
		TestTrue(TEXT("Not a slide: the targets are the start slots"), Not.Targets.Num() == Three.Num() && !Not.Lead.IsSet()
			&& FVector2D::Distance(Not.LeaderTarget, T0) < 1e-3);
	}
	return true;
}

#undef FORMATION_COVER_TEST

#endif
