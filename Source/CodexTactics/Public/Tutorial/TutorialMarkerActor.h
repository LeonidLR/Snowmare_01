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
 * (BeatId); hidden otherwise (also with the tutorial off / before any steps exist). Placeholder look from engine shapes
 * with the colour parameter "Color"; MarkerMaterial is the slot for Gemini's marker material (TANDEM request #18).
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

	/** Amber = path / warmth (art bible navigation language). Fed to the material's "Color" parameter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Tutorial")
	FLinearColor Color = FLinearColor(1.f, 0.55f, 0.08f);

	/** Gemini's marker material (request #18); empty = the engine placeholder. Needs a vector parameter "Color" to tint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Tutorial")
	TObjectPtr<UMaterialInterface> MarkerMaterial;

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

	bool bShown = false;
	float PulseTime = 0.f;
};
