#include "Tactics/CoverQueryLibrary.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Tactics/CoverTraceRules.h"

bool UCoverQueryLibrary::FindCoverSlotAt(const UObject* WorldContextObject, FVector Point, FVector Direction, FCoverSlot& OutSlot)
{
	OutSlot = FCoverSlot();
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	Direction.Z = 0.f;
	if (!World || !Direction.Normalize())
	{
		return false;
	}
	return CoverTraceRules::FindCoverSlotAt(World, Point, Direction, OutSlot);
}

FString UCoverQueryLibrary::GetCoverHeightName(const FCoverSlot& Slot)
{
	switch (Slot.Height)
	{
	case ECoverHeight::HighCover: return TEXT("High");
	case ECoverHeight::LowCover: return TEXT("Low");
	default: return TEXT("None");
	}
}
