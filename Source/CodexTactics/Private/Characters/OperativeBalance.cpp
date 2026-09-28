#include "Characters/OperativeBalance.h"
#include "Characters/OperativeCharacter.h"
#include "Combat/HealthComponent.h"
#include "Data/GodotBalanceAsset.h"

void OperativeBalance::Apply(const UGodotBalanceAsset& Config, AOperativeCharacter& Operative)
{
	const TCHAR* Prefix = Operative.SquadRole == EOperativeRole::Engineer ? TEXT("engineer_")
		: (Operative.SquadRole == EOperativeRole::MedicSapper ? TEXT("medic_") : TEXT("commander_"));
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
	Operative.MaxColdToLiftObjects = Config.GetNumber(TEXT("max_cold_to_lift_objects"), Operative.MaxColdToLiftObjects);
	Operative.MinHealthFractionToLift = Config.GetNumber(TEXT("min_health_percent_to_lift"), Operative.MinHealthFractionToLift);
}
