// Dev-only console command: rendered proof of the RPG-7 on the game designer's launcher pack for his phone (2026-11-09, feat/rpg7-launcher-pack).
// Needs rendering (no -nullrhi), a fixed frame time so the clips play at real speed:
//   UnrealEditor.exe CodexTactics.uproject /Game/Maps/L_MovementTest -game -RenderOffscreen -ResX=1280 -ResY=720 -nosound
//     -benchmark -fps=30 -ExecCmds="CodexTactics.RpgShots"
// Output: Saved/Screenshots/RPG7/NN_<what>.png - the Commander with the RPG-7: the model in his hands (side / front / top), idle, walk / run /
// sprint / crouch walk from the side (the pack's legs), aim up / down, the shot (the rocket leaving the tube, three frames), the reload (the
// rocket off the tube, pushed back in) standing and crouched. The camera follows him; no keys, no desktop capture.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/WeaponDataAsset.h"
#include "Debug/FrontendSmokeUtils.h"
#include "Debug/SmokeUtils.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "GameFramework/HUD.h"
#include "UnrealClient.h"

namespace RpgShots
{
	using FStep = TFunction<int32(UWorld*)>;

	struct FRunner
	{
		TArray<FStep> Steps;
		int32 Index = 0;
		int32 Wait = 0;
		int32 Frame = 0;
		int32 StillCount = 0;
		FString Dir;
		FDelegateHandle Handle;
		TWeakObjectPtr<AOperativeCharacter> Hero;
		TWeakObjectPtr<AEnemyCharacter> Target;
		TWeakObjectPtr<ACameraActor> Camera;
		/** Camera offset from the hero (ahead, right, up from the capsule centre), the look-at height; the hand close-up. */
		FVector CamOffset = FVector(380.f, 300.f, 110.f);
		float LookHeight = 20.f;
		bool bHandCam = false;
		FVector HandCamOffset = FVector(0.f, 120.f, 10.f);
		float Fov = 50.f;
		/** Look at a point ahead of him instead (the rocket's flight). */
		float LookAhead = 0.f;
		/** Frame on the line to the target (aim / shot: he faces it with the aim offset, the capsule may point elsewhere). */
		bool bFrameOnTarget = false;
	};

	TSharedPtr<FRunner> Runner;
	/** The empty strip of the map the shots are taken on (SmokeUtils::FindClearArena), so nothing hides him and no cover pulls him in. */
	FVector ArenaOrigin = FVector::ZeroVector;
	FVector ArenaForward = FVector::ForwardVector;

	void Still(const FString& Name)
	{
		if (const AOperativeCharacter* Hero = Runner->Hero.Get())
		{
			const UOperativeAnimInstance* Anim = Hero->GetMesh() ? Cast<UOperativeAnimInstance>(Hero->GetMesh()->GetAnimInstance()) : nullptr;
			const UAnimSequenceBase* Clip = Anim ? Anim->GetShotgunDominantClip() : nullptr;
			UE_LOG(LogCodexTactics, Display, TEXT("RpgShots: %s - mask %d upper %.2f lower %.2f clip %s action %d %s rocket %d speed %.0f"), *Name,
				Anim ? static_cast<int32>(Anim->GetShotgunLayerMask()) : -1, Anim ? Anim->GetShotgunUpperWeight() : 0.f, Anim ? Anim->GetShotgunLowerWeight() : 0.f,
				Clip ? *Clip->GetName() : TEXT("none"), Anim ? static_cast<int32>(Anim->GetLauncherAction()) : -1,
				Anim && Anim->GetLauncherActionClip() ? *Anim->GetLauncherActionClip()->GetName() : TEXT("-"), Hero->IsLauncherRoundVisible() ? 1 : 0,
				Hero->GetVelocity().Size2D());
			// Where the tube points (the pack's muzzle axis = the model's +Y) against his facing and the target line.
			if (Hero->WeaponMesh)
			{
				const FVector Tube = Hero->WeaponMesh->GetComponentTransform().TransformVectorNoScale(FVector(0.f, 1.f, 0.f));
				const AEnemyCharacter* Aimed = Runner->Target.Get();
				const FVector ToTarget = Aimed ? (Aimed->GetActorLocation() - Hero->GetActorLocation()) : Hero->GetActorForwardVector();
				UE_LOG(LogCodexTactics, Display, TEXT("RpgShots: %s - tube yaw %.1f deg off his facing, %.1f off the target line, pitch %.1f"), *Name,
					FMath::FindDeltaAngleDegrees(Hero->GetActorRotation().Yaw, Tube.Rotation().Yaw),
					FMath::FindDeltaAngleDegrees(ToTarget.Rotation().Yaw, Tube.Rotation().Yaw), Tube.Rotation().Pitch);
			}
		}
		const FString Path = Runner->Dir / FString::Printf(TEXT("%02d_%s.png"), ++Runner->StillCount, *Name);
		FScreenshotRequest::RequestScreenshot(Path, false, false);
	}

	void Finish()
	{
		UE_LOG(LogCodexTactics, Display, TEXT("RpgShots: done (%d stills)"), Runner->StillCount);
		FCoreDelegates::OnEndFrame.Remove(Runner->Handle);
		FPlatformMisc::RequestExit(false, TEXT("RpgShots"));
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
				Spawned->GetCameraComponent()->bConstrainAspectRatio = false;
			}
		}
		ACameraActor* Camera = Runner->Camera.Get();
		if (!Camera)
		{
			return;
		}
		Camera->GetCameraComponent()->SetFieldOfView(Runner->Fov);
		// While he moves, the frame follows the TRAVEL (a true side profile of the legs: the actor may face a little off the move); standing,
		// the actor's facing.
		const FVector Velocity = Hero->GetVelocity();
		FVector Forward = Velocity.Size2D() > 50.f ? FVector(Velocity.X, Velocity.Y, 0.f).GetSafeNormal() : Hero->GetActorForwardVector();
		if (const AEnemyCharacter* Aimed = Runner->bFrameOnTarget ? Runner->Target.Get() : nullptr)
		{
			const FVector ToTarget = Aimed->GetActorLocation() - Hero->GetActorLocation();
			Forward = FVector(ToTarget.X, ToTarget.Y, 0.f).GetSafeNormal(KINDA_SMALL_NUMBER, Forward);
		}
		const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
		FVector Centre = Hero->GetActorLocation() + FVector(0.f, 0.f, Runner->LookHeight) + Forward * Runner->LookAhead;
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
			Eye = Centre + Forward * O.X + Right * O.Y + FVector(0.f, 0.f, O.Z);
		}
		// No HUD in the pictures.
		UWidgetLayoutLibrary::RemoveAllWidgets(World);
		if (AHUD* Hud = PC->GetHUD())
		{
			Hud->bShowHUD = false;
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
		if (R.Frame > 30 * 180)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("RpgShots: timeout in step %d"), R.Index);
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

	AOperativeCharacter* CurrentHero() { return Runner->Hero.Get(); }

	/** The target hound Ahead cm in front of him, Up cm above his feet. */
	void PlaceTarget(float Ahead, float Up)
	{
		AOperativeCharacter* H = CurrentHero();
		AEnemyCharacter* T = Runner->Target.Get();
		if (H && T)
		{
			T->SetActorLocationAndRotation(H->GetActorLocation() + H->GetActorForwardVector() * Ahead + FVector(0.f, 0.f, Up), (-H->GetActorForwardVector()).Rotation(),
				false, nullptr, ETeleportType::TeleportPhysics);
		}
	}

	void Fire(int32 Reserve)
	{
		AOperativeCharacter* H = CurrentHero();
		AEnemyCharacter* T = Runner->Target.Get();
		if (H && T)
		{
			H->CurrentClip = 1;
			H->ReserveAmmo = Reserve;
			H->bIsReloading = false;
			H->bForceHitForTesting = true;
			H->ShootAtTarget(T);
			H->bForceHitForTesting = false;
		}
	}

	/** Walks him Distance cm along his right (sideways to the camera's view axis is not what we want: he walks across a side camera). */
	void MoveAcross(UWorld* World, float Distance, bool bRun)
	{
		if (AOperativeCharacter* H = CurrentHero())
		{
			H->OrderMoveTo(SmokeUtils::ClearPoint(World, H->GetActorLocation(), H->GetActorLocation() + H->GetActorForwardVector() * Distance), bRun);
		}
	}

	/** Back to the arena's start facing along it, in Stance. */
	void BackToStart(EOperativeStance Stance)
	{
		if (AOperativeCharacter* H = CurrentHero())
		{
			H->StopOperative();
			H->TeleportTo(ArenaOrigin, ArenaForward.Rotation(), false, true);
			H->SetStance(Stance);
		}
	}

	void AddMoveStills(TArray<FStep>& S, const FString& Name, float Distance, bool bRun, bool bCrouch = false)
	{
		S.Add([bCrouch](UWorld*) { BackToStart(bCrouch ? EOperativeStance::Crouching : EOperativeStance::Standing); return 50; });
		S.Add([Distance, bRun](UWorld* World)
		{
			MoveAcross(World, Distance, bRun);
			Runner->CamOffset = FVector(0.f, 520.f, 30.f);
			Runner->LookHeight = -5.f;
			Runner->Fov = 40.f;
			return bRun ? 30 : 22;
		});
		// Both sides: the pack's Set_02 stance turns the torso ~90 deg off the tube, so his right side shows the back, his left side the chest.
		for (int32 Index = 0; Index < 2; ++Index)
		{
			S.Add([Name, Index](UWorld*) { Still(FString::Printf(TEXT("%s_right_%d"), *Name, Index + 1)); return 5; });
		}
		S.Add([](UWorld*) { Runner->CamOffset.Y = -520.f; return 2; });
		for (int32 Index = 0; Index < 2; ++Index)
		{
			S.Add([Name, Index](UWorld*) { Still(FString::Printf(TEXT("%s_left_%d"), *Name, Index + 1)); return 5; });
		}
		S.Add([](UWorld*) { const AOperativeCharacter* H = CurrentHero(); return H && H->GetVelocity().Size2D() > 10.f ? -1 : 20; });
	}

	void AddSteps(TArray<FStep>& S)
	{
		S.Add([](UWorld* World) { return World->GetSubsystem<USquadSubsystem>() && World->GetSubsystem<USquadSubsystem>()->GetMembers().Num() >= 3 ? 60 : -1; });
		S.Add([](UWorld* World)
		{
			// Exploration (in combat the squad takes cover behind the level's barricades), no enemies but the frozen target hound.
			World->GetSubsystem<UGameFlowSubsystem>()->FinishCutscene();
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->Destroy();
			}
			SmokeUtils::PlaceSquadAtTestStart(World);
			USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
			AOperativeCharacter* Commander = nullptr;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Commander = Member->SquadRole == EOperativeRole::Commander ? Member : Commander;
			}
			if (!Commander)
			{
				return 1;
			}
			Runner->Hero = Commander;
			Commander->SwitchToWeaponById(TEXT("rpg7"));
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->HealthComponent->SetMaxHealth(5000.f, true);
				Member->bTacticalCeaseFire = true;
				if (Member != Commander)
				{
					Member->TeleportTo(SmokeUtils::FreeSpot(World, Commander->GetActorLocation() - Commander->GetActorForwardVector() * 1200.f
						+ Commander->GetActorRightVector() * (Member->SquadRole == EOperativeRole::Engineer ? -400.f : 400.f), Member), Member->GetActorRotation(), false, true);
				}
			}
			Squad->SetLeader(Commander);
			TArray<const AActor*> Ignore;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Ignore.Add(Member);
			}
			const SmokeUtils::FClearArena Arena = SmokeUtils::FindClearArena(World, Commander->GetActorLocation(), Commander->GetActorForwardVector(),
				FBox2D(FVector2D(-300.f, -500.f), FVector2D(2800.f, 500.f)), Ignore);
			ArenaOrigin = Arena.bFound ? Arena.Origin + FVector(0.f, 0.f, 100.f) : Commander->GetActorLocation();
			ArenaForward = Arena.bFound ? Arena.Forward : Commander->GetActorForwardVector();
			UE_LOG(LogCodexTactics, Display, TEXT("RpgShots: arena %s at %s"), Arena.bFound ? TEXT("found") : TEXT("NOT found"), *ArenaOrigin.ToCompactString());
			Commander->TeleportTo(ArenaOrigin, ArenaForward.Rotation(), false, true);
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member != Commander)
				{
					Member->TeleportTo(SmokeUtils::FreeSpot(World, ArenaOrigin - ArenaForward * 1500.f, Member), Member->GetActorRotation(), false, true);
				}
			}
			const FVector Spot = SmokeUtils::ClearPoint(World, Commander->GetActorLocation(), Commander->GetActorLocation() + Commander->GetActorForwardVector() * 2500.f);
			if (AEnemyCharacter* Hound = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound, Spot + FVector(0.f, 0.f, 60.f),
				(-Commander->GetActorForwardVector()).Rotation()))
			{
				Hound->GetHealthComponent()->SetMaxHealth(1000000.f, true);
				Hound->CustomTimeDilation = 0.f;
				Hound->SetActorTickEnabled(false);
				Runner->Target = Hound;
			}
			if (IConsoleVariable* Chance = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Knockdown.Chance")))
			{
				Chance->Set(0.f, ECVF_SetByConsole);
			}
			Runner->bHandCam = true;
			return 45;
		});
		// The model in his hands.
		S.Add([](UWorld*) { Runner->HandCamOffset = FVector(10.f, 200.f, 20.f); return 6; });
		S.Add([](UWorld*) { Still(TEXT("hands_side")); return 4; });
		S.Add([](UWorld*) { Runner->HandCamOffset = FVector(220.f, 60.f, 30.f); return 6; });
		S.Add([](UWorld*) { Still(TEXT("hands_front")); return 4; });
		S.Add([](UWorld*) { Runner->HandCamOffset = FVector(-60.f, 60.f, 200.f); return 6; });
		S.Add([](UWorld*) { Still(TEXT("hands_top")); return 4; });
		S.Add([](UWorld*) { Runner->bHandCam = false; Runner->CamOffset = FVector(380.f, 300.f, 110.f); Runner->LookHeight = 20.f; return 20; });
		S.Add([](UWorld*) { Still(TEXT("idle")); return 30; });
		S.Add([](UWorld*) { Still(TEXT("idle_b")); return 4; });
		// The pack's legs, from the side.
		AddMoveStills(S, TEXT("walk"), 800.f, false);
		AddMoveStills(S, TEXT("sprint"), 2500.f, true);
		S.Add([](UWorld*) { BackToStart(EOperativeStance::Crouching); Runner->CamOffset = FVector(0.f, 520.f, 30.f); Runner->LookHeight = -5.f; Runner->Fov = 40.f; return 50; });
		S.Add([](UWorld*) { Still(TEXT("crouch_idle")); return 4; });
		AddMoveStills(S, TEXT("crouch_walk"), 600.f, false, true);
		S.Add([](UWorld*) { BackToStart(EOperativeStance::Standing); Runner->Fov = 50.f; return 50; });
		// Aim up / down (no reserve: no reload, the aim offset stays on for the second after the shot).
		S.Add([](UWorld*) { Runner->CamOffset = FVector(0.f, -420.f, 20.f); Runner->LookHeight = 60.f; Runner->LookAhead = 0.f; Runner->Fov = 45.f; Runner->bFrameOnTarget = true; PlaceTarget(1500.f, 900.f); return 5; });
		S.Add([](UWorld*) { Fire(0); return 22; });
		S.Add([](UWorld*) { Still(TEXT("aim_up")); return 10; });
		S.Add([](UWorld*) { PlaceTarget(2000.f, -400.f); return 5; });
		S.Add([](UWorld*) { Fire(0); return 22; });
		S.Add([](UWorld*) { Still(TEXT("aim_down")); return 30; });
		// The shot: the rocket leaving the tube (frames 1, 2, 4 after the trigger), side profile from his left, the chest side (him in the middle of the frame, the camera square to the shot line).
		S.Add([](UWorld*) { PlaceTarget(2500.f, 60.f); Runner->CamOffset = FVector(0.f, -480.f, 0.f); Runner->LookHeight = 60.f; Runner->LookAhead = 0.f; Runner->Fov = 55.f; return 20; });
		S.Add([](UWorld*) { Still(TEXT("shot_0_before")); return 3; });
		S.Add([](UWorld*) { Fire(3); return 0; });
		S.Add([](UWorld*) { Still(TEXT("shot_1")); return 0; });
		S.Add([](UWorld*) { Still(TEXT("shot_2")); return 1; });
		S.Add([](UWorld*) { Still(TEXT("shot_3")); return 3; });
		S.Add([](UWorld*) { Still(TEXT("shot_4_followthrough")); return 2; });
		// The reload that follows (cease fire off so it runs; the hound parked): stills over its 4.5 s.
		S.Add([](UWorld*)
		{
			Runner->CamOffset = FVector(330.f, 260.f, 80.f);
			Runner->bFrameOnTarget = false;
			Runner->LookHeight = 30.f;
			Runner->LookAhead = 0.f;
			Runner->Fov = 50.f;
			if (AOperativeCharacter* H = CurrentHero())
			{
				H->bTacticalCeaseFire = false;
			}
			if (AEnemyCharacter* T = Runner->Target.Get())
			{
				SmokeUtils::ParkEnemyFarAway(T);
			}
			return 10;
		});
		for (int32 Index = 0; Index < 7; ++Index)
		{
			S.Add([Index](UWorld*) { Still(FString::Printf(TEXT("reload_%d"), Index + 1)); return 17; });
		}
		S.Add([](UWorld*) { const AOperativeCharacter* H = CurrentHero(); return H && H->bIsReloading ? -1 : 15; });
		S.Add([](UWorld*) { Still(TEXT("reload_done")); return 5; });
		// Crouched: the Sit_Shot and the Sit_Reload.
		S.Add([](UWorld*)
		{
			if (AOperativeCharacter* H = CurrentHero())
			{
				H->bTacticalCeaseFire = true;
				H->SetStance(EOperativeStance::Crouching);
			}
			return 45;
		});
		S.Add([](UWorld*) { PlaceTarget(2500.f, 60.f); return 5; });
		S.Add([](UWorld*) { Fire(3); return 2; });
		S.Add([](UWorld*) { Still(TEXT("crouch_shot")); return 10; });
		S.Add([](UWorld*)
		{
			if (AOperativeCharacter* H = CurrentHero())
			{
				H->bTacticalCeaseFire = false;
			}
			if (AEnemyCharacter* T = Runner->Target.Get())
			{
				SmokeUtils::ParkEnemyFarAway(T);
			}
			return 30;
		});
		for (int32 Index = 0; Index < 4; ++Index)
		{
			S.Add([Index](UWorld*) { Still(FString::Printf(TEXT("crouch_reload_%d"), Index + 1)); return 25; });
		}
		S.Add([](UWorld*) { const AOperativeCharacter* H = CurrentHero(); return H && H->bIsReloading ? -1 : 10; });
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		Runner = MakeShared<FRunner>();
		Runner->Dir = FPaths::ProjectSavedDir() / TEXT("Screenshots/RPG7");
		IFileManager::Get().DeleteDirectory(*Runner->Dir, false, true);
		IFileManager::Get().MakeDirectory(*Runner->Dir, true);
		AddSteps(Runner->Steps);
		Runner->Handle = FCoreDelegates::OnEndFrame.AddStatic(&Tick);
		UE_LOG(LogCodexTactics, Display, TEXT("RpgShots: start -> %s"), *Runner->Dir);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.RpgShots"),
		TEXT("Rendered proof: the Commander with the RPG-7 on the launcher pack (the model in his hands, idle, walk / sprint / crouch walk from the side, aim up / down, the shot with the rocket leaving, the reload standing and crouched) -> Saved/Screenshots/RPG7/."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
