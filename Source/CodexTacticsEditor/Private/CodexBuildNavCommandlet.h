#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "CodexBuildNavCommandlet.generated.h"

/**
 * Bakes the navmesh of one map headlessly and saves only that map package.
 * No Godot reference: Unreal-only tooling (docs/port/TRAPS.md, navmesh rows; Scripts/bake_navmesh.ps1).
 *
 * UnrealEditor-Cmd.exe CodexTactics.uproject -run=CodexTacticsEditor.CodexBuildNav -Map=L_Demo01_B_FrozenFactory [-NoSave] [-MinTiles=1]
 * (module-qualified: CodexTacticsEditor loads at PostEngineInit, after commandlet lookup; the prefix makes the launcher load it).
 * -Map accepts a bare name (looked up under /Game/Maps) or a long package name (/Game/Maps/L_X).
 * Exit code 0 = built (and saved), 1 = bad arguments / load failure, 2 = build failed or the Default agent has fewer
 * than MinTiles active tiles (nothing saved), 3 = save failed.
 */
UCLASS()
class UCodexBuildNavCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UCodexBuildNavCommandlet();

	virtual int32 Main(const FString& Params) override;
};
