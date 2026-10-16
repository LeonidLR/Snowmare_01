// Dev-only console command: rendered knockdown proof for a remote review (user request 2026-10-11, watched on a phone).
// Needs rendering (no -nullrhi); run offscreen with a fixed frame time so the sequence plays back at real speed:
//   UnrealEditor.exe CodexTactics.uproject /Game/Maps/L_MovementTest -game -RenderOffscreen -ResX=1280 -ResY=720 -nosound
//     -benchmark -fps=30 -ExecCmds="CodexTactics.KnockdownShots"
// Output: Saved/Screenshots/Phone/frames/knockdown_faster_getup/f_NNNN.png (real time: a heavy hit floors the commander,
// he lies 0.6 s and gets up in 0.8 s), Saved/Screenshots/Phone/knockdown_tb_getup.png (turn-based: he lies on his turn,
// the panel offers «GET UP (2 AP) [Z]») and frames/knockdown_tb_getup/f_NNNN.png (the paid get-up). The camera follows the
// operative (beside / ahead of him), never fixed map coordinates. Driven only from inside the engine (no keys, no desktop
// capture). Codex.Knockdown.Chance is forced to 1 for the run.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/KnockdownComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Debug/FrontendSmokeUtils.h"
#include "Debug/SmokeUtils.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "UnrealClient.h"

namespace KnockdownShots
{
	/** One step: frames to wait before the next step, or -1 to run again next frame (waiting for a condition). */
	using FStep = TFunction<int32(UWorld*)>;

	struct FRunner
	{
		TArray<FStep> Steps;
		int32 Index = 0;
		int32 Wait = 0;
		int32 Frame = 0;
		FString Dir;
		FString ClipName;
		int32 ClipCount = 0;
		FDelegateHandle Handle;
		TWeakObjectPtr<AOperativeCharacter> Hero;
		TWeakObjectPtr<AEnemyCharacter> Brute;
		TWeakObjectPtr<ACameraActor> Camera;
		float SavedChance = -1.f;
		/** Camera offsets from the operative: ahead, to his right, above his mesh centre (cm). */
		FVector CamOffset = FVector(430.f, 400.f, 120.f);
	};

	TSharedPtr<FRunner> Runner;

	IConsoleVariable* ChanceVar()
	{
		return IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Knockdown.Chance"));
	}

	void Shot(const FString& Name)
	{
		const FString Path = Runner->Dir / (Name + TEXT(".png"));
		FScreenshotRequest::RequestScreenshot(Path, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
		UE_LOG(LogCodexTactics, Display, TEXT("KnockdownShots: still %s"), *Path);
	}

	void StartClip(const FString& Name)
	{
		Runner->ClipName = Name;
		Runner->ClipCount = 0;
		IFileManager::Get().DeleteDirectory(*(Runner->Dir / TEXT("frames") / Name), false, true);
	}

	void StopClip()
	{
		UE_LOG(LogCodexTactics, Display, TEXT("KnockdownShots: clip %s, %d frames"), *Runner->ClipName, Runner->ClipCount);
		Runner->ClipName.Reset();
	}

	void Finish()
	{
		if (IConsoleVariable* Chance = ChanceVar())
		{
			Chance->Set(Runner->SavedChance, ECVF_SetByConsole);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("KnockdownShots: done"));
		FCoreDelegates::OnEndFrame.Remove(Runner->Handle);
		FPlatformMisc::RequestExit(false, TEXT("KnockdownShots"));
	}

	/** The review camera beside / ahead of the operative, looking at his mesh centre (every frame: he falls and rises). */
	void UpdateCamera(UWorld* World)
	{
		AOperativeCharacter* Hero = Runner->Hero.Get();
		APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		if (!Hero || !PC)
		{
			return;
		}
		if (!Runner->Camera.IsValid())
		{
			Runner->Camera = World->SpawnActor<ACameraActor>(Hero->GetActorLocation(), FRotator::ZeroRotator);
			if (ACameraActor* Spawned = Runner->Camera.Get())
			{
				Spawned->GetCameraComponent()->SetFieldOfView(55.f);
				Spawned->GetCameraComponent()->bConstrainAspectRatio = false;
			}
		}
		ACameraActor* Camera = Runner->Camera.Get();
		if (!Camera)
		{
			return;
		}
		// Look at the capsule (stays put while the mesh falls), a little low so the lying body stays in the frame.
		const FVector Centre = Hero->GetActorLocation() - FVector(0.f, 0.f, 15.f);
		const FVector& O = Runner->CamOffset;
		const FVector Eye = Centre + Hero->GetActorForwardVector() * O.X + Hero->GetActorRightVector() * O.Y + FVector(0.f, 0.f, O.Z);
		Camera->SetActorLocationAndRotation(Eye, (Centre - Eye).Rotation());
		if (PC->GetViewTarget() != Camera)
		{
			PC->SetViewTarget(Camera);
		}
	}

	void Tick()
	{
		FRunner& R = *Runner;
		++R.Frame;
		if (!R.ClipName.IsEmpty())
		{
			FScreenshotRequest::RequestScreenshot(R.Dir / TEXT("frames") / R.ClipName / FString::Printf(TEXT("f_%04d.png"), R.ClipCount++), true, false);
		}
		if (R.Frame > 30 * 120)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("KnockdownShots: timeout in step %d"), R.Index);
			Finish();
			return;
		}
		UWorld* World = FrontendSmokeUtils::FindGameWorld();
		if (World)
		{
			UpdateCamera(World);
		}
		if (R.Wait > 0)
		{
			--R.Wait;
			return;
		}
		if (!World)
		{
			return;
		}
		if (!R.Steps.IsValidIndex(R.Index))
		{
			Finish();
			return;
		}
		const int32 Result = R.Steps[R.Index](World);
		if (Result >= 0)
		{
			++R.Index;
			R.Wait = Result;
		}
	}

	UKnockdownComponent* KnockOf(const AOperativeCharacter* Unit)
	{
		return Unit ? Unit->KnockdownComponent.Get() : nullptr;
	}

	void AddSteps(TArray<FStep>& S)
	{
		S.Add([](UWorld* World) { return World->GetSubsystem<USquadSubsystem>() && World->GetSubsystem<USquadSubsystem>()->GetLeader() ? 60 : -1; });
		S.Add([](UWorld* World)
		{
			UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
			Flow->TriggerCombatZone();
			Flow->FinishCutscene();
			Flow->FinishPreparation();
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->Destroy();
			}
			SmokeUtils::PlaceSquadAtTestStart(World);
			USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
			AOperativeCharacter* Hero = Squad->GetLeader();
			Runner->Hero = Hero;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->HealthComponent->SetMaxHealth(5000.f, true);
				Member->bTacticalCeaseFire = true;
				if (Member != Hero)
				{
					// The others 7 m behind him, out of the frame.
					Member->TeleportTo(SmokeUtils::FreeSpot(World, Hero->GetActorLocation() - Hero->GetActorForwardVector() * 700.f
						+ Hero->GetActorRightVector() * (Member->SquadRole == EOperativeRole::Commander ? -300.f : 300.f), Member),
						Member->GetActorRotation(), false, true);
				}
			}
			// A frozen Frost Brute 5 m ahead: the attacker (and the live enemy the turn-based fight needs).
			const FVector From = Hero->GetActorLocation();
			const FVector Spot = SmokeUtils::ClearPoint(World, From, From + Hero->GetActorForwardVector() * 500.f);
			if (AEnemyCharacter* Brute = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::Brute, Spot + FVector(0.f, 0.f, 30.f),
				(-Hero->GetActorForwardVector()).Rotation()))
			{
				Brute->GetHealthComponent()->SetMaxHealth(100000.f, true);
				Brute->CustomTimeDilation = 0.f;
				Runner->Brute = Brute;
			}
			return 45;
		});
		// Real time: an 80 HP hit (>= 40 after fortitude) from the brute's side floors him on his back; he lies 0.6 s and gets up in 0.8 s.
		S.Add([](UWorld* World) { StartClip(TEXT("knockdown_faster_getup")); return 20; });
		S.Add([](UWorld* World)
		{
			AOperativeCharacter* Hero = Runner->Hero.Get();
			if (!Hero)
			{
				return 1;
			}
			Hero->ForcedDodgeRollForTesting = 0.f;
			Hero->TakeHit(80.f, TEXT("Frost Brute"), false, false, Runner->Brute.Get());
			UKnockdownComponent* Knock = KnockOf(Hero);
			UE_LOG(LogCodexTactics, Display, TEXT("KnockdownShots: real-time knockdown %s (fall %.2f, downed %.2f, get-up %.2f s)"),
				Knock && Knock->IsDown() ? TEXT("started") : TEXT("FAILED"), Knock ? Knock->GetState().FallDuration : 0.f,
				Knock ? Knock->GetState().DownedDuration : 0.f, Knock ? Knock->GetState().GetUpDuration : 0.f);
			return 2;
		});
		S.Add([](UWorld* World)
		{
			const UKnockdownComponent* Knock = KnockOf(Runner->Hero.Get());
			return Knock && Knock->IsDown() ? -1 : 25; // until he is up, then a short tail
		});
		S.Add([](UWorld* World) { StopClip(); return 20; });
		// Turn-based: he is knocked down on his turn and lies; the panel offers «GET UP (2 AP) [Z]».
		S.Add([](UWorld* World)
		{
			World->GetSubsystem<UGameFlowSubsystem>()->RequestEnterTurnBased(true);
			return 30;
		});
		S.Add([](UWorld* World)
		{
			UTurnBasedCombatSubsystem* TurnBased = World->GetSubsystem<UTurnBasedCombatSubsystem>();
			if (!TurnBased->IsActive() || TurnBased->GetPhase() != ETurnPhase::Squad || TurnBased->IsBusy())
			{
				return -1;
			}
			AOperativeCharacter* Hero = Runner->Hero.Get();
			TurnBased->SelectUnit(Hero);
			UKnockdownComponent* Knock = KnockOf(Hero);
			const bool bDown = Knock && Knock->ForceKnockDown(EKnockdownDirection::Back, EKnockdownCause::Explosion);
			UE_LOG(LogCodexTactics, Display, TEXT("KnockdownShots: turn-based knockdown %s"), bDown ? TEXT("started") : TEXT("FAILED"));
			Runner->CamOffset = FVector(380.f, 380.f, 200.f);
			return 2;
		});
		S.Add([](UWorld* World)
		{
			const UKnockdownComponent* Knock = KnockOf(Runner->Hero.Get());
			return Knock && Knock->GetPhase() == EKnockdownPhase::Downed ? 45 : -1; // he lies through his turn (1.5 s)
		});
		S.Add([](UWorld* World)
		{
			const UTurnBasedCombatSubsystem* TurnBased = World->GetSubsystem<UTurnBasedCombatSubsystem>();
			const FTurnUnitState* Unit = TurnBased->GetUnitState(Runner->Hero.Get());
			UE_LOG(LogCodexTactics, Display, TEXT("KnockdownShots: lying on his turn, AP %d, GET UP offered %s"), Unit ? Unit->AP : -1,
				TurnBased->IsGetUpAvailable() ? TEXT("yes") : TEXT("NO"));
			Shot(TEXT("knockdown_tb_getup"));
			return 5;
		});
		S.Add([](UWorld* World) { StartClip(TEXT("knockdown_tb_getup")); return 15; });
		S.Add([](UWorld* World)
		{
			UTurnBasedCombatSubsystem* TurnBased = World->GetSubsystem<UTurnBasedCombatSubsystem>();
			const bool bOk = TurnBased->GetUpActiveUnit();
			const FTurnUnitState* Unit = TurnBased->GetUnitState(Runner->Hero.Get());
			UE_LOG(LogCodexTactics, Display, TEXT("KnockdownShots: GET UP %s, AP left %d"), bOk ? TEXT("ordered") : TEXT("REFUSED"), Unit ? Unit->AP : -1);
			return 2;
		});
		S.Add([](UWorld* World)
		{
			const UKnockdownComponent* Knock = KnockOf(Runner->Hero.Get());
			return Knock && Knock->IsDown() ? -1 : 25;
		});
		S.Add([](UWorld* World) { StopClip(); return 2; });
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		Runner = MakeShared<FRunner>();
		Runner->Dir = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / TEXT("..") / TEXT("Phone"));
		FPaths::CollapseRelativeDirectories(Runner->Dir);
		IFileManager::Get().MakeDirectory(*Runner->Dir, true);
		if (IConsoleVariable* Chance = ChanceVar())
		{
			Runner->SavedChance = Chance->GetFloat();
			Chance->Set(1.f, ECVF_SetByConsole);
		}
		AddSteps(Runner->Steps);
		UE_LOG(LogCodexTactics, Display, TEXT("KnockdownShots: %d steps -> %s"), Runner->Steps.Num(), *Runner->Dir);
		Runner->Handle = FCoreDelegates::OnEndFrame.AddStatic(&Tick);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.KnockdownShots"),
		TEXT("Dev: rendered knockdown clip (real-time faster get-up) + turn-based GET UP (2 AP) still/clip into Saved/Screenshots/Phone; "
			"needs rendering, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
