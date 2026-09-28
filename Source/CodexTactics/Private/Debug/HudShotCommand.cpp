// Dev-only console command for a visual HUD / stance check (needs rendering, not -nullrhi):
//   UnrealEditor.exe CodexTactics.uproject /Game/Maps/L_MovementTest -game -windowed -ResX=1600 -ResY=900 -ExecCmds="CodexTactics.HudShot [close]"
// Puts the squad into all three stances, posts a feed message, saves Saved/Screenshots/.../HudShot.png and exits.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/TacticalCameraPawn.h"
#include "Characters/OperativeCharacter.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/BarrelActor.h"
#include "Interactables/InteractionSubsystem.h"
#include "Interactables/RelocationSubsystem.h"
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
		// "close": zoom the tactical camera fully in to inspect characters.
		if (Args.Contains(TEXT("close")))
		{
			if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
			{
				if (ATacticalCameraPawn* Camera = PC->GetPawn<ATacticalCameraPawn>())
				{
					Camera->AddZoomNotches(-30.f);
				}
			}
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		const bool bWalk = Args.Contains(TEXT("walk"));
		const bool bMenu = Args.Contains(TEXT("menu"));
		const bool bPlace = Args.Contains(TEXT("place"));
		FTimerHandle PoseHandle;
		World->GetTimerManager().SetTimer(PoseHandle, FTimerDelegate::CreateLambda([WeakWorld, bWalk, bMenu, bPlace]()
		{
			UWorld* W = WeakWorld.Get();
			if (!W)
			{
				return;
			}
			USquadSubsystem* Squad = W->GetSubsystem<USquadSubsystem>();
			AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
			if (bPlace && Leader)
			{
				// "place": placement mode for a barrel, ghost 4 m to the side.
				FActorSpawnParameters Params;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
				const FVector Spot = Leader->GetActorLocation() + Leader->GetActorForwardVector() * 250.f + FVector(0.f, 0.f, -20.f);
				if (ABarrelActor* Barrel = W->SpawnActor<ABarrelActor>(Spot, FRotator::ZeroRotator, Params))
				{
					URelocationSubsystem* Relocation = W->GetSubsystem<URelocationSubsystem>();
					Relocation->StartRelocate(Barrel, Leader);
					Relocation->UpdatePreview(Barrel->GetActorLocation() - Leader->GetActorRightVector() * 400.f);
				}
			}
			else if (bMenu && Leader)
			{
				// "menu": a barrel right in front of the leader, its action menu opens at once.
				FActorSpawnParameters Params;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
				const FVector Spot = Leader->GetActorLocation() + Leader->GetActorForwardVector() * 190.f + FVector(0.f, 0.f, -20.f);
				if (ABarrelActor* Barrel = W->SpawnActor<ABarrelActor>(Spot, FRotator::ZeroRotator, Params))
				{
					W->GetSubsystem<UInteractionSubsystem>()->RequestInteraction(Barrel);
				}
			}
			else if (bWalk && Leader)
			{
				// "walk": the leader walks and the squad follows, to inspect locomotion.
				Leader->OrderMoveTo(Leader->GetActorLocation() + Leader->GetActorForwardVector() * 1500.f, false);
			}
			else
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
