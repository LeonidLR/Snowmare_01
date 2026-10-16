// Dev-only console command for a headless check of the see-through silhouette on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.SilhouetteSmoke
// Godot player.gd _check_silhouette_occlusion + silhouette.gdshader: a block between the camera and the leader shows
// the cyan outline on his meshes; once it is gone the outline goes too.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/PlayerCameraManager.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"

namespace SilhouetteSmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		TWeakObjectPtr<AStaticMeshActor> Block;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("SilhouetteSmoke"));
		return false;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time < 3.f)
		{
			return World != nullptr;
		}
		AOperativeCharacter* Leader = World->GetSubsystem<USquadSubsystem>()->GetLeader();
		const APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		if (!Leader || !PC || !PC->PlayerCameraManager || State.Time > 20.f)
		{
			Check(State, false, TEXT("leader / camera / timeout"));
			return Finish(State, false);
		}
		auto Next = [&State]() { ++State.Stage; State.StageTime = 0.f; };
		auto HasOverlay = [Leader]()
		{
			TInlineComponentArray<UMeshComponent*> Meshes(Leader);
			for (const UMeshComponent* Mesh : Meshes)
			{
				if (Mesh->IsVisible() && Mesh->GetOverlayMaterial())
				{
					return true;
				}
			}
			return false;
		};
		switch (State.Stage)
		{
		case 0:
		{
			Check(State, !Leader->IsSilhouetteVisible() && !HasOverlay(), TEXT("in plain view: no silhouette"));
			// A 3 m block halfway between the camera and the leader.
			const FVector Camera = PC->PlayerCameraManager->GetCameraLocation();
			const FVector Middle = FMath::Lerp(Camera, Leader->GetActorLocation(), 0.5f);
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AStaticMeshActor* Block = World->SpawnActor<AStaticMeshActor>(Middle, FRotator::ZeroRotator, Params);
			if (!Block)
			{
				return Finish(State, false);
			}
			Block->SetMobility(EComponentMobility::Movable);
			Block->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
			Block->SetActorScale3D(FVector(3.f));
			Block->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
			State.Block = Block;
			Next();
			return true;
		}
		case 1:
			if (State.StageTime < 0.3f)
			{
				return true;
			}
			Check(State, Leader->IsSilhouetteVisible() && HasOverlay(), TEXT("behind the block: silhouette overlay on"));
			{
				const UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Leader->GetMesh()->GetOverlayMaterial());
				FLinearColor Color;
				Check(State, MID && MID->GetVectorParameterValue(TEXT("Color"), Color) && Color.Equals(Leader->GetSilhouetteColor(), 0.01f)
					&& Color.B > Color.R, TEXT("leader colour: cyan"));
			}
			if (State.Block.IsValid())
			{
				State.Block->Destroy();
			}
			Next();
			return true;
		case 2:
			if (State.StageTime < 0.3f)
			{
				return true;
			}
			Check(State, !Leader->IsSilhouetteVisible() && !HasOverlay(), TEXT("block gone: silhouette off"));
			return Finish(State, true);
		default:
			return Finish(State, false);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		{
			FTimerHandle PlaceHandle;
			TWeakObjectPtr<UWorld> PlaceWorld(World);
			World->GetTimerManager().SetTimer(PlaceHandle, FTimerDelegate::CreateLambda([PlaceWorld]()
			{
				SmokeUtils::PlaceSquadAtTestStart(PlaceWorld.Get());
			}), 0.5f, false);
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.SilhouetteSmoke"),
		TEXT("Dev check: the see-through silhouette behind a block; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
