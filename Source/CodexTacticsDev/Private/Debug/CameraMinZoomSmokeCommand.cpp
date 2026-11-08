// Dev-only headless check of the camera's minimum zoom (bug #52, game designer video 2026-11-06: the camera zoomed point-blank, the screen full
// of an operative's hand and rifle):
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands CodexTactics.CameraMinZoomSmoke
// 1. the mouse wheel zoomed in 50 times with real input (MouseScrollUp through APlayerController::InputKey): the camera is no closer to its
// focus than the wheel's minimum TacticalCameraRules::GetMinZoomDistance (8 m) and outside every character capsule;
// 2. the other zoom paths: a glide to the leader / an enemy with a tiny distance (SmoothFocusOnTarget 100 cm, the death cam / enemy focus
// path) and a snap (SnapFocusOnTarget 50 cm) end at the hard floor MinSafeDistance (7 m: cinematic / focus shots may close to it, the
// horde intro's type shot is exactly 7 m), not point-blank;
// 3. a character teleported onto the camera: the camera leaves his capsule.
// Code: Camera/TacticalCameraPawn.cpp (Tick clamp, PushOutOfCharacters), Camera/TacticalCameraRules.cpp (ClampMinDistance, PushOutOfCapsule).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/TacticalCameraPawn.h"
#include "Camera/TacticalCameraRules.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"

namespace CameraMinZoomSmoke
{
	constexpr float StepSeconds = 0.05f;
	constexpr int32 WheelNotches = 50;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		int32 Notches = 0;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("CameraMinZoomSmoke"));
		return false;
	}

	/** True when Location is inside any character's capsule (no clearance). */
	bool InsideAnyCapsule(UWorld& World, const FVector& Location, FString& Who)
	{
		for (TActorIterator<ACharacter> It(&World); It; ++It)
		{
			const UCapsuleComponent* Capsule = It->GetCapsuleComponent();
			if (Capsule && !TacticalCameraRules::PushOutOfCapsule(Location, Capsule->GetComponentLocation(),
				Capsule->GetScaledCapsuleHalfHeight(), Capsule->GetScaledCapsuleRadius(), 0.f).Equals(Location, 0.5f))
			{
				Who = It->GetName();
				return true;
			}
		}
		return false;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 120.f)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke stopped in stage %d"), State.Stage);
			return World ? Finish(State, false) : false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		ATacticalCameraPawn* Camera = PC ? Cast<ATacticalCameraPawn>(PC->GetPawn()) : nullptr;
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Camera || !Leader)
		{
			return State.Time < 10.f;
		}
		const float MinDistance = TacticalCameraRules::GetMinZoomDistance(Camera->Config); // the wheel's minimum
		const float FloorDistance = Camera->Config.MinSafeDistance; // the hard floor of the glides / snaps
		auto Next = [&State]() { ++State.Stage; State.StageTime = 0.f; };
		FString Who;
		switch (State.Stage)
		{
		case 0: // settle
			if (State.StageTime < 4.f)
			{
				return true;
			}
			UE_LOG(LogCodexTactics, Display, TEXT("CameraMinZoomSmoke: minimum %.0f cm (DistanceMin %.0f, MinSafeDistance %.0f), start %.0f cm"),
				MinDistance, Camera->Config.DistanceMin, Camera->Config.MinSafeDistance, Camera->GetCurrentDistance());
			Next();
			return true;
		case 1: // the wheel, 50 notches in
			if (State.Notches < WheelNotches)
			{
				PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseScrollUp, IE_Pressed, 1.f));
				PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseScrollUp, IE_Released, 0.f));
				++State.Notches;
				return true;
			}
			if (State.StageTime < 5.f)
			{
				return true;
			}
			{
				const float Dist = FVector::Dist(Camera->GetActorLocation(), Camera->GetViewCentre());
				Check(State, Camera->GetCurrentDistance() >= MinDistance - 1.f, FString::Printf(TEXT("after %d wheel notches in: distance %.0f cm >= minimum %.0f"), WheelNotches, Camera->GetCurrentDistance(), MinDistance));
				Check(State, Dist >= MinDistance - 5.f, FString::Printf(TEXT("... the camera is %.0f cm from the view centre"), Dist));
				Check(State, !InsideAnyCapsule(*World, Camera->GetActorLocation(), Who), TEXT("... and in no character's capsule ") + Who);
			}
			Next();
			return true;
		case 2: // a glide with a tiny distance (focus / death cam / enemy focus paths)
			Camera->SmoothFocusOnTarget(Leader, 0.2f, 100.f);
			Next();
			return true;
		case 3:
			if (State.StageTime < 2.f)
			{
				return true;
			}
			Check(State, FMath::IsNearlyEqual(Camera->GetCurrentDistance(), FloorDistance, 1.f), FString::Printf(TEXT("SmoothFocusOnTarget(leader, 100 cm) ends at the floor: %.0f cm = %.0f"), Camera->GetCurrentDistance(), FloorDistance));
			{
				AEnemyCharacter* Enemy = nullptr;
				for (TActorIterator<AEnemyCharacter> It(World); It && !Enemy; ++It)
				{
					Enemy = *It;
				}
				if (Enemy)
				{
					Camera->SmoothFocusOnTarget(Enemy, 0.2f, 100.f);
				}
				else
				{
					UE_LOG(LogCodexTactics, Display, TEXT("CameraMinZoomSmoke: no enemy on this map - the enemy focus check is skipped"));
				}
			}
			Next();
			return true;
		case 4:
			if (State.StageTime < 2.f)
			{
				return true;
			}
			Check(State, Camera->GetCurrentDistance() >= FloorDistance - 1.f, FString::Printf(TEXT("focus on an enemy with 100 cm: %.0f cm >= the floor %.0f"), Camera->GetCurrentDistance(), FloorDistance));
			Check(State, !InsideAnyCapsule(*World, Camera->GetActorLocation(), Who), TEXT("... the camera is in no capsule ") + Who);
			Camera->SnapFocusOnTarget(Leader, 50.f);
			Next();
			return true;
		case 5:
			if (State.StageTime < 1.f)
			{
				return true;
			}
			Check(State, FMath::IsNearlyEqual(Camera->GetCurrentDistance(), FloorDistance, 1.f), FString::Printf(TEXT("SnapFocusOnTarget(leader, 50 cm): at the floor %.0f cm = %.0f"), Camera->GetCurrentDistance(), FloorDistance));
			// A character walks into the camera.
			Leader->SetActorLocation(Camera->GetActorLocation(), false, nullptr, ETeleportType::TeleportPhysics);
			Next();
			return true;
		case 6:
			if (State.StageTime < 0.3f)
			{
				return true;
			}
			Check(State, !InsideAnyCapsule(*World, Camera->GetActorLocation(), Who), TEXT("a character teleported onto the camera: the camera left the capsule ") + Who);
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
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.CameraMinZoomSmoke"),
		TEXT("Dev check (bug #52, 2026-11-06): 50 wheel notches in, tiny-distance glides / snaps and a character walking into the camera - never closer than the minimum, never inside a capsule; PASS/FAIL, exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
