#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Characters/ProgressionRules.h"
#include "ProfileDialogWidget.generated.h"

class AOperativeCharacter;
class UButton;
class UProgressBar;
class UTextBlock;
class UWidget;

/**
 * Character profile (key P): portrait, name, role, level, free points, then EXP / HP / luck / accuracy / fortitude rows
 * with bars and - / + buttons for the stat points, «< Пред.» / «Закрыть [P]» / «След. >» to page through the squad.
 * Refreshes every frame while open. Built in C++ (restyle through a Widget Blueprint subclass).
 * Godot reference: Scenes/ui/profile/profile_dialog.gd (430 x 530, gold frame), main.gd _toggle_profile_dialog /
 * _populate_profile_dialog.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API UProfileDialogWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Shows the profile of Member (the leader when null). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Profile")
	void Open(AOperativeCharacter* Member);

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Profile")
	void Close();

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Profile")
	bool IsOpen() const;

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Profile")
	AOperativeCharacter* GetMember() const { return Member.Get(); }

	/** Next (+1) / previous (-1) squad member, wrapping around. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Profile")
	void SwitchMember(int32 Direction);

	/** A - (Direction < 0) or + click on a stat row. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Profile")
	void ClickStat(EProgressStat Stat, int32 Direction);

	void Refresh();

	/** Header lines: name, role, «Уровень: N», «Свободных очков: N». */
	FString GetHeaderText() const;

	/** Row texts: 0 EXP, 1 HP, 2 luck, 3 accuracy, 4 fortitude. */
	FString GetRowText(int32 Row) const;

	bool IsStatButtonEnabled(EProgressStat Stat, int32 Direction) const;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	void BuildDefaultLayout();
	UTextBlock* MakeText(const FName& Name, int32 Size, const FLinearColor& Color);

	UFUNCTION() void HandleMinusHealth();
	UFUNCTION() void HandlePlusHealth();
	UFUNCTION() void HandleMinusLuck();
	UFUNCTION() void HandlePlusLuck();
	UFUNCTION() void HandleMinusAccuracy();
	UFUNCTION() void HandlePlusAccuracy();
	UFUNCTION() void HandleMinusFortitude();
	UFUNCTION() void HandlePlusFortitude();
	UFUNCTION() void HandlePrev();
	UFUNCTION() void HandleNext();
	UFUNCTION() void HandleClose();

	TWeakObjectPtr<AOperativeCharacter> Member;

	UPROPERTY()
	TObjectPtr<UWidget> Panel;

	UPROPERTY()
	TObjectPtr<UTextBlock> PortraitText;

	UPROPERTY()
	TObjectPtr<UTextBlock> NameText;

	UPROPERTY()
	TObjectPtr<UTextBlock> RoleText;

	UPROPERTY()
	TObjectPtr<UTextBlock> LevelText;

	UPROPERTY()
	TObjectPtr<UTextBlock> UnspentText;

	UPROPERTY()
	TArray<TObjectPtr<UTextBlock>> RowTexts;

	UPROPERTY()
	TArray<TObjectPtr<UProgressBar>> RowBars;

	/** Per stat (EProgressStat order): - and + buttons. */
	UPROPERTY()
	TArray<TObjectPtr<UButton>> MinusButtons;

	UPROPERTY()
	TArray<TObjectPtr<UButton>> PlusButtons;
};
