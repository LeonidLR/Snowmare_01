#pragma once

#include "CoreMinimal.h"
#include "Interactables/BarrelRules.h"
#include "Interactables/InteractableActor.h"
#include "BarrelActor.generated.h"

class UPointLightComponent;
class UMaterialInstanceDynamic;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnBarrelBurningChanged, ABarrelActor*, Barrel, bool, bBurning);

/**
 * Fuel barrel («Горючая бочка»): lit once with a match from the operative's supply, it burns for BurnDuration,
 * warms operatives within the heat radius and lights the area, fades during the last seconds, then stays charred.
 * The burn timer is frozen in turn-based combat (rounds drive it there).
 * Godot reference: Scenes/movements/interactable.gd (barrel part), main.gd `_trigger_menu_for_object` (barrel menu),
 * movements_demo.tscn BarrelObject (1.2 x 1.4 m, WarmZone3D heat_radius 5.5 m, OmniLight range 10 m).
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API ABarrelActor : public AInteractableActor
{
	GENERATED_BODY()

public:
	ABarrelActor();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual FActionMenuRequest BuildActionMenu(const AOperativeCharacter* Leader) const override;
	virtual void ExecuteAction(AOperativeCharacter* User) override;

	/** Lights the barrel with one of User's matches and posts the radio line. Returns true if it caught fire. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Barrel")
	bool Ignite(AOperativeCharacter* User);

	/** Puts the fire out (the barrel stays burnt). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Barrel")
	void Extinguish();

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Barrel")
	bool IsBurning() const { return Burn.bBurning; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Barrel")
	bool IsBurnt() const { return Burn.bBurnt && !Burn.bBurning; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Barrel")
	float GetBurnTimeLeft() const { return Burn.TimeLeft; }

	/** Burn time, s (Godot burn_duration 35). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Barrel", meta = (ClampMin = "0.1"))
	float BurnDuration = 35.f;

	/** The fire flickers and fades during the last seconds (Godot: 7). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Barrel", meta = (ClampMin = "0"))
	float FadeSeconds = 7.f;

	/** Fire light brightness at full strength. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Barrel", meta = (ClampMin = "0"))
	float FireLightIntensity = 60.f;

	/** Placeholder body colours (Godot albedo, sRGB: rusty 0.65/0.2/0.1, glowing red 0.85/0.12/0.08, charred 0.12/0.1/0.1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Barrel")
	FLinearColor NormalColor = FLinearColor::FromSRGBColor(FColor(166, 51, 26));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Barrel")
	FLinearColor BurningColor = FLinearColor::FromSRGBColor(FColor(217, 31, 20));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Barrel")
	FLinearColor CharredColor = FLinearColor::FromSRGBColor(FColor(31, 26, 26));

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Barrel")
	TObjectPtr<UPointLightComponent> FireLight;

	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Barrel")
	FOnBarrelBurningChanged OnBurningChanged;

	/** Blueprint hook for fire VFX / sound. */
	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Barrel", meta = (DisplayName = "On Burning Changed"))
	void ReceiveBurningChanged(bool bBurning);

private:
	void ApplyVisuals();
	/** Shared switch-off: charred look, heat and light off, events. */
	void HandleFireOut();
	void PostLine(const FText& Speaker, const FText& Text) const;
	/** Godot can_push: the barrel may be pushed to a new spot. */
	bool CanPushNow() const;

	FBarrelBurnState Burn;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BodyMaterial;
};
