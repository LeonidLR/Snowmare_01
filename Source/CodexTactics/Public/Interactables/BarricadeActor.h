#pragma once

#include "CoreMinimal.h"
#include "Interactables/DeployableActor.h"
#include "BarricadeActor.generated.h"

class UHealthComponent;

/** Godot barricade.gd ContactType: what touching / hitting the barricade does to an enemy. */
UENUM(BlueprintType)
enum class EBarricadeContact : uint8
{
	None UMETA(DisplayName = "None"),
	Physical UMETA(DisplayName = "Physical (spikes, barbed wire)"),
	Fire UMETA(DisplayName = "Fire"),
	Cryo UMETA(DisplayName = "Cryo"),
	Energy UMETA(DisplayName = "Energy")
};

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

	/** Barricade height, cm (Sprint 08: 60, SightRules::CoverHeightCm). */
	static constexpr float HeightCm = 60.f;

	/** Sets a map-placed barricade down onto the ground below it (they were laid out 1 m high). */
	void SettleOnGround();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void DetonateTrap(bool bByShot = false, const FText& InstigatorName = FText::GetEmpty()) override;
	virtual ETrapFlavor GetTrapFlavor() const override { return ETrapFlavor::Barricade; }

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Barricade")
	TObjectPtr<UHealthComponent> Health;

	/** Enemy distance that sets the tripwire off, cm (Godot 1.8 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Barricade", meta = (ClampMin = "0"))
	float TrapContactDistance = 180.f;

	/** The squad may vault over it (Godot barricade.gd vault, on by default); enemies never do. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Barricade")
	bool bVaultable = true;

	/**
	 * Contact damage (Godot contact_type / contact_damage / contact_tick_interval): every interval enemies within
	 * 2.2 m take half of it, an enemy striking the barricade takes all of it; fire burns 3 s, cryo freezes 2.5 s,
	 * energy staggers 1 s. None by default (Godot); the generator barricade of movements_demo has spikes 5.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Barricade")
	EBarricadeContact ContactType = EBarricadeContact::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Barricade", meta = (ClampMin = "0"))
	float ContactDamage = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Barricade", meta = (ClampMin = "0.1"))
	float ContactTickInterval = 1.f;

	/** Godot take_damage retaliation: an enemy that strikes it gets the full contact effect. */
	void RetaliateAgainst(AActor* Attacker);

	/** Godot _apply_contact_effect_to_enemy. */
	void ApplyContactTo(AActor* Enemy, float Damage);

	/**
	 * Turn-based contact (Sprint 06-C): the real-time contact timer stands still in turn-based combat; instead an enemy
	 * starting its turn within 2.2 m takes the area contact damage (half of ContactDamage) once. True if it applied.
	 */
	bool ApplyTurnContact(AActor* Enemy);

	/** Placeholder colour (Godot albedo 0.85 / 0.65 / 0.25). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Barricade")
	FLinearColor BodyColor = FLinearColor::FromSRGBColor(FColor(217, 166, 64));

protected:
	virtual void DescribeForMenu(const AOperativeCharacter* Leader, FText& OutTitle, FText& OutDescription, FText& OutConfirm,
		bool& bOutDisabled) const override;

private:
	UFUNCTION()
	void HandleDestroyed(AActor* Victim, const FString& AttackerSource);

	float ContactTimer = 0.f;
};
