#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SaveLoadDialogWidget.generated.h"

class UBorder;
class UButton;
class UEditableTextBox;
class UScrollBox;
class UTextBlock;
class UVerticalBox;
class USaveGameSubsystem;

class USaveLoadDialogWidget;

/** One card button of the slot list: remembers its slot and action (UButton::OnClicked carries no payload). */
UCLASS()
class CODEXTACTICS_API USaveCardProxy : public UObject
{
	GENERATED_BODY()

public:
	enum class EAction : uint8 { Select, Load, Delete };

	FString Slot;
	EAction Action = EAction::Select;
	TWeakObjectPtr<USaveLoadDialogWidget> Dialog;

	UFUNCTION()
	void HandleClicked();
};

/** Which pause-menu button opened the dialog (Godot open(mode)). */
UENUM()
enum class ESaveDialogMode : uint8
{
	All,
	Save,
	Load
};

/**
 * Save slot manager: slot name field (suggested «Leonid_NN»), «Save» / «Overwrite» (with an overwrite
 * confirmation), the slot cards newest first (badge AUTO / QUICK / MANUAL, stage, author, time, squad; click selects,
 * a second click on the selected card acts, «Load», delete), status line, «Back to menu».
 * Godot reference: Scenes/ui/pause/save_load_dialog.gd, main.gd _on_save_slot_requested / _on_load_slot_requested.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API USaveLoadDialogWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void Open(ESaveDialogMode Mode);
	void Close();
	bool IsOpen() const;
	bool IsConfirmOpen() const;
	void CancelConfirmation();

	/** Rebuilds the slot cards from disk. */
	void RefreshSavesList();

	FText GetTitleText() const;
	FText GetStatusText() const;
	FString GetSlotNameText() const;
	void SetSlotNameText(const FString& Name);
	FText GetSaveButtonText() const;
	int32 GetCardCount() const { return CardFrames.Num(); }

	/** «Save»: new slot saves at once, an existing one asks for the overwrite confirmation. */
	void PressSave();
	/** Confirmation «Yes, overwrite». */
	void ConfirmOverwrite();
	void LoadSlot(const FString& SlotName);
	void DeleteSlot(const FString& SlotName);
	/** Card click: the first selects it, a second one on the selected card saves / loads (Godot double click). */
	void ClickCard(const FString& SlotName);

protected:
	virtual void NativeOnInitialized() override;

private:
	void BuildDefaultLayout();
	void SetStatus(const FString& Text, bool bError = false);
	void UpdateSaveButton();
	void RequestOverwrite(const FString& SlotName);
	void SaveSlot(const FString& SlotName);
	USaveGameSubsystem* GetSaves() const;

	UFUNCTION() void HandleSave();
	UFUNCTION() void HandleClose();
	UFUNCTION() void HandleConfirmYes();
	UFUNCTION() void HandleConfirmNo();
	UFUNCTION() void HandleSlotNameChanged(const FText& Text);

	ESaveDialogMode CurrentMode = ESaveDialogMode::All;
	FString SelectedSlot;
	FString PendingOverwrite;

	UPROPERTY() TObjectPtr<UWidget> Panel;
	UPROPERTY() TObjectPtr<UTextBlock> TitleText;
	UPROPERTY() TObjectPtr<UEditableTextBox> SlotNameEdit;
	UPROPERTY() TObjectPtr<UButton> SaveButton;
	UPROPERTY() TObjectPtr<UTextBlock> SaveButtonText;
	UPROPERTY() TObjectPtr<UVerticalBox> SavesList;
	UPROPERTY() TObjectPtr<UTextBlock> StatusText;
	UPROPERTY() TObjectPtr<UWidget> ConfirmOverlay;
	UPROPERTY() TObjectPtr<UTextBlock> ConfirmMessage;

	UPROPERTY() TArray<TObjectPtr<USaveCardProxy>> CardProxies;
	UPROPERTY() TMap<FString, TObjectPtr<UBorder>> CardFrames;
};
