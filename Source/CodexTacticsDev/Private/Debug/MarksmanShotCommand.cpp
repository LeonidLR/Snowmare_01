// Dev-only console command for a visual check of the Marksman art (needs rendering, not -nullrhi):
//   UnrealEditor.exe CodexTactics.uproject /Game/Maps/L_MovementTest -game -windowed -ResX=1600 -ResY=900 -ExecCmds="CodexTactics.MarksmanShot"
// Three marksmen (BP_Enemy_Marksman, AI off) stand, crouch and lie 3.5 m in front of the leader facing him; the camera
// zooms in; saves Saved/Screenshots/.../MarksmanShot.png and exits.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/TacticalCameraPawn.h"
#include "Characters/MarksmanEnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/WaveSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "UnrealClient.h"

namespace MarksmanShot
{
	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld]()
		{
			UWorld* W = WeakWorld.Get();
			USquadSubsystem* Squad = W ? W->GetSubsystem<USquadSubsystem>() : nullptr;
			AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
			if (!Leader)
			{
				FPlatformMisc::RequestExit(false, TEXT("MarksmanShot"));
				return;
			}
			const EOperativeStance Stances[] = { EOperativeStance::Standing, EOperativeStance::Crouching, EOperativeStance::Prone };
			const FVector Forward = Leader->GetActorForwardVector();
			const FVector Right = Leader->GetActorRightVector();
			for (int32 Index = 0; Index < 3; ++Index)
			{
				const FVector At = Leader->GetActorLocation() + Forward * 350.f + Right * (Index - 1) * 170.f + FVector(0.f, 0.f, 20.f);
				if (AMarksmanEnemyCharacter* Marksman = Cast<AMarksmanEnemyCharacter>(
					W->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::Marksman, At, (-Forward).Rotation())))
				{
					Marksman->SetActorTickEnabled(false); // no AI: a still pose per stance
					Marksman->SetMarksmanStance(Stances[Index]);
				}
			}
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->bTacticalCeaseFire = true;
			}
			if (APlayerController* PC = UGameplayStatics::GetPlayerController(W, 0))
			{
				if (ATacticalCameraPawn* Camera = PC->GetPawn<ATacticalCameraPawn>())
				{
					Camera->AddZoomNotches(-30.f);
				}
			}
			FTimerHandle ShotHandle;
			W->GetTimerManager().SetTimer(ShotHandle, FTimerDelegate::CreateLambda([WeakWorld]()
			{
				FScreenshotRequest::RequestScreenshot(FPaths::ScreenShotDir() / TEXT("MarksmanShot.png"), false, false);
				if (UWorld* W2 = WeakWorld.Get())
				{
					FTimerHandle ExitHandle;
					W2->GetTimerManager().SetTimer(ExitHandle, FTimerDelegate::CreateLambda([]()
					{
						FPlatformMisc::RequestExit(false, TEXT("MarksmanShot"));
					}), 1.5f, false);
				}
			}), 4.f, false);
		}), 3.f, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.MarksmanShot"),
		TEXT("Dev: screenshot of three marksmen standing / crouching / prone in front of the leader, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
