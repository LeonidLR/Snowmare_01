#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TutorialMarkerActor.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;

/**
 * A world objective marker the user places for the tutorial (Demo 01 C2, UX brief 03 §2.1 "Highlight"): a flat ground
 * ring plus an optional beacon, shown while its step is active (StepId) or, without a step, while its beat runs
 * (BeatId); hidden otherwise (also with the tutorial off / before any steps exist). The runtime guide beacon of the current
 * navigation target (lure spots, objective beacons, "@enemy" spots) is one too. Engine shapes (the cylinder as a disc and a
 * pole) wearing the orange HOLOGRAM (2026-10-23, game designer play-test: "all the disc-with-a-pole markers must be holographic,
 * orange"): /Game/UI/Materials/MI_HoloMarker_Orange of M_HoloMarker (unlit translucent, fresnel rim, scanlines, pulse, flicker,
 * depth fade on the ground, no fog, exposure-invariant; Scripts/Editor/create_holo_marker_material.py). Custom primitive data 0 =
 * 1 on the disc, 0 on the pole (the material draws rings on the disc, scanlines up the pole).
 * bScreenEdgePointer: the tutorial overlay points at it from the screen edge while it is off-screen. UE-only.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API ATutorialMarkerActor : public AActor
{
	GENERATED_BODY()

public:
	ATutorialMarkerActor();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	TObjectPtr<USceneComponent> MarkerRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	TObjectPtr<UStaticMeshComponent> Ring;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	TObjectPtr<UStaticMeshComponent> Beacon;

	/** Shown while this step is active (steps JSON "id"). */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "CodexTactics|Tutorial")
	FName StepId;

	/**
	 * A role spot (2026-10-19, the A3 cover lesson "each role has its own spot"): the EOperativeRole name of the operative who must
	 * stand here ("Commander", "Engineer", "MedicSapper"). A "role_spots" step (FTutorialCompletion::bRoleSpots) counts an operative
	 * only within RingRadiusCm + RoleSpotToleranceCm of his role's marker; the marker hides once he counted. None = any operative.
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "CodexTactics|Tutorial")
	FName RoleId;

	/** Text shown over the marker while it is up (the role's name on a role spot); empty = none. Drawn by the tutorial overlay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Tutorial")
	FText Label;

	/** A role spot accepts its operative this far beyond the ring (he stands in cover 45 cm off the wall), cm. */
	static constexpr float RoleSpotToleranceCm = 80.f;

	/** Without StepId: shown while this beat is the current one. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "CodexTactics|Tutorial")
	FName BeatId;

	/** Ring radius on the ground, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Tutorial", meta = (ClampMin = "20"))
	float RingRadiusCm = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Tutorial")
	bool bShowBeacon = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Tutorial", meta = (ClampMin = "50"))
	float BeaconHeightCm = 350.f;

	/** Off-screen: the overlay draws an arrow at the screen edge towards it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Tutorial")
	bool bScreenEdgePointer = true;

	/**
	 * A tint for the material's "Color" parameter - used only with bCustomColor (2026-10-23: every marker is the hologram ORANGE of
	 * MI_HoloMarker_Orange; the role spots keep their old per-role colours here, unused).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Tutorial")
	FLinearColor Color = FLinearColor(1.f, 0.195f, 0.005f);

	/** Tint this marker with Color instead of the material's own orange (off by default: the game designer wants every marker orange). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Tutorial")
	bool bCustomColor = false;

	/**
	 * The marker material: MI_HoloMarker_Orange by default (2026-10-23). A marker still set to the old engine placeholder
	 * (BasicShapeMaterial) gets the hologram too. Needs a vector parameter "Color" to tint (bCustomColor).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Tutorial")
	TObjectPtr<UMaterialInterface> MarkerMaterial;

	/** The hologram marker material (the parent of MI_HoloMarker_Orange). */
	static constexpr const TCHAR* HoloMaterialPath = TEXT("/Game/UI/Materials/M_HoloMarker.M_HoloMarker");
	static constexpr const TCHAR* HoloInstancePath = TEXT("/Game/UI/Materials/MI_HoloMarker_Orange.MI_HoloMarker_Orange");

	/** Ring and pole wear the hologram (their base material is M_HoloMarker) - smokes / the marker shots assert it. */
	bool IsHologram() const;

	/**
	 * Dev / comparison shots only: dress this marker in the old placeholder look (BasicShapeMaterial, amber) instead of the hologram.
	 * Never used by the game.
	 */
	void ApplyLegacyLookForComparison();

	/**
	 * 2026-10-20: the guide beacon UTutorialSubsystem spawns at a volume that is the current navigation target (no marker of its own
	 * there: the next beacon / the next beat's start). Shown only while it is the target; never placed by hand.
	 */
	UPROPERTY(VisibleInstanceOnly, Transient, BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	bool bRuntimeGuide = false;

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Tutorial")
	bool IsMarkerShown() const { return bShown; }

	/** Called by UTutorialSubsystem when its step / beat state changes. */
	void SetMarkerShown(bool bShow);

	/** Blueprint hook for a sound / VFX when the marker appears or hides. */
	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Tutorial")
	void OnMarkerShownChanged(bool bNowShown);

private:
	void ApplyShape();
	void ApplyMaterial();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MaterialInstance;

	/**
	 * The weather presenter's colour grade desaturates the night / snowfall (to 45-70 %): the hologram's "SaturationCompensation" = 1 / that
	 * saturation (1-2.5), refreshed twice a second while shown, keeps it orange in every lighting.
	 */
	void UpdateSaturationCompensation();
	float SaturationCheckIn = 0.f;

	bool bShown = false;
	float PulseTime = 0.f;
};
