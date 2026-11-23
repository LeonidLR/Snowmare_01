// PMC-owned level charges: the nav area and the PMC fighters' filters (smart-enemies T-11 second half). UE-only, no Godot reference.

#include "AI/PmcEncounterNav.h"

#include "GameFramework/Actor.h"
#include "Interactables/DeployableRules.h"
#include "Interactables/MantleNavigation.h"
#include "Interactables/VaultNavigation.h"
#include "NavModifierComponent.h"

UNavArea_PmcCharge::UNavArea_PmcCharge()
{
	DefaultCost = 1.f; // the area itself changes nothing; only the PMC filters price it
	DrawColor = FColor(200, 40, 40);
}

namespace PmcEncounterNavLocal
{
	void AvoidOwnCharges(TArray<FNavigationFilterArea>& Areas)
	{
		FNavigationFilterArea& Charge = Areas.AddDefaulted_GetRef();
		Charge.AreaClass = UNavArea_PmcCharge::StaticClass();
		Charge.bOverrideTravelCost = true;
		Charge.TravelCostOverride = PmcEncounterNav::OwnChargeTravelCost;
		Charge.bOverrideEnteringCost = true;
		Charge.EnteringCostOverride = PmcEncounterNav::OwnChargeTravelCost;
	}
}

// Fable review 2026-11-23: no PMC fighter paths through a whole window (UNavArea_WindowVault excluded) - breaking one is lure noise and a
// runtime navmesh re-area mid-arena (determinism of the enemy-AI A/B). A broken window is an ordinary vault area for them too.
UNavFilter_PmcFighter::UNavFilter_PmcFighter()
{
	PmcEncounterNavLocal::AvoidOwnCharges(Areas);
	VaultNavigation::CloseWindowArea(Areas);
}

UNavFilter_PmcFighterNoMantle::UNavFilter_PmcFighterNoMantle()
{
	MantleNavigation::CloseMantleArea(Areas, false); // the window area: inherited (UNavFilter_PmcFighter)
}

UNavFilter_PmcFighterProne::UNavFilter_PmcFighterProne()
{
	PmcEncounterNavLocal::AvoidOwnCharges(Areas);
	VaultNavigation::CloseWindowArea(Areas);
}

UNavFilter_PmcFighterProneNoMantle::UNavFilter_PmcFighterProneNoMantle()
{
	MantleNavigation::CloseMantleArea(Areas, false); // the window area: inherited (UNavFilter_PmcFighterProne)
}

TSubclassOf<UNavigationQueryFilter> PmcEncounterNav::FilterFor(bool bProne, bool bMantleOpen)
{
	if (bProne)
	{
		return bMantleOpen ? UNavFilter_PmcFighterProne::StaticClass() : UNavFilter_PmcFighterProneNoMantle::StaticClass();
	}
	return bMantleOpen ? UNavFilter_PmcFighter::StaticClass() : UNavFilter_PmcFighterNoMantle::StaticClass();
}

void PmcEncounterNav::AdoptCharge(AActor& Charge, const FVector& FailsafeExtent)
{
	if (DeployableRules::IsPmcOwned(Charge))
	{
		return; // already adopted
	}
	Charge.Tags.AddUnique(DeployableRules::PmcOwnerTag());
	{
		UNavModifierComponent* Modifier = NewObject<UNavModifierComponent>(&Charge);
		Modifier->SetAreaClass(UNavArea_PmcCharge::StaticClass());
		// The charge's shapes are no navigation geometry: the modifier uses its failsafe box.
		Modifier->FailsafeExtent = FailsafeExtent;
		Modifier->RegisterComponent();
	}
}
