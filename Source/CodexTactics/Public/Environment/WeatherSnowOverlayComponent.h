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
 * Wet mode (section 18, temperate levels: no snow there): the SAME targets get the wet materials instead - M_WetOverlay on objects
 * (darker, glossy upward faces) and M_WetGroundOverlay on the ground slabs (a wet film, puddles, rain ripples; the interior zones stay
 * dry), driven by MPC_Environment Wetness / PuddleAmount / RainIntensity. The presenter picks the mode by the climate (ApplyWetCover vs
 * ApplySnowCover); an arctic level never enters it and never loads the wet materials.
 */
UCLASS(ClassGroup = (Environment), meta = (BlueprintSpawnableComponent))
class CODEXTACTICS_API UWeatherSnowOverlayComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWeatherSnowOverlayComponent();

	/** Called by the presenter: SnowAmount 0..1 (persistent cover, FEnvironmentState), FrostAmount 0..1 (blended preset). */
	void ApplySnowCover(float SnowAmount, float FrostAmount);

	/**
	 * Section 18, temperate levels: Wetness / PuddleAmount 0..1 (FEnvironmentState). Switches to the wet materials (the snow overlays are
	 * taken off first); shown while ShouldShowWetOverlay. ApplySnowCover switches back.
	 */
	void ApplyWetCover(float Wetness, float PuddleAmount);

	/** The wet materials are in use (section 18). */
	bool IsWetMode() const { return bWetMode; }

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

	/** Section 18: the wet object / ground overlay materials (defaults MI_WetOverlay / MI_WetGroundOverlay in /Game/Environment/Weather/Rain/Materials). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment|Rain")
	TSoftObjectPtr<UMaterialInterface> WetOverlayMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment|Rain")
	TSoftObjectPtr<UMaterialInterface> WetGroundMaterial;

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
	/** The materials of the mode in force (snow or wet, section 18). */
	UMaterialInterface* ActiveOverlay() const;
	UMaterialInstanceDynamic* ActiveGround() const;
	/** Section 18: switches the mode (the previous mode's overlays are taken off first). */
	void SetWetMode(bool bWet);
	/** Section 18: the wet materials (loaded once; null when missing - an arctic level never calls these). */
	UMaterialInterface* ResolveWetOverlayMaterial();
	UMaterialInstanceDynamic* ResolveWetGroundMaterial();
	/** Writes the nearest interior zones (closed / shelter camera zones) into the ground instance's Zone<i>A / Zone<i>B. */
	void UpdateGroundZones(const TArray<TPair<FTransform, FVector>>& Zones);
	/** The point the player's view looks at (the tactical camera's focus, else the ground under the view centre). */
	FVector GetViewFocus() const;
	/** Shows (scan / sync) or removes the overlays of the mode in force (bCanShow: visuals on and the mode's amounts above zero). */
	void ApplyShown(bool bCanShow);
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

	/** Section 18: the wet object overlay and the wet ground dynamic instance. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> LoadedWetOverlay;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> WetGroundInstance;

	FVector LastRefreshFocus = FVector::ZeroVector;
	double LastRefreshSeconds = -1.0e9;
	float LastSnow = 0.f;
	float LastFrost = 0.f;
	float LastWetness = 0.f;
	float LastPuddles = 0.f;
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
	bool bWetMode = false;
	bool bWetResolved = false;
	bool bWetGroundResolved = false;
};
