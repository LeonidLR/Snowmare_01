// Nav areas of the generated drop links (game designer items 6 / 10 / 11a, 2026-11-03). UE-only, no Godot reference.

#include "Falls/DropLinkNavigation.h"

#include "AI/Navigation/NavQueryFilter.h"
#include "NavigationData.h"

UNavArea_Drop::UNavArea_Drop()
{
	// PENDING confirmation: like a ladder per cm (3) plus 10 m on entering - a stair / ramp / ladder of a similar length wins.
	DefaultCost = 3.f;
	FixedAreaEnteringCost = 1000.f;
	DrawColor = FColor(255, 170, 40);
}

UNavArea_DropHurt::UNavArea_DropHurt()
{
	// A drop that hurts: taken only when the way round is some 25 m longer.
	DefaultCost = 3.f;
	FixedAreaEnteringCost = 2500.f;
	DrawColor = FColor(255, 60, 40);
}

// --- The enemies' drop filters (user decision 2026-11-03) ----------------------------------------------------------------------

namespace DropLinkNavigation
{
	void OpenDropAreas(TArray<FNavigationFilterArea>& Areas)
	{
		Areas.RemoveAll([](const FNavigationFilterArea& Area)
		{
			return Area.AreaClass == UNavArea_Drop::StaticClass() || Area.AreaClass == UNavArea_DropHurt::StaticClass();
		});
	}

	void ApplyFreeDropCosts(const ANavigationData& NavData, FNavigationQueryFilter& Filter)
	{
		const int32 AreaId = NavData.GetAreaID(UNavArea_DropHurt::StaticClass());
		if (AreaId >= 0 && AreaId < 255)
		{
			Filter.SetFixedAreaEnteringCost(static_cast<uint8>(AreaId), FMath::Max(0.f, FallRules::GetConfig().EnemyHurtDropEnteringCm));
		}
	}

	TSubclassOf<UNavigationQueryFilter> EnemyFilterClass(FallRules::EEnemyDropUse Use, bool bClimbsLadders)
	{
		switch (Use)
		{
		case FallRules::EEnemyDropUse::Free:
			return bClimbsLadders ? UNavFilter_EnemyDropFreeLadder::StaticClass() : UNavFilter_EnemyDropFree::StaticClass();
		case FallRules::EEnemyDropUse::Jumper:
			return bClimbsLadders ? UNavFilter_EnemyDropJumperLadder::StaticClass() : UNavFilter_EnemyDropJumper::StaticClass();
		default:
			return bClimbsLadders ? UNavFilter_NoVaultLadder::StaticClass() : UNavFilter_NoVault::StaticClass();
		}
	}
}

UNavFilter_EnemyDropFree::UNavFilter_EnemyDropFree()
{
	DropLinkNavigation::OpenDropAreas(Areas);
}

void UNavFilter_EnemyDropFree::InitializeFilter(const ANavigationData& NavData, const UObject* Querier, FNavigationQueryFilter& Filter) const
{
	Super::InitializeFilter(NavData, Querier, Filter);
	DropLinkNavigation::ApplyFreeDropCosts(NavData, Filter);
}

UNavFilter_EnemyDropFreeLadder::UNavFilter_EnemyDropFreeLadder()
{
	DropLinkNavigation::OpenDropAreas(Areas);
}

void UNavFilter_EnemyDropFreeLadder::InitializeFilter(const ANavigationData& NavData, const UObject* Querier, FNavigationQueryFilter& Filter) const
{
	Super::InitializeFilter(NavData, Querier, Filter);
	DropLinkNavigation::ApplyFreeDropCosts(NavData, Filter);
}

UNavFilter_EnemyDropJumper::UNavFilter_EnemyDropJumper()
{
	DropLinkNavigation::OpenDropAreas(Areas);
}

UNavFilter_EnemyDropJumperLadder::UNavFilter_EnemyDropJumperLadder()
{
	DropLinkNavigation::OpenDropAreas(Areas);
}
