// Dev-only read-out of the Marksman's feet height (user bug report 2026-10-21 "the Marksman floats"; UE-only, no Godot reference).
//   UnrealEditor.exe CodexTactics.uproject /Game/Maps/L_MovementTest -game -RenderOffscreen -ResX=1280 -ResY=720 -nosound
//       -UseFixedTimeStep -FPS=60 -ExecCmds="CodexTactics.MarksmanFeetProbe"
// Spawns BP_Enemy_Marksman (deaf) on an empty spot, lets it idle, then logs per sample: the ground under it (line trace), the
// capsule bottom, the mesh component Z, root / pelvis / foot / ball bone heights and the lowest vertex-bounds Z; a still per
// sample when rendering (Saved/Screenshots/<Platform>/MarksmanFeet/). Changes nothing on disk.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AIController.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Debug/FrontendSmokeUtils.h"
#include "Debug/SmokeUtils.h"
#include "Engine/SkeletalMesh.h"
#include "EngineUtils.h"
#include "GameFlow/LevelEncounterSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace MarksmanFeetProbe
{
	struct FRunner
	{
		int32 Frame = 0;
		int32 Samples = 0;
		bool bRender = false;
		FString Dir;
		FDelegateHandle Handle;
		TWeakObjectPtr<AEnemyCharacter> Marksman;
		/** Mesh=<object path>: a runtime-only swap of the body (same skeleton) to compare. */
		FString MeshOverride;
		TWeakObjectPtr<ACameraActor> Camera;
		SmokeUtils::FClearArena Arena;
	};

	TSharedPtr<FRunner> Runner;

	float GroundZ(UWorld* World, const FVector& At, const AActor* Ignore)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MarksmanFeetProbe), false, Ignore);
		return World->LineTraceSingleByChannel(Hit, At + FVector(0.f, 0.f, 200.f), At - FVector(0.f, 0.f, 600.f), ECC_Visibility, Params)
			? Hit.ImpactPoint.Z : -99999.f;
	}

	void Sample(UWorld* World, const TCHAR* Label)
	{
		AEnemyCharacter* M = Runner->Marksman.Get();
		if (!M)
		{
			return;
		}
		USkeletalMeshComponent* Mesh = M->GetMesh();
		const float Ground = GroundZ(World, M->GetActorLocation(), M);
		const float CapsuleBottom = M->GetActorLocation().Z - M->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		FString Bones;
		for (const TCHAR* Bone : { TEXT("root"), TEXT("pelvis"), TEXT("foot_l"), TEXT("foot_r"), TEXT("ball_l"), TEXT("ball_r") })
		{
			if (Mesh->GetBoneIndex(Bone) != INDEX_NONE)
			{
				Bones += FString::Printf(TEXT(" %s %+.1f"), Bone, Mesh->GetBoneLocation(Bone).Z - Ground);
			}
		}
		UE_LOG(LogCodexTactics, Display, TEXT("MarksmanFeetProbe [%s]: mesh %s, anim %s | above the ground (cm): capsule bottom %+.1f, mesh component %+.1f, bounds min %+.1f |%s"),
			Label, *GetNameSafe(Mesh->GetSkeletalMeshAsset()), *GetNameSafe(Mesh->GetAnimInstance() ? Mesh->GetAnimInstance()->GetClass() : nullptr),
			CapsuleBottom - Ground, Mesh->GetComponentLocation().Z - Ground, Mesh->Bounds.GetBox().Min.Z - Ground, *Bones);
		if (Runner->bRender)
		{
			FScreenshotRequest::RequestScreenshot(Runner->Dir / FString::Printf(TEXT("%02d_%s.png"), Runner->Samples, Label), false, false);
		}
		++Runner->Samples;
	}

	void Tick()
	{
		FRunner& R = *Runner;
		UWorld* World = FrontendSmokeUtils::FindGameWorld();
		if (!World)
		{
			return;
		}
		++R.Frame;
		if (R.Frame == 30)
		{
			USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
			TArray<AOperativeCharacter*> Members = Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>();
			if (Members.IsEmpty())
			{
				R.Frame = 29;
				return;
			}
			TArray<const AActor*> Ignore;
			for (const AOperativeCharacter* Each : Members)
			{
				Ignore.Add(Each);
			}
			R.Arena = SmokeUtils::FindClearArena(World, Members[0]->GetActorLocation(), Members[0]->GetActorForwardVector(),
				FBox2D(FVector2D(-400.f, -400.f), FVector2D(1900.f, 400.f)), Ignore);
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				SmokeUtils::ParkEnemyFarAway(*It);
			}
			int32 Slot = -1;
			for (AOperativeCharacter* Each : Members)
			{
				Each->StopOperative();
				Each->bTacticalCeaseFire = true;
				const FVector Spot = R.Arena.Origin + R.Arena.Forward * 1600.f + R.Arena.Right * (200.f * Slot++) + FVector(0.f, 0.f, 95.f);
				Each->TeleportTo(SmokeUtils::FreeSpot(World, Spot, Each), (-R.Arena.Forward).Rotation(), false, true);
			}
			UClass* Class = LoadClass<AEnemyCharacter>(nullptr, TEXT("/Game/Characters/Enemies/Marksman/BP_Enemy_Marksman.BP_Enemy_Marksman_C"));
			const FTransform At(R.Arena.Forward.Rotation(), R.Arena.Origin + FVector(0.f, 0.f, 120.f));
			AEnemyCharacter* M = Class ? World->SpawnActorDeferred<AEnemyCharacter>(Class, At, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn) : nullptr;
			if (M)
			{
				FEnemyPerceptionParams Deaf = M->GetPerception();
				Deaf.SightRangeCm = 0.f;
				Deaf.ProximityCm = 0.f;
				Deaf.HearWalkCm = Deaf.HearRunCm = Deaf.HearCrouchWalkCm = Deaf.HearCrawlCm = Deaf.HearGunshotCm = Deaf.HearExplosionCm = 0.f;
				M->PerceptionOverride = Deaf;
				M->bOverridePerception = true;
				M->FinishSpawning(At);
				if (ULevelEncounterSubsystem* Encounter = World->GetSubsystem<ULevelEncounterSubsystem>())
				{
					Encounter->MarkLevelEnemy(M);
				}
				M->GetHealthComponent()->SetMaxHealth(100000.f, true);
				M->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				if (!R.MeshOverride.IsEmpty())
				{
					if (USkeletalMesh* Other = LoadObject<USkeletalMesh>(nullptr, *R.MeshOverride))
					{
						M->GetMesh()->EmptyOverrideMaterials();
						M->GetMesh()->SetSkeletalMeshAsset(Other);
					}
				}
			}
			R.Marksman = M;
			R.Camera = World->SpawnActor<ACameraActor>(R.Arena.Origin + R.Arena.Right * 420.f + FVector(0.f, 0.f, 90.f),
				(-R.Arena.Right).Rotation());
			if (ACameraActor* Camera = R.Camera.Get())
			{
				Camera->GetCameraComponent()->bConstrainAspectRatio = false;
				Camera->GetCameraComponent()->SetFieldOfView(45.f);
				if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
				{
					PC->SetViewTarget(Camera);
				}
			}
			UE_LOG(LogCodexTactics, Display, TEXT("MarksmanFeetProbe: spawned %s at %s (arena %d)"), *GetNameSafe(M), *At.GetLocation().ToCompactString(), R.Arena.bFound ? 1 : 0);
		}
		else if (R.Frame == 150)
		{
			Sample(World, TEXT("idle_2s"));
		}
		else if (R.Frame == 300)
		{
			Sample(World, TEXT("idle_4s"));
		}
		else if (R.Frame == 330)
		{
			if (AEnemyCharacter* M = R.Marksman.Get())
			{
				// a short walk: the side camera sees the feet over the ground
				if (AAIController* AIC = Cast<AAIController>(M->GetController()))
				{
					AIC->MoveToLocation(M->GetActorLocation() + R.Arena.Forward * 600.f, 20.f);
				}
			}
		}
		else if (R.Frame == 400)
		{
			Sample(World, TEXT("walk"));
		}
		else if (R.Frame > 420)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("MarksmanFeetProbe: %d samples in %s"), R.Samples, *R.Dir);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: PASS"));
			FCoreDelegates::OnEndFrame.Remove(R.Handle);
			FPlatformMisc::RequestExit(false, TEXT("MarksmanFeetProbe"));
		}
	}

	void Run(const TArray<FString>& Args, UWorld*)
	{
		Runner = MakeShared<FRunner>();
		for (const FString& Arg : Args)
		{
			FParse::Value(*Arg, TEXT("Mesh="), Runner->MeshOverride);
		}
		Runner->bRender = FApp::CanEverRender();
		Runner->Dir = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / TEXT("MarksmanFeet"));
		Runner->Handle = FCoreDelegates::OnEndFrame.AddStatic(&Tick);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.MarksmanFeetProbe"),
		TEXT("Dev: logs how high the Marksman's capsule / mesh / feet are above the ground (idle, walk); stills when rendering; exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
