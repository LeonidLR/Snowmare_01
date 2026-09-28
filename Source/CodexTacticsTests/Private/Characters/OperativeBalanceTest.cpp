#include "Misc/AutomationTest.h"
#include "Characters/OperativeBalance.h"
#include "Characters/OperativeCharacter.h"
#include "Combat/HealthComponent.h"
#include "Survival/ColdSurvivalComponent.h"
#include "Data/GodotBalanceAsset.h"

#if WITH_DEV_AUTOMATION_TESTS

// Godot player.gd apply_balance_config parity (per-role keys, metres -> cm).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOperativeBalanceApplyTest, "CodexTactics.Characters.BalanceApply",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOperativeBalanceApplyTest::RunTest(const FString&)
{
	UGodotBalanceAsset* Config = NewObject<UGodotBalanceAsset>();
	Config->Numbers = {
		{ TEXT("commander_max_health"), 140.f }, { TEXT("engineer_max_health"), 150.f }, { TEXT("medic_max_health"), 120.f },
		{ TEXT("engineer_matches_count"), 5.f }, { TEXT("commander_prep_radius"), 15.f },
		{ TEXT("anim_walk_speed"), 2.2f }, { TEXT("anim_run_speed"), 7.25f }, { TEXT("anim_crouch_speed"), 1.25f },
		{ TEXT("character_acceleration"), 14.f }, { TEXT("max_cold_to_sprint"), 55.f }, { TEXT("cold_accumulation_rate"), 0.3f } };

	AOperativeCharacter* Commander = NewObject<AOperativeCharacter>();
	Commander->SquadRole = EOperativeRole::Commander;
	OperativeBalance::Apply(*Config, *Commander);
	TestEqual(TEXT("Commander 140 HP"), Commander->HealthComponent->GetMaxHealth(), 140.f);
	TestEqual(TEXT("Prep radius 15 m"), Commander->PlacementRadius, 1500.f);
	TestEqual(TEXT("Walk 2.2 m/s"), Commander->MovementConfig.WalkSpeed, 220.f, 0.01f);
	TestEqual(TEXT("Run 7.25 m/s"), Commander->MovementConfig.RunSpeed, 725.f, 0.01f);
	TestEqual(TEXT("Crouch 1.25 m/s"), Commander->MovementConfig.CrouchSpeed, 125.f, 0.01f);
	TestEqual(TEXT("Acceleration 14 m/s2"), Commander->MovementConfig.Acceleration, 1400.f, 0.01f);
	TestEqual(TEXT("Sprint cold limit"), Commander->MovementConfig.MaxColdToSprint, 55.f);
	TestEqual(TEXT("Cold rate"), Commander->ColdSurvival->Config.AccumulationRate, 0.3f, 0.0001f);

	AOperativeCharacter* Engineer = NewObject<AOperativeCharacter>();
	Engineer->SquadRole = EOperativeRole::Engineer;
	OperativeBalance::Apply(*Config, *Engineer);
	TestEqual(TEXT("Engineer 150 HP"), Engineer->HealthComponent->GetMaxHealth(), 150.f);
	TestEqual(TEXT("Engineer matches"), Engineer->MatchesCount, 5);

	AOperativeCharacter* Medic = NewObject<AOperativeCharacter>();
	Medic->SquadRole = EOperativeRole::MedicSapper;
	OperativeBalance::Apply(*Config, *Medic);
	TestEqual(TEXT("Medic 120 HP"), Medic->HealthComponent->GetMaxHealth(), 120.f);
	TestEqual(TEXT("Missing key keeps the code default (matches 3)"), Medic->MatchesCount, 3);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
