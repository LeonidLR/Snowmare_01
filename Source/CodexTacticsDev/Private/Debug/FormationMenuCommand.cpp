// Dev console stand-in for the hold-G radial formation menu (not built yet; docs/design/FORMATION_SECTORS.md "Selection", contract
// docs/design/FORMATION_CONTRACT.md 9): Codex.Formation.Menu <auto | line | diamond | echelon_right | blizzard | column | hedgehog |
// echelon_left | all_forward> sets the squad's formation choice (USquadSubsystem::SetFormationMenuChoice; a manual choice holds until auto).
// The rules v2.1 wall guard keeps the squad whole whatever is chosen (the HUD shows "Echelon left -> Column (wall)").
// Godot reference: none (UE-only).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Engine/World.h"
#include "Formation/FormationMenuRules.h"
#include "HAL/IConsoleManager.h"

namespace FormationMenuCommand
{
	void Run(const TArray<FString>& Args, UWorld* World)
	{
		USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
		if (!Squad)
		{
			return;
		}
		TOptional<EFormationMenuItem> Choice;
		if (Args.Num() == 0 || !FormationMenuRules::ParseChoice(Args[0], Choice))
		{
			const TOptional<EFormationMenuItem> Now = Squad->GetFormationMenuChoice();
			UE_LOG(LogCodexTactics, Display, TEXT("Codex.Formation.Menu: %s (usage: auto | line | diamond | echelon_right | blizzard | column | hedgehog | echelon_left | all_forward)"),
				Now.IsSet() ? FormationMenuRules::ToString(Now.GetValue()) : TEXT("auto"));
			return;
		}
		Squad->SetFormationMenuChoice(Choice);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(TEXT("Codex.Formation.Menu"),
		TEXT("Dev stand-in for the hold-G formation menu: auto | line | diamond | echelon_right | blizzard | column | hedgehog | echelon_left | all_forward ")
		TEXT("(a manual choice holds until auto; the wall guard keeps the squad whole)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
