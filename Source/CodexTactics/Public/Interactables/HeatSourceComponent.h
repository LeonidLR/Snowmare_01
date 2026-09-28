#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "HeatSourceComponent.generated.h"

/**
 * Warm zone around a heat source (running generator, burning barrel). Operatives within Radius warm up;
 * the cold survival system queries active sources via IsLocationWarm / GetActiveSources.
 * Godot reference: Scenes/movements/warm_zone.gd (distance check with +0.5 m tolerance).
 */
UCLASS(ClassGroup = (CodexTactics), meta = (BlueprintSpawnableComponent))
class CODEXTACTICS_API UHeatSourceComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UHeatSourceComponent();

	virtual void OnRegister() override;
	virtual void OnUnregister() override;

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Heat")
	void SetHeatActive(bool bNewActive) { bHeatActive = bNewActive; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Heat")
	bool IsHeatActive() const { return bHeatActive; }

	/** True if the source is active and Location is within Radius (+ Tolerance). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Heat")
	bool IsLocationWarm(const FVector& Location) const;

	/** All registered heat sources in every world (active or not); callers filter by GetWorld(). */
	static const TArray<TWeakObjectPtr<UHeatSourceComponent>>& GetAllSources();

	/** Warm zone radius, cm. Architect spec: 400 (Godot generator scene: 5.5 m, warm_zone default 6 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Heat", meta = (ClampMin = "0"))
	float Radius = 400.f;

	/** Extra distance counted as inside, cm (Godot: +0.5 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Heat", meta = (ClampMin = "0"))
	float Tolerance = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Heat")
	bool bHeatActive = false;
};
