// CodexTactics.Formation.Sector.* - look sectors (Formation/FormationSectorRules; design docs/design/FORMATION_SECTORS.md "Roles and
// sectors", mirrors Tools/TrainingGround/src/movement/rules.test.ts). UE-only (no Godot reference). Positive yaw offsets = right.

#include "Misc/AutomationTest.h"
#include "Formation/FormationSectorRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define FORMATION_SECTOR_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Formation.Sector." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace FormationSectorTestUtil
{
	/** Relative look of Role over 12 s of patrol (diamond, walk), sampled every 10 ms, -180..180. */
	TArray<float> Sweep(EFormationRole Role, int32 MemberCount, float Heading = 0.f)
	{
		const FFormationConfig Config;
		TArray<float> Out;
		for (int32 Step = 0; Step < 1200; ++Step)
		{
			const float Yaw = FormationSectorRules::LookYaw(Role, EFormationKind::Diamond, EFormationMode::Walk, MemberCount, Step * 0.01f, Heading, TOptional<float>(), Config);
			Out.Add(FRotator::NormalizeAxis(Yaw - Heading));
		}
		return Out;
	}

	float MinOf(const TArray<float>& Values)
	{
		return FMath::Min(Values);
	}

	float MaxOf(const TArray<float>& Values)
	{
		return FMath::Max(Values);
	}
}

FORMATION_SECTOR_TEST(FFormationSectorPatrolTest, "PatrolScans")
bool FFormationSectorPatrolTest::RunTest(const FString&)
{
	using namespace FormationSectorTestUtil;
	const FFormationConfig Config;
	const TArray<float> P = Sweep(EFormationRole::Right, 4);
	const TArray<float> L = Sweep(EFormationRole::Left, 4);
	const TArray<float> K = Sweep(EFormationRole::Commander, 4);
	const TArray<float> Z = Sweep(EFormationRole::Rear, 4);
	TestTrue(TEXT("P scans 12 -> 3"), FMath::IsNearlyEqual(MinOf(P), 0.f, 0.5f) && FMath::IsNearlyEqual(MaxOf(P), 90.f, 0.5f));
	TestTrue(TEXT("L scans 9 -> 12"), FMath::IsNearlyEqual(MinOf(L), -90.f, 0.5f) && FMath::IsNearlyEqual(MaxOf(L), 0.f, 0.5f));
	TestTrue(TEXT("K: search round 12 within scan.amp_lead"), FMath::Max(FMath::Abs(MinOf(K)), FMath::Abs(MaxOf(K))) <= Config.ScanAmpLead + 1e-3f);
	TestTrue(TEXT("K searches 11 - 1 o'clock (v1.2: scan.amp_lead 30)"), FMath::IsNearlyEqual(MinOf(K), -30.f, 0.5f) && FMath::IsNearlyEqual(MaxOf(K), 30.f, 0.5f));
	TestTrue(TEXT("K does search (not frozen on 12)"), MaxOf(K) - MinOf(K) > Config.ScanAmpLead);
	bool bZAlwaysSix = true;
	for (const float Value : Z)
	{
		bZAlwaysSix &= FMath::IsNearlyEqual(FMath::Abs(Value), 180.f, 1e-3f);
	}
	TestTrue(TEXT("Z looks at 6 all the time (walks backwards)"), bZAlwaysSix);
	// Counter-phase: when P is out at 3, L is out at 9.
	const float Quarter = Config.ScanPeriod / 4.f;
	TestTrue(TEXT("P at 3 at a quarter period"), FMath::IsNearlyEqual(FormationSectorRules::LookYaw(EFormationRole::Right, EFormationKind::Diamond,
		EFormationMode::Walk, 4, Quarter, 0.f, TOptional<float>(), Config), 90.f, 1e-3f));
	TestTrue(TEXT("L at 9 at the same time"), FMath::IsNearlyEqual(FormationSectorRules::LookYaw(EFormationRole::Left, EFormationKind::Diamond,
		EFormationMode::Walk, 4, Quarter, 0.f, TOptional<float>(), Config), -90.f, 1e-3f));
	// The look follows the leader's heading.
	TestTrue(TEXT("Heading 40: Z at -140"), FMath::IsNearlyEqual(FormationSectorRules::LookYaw(EFormationRole::Rear, EFormationKind::Diamond,
		EFormationMode::Walk, 4, 1.f, 40.f, TOptional<float>(), Config), -140.f, 1e-3f));
	return true;
}

FORMATION_SECTOR_TEST(FFormationSectorDwellTest, "DwellOnPoints")
bool FFormationSectorDwellTest::RunTest(const FString&)
{
	using namespace FormationSectorTestUtil;
	// Dwell: the flanker spends more of the scan near the sector's ends (within 15 deg of 12 or 3) than a plain sine would.
	const TArray<float> P = Sweep(EFormationRole::Right, 4);
	int32 NearEnds = 0;
	for (const float Value : P)
	{
		NearEnds += (Value < 15.f || Value > 75.f) ? 1 : 0;
	}
	const float DwellShare = static_cast<float>(NearEnds) / static_cast<float>(P.Num());
	// A plain sine (exp 1) is within 15 deg of the ends asin-wise ~ 2 * (1 - acos(2/3)/(pi/2)) / 2 ~ 46 % of the time.
	FFormationConfig Plain;
	Plain.ScanDwellExp = 1.f;
	int32 PlainNearEnds = 0;
	for (int32 Step = 0; Step < 1200; ++Step)
	{
		const float Value = FormationSectorRules::LookYaw(EFormationRole::Right, EFormationKind::Diamond, EFormationMode::Walk, 4, Step * 0.01f, 0.f, TOptional<float>(), Plain);
		PlainNearEnds += (Value < 15.f || Value > 75.f) ? 1 : 0;
	}
	TestTrue(FString::Printf(TEXT("Dwell holds near the ends longer (%.0f %% vs plain %.0f %%)"), 100.f * DwellShare,
		100.f * PlainNearEnds / 1200.f), NearEnds > PlainNearEnds);
	TestTrue(TEXT("Dwell(1) = 1, Dwell(-1) = -1, Dwell(0) = 0"), FMath::IsNearlyEqual(FormationSectorRules::Dwell(1.f, 0.45f), 1.f)
		&& FMath::IsNearlyEqual(FormationSectorRules::Dwell(-1.f, 0.45f), -1.f) && FormationSectorRules::Dwell(0.f, 0.45f) == 0.f);
	return true;
}

FORMATION_SECTOR_TEST(FFormationSectorPairTest, "PairPartnerSweepsThreeSixNine")
bool FFormationSectorPairTest::RunTest(const FString&)
{
	using namespace FormationSectorTestUtil;
	// User decision: the pair's partner sweeps 3 -> 6 -> 9, through 6, never through 12.
	TArray<float> Z = Sweep(EFormationRole::Rear, 2);
	for (float& Value : Z)
	{
		Value = Value < 0.f ? Value + 360.f : Value;
	}
	TestTrue(TEXT("Sweep reaches 3"), FMath::IsNearlyEqual(MinOf(Z), 90.f, 0.5f));
	TestTrue(TEXT("Sweep reaches 9"), FMath::IsNearlyEqual(MaxOf(Z), 270.f, 0.5f));
	bool bThroughSix = true;
	for (const float Value : Z)
	{
		bThroughSix &= Value >= 90.f - 1e-3f && Value <= 270.f + 1e-3f;
	}
	TestTrue(TEXT("Only through 6 (never the front half)"), bThroughSix);
	const FFormationConfig Config;
	TestTrue(TEXT("Pair halt: the partner holds 6"), FMath::IsNearlyEqual(FMath::Abs(FormationSectorRules::LookYaw(EFormationRole::Rear,
		EFormationKind::Diamond, EFormationMode::Halt, 2, 0.7f, 0.f, TOptional<float>(), Config)), 180.f, 1e-3f));
	TestTrue(TEXT("Pair leader still searches round 12"), FMath::Abs(FormationSectorRules::LookYaw(EFormationRole::Commander, EFormationKind::Diamond,
		EFormationMode::Walk, 2, 0.f, 0.f, TOptional<float>(), Config)) <= Config.ScanAmpLead + 1e-3f);
	return true;
}

FORMATION_SECTOR_TEST(FFormationSectorRunHaltTest, "RunAllTwelveHaltSectorCentres")
bool FFormationSectorRunHaltTest::RunTest(const FString&)
{
	const FFormationConfig Config;
	const EFormationRole Roles[] = { EFormationRole::Commander, EFormationRole::Right, EFormationRole::Left, EFormationRole::Rear };
	const EFormationKind Kinds[] = { EFormationKind::Diamond, EFormationKind::Column, EFormationKind::Line, EFormationKind::EchelonRight,
		EFormationKind::Hedgehog, EFormationKind::Blizzard, EFormationKind::Wall };
	for (const EFormationKind Kind : Kinds)
	{
		for (const EFormationRole Role : Roles)
		{
			TestTrue(FString::Printf(TEXT("Run: %s in %s faces 12"), FormationNames::ToString(Role), FormationNames::ToString(Kind)),
				FMath::IsNearlyEqual(FormationSectorRules::LookYaw(Role, Kind, EFormationMode::Run, 4, 1.7f, 30.f, TOptional<float>(), Config), 30.f, 1e-3f));
			// v1.2: the menu's "All forward" - walking, everybody on 12 whatever the formation.
			TestTrue(FString::Printf(TEXT("All forward: %s in %s faces 12"), FormationNames::ToString(Role), FormationNames::ToString(Kind)),
				FMath::IsNearlyEqual(FormationSectorRules::LookYaw(Role, Kind, EFormationMode::Forward, 4, 1.7f, 30.f, TOptional<float>(460.f), Config), 30.f, 1e-3f));
		}
	}
	// Halt: K 12, P 2, L 10, Z 6.
	TestTrue(TEXT("Halt K 12"), FMath::IsNearlyEqual(FormationSectorRules::LookYaw(EFormationRole::Commander, EFormationKind::Diamond, EFormationMode::Halt, 4, 1.7f, 0.f, TOptional<float>(), Config), 0.f));
	TestTrue(TEXT("Halt P 2"), FMath::IsNearlyEqual(FormationSectorRules::LookYaw(EFormationRole::Right, EFormationKind::Diamond, EFormationMode::Halt, 4, 1.7f, 0.f, TOptional<float>(), Config),
		FormationMath::ClockToYaw(2.f, 0.f)));
	TestTrue(TEXT("Halt L 10"), FMath::IsNearlyEqual(FormationSectorRules::LookYaw(EFormationRole::Left, EFormationKind::Diamond, EFormationMode::Halt, 4, 1.7f, 0.f, TOptional<float>(), Config),
		FormationMath::ClockToYaw(10.f, 0.f)));
	TestTrue(TEXT("Halt Z 6"), FMath::IsNearlyEqual(FMath::Abs(FormationSectorRules::LookYaw(EFormationRole::Rear, EFormationKind::Diamond, EFormationMode::Halt, 4, 1.7f, 0.f, TOptional<float>(), Config)), 180.f));
	return true;
}

FORMATION_SECTOR_TEST(FFormationSectorShapesTest, "ColumnLineEchelonSectors")
bool FFormationSectorShapesTest::RunTest(const FString&)
{
	const FFormationConfig Config;
	const auto Look = [&Config](EFormationRole Role, EFormationKind Kind, int32 Count)
	{
		return FormationSectorRules::LookYaw(Role, Kind, EFormationMode::Walk, Count, 0.f, 0.f, TOptional<float>(), Config);
	};
	// Column herringbone: K 0, P +70, L -70, Z 180.
	TestTrue(TEXT("Column sectors"), FMath::IsNearlyEqual(Look(EFormationRole::Commander, EFormationKind::Column, 4), 0.f)
		&& FMath::IsNearlyEqual(Look(EFormationRole::Right, EFormationKind::Column, 4), 70.f)
		&& FMath::IsNearlyEqual(Look(EFormationRole::Left, EFormationKind::Column, 4), -70.f)
		&& FMath::IsNearlyEqual(FMath::Abs(Look(EFormationRole::Rear, EFormationKind::Column, 4)), 180.f));
	// Echelon right: every gun on the open right side, growing back to the rear-right (order K, L, P, Z).
	TestTrue(TEXT("Echelon right 10 / 45 / 80 / 150 (v1.1: the rear sector 150 keeps 5-7 watched)"), FMath::IsNearlyEqual(Look(EFormationRole::Commander, EFormationKind::EchelonRight, 4), 10.f)
		&& FMath::IsNearlyEqual(Look(EFormationRole::Left, EFormationKind::EchelonRight, 4), 45.f)
		&& FMath::IsNearlyEqual(Look(EFormationRole::Right, EFormationKind::EchelonRight, 4), 80.f)
		&& FMath::IsNearlyEqual(Look(EFormationRole::Rear, EFormationKind::EchelonRight, 4), 150.f));
	TestTrue(TEXT("Echelon left mirrored (order K, P, L, Z)"), FMath::IsNearlyEqual(Look(EFormationRole::Right, EFormationKind::EchelonLeft, 4), -45.f)
		&& FMath::IsNearlyEqual(Look(EFormationRole::Left, EFormationKind::EchelonLeft, 4), -80.f)
		&& FMath::IsNearlyEqual(Look(EFormationRole::Rear, EFormationKind::EchelonLeft, 4), -150.f));
	// Line: all forward with a slight fan (outer 25, inner 5).
	TestTrue(TEXT("Line fan L -25, K -5, P +5, Z +25"), FMath::IsNearlyEqual(Look(EFormationRole::Left, EFormationKind::Line, 4), -25.f)
		&& FMath::IsNearlyEqual(Look(EFormationRole::Commander, EFormationKind::Line, 4), -5.f)
		&& FMath::IsNearlyEqual(Look(EFormationRole::Right, EFormationKind::Line, 4), 5.f)
		&& FMath::IsNearlyEqual(Look(EFormationRole::Rear, EFormationKind::Line, 4), 25.f));
	for (const EFormationRole Role : { EFormationRole::Commander, EFormationRole::Right, EFormationRole::Left, EFormationRole::Rear })
	{
		TestTrue(TEXT("Line: everybody within 30 deg of 12"), FMath::Abs(Look(Role, EFormationKind::Line, 4)) <= 30.f);
	}
	// Wall mode with the outward normal as the reference: K looks along it, nobody turns back towards the wall.
	for (const EFormationRole Role : { EFormationRole::Commander, EFormationRole::Right, EFormationRole::Left, EFormationRole::Rear })
	{
		const float Yaw = FormationSectorRules::LookYaw(Role, EFormationKind::Diamond, EFormationMode::Wall, 4, 0.f, -90.f, TOptional<float>(), Config);
		TestTrue(FString::Printf(TEXT("Wall mode: %s within 90 deg of the outward normal"), FormationNames::ToString(Role)),
			FormationMath::AngleBetween(Yaw, -90.f) < 90.f);
	}
	return true;
}

FORMATION_SECTOR_TEST(FFormationSectorFacingPriorityTest, "FacingPriority")
bool FFormationSectorFacingPriorityTest::RunTest(const FString&)
{
	FFormationFacingInputs Inputs;
	Inputs.MovementYaw = 5.f;
	float Yaw = 0.f;
	TestEqual(TEXT("Nothing else: the movement direction"), FormationSectorRules::ResolveFacing(Inputs, Yaw), EFormationFacingSource::Movement);
	TestEqual(TEXT("Movement yaw"), Yaw, 5.f);
	Inputs.SectorYaw = 60.f;
	TestEqual(TEXT("The sector beats the movement direction"), FormationSectorRules::ResolveFacing(Inputs, Yaw), EFormationFacingSource::FormationSector);
	Inputs.CoverYaw = 100.f;
	TestEqual(TEXT("Cover beats the sector"), FormationSectorRules::ResolveFacing(Inputs, Yaw), EFormationFacingSource::Cover);
	Inputs.FacingOrderYaw = -30.f;
	TestEqual(TEXT("An explicit facing order beats cover"), FormationSectorRules::ResolveFacing(Inputs, Yaw), EFormationFacingSource::FacingOrder);
	Inputs.CombatTargetYaw = 170.f;
	TestEqual(TEXT("The combat target beats everything"), FormationSectorRules::ResolveFacing(Inputs, Yaw), EFormationFacingSource::CombatTarget);
	TestEqual(TEXT("Combat target yaw"), Yaw, 170.f);
	return true;
}

FORMATION_SECTOR_TEST(FFormationSectorPaceTest, "PatrolPace")
bool FFormationSectorPaceTest::RunTest(const FString&)
{
	const FFormationConfig Config;
	TestEqual(TEXT("Patrol pace = 0.8 of the walk speed"), Config.PacePatrol, 0.8f);
	TestTrue(TEXT("Patrol speed of 220 = 176"), FMath::IsNearlyEqual(FormationSectorRules::PatrolSpeed(220.f, Config), 176.f));
	TestTrue(TEXT("Walk mode uses the patrol pace"), FMath::IsNearlyEqual(FormationSectorRules::SquadPace(EFormationMode::Walk, Config),
		Config.PaceWalkSpeed * Config.PacePatrol));
	// The rear man walking backwards (at most move.backward_mult of the walk speed) keeps up with the squad's patrol pace.
	TestTrue(TEXT("Backward walk keeps up with the patrol pace"), Config.PaceWalkSpeed * Config.MoveBackwardMult >= FormationSectorRules::SquadPace(EFormationMode::Walk, Config) - 1e-3f);
	TestEqual(TEXT("Run = the run speed"), FormationSectorRules::SquadPace(EFormationMode::Run, Config), Config.PaceRunSpeed);
	TestEqual(TEXT("Halt = 0"), FormationSectorRules::SquadPace(EFormationMode::Halt, Config), 0.f);
	return true;
}

FORMATION_SECTOR_TEST(FFormationSectorNarrowColumnTest, "NarrowColumnNobodyLooksIntoTheWalls")
bool FFormationSectorNarrowColumnTest::RunTest(const FString&)
{
	const FFormationConfig Config;
	const auto Look = [&Config](EFormationRole Role, const TOptional<float>& Width)
	{
		return FormationSectorRules::LookYaw(Role, EFormationKind::Column, EFormationMode::Walk, 4, 0.f, 0.f, Width, Config);
	};
	// v1.1, user decision: in a narrow passage P looks forward over the shoulder, L back at a slant, Z straight back, K 12.
	const TOptional<float> Narrow(460.f);
	TestTrue(TEXT("Narrow: K 0"), FMath::IsNearlyEqual(Look(EFormationRole::Commander, Narrow), 0.f));
	TestTrue(TEXT("Narrow: P +15 (over the shoulder)"), FMath::IsNearlyEqual(Look(EFormationRole::Right, Narrow), Config.ColumnNarrowFwdDeg));
	TestTrue(TEXT("Narrow: L -165 (back at a slant)"), FMath::IsNearlyEqual(Look(EFormationRole::Left, Narrow), -Config.ColumnNarrowBackDeg));
	TestTrue(TEXT("Narrow: Z 180"), FMath::IsNearlyEqual(FMath::Abs(Look(EFormationRole::Rear, Narrow)), 180.f));
	// Within 40 deg of 12 or of 6: nobody looks into a side wall.
	for (const EFormationRole Role : { EFormationRole::Commander, EFormationRole::Right, EFormationRole::Left, EFormationRole::Rear })
	{
		const float Offset = FMath::Abs(Look(Role, Narrow));
		TestTrue(FString::Printf(TEXT("Narrow: %s not towards a side wall (%.0f)"), FormationNames::ToString(Role), Offset), Offset <= 40.f || Offset >= 140.f);
	}
	// A wide passage (>= column.narrow_width) or no passage at all: the +-70 herringbone.
	TestTrue(TEXT("Wide passage: herringbone P +70"), FMath::IsNearlyEqual(Look(EFormationRole::Right, TOptional<float>(760.f)), Config.ColumnSideDeg));
	TestTrue(TEXT("At the threshold: wide"), FMath::IsNearlyEqual(Look(EFormationRole::Left, TOptional<float>(Config.ColumnNarrowWidth)), -Config.ColumnSideDeg));
	TestTrue(TEXT("No passage: herringbone L -70"), FMath::IsNearlyEqual(Look(EFormationRole::Left, TOptional<float>()), -Config.ColumnSideDeg));
	// The width matters only for the column.
	TestTrue(TEXT("Diamond ignores the width"), FMath::IsNearlyEqual(FormationSectorRules::LookYaw(EFormationRole::Right, EFormationKind::Diamond,
		EFormationMode::Halt, 4, 0.f, 0.f, Narrow, Config), Config.HaltSideDeg));
	return true;
}

#undef FORMATION_SECTOR_TEST

#endif
