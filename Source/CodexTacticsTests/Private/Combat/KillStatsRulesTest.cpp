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
	KillStatsRules::RegisterKill(Stats, EEnemyArchetype::FrostHound, TEXT("Командир"), TEXT("Инженер"));
	KillStatsRules::RegisterKill(Stats, EEnemyArchetype::Spitter, TEXT("Турель"), TEXT("Инженер"));
	KillStatsRules::RegisterKill(Stats, EEnemyArchetype::Brute, TEXT("Инженер"), TEXT("Командир"));
	KillStatsRules::RegisterKill(Stats, EEnemyArchetype::FrostHound, TEXT("Мина"), TEXT("Командир"));
	KillStatsRules::RegisterKill(Stats, EEnemyArchetype::Cutter, TEXT("Медик"), TEXT("Командир"));
	TestTrue(TEXT("Commander: hound + turret spitter"), Stats.Commander.Hound == 1 && Stats.Commander.Spitter == 1
		&& Stats.Commander.TurretKills == 1 && Stats.Commander.Total == 2 && Stats.TurretKills == 1);
	TestTrue(TEXT("Engineer: brute"), Stats.Engineer.Brute == 1 && Stats.Engineer.Total == 1);
	TestTrue(TEXT("Medic: mine hound + cutter (total only)"), Stats.Medic.Hound == 1 && Stats.Medic.MineKills == 1
		&& Stats.Medic.Total == 2 && Stats.MineKills == 1);

	// Other sources count for the leader; the recruit has no entry; a barricade kill is also counted.
	KillStatsRules::RegisterKill(Stats, EEnemyArchetype::Spitter, TEXT("Граната"), TEXT("Инженер"));
	TestEqual(TEXT("Grenade kill for the engineer leading"), Stats.Engineer.Spitter, 1);
	KillStatsRules::RegisterKill(Stats, EEnemyArchetype::Spitter, TEXT("Холод"), TEXT("Иван Сусанин"));
	TestEqual(TEXT("Recruit leading: not counted"), Stats.GetTotal(), 6);
	KillStatsRules::RegisterKill(Stats, EEnemyArchetype::Frostbitten, TEXT("Баррикада"), TEXT("Командир"));
	TestTrue(TEXT("Barricade kill"), Stats.BarricadeKills == 1 && Stats.Commander.Total == 3 && Stats.GetTotal() == 7);

	const FString Text = KillStatsRules::Format(Stats);
	TestTrue(TEXT("Header total"), Text.StartsWith(TEXT("📊 ИТОГОВАЯ СТАТИСТИКА БОЯ (ВСЕГО УНИЧТОЖЕНО: 7):")));
	TestTrue(TEXT("Commander line"), Text.Contains(TEXT("🎖️ Командир: 🐺 1 | 🏹 1 | ❄️ 0  (💥 Турель: 1) ➔ 3 убийств")));
	TestTrue(TEXT("Medic line"), Text.Contains(TEXT("🩺 Медик-сапёр: 🐺 1 | 🏹 0 | ❄️ 0  (💣 Мины: 1) ➔ 2 убийств")));

	// Recovery: first member under the cap in the given order; -1 = no such member.
	TestEqual(TEXT("Leader has room"), DeployableRules::PickRecoveryRecipient({ 0, 2, 1 }, 2), 0);
	TestEqual(TEXT("Leader full, next"), DeployableRules::PickRecoveryRecipient({ 2, -1, 1 }, 2), 2);
	TestEqual(TEXT("All full"), DeployableRules::PickRecoveryRecipient({ 2, 2 }, 2), INDEX_NONE);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
