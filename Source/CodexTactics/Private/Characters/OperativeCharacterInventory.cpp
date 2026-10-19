// AOperativeCharacter: Personal items, deployables, field aid.
// Split out of OperativeCharacter.cpp verbatim (cleanup day 2026-10-14; map: docs/port/REFACTOR_OPERATIVE_2026-10.md).
// Godot reference: Scenes/movements/player.gd (use_personal_item / heal_with_item, turrets_count / barricades_count /
// mines_count, selected_deployable_type). Field aid: Sprint 07-D (UE-only).

#include "Characters/OperativeCharacter.h"

#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Data/TacticalHeatTuning.h"
#include "Engine/World.h"
#include "Subsystems/CodexEventBus.h"
#include "Survival/TacticalHeatSubsystem.h"
#include "UI/FloatingTextSubsystem.h"

int32 AOperativeCharacter::GetItemCount(EPersonalItem Item) const
{
	switch (Item)
	{
	case EPersonalItem::Medkit: return MedkitsCount;
	case EPersonalItem::CannedFood: return CannedFoodCount;
	case EPersonalItem::Bread: return BreadCount;
	case EPersonalItem::HeatPack: return HeatPacksCount;
	default: return ChocolateCount;
	}
}

bool AOperativeCharacter::UsePersonalItem(EPersonalItem Item)
{
	if (Item == EPersonalItem::HeatPack)
	{
		// Sprint 15: on himself through the heat subsystem (turn-based AP, feed line, buffer).
		UTacticalHeatSubsystem* Heat = GetWorld() ? GetWorld()->GetSubsystem<UTacticalHeatSubsystem>() : nullptr;
		return Heat && Heat->UseHeatPack(this, this);
	}
	if (GetItemCount(Item) <= 0 || !HealthComponent
		|| !PersonalItemRules::CanUse(HealthComponent->GetCurrentHealth(), HealthComponent->GetMaxHealth(), ColdLevel))
	{
		return false;
	}
	const FPersonalItemEffect Effect = PersonalItemRules::GetEffect(Item);
	const float HealthBefore = HealthComponent->GetCurrentHealth();
	HealthComponent->Heal(Effect.Heal);
	const int32 Gained = FMath::FloorToInt(HealthComponent->GetCurrentHealth() - HealthBefore);
	if (UFloatingTextSubsystem* Floating = Gained > 0 && GetWorld() ? GetWorld()->GetSubsystem<UFloatingTextSubsystem>() : nullptr)
	{
		Floating->Spawn(GetActorLocation() - FVector(0.f, 0.f, GetSimpleCollisionHalfHeight() - 210.f), FString::Printf(TEXT("+%d HP"), Gained),
			FLinearColor(0.2f, 1.f, 0.4f), 0.9f, 70.f);
	}
	ColdLevel = FMath::Max(0.f, ColdLevel - Effect.Warmth);
	int32& Count = Item == EPersonalItem::Medkit ? MedkitsCount
		: (Item == EPersonalItem::CannedFood ? CannedFoodCount : (Item == EPersonalItem::Bread ? BreadCount : ChocolateCount));
	--Count;
	// Godot inventory_drawer.gd item_used: raised here, where the item really was used (H / J / K / L, the drawer line, the bot) - it
	// used to come only from the drawer, also when the use was refused (rev 2026-10-19, the medkit lesson completes on it).
	if (UCodexEventBus* Bus = UCodexEventBus::Get(this))
	{
		Bus->OnItemUsed.Broadcast(this, PersonalItemRules::GetEventId(Item));
	}
	return true;
}

int32 AOperativeCharacter::GetDeployableCount(EDeployableType Type) const
{
	switch (Type)
	{
	case EDeployableType::Turret: return TurretsCount;
	case EDeployableType::Barricade: return BarricadesCount;
	case EDeployableType::Mine: return MinesCount;
	case EDeployableType::PortableStove: return PortableStovesCount;
	default: return TacticalHeatRules::GetCraftableCampfires(TacticalHeatTuning::Get(), MatchesCount, FirewoodCount, GetReserve(TEXT("flamethrower")));
	}
}

void AOperativeCharacter::AddDeployable(EDeployableType Type, int32 Delta)
{
	if (Type == EDeployableType::Campfire)
	{
		// Sprint 15: a campfire is its materials.
		const FTacticalHeatConfig& Heat = TacticalHeatTuning::Get();
		if (Delta > 0)
		{
			MatchesCount += Delta * Heat.CampfireMatchesCost;
			FirewoodCount += Delta * FMath::Max(1, Heat.CampfireFirewoodCost);
		}
		for (int32 Index = 0; Index < -Delta; ++Index)
		{
			int32 Fuel = GetReserve(TEXT("flamethrower"));
			const int32 FuelBefore = Fuel;
			if (!TacticalHeatRules::ConsumeCampfireMaterials(Heat, MatchesCount, FirewoodCount, Fuel))
			{
				break;
			}
			if (Fuel < FuelBefore)
			{
				TakeReserve(TEXT("flamethrower"), FuelBefore - Fuel);
			}
		}
		return;
	}
	int32& Count = Type == EDeployableType::Turret ? TurretsCount : (Type == EDeployableType::Barricade ? BarricadesCount
		: (Type == EDeployableType::PortableStove ? PortableStovesCount : MinesCount));
	Count = FMath::Clamp(Count + Delta, 0, DeployableRules::GetMaxCarried(Type));
	// A stove's gas (StoveGasSeconds) moves with it: APortableStoveActor::OnDeployedBy takes it, MoveDeployableTo hands it over.
}

bool AOperativeCharacter::MoveDeployableTo(AOperativeCharacter& To, EDeployableType Type)
{
	if (&To == this || GetDeployableCount(Type) <= 0 || To.GetDeployableCount(Type) >= DeployableRules::GetMaxCarried(Type))
	{
		return false;
	}
	const float Gas = StoveGasSeconds;
	AddDeployable(Type, -1);
	To.AddDeployable(Type, 1);
	if (Type == EDeployableType::PortableStove)
	{
		To.StoveGasSeconds = Gas; // max one stove each: the recipient had none
		StoveGasSeconds = GetDeployableCount(Type) > 0 ? StoveGasSeconds : 0.f;
	}
	return true;
}

EDeployableType AOperativeCharacter::CycleDeployableType()
{
	for (int32 Step = 1; Step <= DeployableTypeCount; ++Step)
	{
		const EDeployableType Next = static_cast<EDeployableType>((static_cast<int32>(SelectedDeployType) + Step) % DeployableTypeCount);
		if (GetDeployableCount(Next) > 0 || Step == DeployableTypeCount)
		{
			SelectedDeployType = Next;
			break;
		}
	}
	return SelectedDeployType;
}

bool AOperativeCharacter::HealAlly(AOperativeCharacter& Patient)
{
	UHealthComponent* PatientHealth = Patient.HealthComponent;
	if (MedkitsCount <= 0 || &Patient == this || !PatientHealth || !PatientHealth->IsAlive()
		|| PatientHealth->GetCurrentHealth() >= PatientHealth->GetMaxHealth()
		|| FVector::Dist2D(GetActorLocation(), Patient.GetActorLocation()) > AidReachCm)
	{
		return false;
	}
	const float HealthBefore = PatientHealth->GetCurrentHealth();
	PatientHealth->Heal(PersonalItemRules::GetEffect(EPersonalItem::Medkit).Heal);
	--MedkitsCount;
	const int32 Gained = FMath::FloorToInt(PatientHealth->GetCurrentHealth() - HealthBefore);
	if (UFloatingTextSubsystem* Floating = GetWorld() ? GetWorld()->GetSubsystem<UFloatingTextSubsystem>() : nullptr)
	{
		Floating->Spawn(Patient.GetActorLocation() - FVector(0.f, 0.f, Patient.GetSimpleCollisionHalfHeight() - 210.f),
			FString::Printf(TEXT("🩹 +%d HP"), Gained), FLinearColor(0.2f, 1.f, 0.4f), 0.9f, 70.f);
	}
	UE_LOG(LogCodexTactics, Display, TEXT("%s gives first aid to %s: +%d HP (%d medkits left)"), *DisplayName.ToString(),
		*Patient.DisplayName.ToString(), Gained, MedkitsCount);
	if (UCodexEventBus* Bus = UCodexEventBus::Get(this))
	{
		Bus->OnItemUsed.Broadcast(&Patient, PersonalItemRules::GetEventId(EPersonalItem::Medkit)); // a medkit healed him
	}
	return true;
}
