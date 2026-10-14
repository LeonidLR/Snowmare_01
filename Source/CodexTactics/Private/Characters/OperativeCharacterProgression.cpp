// AOperativeCharacter: Levels, EXP and stat points.
// Split out of OperativeCharacter.cpp verbatim (cleanup day 2026-10-14; map: docs/port/REFACTOR_OPERATIVE_2026-10.md).
// Godot reference: Scenes/movements/player.gd (add_exp, get_next_level_exp, increase / decrease stat, initial_base_*).

#include "Characters/OperativeCharacter.h"

#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Engine/World.h"
#include "Survival/ColdSurvivalComponent.h"
#include "UI/FloatingTextSubsystem.h"
#include "UI/GameMessageSubsystem.h"

int32 AOperativeCharacter::GetNextLevelExp() const
{
	return ProgressionRules::NextLevelExp(Level);
}

void AOperativeCharacter::AddExp(int32 Amount)
{
	const int32 FirstLevel = Level;
	const int32 Gained = ProgressionRules::AddExp(Level, CurrentExp, Amount);
	for (int32 NewLevel = FirstLevel + 1; NewLevel <= FirstLevel + Gained; ++NewLevel)
	{
		// Godot _on_level_up: +3 points, full heal, floating "⭐ LEVEL N!", radio line.
		UnspentStatPoints += ProgressionRules::PointsPerLevel;
		if (HealthComponent)
		{
			HealthComponent->Heal(HealthComponent->GetMaxHealth() - HealthComponent->GetCurrentHealth());
		}
		UFloatingTextSubsystem::SpawnAboveOperative(this, FString::Printf(TEXT("⭐ LEVEL %d!"), NewLevel), FLinearColor(1.f, 0.85f, 0.1f));
		if (UGameMessageSubsystem* Messages = GetWorld() ? GetWorld()->GetSubsystem<UGameMessageSubsystem>() : nullptr)
		{
			Messages->PostMessage(DisplayName, FText::FromString(FString::Printf(
				TEXT("⭐ NEW LEVEL %d! +3 attribute points to assign!"), NewLevel)));
		}
		UE_LOG(LogCodexTactics, Log, TEXT("%s reached level %d"), *DisplayName.ToString(), NewLevel);
	}
}

float AOperativeCharacter::GetStatValue(EProgressStat Stat) const
{
	switch (Stat)
	{
	case EProgressStat::Health: return HealthComponent ? HealthComponent->GetMaxHealth() : 100.f;
	case EProgressStat::Luck: return Luck;
	case EProgressStat::Accuracy: return Accuracy;
	default: return ColdSurvival ? ColdSurvival->Fortitude : 15.f;
	}
}

bool AOperativeCharacter::CanIncreaseStat(EProgressStat Stat) const
{
	return ProgressionRules::CanIncrease(Stat, GetStatValue(Stat), UnspentStatPoints);
}

bool AOperativeCharacter::CanDecreaseStat(EProgressStat Stat) const
{
	const float Base = Stat == EProgressStat::Health ? InitialBaseHealth
		: (Stat == EProgressStat::Luck ? InitialBaseLuck : (Stat == EProgressStat::Accuracy ? InitialBaseAccuracy : InitialBaseFortitude));
	return ProgressionRules::CanDecrease(Stat, GetStatValue(Stat), Base);
}

bool AOperativeCharacter::IncreaseStat(EProgressStat Stat)
{
	if (!CanIncreaseStat(Stat))
	{
		return false;
	}
	const float Step = ProgressionRules::StatStep(Stat);
	switch (Stat)
	{
	case EProgressStat::Health:
		if (HealthComponent)
		{
			HealthComponent->SetMaxHealth(HealthComponent->GetMaxHealth() + Step, false);
			HealthComponent->Heal(Step);
		}
		break;
	case EProgressStat::Luck: Luck += Step; break;
	case EProgressStat::Accuracy: Accuracy += Step; break;
	default:
		if (ColdSurvival)
		{
			ColdSurvival->Fortitude += Step;
		}
		break;
	}
	--UnspentStatPoints;
	return true;
}

bool AOperativeCharacter::DecreaseStat(EProgressStat Stat)
{
	if (!CanDecreaseStat(Stat))
	{
		return false;
	}
	const float Step = ProgressionRules::StatStep(Stat);
	switch (Stat)
	{
	case EProgressStat::Health:
		if (HealthComponent)
		{
			HealthComponent->SetMaxHealth(HealthComponent->GetMaxHealth() - Step, false);
		}
		break;
	case EProgressStat::Luck: Luck -= Step; break;
	case EProgressStat::Accuracy: Accuracy -= Step; break;
	default:
		if (ColdSurvival)
		{
			ColdSurvival->Fortitude -= Step;
		}
		break;
	}
	++UnspentStatPoints;
	return true;
}

void AOperativeCharacter::CaptureProgressionBases()
{
	InitialBaseHealth = GetStatValue(EProgressStat::Health);
	InitialBaseLuck = Luck;
	InitialBaseAccuracy = Accuracy;
	InitialBaseFortitude = GetStatValue(EProgressStat::Fortitude);
}
