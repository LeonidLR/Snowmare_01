#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CodexEventBus.generated.h"

class AOperativeCharacter;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCodexSoldierEvent, AOperativeCharacter*, Soldier);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCodexItemUsedEvent, AOperativeCharacter*, Soldier, const FString&, ItemId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCodexMineSpottedEvent, AActor*, Mine, AOperativeCharacter*, Spotter);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCodexLineEvent, const FString&, Speaker, const FString&, Text);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCodexSimpleEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCodexPowerEvent, bool, bPowered);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCodexGameSavedEvent, const FString&, SlotName, bool, bAutosave);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCodexGameLoadedEvent, const FString&, SlotName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCodexSoldierReasonEvent, AOperativeCharacter*, Soldier, const FString&, Reason);

/**
 * Global game events for Blueprints, audio, VFX and UI hooks: the Godot EventBus signals the game actually emits.
 * Gameplay systems still talk to each other directly (as in Godot, where the bus mostly feeds UI); this is the one
 * place content can listen to without knowing the emitter.
 * Not carried over: the signals Godot declares but never emits (wave_started, battle_victory, soldier_health_changed,
 * tactical_pause_toggled, ...), panic / allegiance ones (those components are not ported), camera_shake_requested
 * (Godot's fallback only; the camera is called directly).
 * Godot reference: Scripts/events/event_bus.gd.
 */
UCLASS()
class CODEXTACTICS_API UCodexEventBus : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** The bus of Context's game instance (null without one, e.g. in unit tests). */
	static UCodexEventBus* Get(const UObject* Context);

	/** A squad member became the leader (Godot main.gd select_new_leader). */
	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Events")
	FCodexSoldierEvent OnSquadMemberSelected;

	/** A stat point was spent / taken back in the profile (Godot profile_dialog.gd). */
	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Events")
	FCodexSoldierEvent OnSoldierStatsUpdated;

	/** A provision was used from the inventory drawer: «MEDKIT», «CANNED_FOOD», «BREAD», «CHOCOLATE». */
	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Events")
	FCodexItemUsedEvent OnItemUsed;

	/** A hidden mine was spotted (Godot player.gd _on_mine_spotted). */
	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Events")
	FCodexMineSpottedEvent OnMineSpotted;

	/** An operative fell (Godot player.gd die). */
	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Events")
	FCodexSoldierEvent OnSoldierDowned;

	/** Every line posted to the message feed (Godot main.gd _on_quest_message -> dialogue_line_displayed). */
	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Events")
	FCodexLineEvent OnDialogueLineDisplayed;

	/** The bottom dialogue window finished / was closed (Godot dialogue_box.gd). */
	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Events")
	FCodexSimpleEvent OnDialogueFinished;

	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Events")
	FCodexSoldierEvent OnRageStarted;

	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Events")
	FCodexSoldierEvent OnRageEnded;

	/** Godot EventBus.soldier_panicked / soldier_calmed (PanicComponent). */
	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Events")
	FCodexSoldierReasonEvent OnSoldierPanicked;

	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Events")
	FCodexSoldierReasonEvent OnSoldierCalmed;

	/** The generator started / broke down / was repaired (Godot interactable.gd). */
	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Events")
	FCodexPowerEvent OnGeneratorStateChanged;

	/** Godot save_manager.gd save_game / load_game. */
	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Events")
	FCodexGameSavedEvent OnGameSaved;

	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Events")
	FCodexGameLoadedEvent OnGameLoaded;
};
