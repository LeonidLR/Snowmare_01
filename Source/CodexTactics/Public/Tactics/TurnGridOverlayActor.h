#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TurnGridOverlayActor.generated.h"

class UGorkyGridManager;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;

/** Which layer of the tactical grid overlay a set of cells goes to. */
UENUM()
enum class ETurnOverlayLayer : uint8
{
	/** Faint grid lines of the whole 14 x 14 area. */
	Grid,
	/** Cells the active operative can walk to (green). */
	Reachable,
	/** Cells the active weapon can hit (red). */
	Attack,
	/** Enemy movement preview (red, transparent). */
	EnemyReach,
	/** The active operative's cell (cyan). */
	Active,
	/** A squad member an enemy is about to attack (yellow). */
	Warning,
	/** Fire-fear cells around burning barrels (orange). */
	Fear
};

/**
 * Glowing floor tiles of the Gorky 17 tactical grid (one instanced mesh per layer, M_CombatFeedback glow).
 * Godot reference: Scripts/tactics/tactical_grid_overlay.gd (update_reachable_cells, attack cells, fear zones,
 * show_target_warning, active unit marker).
 */
UCLASS(NotBlueprintable)
class CODEXTACTICS_API ATurnGridOverlayActor : public AActor
{
	GENERATED_BODY()

public:
	ATurnGridOverlayActor();

	void SetGrid(const UGorkyGridManager* InGrid);

	/** Replaces the cells of one layer. */
	void SetCells(ETurnOverlayLayer Layer, const TArray<FIntPoint>& Cells);

	void ClearLayer(ETurnOverlayLayer Layer) { SetCells(Layer, TArray<FIntPoint>()); }

	int32 GetCellCount(ETurnOverlayLayer Layer) const;

	UPROPERTY(EditDefaultsOnly, Category = "CodexTactics|Tactics")
	TObjectPtr<UMaterialInterface> GlowMaterial;

private:

	UPROPERTY()
	TMap<ETurnOverlayLayer, TObjectPtr<UInstancedStaticMeshComponent>> OverlayLayers;

	UPROPERTY()
	TMap<ETurnOverlayLayer, FLinearColor> LayerColors;

	UPROPERTY()
	TMap<ETurnOverlayLayer, float> LayerIntensity;

	UPROPERTY()
	TMap<ETurnOverlayLayer, float> LayerHeights;

	TWeakObjectPtr<const UGorkyGridManager> Grid;
};
