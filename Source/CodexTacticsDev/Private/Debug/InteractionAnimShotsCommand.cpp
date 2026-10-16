// Dev-only console command: rendered stills of the interaction clips (game designer 2026-10-15; Polygonmaker pack via
// AM_Interact_*). Needs rendering (no -nullrhi); run offscreen with a fixed frame time:
//   UnrealEditor.exe CodexTactics.uproject /Game/Maps/L_MovementTest -game -RenderOffscreen -ResX=1280 -ResY=720 -nosound
//     -benchmark -fps=30 -ExecCmds="CodexTactics.InteractionAnimShots"
// Output (Saved/Screenshots/): anim_chest_open.png / _2 (the standing commander opens a crate: full body, the loot window after it),
// anim_chest_search.png / _2 (Take All -> the chest search), anim_pickup_crouched_upper.png / _2 (a crouched engineer disarms a PMC
// wire: the pickup on the upper body, the legs keep the crouch), anim_pickup_standing.png (a standing one: full body, rising from
// the low pose). The others hold still in solo mode, 8 m behind; UI hidden in the stills.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/SkeletalMeshComponent.h"
#include "Debug/FrontendSmokeUtils.h"
#include "Debug/SmokeUtils.h"
#include "GameFramework/HUD.h"
#include "HAL/FileManager.h"
#include "Interactables/InteractionSubsystem.h"
#include "Interactables/LootCrateActor.h"
#include "Interactables/TripwireActor.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace InteractionAnimShots
{
	/** One step: frames to wait before the next step, or -1 to run again next frame (waiting for a condition). */
	using FStep = TFunction<int32(UWorld*)>;

	struct FRunner
	{
		TArray<FStep> Steps;
		int32 Index = 0;
		int32 Wait = 0;
		int32 Frame = 0;
		int32 StepFrame = 0;
		FString Dir;
		FDelegateHandle Handle;
		TWeakObjectPtr<ACameraActor> Camera;
		TWeakObjectPtr<AOperativeCharacter> Commander;
		TWeakObjectPtr<AOperativeCharacter> Engineer;
		TWeakObjectPtr<ALootCrateActor> Crate;
		TWeakObjectPtr<ATripwireActor> Wire;
		FVector Start = FVector::ZeroVector;
		FVector Forward = FVector::ForwardVector;
	};

	TSharedPtr<FRunner> Runner;

	void Finish()
	{
		UE_LOG(LogCodexTactics, Display, TEXT("InteractionAnimShots: done"));
		FCoreDelegates::OnEndFrame.Remove(Runner->Handle);
		FPlatformMisc::RequestExit(false, TEXT("InteractionAnimShots"));
	}

	void Shot(const TCHAR* Name, const AOperativeCharacter* Subject)
	{
		const FString Path = Runner->Dir / FString::Printf(TEXT("%s.png"), Name);
		FScreenshotRequest::RequestScreenshot(Path, /*bShowUI*/ false, /*bAddFilenameSuffix*/ false);
		const UOperativeAnimInstance* Anim = UOperativeAnimInstance::Of(Subject);
		UE_LOG(LogCodexTactics, Display, TEXT("InteractionAnimShots: %s (%s stance %d, playing %d on layer %d, pelvis %.0f cm over the feet)"), *Path,
			Subject ? *Subject->DisplayName.ToString() : TEXT("-"), Subject ? static_cast<int32>(Subject->GetStance()) : -1,
			Anim ? static_cast<int32>(Anim->InteractionAnim) : -1, Anim ? static_cast<int32>(Anim->InteractionLayer) : -1,
			Subject ? Subject->GetMesh()->GetBoneLocation(TEXT("pelvis")).Z - (Subject->GetActorLocation().Z - Subject->GetSimpleCollisionHalfHeight()) : 0.f);
	}

	/** A 3/4 view of Subject: YawOffset from his front, Distance away, Height over his root, looking at his hips. */
	void Aim(UWorld* World, const AActor* Subject, float YawOffset, float Distance, float Height)
	{
		if (!Subject)
		{
			return;
		}
		const FVector Target = Subject->GetActorLocation() + FVector(0.f, 0.f, -25.f);
		const FVector Direction = FRotator(0.f, Subject->GetActorRotation().Yaw + YawOffset, 0.f).Vector();
		const FVector Eye = Target + Direction * Distance + FVector(0.f, 0.f, Height);
		if (!Runner->Camera.IsValid())
		{
			Runner->Camera = World->SpawnActor<ACameraActor>(Eye, (Target - Eye).Rotation());
			if (ACameraActor* Camera = Runner->Camera.Get())
			{
				Camera->GetCameraComponent()->SetFieldOfView(55.f);
				Camera->GetCameraComponent()->bConstrainAspectRatio = false;
			}
		}
		if (ACameraActor* Camera = Runner->Camera.Get())
		{
			Camera->SetActorLocationAndRotation(Eye, (Target - Eye).Rotation());
			if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
			{
				PC->SetViewTarget(Camera);
				if (AHUD* Hud = PC->GetHUD())
				{
					Hud->bShowHUD = false; // clean stills: no feed / squad panel
				}
			}
		}
	}

	void Tick()
	{
		FRunner& R = *Runner;
		++R.Frame;
		UWorld* World = FrontendSmokeUtils::FindGameWorld();
		if (R.Frame > 30 * 120)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("InteractionAnimShots: timeout in step %d"), R.Index);
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
			R.StepFrame = R.Frame;
		}
	}

	AOperativeCharacter* FindRole(USquadSubsystem* Squad, EOperativeRole Role)
	{
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			if (Member->SquadRole == Role)
			{
				return Member;
			}
		}
		return nullptr;
	}

	/** A revealed PMC wire 70 cm in front of Operative (across his view), a sure disarm. */
	ATripwireActor* WireInFront(UWorld* World, AOperativeCharacter* Operative)
	{
		const FVector Forward = Operative->GetActorForwardVector().GetSafeNormal2D();
		const FVector Centre = Operative->GetActorLocation() - FVector(0.f, 0.f, Operative->GetSimpleCollisionHalfHeight()) + Forward * 70.f;
		const FTransform Transform(FVector::CrossProduct(FVector::UpVector, Forward).Rotation(), Centre + FVector(0.f, 0.f, 5.f));
		APMCTripwireActor* Wire = World->SpawnActorDeferred<APMCTripwireActor>(APMCTripwireActor::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Wire)
		{
			Wire->AnchorA = FVector(-120.f, 0.f, 0.f);
			Wire->AnchorB = FVector(120.f, 0.f, 0.f);
			Wire->FinishSpawning(Transform);
			Wire->Reveal();
			Wire->DefusalRollForTesting = 0.f;
			Wire->DefusalExplosionRollForTesting = 99.f;
		}
		return Wire;
	}

	void AddPickup(TArray<FStep>& S, EOperativeStance Stance, int32 FirstDelayFrames, const TCHAR* First, const TCHAR* Second)
	{
		S.Add([Stance](UWorld* World)
		{
			AOperativeCharacter* Engineer = Runner->Engineer.Get();
			Engineer->StopOperative();
			Engineer->TeleportTo(SmokeUtils::FreeSpot(World, Runner->Start + Runner->Forward * 500.f, Engineer), Runner->Forward.Rotation(), false, true);
			Engineer->SetStance(Stance);
			return 45;
		});
		S.Add([](UWorld* World)
		{
			AOperativeCharacter* Engineer = Runner->Engineer.Get();
			Runner->Wire = WireInFront(World, Engineer);
			Aim(World, Engineer, 75.f, 420.f, 90.f);
			if (ATripwireActor* Wire = Runner->Wire.Get())
			{
				Wire->ExecuteAction(Engineer);
			}
			return 0;
		});
		S.Add([FirstDelayFrames](UWorld* World) { return Runner->Wire.IsValid() ? -1 : FirstDelayFrames; }); // disarmed: the pickup starts now
		S.Add([First](UWorld* World) { Shot(First, Runner->Engineer.Get()); return 9; });
		S.Add([Second](UWorld* World)
		{
			if (Second)
			{
				Shot(Second, Runner->Engineer.Get());
			}
			return 75;
		});
	}

	void AddSteps(TArray<FStep>& S)
	{
		S.Add([](UWorld* World)
		{
			USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
			if (!Squad || Squad->GetMembers().Num() < 3)
			{
				return -1;
			}
			SmokeUtils::PlaceSquadAtTestStart(World);
			Runner->Commander = FindRole(Squad, EOperativeRole::Commander);
			Runner->Engineer = FindRole(Squad, EOperativeRole::Engineer);
			if (!Squad->IsSoloMode())
			{
				Squad->ToggleSoloMode();
			}
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->bTacticalCeaseFire = true;
				Member->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			}
			return 60;
		});
		S.Add([](UWorld* World)
		{
			AOperativeCharacter* Commander = Runner->Commander.Get();
			USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
			Squad->SetLeader(Commander);
			Runner->Start = Commander->GetActorLocation();
			Runner->Forward = Commander->GetActorForwardVector().GetSafeNormal2D();
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member != Commander)
				{
					Member->TeleportTo(SmokeUtils::FreeSpot(World, Runner->Start - Runner->Forward * 800.f, Member), Member->GetActorRotation(), false, true);
				}
			}
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			const FVector Feet = Commander->GetActorLocation() - FVector(0.f, 0.f, Commander->GetSimpleCollisionHalfHeight());
			Runner->Crate = World->SpawnActor<ALootCrateActor>(Feet + Runner->Forward * 105.f + FVector(0.f, 0.f, 40.f), Runner->Forward.Rotation(), Params);
			if (ALootCrateActor* Crate = Runner->Crate.Get())
			{
				Crate->Contents.Medkits = 2;
			}
			Aim(World, Commander, 70.f, 360.f, 80.f);
			return 30;
		});
		S.Add([](UWorld* World)
		{
			if (ALootCrateActor* Crate = Runner->Crate.Get())
			{
				Crate->StartOpening(Runner->Commander.Get());
			}
			return 27; // 0.9 s: bent over the lid
		});
		S.Add([](UWorld* World) { Shot(TEXT("anim_chest_open"), Runner->Commander.Get()); return 18; });
		S.Add([](UWorld* World) { Shot(TEXT("anim_chest_open_2"), Runner->Commander.Get()); return 0; });
		S.Add([](UWorld* World)
		{
			UInteractionSubsystem* Interactions = World->GetSubsystem<UInteractionSubsystem>();
			if (!Interactions || Interactions->GetLootCrate() != Runner->Crate.Get())
			{
				return -1; // the window opens after the clip
			}
			UE_LOG(LogCodexTactics, Display, TEXT("InteractionAnimShots: loot window open %.2f s after StartOpening"), Runner->Crate->GetLastOpenDelaySeconds());
			Interactions->LootAll();
			return 30; // 1.0 s into the search
		});
		S.Add([](UWorld* World) { Shot(TEXT("anim_chest_search"), Runner->Commander.Get()); return 15; });
		S.Add([](UWorld* World) { Shot(TEXT("anim_chest_search_2"), Runner->Commander.Get()); return 60; });
		// Upper-body pickup: 1.3 s into the clip the hands are near the ground (the lowest pose at 1.4 s), legs crouched.
		AddPickup(S, EOperativeStance::Crouching, 36, TEXT("anim_pickup_crouched_upper"), TEXT("anim_pickup_crouched_upper_2"));
		// Standing: the full body rises from the low pose (starts at 1.0 s of the clip).
		AddPickup(S, EOperativeStance::Standing, 9, TEXT("anim_pickup_standing"), nullptr);
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		Runner = MakeShared<FRunner>();
		Runner->Dir = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / TEXT(".."));
		FPaths::CollapseRelativeDirectories(Runner->Dir);
		IFileManager::Get().MakeDirectory(*Runner->Dir, true);
		AddSteps(Runner->Steps);
		UE_LOG(LogCodexTactics, Display, TEXT("InteractionAnimShots: %d steps -> %s"), Runner->Steps.Num(), *Runner->Dir);
		Runner->Handle = FCoreDelegates::OnEndFrame.AddStatic(&Tick);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.InteractionAnimShots"),
		TEXT("Dev: rendered stills of the interaction clips (chest open / search, crouched + standing pickup after a disarm) into "
			"Saved/Screenshots; needs rendering, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
