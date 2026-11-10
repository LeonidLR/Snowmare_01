// Dev-only console command: rendered proof of the D-1 shotgun's FULL-BODY locomotion on the ShotgunLocomotionPack's Loop_Root loops for the game
// designer's phone (2026-11-09 b: "In /Game/ShotgunLocomotionPack/Animations/Loop_Root there are the animations to set up the shotgun locomotion").
// Needs rendering (no -nullrhi), a fixed frame time so the clips play at real speed:
//   UnrealEditor.exe CodexTactics.uproject /Game/Maps/L_MovementTest -game -RenderOffscreen -ResX=1280 -ResY=720 -nosound
//     -benchmark -fps=30 -ExecCmds="CodexTactics.ShotgunLoopShots"
// Output: Saved/Screenshots/ShotgunLoopRoot/NN_<what>.png - the Engineer with the D-1 from the side (the pack's legs): idle, walk (150 cm/s),
// his normal walk (220), jog (400), sprint, aim walk, crouch walk. The camera follows him; no keys, no desktop capture.
// "CodexTactics.ShotgunLoopShots crouch" (the game designer's play-test 2026-11-09: crouched = the ABP's crouch, "crouch_body": "abp"): only the
// crouch - idle and crouch walk from his right AND his left side (the left hand on the D-1's fore-end) -> Saved/Screenshots/ShotgunCrouch/.

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
#include "Components/SkeletalMeshComponent.h"
#include "Debug/FrontendSmokeUtils.h"
#include "Debug/SmokeUtils.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "GameFramework/HUD.h"
#include "UnrealClient.h"

namespace ShotgunLoopShots
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
	};

	TSharedPtr<FRunner> Runner;
	/** A speed override for the walk legs (cm/s; < 0 = his own). */
	float SpeedOverride = -1.f;
	/** The empty strip of the map the legs are shot on (SmokeUtils::FindClearArena). */
	FVector ArenaOrigin = FVector::ZeroVector;
	FVector ArenaForward = FVector::ForwardVector;

	void Still(const FString& Name)
	{
		if (const AOperativeCharacter* Hero = Runner->Hero.Get())
		{
			const UOperativeAnimInstance* Anim = Hero->GetMesh() ? Cast<UOperativeAnimInstance>(Hero->GetMesh()->GetAnimInstance()) : nullptr;
			const UAnimSequenceBase* Clip = Anim ? Anim->GetShotgunDominantClip() : nullptr;
			UE_LOG(LogCodexTactics, Display, TEXT("ShotgunLoopShots: %s - mask %d upper %.2f lower %.2f clip %s speed %.0f"), *Name,
				Anim ? static_cast<int32>(Anim->GetShotgunLayerMask()) : -1, Anim ? Anim->GetShotgunUpperWeight() : 0.f, Anim ? Anim->GetShotgunLowerWeight() : 0.f,
				Clip ? *Clip->GetName() : TEXT("none"), Hero->GetVelocity().Size2D());
		}
		const FString Path = Runner->Dir / FString::Printf(TEXT("%02d_%s.png"), ++Runner->StillCount, *Name);
		FScreenshotRequest::RequestScreenshot(Path, false, false);
	}

	void Finish()
	{
		UE_LOG(LogCodexTactics, Display, TEXT("ShotgunLoopShots: done (%d stills)"), Runner->StillCount);
		FCoreDelegates::OnEndFrame.Remove(Runner->Handle);
		FPlatformMisc::RequestExit(false, TEXT("ShotgunLoopShots"));
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
		if (SpeedOverride > 0.f && Hero->GetCharacterMovement())
		{
			Hero->GetCharacterMovement()->MaxWalkSpeed = SpeedOverride;
		}
		FVector Centre = Hero->GetActorLocation() + FVector(0.f, 0.f, Runner->LookHeight) + Hero->GetActorForwardVector() * Runner->LookAhead;
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
		// No HUD in the pictures.
		UWidgetLayoutLibrary::RemoveAllWidgets(World);
		if (AHUD* Hud = PC->GetHUD())
		{
			Hud->bShowHUD = false;
		}
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
			UE_LOG(LogCodexTactics, Display, TEXT("ShotgunLoopShots: timeout in step %d"), R.Index);
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

	void MoveAhead(UWorld* World, float Distance, bool bRun)
	{
		if (AOperativeCharacter* H = CurrentHero())
		{
			H->OrderMoveTo(SmokeUtils::ClearPoint(World, H->GetActorLocation(), H->GetActorLocation() + H->GetActorForwardVector() * Distance), bRun);
		}
	}

	void AddMoveStills(TArray<FStep>& S, const FString& Name, float Distance, bool bRun, float Speed, bool bCrouch = false, float CamSide = 520.f)
	{
		// Back to the arena's start, the stance set, then the leg.
		S.Add([bCrouch](UWorld*)
		{
			if (AOperativeCharacter* H = CurrentHero())
			{
				H->StopOperative();
				H->TeleportTo(ArenaOrigin, ArenaForward.Rotation(), false, true);
				H->SetStance(bCrouch ? EOperativeStance::Crouching : EOperativeStance::Standing);
			}
			return 50;
		});
		S.Add([Distance, bRun, Speed, CamSide](UWorld* World)
		{
			SpeedOverride = Speed;
			MoveAhead(World, Distance, bRun);
			Runner->CamOffset = FVector(0.f, CamSide, 30.f);
			Runner->LookHeight = -5.f;
			Runner->Fov = 40.f;
			return bRun ? 30 : 22;
		});
		for (int32 Index = 0; Index < 4; ++Index)
		{
			S.Add([Name, Index](UWorld*) { Still(FString::Printf(TEXT("%s_side_%d"), *Name, Index + 1)); return 4; });
		}
		S.Add([](UWorld*) { const AOperativeCharacter* H = CurrentHero(); return H && H->GetVelocity().Size2D() > 10.f ? -1 : 20; });
		S.Add([](UWorld*) { SpeedOverride = -1.f; if (AOperativeCharacter* H = CurrentHero()) { H->ApplyMovementParams(); } return 5; });
	}

	void AddSteps(TArray<FStep>& S, bool bCrouchOnly)
	{
		S.Add([](UWorld* World) { return World->GetSubsystem<USquadSubsystem>() && World->GetSubsystem<USquadSubsystem>()->GetMembers().Num() >= 3 ? 60 : -1; });
		S.Add([](UWorld* World)
		{
			// Exploration (no combat: the squad would take cover behind the level's barricades), no enemies.
			World->GetSubsystem<UGameFlowSubsystem>()->FinishCutscene();
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->Destroy();
			}
			SmokeUtils::PlaceSquadAtTestStart(World);
			USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
			AOperativeCharacter* Engineer = nullptr;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Engineer = Member->SquadRole == EOperativeRole::Engineer ? Member : Engineer;
			}
			if (!Engineer)
			{
				return 1;
			}
			Runner->Hero = Engineer;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->HealthComponent->SetMaxHealth(5000.f, true);
				Member->bTacticalCeaseFire = true;
				if (Member != Engineer)
				{
					Member->TeleportTo(SmokeUtils::FreeSpot(World, Engineer->GetActorLocation() - Engineer->GetActorForwardVector() * 1200.f
						+ Engineer->GetActorRightVector() * (Member->SquadRole == EOperativeRole::Commander ? -400.f : 400.f), Member), Member->GetActorRotation(), false, true);
				}
			}
			Squad->SetLeader(Engineer);
			TArray<const AActor*> Ignore;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Ignore.Add(Member);
			}
			const SmokeUtils::FClearArena Arena = SmokeUtils::FindClearArena(World, Engineer->GetActorLocation(), Engineer->GetActorForwardVector(),
				FBox2D(FVector2D(-300.f, -500.f), FVector2D(2300.f, 500.f)), Ignore);
			ArenaOrigin = Arena.bFound ? Arena.Origin + FVector(0.f, 0.f, 100.f) : Engineer->GetActorLocation();
			ArenaForward = Arena.bFound ? Arena.Forward : Engineer->GetActorForwardVector();
			UE_LOG(LogCodexTactics, Display, TEXT("ShotgunLoopShots: arena %s at %s"), Arena.bFound ? TEXT("found") : TEXT("NOT found"), *ArenaOrigin.ToCompactString());
			Engineer->TeleportTo(ArenaOrigin, ArenaForward.Rotation(), false, true);
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member != Engineer)
				{
					Member->TeleportTo(SmokeUtils::FreeSpot(World, ArenaOrigin - ArenaForward * 1500.f, Member), Member->GetActorRotation(), false, true);
				}
			}
			return 45;
		});
		S.Add([](UWorld*) { Runner->CamOffset = FVector(0.f, 520.f, 30.f); Runner->LookHeight = -5.f; Runner->Fov = 40.f; return 20; });
		if (bCrouchOnly)
		{
			S.Add([](UWorld*) { if (AOperativeCharacter* H = CurrentHero()) { H->TeleportTo(ArenaOrigin, ArenaForward.Rotation(), false, true); H->SetStance(EOperativeStance::Crouching); } return 50; });
			S.Add([](UWorld*) { Still(TEXT("crouch_idle_right")); return 4; });
			S.Add([](UWorld*) { Runner->CamOffset = FVector(0.f, -520.f, 30.f); return 10; });
			S.Add([](UWorld*) { Still(TEXT("crouch_idle_left")); return 4; });
			AddMoveStills(S, TEXT("crouch_walk_right"), 600.f, false, -1.f, true, 520.f);
			AddMoveStills(S, TEXT("crouch_walk_left"), 600.f, false, -1.f, true, -520.f);
			return;
		}
		S.Add([](UWorld*) { Still(TEXT("idle_side")); return 4; });
		AddMoveStills(S, TEXT("walk150"), 700.f, false, 150.f);
		AddMoveStills(S, TEXT("walk220"), 700.f, false, -1.f);
		AddMoveStills(S, TEXT("jog400"), 1000.f, false, 400.f);
		AddMoveStills(S, TEXT("sprint"), 2000.f, true, -1.f);
		S.Add([](UWorld*) { if (AOperativeCharacter* H = CurrentHero()) { H->TeleportTo(ArenaOrigin, ArenaForward.Rotation(), false, true); H->SetStance(EOperativeStance::Crouching); } return 50; });
		S.Add([](UWorld*) { Still(TEXT("crouch_idle_side")); return 4; });
		AddMoveStills(S, TEXT("crouch_walk"), 600.f, false, -1.f, true);
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		Runner = MakeShared<FRunner>();
		const bool bCrouchOnly = Args.Contains(TEXT("crouch"));
		Runner->Dir = FPaths::ProjectSavedDir() / (bCrouchOnly ? TEXT("Screenshots/ShotgunCrouch") : TEXT("Screenshots/ShotgunLoopRoot"));
		IFileManager::Get().DeleteDirectory(*Runner->Dir, false, true);
		IFileManager::Get().MakeDirectory(*Runner->Dir, true);
		AddSteps(Runner->Steps, bCrouchOnly);
		Runner->Handle = FCoreDelegates::OnEndFrame.AddStatic(&Tick);
		UE_LOG(LogCodexTactics, Display, TEXT("ShotgunLoopShots: start -> %s"), *Runner->Dir);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.ShotgunLoopShots"),
		TEXT("Rendered proof: the Engineer with the D-1 on the full-body Loop_Root locomotion (idle, walk, jog, sprint, crouch walk from the side, no turn) -> Saved/Screenshots/ShotgunLoopRoot/."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
