// CodexTactics.Combat.VisionCone.* — ground vision cones (user request 2026-10-11; Godot Scenes/movements/player.gd
// _rebuild_vision_cone_mesh / _update_vision_cone / _is_any_enemy_spotted; enemy cones from the port's perception data).

#include "AI/PerceptionRules.h"
#include "Combat/VisionConeRules.h"
#include "Data/EnemyPerception.h"
#include "Misc/AutomationTest.h"

namespace VisionConeTest
{
	float PlanarAngleDeg(const FVector& Point)
	{
		return FMath::RadiansToDegrees(FMath::Atan2(Point.Y, Point.X));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisionConeFillTest, "CodexTactics.Combat.VisionCone.GodotFillGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVisionConeFillTest::RunTest(const FString& Parameters)
{
	using namespace VisionConeRules;
	// Godot: 32 segments, 14 m, 70 deg FULL angle (+-35), fan from the centre.
	const FVisionConeMesh Fill = BuildFill(GodotRangeCm, GodotHalfAngleDeg, GodotSegments, GodotHeightCm);
	TestEqual(TEXT("apex + 33 arc points"), Fill.Vertices.Num(), 34);
	TestEqual(TEXT("32 triangles x 2 windings"), Fill.Triangles.Num(), 32 * 6);
	TestTrue(TEXT("apex at the origin, 8 cm up"), Fill.Vertices[0].Equals(FVector(0.f, 0.f, 8.f), 0.01f));
	TestEqual(TEXT("first arc point at -35 deg"), VisionConeTest::PlanarAngleDeg(Fill.Vertices[1]), -35.f, 0.01f);
	TestEqual(TEXT("last arc point at +35 deg"), VisionConeTest::PlanarAngleDeg(Fill.Vertices.Last()), 35.f, 0.01f);
	TestEqual(TEXT("middle arc point straight ahead"), VisionConeTest::PlanarAngleDeg(Fill.Vertices[17]), 0.f, 0.01f);
	bool bOnArc = true;
	for (int32 Index = 1; Index < Fill.Vertices.Num(); ++Index)
	{
		bOnArc &= FMath::IsNearlyEqual(FVector(Fill.Vertices[Index].X, Fill.Vertices[Index].Y, 0.f).Size(), GodotRangeCm, 0.05f);
	}
	TestTrue(TEXT("every arc point at the 14 m range"), bOnArc);
	// Both windings: each triangle is followed by its mirror.
	TestEqual(TEXT("second winding mirrors the first"), Fill.Triangles[4], Fill.Triangles[2]);
	TestEqual(TEXT("second winding mirrors the first (2)"), Fill.Triangles[5], Fill.Triangles[1]);
	// Degenerate input.
	TestEqual(TEXT("no range: empty"), BuildFill(0.f, 35.f, 32).Vertices.Num(), 0);
	TestEqual(TEXT("segments clamped to 3"), BuildFill(100.f, 35.f, 1).Vertices.Num(), 5);
	TestEqual(TEXT("half angle clamped to 180"), ClampHalfAngle(400.f), 180.f);

	// Outline: one band quad per segment + the two side edges, all inside the sector.
	const FVisionConeMesh Outline = BuildOutline(GodotRangeCm, GodotHalfAngleDeg, GodotSegments, 8.f);
	TestEqual(TEXT("outline quads"), Outline.Vertices.Num(), (32 + 2) * 4);
	TestEqual(TEXT("outline triangles"), Outline.Triangles.Num(), (32 + 2) * 12);
	bool bInside = true;
	for (const FVector& Vertex : Outline.Vertices)
	{
		bInside &= IsInsideCone(FVector::ZeroVector, FVector::ForwardVector, Vertex, GodotRangeCm + 0.1f, GodotHalfAngleDeg + 0.01f);
	}
	TestTrue(TEXT("outline band stays inside the sector"), bInside);
	TestEqual(TEXT("all-round outline has no side edges"), BuildOutline(500.f, 180.f, 16, 8.f).Vertices.Num(), 16 * 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisionConePerceptionTest, "CodexTactics.Combat.VisionCone.MatchesEnemyPerception",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVisionConePerceptionTest::RunTest(const FString& Parameters)
{
	using namespace VisionConeRules;
	// The enemy cone is drawn from its perception (AEnemyCharacter::GetVisionConeRange / GetVisionConeHalfAngle): the sector
	// covers exactly where PerceptionRules sees a standing operative (ignoring the trace and the proximity circle).
	for (const EEnemyArchetype Archetype : { EEnemyArchetype::FrostHound, EEnemyArchetype::Marksman, EEnemyArchetype::Brute })
	{
		const FEnemyPerceptionParams Params = EnemyPerception::Get(Archetype);
		const float Range = PerceptionRules::EffectiveSightRange(Params, EOperativeStance::Standing);
		const FVisionConeMesh Fill = BuildFill(Range, Params.SightHalfAngleDeg, GodotSegments);
		const FString Name = UEnum::GetValueAsString(Archetype);
		TestTrue(*(Name + TEXT(": has a cone")), Fill.Vertices.Num() > 2 && Range > 0.f);
		TestEqual(*(Name + TEXT(": cone edge = field of view")), VisionConeTest::PlanarAngleDeg(Fill.Vertices.Last()),
			FMath::Min(Params.SightHalfAngleDeg, 180.f), 0.05f);
		TestEqual(*(Name + TEXT(": cone reach = standing sight range")), static_cast<float>(FVector(Fill.Vertices[1].X, Fill.Vertices[1].Y, 0.f).Size()), Range, 0.1f);
		// Point samples: inside the cone <=> within range and in the field of view.
		FEnemyPerceptionParams NoProximity = Params;
		NoProximity.ProximityCm = 0.f;
		int32 Mismatches = 0;
		for (int32 Ring = 1; Ring <= 6; ++Ring)
		{
			for (int32 Step = 0; Step < 36; ++Step)
			{
				const FVector Target = FVector(1.f, 0.f, 0.f).RotateAngleAxis(Step * 10.f + 3.f, FVector::UpVector) * (Range * Ring / 5.f);
				const bool bCone = IsInsideCone(FVector::ZeroVector, FVector::ForwardVector, Target, Range, Params.SightHalfAngleDeg);
				const bool bSees = PerceptionRules::CanSee(NoProximity, FVector::ZeroVector, FVector::ForwardVector, Target,
					EOperativeStance::Standing, /*bLineClear*/ true);
				Mismatches += bCone != bSees ? 1 : 0;
			}
		}
		TestEqual(*(Name + TEXT(": cone area = what it sees")), Mismatches, 0);
	}
	// Search (x1.25 perception): the cone grows with it.
	const FEnemyPerceptionParams Hound = EnemyPerception::Get(EEnemyArchetype::FrostHound);
	const FEnemyPerceptionParams Searching = PerceptionRules::Scaled(Hound, 1.25f);
	TestEqual(TEXT("searching hound cone 1.25 x longer"), PerceptionRules::EffectiveSightRange(Searching, EOperativeStance::Standing),
		PerceptionRules::EffectiveSightRange(Hound, EOperativeStance::Standing) * 1.25f, 0.1f);
	TestEqual(TEXT("searching keeps the field of view"), Searching.SightHalfAngleDeg, Hound.SightHalfAngleDeg);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisionConeToggleTest, "CodexTactics.Combat.VisionCone.ToggleAndColours",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVisionConeToggleTest::RunTest(const FString& Parameters)
{
	using namespace VisionConeRules;
	// Codex.Debug.VisionCones: -1 = per actor, 0 = all off, 1 = all on; never on the dead.
	TestTrue(TEXT("-1 + actor on"), ResolveVisible(-1, true, true));
	TestFalse(TEXT("-1 + actor off"), ResolveVisible(-1, false, true));
	TestFalse(TEXT("0 hides an actor set on"), ResolveVisible(0, true, true));
	TestTrue(TEXT("1 shows an actor set off"), ResolveVisible(1, false, true));
	TestFalse(TEXT("dead: never"), ResolveVisible(1, true, false));
	TestTrue(TEXT("other values act like -1"), ResolveVisible(7, true, true) && !ResolveVisible(7, false, true));

	// Enemy states.
	TestTrue(TEXT("engaged = alert"), GetEnemyState(false, false, 0.f) == EVisionConeState::Alert);
	TestTrue(TEXT("patrol, calm = idle"), GetEnemyState(true, false, 0.f) == EVisionConeState::Idle);
	TestTrue(TEXT("patrol, suspicion = suspicious"), GetEnemyState(true, false, 0.4f) == EVisionConeState::Suspicious);
	TestTrue(TEXT("searching = suspicious"), GetEnemyState(true, true, 0.f) == EVisionConeState::Suspicious);

	// Godot operative colours (player.gd _update_vision_cone).
	TestTrue(TEXT("clear = Godot green"), GetStateColor(EVisionConeState::Clear).Equals(FLinearColor(0.12f, 0.88f, 0.35f), 0.001f));
	TestTrue(TEXT("spotted = Godot red"), GetStateColor(EVisionConeState::Spotted).Equals(FLinearColor(0.95f, 0.18f, 0.18f), 0.001f));
	TestTrue(TEXT("suspicion 0 looks idle"), GetStateColor(EVisionConeState::Suspicious, 0.f).Equals(GetStateColor(EVisionConeState::Idle), 0.001f));
	TestFalse(TEXT("full suspicion differs from idle"), GetStateColor(EVisionConeState::Suspicious, 1.f).Equals(GetStateColor(EVisionConeState::Idle), 0.05f));
	TestTrue(TEXT("alert is red"), GetStateColor(EVisionConeState::Alert).R > 0.9f && GetStateColor(EVisionConeState::Alert).G < 0.3f);

	// Godot lerp(target, delta * 14): halfway after 1/28 s, there after 1/14 s.
	const FLinearColor Half = StepColor(FLinearColor::Black, FLinearColor::White, 1.f / 28.f);
	TestEqual(TEXT("colour halfway"), Half.R, 0.5f, 0.001f);
	TestTrue(TEXT("colour reached"), StepColor(FLinearColor::Black, FLinearColor::White, 0.2f).Equals(FLinearColor::White));

	// Inside test (planar, ignores height).
	TestTrue(TEXT("ahead in range"), IsInsideCone(FVector::ZeroVector, FVector::ForwardVector, FVector(1000.f, 0.f, 300.f), 1400.f, 35.f));
	TestFalse(TEXT("beyond range"), IsInsideCone(FVector::ZeroVector, FVector::ForwardVector, FVector(1500.f, 0.f, 0.f), 1400.f, 35.f));
	TestFalse(TEXT("outside the angle"), IsInsideCone(FVector::ZeroVector, FVector::ForwardVector, FVector(500.f, 500.f, 0.f), 1400.f, 35.f));
	TestTrue(TEXT("all round"), IsInsideCone(FVector::ZeroVector, FVector::ForwardVector, FVector(-500.f, 0.f, 0.f), 1400.f, 180.f));
	return true;
}
