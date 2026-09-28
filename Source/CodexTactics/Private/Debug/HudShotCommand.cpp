// Dev-only console command for a visual HUD / stance check (needs rendering, not -nullrhi):
//   UnrealEditor.exe CodexTactics.uproject /Game/Maps/L_MovementTest -game -windowed -ResX=1600 -ResY=900 -ExecCmds=CodexTactics.HudShot
// Puts the squad into all three stances, posts a feed message, saves Saved/Screenshots/.../HudShot.png and exits.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UI/GameMessageSubsystem.h"
#include "UnrealClient.h"

namespace HudShot
{
	void Pose(UWorld* World)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		TArray<AOperativeCharacter*> Members = Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>();
		Members.Sort([](const AOperativeCharacter& A, const AOperativeCharacter& B) { return A.SquadIndex < B.SquadIndex; });
		const EOperativeStance Stances[] = { EOperativeStance::Standing, EOperativeStance::Crouching, EOperativeStance::Prone };
		for (int32 Index = 0; Index < Members.Num(); ++Index)
		{
			Members[Index]->SetStance(Stances[Index % 3]);
		}
		if (UGameMessageSubsystem* Messages = World->GetSubsystem<UGameMessageSubsystem>())
		{
			Messages->PostMessage(FText::FromString(TEXT("ШТАБ")), FText::FromString(TEXT("✅ Проверка HUD: лента сообщений и стойки отряда.")));
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTimerHandle PoseHandle;
		World->GetTimerManager().SetTimer(PoseHandle, FTimerDelegate::CreateLambda([WeakWorld]()
		{
			if (UWorld* W = WeakWorld.Get())
			{
				Pose(W);
			}
		}), 3.f, false);
		FTimerHandle ShotHandle;
		World->GetTimerManager().SetTimer(ShotHandle, FTimerDelegate::CreateLambda([]()
		{
			const FString File = FPaths::ScreenShotDir() / TEXT("HudShot.png");
			FScreenshotRequest::RequestScreenshot(File, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
			UE_LOG(LogCodexTactics, Display, TEXT("HudShot saved to %s"), *File);
		}), 4.5f, false);
		FTimerHandle ExitHandle;
		World->GetTimerManager().SetTimer(ExitHandle, FTimerDelegate::CreateLambda([]()
		{
			FPlatformMisc::RequestExit(false, TEXT("HudShot"));
		}), 6.f, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.HudShot"),
		TEXT("Dev check: squad in all stances + feed message, saves HudShot.png, then exits (needs rendering)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
