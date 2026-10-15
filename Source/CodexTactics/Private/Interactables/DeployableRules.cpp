#include "Interactables/DeployableRules.h"
#include "Data/TacticalHeatTuning.h"

#define LOCTEXT_NAMESPACE "DeployableRules"

float DeployableRules::GetRoleDefusalBase(EOperativeRole Role)
{
	switch (Role)
	{
	case EOperativeRole::MedicSapper: return 60.f;
	case EOperativeRole::Engineer: return 45.f;
	default: return 35.f;
	}
}

FDefusalChance DeployableRules::CalculateDefusal(EOperativeRole Role, EOperativeStance Stance, float Luck, float ColdLevel,
	int32 FailedAttempts, bool bWarned)
{
	FDefusalChance Result;
	Result.StanceBonus = Stance == EOperativeStance::Prone ? 25.f : (Stance == EOperativeStance::Crouching ? 10.f : -15.f);
	Result.ColdPenalty = ColdLevel > 75.f ? 50.f : (ColdLevel > 50.f ? 30.f : (ColdLevel > 25.f ? 15.f : 0.f));
	const float Raw = GetRoleDefusalBase(Role) + Result.StanceBonus + Luck * 0.5f - Result.ColdPenalty - FailedAttempts * 25.f;
	Result.Chance = FMath::Clamp(Raw, 1.f, 99.f);
	Result.bDangerous = Result.Chance < 45.f || FailedAttempts > 0 || bWarned;
	return Result;
}

EDefusalResult DeployableRules::ResolveDefusal(const FDefusalChance& Chance, bool& bInOutWarned, int32& InOutFailedAttempts,
	float Roll, float ExplosionRoll)
{
	if (Chance.bDangerous && !bInOutWarned && InOutFailedAttempts == 0)
	{
		bInOutWarned = true;
		return EDefusalResult::Warning;
	}
	if (Roll <= Chance.Chance)
	{
		return EDefusalResult::Success;
	}
	++InOutFailedAttempts;
	const float Risk = (bInOutWarned || InOutFailedAttempts > 1 || Chance.Chance < 40.f) ? 80.f : 40.f;
	return ExplosionRoll <= Risk ? EDefusalResult::Detonation : EDefusalResult::Failure;
}

float DeployableRules::GetSpotRangeCm(float BaseCm, float SapperBonusCm, EOperativeRole Role)
{
	return BaseCm + (Role == EOperativeRole::MedicSapper ? SapperBonusCm : 0.f);
}

float DeployableRules::GetMineMishapChance(bool bSapper, float ColdLevel)
{
	return FMath::Clamp((bSapper ? 2.f : 10.f) + ColdLevel / 100.f * 20.f, 0.f, 95.f);
}

float DeployableRules::GetBlastDamage(float Damage, float Distance, float Radius, float Falloff)
{
	if (Radius <= 0.f || Distance > Radius)
	{
		return 0.f;
	}
	return Damage * (1.f - Distance / Radius * Falloff);
}

int32 DeployableRules::GetMaxCarried(EDeployableType Type)
{
	switch (Type)
	{
	case EDeployableType::Turret: return 2;
	case EDeployableType::Barricade: return 4;
	case EDeployableType::Mine: return 5;
	case EDeployableType::PortableStove: return FMath::Max(1, TacticalHeatTuning::Get().StoveMaxCarried);
	default: return MAX_int32;
	}
}

bool DeployableRules::IsEngineeringType(EDeployableType Type)
{
	return Type == EDeployableType::Turret || Type == EDeployableType::Barricade || Type == EDeployableType::Mine;
}

int32 DeployableRules::PickRecoveryRecipient(const TArray<int32>& CarriedInOrder, int32 MaxCarried)
{
	for (int32 Index = 0; Index < CarriedInOrder.Num(); ++Index)
	{
		if (CarriedInOrder[Index] >= 0 && CarriedInOrder[Index] < MaxCarried)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

float DeployableRules::StepArmingCountdown(float SecondsLeft, float DeltaSeconds)
{
	return FMath::Max(0.f, SecondsLeft - FMath::Max(0.f, DeltaSeconds));
}

FText DeployableRules::GetDefusalStanceName(EOperativeStance Stance)
{
	switch (Stance)
	{
	case EOperativeStance::Crouching: return LOCTEXT("Crouch", "Crouched");
	case EOperativeStance::Prone: return LOCTEXT("Prone", "Prone");
	default: return LOCTEXT("Stand", "Standing");
	}
}

#undef LOCTEXT_NAMESPACE
