#pragma once

#include "CoreMinimal.h"
#include "Interactables/DeployableActor.h"
#include "BarricadeActor.generated.h"

class UHealthComponent;

/**
 * Tactical barricade: 3 x 0.6 x 1 m cover with 200 HP. Can carry a tripwire that goes off when an enemy comes
 * within 1.8 m, is hit by an enemy or a defusal fails; the blast also wrecks the barricade.
 * Godot reference: Scenes/movements/deployables/barricade.gd, Scenes/deployables/barricade_deployable.tscn.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API ABarricadeActor : public ADeployableActor
{
	GENERATED_BODY()

public:
	/** Godot barricade.gd _update_overhead_ui: «🧱 Баррикада [ЛОВУШКА]: HP/max» 1.35 m up. */
	virtual bool GetOverheadLabel(FOverheadLabel& OutLabel) const override;

	ABarricadeActor();

	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void DetonateTrap(bool bByShot = false, const FText& InstigatorName = FText::GetEmpty()) override;
	virtual ETrapFlavor GetTrapFlavor() const override { return ETrapFlavor::Barricade; }

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Barricade")
	TObjectPtr<UHealthComponent> Health;

	/** Enemy distance that sets the tripwire off, cm (Godot 1.8 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Barricade", meta = (ClampMin = "0"))
	float TrapContactDistance = 180.f;

	/** Placeholder colour (Godot albedo 0.85 / 0.65 / 0.25). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Barricade")
	FLinearColor BodyColor = FLinearColor::FromSRGBColor(FColor(217, 166, 64));

protected:
	virtual void DescribeForMenu(const AOperativeCharacter* Leader, FText& OutTitle, FText& OutDescription, FText& OutConfirm,
		bool& bOutDisabled) const override;

private:
	UFUNCTION()
	void HandleDestroyed(AActor* Victim, const FString& AttackerSource);
};
