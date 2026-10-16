// Dev-only console command: rendered proof of the prone dive on the move (rev 5f, game designer 2026-10-14: "he stops dead, dives
// in place, then the mesh snaps back"). Needs rendering (no -nullrhi); run offscreen with a fixed frame time:
//   UnrealEditor.exe CodexTactics.uproject /Game/Maps/L_MovementTest -game -RenderOffscreen -ResX=1280 -ResY=720 -nosound
//     -benchmark -fps=30 -ExecCmds="CodexTactics.ProneDiveShots"
// Output: Saved/Screenshots/prone_dive_<body>_NN.png - a fixed SIDE camera (beside the run, 7 m off, the whole slide in the frame),
// one still every 0.1 s from 0.2 s before the prone order to 2 s after it, for the regular operative (BP_Operative) and the
// Female Soldier body (BP_Operative_FemaleSoldier's mesh on the same operative). The log lists the capsule and the pelvis along the
// run per still ("ProneDiveShots: <body> t=... capsule ... pelvis ..."). The others hold still in solo mode, 8 m behind.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/SkeletalMeshComponent.h"
#include "Debug/FrontendSmokeUtils.h"
#include "Debug/SmokeUtils.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace ProneDiveShots
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
		FDelegateHandle Handle;
		TWeakObjectPtr<AOperativeCharacter> Hero;
		TWeakObjectPtr<ACameraActor> Camera;
		FString Body;
		FVector RunStart = FVector::ZeroVector;
		FVector RunDirection = FVector::ForwardVector;
		/** Frame of the prone order; stills every 3 frames (0.1 s at 30 fps) from 6 frames before it. */
		int32 DiveFrame = -1;
		int32 StillIndex = 0;
		TWeakObjectPtr<USkeletalMesh> MaleMesh;
		FTransform MaleMeshTransform;
	};

	TSharedPtr<FRunner> Runner;

	void Finish()
	{
		UE_LOG(LogCodexTactics, Display, TEXT("ProneDiveShots: done"));
		FCoreDelegates::OnEndFrame.Remove(Runner->Handle);
		FPlatformMisc::RequestExit(false, TEXT("ProneDiveShots"));
	}

	float Along(const FVector& Point)
	{
		return static_cast<float>(FVector::DotProduct(FVector(Point.X - Runner->RunStart.X, Point.Y - Runner->RunStart.Y, 0.f), Runner->RunDirection));
	}

	void Tick()
	{
		FRunner& R = *Runner;
		++R.Frame;
		UWorld* World = FrontendSmokeUtils::FindGameWorld();
		AOperativeCharacter* Hero = R.Hero.Get();
		if (World && Hero && R.DiveFrame >= 0)
		{
			const int32 Since = R.Frame - R.DiveFrame;
			if (Since >= -6 && Since <= 60 && (Since + 6) % 3 == 0)
			{
				const FString Path = R.Dir / FString::Printf(TEXT("prone_dive_%s_%02d.png"), *R.Body, R.StillIndex++);
				FScreenshotRequest::RequestScreenshot(Path, /*bShowUI*/ false, /*bAddFilenameSuffix*/ false);
				UE_LOG(LogCodexTactics, Display, TEXT("ProneDiveShots: %s t=%+.1fs capsule %.0f cm pelvis %.0f cm speed %.0f cm/s dive %d -> %s"), *R.Body,
					Since / 30.f, Along(Hero->GetActorLocation()), Along(Hero->GetMesh()->GetBoneLocation(TEXT("pelvis"))), Hero->GetVelocity().Size2D(),
					Hero->IsProneDiving() ? 1 : 0, *FPaths::GetCleanFilename(Path));
			}
		}
		if (R.Frame > 30 * 90)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("ProneDiveShots: timeout in step %d"), R.Index);
			Finish();
			return;
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

	/** The run: the hero back at the start facing the run, the others in solo mode behind him, the side camera fixed on the slide. */
	int32 SetUpRun(UWorld* World)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Hero = Runner->Hero.Get();
		if (!Squad || !Hero)
		{
			return -1;
		}
		Hero->StopOperative();
		Hero->SetStance(EOperativeStance::Standing);
		SmokeUtils::PlaceSquadAtTestStart(World);
		if (!Squad->IsSoloMode())
		{
			Squad->ToggleSoloMode();
		}
		Runner->RunStart = Hero->GetActorLocation();
		Runner->RunDirection = Hero->GetActorForwardVector().GetSafeNormal2D();
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->bTacticalCeaseFire = true;
			if (Member != Hero)
			{
				Member->TeleportTo(SmokeUtils::FreeSpot(World, Runner->RunStart - Runner->RunDirection * 800.f, Member), Member->GetActorRotation(), false, true);
			}
		}
		// The side view: beside the slide (sprint ~4 m, the dive ~5 m from there), 7 m off the run, at hip height.
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Runner->RunDirection);
		const FVector Middle = Runner->RunStart + Runner->RunDirection * 750.f + FVector(0.f, 0.f, -20.f);
		const FVector Eye = Middle + Side * 900.f + FVector(0.f, 0.f, 60.f);
		if (!Runner->Camera.IsValid())
		{
			Runner->Camera = World->SpawnActor<ACameraActor>(Eye, (Middle - Eye).Rotation());
			if (ACameraActor* Camera = Runner->Camera.Get())
			{
				Camera->GetCameraComponent()->SetFieldOfView(70.f);
				Camera->GetCameraComponent()->bConstrainAspectRatio = false;
			}
		}
		if (ACameraActor* Camera = Runner->Camera.Get())
		{
			Camera->SetActorLocationAndRotation(Eye, (Middle - Eye).Rotation());
			if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
			{
				PC->SetViewTarget(Camera);
			}
		}
		Runner->DiveFrame = -1;
		Runner->StillIndex = 0;
		return 30;
	}

	void AddRun(TArray<FStep>& S, const TCHAR* Body)
	{
		S.Add([Body](UWorld* World) { Runner->Body = Body; return SetUpRun(World); });
		S.Add([](UWorld* World)
		{
			AOperativeCharacter* Hero = Runner->Hero.Get();
			Hero->OrderMoveTo(SmokeUtils::ClearPoint(World, Runner->RunStart, Runner->RunStart + Runner->RunDirection * 2000.f), /*bSprint*/ true);
			return 0;
		});
		// At full sprint ~4 m into the run: the prone order (stills start 0.2 s before it).
		S.Add([](UWorld* World)
		{
			AOperativeCharacter* Hero = Runner->Hero.Get();
			const bool bFast = Hero->GetVelocity().Size2D() >= Hero->GetCharacterMovement()->MaxWalkSpeed * 0.9f;
			if (Runner->DiveFrame < 0 && bFast && Along(Hero->GetActorLocation()) >= 230.f)
			{
				Runner->DiveFrame = Runner->Frame + 6;
			}
			return Runner->DiveFrame >= 0 ? 6 : -1;
		});
		S.Add([](UWorld* World)
		{
			AOperativeCharacter* Hero = Runner->Hero.Get();
			const EProneEntry Entry = Hero->DropProne();
			UE_LOG(LogCodexTactics, Display, TEXT("ProneDiveShots: %s prone order at %.0f cm/s -> entry %d, run dive %d"), *Runner->Body,
				Hero->GetVelocity().Size2D(), static_cast<int32>(Entry), Hero->GetProneDive() == EProneDive::FromRun ? 1 : 0);
			// No crawl on afterwards: the stills end with him lying where the dive ended.
			Hero->StopOperative();
			return 62;
		});
	}

	void AddSteps(TArray<FStep>& S)
	{
		S.Add([](UWorld* World)
		{
			USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
			if (!Squad || !Squad->GetLeader())
			{
				return -1;
			}
			Runner->Hero = Squad->GetLeader();
			Runner->Hero->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			return 60;
		});
		AddRun(S, TEXT("operative"));
		S.Add([](UWorld* World)
		{
			// The Female Soldier body exactly as her Blueprint sets it (mesh + mesh transform) on the same operative.
			AOperativeCharacter* Hero = Runner->Hero.Get();
			UClass* FemaleClass = LoadClass<AOperativeCharacter>(nullptr, TEXT("/Game/Characters/Operatives/BP_Operative_FemaleSoldier.BP_Operative_FemaleSoldier_C"));
			const AOperativeCharacter* Defaults = FemaleClass ? FemaleClass->GetDefaultObject<AOperativeCharacter>() : nullptr;
			USkeletalMesh* Mesh = Defaults && Defaults->GetMesh() ? Defaults->GetMesh()->GetSkeletalMeshAsset() : nullptr;
			Hero->StopOperative();
			Hero->SetStance(EOperativeStance::Standing);
			if (Mesh)
			{
				Hero->GetMesh()->SetSkeletalMeshAsset(Mesh);
				Hero->GetMesh()->SetRelativeTransform(Defaults->GetMesh()->GetRelativeTransform());
			}
			UE_LOG(LogCodexTactics, Display, TEXT("ProneDiveShots: female body %s"), *GetNameSafe(Mesh));
			return 60;
		});
		AddRun(S, TEXT("female"));
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		Runner = MakeShared<FRunner>();
		Runner->Dir = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / TEXT(".."));
		FPaths::CollapseRelativeDirectories(Runner->Dir);
		IFileManager::Get().MakeDirectory(*Runner->Dir, true);
		AddSteps(Runner->Steps);
		UE_LOG(LogCodexTactics, Display, TEXT("ProneDiveShots: %d steps -> %s"), Runner->Steps.Num(), *Runner->Dir);
		Runner->Handle = FCoreDelegates::OnEndFrame.AddStatic(&Tick);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.ProneDiveShots"),
		TEXT("Dev: rendered side-view stills of a sprint -> prone dive (regular operative + Female Soldier body) into Saved/Screenshots; "
			"needs rendering, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
