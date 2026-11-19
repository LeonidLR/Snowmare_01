#include "Misc/AutomationTest.h"
#include "AI/PmcEncounterNav.h"
#include "Dom/JsonValue.h"
#include "Interactables/DeployableRules.h"
#include "LevelGen/LevelPropsTypes.h"
#include "LevelGen/PropsGameplayRules.h"
#include "NavFilters/NavigationQueryFilter.h"

#if WITH_DEV_AUTOMATION_TESTS

// Smart-enemies T-11 second half (PMC_DEFENCE D2, OQ-21): the charges of the generator's pmc_charges layer belong to the PMC.
// Pure rules: who owns an object, whom an owned charge ignores, which nav filters price the owned area.

namespace PmcOwnedChargesTestLocal
{
	FLevelPropsObject ObjectWithOwner(const TCHAR* Owner)
	{
		FLevelPropsObject Object;
		Object.Id = TEXT("pmc_charges_PMC_01_0");
		Object.Variant = TEXT("mine_proximity");
		if (Owner)
		{
			Object.Extra.Add(TEXT("owner"), MakeShared<FJsonValueString>(Owner));
		}
		return Object;
	}

	/** The override this filter class holds for an area class (nullptr = none). */
	const FNavigationFilterArea* AreaOverride(const UNavigationQueryFilter& Filter, const TSubclassOf<UNavArea>& Area)
	{
		return Filter.Areas.FindByPredicate([&Area](const FNavigationFilterArea& Entry) { return Entry.AreaClass == Area; });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcOwnedChargesOwnershipTest, "CodexTactics.AI.PmcEncounter.OwnedChargesOwnership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcOwnedChargesOwnershipTest::RunTest(const FString&)
{
	using namespace PmcOwnedChargesTestLocal;
	// "owner": "pmc" is the generator's key (087: every pmc_charges object carries it); case and spaces do not matter.
	TestTrue(TEXT("owner pmc"), PropsGameplayRules::IsPmcOwned(TEXT("pmc_charges"), ObjectWithOwner(TEXT("pmc"))));
	TestTrue(TEXT("owner PMC in another layer"), PropsGameplayRules::IsPmcOwned(TEXT("mines"), ObjectWithOwner(TEXT(" PMC "))));
	// No owner key: the pmc_charges layer is the PMC's, any other layer is an ordinary level trap.
	TestTrue(TEXT("pmc_charges layer without the key"), PropsGameplayRules::IsPmcOwned(TEXT("pmc_charges"), ObjectWithOwner(nullptr)));
	TestFalse(TEXT("an ordinary mines layer"), PropsGameplayRules::IsPmcOwned(TEXT("mines"), ObjectWithOwner(nullptr)));
	TestFalse(TEXT("another owner wins over the layer"), PropsGameplayRules::IsPmcOwned(TEXT("pmc_charges"), ObjectWithOwner(TEXT("squad"))));
	TestFalse(TEXT("another owner"), PropsGameplayRules::IsPmcOwned(TEXT("mines"), ObjectWithOwner(TEXT("squad"))));

	// An owned charge ignores the PMC fighters only; the player's men and everything else still set it off.
	TestTrue(TEXT("owned charge, PMC fighter: ignored"), DeployableRules::IgnoresBody(true, true));
	TestFalse(TEXT("owned charge, the player's side: triggers"), DeployableRules::IgnoresBody(true, false));
	TestFalse(TEXT("ordinary charge, PMC fighter: unchanged (triggers)"), DeployableRules::IgnoresBody(false, true));
	TestFalse(TEXT("ordinary charge, the player's side: unchanged"), DeployableRules::IgnoresBody(false, false));
	TestEqual(TEXT("the owner tag"), DeployableRules::PmcOwnerTag(), FName(TEXT("trap_owner=pmc")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcOwnedChargesNavTest, "CodexTactics.AI.PmcEncounter.OwnedChargesNav",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcOwnedChargesNavTest::RunTest(const FString&)
{
	using namespace PmcOwnedChargesTestLocal;
	const TSubclassOf<UNavArea> OwnArea = UNavArea_PmcCharge::StaticClass();

	// The area changes nothing by itself: the player's squad is not steered round charges it cannot see.
	TestEqual(TEXT("the area's own cost is the plain 1"), GetDefault<UNavArea_PmcCharge>()->DefaultCost, 1.f);
	for (const TSubclassOf<UNavigationQueryFilter>& Player : { TSubclassOf<UNavigationQueryFilter>(UNavFilter_TrapLiteral::StaticClass()),
		TSubclassOf<UNavigationQueryFilter>(UNavFilter_NoCrawlTrapLiteral::StaticClass()) })
	{
		TestTrue(TEXT("a player-side filter does not price the PMC area"), AreaOverride(*Player->GetDefaultObject<UNavigationQueryFilter>(), OwnArea) == nullptr);
	}

	// The four PMC filters: distinct, each avoiding the owned area, none excluding it (a start inside it must still path).
	TSet<UClass*> Classes;
	for (const bool bProne : { false, true })
	{
		for (const bool bMantleOpen : { false, true })
		{
			const TSubclassOf<UNavigationQueryFilter> Filter = PmcEncounterNav::FilterFor(bProne, bMantleOpen);
			if (!TestTrue(TEXT("a filter class"), Filter != nullptr))
			{
				return false;
			}
			Classes.Add(Filter.Get());
			const FNavigationFilterArea* Override = AreaOverride(*Filter->GetDefaultObject<UNavigationQueryFilter>(), OwnArea);
			if (!TestNotNull(TEXT("the PMC filter prices the owned area"), Override))
			{
				continue;
			}
			TestTrue(TEXT("travel cost overridden"), Override->bOverrideTravelCost);
			TestTrue(TEXT("a route over it costs a multiple of the plain path"), Override->TravelCostOverride >= 100.f);
			TestEqual(TEXT("the cost is the rules constant"), Override->TravelCostOverride, PmcEncounterNav::OwnChargeTravelCost);
			TestFalse(TEXT("not excluded"), Override->bIsExcluded);
		}
	}
	TestEqual(TEXT("prone x mantle: four filters"), Classes.Num(), 4);
	// A PMC fighter still walks into the squad's known traps (the old literal-trap filters' override is inherited), prone ones may crawl.
	TestNotNull(TEXT("known squad traps stay free for a PMC fighter"), AreaOverride(*GetDefault<UNavFilter_PmcFighter>(), UNavArea_Tripwire::StaticClass()));
	TestTrue(TEXT("... at cost 1"), AreaOverride(*GetDefault<UNavFilter_PmcFighter>(), UNavArea_Tripwire::StaticClass())->TravelCostOverride == 1.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
