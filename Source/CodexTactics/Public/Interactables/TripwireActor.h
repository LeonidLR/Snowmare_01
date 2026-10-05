#pragma once

#include "CoreMinimal.h"
#include "Interactables/InteractableActor.h"
#include "NavAreas/NavArea.h"
#include "TripwireActor.generated.h"

class UBoxComponent;
class UMaterialInstanceDynamic;
class UNavModifierComponent;
class UStaticMeshComponent;

/**
 * Navmesh area along an armed tripwire: costly for the squad's default query filter (operatives path around their own
 * wires, Sprint 09-C), cost 1 for the enemies (UNavFilter_NoVault overrides it: they do not know about it).
 */
UCLASS()
class CODEXTACTICS_API UNavArea_Tripwire : public UNavArea
{
	GENERATED_BODY()

public:
	UNavArea_Tripwire();
};

/**
 * Tripwire mine «Растяжка» МУВ-3 with two Ф-1 grenades (Sprint 09, TANDEM «SPRINT 09 DIRECTIVE»; UE-only). A wire
 * 1-5 m long at 30 cm between two anchors (a ground peg, or a bracket on an object). Anyone — enemy or operative — who
 * crosses it with a body higher than the wire (not prone) pulls the pin: «ЩЁЛК!», 0.25 s, the paired Ф-1 blast
 * (140, 4.5 m, full armour penetration, stagger). A medic-sapper disarms it in 3 s and gets the grenades back (one is
 * lost on a fumble). Rules: TripwireRules; placed by URelocationSubsystem (two clicks). A preview copy (bPreview) is
 * the placement hologram: cyan valid, red invalid.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API ATripwireActor : public AInteractableActor
{
	GENERATED_BODY()

public:
	ATripwireActor();

	virtual void Tick(float DeltaSeconds) override;

	/**
	 * Lays the wire between the ground points A and B (anchors on objects get a bracket, on the ground a peg). An armed
	 * wire (not bPreview) starts its arming delay; RiggedBy is ignored by the trigger until he leaves the wire.
	 */
	void Setup(const FVector& GroundA, const FVector& GroundB, bool bAOnObject, bool bBOnObject, bool bInPreview,
		AActor* RiggedBy = nullptr);

	/** Preview colour: cyan valid, red invalid. */
	void SetPreviewValid(bool bValid);

	/** Pulls the pin (as if someone crossed it): click, then the blast after the fuse delay. */
	void Trip(AActor* Tripper);

	bool IsArmed() const { return !bPreview && ArmingLeft <= 0.f && !bTripped; }
	bool IsTripped() const { return bTripped; }
	FVector GetAnchorA() const { return WireA; }
	FVector GetAnchorB() const { return WireB; }

	virtual FActionMenuRequest BuildActionMenu(const AOperativeCharacter* Leader) const override;
	virtual void ExecuteAction(AOperativeCharacter* User) override;
	virtual bool CanReceiveTrap() const override { return false; }
	virtual bool GetOverheadLabel(FOverheadLabel& OutLabel) const override;

	/** Blueprint hook for the pin click / the blast effects (no sound / VFX code in C++ yet). */
	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Tripwire", meta = (DisplayName = "On Pin Pulled"))
	void ReceivePinPulled();

private:
	void Detonate();
	void FinishDisarm();
	/** Body height above the feet that meets the wire (operatives / marksman by stance, others by size). */
	static float BodyHeight(const AActor& Actor);

	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Tripwire")
	TObjectPtr<UStaticMeshComponent> PegA;

	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Tripwire")
	TObjectPtr<UStaticMeshComponent> PegB;

	/** Wide box the nav modifier reads (no blocking). */
	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Tripwire")
	TObjectPtr<UBoxComponent> NavBox;

	UPROPERTY(Transient)
	TObjectPtr<UNavModifierComponent> NavModifier;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> WireMaterial;

	FVector WireA = FVector::ZeroVector;
	FVector WireB = FVector::ZeroVector;
	bool bPreview = false;
	bool bTripped = false;
	float ArmingLeft = 0.f;
	/** Ignored by the trigger until they leave the wire (the rigger). */
	TArray<TWeakObjectPtr<AActor>> Ignored;
	TWeakObjectPtr<AOperativeCharacter> Disarmer;
	FTimerHandle FuseTimer;
	FTimerHandle DisarmTimer;
};
