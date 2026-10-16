// Dev-only still of the APC turret firing (APC P2; needs rendering, not -nullrhi):
//   UnrealEditor.exe CodexTactics.uproject /Game/Maps/L_MovementTest -game -windowed -ResX=1600 -ResY=900 -RenderOffscreen
//     -ExecCmds="CodexTactics.VehicleTurretShot <absolute png path>"
// The APC parks at the map layout with the Engineer driving and the Commander on the gun, a Brute stands 20 m ahead (AI
// held); the screenshot is taken on the frame the autocannon fires (muzzle flash + tracer), then the game exits.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/TacticalCameraPawn.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Vehicles/VehiclePawn.h"
#include "Vehicles/VehicleSubsystem.h"
#include "Vehicles/VehicleTurretComponent.h"

namespace VehicleTurretShot
{
	struct FState
	{
		float Time = 0.f;
		int32 Stage = 0;
		FString Path;
		TWeakObjectPtr<AVehiclePawn> Apc;
		int32 ShotsBefore = 0;
		float ShotAt = -1.f;
	};

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State, float Delta)
	{
		State.Time += Delta;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 60.f)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[VehicleTurretShot] gave up"));
			FPlatformMisc::RequestExit(false, TEXT("VehicleTurretShot"));
			return false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UVehicleSubsystem* Vehicles = World->GetSubsystem<UVehicleSubsystem>();
		AVehiclePawn* Apc = State.Apc.Get();
		if (State.Stage == 0)
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
				FPlatformMisc::RequestExit(false, TEXT("VehicleTurretShot"));
				return false;
			}
			State.Apc = Apc;
			const float Yaw = Layout.Rotator().Yaw;
			Apc->RestoreTransform(At, Yaw);
			Vehicles->MountImmediately(Engineer, Apc);
			Vehicles->MountImmediately(Commander, Apc);
			Squad->SetLeader(Commander);
			Squad->SetSquadPosture(ESquadFirePosture::Aggressive);
			const FVector Forward = FRotator(0.f, Yaw + 25.f, 0.f).Vector();
			if (UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>())
			{
				if (AEnemyCharacter* Brute = Waves->SpawnEnemy(EEnemyArchetype::Brute, Apc->GetHullCentreWorld() + Forward * 2000.f + FVector(0.f, 0.f, 50.f), (-Forward).Rotation()))
				{
					Brute->SetTurnBasedHeld(true);
					Brute->GetHealthComponent()->SetMaxHealth(100000.f);
				}
			}
			if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
			{
				if (ATacticalCameraPawn* Camera = PC->GetPawn<ATacticalCameraPawn>())
				{
					Camera->SnapFocusOnTarget(Commander);
					Camera->AddZoomNotches(-3.f);
				}
				if (PC->MyHUD)
				{
					PC->MyHUD->bShowHUD = false; // a clean still of the hull, the flash and the tracer
				}
			}
			State.Stage = 1;
			return true;
		}
		if (State.Stage == 1 && Apc && Apc->Turret)
		{
			const int32 Shots = Apc->Turret->GetShotsFired(EVehicleTurretWeapon::Cannon);
			if (State.ShotsBefore == 0 && Shots >= 2)
			{
				// The second shell of a burst: the turret has settled on the target. Shoot the frame of the flash.
				FScreenshotRequest::RequestScreenshot(State.Path, false, false);
				UE_LOG(LogCodexTactics, Display, TEXT("[VehicleTurretShot] screenshot requested: %s"), *State.Path);
				State.ShotsBefore = Shots;
				State.ShotAt = State.Time;
			}
			if (State.ShotAt > 0.f && State.Time > State.ShotAt + 1.5f)
			{
				FPlatformMisc::RequestExit(false, TEXT("VehicleTurretShot"));
				return false;
			}
		}
		return true;
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		TSharedRef<FState> State = MakeShared<FState>();
		State->Path = Args.Num() > 0 ? FString::Join(Args, TEXT(" ")) : FPaths::ScreenShotDir() / TEXT("VehicleTurretShot.png"); // a path with spaces arrives split
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float Delta)
		{
			return Step(WeakWorld, *State, Delta);
		}), 0.f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.VehicleTurretShot"),
		TEXT("Dev: screenshot of the APC turret firing its autocannon at a Brute (arg: absolute png path), then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
