#pragma once

#include "CoreMinimal.h"
#include "Interactables/DeployableActor.h"
#include "ProximityMineActor.generated.h"

/**
 * Anti-personnel mine. While armed (bTrapped) it blows up when an enemy or an operative (except the one walking up
 * to defuse it) comes within TriggerRadius: ExplosionDamage within ExplosionRadius to enemies, 75 % to operatives.
 * Mines placed by the squad arm after ArmingDelay. Mines lying on the level are hidden under the snow until an
 * operative spots them (4.5 m, sapper +1.5 m): the squad stops and reports it; pushed objects are dropped.
 * Its collision box is the trigger area (not blocking), so operatives stop outside it to defuse.
 * Godot reference: Scenes/movements/deployables/mine.gd, player.gd `_check_for_hidden_mines` / `_on_mine_spotted`.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API AProximityMineActor : public ADeployableActor
{
	GENERATED_BODY()

public:
	AProximityMineActor();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void DetonateTrap(bool bByShot = false, const FText& InstigatorName = FText::GetEmpty()) override;
	virtual ETrapFlavor GetTrapFlavor() const override { return ETrapFlavor::Mine; }

	/** Blows the mine up (Godot detonate). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Mine")
	void Detonate();

	/** Shows a mine hidden under the snow. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Mine")
	void Reveal();

	/** Marks a freshly set squad mine: visible, safe until ArmingDelay passes. */
	void SetPlacedBySquad();

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Mine")
	bool IsRevealed() const { return bRevealed; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Mine")
	bool IsArmedNow() const { return bTrapped && ArmingTimeLeft <= 0.f; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Mine", meta = (ClampMin = "0"))
	float ExplosionDamage = 120.f;

	/** cm (Godot 3.5 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Mine", meta = (ClampMin = "0"))
	float ExplosionRadius = 350.f;

	/** cm (Godot 1.6 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Mine", meta = (ClampMin = "0"))
	float TriggerRadius = 160.f;

	/** Squad mines react to operatives too (Godot friendly_fire true). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Mine")
	bool bFriendlyFire = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Mine")
	bool bPlacedBySquad = false;

	/** s (Godot arming_delay 3). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Mine", meta = (ClampMin = "0"))
	float ArmingDelay = 3.f;

	/** Spotting range of an operative, cm (Godot mine_detection_range 4.5 m; sapper bonus 1.5 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Mine", meta = (ClampMin = "0"))
	float DetectionRange = 450.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Mine", meta = (ClampMin = "0"))
	float SapperDetectionBonus = 150.f;

	/** Placeholder colours (Godot: level mines glow red, squad mines amber). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Mine")
	FLinearColor HostileColor = FLinearColor::FromSRGBColor(FColor(217, 51, 38));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Mine")
	FLinearColor SquadColor = FLinearColor::FromSRGBColor(FColor(255, 217, 26));

	virtual bool NeedsDefusal() const override { return bTrapped; }

protected:
	virtual void DescribeForMenu(const AOperativeCharacter* Leader, FText& OutTitle, FText& OutDescription, FText& OutConfirm,
		bool& bOutDisabled) const override;

private:
	void UpdateVisuals();
	void ScanForSpotters();
	void HandleSpotted(AOperativeCharacter* Spotter);

	UPROPERTY(EditAnywhere, Category = "CodexTactics|Mine")
	bool bRevealed = false;

	float ArmingTimeLeft = 0.f;
	bool bDetonated = false;
};
