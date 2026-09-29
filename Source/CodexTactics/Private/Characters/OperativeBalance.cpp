#include "Characters/OperativeBalance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/RageComponent.h"
#include "Combat/HealthComponent.h"
#include "Data/GodotBalanceAsset.h"
#include "Survival/ColdSurvivalComponent.h"

void OperativeBalance::Apply(const UGodotBalanceAsset& Config, AOperativeCharacter& Operative)
{
	const TCHAR* Prefix = Operative.SquadRole == EOperativeRole::Engineer ? TEXT("engineer_")
		: (Operative.SquadRole == EOperativeRole::MedicSapper ? TEXT("medic_")
		: (Operative.SquadRole == EOperativeRole::Recruit ? TEXT("susanin_") : TEXT("commander_")));
	auto Key = [Prefix](const TCHAR* Name) { return FName(FString(Prefix) + Name); };

	if (Operative.HealthComponent)
	{
		const float MaxHealth = Config.GetNumber(Key(TEXT("max_health")), Operative.HealthComponent->GetMaxHealth());
		Operative.HealthComponent->SetMaxHealth(MaxHealth, true);
	}
	Operative.MatchesCount = Config.GetInt(Key(TEXT("matches_count")), Operative.MatchesCount);
	if (Operative.SquadRole == EOperativeRole::Engineer)
	{
		Operative.BarricadesCount = Config.GetInt(TEXT("engineer_barricades_count"), Operative.BarricadesCount);
	}
	else if (Operative.SquadRole == EOperativeRole::MedicSapper)
	{
		Operative.MinesCount = Config.GetInt(TEXT("medic_mines_count"), Operative.MinesCount);
	}
	else if (Operative.SquadRole == EOperativeRole::Recruit)
	{
		// Godot recruit_susanin.gd apply_balance_config: accuracy, luck, fortitude from susanin_* (health above).
		Operative.Accuracy = Config.GetNumber(TEXT("susanin_accuracy"), Operative.Accuracy);
		Operative.Luck = Config.GetNumber(TEXT("susanin_luck"), Operative.Luck);
		if (Operative.ColdSurvival)
		{
			Operative.ColdSurvival->Fortitude = Config.GetNumber(TEXT("susanin_fortitude"), Operative.ColdSurvival->Fortitude);
		}
	}
	else
	{
		Operative.PlacementRadius = Config.GetNumber(TEXT("commander_prep_radius"), Operative.PlacementRadius / 100.f) * 100.f;
	}

	// Godot: speed = anim_walk_speed; sprint / crouch multipliers are the anim run / crouch speeds over it.
	FOperativeMovementConfig& Movement = Operative.MovementConfig;
	const float Walk = Config.GetNumber(TEXT("anim_walk_speed"), 0.f);
	if (Walk > 0.f)
	{
		Movement.WalkSpeed = Walk * 100.f;
		const float Run = Config.GetNumber(TEXT("anim_run_speed"), 0.f);
		const float Crouch = Config.GetNumber(TEXT("anim_crouch_speed"), 0.f);
		if (Run > 0.f)
		{
			Movement.RunSpeed = Run * 100.f;
		}
		if (Crouch > 0.f)
		{
			Movement.CrouchSpeed = Crouch * 100.f;
		}
	}
	Movement.MaxColdToSprint = Config.GetNumber(TEXT("max_cold_to_sprint"), Movement.MaxColdToSprint);
	Movement.CarryingSpeedMultiplier = Config.GetNumber(TEXT("carrying_speed_multiplier"), Movement.CarryingSpeedMultiplier);
	Movement.WoundedSpeedMultiplier = Config.GetNumber(TEXT("wounded_speed_multiplier"), Movement.WoundedSpeedMultiplier);
	Movement.Acceleration = Config.GetNumber(TEXT("character_acceleration"), Movement.Acceleration / 100.f) * 100.f;
	Movement.Deceleration = Config.GetNumber(TEXT("character_deceleration"), Movement.Deceleration / 100.f) * 100.f;
	if (UColdSurvivalComponent* Cold = Operative.ColdSurvival)
	{
		FColdConfig& Rules = Cold->Config;
		Rules.AccumulationRate = Config.GetNumber(TEXT("cold_accumulation_rate"), Rules.AccumulationRate);
		Rules.WarmthRecoveryRate = Config.GetNumber(TEXT("cold_warmth_recovery_rate"), Rules.WarmthRecoveryRate);
		Rules.StanceMultiplierStanding = Config.GetNumber(TEXT("stance_cold_multiplier_standing"), Rules.StanceMultiplierStanding);
		Rules.StanceMultiplierCrouching = Config.GetNumber(TEXT("stance_cold_multiplier_crouching"), Rules.StanceMultiplierCrouching);
		Rules.StanceMultiplierProne = Config.GetNumber(TEXT("stance_cold_multiplier_prone"), Rules.StanceMultiplierProne);
		Rules.MisfireThreshold = Config.GetNumber(TEXT("realtime_cold_misfire_threshold"), Rules.MisfireThreshold);
		Rules.MisfireMaxChance = Config.GetNumber(TEXT("realtime_cold_misfire_max_chance"), Rules.MisfireMaxChance);
		Rules.WeaponFreezeThreshold = Config.GetNumber(TEXT("realtime_cold_weapon_freeze_threshold"), Rules.WeaponFreezeThreshold);
		Rules.MisfireDelay = Config.GetNumber(TEXT("realtime_cold_misfire_delay"), Rules.MisfireDelay);
		Rules.AimPenaltyMax = Config.GetNumber(TEXT("realtime_cold_aim_penalty_max"), Rules.AimPenaltyMax);
	}
	Operative.FireConfig = SquadFireRules::ConfigFromBalance(&Config);
	if (Operative.RageComponent)
	{
		// Godot: the squad gets its own <role>_rage_* keys from main.gd; a recruit spawned later keeps the general ones.
		Operative.RageComponent->Config = RageRules::ConfigFromBalance(&Config, Operative.SquadRole == EOperativeRole::Recruit ? FString()
			: FString(Prefix).LeftChop(1));
	}
	Operative.MaxColdToLiftObjects = Config.GetNumber(TEXT("max_cold_to_lift_objects"), Operative.MaxColdToLiftObjects);
	Operative.MinHealthFractionToLift = Config.GetNumber(TEXT("min_health_percent_to_lift"), Operative.MinHealthFractionToLift);
}
