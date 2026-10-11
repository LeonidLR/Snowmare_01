#pragma once

#include "CoreMinimal.h"

/** One line of the Controls screen: the key(s) and what they do. */
struct CODEXTACTICS_API FCodexControlsRow
{
	FName CommandId;
	FString Keys;
	FString Description;
};

/** One titled group of rows on the Controls screen. */
struct CODEXTACTICS_API FCodexControlsSection
{
	FString Title;
	TArray<FCodexControlsRow> Rows;
};

/**
 * Pure content model of the F1 Controls screen (UCodexControlsScreen): generated from the player command registry
 * (PlayerCommands::GetRegistry / GetSections / GetKeyLabel), so the screen and the registry cannot diverge.
 * No Godot reference (2026-10-11 user decision: tester demo controls reference).
 */
namespace CodexControlsRules
{
	/** The sections in display order; a section with no command is left out. Keys come from PlayerCommands::GetKeyLabel (rebinds). */
	CODEXTACTICS_API TArray<FCodexControlsSection> BuildSections();

	/** Total number of rows over all sections. */
	CODEXTACTICS_API int32 CountRows(const TArray<FCodexControlsSection>& Sections);

	/** Registry problems (empty = fine): a command in no known section, a duplicate id, an empty key label or description. */
	CODEXTACTICS_API TArray<FString> ValidateRegistry();
}
