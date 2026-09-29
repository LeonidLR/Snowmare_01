#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Characters/OperativeMovementRules.h"
#include "Characters/PersonalItemRules.h"
#include "Interactables/DeployableRules.h"
#include "Combat/SpaceInput.h"
#include "CodexTacticsPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
class USquadSubsystem;

/**
 * Squad orders from mouse and keyboard. The controller possesses the camera pawn, never an operative.
 * Click ground: leader moves there; double click: sprint; click an operative: make it leader;
 * 1..3: select leader; Z / C / X: stand / crouch / prone (with Alt: whole squad).
 * Camera: wheel zoom, Q/E or arrows rotate, RMB drag rotates, MMB drag pans (WASD / edges are polled by the camera).
 * Ctrl + click: targeted shot by the leader — enemy = priority target, barrel = explode, mine = remote shot (hit chance),
 * supply crate / trapped object = remote detonation; during the tactical pause the shot is planned instead.
 * Ctrl + X: restart the mission (Godot _restart_current_test_mode).
 * While the bottom dialogue is open, orders are blocked: Space / Enter = next line, Esc = skip.
 * Turn-based combat (UTurnBasedCombatSubsystem): click = select / attack / walk, 1..3 select, Tab = next operative,
 * Enter = end the squad turn, Z / C / V = stance (1 AP), R = turn 90° (1 AP).
 * Space: tap = tactical pause (during a wave), hold = enter / leave turn-based combat. During the pause, clicks
 * plan moves (executed together on release); during turn-based combat ground clicks do not issue real-time moves.
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
	float SelectRadius = 120.f;

	/** Space pressed / released (public for headless checks that drive the same path as the keyboard). */
	void SpacePressed();
	void SpaceReleased();

	/**
	 * Ctrl + click on HitActor (Godot main.gd Ctrl branch): the leader shoots it now, or plans the shot during the
	 * tactical pause. Clicking anything else posts the hint. Public for headless checks.
	 */
	void IssueTargetedShot(AActor* HitActor);

	/** Action bar stance slot: Standing -> Crouching -> Prone (prone skipped while moving); the squad follows outside the
	 * preparation unless in solo mode (Godot _cycle_leader_stance). */
	void CycleLeaderStance();

	/** Action bar «ПЕР»: toggles «click an object to move it» (cancels an active placement). Godot _on_relocate_slot_clicked. */
	void ToggleRelocateSelectMode();

	bool IsRelocateSelectMode() const { return bRelocateSelectMode; }

	/** Action bar squad slot / keys 1..3. */
	void SelectSquadMember(int32 RosterIndex) { SelectMember(RosterIndex); }

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;

private:
	/** Tap: tactical pause on/off during a wave (Godot main.gd KEY_SPACE release). */
	void HandleSpaceTap();
	/** Hold: enter or leave Gorky 17 turn-based combat. */
	void HandleSpaceHold();
	void PostHeadquarters(const FText& Text) const;

	void CreateInputActions();
	void OnClick();
	void RestartMission();
	/** Next object click starts its relocation (action bar «ПЕР»). */
	bool bRelocateSelectMode = false;
	/** True while a story dialogue blocks world orders (Godot _unhandled_input). */
	bool IsDialogueOpen() const;
	void DialogueNext();
	/** Enter: next dialogue line, or end the squad turn in turn-based combat. */
	void EnterPressed();
	/** Tab: next operative in turn-based combat. */
	void TabPressed();
	/** Turn-based combat subsystem while a grid fight is running, else null. */
	class UTurnBasedCombatSubsystem* GetActiveTurnBased() const;
	void DialogueSkip();
	void SelectMember1() { SelectMember(0); }
	void SelectMember2() { SelectMember(1); }
	void SelectMember3() { SelectMember(2); }
	void SelectMember4() { SelectMember(3); } // Godot KEY_4: Ivan Susanin once recruited
	void StanceStand() { ApplyStance(EOperativeStance::Standing); }
	void StanceCrouch() { ApplyStance(EOperativeStance::Crouching); }
	void StanceProne() { ApplyStance(EOperativeStance::Prone); }
	void ToggleSoloMode();
	void SelectMember(int32 RosterIndex);
	void ApplyStance(EOperativeStance Stance);
	USquadSubsystem* GetSquad() const;
	class ATacticalCameraPawn* GetCameraPawn() const;

	/** Mouse wheel via BindKey (MouseScrollUp/Down): deterministic with the Slate cursor (architect decision). */
	void OnMouseWheelUp();
	/** R: rotate the object being placed by 45°. */
	void RotatePlacement();
	/** F: set up an engineering item from the leader's supply / switch type while placing. */
	void DeployAbility();
	/**
	 * G (Godot main.gd KEY_G): grenade in hands -> back to the rifle (or cancel the aim); otherwise take a grenade and
	 * start the throw aim (turn-based combat: the grid weapon switch only).
	 */
	void GrenadeKey();
public:
	/** T / action bar «ОБОР» (Godot _on_guard_slot_clicked): the leader holds its spot or rejoins the formation. */
	void GuardKey();

	/** H / J / K / L and the inventory drawer (Godot use_squad_item): the leader uses a provision, feed line. */
	void UseSquadItem(EPersonalItem Item);

	/**
	 * Inventory drawer turret / barricade / mine (Godot _start_placement_for_type): selects the type, a squad mate hands
	 * one over when the leader has none, then the placement ghost starts.
	 */
	void StartPlacementForType(EDeployableType Type);

private:
	/** Ground point under the cursor for the grenade aim (hit, else the thrower's floor plane). */
	bool GetGrenadeAimPoint(FVector& OutPoint) const;
	/** RMB / Esc while aiming a grenade (Godot «Бросок отменён.»). Returns true if an aim was cancelled. */
	bool CancelGrenadeAim();
	/** Ground point under the cursor on the placement plane of the object being moved. */
	bool GetPlacementPoint(FVector& OutPoint) const;
	class URelocationSubsystem* GetPlacingRelocation() const;
	void OnMouseWheelDown();
	void CameraRotateLeft();
	void CameraRotateRight();
	void CameraDragRotateStart();
	void CameraDragRotateStop();
	void CameraDragPanStart();
	void CameraDragPanStop();

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> MappingContext;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> ClickAction;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInputAction>> SelectActions;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInputAction>> StanceActions;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> SoloModeAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> CameraRotateLeftAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> CameraRotateRightAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> CameraDragRotateAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> CameraDragPanAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> SpaceAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> RotatePlacementAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> DeployAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> GrenadeAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> GuardAction;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInputAction>> ItemActions;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> QuickSaveAction;

	/** F5 (Godot _perform_quick_save). */
	void QuickSaveKey();

	void UseMedkit() { UseSquadItem(EPersonalItem::Medkit); }
	void UseCannedFood() { UseSquadItem(EPersonalItem::CannedFood); }
	void UseBread() { UseSquadItem(EPersonalItem::Bread); }
	void UseChocolate() { UseSquadItem(EPersonalItem::Chocolate); }

	FSpaceInputTracker SpaceInput;
	double LastClickTime = -1.0;
	FVector2D LastClickPosition = FVector2D::ZeroVector;
};
