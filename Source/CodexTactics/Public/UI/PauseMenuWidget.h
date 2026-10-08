#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PauseMenuWidget.generated.h"

class UButton;
class UTextBlock;

/**
 * «⏸️ PAUSE MENU» (Esc when no other window is open): continue, save, load (disabled without saves), main menu, quit;
 * the status line names the newest save. The world is paused while it is open. Built in C++ (restyle through a Widget
 * Blueprint subclass).
 * Godot reference: Scenes/ui/pause/pause_menu_dialog.gd, main.gd _open_pause_menu / _close_pause_menu,
 * _on_pause_*_pressed.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API UPauseMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Shows the menu, pauses the world, refreshes the save status. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Pause")
	void Open();

	/** Hides the menu; bResume also unpauses the world. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Pause")
	void Close(bool bResume = true);

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Pause")
	bool IsOpen() const;

	FText GetStatusText() const;
	bool IsLoadEnabled() const;

	/** Button actions (also used by headless checks). */
	void Resume();
	void OpenSave();
	void OpenLoad();
	void ToMainMenu();

protected:
	virtual void NativeOnInitialized() override;

private:
	void BuildDefaultLayout();

	UFUNCTION() void HandleResume();
	UFUNCTION() void HandleSave();
	UFUNCTION() void HandleLoad();
	UFUNCTION() void HandleMainMenu();
	UFUNCTION() void HandleQuit();

	UPROPERTY()
	TObjectPtr<UWidget> Panel;

	UPROPERTY()
	TObjectPtr<UButton> LoadButton;

	UPROPERTY()
	TObjectPtr<UTextBlock> StatusText;
};
