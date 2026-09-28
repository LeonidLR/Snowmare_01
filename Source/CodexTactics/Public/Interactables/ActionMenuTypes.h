#pragma once

#include "CoreMinimal.h"
#include "ActionMenuTypes.generated.h"

/**
 * Content of the object action menu (title, description, confirm / relocate / cancel buttons).
 * Godot reference: main.gd `_open_action_menu(target, title, desc, confirm, cancel, is_disabled, allow_relocate, relocate)`.
 */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FActionMenuSpec
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|ActionMenu")
	FText Title;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|ActionMenu")
	FText Description;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|ActionMenu")
	FText ConfirmText;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|ActionMenu")
	FText CancelText;

	/** Confirm button greyed out (e.g. «Нет спичек», «Нужна емкость»). */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|ActionMenu")
	bool bConfirmDisabled = false;

	/** Show the relocate button («Переместить» / «Вытолкать»). */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|ActionMenu")
	bool bAllowRelocate = false;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|ActionMenu")
	FText RelocateText;
};

/** What an object answers to a menu request: open the menu, or just post a line to the feed. */
struct CODEXTACTICS_API FActionMenuRequest
{
	bool bOpenMenu = false;
	FActionMenuSpec Menu;
	/** Feed line when no menu opens (Godot `_on_quest_message(speaker, text)`). */
	FText MessageSpeaker;
	FText Message;

	static FActionMenuRequest MakeMenu(const FText& Title, const FText& Description, const FText& Confirm, const FText& Cancel,
		bool bDisabled, bool bRelocate = false, const FText& Relocate = FText::GetEmpty())
	{
		FActionMenuRequest Request;
		Request.bOpenMenu = true;
		Request.Menu.Title = Title;
		Request.Menu.Description = Description;
		Request.Menu.ConfirmText = Confirm;
		Request.Menu.CancelText = Cancel;
		Request.Menu.bConfirmDisabled = bDisabled;
		Request.Menu.bAllowRelocate = bRelocate;
		Request.Menu.RelocateText = Relocate;
		return Request;
	}

	static FActionMenuRequest MakeMessage(const FText& Speaker, const FText& Text)
	{
		FActionMenuRequest Request;
		Request.MessageSpeaker = Speaker;
		Request.Message = Text;
		return Request;
	}
};
