// Dev-only console command for a visual HUD / stance check (needs rendering, not -nullrhi):
//   UnrealEditor.exe CodexTactics.uproject /Game/Maps/L_MovementTest -game -windowed -ResX=1600 -ResY=900 -ExecCmds="CodexTactics.HudShot [close]"
// "failed": an operative dies -> mission-failed screen.
// "shoot": Ctrl + click shot at a barrel with the world slowed down, to see the tracer, target flash and a plan marker.
// Otherwise puts the squad into all three stances, posts a feed message, saves Saved/Screenshots/.../HudShot.png and exits.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/TacticalCameraPawn.h"
#include "Characters/OperativeCharacter.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/CombatFeedbackSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Containers/Ticker.h"
#include "GameFramework/WorldSettings.h"
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
		const bool bShoot = Args.Contains(TEXT("shoot"));
		const bool bFailed = Args.Contains(TEXT("failed"));
		FTimerHandle PoseHandle;
		World->GetTimerManager().SetTimer(PoseHandle, FTimerDelegate::CreateLambda([WeakWorld, bWalk, bMenu, bPlace, bShoot, bFailed]()
		{
			UWorld* W = WeakWorld.Get();
			if (!W)
			{
				return;
			}
			USquadSubsystem* Squad = W->GetSubsystem<USquadSubsystem>();
			AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
			if ((bShoot || bFailed) && Leader)
			{
				// World time stops (slowed shot / game over): take the screenshot and exit on real time.
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
				{
					const FString File = FPaths::ScreenShotDir() / TEXT("HudShot.png");
					FScreenshotRequest::RequestScreenshot(File, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
					FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
					{
						FPlatformMisc::RequestExit(false, TEXT("HudShot"));
						return false;
					}), 1.5f);
					return false;
				}), 0.5f);
			}
			if (bFailed && Squad && Squad->GetMembers().Num() > 1)
			{
				Squad->GetMembers()[1]->HealthComponent->ApplyDirectHealthLoss(10000.f, TEXT("HudShot"));
			}
			else if (bShoot && Leader)
			{
				FActorSpawnParameters Params;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
				const FVector Spot = Leader->GetActorLocation() + Leader->GetActorForwardVector() * 600.f + FVector(0.f, 0.f, -20.f);
				if (ABarrelActor* Barrel = W->SpawnActor<ABarrelActor>(Spot, FRotator::ZeroRotator, Params))
				{
					UCombatFeedbackSubsystem* Feedback = W->GetSubsystem<UCombatFeedbackSubsystem>();
					Feedback->SpawnWaypointMarker(Leader->GetActorLocation() - Leader->GetActorRightVector() * 300.f
						- FVector(0.f, 0.f, Leader->GetSimpleCollisionHalfHeight()));
					W->GetWorldSettings()->SetTimeDilation(0.01f); // freeze the fading effects for the screenshot
					Feedback->HighlightTarget(Barrel);
					Leader->ShootAtObject(Barrel);
				}
			}
			else if (bPlace && Leader)
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
