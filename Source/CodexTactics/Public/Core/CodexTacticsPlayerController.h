#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Characters/OperativeMovementRules.h"
#include "CodexTacticsPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
class USquadSubsystem;

/**
 * Squad orders from mouse and keyboard. The controller possesses the camera pawn, never an operative.
 * Click ground: leader moves there; double click: sprint; click an operative: make it leader;
 * 1..3: select leader; Z / C / X: stand / crouch / prone (with Alt: whole squad).
 * Input actions are created in code for now; they move to assets once the editor setup exists.
 * Godot reference: Scenes/movements/main.gd (_input, raycast_from_mouse, _select_squad_member_by_index).
 */
UCLASS()
class CODEXTACTICS_API ACodexTacticsPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ACodexTacticsPlayerController();

	/** Max real seconds between clicks that form a double click (Godot: 350 ms). */
	UPROPERTY(EditAnywhere, Category = "CodexTactics|Input", meta = (ClampMin = "0"))
	float DoubleClickSeconds = 0.35f;

	/** Max cursor travel between clicks of a double click, pixels (Godot: 30 px). */
	UPROPERTY(EditAnywhere, Category = "CodexTactics|Input", meta = (ClampMin = "0"))
	float DoubleClickPixels = 30.f;

	/** A click this close to an operative (cm, on the ground plane) selects it instead of moving. */
	UPROPERTY(EditAnywhere, Category = "CodexTactics|Input", meta = (ClampMin = "0"))
	float SelectRadius = 60.f;

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

private:
	void CreateInputActions();
	void OnClick();
	void SelectMember1() { SelectMember(0); }
	void SelectMember2() { SelectMember(1); }
	void SelectMember3() { SelectMember(2); }
	void StanceStand() { ApplyStance(EOperativeStance::Standing); }
	void StanceCrouch() { ApplyStance(EOperativeStance::Crouching); }
	void StanceProne() { ApplyStance(EOperativeStance::Prone); }
	void SelectMember(int32 RosterIndex);
	void ApplyStance(EOperativeStance Stance);
	USquadSubsystem* GetSquad() const;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> MappingContext;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> ClickAction;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInputAction>> SelectActions;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInputAction>> StanceActions;

	double LastClickTime = -1.0;
	FVector2D LastClickPosition = FVector2D::ZeroVector;
};
