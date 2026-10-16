// Dev-only still of a disabled APC (APC P4; needs rendering, not -nullrhi):
//   UnrealEditor.exe CodexTactics.uproject /Game/Maps/L_MovementTest -game -windowed -ResX=1600 -ResY=900 -RenderOffscreen
//     -ExecCmds="CodexTactics.VehicleDisabledShot <absolute png path>"
// The APC parks at the map layout with the Engineer and the Commander aboard; its hull is brought to 0 HP: the crew is
// ejected into hull cover, the damaged model + wreck smoke show; the screenshot is taken a few seconds later, then the
// game exits.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/TacticalCameraPawn.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Vehicles/VehiclePawn.h"
#include "Vehicles/VehicleSubsystem.h"

namespace VehicleDisabledShot
{
	struct FState
	{
		float Time = 0.f;
		float DisabledAt = -1.f;
		float ShotAt = -1.f;
		FString Path;
		TWeakObjectPtr<AVehiclePawn> Apc;
	};

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State, float Delta)
	{
		State.Time += Delta;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 60.f)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[VehicleDisabledShot] gave up"));
			FPlatformMisc::RequestExit(false, TEXT("VehicleDisabledShot"));
			return false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UVehicleSubsystem* Vehicles = World->GetSubsystem<UVehicleSubsystem>();
		AVehiclePawn* Apc = State.Apc.Get();
		if (!Apc)
		{
			if (State.Time < 3.f)
			{
				return true;
			}
			AOperativeCharacter* Commander = nullptr;
			AOperativeCharacter* Engineer = nullptr;
			for (AOperativeCharacter* Member : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
			{
				Member->HealthComponent->SetMaxHealth(100000.f);
				Commander = Member->SquadRole == EOperativeRole::Commander ? Member : Commander;
				Engineer = Member->SquadRole == EOperativeRole::Engineer ? Member : Engineer;
			}
			const FTransform Layout = SmokeUtils::LayoutTransform(World);
			const FVector At = Layout.TransformPosition(FVector(-1200.f, 600.f, 0.f));
			UClass* ApcClass = LoadClass<AVehiclePawn>(nullptr, TEXT("/Game/Vehicles/BP_APC_BTR82.BP_APC_BTR82_C"));
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Apc = World->SpawnActor<AVehiclePawn>(ApcClass ? ApcClass : AVehiclePawn::StaticClass(), At + FVector(0.f, 0.f, 200.f), FRotator::ZeroRotator, Params);
			if (!Apc || !Vehicles || !Commander || !Engineer)
			{
				FPlatformMisc::RequestExit(false, TEXT("VehicleDisabledShot"));
				return false;
			}
			State.Apc = Apc;
			Apc->RestoreTransform(At, Layout.Rotator().Yaw + 30.f);
			Vehicles->MountImmediately(Engineer, Apc);
			Vehicles->MountImmediately(Commander, Apc);
			Squad->SetLeader(Commander);
			if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
			{
				if (ATacticalCameraPawn* Camera = PC->GetPawn<ATacticalCameraPawn>())
				{
					Camera->SnapFocusOnTarget(Commander);
					Camera->AddZoomNotches(-3.f);
				}
			}
			return true;
		}
		if (State.DisabledAt < 0.f && State.Time > 4.f)
		{
			Apc->ApplyHullDamage(Apc->GetHullHealth() + 100.f, EVehicleDamageSource::HeavyMelee, TEXT("VehicleDisabledShot"));
			State.DisabledAt = State.Time;
		}
		if (State.DisabledAt > 0.f && State.ShotAt < 0.f && State.Time > State.DisabledAt + 3.f && State.Time < State.DisabledAt + 3.1f)
		{
			// Frame the hull itself (the leader was moved out of it).
			if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
			{
				if (ATacticalCameraPawn* Camera = PC->GetPawn<ATacticalCameraPawn>())
				{
					Camera->SnapFocusOnTarget(Apc);
					UE_LOG(LogCodexTactics, Display, TEXT("[VehicleDisabledShot] camera at %s, APC at %s"), *Camera->GetActorLocation().ToCompactString(),
						*Apc->GetActorLocation().ToCompactString());
				}
			}
		}
		if (State.DisabledAt > 0.f && State.ShotAt < 0.f && State.Time > State.DisabledAt + 4.f)
		{
			FScreenshotRequest::RequestScreenshot(State.Path, true, false);
			UE_LOG(LogCodexTactics, Display, TEXT("[VehicleDisabledShot] screenshot requested: %s (disabled %d, crew %d)"), *State.Path,
				Apc->IsDisabled() ? 1 : 0, Apc->GetCrewCount());
			State.ShotAt = State.Time;
		}
		if (State.ShotAt > 0.f && State.Time > State.ShotAt + 1.5f)
		{
			FPlatformMisc::RequestExit(false, TEXT("VehicleDisabledShot"));
			return false;
		}
		return true;
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		TSharedRef<FState> State = MakeShared<FState>();
		State->Path = Args.Num() > 0 ? FString::Join(Args, TEXT(" ")) : FPaths::ScreenShotDir() / TEXT("VehicleDisabledShot.png"); // a path with spaces arrives split
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float Delta)
		{
			return Step(WeakWorld, *State, Delta);
		}), 0.f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.VehicleDisabledShot"),
		TEXT("Dev: screenshot of a disabled APC (damaged model, wreck smoke, the crew in hull cover; arg: absolute png path), then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
