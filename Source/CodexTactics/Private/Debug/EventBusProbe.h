#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Core/PlayerCommands.h"
#include "GameFramework/Actor.h"
#include "EventBusProbe.generated.h"

class AOperativeCharacter;
class UCodexEventBus;

/** Dev-only listener for EventBusSmoke: counts every UCodexEventBus event it hears. */
UCLASS(Transient)
class UEventBusProbe : public UObject
{
	GENERATED_BODY()

public:
	void Listen(UCodexEventBus* Bus);

	TMap<FString, int32> Counts;
	FString LastItem;
	FString LastSlot;
	bool bLastPowered = false;

	UFUNCTION() void OnSelected(AOperativeCharacter* Soldier) { ++Counts.FindOrAdd(TEXT("selected")); }
	UFUNCTION() void OnStats(AOperativeCharacter* Soldier) { ++Counts.FindOrAdd(TEXT("stats")); }
	UFUNCTION() void OnItem(AOperativeCharacter* Soldier, const FString& ItemId) { ++Counts.FindOrAdd(TEXT("item")); LastItem = ItemId; }
	UFUNCTION() void OnDowned(AOperativeCharacter* Soldier) { ++Counts.FindOrAdd(TEXT("downed")); }
	UFUNCTION() void OnLine(const FString& Speaker, const FString& Text) { ++Counts.FindOrAdd(TEXT("line")); }
	UFUNCTION() void OnDialogueFinished() { ++Counts.FindOrAdd(TEXT("dialogue")); }
	UFUNCTION() void OnRageStarted(AOperativeCharacter* Soldier) { ++Counts.FindOrAdd(TEXT("rage+")); }
	UFUNCTION() void OnRageEnded(AOperativeCharacter* Soldier) { ++Counts.FindOrAdd(TEXT("rage-")); }
	UFUNCTION() void OnGenerator(bool bPowered) { ++Counts.FindOrAdd(TEXT("generator")); bLastPowered = bPowered; }
	UFUNCTION() void OnSaved(const FString& SlotName, bool bAutosave) { ++Counts.FindOrAdd(TEXT("saved")); LastSlot = SlotName; }
	UFUNCTION() void OnLoaded(const FString& SlotName) { ++Counts.FindOrAdd(TEXT("loaded")); }

	int32 Count(const TCHAR* Key) const { const int32* Value = Counts.Find(Key); return Value ? *Value : 0; }

	// --- Named gameplay events (PMCTripwireSmoke, CoverEventSmoke): ListenGameplayEvents binds only these. ---

	/** Every OnGameplayEvent in order with its subject's name. */
	TArray<FName> EventNames;
	TArray<FName> EventSubjects;
	/** Every OnGameplayEventDetailed in order. */
	TArray<FName> DetailedNames;
	TArray<FGameplayEventContext> DetailedContexts;

	void ListenGameplayEvents(UCodexEventBus* Bus);

	UFUNCTION() void OnGameplayEvent(FName EventName, AActor* Subject)
	{
		EventNames.Add(EventName);
		EventSubjects.Add(Subject ? Subject->GetFName() : NAME_None);
	}
	UFUNCTION() void OnGameplayEventDetailed(FName EventName, const FGameplayEventContext& Context)
	{
		DetailedNames.Add(EventName);
		DetailedContexts.Add(Context);
	}

	/** How many times EventName was heard (optionally only with this subject). */
	int32 CountEvent(FName EventName, FName SubjectId = NAME_None) const
	{
		int32 Total = 0;
		for (int32 Index = 0; Index < EventNames.Num(); ++Index)
		{
			Total += EventNames[Index] == EventName && (SubjectId.IsNone() || EventSubjects[Index] == SubjectId) ? 1 : 0;
		}
		return Total;
	}
};
