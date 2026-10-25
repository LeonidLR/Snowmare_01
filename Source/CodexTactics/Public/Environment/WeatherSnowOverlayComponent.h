#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WeatherSnowOverlayComponent.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMeshComponent;
class UStaticMeshComponent;

/**
 * Environment phase 4: the GRADUAL snow cover on objects (user requirement 2026-10-22: «objects are gradually covered with snow»;
 * docs/systems/environment-weather.md §6 / §12). Created by AWeatherPresenterActor. Applies the NEW overlay material M_SnowOverlay
 * (/Game/Environment/Weather/Snow/Materials; world-up mask x MPC_Environment.SnowAmount, frost tint from FrostAmount) as the
 * UMeshComponent overlay material on outdoor static meshes - NO existing material or asset is changed or re-saved. Skips meshes that
 * already have an overlay material, landscapes, skeletal meshes (characters) and meshes flagged / tagged as indoor ("NoSnow" actor or
 * component tag). Restores every touched component when the visuals go off or on EndPlay. Visual only.
 * Ground mode (§12.d, user decision 2026-10-23 «dust the current floors»): the ground slabs the object overlay skips (floor templates,
 * flat slabs over 20 m, meshes wider than 60 m that stay low) get the NEW M_SnowGroundOverlay instead (a world-XY snow layer, coverage
 * by SnowAmount, the interactive-snow track read), through one dynamic instance per world that also carries the nearest interior-zone
 * rectangles (kept bare). Same rules otherwise; own cap / switch (Codex.Env.SnowGroundMax / Codex.Env.SnowGround).
 */
UCLASS(ClassGroup = (Environment), meta = (BlueprintSpawnableComponent))
class CODEXTACTICS_API UWeatherSnowOverlayComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWeatherSnowOverlayComponent();

	/** Called by the presenter: SnowAmount 0..1 (persistent cover, FEnvironmentState), FrostAmount 0..1 (blended preset). */
	void ApplySnowCover(float SnowAmount, float FrostAmount);

	/** Visuals on / off: off removes the overlay from every component it added it to. */
	void SetVisualsEnabled(bool bEnabled);

	/** Re-scan the level for target meshes (streaming / spawned props); the presenter calls it rarely (e.g. every few seconds). */
	void RefreshTargets();

	/** Overlay material (default M_SnowOverlay; a user BP presenter may override). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment|Snow")
	TSoftObjectPtr<UMaterialInterface> OverlayMaterial;

	/** Ground overlay material (default MI_SnowGroundOverlay); a dynamic instance of it is put on the ground slabs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment|Snow")
	TSoftObjectPtr<UMaterialInterface> GroundMaterial;

	/** Ground slabs holding the ground overlay now. */
	int32 GetGroundTargetCount() const { return GroundTouched.Num(); }

	/** Object meshes holding the object overlay now. */
	int32 GetObjectTargetCount() const { return Touched.Num(); }

	/** The material put on the ground slabs (the dynamic instance; null before the first scan or when the asset is missing). */
	UMaterialInterface* GetGroundOverlayMaterial() const;

	/** Max distance from the player's view target for a mesh to get the overlay, cm (0 = whole level). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment|Snow", meta = (ClampMin = "0"))
	float MaxDistance = 0.f;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** Rendering is possible in this world (no -nullrhi, a game world, no dedicated server). */
	bool CanDraw() const;
	/** OverlayMaterial loaded once (null when the asset is missing). */
	UMaterialInterface* ResolveOverlayMaterial();
	/** The ground overlay's dynamic instance, made once from GroundMaterial (null when the asset is missing). */
	UMaterialInstanceDynamic* ResolveGroundMaterial();
	/** Writes the nearest interior zones (closed / shelter camera zones) into the ground instance's Zone<i>A / Zone<i>B. */
	void UpdateGroundZones(const TArray<TPair<FTransform, FVector>>& Zones);
	/** The point the player's view looks at (the tactical camera's focus, else the ground under the view centre). */
	FVector GetViewFocus() const;
	/** Puts the overlay on every target / takes it off the components that are no target any more (or all when hidden). */
	void SyncOverlays();
	/** Puts Material on Wanted (bShow) / takes it off the components in InOutTouched that are no target any more (or all when hidden). */
	void SyncList(const TArray<TWeakObjectPtr<UMeshComponent>>& Wanted, TArray<TWeakObjectPtr<UMeshComponent>>& InOutTouched, UMaterialInterface* Material,
		bool bShow);
	/** Takes the overlay off every touched component (restores nullptr), objects and ground. */
	void RemoveOverlays();
	/** Gives Mesh its Nanite rendering back when this component switched it to the fallback mesh (Codex.Env.SnowOverlayNanite). */
	void RestoreNanite(UMeshComponent* Mesh);

	/** Components that hold the overlay now (restored to no overlay when hidden / on EndPlay). */
	TArray<TWeakObjectPtr<UMeshComponent>> Touched;
	/** The current selection (outdoor static meshes near the view, capped by Codex.Env.SnowOverlayMax). */
	TArray<TWeakObjectPtr<UMeshComponent>> Targets;
	/** Ground slabs that hold the ground overlay now. */
	TArray<TWeakObjectPtr<UMeshComponent>> GroundTouched;
	/** The current ground selection (nearest edge first, capped by Codex.Env.SnowGroundMax). */
	TArray<TWeakObjectPtr<UMeshComponent>> GroundTargets;
	/** Nanite meshes this component switched to their fallback mesh (restored with the overlay). */
	TArray<TWeakObjectPtr<UStaticMeshComponent>> NaniteForced;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> LoadedOverlay;

	/** One dynamic instance of GroundMaterial for every ground slab of this world (it holds the interior-zone rectangles). */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> GroundInstance;

	FVector LastRefreshFocus = FVector::ZeroVector;
	double LastRefreshSeconds = -1.0e9;
	float LastSnow = 0.f;
	float LastFrost = 0.f;
	int32 LastMaxMeshes = -1;
	int32 LastGroundMax = -1;
	int32 LastLoggedGroundSummary = -1;
	int32 LastLoggedSummary = -1;
	bool bVisuals = true;
	bool bOverlayShown = false;
	bool bGroundShown = false;
	bool bScanned = false;
	bool bMaterialResolved = false;
	bool bGroundResolved = false;
};
