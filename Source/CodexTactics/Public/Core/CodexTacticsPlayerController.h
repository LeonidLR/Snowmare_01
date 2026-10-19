#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Characters/OperativeMovementRules.h"
#include "Characters/PersonalItemRules.h"
#include "Interactables/DeployableRules.h"
#include "Combat/SpaceInput.h"
#include "Characters/FirePostureRules.h"
#include "Tactics/CoverTypes.h"
#include "CodexTacticsPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
class USquadSubsystem;
class AOperativeCharacter;

/**
 * Squad orders from mouse and keyboard. The controller possesses the camera pawn, never an operative.
 * Click ground: leader moves there; double click: sprint; click an operative: make it leader;
 * 1..3: select leader; Z / C / X: stand / crouch / prone (with Alt: whole squad).
 * Camera: wheel zoom, Q/E or arrows rotate, RMB drag rotates, MMB drag pans (WASD / edges are polled by the camera).
 * Ctrl + click: targeted shot by the leader — enemy = priority target, barrel = explode, mine = remote shot (hit chance),
 * supply crate / trapped object = remote detonation; during the tactical pause the shot is planned instead. Ctrl + click
 * anywhere else (no visible enemy within 1.5 m, nothing shootable hit; user decision 2026-10-19) = quick fire: the
 * selection turns to the point and fires there at once (IssueCtrlClick -> IssueQuickFire).
 * Ctrl + X: restart the mission (Godot _restart_current_test_mode).
 * While the bottom dialogue is open, orders are blocked: Space / Enter = next line, Esc = skip.
 * Turn-based combat (UTurnBasedCombatSubsystem): click = select / attack an enemy / walk, Ctrl + click = attack (enemy,
 * barrel, barricade — the same attack key as in real time and in the pause, user request 2026-10-06; Shift no longer
 * attacks there), 1..3 select, Tab = next operative, Enter = end the squad turn, Z / C / V = stance (1 AP), R = turn
 * 90° (1 AP).
 * Combat time modes (RTS control, user request 2026-10-06, FCombatTimeModeRules): a fight starts in full real time —
 * clicks, stances, attacks and abilities run at once, no Space needed; Space TAP = tactical pause on / off (orders are
 * planned with markers and run on resume); Space HOLD 1.5 s = turn-based fight (from real time or the pause), HOLD 1.5 s
 * there = back to full real time. During turn-based combat ground clicks do not issue real-time moves.
 * Tactical cover (Sprint 12, Gemini spec): a click on a wall / high obstacle (plain click on world geometry, Alt + click
 * on a barricade or any interactable) shows a holographic ghost of the leader pressed against it; a second click there
 * confirms — the leader sprints to the slot and takes cover (planned in the tactical pause; snapped to a cell in the
 * turn-based fight). A click along the same wall shimmies, a ground click leaves the cover, RMB / Esc hides the ghost.
 * N toggles the cover fire mode (corner lean / blind fire) of the selected operative(s); Z / C switch the stance at the wall.
 * Fire posture keys (user decisions 2026-10-06): , = Passive, . = Defensive, / = Aggressive for the SELECTED operative(s)
 * only — the box-selected group, else the leader you control; Alt + , . / = the whole squad (like Alt + Z / C / V for
 * the stance). The action bar buttons follow the same rule (Alt + click = squad). USquadSubsystem::ApplyPostureOrder.
 * Input actions are created in code for now; they move to assets once the editor setup exists.
 * Layout (cleanup 2026-10-15): one class, members defined per input area in Private/Core/CodexTacticsPlayerController*.cpp -
 * the main file (input setup, PlayerTick, shared queries), Selection, Orders (HandleWorldHit), Stance, Items, Time, Menus,
 * Camera, Cover; shared helpers in CodexTacticsPlayerControllerInternal.h (docs/port/REFACTOR_CONTROLLER_2026-10.md).
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

	/** Cursor travel with LMB held that turns a click into a selection box, pixels (Godot DRAG_THRESHOLD 12). */
	UPROPERTY(EditAnywhere, Category = "CodexTactics|Input", meta = (ClampMin = "0"))
	float BoxSelectThreshold = 12.f;

	/** The selection box being dragged, in viewport pixels (false when none). */
	bool GetSelectionBox(FVector2D& OutMin, FVector2D& OutMax) const;

	/**
	 * Selects the living squad members whose feet, centre or head (1.8 m) project into the box
	 * (Godot main.gd _perform_box_selection). Nothing inside: the selection stays. Returns how many were selected.
	 */
	int32 SelectInBox(const FVector2D& Min, const FVector2D& Max);

	/** Ground order for the leader or the box-selected group (Godot moving_group / _get_group_target_positions). */
	void OrderGroupMove(const FVector& Destination, bool bSprint, bool bPlan);

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

	/**
	 * Ctrl + click on Hit outside the grid fight (user decision 2026-10-19): a shootable actor (a live enemy, a barrel, a mine,
	 * a supply crate, a trapped object) or a visible live enemy within ~1.5 m of the point (QuickFireRules::PickTarget) =
	 * the targeted shot exactly as before (IssueTargetedShot); a squad mate = its hint; anything else = IssueQuickFire.
	 */
	void IssueCtrlClick(const FHitResult& Hit);

	/**
	 * Quick fire at Hit's point (user decision 2026-10-19: Ctrl + click on a point with nothing to aim at): the selected
	 * operative(s) - the box-selected group, else the leader - turn to it and fire at it at once
	 * (AOperativeCharacter::OrderQuickFire, area fire; from cover the corner shot / the open shot as a targeted shot).
	 * Tactical pause: planned (a marker), fired on the release. Turn-based: the active operative's grid attack on an enemy /
	 * object there, else a hint. Tutorial lock "Attack.QuickFire" (under "Attack.*"). Public for headless checks.
	 */
	void IssueQuickFire(const FHitResult& Hit);

	/** RMB pressed / released exactly as the key (IA_CameraDragRotate: the camera drag; it never fires). Headless checks. */
	void RightMousePressed() { CameraDragRotateStart(); }
	void RightMouseReleased() { CameraDragRotateStop(); }

	/** Headless checks: Ctrl counts as held (IsAttackModifierDown) - the real Ctrl + click path through HandleWorldHit. */
	void SetSimulatedAttackModifier(bool bDown) { bSimulatedAttackModifier = bDown; }

	/** Action bar stance slot: Standing -> Crouching -> Prone (prone skipped while moving); the squad follows outside the
	 * preparation unless in solo mode (Godot _cycle_leader_stance). */
	void CycleLeaderStance();

	/** Action bar "MOVE": toggles «click an object to move it» (cancels an active placement). Godot _on_relocate_slot_clicked. */
	void ToggleRelocateSelectMode();

	bool IsRelocateSelectMode() const { return bRelocateSelectMode; }

	/**
	 * The optional real-time order lock of 2026-10-05 (only with Codex.RealTimeOrders 0; the default since the RTS
	 * request of 2026-10-06 is 1 = orders run at once in real time): in a real-time wave fight the player gives no orders
	 * then — only the tactical pause, Commander Mode and picking the operative. True (with a throttled hint) when an
	 * order is refused now.
	 */
	bool BlockRealTimeOrder();
	/** FCombatTimeModeRules::GetOrderDispatch says Blocked now (no hint). */
	bool IsRealTimeOrderLocked() const;

	/**
	 * Posture keys / action bar posture buttons (ignored during a dialogue): the selected operative(s), or the whole squad
	 * when bSquadWide (Alt held). See USquadSubsystem::ApplyPostureOrder.
	 */
	void ApplyFirePosture(ESquadFirePosture Posture, bool bSquadWide = false);

	/** Ctrl (either side) is held: the attack-order modifier of a click in every combat mode. */
	bool IsAttackModifierDown() const;

	/** Alt (either side) is held: squad-wide stance / posture orders. */
	bool IsSquadWideModifierDown() const;

	/** Commander Mode on / off (Ctrl + T, the action bar "AUTO" button). */
	void ToggleAutonomy();

	/**
	 * Keys 1-4 caught by a Slate input pre-processor before any widget (user report 2026-10-05: in the fight most
	 * presses never reached the game — a widget with the keyboard focus took them). True when it selected.
	 */
	bool HandleSquadNumberKey(const FKey& Key);

	/** Action bar squad slot / keys 1..3. */
	void SelectSquadMember(int32 RosterIndex) { SelectMember(RosterIndex); }

	/** Space held now and for how long (the HUD charge bar; Godot gorky17_combat_hud.update_charge_progress). */
	bool IsSpaceHeld() const { return SpaceInput.IsPressed(); }
	float GetSpaceHeldTime() const { return SpaceInput.GetHeldTime(); }

	/** The Space-hold dome / rings (Godot tactical_hold_sphere.gd). */
	class AHoldSphereActor* GetHoldSphere() const { return HoldSphere; }

	/** Everything a left click on the world does once the modes above had their say (select, move, interact, ...). */
	void HandleWorldHit(const FHitResult& InHit);

	// --- Tactical cover (Sprint 12) ---

	/**
	 * A click on a wall: preview (ghost), confirm (second click on the same slot), or shimmy along the leader's wall.
	 * True when the click was a cover click. bTurnBased: the slot snaps to the grid, the walk is the grid's.
	 */
	bool TryHandleCoverClick(const FHitResult& Hit, class UTurnBasedCombatSubsystem* TurnBased);

	/** Shows the ghost of Operative at Slot (replaces a previous preview). */
	void ShowCoverPreview(AOperativeCharacter* Operative, const FCoverSlot& Slot);
	void HideCoverPreview();
	bool HasCoverPreview() const;
	const FCoverSlot& GetCoverPreviewSlot() const { return CoverPreviewSlot; }

	/** Orders the previewed cover: the walk now, planned in the pause, the grid walk in turn-based combat. */
	void ConfirmCoverPreview();

	/** N: lean / blind fire toggle of the selected operative(s). */
	void ToggleCoverFireModeKey();

	/**
	 * APC P1: Y (bSquadWide = Alt + Y) - the selected operatives (the whole squad) board the hovered / nearest vehicle, or
	 * get out when they are all aboard (UVehicleSubsystem::HandleBoardKey). Public for the action bar and the smokes.
	 */
	void VehicleKey(bool bSquadWide);

	/** The slot the leader would take for a wall under the cursor hit (smokes / HUD). */
	bool FindCoverSlotForHit(const FHitResult& Hit, FCoverSlot& OutSlot) const;

	/**
	 * Manual corner peek (user request 2026-10-13, rev 5c): Hit lies on the exposed END of Operative's own high cover (his wall
	 * actor; CoverFacingRules::FindPeekEdge, a slot probed near the click for an edge beyond his slot's probes). OutEdgeAlongCm
	 * = the edge along the wall from his cover slot (AOperativeCharacter::OrderCornerPeek).
	 */
	bool FindCornerPeekForHit(const AOperativeCharacter* Operative, const FHitResult& Hit, ECoverFacing& OutSide, float& OutEdgeAlongCm) const;

	/**
	 * The corner peek preview under the cursor (PlayerTick; smokes call it with their own hit): over an edge of Operative's cover
	 * the peek ghost (ACoverGhostActor::ShowCornerPeekFor, the corner-aim pose) stands at that corner and the cursor becomes the
	 * crosshair; else both are cleared. Null Hit = clear. True while it is shown.
	 */
	bool UpdateCornerPeekHover(AOperativeCharacter* Operative, const FHitResult* Hit);

	/** The corner peek preview ghost is up (smokes). */
	bool IsCornerPeekPreviewShown() const;

	/** Godot Alt + Z / C / V: the whole squad changes stance (prone refused while anyone moves). */
	void SetEntireSquadStance(EOperativeStance Stance);

	// --- Minimap (user decisions 2026-10-18, docs/design/MINIMAP.md; CodexTacticsPlayerControllerMap.cpp) ---

	/** F2 / a click on the panel header: the top-right panel switches MAP <-> MESSAGES (Map.Panel). */
	void MapPanelKey();
	/** Tab outside turn-based combat / F3 always: the full-screen map opens / closes (Map.Full). */
	void FullMapKey();
	/** Tab: the full map, in turn-based combat the next operative (as before). Public for the smokes. */
	void TabKey() { TabPressed(); }
	/**
	 * A click at Screen (viewport pixels) on the minimap / the full map: the header switches the panel, LMB moves the camera
	 * there (Map.Camera), RMB orders a move there through HandleWorldHit (every rule of a ground click applies: tutorial locks,
	 * confinement, the combat dispatch, the turn-based grid). False when Screen is not over the map (the world takes it).
	 */
	bool HandleMapClick(const FVector2D& Screen, bool bRightButton);
	/** The wheel over the map zooms it (Map.Zoom); false when the cursor is not over it. */
	bool HandleMapWheel(float Notches);
	/** The mouse cursor is over the corner map / its header / the open full map. */
	bool IsCursorOverMinimap() const;
	/** Esc: closes the full map; true when it was open. */
	bool CloseFullMap();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
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
	/** LMB released: ends a selection box, or performs the click on the world (Godot acts on release). */
	void OnClickReleased();
	void RestartMission();
	/** Ctrl + T: Commander Mode (autonomous squad combat) on / off. */
	void ToggleAutonomyKey() { ToggleAutonomy(); }
	void VehicleKeyPressed();
	void PosturePassiveKey() { ApplyFirePosture(ESquadFirePosture::Passive, IsSquadWideModifierDown()); }
	void PostureDefensiveKey() { ApplyFirePosture(ESquadFirePosture::Defensive, IsSquadWideModifierDown()); }
	void PostureAggressiveKey() { ApplyFirePosture(ESquadFirePosture::Aggressive, IsSquadWideModifierDown()); }
	/** The squad member a world click hit (body / owned actor, else the closest within SelectRadius); nullptr when none. */
	class AOperativeCharacter* FindClickedMember(const FHitResult& Hit) const;
	double LastOrderLockHintTime = -100.0;
	/** The Slate pre-processor for the keys 1-4 (registered while the controller plays). */
	TSharedPtr<class IInputProcessor> SquadKeyProcessor;
	/** Next object click starts its relocation (action bar "MOVE"). */
	bool bRelocateSelectMode = false;
	/** True while a story dialogue blocks world orders (Godot _unhandled_input). */
	bool IsDialogueOpen() const;
	void DialogueNext();
	/**
	 * Enter: the dialogue key while a story dialogue or a tutorial radio / walk-and-talk line is up (tap = next line on the
	 * release, hold DialogueRules::SkipHoldSeconds = skip the whole conversation); otherwise end the squad turn in
	 * turn-based combat (there the tutorial speech is skipped with the strip's SKIP button instead).
	 */
	void EnterPressed();
	void EnterReleased();
	/** The conversation Enter acts on now: the story dialogue window first, else the tutorial speech (not in turn-based). */
	bool HasDialogueForKey() const;
	/** Next line (bSkipAll false) or skip the whole conversation on that conversation; reports Dialogue.Next / Dialogue.Skip. */
	void ApplyDialogueKey(bool bSkipAll);
	/** Enter is down on a conversation (press time in real seconds; the hold works in the tactical pause too). */
	bool bDialogueKeyDown = false;
	bool bDialogueSkipFired = false;
	double DialogueKeyDownTime = 0.0;
	/**
	 * Demo 01 progressive unlock (user request 2026-10-13): true when the tutorial locks CommandId now; the handler then does
	 * nothing (UTutorialSubsystem::CheckCommandLocked raises "Tutorial.Locked.<id>" and one short feed line per family).
	 */
	bool RefuseIfLocked(FName CommandId) const;
	/** Tab: next operative in turn-based combat. */
	void TabPressed();
	/** Turn-based combat subsystem while a grid fight is running, else null. */
	class UTurnBasedCombatSubsystem* GetActiveTurnBased() const;
	void DialogueSkip();
	/** F1: opens / closes the Controls reference (UCodexUISubsystem::ToggleControlsScreen); also while the world is paused. */
	void ControlsKey();
	/** I: opens / closes the inventory drawer (ACodexTacticsHUD::ToggleInventoryDrawer; the tutorial may lock it). */
	void InventoryKey();
	/** O: shows / hides the enemies' vision cones (UVisionConeComponent::SetPlayerEnemyCones; user decision 2026-10-13). */
	void VisionConesKey();
	/**
	 * Rev 5g (user request 2026-10-14): M inside a building - the fixed interior camera <-> the normal top-down view
	 * (UInteriorExitSubsystem::ToggleInteriorView; command Camera.InteriorView, never locked).
	 */
	void InteriorViewKey();
	void SelectMember1() { SelectMember(0); }
	void SelectMember2() { SelectMember(1); }
	void SelectMember3() { SelectMember(2); }
	void SelectMember4() { SelectMember(3); } // Godot KEY_4: Ivan Susanin once recruited
	void StanceStand() { ApplyStance(EOperativeStance::Standing); }
	void StanceCrouch() { ApplyStance(EOperativeStance::Crouching); }
	void StanceProne() { ApplyStance(EOperativeStance::Prone); }
	void ToggleSoloMode();
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
	/** X (Godot switch_weapon): the leader's next arsenal weapon, outside the grid fight. */
	void CycleWeaponKey();
public:
	/** T / action bar "GUARD" (Godot _on_guard_slot_clicked): the leader holds its spot or rejoins the formation. */
	void GuardKey();

	/**
	 * H / J / K / L and the inventory drawer (Godot use_squad_item): the leader uses a provision, feed line. Sprint 15: U / the
	 * drawer = a heat pack on the leader (UTacticalHeatSubsystem::UseHeatPack; 1 AP in turn-based combat).
	 */
	void UseSquadItem(EPersonalItem Item);

	/** Sprint 15: Shift + G / the selector / drawer flare line — the leader's signal flare aim (again: cancel). */
	void StartFlareAim();

	/**
	 * Stealth lure (user decisions 2026-10-18): 5 / the action bar WHISTLE - the selected operative whistles
	 * (UStealthLureSubsystem::Whistle; refused with a feed line when fighting / detected / on cooldown; a tutorial lock target).
	 */
	void StealthWhistleKey();

	/** 6 / the action bar THROW: the stone throw aim (UGrenadeSubsystem::StartLureAim); again while aiming: cancel. */
	void StealthThrowKey();

	/**
	 * Inventory drawer turret / barricade / mine (Godot _start_placement_for_type): selects the type, a squad mate hands
	 * one over when the leader has none, then the placement ghost starts.
	 */
	void StartPlacementForType(EDeployableType Type);

	/**
	 * Sprint 10: Shift + RMB — the leader (or the selected group) holds the object under the cursor (generator, terminal,
	 * gate, barricade) or that spot at all costs. Refused in the turn-based fight (and in the real-time fight only with
	 * the optional order lock, Codex.RealTimeOrders 0).
	 */
	void AssignDefenseUnderCursor();

	/** Inventory "Tripwire" (Sprint 09): two-click tripwire placement for the leader (2 grenades of the squad). */
	void StartTripwirePlacement();

	/** Key P: character profile (Godot main.gd KEY_P -> _toggle_profile_dialog). */
	void ProfilePressed();

	/** Keys 1-4: that member leads (turn-based: the active unit); the leader's own number opens his profile. */
	void SelectMember(int32 RosterIndex);

private:
	/** Ground point under the cursor for the grenade aim (hit, else the thrower's floor plane). */
	bool GetGrenadeAimPoint(FVector& OutPoint) const;
	/** RMB / Esc while aiming a grenade (Godot "Throw cancelled."). Returns true if an aim was cancelled. */
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

	/** Stealth lure keys 5 (whistle) / 6 (throw) - free number keys next to the squad keys 1-4. */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> StealthWhistleAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> StealthThrowAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> QuickSaveAction;

	/** F5 (Godot _perform_quick_save). */
	void QuickSaveKey();

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> QuickLoadAction;

	/** F9 (user decision 2026-10-09): reopen the newest save (USaveGameSubsystem::QuickLoad). */
	void QuickLoadKey();

	void UseMedkit() { UseSquadItem(EPersonalItem::Medkit); }
	void UseCannedFood() { UseSquadItem(EPersonalItem::CannedFood); }
	void UseBread() { UseSquadItem(EPersonalItem::Bread); }
	void UseChocolate() { UseSquadItem(EPersonalItem::Chocolate); }
	void UseHeatPackKey() { UseSquadItem(EPersonalItem::HeatPack); }

	FSpaceInputTracker SpaceInput;

	UPROPERTY(Transient)
	TObjectPtr<class AHoldSphereActor> HoldSphere;

	/** Sprint 12 cover preview. */
	UPROPERTY(Transient)
	TObjectPtr<class ACoverGhostActor> CoverGhost;
	FCoverSlot CoverPreviewSlot;
	TWeakObjectPtr<AOperativeCharacter> CoverPreviewOperative;
	bool bCoverPreviewTurnBased = false;

	/** Rev 5c manual corner peek preview: its own ghost, the cursor, the last hover probe (cached while the cursor rests). */
	UPROPERTY(Transient)
	TObjectPtr<class ACoverGhostActor> CornerPeekGhost;
	bool bCornerPeekCursor = false;
	/** Rev 5e: the hand cursor over a spotted tripwire (or the ground right next to it). */
	bool bTrapHoverCursor = false;
	TWeakObjectPtr<AActor> PeekHoverActor;
	TWeakObjectPtr<AOperativeCharacter> PeekHoverOperative;
	FVector PeekHoverPoint = FVector(TNumericLimits<float>::Max());
	FVector PeekHoverSlotLocation = FVector::ZeroVector;
	bool bPeekHoverEdge = false;
	ECoverFacing PeekHoverSide = ECoverFacing::Right;
	float PeekHoverEdgeAlong = 0.f;

	/** Godot _set_squad_tactical_cease_fire. */
	void SetSquadCeaseFire(bool bCease);
	bool bCeaseFireSet = false;
	double LastClickTime = -1.0;
	FVector2D LastClickPosition = FVector2D::ZeroVector;

	/** SetSimulatedAttackModifier (smokes). */
	bool bSimulatedAttackModifier = false;

	/** LMB held on the world (not consumed by a mode on press) and the box drag from BoxStart. */
	bool bLmbDown = false;
	bool bBoxSelecting = false;
	FVector2D BoxStart = FVector2D::ZeroVector;
	FVector2D BoxCurrent = FVector2D::ZeroVector;
};
