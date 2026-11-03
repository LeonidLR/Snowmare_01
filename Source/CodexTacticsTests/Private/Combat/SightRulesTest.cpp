// CodexTactics.Combat.Sight.* — Sprint 08 line of sight against 60 cm barricades (TANDEM «SPRINT 08 DIRECTIVE»).

#include "Combat/SightRules.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Misc/AutomationTest.h"
#include "UI/MinimapSubsystem.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSightHeightsTest, "CodexTactics.Combat.Sight.CoverOcclusion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSightHeightsTest::RunTest(const FString& Parameters)
{
	using namespace SightRules;
	TestEqual(TEXT("eyes standing"), EyeHeight(EOperativeStance::Standing), 160.f);
	TestEqual(TEXT("eyes crouched"), EyeHeight(EOperativeStance::Crouching), 95.f);
	TestEqual(TEXT("eyes prone"), EyeHeight(EOperativeStance::Prone), 25.f);
	TestEqual(TEXT("profile standing"), ProfileHeight(EOperativeStance::Standing), 150.f);
	TestEqual(TEXT("profile crouched"), ProfileHeight(EOperativeStance::Crouching), 90.f);
	TestEqual(TEXT("profile prone"), ProfileHeight(EOperativeStance::Prone), 25.f);

	// 8-A.1: crouched 0.5 m behind his barricade, enemies 10 m away.
	TestTrue(TEXT("crouched sees a standing enemy over his cover"), ClearsCover(95.f, 150.f, 50.f, 1000.f));
	TestTrue(TEXT("crouched sees a crouching enemy over his cover"), ClearsCover(95.f, 90.f, 50.f, 1000.f));
	// 8-A.2: the enemy goes prone 1 m behind a barricade -> hidden from the crouched observer 10 m away.
	TestFalse(TEXT("prone enemy behind its barricade hidden (crouched, 10 m)"), ClearsCover(95.f, 25.f, 900.f, 1000.f));
	// 8-A.3: standing up gives the steep angle — close in (3 m) the prone one shows, crouched it does not.
	TestTrue(TEXT("standing 3 m away sees the prone one"), ClearsCover(160.f, 25.f, 200.f, 300.f));
	TestFalse(TEXT("crouched 3 m away does not"), ClearsCover(95.f, 25.f, 200.f, 300.f));
	// 8-B: symmetric — an enemy standing 10 m away does not see a prone operative 1 m behind a barricade, sees a crouched one.
	TestFalse(TEXT("prone operative hidden from a standing enemy"), ClearsCover(160.f, 25.f, 900.f, 1000.f));
	TestTrue(TEXT("crouched operative seen over the cover"), ClearsCover(160.f, 90.f, 900.f, 1000.f));
	TestEqual(TEXT("line height halfway"), LineHeightAt(160.f, 20.f, 500.f, 1000.f), 90.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSightSensesTest, "CodexTactics.Combat.Sight.HearingBlindFireMemory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSightSensesTest::RunTest(const FString& Parameters)
{
	using namespace SightRules;
	TestEqual(TEXT("demask 2 s"), DemaskSeconds, 2.f);
	TestEqual(TEXT("squad hears within 12 m"), SquadHearingCm, 1200.f);
	TestEqual(TEXT("enemy hears a walking operative at 12 m"), EnemyHearingRadius(EOperativeStance::Crouching), 1200.f);
	TestEqual(TEXT("a crawling one only at 5 m"), EnemyHearingRadius(EOperativeStance::Prone), 500.f);
	TestEqual(TEXT("blind fire -80 %"), BlindFireHitChance(0.8f, 50.f), 0.16f, 0.001f);
	TestEqual(TEXT("blind fire at an empty spot never hits"), BlindFireHitChance(0.8f, 400.f), 0.f);
	TestFalse(TEXT("pursues a fresh last known spot"), ShouldForget(5.f, 0.f));
	TestFalse(TEXT("searches the spot a while"), ShouldForget(8.f, 3.f));
	TestTrue(TEXT("gives up after searching 5 s"), ShouldForget(10.f, 5.f));
	TestTrue(TEXT("forgets an old memory"), ShouldForget(25.f, 0.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSightHiddenPoseTest, "CodexTactics.Combat.Sight.HiddenEnemiesKeepAnimating",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSightHiddenPoseTest::RunTest(const FString& Parameters)
{
	// User report 2026-10-06: hidden enemies (and their silhouettes) slid in the T-pose — an unrendered mesh did not
	// refresh its bones. While hidden the mesh must evaluate its pose; shown, its authored option comes back.
	USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>(GetTransientPackage());
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	UTacticalSightSubsystem::FSavedAnimTickOptions Saved;

	UTacticalSightSubsystem::KeepPoseWhileHidden(*Mesh, true, Saved);
	TestTrue(TEXT("hidden: pose ticked and bones refreshed although not rendered"),
		Mesh->VisibilityBasedAnimTickOption == EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones);
	TestEqual(TEXT("the authored option is saved"), Saved.Num(), 1);

	UTacticalSightSubsystem::KeepPoseWhileHidden(*Mesh, true, Saved);
	UTacticalSightSubsystem::KeepPoseWhileHidden(*Mesh, false, Saved);
	TestTrue(TEXT("shown: the authored option is back (a second hide does not overwrite it)"),
		Mesh->VisibilityBasedAnimTickOption == EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered);
	TestEqual(TEXT("nothing left saved"), Saved.Num(), 0);

	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPose;
	UTacticalSightSubsystem::KeepPoseWhileHidden(*Mesh, false, Saved);
	TestTrue(TEXT("shown without a hide: untouched"), Mesh->VisibilityBasedAnimTickOption == EVisibilityBasedAnimTickOption::AlwaysTickPose);
	return true;
}

// Bug hunt 2026-10-18 (CornerHoldSmoke 1 in 3): a frostbitten waiting 1.7 m off right behind the wall's corner stayed hidden
// from the operative holding that corner, so he never fired at it; an enemy at arm's length is noticed without a sight line.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSightTouchRevealTest, "CodexTactics.Combat.Sight.TouchReveal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSightTouchRevealTest::RunTest(const FString&)
{
	TestTrue(TEXT("a frostbitten stopped 173 cm off behind the corner is noticed"), SightRules::IsWithinTouchReveal(173.f));
	TestTrue(TEXT("at the reveal distance"), SightRules::IsWithinTouchReveal(SightRules::SquadTouchRevealCm));
	TestFalse(TEXT("3 m off behind a barricade stays hidden (SightSmoke)"), SightRules::IsWithinTouchReveal(300.f));
	TestTrue(TEXT("closer than any sight check (2.5 m and up)"), SightRules::SquadTouchRevealCm < 250.f);
	return true;
}

// Game designer 2026-10-19 (symmetric sight): "prone behind a wall the enemies do not see us, yet we still see them". An operative sees
// an enemy only along a clear line from HIS eye height for his stance to the enemy's profile - the same rule the enemies use - while
// exploring too. A 1.2 m wall 1 m in front of him, a man-sized enemy (profile 150 cm) 6 m beyond it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSightEyeHeightWallTest, "CodexTactics.Combat.Sight.EyeHeightVsWall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSightEyeHeightWallTest::RunTest(const FString&)
{
	using namespace SightRules;
	constexpr float WallTop = 120.f;
	constexpr float WallAt = 100.f;
	constexpr float EnemyAt = 700.f;
	constexpr float EnemyProfile = 150.f;
	TestFalse(TEXT("prone (25 cm eyes) 1 m behind a 1.2 m wall: the enemy is hidden"),
		ClearsCover(EyeHeight(EOperativeStance::Prone), EnemyProfile, WallAt, EnemyAt, WallTop));
	TestFalse(TEXT("crouched (95 cm eyes): still hidden"), ClearsCover(EyeHeight(EOperativeStance::Crouching), EnemyProfile, WallAt, EnemyAt, WallTop));
	TestTrue(TEXT("standing (160 cm eyes): seen over the wall"), ClearsCover(EyeHeight(EOperativeStance::Standing), EnemyProfile, WallAt, EnemyAt, WallTop));
	// Symmetric: the same wall hides the prone operative from that enemy (its eyes ~150 cm, his profile 25 cm).
	TestFalse(TEXT("... and the enemy does not see the prone operative either"), ClearsCover(150.f, ProfileHeight(EOperativeStance::Prone), EnemyAt - WallAt, EnemyAt, WallTop));
	// A low (60 cm) barricade does not hide a standing enemy from a crouched operative.
	TestTrue(TEXT("crouched over a 60 cm barricade: seen"), ClearsCover(EyeHeight(EOperativeStance::Crouching), EnemyProfile, WallAt, EnemyAt, CoverHeightCm));

	// When the squad's own sight runs: the fight (real time / pause) and - since 2026-10-19 - exploring.
	TestTrue(TEXT("exploring: on"), IsSquadSightPhase(ECodexGamePhase::Exploration, ECodexCombatMode::None, true));
	TestFalse(TEXT("exploring with Codex.Sight.Exploration 0: off (the old rule)"), IsSquadSightPhase(ECodexGamePhase::Exploration, ECodexCombatMode::None, false));
	TestTrue(TEXT("real-time fight: on"), IsSquadSightPhase(ECodexGamePhase::WaveCombat, ECodexCombatMode::RealTime, false));
	TestTrue(TEXT("tactical pause: on"), IsSquadSightPhase(ECodexGamePhase::WaveCombat, ECodexCombatMode::TacticalPause, true));
	TestFalse(TEXT("turn-based: its own rules"), IsSquadSightPhase(ECodexGamePhase::WaveCombat, ECodexCombatMode::TurnBased, true));
	TestFalse(TEXT("after the fight: everything shown"), IsSquadSightPhase(ECodexGamePhase::PostCombat, ECodexCombatMode::None, true));

	// What the player sees for an enemy nobody sees: hidden; a last-known silhouette that fades exploring (with the minimap icon).
	TestFalse(TEXT("a fresh silhouette stays"), ShouldDropLastKnownGhost(false, false, 3.f));
	TestTrue(TEXT("exploring: it fades after the minimap's fade time"), ShouldDropLastKnownGhost(false, false, LastKnownFadeSeconds));
	TestFalse(TEXT("a heard one follows the sound"), ShouldDropLastKnownGhost(false, true, 60.f));
	TestFalse(TEXT("in a fight it stays (blind fire)"), ShouldDropLastKnownGhost(true, false, 60.f));
	TestEqual(TEXT("one fade time for the world and the minimap"), UMinimapSubsystem::EnemyFadeSeconds, LastKnownFadeSeconds);
	return true;
}

// Field of view (game designer 2026-10-20: "the other two crawl away in the opposite direction, yet the enemies are fully visible").
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSightFieldOfViewTest, "CodexTactics.Combat.Sight.FieldOfView",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSightFieldOfViewTest::RunTest(const FString& Parameters)
{
	using namespace SightRules;
	const FSightFovConfig Config;
	TestEqual(TEXT("data: standing 140 deg"), FieldOfViewDeg(Config, EOperativeStance::Standing), 140.f);
	TestEqual(TEXT("data: crouched 140 deg"), FieldOfViewDeg(Config, EOperativeStance::Crouching), 140.f);
	TestEqual(TEXT("data: prone 110 deg"), FieldOfViewDeg(Config, EOperativeStance::Prone), 110.f);

	const FVector Eye(0.f, 0.f, 160.f);
	const FVector Facing(1.f, 0.f, 0.f);
	auto At = [](float YawDeg, float DistanceCm) { return FRotator(0.f, YawDeg, 0.f).Vector() * DistanceCm + FVector(0.f, 0.f, 150.f); };
	// Standing / crouched: +-70 deg.
	TestTrue(TEXT("straight ahead"), IsInFieldOfView(Eye, Facing, At(0.f, 2000.f), FieldOfViewDeg(Config, EOperativeStance::Standing)));
	TestTrue(TEXT("standing: 69 deg off still seen"), IsInFieldOfView(Eye, Facing, At(69.f, 2000.f), 140.f));
	TestTrue(TEXT("standing: -69 deg off still seen"), IsInFieldOfView(Eye, Facing, At(-69.f, 2000.f), 140.f));
	TestFalse(TEXT("standing: 72 deg off is outside"), IsInFieldOfView(Eye, Facing, At(72.f, 2000.f), 140.f));
	TestFalse(TEXT("standing: beside him (90 deg) is outside"), IsInFieldOfView(Eye, Facing, At(90.f, 2000.f), 140.f));
	TestFalse(TEXT("standing: directly behind him (the screenshot: enemies 20 m behind the squad's backs)"), IsInFieldOfView(Eye, Facing, At(180.f, 2000.f), 140.f));
	// Prone: +-55 deg.
	TestTrue(TEXT("prone: 54 deg off seen"), IsInFieldOfView(Eye, Facing, At(54.f, 2000.f), FieldOfViewDeg(Config, EOperativeStance::Prone)));
	TestFalse(TEXT("prone: 60 deg off is outside (standing would see it)"), IsInFieldOfView(Eye, Facing, At(60.f, 2000.f), FieldOfViewDeg(Config, EOperativeStance::Prone)));
	TestTrue(TEXT("... the same 60 deg standing"), IsInFieldOfView(Eye, Facing, At(60.f, 2000.f), FieldOfViewDeg(Config, EOperativeStance::Standing)));
	// The angle is planar: height does not matter; a point right above / below him and a zero facing count as inside; 360 sees all round.
	TestTrue(TEXT("planar: a target high above the eye at the same angle"), IsInFieldOfView(Eye, Facing, FVector(2000.f, 0.f, 900.f), 140.f));
	TestTrue(TEXT("straight above: inside"), IsInFieldOfView(Eye, Facing, FVector(0.f, 0.f, 900.f), 140.f));
	TestTrue(TEXT("no facing: inside"), IsInFieldOfView(Eye, FVector::ZeroVector, At(180.f, 500.f), 140.f));
	TestTrue(TEXT("360: all round"), IsInFieldOfView(Eye, Facing, At(180.f, 500.f), 360.f));
	// The facing is any direction, not just +X.
	TestTrue(TEXT("facing +Y: a target at +Y"), IsInFieldOfView(Eye, FVector(0.f, 1.f, 0.f), FVector(0.f, 1500.f, 150.f), 140.f));
	TestFalse(TEXT("facing +Y: a target at -Y"), IsInFieldOfView(Eye, FVector(0.f, 1.f, 0.f), FVector(0.f, -1500.f, 150.f), 140.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSightBearingsNearestFirstTest, "CodexTactics.Combat.Sight.BearingsNearestFirst",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSightBearingsNearestFirstTest::RunTest(const FString&)
{
	// 2026-10-26: the TutorialSmoke sight scene (a 1.2 m wall 1 m ahead, an enemy 7 m on) is laid out along the first CLEAR bearing from
	// the leader's facing - straight along it, the enemy fell behind a 2.4 m container 5 m away (InteractableActor_1) in ~1 run of 4.
	const TArray<float> Bearings = SightRules::BearingsNearestFirst(115.f, 30.f);
	TestEqual(TEXT("the preferred bearing first"), Bearings.Num() > 0 ? Bearings[0] : 0.f, 115.f);
	TestEqual(TEXT("then +step, -step"), Bearings.Num() > 2 ? Bearings[1] : 0.f, 145.f);
	TestEqual(TEXT("... -step"), Bearings.Num() > 2 ? Bearings[2] : 0.f, 85.f);
	TestEqual(TEXT("+2 step wraps to (-180, 180]"), Bearings.Num() > 3 ? Bearings[3] : 0.f, 175.f);
	TestEqual(TEXT("+3 step normalized"), Bearings.Num() > 5 ? Bearings[5] : 0.f, -155.f);
	TestEqual(TEXT("every bearing once round the circle (180 deg off only once)"), Bearings.Num(), 12);
	TestEqual(TEXT("the last is straight behind"), Bearings.Last(), -65.f);
	TestEqual(TEXT("no step: just the preferred"), SightRules::BearingsNearestFirst(10.f, 0.f).Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSightHeightRangeTest, "CodexTactics.Combat.Sight.HeightRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSightHeightRangeTest::RunTest(const FString& Parameters)
{
	// Game designer 2026-11-02 (L_Gen_Depot_1, numbers pending his confirmation): the sight range grows with the height level - 3 m steps of the
	// eye over the ground below; the ground and everything below it x1, +1 x1.25, +2 x1.5, +3 x1.75, capped at x2.
	using namespace SightRules;
	const FHeightSightConfig Config;
	TestEqual(TEXT("default step 3 m"), Config.LevelStepCm, 300.f);
	// The level from the eye height above the ground below him (eye = feet + stance eye).
	TestEqual(TEXT("standing on the ground (eye 160): level 0"), HeightLevel(160.f), 0);
	TestEqual(TEXT("prone in the -3 m cut, his ground the cut floor (eye 25): level 0"), HeightLevel(25.f), 0);
	TestEqual(TEXT("an eye below the ground below him (a pit's lip): level 0, never negative"), HeightLevel(-200.f), 0);
	TestEqual(TEXT("standing on a +1 roof (300 + 160): level 1"), HeightLevel(460.f), 1);
	TestEqual(TEXT("prone on a +1 roof (300 + 25): level 1"), HeightLevel(325.f), 1);
	TestEqual(TEXT("prone on the ruin's +2 roof (600 + 25): level 2"), HeightLevel(625.f), 2);
	TestEqual(TEXT("prone on a +3 hall roof (900 + 25): level 3"), HeightLevel(925.f), 3);
	TestEqual(TEXT("standing on a +3 hall roof (900 + 160): level 3"), HeightLevel(1060.f), 3);
	TestEqual(TEXT("exactly 3 m: level 1 (floor)"), HeightLevel(300.f), 1);
	TestEqual(TEXT("just under 3 m: level 0"), HeightLevel(299.f), 0);
	TestEqual(TEXT("no step: level 0"), HeightLevel(925.f, 0.f), 0);
	// The multiplier ladder.
	TestEqual(TEXT("level -1 (below ground): x1, never less"), HeightRangeMultiplier(Config, -1), 1.f);
	TestEqual(TEXT("level 0 (ground): x1"), HeightRangeMultiplier(Config, 0), 1.f);
	TestEqual(TEXT("level +1 (3 m): x1.25"), HeightRangeMultiplier(Config, 1), 1.25f);
	TestEqual(TEXT("level +2 (6 m): x1.5"), HeightRangeMultiplier(Config, 2), 1.5f);
	TestEqual(TEXT("level +3 (9 m): x1.75"), HeightRangeMultiplier(Config, 3), 1.75f);
	TestEqual(TEXT("level +4 (12 m): x2 (the cap)"), HeightRangeMultiplier(Config, 4), 2.f);
	TestEqual(TEXT("level +7: still x2"), HeightRangeMultiplier(Config, 7), 2.f);
	TestEqual(TEXT("eye 925 over the ground: x1.75"), HeightRangeMultiplierAt(Config, 925.f), 1.75f);
	TestEqual(TEXT("eye 160 over the ground: x1"), HeightRangeMultiplierAt(Config, 160.f), 1.f);
	// The squad's 60 m on a +3 roof reaches 105 m; a Marksman (45 m) on it 78.75 m.
	TestEqual(TEXT("60 m x +3 = 105 m"), 6000.f * HeightRangeMultiplier(Config, 3), 10500.f);
	TestEqual(TEXT("marksman 45 m x +3 = 78.75 m"), 4500.f * HeightRangeMultiplier(Config, 3), 7875.f, 0.01f);
	// Data-tuned: another bonus / cap / step.
	FHeightSightConfig Tuned;
	Tuned.BonusPerLevel = 0.5f;
	Tuned.MaxMultiplier = 1.8f;
	Tuned.LevelStepCm = 400.f;
	TestEqual(TEXT("tuned: +1 x1.5"), HeightRangeMultiplier(Tuned, 1), 1.5f);
	TestEqual(TEXT("tuned: +2 capped at x1.8"), HeightRangeMultiplier(Tuned, 2), 1.8f);
	TestEqual(TEXT("tuned step 4 m: eye 625 = level 1"), HeightRangeMultiplierAt(Tuned, 625.f), 1.5f);
	FHeightSightConfig Broken;
	Broken.MaxMultiplier = 0.5f;
	Broken.BonusPerLevel = -1.f;
	TestEqual(TEXT("a cap below 1 / a negative bonus never shrink the range"), HeightRangeMultiplier(Broken, 3), 1.f);
	// The ground below him: the lowest surface the column under his feet crosses.
	const float HallRoof[] = { 900.f, 900.f, 0.f };
	TestEqual(TEXT("hall roof edge: roof slab, the wall top under it, the street -> the street"), GroundBelowZ(900.f, HallRoof), 0.f);
	const float Ruin[] = { 600.f, 300.f, 0.f };
	TestEqual(TEXT("ruin roof: roof, +1 floor, street -> the street (+2, not +1)"), GroundBelowZ(600.f, Ruin), 0.f);
	const float Cut[] = { -300.f };
	TestEqual(TEXT("in the cut: the cut floor is his ground"), GroundBelowZ(-300.f, Cut), -300.f);
	TestEqual(TEXT("nothing below: his feet"), GroundBelowZ(120.f, TArrayView<const float>()), 120.f);
	return true;
}
