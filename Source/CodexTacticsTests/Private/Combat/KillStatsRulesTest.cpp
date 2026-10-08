#include "Misc/AutomationTest.h"
#include "Combat/KillStatsRules.h"
#include "Interactables/DeployableRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Godot main.gd register_enemy_kill / _format_kill_stats_bbcode / _auto_recover_all_deployables parity.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKillStatsRulesTest, "CodexTactics.Combat.KillStats.Rules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKillStatsRulesTest::RunTest(const FString&)
{
	FSquadKillStats Stats;
	KillStatsRules::RegisterKill(Stats, EEnemyArchetype::FrostHound, TEXT("Commander"), TEXT("Engineer"));
	KillStatsRules::RegisterKill(Stats, EEnemyArchetype::Spitter, TEXT("Turret"), TEXT("Engineer"));
	KillStatsRules::RegisterKill(Stats, EEnemyArchetype::Brute, TEXT("Engineer"), TEXT("Commander"));
	KillStatsRules::RegisterKill(Stats, EEnemyArchetype::FrostHound, TEXT("Mine"), TEXT("Commander"));
	KillStatsRules::RegisterKill(Stats, EEnemyArchetype::Cutter, TEXT("Medic"), TEXT("Commander"));
	TestTrue(TEXT("Commander: hound + turret spitter"), Stats.Commander.Hound == 1 && Stats.Commander.Spitter == 1
		&& Stats.Commander.TurretKills == 1 && Stats.Commander.Total == 2 && Stats.TurretKills == 1);
	TestTrue(TEXT("Engineer: brute"), Stats.Engineer.Brute == 1 && Stats.Engineer.Total == 1);
	TestTrue(TEXT("Medic: mine hound + cutter (total only)"), Stats.Medic.Hound == 1 && Stats.Medic.MineKills == 1
		&& Stats.Medic.Total == 2 && Stats.MineKills == 1);

	// Other sources count for the leader; the recruit has no entry; a barricade kill is also counted.
	KillStatsRules::RegisterKill(Stats, EEnemyArchetype::Spitter, TEXT("Grenade"), TEXT("Engineer"));
	TestEqual(TEXT("Grenade kill for the engineer leading"), Stats.Engineer.Spitter, 1);
	KillStatsRules::RegisterKill(Stats, EEnemyArchetype::Spitter, TEXT("Cold"), TEXT("Ivan Susanin"));
	TestEqual(TEXT("Recruit leading: not counted"), Stats.GetTotal(), 6);
	KillStatsRules::RegisterKill(Stats, EEnemyArchetype::Frostbitten, TEXT("Barricade"), TEXT("Commander"));
	TestTrue(TEXT("Barricade kill"), Stats.BarricadeKills == 1 && Stats.Commander.Total == 3 && Stats.GetTotal() == 7);

	// Legacy Russian producer strings / Blueprint DisplayNames still count (matching contract: English or Russian, exact).
	FSquadKillStats Legacy;
	KillStatsRules::RegisterKill(Legacy, EEnemyArchetype::Spitter, TEXT("Турель"), TEXT("Инженер"));
	KillStatsRules::RegisterKill(Legacy, EEnemyArchetype::FrostHound, TEXT("Мина"), TEXT("Командир"));
	KillStatsRules::RegisterKill(Legacy, EEnemyArchetype::Brute, TEXT("Grenade"), TEXT("Инженер"));
	KillStatsRules::RegisterKill(Legacy, EEnemyArchetype::Frostbitten, TEXT("Баррикада"), TEXT("Медик-сапёр"));
	TestTrue(TEXT("Legacy Russian sources"), Legacy.Commander.TurretKills == 1 && Legacy.Medic.MineKills == 1
		&& Legacy.Engineer.Brute == 1 && Legacy.Medic.Total == 2 && Legacy.BarricadeKills == 1);

	const FString Text = KillStatsRules::Format(Stats);
	TestTrue(TEXT("Header total"), Text.StartsWith(TEXT("📊 FINAL COMBAT STATS (TOTAL KILLED: 7):")));
	TestTrue(TEXT("Commander line"), Text.Contains(TEXT("🎖️ Commander: 🐺 1 | 🏹 1 | ❄️ 0  (💥 Turret: 1) ➔ 3 kills")));
	TestTrue(TEXT("Medic line"), Text.Contains(TEXT("🩺 Medic-Sapper: 🐺 1 | 🏹 0 | ❄️ 0  (💣 Mines: 1) ➔ 2 kills")));

	// Recovery: first member under the cap in the given order; -1 = no such member.
	TestEqual(TEXT("Leader has room"), DeployableRules::PickRecoveryRecipient({ 0, 2, 1 }, 2), 0);
	TestEqual(TEXT("Leader full, next"), DeployableRules::PickRecoveryRecipient({ 2, -1, 1 }, 2), 2);
	TestEqual(TEXT("All full"), DeployableRules::PickRecoveryRecipient({ 2, 2 }, 2), INDEX_NONE);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
