#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Tactics/CoverTypes.h"
#include "CoverQueryLibrary.generated.h"

/**
 * Blueprint / editor-Python access to the cover traces (CoverTraceRules), for level checks and design tools: "does the wall
 * here give a cover slot, how high, which corners are exposed". The Demo 01 auto-layout verify step uses it to check the hut
 * door stack spots (Scripts/Editor/build_demo01_lake_layout.py, rev 5c). UE-only, no Godot reference.
 */
UCLASS()
class CODEXTACTICS_API UCoverQueryLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * The cover slot an operative would take at a clicked wall point (CoverTraceRules::FindCoverSlotAt): traces along Direction
	 * (planar, towards the wall). False when nothing cover-like is there.
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Cover", meta = (WorldContext = "WorldContextObject"))
	static bool FindCoverSlotAt(const UObject* WorldContextObject, FVector Point, FVector Direction, FCoverSlot& OutSlot);

	/** Name of the slot's height class ("High", "Low", "None"). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Cover")
	static FString GetCoverHeightName(const FCoverSlot& Slot);
};
