#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FrostVignetteWidget.generated.h"

class UImage;
class UMaterialInstanceDynamic;

/**
 * Full-screen frost vignette: M_FrostVignette (UI material) on an image over the game view, its "ColdPct" set to the
 * coldest operative; hidden up to 35 % cold (the shader shows nothing below). Never takes input.
 * Godot reference: Scenes/movements/movements_demo.tscn UI/FrostOverlay + Shaders/frost_vignette.gdshader,
 * main.gd _update_squad_hud (cold_pct = max_squad_cold).
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API UFrostVignetteWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Sets the cold (0..100) and shows / hides the overlay. */
	void SetColdPct(float ColdPct);

	float GetColdPct() const { return CurrentColdPct; }
	bool IsShown() const;

protected:
	virtual void NativeOnInitialized() override;

private:
	UPROPERTY()
	TObjectPtr<UImage> Overlay;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Material;

	float CurrentColdPct = 0.f;
};
