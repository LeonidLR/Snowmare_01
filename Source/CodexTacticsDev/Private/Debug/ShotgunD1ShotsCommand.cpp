// Dev-only console command: rendered proof of the Engineer with the D-1 shotgun for the game designer's phone (2026-11-08).
// Needs rendering (no -nullrhi), fixed frame time so the clips play back at real speed:
//   UnrealEditor.exe CodexTactics.uproject /Game/Maps/L_MovementTest -game -RenderOffscreen -ResX=1280 -ResY=720 -nosound
//     -benchmark -fps=30 -ExecCmds="CodexTactics.ShotgunD1Shots"
// Output: Saved/Screenshots/ShotgunD1/: stills hand_side.png / hand_front.png (the D-1 in his hands, close), frames/<clip>/f_NNNN.png for
// idle, walk, aim_fire (three shots at a target 8 m out), reload (per shell, empty -> full). "CodexTactics.ShotgunD1Shots stills" = the
// stills only (the grip calibration). The camera follows the Engineer; no keys, no desktop capture.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeAnimInstance.h"
#include "Data/WeaponDataAsset.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Debug/FrontendSmokeUtils.h"
#include "Debug/SmokeUtils.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/DeployableRules.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace ShotgunD1Shots
{
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
		bool bStillsOnly = false;
		/** "reload2": only the reload (after a settle), into frames/reload2 (the per-shell loop fix, 2026-11-08). */
		bool bReloadOnly = false;
		/** "pistol" (the whole pistol, 2026-11-08): the Commander with the pistol instead -> Saved/Screenshots/PistolD/, plus the switch from the
		 * rifle (the equip clip) at the end. */
		bool bPistol = false;
		/** "sniper" (the sniper rifle on the framework, 2026-11-09): the Medic-Sapper with the sniper rifle -> Saved/Screenshots/SniperRifle/ (she
		 * kneels for the shots: the rifle fires only kneeling / prone). */
		bool bSniper = false;
		FString ReloadClipName = TEXT("reload");
		int32 ReloadStep = INDEX_NONE;
		FDelegateHandle Handle;
		TWeakObjectPtr<AOperativeCharacter> Hero;
		TWeakObjectPtr<AEnemyCharacter> Target;
		TWeakObjectPtr<ACameraActor> Camera;
		/** Camera offset from the Engineer (ahead, right, up from the capsule centre) and the look-at height. */
		FVector CamOffset = FVector(380.f, 300.f, 110.f);
		float LookHeight = 20.f;
		/** Close-up on the right hand (the calibration stills). */
		bool bHandCam = false;
		FVector HandCamOffset = FVector(0.f, 120.f, 10.f);
		int32 ShotsLeft = 0;
	};

	TSharedPtr<FRunner> Runner;

	void Still(const FString& Name)
	{
		if (const AOperativeCharacter* Hero = Runner->Hero.Get())
		{
			const UOperativeAnimInstance* Anim = Hero->GetMesh() ? Cast<UOperativeAnimInstance>(Hero->GetMesh()->GetAnimInstance()) : nullptr;
			const UAnimSequenceBase* Clip = Anim ? Anim->GetShotgunDominantClip() : nullptr;
			UE_LOG(LogCodexTactics, Display, TEXT("ShotgunD1Shots: %s - layer mask %d upper %.2f clip %s weapon %s"), *Name, Anim ? static_cast<int32>(Anim->GetShotgunLayerMask()) : -1,
				Anim ? Anim->GetShotgunUpperWeight() : 0.f, Clip ? *Clip->GetName() : TEXT("none"),
				Hero->CurrentWeapon ? *Hero->CurrentWeapon->WeaponId : TEXT("none"));
		}
		const FString Path = Runner->Dir / (Name + TEXT(".png"));
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		UE_LOG(LogCodexTactics, Display, TEXT("ShotgunD1Shots: still %s"), *Path);
	}

	void StartClip(const FString& Name)
	{
		Runner->ClipName = Name;
		Runner->ClipCount = 0;
		IFileManager::Get().DeleteDirectory(*(Runner->Dir / TEXT("frames") / Name), false, true);
	}

	void StopClip()
	{
		UE_LOG(LogCodexTactics, Display, TEXT("ShotgunD1Shots: clip %s, %d frames"), *Runner->ClipName, Runner->ClipCount);
		Runner->ClipName.Reset();
	}

	void Finish()
	{
		UE_LOG(LogCodexTactics, Display, TEXT("ShotgunD1Shots: done"));
		FCoreDelegates::OnEndFrame.Remove(Runner->Handle);
		FPlatformMisc::RequestExit(false, TEXT("ShotgunD1Shots"));
	}

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
				Spawned->GetCameraComponent()->SetFieldOfView(50.f);
				Spawned->GetCameraComponent()->bConstrainAspectRatio = false;
			}
		}
		ACameraActor* Camera = Runner->Camera.Get();
		if (!Camera)
		{
			return;
		}
		FVector Centre = Hero->GetActorLocation() + FVector(0.f, 0.f, Runner->LookHeight);
		FVector Eye;
		if (Runner->bHandCam && Hero->GetMesh())
		{
			Centre = Hero->GetMesh()->GetBoneLocation(TEXT("hand_r"));
			const FVector& O = Runner->HandCamOffset;
			Eye = Centre + Hero->GetActorForwardVector() * O.X + Hero->GetActorRightVector() * O.Y + FVector(0.f, 0.f, O.Z);
		}
		else
		{
			const FVector& O = Runner->CamOffset;
			Eye = Centre + Hero->GetActorForwardVector() * O.X + Hero->GetActorRightVector() * O.Y + FVector(0.f, 0.f, O.Z);
		}
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
			FScreenshotRequest::RequestScreenshot(R.Dir / TEXT("frames") / R.ClipName / FString::Printf(TEXT("f_%04d.png"), R.ClipCount++), false, false);
		}
		if (R.Frame > 30 * 150)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("ShotgunD1Shots: timeout in step %d"), R.Index);
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

	void Fire()
	{
		AOperativeCharacter* Hero = Runner->Hero.Get();
		AEnemyCharacter* Target = Runner->Target.Get();
		if (Hero && Target)
		{
			Hero->SetActorRotation((Target->GetActorLocation() - Hero->GetActorLocation()).GetSafeNormal2D().Rotation());
			Hero->CurrentClip = FMath::Max(Hero->CurrentClip, 1);
			Hero->bForceHitForTesting = true;
			Hero->ShootAtTarget(Target);
			Hero->bForceHitForTesting = false;
		}
	}

	void AddSteps(TArray<FStep>& S)
	{
		S.Add([](UWorld* World) { return World->GetSubsystem<USquadSubsystem>() && World->GetSubsystem<USquadSubsystem>()->GetMembers().Num() >= 3 ? 60 : -1; });
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
			AOperativeCharacter* Hero = nullptr;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				const EOperativeRole Wanted = Runner->bSniper ? EOperativeRole::MedicSapper : (Runner->bPistol ? EOperativeRole::Commander : EOperativeRole::Engineer);
				Hero = Member->SquadRole == Wanted ? Member : Hero;
			}
			if (!Hero)
			{
				return 1;
			}
			Runner->Hero = Hero;
			if (Runner->bPistol || Runner->bSniper)
			{
				Hero->SwitchToWeaponById(Runner->bSniper ? TEXT("sniper_rifle") : TEXT("pistol"));
			}
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->HealthComponent->SetMaxHealth(5000.f, true);
				Member->bTacticalCeaseFire = true;
				if (Member != Hero)
				{
					Member->TeleportTo(SmokeUtils::FreeSpot(World, Hero->GetActorLocation() - Hero->GetActorForwardVector() * 900.f
						+ Hero->GetActorRightVector() * (Member->SquadRole == EOperativeRole::Commander ? -300.f : 300.f), Member), Member->GetActorRotation(), false, true);
				}
			}
			Squad->SetLeader(Hero);
			const FVector Spot = SmokeUtils::ClearPoint(World, Hero->GetActorLocation(), Hero->GetActorLocation() + Hero->GetActorForwardVector() * 800.f);
			if (AEnemyCharacter* Hound = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound, Spot + FVector(0.f, 0.f, 60.f),
				(-Hero->GetActorForwardVector()).Rotation()))
			{
				Hound->GetHealthComponent()->SetMaxHealth(100000.f, true);
				Hound->CustomTimeDilation = 0.f;
				Hound->SetActorTickEnabled(false);
				Runner->Target = Hound;
			}
			Runner->bHandCam = true;
			return 45;
		});
		// The grip: the D-1 in his hands, from his right and from the front.
		S.Add([](UWorld*) { Runner->HandCamOffset = FVector(0.f, 230.f, 20.f); return 6; });
		S.Add([](UWorld*) { Still(TEXT("hand_side")); return 6; });
		S.Add([](UWorld*) { Runner->HandCamOffset = FVector(230.f, 40.f, 20.f); return 6; });
		S.Add([](UWorld*) { Still(TEXT("hand_front")); return 6; });
		S.Add([](UWorld*) { Runner->HandCamOffset = FVector(-80.f, 90.f, 180.f); return 6; });
		S.Add([](UWorld*) { Still(TEXT("hand_top")); return 6; });
		S.Add([](UWorld*)
		{
			Runner->bHandCam = false;
			Runner->CamOffset = FVector(380.f, 300.f, 110.f);
			if (Runner->bStillsOnly)
			{
				Runner->Index = Runner->Steps.Num() - 1; // the caller steps past the end: done
				return 10;
			}
			if (Runner->bReloadOnly && Runner->ReloadStep != INDEX_NONE)
			{
				Runner->Index = Runner->ReloadStep - 1; // the caller steps on to the reload
			}
			return 10;
		});
		S.Add([](UWorld*) { Still(TEXT("full_idle")); StartClip(TEXT("idle")); return 75; });
		S.Add([](UWorld* World)
		{
			StopClip();
			AOperativeCharacter* Hero = Runner->Hero.Get();
			if (Hero)
			{
				StartClip(TEXT("walk"));
				Hero->OrderMoveTo(SmokeUtils::ClearPoint(World, Hero->GetActorLocation(), Hero->GetActorLocation() + Hero->GetActorForwardVector() * 350.f
					+ Hero->GetActorRightVector() * 450.f), false);
			}
			return 30;
		});
		S.Add([](UWorld*)
		{
			const AOperativeCharacter* Hero = Runner->Hero.Get();
			return Hero && Hero->GetVelocity().Size2D() > 10.f ? -1 : 15;
		});
		S.Add([](UWorld*)
		{
			StopClip();
			StartClip(TEXT("aim_fire"));
			Runner->ShotsLeft = 3;
			if (AOperativeCharacter* Hero = Runner->Hero.Get(); Hero && Runner->bSniper)
			{
				Hero->SetStance(EOperativeStance::Crouching); // the sniper rifle fires kneeling / prone only
				return 60;
			}
			return 10;
		});
		S.Add([](UWorld*)
		{
			if (Runner->ShotsLeft-- <= 0)
			{
				return 30;
			}
			Fire();
			Runner->Index -= 1; // the next shot
			return 38;
		});
		Runner->ReloadStep = S.Num();
		S.Add([](UWorld* World)
		{
			StopClip();
			// A cease fire freezes the reload tick (RunCombatShooting returns first): off, with every enemy parked out of reach (a known target
			// interrupts the per-shell reload after each shell - the level's wave would make the clip show stop / restart); a moment to settle.
			if (AOperativeCharacter* Hero = Runner->Hero.Get())
			{
				Hero->bTacticalCeaseFire = false;
				for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
				{
					SmokeUtils::ParkEnemyFarAway(*It);
				}
			}
			return 30;
		});
		S.Add([](UWorld*)
		{
			if (AOperativeCharacter* Hero = Runner->Hero.Get())
			{
				StartClip(Runner->ReloadClipName);
				Hero->CurrentClip = 0;
				Hero->ReserveAmmo = Runner->bSniper ? 15 : (Runner->bPistol ? 24 : 40);
				Hero->StartReload();
			}
			return 10;
		});
		S.Add([](UWorld*)
		{
			const AOperativeCharacter* Hero = Runner->Hero.Get();
			return Hero && Hero->bIsReloading ? -1 : 45; // the outro (pump, back to the aim) after the last shell
		});
		S.Add([](UWorld*) { StopClip(); return 5; });
		// The pistol: the switch from the rifle (the M16 in hands for a moment, then the pistol: its equip clip).
		S.Add([](UWorld*)
		{
			AOperativeCharacter* Hero = Runner->Hero.Get();
			if (!Runner->bPistol || !Hero)
			{
				return 0;
			}
			Hero->SwitchToWeaponById(TEXT("m16"));
			return 45;
		});
		S.Add([](UWorld*)
		{
			AOperativeCharacter* Hero = Runner->Hero.Get();
			if (!Runner->bPistol || !Hero)
			{
				return 0;
			}
			StartClip(TEXT("switch"));
			Hero->SwitchToWeaponById(TEXT("pistol"));
			return 60;
		});
		S.Add([](UWorld*) { StopClip(); return 5; });
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		Runner = MakeShared<FRunner>();
		Runner->bPistol = Args.Contains(TEXT("pistol"));
		Runner->bSniper = Args.Contains(TEXT("sniper"));
		Runner->Dir = FPaths::ProjectSavedDir() / (Runner->bSniper ? TEXT("Screenshots/SniperRifle") : (Runner->bPistol ? TEXT("Screenshots/PistolD") : TEXT("Screenshots/ShotgunD1")));
		Runner->bStillsOnly = Args.Contains(TEXT("stills"));
		Runner->bReloadOnly = Args.Contains(TEXT("reload2"));
		Runner->ReloadClipName = Runner->bReloadOnly ? TEXT("reload2") : TEXT("reload");
		IFileManager::Get().MakeDirectory(*Runner->Dir, true);
		AddSteps(Runner->Steps);
		Runner->Handle = FCoreDelegates::OnEndFrame.AddStatic(&Tick);
		UE_LOG(LogCodexTactics, Display, TEXT("ShotgunD1Shots: start -> %s"), *Runner->Dir);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.ShotgunD1Shots"),
		TEXT("Rendered proof: the Engineer with the D-1 shotgun (grip stills, idle, walk, aim + fire, per-shell reload) -> Saved/Screenshots/ShotgunD1/. Arg 'stills' = the grip stills only, 'reload2' = the reload only into frames/reload2, 'pistol' = the Commander with the pistol -> Saved/Screenshots/PistolD/ (+ the switch from the rifle), 'sniper' = the Medic-Sapper with the sniper rifle -> Saved/Screenshots/SniperRifle/."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
