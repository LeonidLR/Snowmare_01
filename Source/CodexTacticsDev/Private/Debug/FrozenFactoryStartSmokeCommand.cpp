// Dev-only check of the start of campaign level 0.1 (game designer 2026-10-22: "when the next level starts there's a fade, the players are
// on the new map, and behind them you can see the last point of the previous level - the small fenced compound we were in before"):
//   Scripts/smoke_parallel.ps1 -Commands FrozenFactoryStartSmoke   (map /Game/Maps/L_Demo01_B_FrozenFactory, Scripts/test_map.json)
//  1. the level starts behind a black screen with its title "0.1 Frozen Factory" that fades in (level JSON "start_fade_seconds": 2; black
//     during the hold, never brighter later, clear by the end of hold + fade); the factory's own level JSON (1 wave, no gate exit);
//  2. the lake compound's blast door, CLOSED (user decision 2026-10-22: no way back; AGateActor set dressing, inert, not the level's gate -
//     that is Gate_Checkpoint; its shut leaves block pawns and the navmesh) and its
//     walls stand behind the start; the three operatives spawned just OUTSIDE the doorway (1.5..12 m beyond its plane, within its width
//     + slack), facing away from it into the factory, on the navmesh;
//  3. the start camera frames the door and the compound wall behind the squad (the door, its two leaves and the east wall pieces inside the
//     camera frustum once the camera has settled on the leader), the door behind the leader's back.
// Build the map with Scripts/Editor/build_frozen_factory.py. Godot: none (UE-only campaign level).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsGameMode.h"
#include "Core/MissionRules.h"
#include "Core/MissionSubsystem.h"
#include "Data/WaveConfigTypes.h"
#include "UI/DialogueSubsystem.h"
#include "UI/GameMessageSubsystem.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/CampaignRules.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFlow/LevelExitSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "Interactables/GateActor.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"

namespace FrozenFactoryStartSmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FState
	{
		float Time = 0.f;
		int32 Stage = 0;
		float StageTime = 0.f;
		int32 Failures = 0;
		float FirstAlpha = -1.f;
		float FirstGameTime = -1.f;
		float LastAlpha = 2.f;
		bool bBrightened = false;
		float ClearAt = -1.f;
		TWeakObjectPtr<UWorld> World;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("FrozenFactoryStartSmoke"));
		return false;
	}

	void Next(FState& State)
	{
		++State.Stage;
		State.StageTime = 0.f;
	}

	/** Point inside the camera's view frustum (16:9, the camera's horizontal FOV). */
	bool InFrustum(const FVector& Eye, const FRotator& View, float HorizontalFovDeg, const FVector& Point)
	{
		const FRotationMatrix Axes(View);
		const FVector Delta = Point - Eye;
		const float Forward = FVector::DotProduct(Delta, Axes.GetScaledAxis(EAxis::X));
		if (Forward <= 10.f)
		{
			return false;
		}
		const float HalfH = FMath::DegreesToRadians(HorizontalFovDeg * 0.5f);
		const float HalfV = FMath::Atan(FMath::Tan(HalfH) * 9.f / 16.f);
		const float Right = FVector::DotProduct(Delta, Axes.GetScaledAxis(EAxis::Y));
		const float Up = FVector::DotProduct(Delta, Axes.GetScaledAxis(EAxis::Z));
		return FMath::Abs(FMath::Atan2(Right, Forward)) <= HalfH && FMath::Abs(FMath::Atan2(Up, Forward)) <= HalfV;
	}

	bool Step(FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = State.World.Get();
		if (!World)
		{
			return false;
		}
		if (State.Time > 180.f)
		{
			Check(State, false, FString::Printf(TEXT("time out in stage %d"), State.Stage));
			return Finish(State, false);
		}
		ULevelExitSubsystem* Exit = World->GetSubsystem<ULevelExitSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		if (!Exit || !Squad)
		{
			return true;
		}
		// The fade timeline, sampled from the first step on.
		if (Exit->IsStartFadeResolved())
		{
			const float Alpha = Exit->GetStartFadeAlpha();
			if (State.FirstAlpha < 0.f)
			{
				State.FirstAlpha = Alpha;
				State.FirstGameTime = World->GetTimeSeconds();
			}
			State.bBrightened |= Alpha > State.LastAlpha + 0.001f;
			State.LastAlpha = Alpha;
			if (Alpha <= 0.f && State.ClearAt < 0.f)
			{
				State.ClearAt = World->GetTimeSeconds();
			}
		}
		switch (State.Stage)
		{
		case 0:
		{
			if (!World->GetMapName().Contains(TEXT("L_Demo01_B_FrozenFactory")))
			{
				Check(State, false, FString::Printf(TEXT("run on L_Demo01_B_FrozenFactory (this is %s)"), *World->GetMapName()));
				return Finish(State, false);
			}
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			AOperativeCharacter* Leader = Squad->GetLeader();
			FNavLocation Probe;
			if (Squad->GetMembers().Num() < 3 || !Leader || !Exit->IsStartFadeResolved()
				|| !(Nav && Nav->ProjectPointToNavigation(Leader->GetActorLocation(), Probe, FVector(60.f, 60.f, 200.f))))
			{
				if (State.StageTime > 90.f)
				{
					Check(State, false, TEXT("three operatives on the navmesh"));
					return Finish(State, false);
				}
				return true;
			}
			const float Fade = Exit->GetStartFadeSeconds();
			Check(State, FMath::IsNearlyEqual(Fade, 2.f) && Exit->GetStartTitle() == TEXT("0.1 Frozen Factory"),
				FString::Printf(TEXT("the level starts with a %.1f s fade-in and the title \"%s\""), Fade, *Exit->GetStartTitle()));
			Check(State, State.FirstGameTime > CampaignRules::StartHoldSeconds || State.FirstAlpha >= 0.99f,
				FString::Printf(TEXT("black at the start (alpha %.2f at %.2f s)"), State.FirstAlpha, State.FirstGameTime));
			const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
			const ULevelExitSubsystem* ConstExit = Exit;
			Check(State, Flow && Flow->GetConfig().TotalWaves == 1 && !ConstExit->GetConfig().bEnabled,
				FString::Printf(TEXT("the factory's level JSON: 1 wave, no gate exit (%d waves)"), Flow ? Flow->GetConfig().TotalWaves : -1));
			Check(State, Squad->GetMembers().Num() == 3, FString::Printf(TEXT("the squad spawned (%d)"), Squad->GetMembers().Num()));
			// User decisions 2026-10-22: the lake's cold x0.4, and no MovementTest briefing (neither the window nor its radio line).
			const ACodexTacticsGameMode* GameMode = World->GetAuthGameMode<ACodexTacticsGameMode>();
			const ULevelConfigAsset* Level = GameMode ? GameMode->GetActiveLevelConfig() : nullptr;
			Check(State, Level && FMath::IsNearlyEqual(Level->Config.ColdRateMult, 0.4f) && !Level->Config.bIntroDialogue,
				FString::Printf(TEXT("the factory's level JSON: cold x%.2f, intro briefing off"), Level ? Level->Config.ColdRateMult : -1.f));
			const UGameMessageSubsystem* Messages = World->GetSubsystem<UGameMessageSubsystem>();
			const FString IntroRadio = MissionRules::GetModeRadio(EMissionStartMode::Game).ToString();
			const bool bIntroLine = Messages && Messages->GetHistory().ContainsByPredicate([&IntroRadio](const FGameMessage& Message)
			{
				return Message.Text.ToString() == IntroRadio;
			});
			const UDialogueSubsystem* Dialogue = World->GetSubsystem<UDialogueSubsystem>();
			Check(State, !bIntroLine && !(Dialogue && Dialogue->IsDialogueOpen()), TEXT("no MovementTest intro briefing (no window, no radio line)"));
			Next(State);
			return true;
		}
		case 1:
		{
			// The camera settles on the leader (it follows from the level's start).
			if (State.StageTime < 3.f)
			{
				return true;
			}
			AGateActor* Dressing = nullptr;
			int32 DressingCount = 0;
			for (TActorIterator<AGateActor> It(World); It; ++It)
			{
				if (It->bSetDressing)
				{
					Dressing = *It;
					++DressingCount;
				}
			}
			AGateActor* LevelGate = AGateActor::FindLevelGate(World);
			Check(State, DressingCount == 1 && Dressing && !Dressing->IsOpen() && !Dressing->IsOpening() && !Dressing->IsBeaconSpinning(),
				FString::Printf(TEXT("one CLOSED, inert set-dressing blast door (%d; user decision 2026-10-22: no way back)"), DressingCount));
			if (Dressing)
			{
				// The shut leaves block: a pawn trace through the doorway hits the gate, and no full navmesh path leads back into the compound.
				const FVector Door = Dressing->GetActorLocation() + FVector(0.f, 0.f, 100.f);
				const FVector Out = Dressing->GetActorForwardVector().GetSafeNormal2D();
				// Across the doorway at three spots (not on the seam between the leaves): a pawn trace hits the gate, the navmesh raycast is blocked.
				const FVector Side = Dressing->GetActorRightVector().GetSafeNormal2D();
				const float LeafWidth = Dressing->DoorSize.Y;
				UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
				FHitResult Hit;
				bool bBlocked = true;
				bool bNoWayIn = Nav != nullptr;
				for (const float Share : { -0.6f, 0.2f, 0.6f })
				{
					const FVector Across = Door + Side * (LeafWidth * Share);
					bBlocked &= World->LineTraceSingleByChannel(Hit, Across + Out * 200.f, Across - Out * 200.f, ECC_Pawn) && Hit.GetActor() == Dressing;
					FVector NavHit;
					const FVector Ground(Across.X, Across.Y, Dressing->GetActorLocation().Z);
					bNoWayIn &= Nav && Nav->NavigationRaycast(World, Ground + Out * 250.f, Ground - Out * 250.f, NavHit);
				}
				TArray<UBoxComponent*> Leaves;
				Dressing->GetComponents<UBoxComponent>(Leaves);
				for (const UBoxComponent* Leaf : Leaves)
				{
					UE_LOG(LogCodexTactics, Display, TEXT("[FactoryStart] door leaf %s at %s (relative %s), extent %s, collision %d, nav %d; trace hit %s"), *Leaf->GetName(),
						*Leaf->GetComponentLocation().ToCompactString(), *Leaf->GetRelativeLocation().ToCompactString(), *Leaf->GetScaledBoxExtent().ToCompactString(),
						static_cast<int32>(Leaf->GetCollisionEnabled()), Leaf->CanEverAffectNavigation() ? 1 : 0, *GetNameSafe(Hit.GetActor()));
				}
				Check(State, bBlocked && bNoWayIn, FString::Printf(TEXT("the closed door blocks the doorway (pawn traces hit it: %d) and the navmesh (nav raycasts through the doorway blocked: %d)"),
					bBlocked ? 1 : 0, bNoWayIn ? 1 : 0));
			}
			Check(State, LevelGate && LevelGate != Dressing && !LevelGate->IsOpen() && LevelGate->GetActorNameOrLabel().Contains(TEXT("Gate_Checkpoint")),
				FString::Printf(TEXT("the level's gate is the factory's own: %s"), LevelGate ? *LevelGate->GetActorNameOrLabel() : TEXT("none")));
			if (!Dressing)
			{
				return Finish(State, false);
			}
			const FVector Out = Dressing->GetActorForwardVector().GetSafeNormal2D();
			const FVector Along = Dressing->GetActorRightVector().GetSafeNormal2D();
			const FVector Door = Dressing->GetActorLocation();
			const float HalfWidth = Dressing->DoorSize.Y;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				const FVector Delta = Member->GetActorLocation() - Door;
				const float Beyond = FVector::DotProduct(Delta, Out);
				const float Lateral = FVector::DotProduct(Delta, Along);
				const float Facing = FVector::DotProduct(Member->GetActorForwardVector().GetSafeNormal2D(), Out);
				Check(State, Beyond > 150.f && Beyond < 1200.f && FMath::Abs(Lateral) < HalfWidth + 300.f && Facing > 0.7f,
					FString::Printf(TEXT("%s spawned just outside the open door (%.0f cm out, %.0f cm aside), facing away from it (%.2f)"),
						*Member->DisplayName.ToString(), Beyond, Lateral, Facing));
			}
			int32 Walls = 0;
			TArray<FVector> WallPoints;
			for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
			{
				if (It->ActorHasTag(TEXT("FrozenFactory_Compound")))
				{
					++Walls;
					const FString Name = It->GetActorNameOrLabel();
					if (Name.Contains(TEXT("Compound_Wall_E2")))
					{
						WallPoints.Add(It->GetActorLocation()); // the east wall pieces beside the door
					}
				}
			}
			Check(State, Walls >= 18 && WallPoints.Num() == 2, FString::Printf(TEXT("the compound set dressing: %d pieces, east wall beside the door %d"),
				Walls, WallPoints.Num()));
			// The start camera frames the door and the wall behind the squad.
			APlayerController* PC = World->GetFirstPlayerController();
			const APlayerCameraManager* Camera = PC ? PC->PlayerCameraManager.Get() : nullptr;
			if (!Camera)
			{
				Check(State, false, TEXT("a player camera"));
				return Finish(State, false);
			}
			const FVector Eye = Camera->GetCameraLocation();
			const FRotator View = Camera->GetCameraRotation();
			const float Fov = Camera->GetFOVAngle();
			const FVector DoorTop = Door + FVector(0.f, 0.f, 200.f);
			int32 WallsInView = 0;
			for (const FVector& Point : WallPoints)
			{
				WallsInView += InFrustum(Eye, View, Fov, Point) ? 1 : 0;
			}
			Check(State, InFrustum(Eye, View, Fov, DoorTop) && InFrustum(Eye, View, Fov, Door - Along * (HalfWidth * 0.5f) + FVector(0.f, 0.f, 200.f)) && InFrustum(Eye, View, Fov, Door + Along * (HalfWidth * 0.5f) + FVector(0.f, 0.f, 200.f)) && WallsInView >= 1,
				FString::Printf(TEXT("the start camera frames the closed door, both leaves and the wall beside it (%d / %d wall pieces; eye %s, view %s, fov %.0f)"),
					WallsInView, WallPoints.Num(), *Eye.ToCompactString(), *View.ToCompactString(), Fov));
			const AOperativeCharacter* Leader = Squad->GetLeader();
			Check(State, Leader && FVector::DotProduct((Door - Leader->GetActorLocation()).GetSafeNormal2D(), Leader->GetActorForwardVector().GetSafeNormal2D()) < -0.5f,
				TEXT("the door is behind the leader's back"));
			Next(State);
			return true;
		}
		case 2:
		{
			// The fade is over by hold + fade (+ a step), and the screen never got darker again on the way.
			// Measured in game time (the fade itself advances by frame deltas): wall-clock stalls under parallel load must not fail it.
			const float End = CampaignRules::StartHoldSeconds + Exit->GetStartFadeSeconds();
			if (State.ClearAt < 0.f && World->GetTimeSeconds() < End + 2.f)
			{
				return true;
			}
			Check(State, State.ClearAt >= 0.f && State.ClearAt <= End + 0.5f && !State.bBrightened,
				FString::Printf(TEXT("the fade-in clears by %.1f s (clear at %.2f s), never darkening on the way"), End, State.ClearAt));
			return Finish(State, true);
		}
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
		TSharedRef<FState> State = MakeShared<FState>();
		State->World = World;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float)
		{
			return Step(*State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.FrozenFactoryStartSmoke"),
		TEXT("Dev check (2026-10-22 b): campaign level 0.1 L_Demo01_B_FrozenFactory starts with a fade-in from black and its title; the squad spawns just "
			"outside the lake compound's open blast door (set dressing) facing away from it; the start camera frames the door and the compound wall."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
