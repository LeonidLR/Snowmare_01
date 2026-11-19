#pragma once

#include "CoreMinimal.h"
#include "Interactables/TripwireActor.h"
#include "NavAreas/NavArea.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "PmcEncounterNav.generated.h"

class AActor;

/**
 * Smart-enemies T-11 second half (PMC_DEFENCE D2, designer OQ-21 2026-11-19): the charges the generator lays round the PMC posts
 * (props layer "pmc_charges", owner "pmc") belong to the PMC. The area under such a charge: cost 1 for everyone (the player's squad is
 * not steered round charges it cannot see), a huge cost for the PMC fighters' filters below. UE-only, no Godot reference.
 */
UCLASS()
class CODEXTACTICS_API UNavArea_PmcCharge : public UNavArea
{
	GENERATED_BODY()

public:
	UNavArea_PmcCharge();
};

/** The move filter of a standing / crouched PMC fighter: crawl-only areas closed, known squad traps cost nothing (he walks into them), his own charges avoided. */
UCLASS()
class CODEXTACTICS_API UNavFilter_PmcFighter : public UNavFilter_NoCrawlTrapLiteral
{
	GENERATED_BODY()

public:
	UNavFilter_PmcFighter();
};

/** UNavFilter_PmcFighter with the mantle closed. */
UCLASS()
class CODEXTACTICS_API UNavFilter_PmcFighterNoMantle : public UNavFilter_PmcFighter
{
	GENERATED_BODY()

public:
	UNavFilter_PmcFighterNoMantle();
};

/** A prone PMC fighter: crawl-only areas open, otherwise as UNavFilter_PmcFighter. */
UCLASS()
class CODEXTACTICS_API UNavFilter_PmcFighterProne : public UNavFilter_TrapLiteral
{
	GENERATED_BODY()

public:
	UNavFilter_PmcFighterProne();
};

/** UNavFilter_PmcFighterProne with the mantle closed. */
UCLASS()
class CODEXTACTICS_API UNavFilter_PmcFighterProneNoMantle : public UNavFilter_PmcFighterProne
{
	GENERATED_BODY()

public:
	UNavFilter_PmcFighterProneNoMantle();
};

namespace PmcEncounterNav
{
	/**
	 * Travel cost of one unit of a PMC-owned charge's area for the PMC filters: path length x this, so a route round it costs less
	 * than a few metres over it up to a detour of this factor (a hard exclusion would fail a path whose start sits inside the area).
	 */
	inline constexpr float OwnChargeTravelCost = 1000.f;
	/** Extra cm round a mine's trigger radius the area covers (a body's width). */
	inline constexpr float OwnChargeMarginCm = 60.f;

	/** The query filter of a PMC fighter's move (bProne: crawl-only areas open; bMantleOpen: the mantle links open). */
	CODEXTACTICS_API TSubclassOf<UNavigationQueryFilter> FilterFor(bool bProne, bool bMantleOpen);

	/**
	 * Marks a level charge (mine / tripwire) as owned by the PMC: the owner tag (DeployableRules::PmcOwnerTag) and the nav area
	 * (UNavArea_PmcCharge, FailsafeExtent in the actor's space) that the PMC filters avoid. Call after the actor began play.
	 */
	CODEXTACTICS_API void AdoptCharge(AActor& Charge, const FVector& FailsafeExtent);
}
