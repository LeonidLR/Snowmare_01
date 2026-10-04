#pragma once

#include "CoreMinimal.h"
#include "NavAreas/NavArea.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "VaultNavigation.generated.h"

/**
 * Navmesh area over a vaultable obstacle: operatives path across it (and vault on the way), enemies do not
 * (UNavFilter_NoVault; Godot enemies never vault — they smash barricades).
 */
UCLASS()
class CODEXTACTICS_API UNavArea_Vault : public UNavArea
{
	GENERATED_BODY()

public:
	UNavArea_Vault();
};

/** Query filter of the enemies: vault areas are closed. */
UCLASS()
class CODEXTACTICS_API UNavFilter_NoVault : public UNavigationQueryFilter
{
	GENERATED_BODY()

public:
	UNavFilter_NoVault();
};

namespace VaultNavigation
{
	/** Actor tag that makes a level object vaultable (Godot "vault" group / property). */
	CODEXTACTICS_API extern const FName VaultTag;

	/** Barricades with bVaultable and actors tagged "Vault". */
	CODEXTACTICS_API bool IsVaultable(const AActor* Actor);

	/**
	 * Character stands on top of an obstacle nobody may stand on (a barricade, a vault object, a barrel / deployable):
	 * user report 2026-10-04 — an operative landed inside a barricade after a vault (vaulting along its 3 m length),
	 * was pushed up onto it and stayed there (no navmesh on top, every move order failed); a cutter's pounce landed
	 * there too.
	 */
	CODEXTACTICS_API bool IsStandingOnObstacle(const class ACharacter& Character);

	/** A free spot on the ground (not an obstacle, on the navmesh) 1-3.5 m from Character for its capsule; false: none. */
	CODEXTACTICS_API bool FindStepOffSpot(const class ACharacter& Character, FVector& OutCentre);

	/**
	 * Lets the operatives' paths cross the actor: its shapes stop cutting the navmesh and a UNavArea_Vault modifier covers
	 * it instead (enemies still go round). Called from BeginPlay of barricades and for tagged level actors.
	 */
	CODEXTACTICS_API void MakeVaultable(AActor* Actor);
}
