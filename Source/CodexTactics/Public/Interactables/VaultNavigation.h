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
	 * Lets the operatives' paths cross the actor: its shapes stop cutting the navmesh and a UNavArea_Vault modifier covers
	 * it instead (enemies still go round). Called from BeginPlay of barricades and for tagged level actors.
	 */
	CODEXTACTICS_API void MakeVaultable(AActor* Actor);
}
