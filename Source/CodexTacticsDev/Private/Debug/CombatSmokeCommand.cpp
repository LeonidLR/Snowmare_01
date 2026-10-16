#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "CodexTactics.h"

namespace CombatCommands
{
	void FinishPrep(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}

		if (UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>())
		{
			const EGameFlowResult Result = Flow->FinishPreparation();
			UE_LOG(LogCodexTactics, Display, TEXT("CodexTactics.FinishPrep result: %s"), *UEnum::GetValueAsString(Result));
		}
	}

	static FAutoConsoleCommandWithWorldAndArgs CommandFinishPrep(
		TEXT("CodexTactics.FinishPrep"),
		TEXT("Dev command: skips the combat preparation phase immediately and starts the wave."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&FinishPrep));
}

#endif // !UE_BUILD_SHIPPING
