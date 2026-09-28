#pragma once

#include "CoreMinimal.h"
#include "Interactables/ActionMenuTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "InteractionSubsystem.generated.h"

class AInteractableActor;
class AOperativeCharacter;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnActionMenuChanged, bool, bOpen, const FActionMenuSpec&, Menu);

/**
 * Object interaction flow: the squad leader walks to the clicked object; once within its InteractionDistance the
 * object's action menu opens (or a feed line is posted), and the menu buttons run the action / cancel.
 * In the tactical pause the approach is planned like any pause order and runs on release.
 * Godot reference: main.gd `pending_menu_target`, `_trigger_menu_for_object`, `_open_action_menu`,
 * `_on_action_confirmed`, `_on_action_cancelled`, `_close_action_menu`.
 */
UCLASS()
class CODEXTACTICS_API UInteractionSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Sends the leader to Target (sprinting on a double click); the menu opens on arrival. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Interactables")
	bool RequestInteraction(AInteractableActor* Target, bool bSprint = false);

	/** Drops the pending approach and closes the menu (e.g. the player ordered a move elsewhere). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Interactables")
	void CancelInteraction();

	/** Confirm button: runs the object's action with the current leader. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Interactables")
	void ConfirmActionMenu();

	/** Relocate button («Переместить» / «Вытолкать»): placement mode for the menu object with the leader. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Interactables")
	void RelocateActionMenu();

	/** «Заминировать» button: trap the menu object with one of the leader's grenades. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Interactables")
	void TrapActionMenu();

	/** Cancel / close button. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Interactables")
	void CancelActionMenu();

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Interactables")
	AInteractableActor* GetPendingInteraction() const { return Pending.Get(); }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Interactables")
	bool IsActionMenuOpen() const { return MenuTarget.IsValid(); }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Interactables")
	AInteractableActor* GetMenuTarget() const { return MenuTarget.Get(); }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Interactables")
	const FActionMenuSpec& GetActionMenu() const { return Menu; }

	/** Menu opened / closed (the HUD widget listens). */
	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Interactables")
	FOnActionMenuChanged OnActionMenuChanged;

private:
	/** Opens the menu if the leader reached the pending object; returns true when it did. */
	bool TryOpenMenu();
	void OpenMenuFor(AInteractableActor* Target, AOperativeCharacter* Leader);
	void CloseMenu();
	/** Clears the «approaching defuser» mark of a mine (Godot _close_action_menu). */
	void ClearDefuser(AInteractableActor* Target) const;

	TWeakObjectPtr<AInteractableActor> Pending;
	TWeakObjectPtr<AInteractableActor> MenuTarget;
	FActionMenuSpec Menu;
};
