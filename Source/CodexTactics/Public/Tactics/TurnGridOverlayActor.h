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
	Fear,
	/** Outline of a quadrant uncovered for one turn (yellow; SetExposedZones, not SetCells). */
	ExposedWarning,
	/** Outline + corner marks of a quadrant uncovered for two turns or more (red). */
	ExposedDanger,
	/** Frame of the hovered cell (yellow; SetCursorCell). */
	CursorMove,
	/** Frame + corner brackets of a hovered enemy cell (red; SetCursorCell). */
	CursorEnemy
};

/**
 * Glowing floor tiles of the Gorky 17 tactical grid (one instanced mesh per layer, M_CombatFeedback glow).
 * Godot reference: Scripts/tactics/tactical_grid_overlay.gd (update_reachable_cells, attack cells, fear zones,
 * show_target_warning, active unit marker, update_exposed_zone_warning). The Godot outline pulse is not ported.
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

	/**
	 * Attack mode (Godot update_attack_pattern): the weapon's cells as the red-orange dot matrix (M_WeaponMatrixDots),
	 * Falloff per cell (1 at the first cell down to 0.25 at the weapon's range).
	 */
	void SetAttackCells(const TArray<FIntPoint>& Cells, const TArray<float>& Falloff);

	/** Falloff of an attack cell as drawn (-1 when the cell is not shown). */
	float GetAttackCellFalloff(const FIntPoint& Cell) const;

	/**
	 * Outlines of exposed quadrants (Godot update_exposed_zone_warning): TurnsByQuadrant[Q] uncovered turns; 1 = yellow
	 * outline, 2+ = red outline with corner marks, 0 = none.
	 */
	void SetExposedZones(const TArray<int32>& TurnsByQuadrant);

	/**
	 * Godot set_hovered_cell / _rebuild_cursor_mesh: a frame around the cell under the mouse — yellow, or red with corner
	 * brackets over an enemy. An invalid cell hides it.
	 */
	void SetCursorCell(const FIntPoint& Cell, bool bEnemy);

	/** Cell the cursor frame is drawn on (-999 when hidden) and whether it is the enemy frame (tests). */
	FIntPoint GetCursorCell() const { return CursorCell; }
	bool IsCursorOnEnemy() const { return bCursorEnemy; }

	virtual void Tick(float DeltaSeconds) override;

	/** Current glow intensity of a layer (pulses included; tests). */
	float GetLayerIntensity(ETurnOverlayLayer Layer) const;

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

	TMap<FIntPoint, float> AttackFalloff;

	UPROPERTY(Transient)
	TMap<ETurnOverlayLayer, TObjectPtr<UMaterialInstanceDynamic>> LayerInstances;

	float ExposedPulseTime = 0.f;
	FIntPoint CursorCell = FIntPoint(-999, -999);
	bool bCursorEnemy = false;
	float WarningPulseTime = 0.f;

	TWeakObjectPtr<const UGorkyGridManager> Grid;
};
