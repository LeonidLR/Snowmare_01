#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UI/Frontend/CodexFrontendRules.h"
#include "CodexUISubsystem.generated.h"

class APlayerController;
class UCodexActivatableScreen;
class UCodexPrimaryLayout;
class UCommonActivatableWidgetContainerBase;
struct FStreamableHandle;

DECLARE_DYNAMIC_DELEGATE_OneParam(FCodexConfirmResultDelegate, ECodexConfirmResult, Result);

/**
 * Frontend UI service: owns the primary layout of the local player (one per world, created on demand above the HUD)
 * and pushes screens onto its layers by tag. Screen classes come from UCodexFrontendSettings (soft references, loaded
 * asynchronously; a class already in memory is pushed at once). Confirm dialogs: ShowConfirm.
 * No Godot reference (2026-10-08 user decision: professional frontend).
 */
UCLASS()
class CODEXTACTICS_API UCodexUISubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UCodexUISubsystem* Get(const UObject* WorldContext);

	/** The layout of this player's world (created and added to the viewport if missing). */
	UCodexPrimaryLayout* GetOrCreateLayout(APlayerController* PlayerController);

	/** The current world's layout, or null if none was created yet. */
	UCodexPrimaryLayout* GetLayout() const;

	/**
	 * Pushes the screen registered for ScreenTag (settings) onto LayerTag. InitFunc runs before activation (set modes,
	 * texts); OnPushed after (null when it failed). Uses the first local player controller.
	 */
	void PushScreen(FGameplayTag LayerTag, FGameplayTag ScreenTag, TFunction<void(UCodexActivatableScreen&)> InitFunc = {},
		TFunction<void(UCodexActivatableScreen*)> OnPushed = {});

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|UI", meta = (DisplayName = "Push Screen", GameplayTagFilter = "Codex.UI"))
	void BP_PushScreen(FGameplayTag LayerTag, FGameplayTag ScreenTag) { PushScreen(LayerTag, ScreenTag); }

	/** Top (displayed) screen of a layer, or null. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|UI")
	UCodexActivatableScreen* GetTopScreen(FGameplayTag LayerTag) const;

	/** Number of screens on a layer. */
	int32 GetScreenCount(FGameplayTag LayerTag) const;

	/** A screen with this tag on any layer, or null. */
	UCodexActivatableScreen* FindScreen(FGameplayTag ScreenTag) const;

	/** Removes a screen from whatever layer holds it. */
	void RemoveScreen(UCodexActivatableScreen& Screen);

	/** Removes every screen of a layer. */
	void ClearLayer(FGameplayTag LayerTag);

	/** Esc reached the game (CommonUI did not consume it): back on the topmost screen of any layer; false if none. */
	bool HandleBackOnTopScreen();

	/** Any screen on the GameMenu / Menu / Modal layers (the game must not take clicks / keys). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|UI")
	bool IsAnyMenuOpen() const;

	/** Yes / No (or OK) dialog on the Modal layer; OnResult runs once with the choice (back = Cancelled). */
	void ShowConfirm(ECodexConfirmType Type, const FText& Title, const FText& Message, TFunction<void(ECodexConfirmResult)> OnResult);

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|UI", meta = (DisplayName = "Show Confirm"))
	void BP_ShowConfirm(ECodexConfirmType Type, FText Title, FText Message, FCodexConfirmResultDelegate OnResult);

	/** C++ fallback class of a screen tag (used when the configured Widget Blueprint is missing). */
	static TSubclassOf<UCodexActivatableScreen> GetNativeScreenClass(FGameplayTag ScreenTag);

private:
	UCommonActivatableWidgetContainerBase* GetLayerContainer(FGameplayTag LayerTag) const;
	void PushLoadedClass(FGameplayTag LayerTag, FGameplayTag ScreenTag, UClass* LoadedClass, TFunction<void(UCodexActivatableScreen&)> InitFunc,
		TFunction<void(UCodexActivatableScreen*)> OnPushed);

	/** Loads every configured screen class in the background once a layout exists (later pushes are then instant). */
	void PreloadScreenClasses();

	TWeakObjectPtr<UCodexPrimaryLayout> Layout;
	TSharedPtr<FStreamableHandle> PreloadHandle;
};
