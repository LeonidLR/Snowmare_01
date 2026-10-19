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
