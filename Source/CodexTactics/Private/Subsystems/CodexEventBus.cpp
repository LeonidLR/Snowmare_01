#include "Subsystems/CodexEventBus.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

UCodexEventBus* UCodexEventBus::Get(const UObject* Context)
{
	const UWorld* World = Context ? Context->GetWorld() : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UCodexEventBus>() : nullptr;
}
