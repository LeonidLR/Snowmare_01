#pragma once

#include "CoreMinimal.h"
#include "Characters/OperativeMovementRules.h"
#include "DeployableRules.generated.h"

/** Squad role (defusal skill, deployable routing). Godot: character_name «Командир» / «Инженер» / «Медик-сапёр». */
UENUM(BlueprintType)
enum class EOperativeRole : uint8
{
	Commander,
	Engineer,
	MedicSapper,
	/** Civilian recruit (Godot recruit_susanin.gd «Иван Сусанин»; balance prefix susanin_, defusal base 35). */
	Recruit
};

/** Engineering items an operative carries and sets up. Godot: "TURRET" / "BARRICADE" / "MINE". */
UENUM(BlueprintType)
enum class EDeployableType : uint8
{
	Turret,
	Barricade,
	Mine
};

/** Outcome of one defusal attempt. Godot attempt_defusal result: SUCCESS / WARNING / FAILURE / DETONATION. */
enum class EDefusalResult : uint8
{
	Success,
	Warning,
	Failure,
	Detonation
};

/** Defusal odds of one operative on one trap. */
struct CODEXTACTICS_API FDefusalChance
{
	float Chance = 0.f;
	bool bDangerous = false;
	float StanceBonus = 0.f;
	float ColdPenalty = 0.f;
};

/**
 * Traps, defusal and explosions of deployables and trapped objects.
 * Godot reference: deployables/mine.gd, deployables/barricade.gd, interactable.gd (calculate_defusal_chance,
 * attempt_defusal, calculate_placement_mishap_chance, detonate falloffs), player.gd inventory limits.
 */
namespace DeployableRules
{
	/** Role base chance: commander 35, engineer 45, medic-sapper 60. */
	CODEXTACTICS_API float GetRoleDefusalBase(EOperativeRole Role);

	/** base + stance (standing -15, crouching +10, prone +25) + luck * 0.5 - cold (>25: 15, >50: 30, >75: 50)
	 *  - 25 per failed attempt, clamped 1..99; dangerous below 45 % or after a failure / warning. */
	CODEXTACTICS_API FDefusalChance CalculateDefusal(EOperativeRole Role, EOperativeStance Stance, float Luck, float ColdLevel,
		int32 FailedAttempts, bool bWarned);

	/**
	 * Resolves an attempt with rolls in 0..100. A dangerous first attempt only warns (sets bInOutWarned).
	 * A failure detonates with 80 % risk (after a warning / second failure / chance < 40) or 40 % otherwise.
	 */
	CODEXTACTICS_API EDefusalResult ResolveDefusal(const FDefusalChance& Chance, bool& bInOutWarned, int32& InOutFailedAttempts,
		float Roll, float ExplosionRoll);

	/** Mine set-up mishap: 2 % for the sapper, 10 % otherwise, + cold / 100 * 20, clamped 0..95. */
	CODEXTACTICS_API float GetMineMishapChance(bool bSapper, float ColdLevel);

	/** Blast damage at Distance: Damage * (1 - Distance / Radius * Falloff); 0 outside the radius. */
	CODEXTACTICS_API float GetBlastDamage(float Damage, float Distance, float Radius, float Falloff);

	/** Godot player.gd max_turrets 2, max_barricades 4, max_mines 5. */
	CODEXTACTICS_API int32 GetMaxCarried(EDeployableType Type);

	/**
	 * Godot _auto_recover_all_deployables: the first member (in the given order) carrying fewer than the maximum gets
	 * the recovered item; INDEX_NONE when all are full (the caller then gives it to the type's owner anyway).
	 */
	CODEXTACTICS_API int32 PickRecoveryRecipient(const TArray<int32>& CarriedInOrder, int32 MaxCarried);

	/** «Стоя» / «Присев» / «Лёжа» (Godot defusal stance names). */
	CODEXTACTICS_API FText GetDefusalStanceName(EOperativeStance Stance);

	/** Enemy / squad falloff factors (Godot: 0.4 for enemies, 0.5 for the squad; the squad takes 75 %). */
	constexpr float EnemyFalloff = 0.4f;
	constexpr float SquadFalloff = 0.5f;
	constexpr float SquadDamageScale = 0.75f;
}
