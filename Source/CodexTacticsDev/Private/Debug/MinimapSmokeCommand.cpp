// Dev-only check of the minimap (user decisions 2026-10-18, docs/design/MINIMAP.md; UMinimapSubsystem) on L_MovementTest:
//   Scripts/smoke_parallel.ps1 -Commands MinimapSmoke
//   rendered stills (any map, e.g. the lake): UnrealEditor.exe <uproject> /Game/Maps/L_Demo01_A_FrozenLake -game -RenderOffscreen
//       -ResX=1600 -ResY=900 -ExecCmds="CodexTactics.MinimapSmoke Shot"  -> Saved/Screenshots/minimap_hud_<map>.png, minimap_full_<map>.png,
//       minimap_floor2_<map>.png, minimap_floor2_full_<map>.png (in Shot mode the squad is teleported instead of ordered: the tutorial
//       may lock moves; the 2nd floor needs a runtime navmesh for the spawned box: L_MovementTest)
//  0. the level is captured: one floor, its render target, the projection at the start camera's yaw / pitch, the panel shows the
//     messages by default;
//  1. F2 (the real key) switches the panel to the map and back, the session remembers it, a header click switches it too;
//  2. the squad's icons sit at the projected positions (leader highlighted, facing arrow, the corner map centred on him);
//  3. fog: revealed round the squad, dark 40 m away;
//  4. LMB on the map moves the camera there (it stays), the wheel over the map zooms the map, not the camera;
//  5. RMB on the map orders the leader there (the normal ground-click path); the fog grows as he walks;
//  6. Tab (the real key) opens the full map, Tab closes it, Esc closes it too; F3 opens it in turn-based combat too (key bound);
//  7. a two-storey box with a ramp is spawned at runtime, the navmesh rebuilt, CodexTactics.MinimapRecapture redetects the floors:
//     2 floors (the roof on top is no floor: no path), the upper floor's walls / roof hidden on the ground capture, the roof hidden
//     on the 2nd floor's capture, its walls kept;
//  8. the leader walks up the ramp: the map shows the 2nd floor, its fog layer reveals; back down: the ground again;
//  9. save state: fog + panel restored from the minimap's save JSON; the world save carries "minimap".

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/TacticalCameraPawn.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Core/MissionSessionSubsystem.h"
#include "Core/SaveGameSubsystem.h"
#include "Debug/SmokeUtils.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "GameFramework/Controller.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "UI/CodexTacticsHUD.h"
#include "UI/MinimapSubsystem.h"
#include "UnrealClient.h"

namespace MinimapSmoke
{
	constexpr float StepSeconds = 0.05f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		bool bShot = false;
		bool bMovementTest = false;
		FString MapName;
		bool bOrderSent = false;
		FVector Target = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		float GroundZ = 0.f;
		int32 FogBefore = 0;
		FVector SlabTop = FVector::ZeroVector;
		FVector Down = FVector::ZeroVector;
		TWeakObjectPtr<AStaticMeshActor> Slab;
		TWeakObjectPtr<AStaticMeshActor> UpperWall;
		TWeakObjectPtr<AStaticMeshActor> GroundWall;
		TWeakObjectPtr<AStaticMeshActor> Roof;
		TSharedPtr<FJsonObject> Saved;
		int32 SavedFog0 = 0;
		int32 ShotIndex = 0;
		TArray<TWeakObjectPtr<AActor>> Spawned;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("MinimapSmoke"));
		return false;
	}

	void Next(FState& State, int32 Stage = -1)
	{
		State.Stage = Stage >= 0 ? Stage : State.Stage + 1;
		State.StageTime = 0.f;
		State.bOrderSent = false;
	}

	void Tap(APlayerController* PC, const FKey& Key)
	{
		PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Pressed, 1.f));
		PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Released, 0.f));
	}

	AStaticMeshActor* SpawnBlock(UWorld* World, const FVector& Centre, const FVector& Size, const FRotator& Rotation)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* Block = World->SpawnActor<AStaticMeshActor>(Centre, Rotation, Params);
		if (!Block)
		{
			return nullptr;
		}
		Block->SetMobility(EComponentMobility::Movable);
		UStaticMeshComponent* Mesh = Block->GetStaticMeshComponent();
		Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		Mesh->SetCollisionProfileName(TEXT("BlockAll"));
		Mesh->SetCanEverAffectNavigation(true);
		Block->SetActorScale3D(Size / 100.f);
		return Block;
	}

	FVector Feet(const AActor* Actor)
	{
		return Actor->GetActorLocation() - FVector(0.f, 0.f, Actor->GetSimpleCollisionHalfHeight());
	}

	/** Order the leader alone through the controller's real ground click; in Shot mode (tutorial locks) teleport him. */
	void SendLeader(UWorld* World, FState& State, USquadSubsystem* Squad, const FVector& Goal)
	{
		AOperativeCharacter* Leader = Squad->GetLeader();
		Squad->SetSelectedGroup(TArray<AOperativeCharacter*>{ Leader }, false);
		if (State.bShot)
		{
			if (AController* Controller = Leader->GetController())
			{
				Controller->StopMovement(); // an earlier (refused / running) order must not walk him off again
			}
			Leader->SetActorLocation(Goal + FVector(0.f, 0.f, Leader->GetSimpleCollisionHalfHeight() + 5.f), false, nullptr, ETeleportType::TeleportPhysics);
			return;
		}
		if (ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController()))
		{
			FHitResult Hit;
			Hit.ImpactPoint = Goal;
			Hit.Location = Goal;
			Hit.ImpactNormal = FVector::UpVector;
			Hit.bBlockingHit = true;
			PC->HandleWorldHit(Hit);
		}
	}

	void Screenshot(FState& State, const TCHAR* Name)
	{
		// minimap_<shot>_<map>.png (e.g. minimap_hud_L_Demo01_A_FrozenLake.png)
		const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots") / FString::Printf(TEXT("%s_%s.png"), Name, *State.MapName);
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke screenshot %s"), *Path);
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		if (State.Time < 3.f)
		{
			return true;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UMinimapSubsystem* Minimap = World->GetSubsystem<UMinimapSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController());
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		ATacticalCameraPawn* Camera = PC ? PC->GetPawn<ATacticalCameraPawn>() : nullptr;
		if (!Squad || !Minimap || !PC || !Leader || !Camera || State.Time > 240.f)
		{
			Check(State, false, FString::Printf(TEXT("squad / minimap / controller / camera / timeout (stage %d)"), State.Stage));
			return Finish(State, false);
		}
		const bool bRendered = FApp::CanEverRender();
		switch (State.Stage)
		{
		case 0:
		{
			if (!Minimap->IsReady() && State.StageTime < 20.f)
			{
				return true;
			}
			if (!bRendered)
			{
				Minimap->SetViewportSizeForTesting(FVector2D(1280.f, 720.f));
			}
			Check(State, Minimap->IsReady(), FString::Printf(TEXT("captured at load (setup %.1f ms)"), Minimap->GetSetupMs()));
			const UTextureRenderTarget2D* Target = Minimap->GetCapture(0);
			Check(State, Target && Target->SizeX >= 256 && Target->SizeY >= 64, FString::Printf(TEXT("ground floor capture: a %dx%d render target"),
				Target ? Target->SizeX : 0, Target ? Target->SizeY : 0));
			if (State.bMovementTest)
			{
				Check(State, Minimap->GetFloorCount() == 1, FString::Printf(TEXT("L_MovementTest: one floor (%d)"), Minimap->GetFloorCount()));
			}
			const FMinimapIsoProjection& Projection = Minimap->GetProjection();
			Check(State, FMath::IsNearlyEqual(Projection.YawDeg, Camera->Config.BaseYaw, 0.01f) && FMath::IsNearlyEqual(Projection.PitchDeg, Camera->Config.Pitch, 0.01f),
				FString::Printf(TEXT("iso view at the start camera's angle (yaw %.1f pitch %.2f)"), Projection.YawDeg, Projection.PitchDeg));
			const float AspectError = FMath::Abs(Projection.GetAspect() - (Target ? static_cast<float>(Target->SizeX) / Target->SizeY : 0.f));
			Check(State, AspectError < 0.01f, TEXT("projection aspect = the render target's (square pixels)"));
			Camera->RotateStep(+1); // the player turns the camera: the map does not follow
			Check(State, Minimap->GetPanelMode() == EMinimapPanelMode::Messages, TEXT("the panel shows the messages by default"));
			Tap(PC, EKeys::F2);
			Next(State);
			return true;
		}
		case 1:
		{
			if (State.StageTime < 0.2f)
			{
				return true;
			}
			const UMissionSessionSubsystem* Session = World->GetGameInstance()->GetSubsystem<UMissionSessionSubsystem>();
			Check(State, Minimap->GetPanelMode() == EMinimapPanelMode::Map, TEXT("F2 (the real key): the panel shows the map"));
			Check(State, Session && Session->MinimapPanelMode == 1, TEXT("... remembered for the session"));
			const FMinimapView& View = Minimap->GetPanelView();
			Check(State, FMath::IsNearlyEqual(Minimap->GetHeaderRect().GetSize().X, View.ScreenSize.X, 0.5f)
				&& View.ScreenOrigin.X + View.ScreenSize.X <= Minimap->GetViewportSize().X - 15.f && View.ScreenOrigin.Y < 60.f,
				FString::Printf(TEXT("a rectangle in the top-right corner (%.0f, %.0f, %.0f x %.0f)"), View.ScreenOrigin.X, View.ScreenOrigin.Y, View.ScreenSize.X, View.ScreenSize.Y));
			Check(State, FMath::Abs(FMath::FindDeltaAngleDegrees(Camera->GetCurrentYaw(), Camera->Config.BaseYaw)) > 1.f
				&& FMath::IsNearlyEqual(Minimap->GetProjection().YawDeg, Camera->Config.BaseYaw, 0.01f),
				FString::Printf(TEXT("the player turned the camera (yaw %.1f): the map keeps the start angle"), Camera->GetCurrentYaw()));
			Camera->RotateStep(-1);
			// The header: a click on it switches back, and again.
			const FVector2D Header = Minimap->GetHeaderRect().GetCenter();
			Check(State, PC->HandleMapClick(Header, false) && Minimap->GetPanelMode() == EMinimapPanelMode::Messages, TEXT("a click on the header: messages"));
			Check(State, Minimap->GetHeaderRect().GetSize().X > View.ScreenSize.X, TEXT("... the header spans the (wider) feed then"));
			Check(State, PC->HandleMapClick(Minimap->GetHeaderRect().GetCenter(), false) && Minimap->GetPanelMode() == EMinimapPanelMode::Map, TEXT("... and the map again"));
			Next(State);
			if (State.bShot)
			{
				Next(State, 80);
			}
			return true;
		}
		case 80: // Shot: the corner map on the HUD, then the full map
			if (State.StageTime < 6.f)
			{
				return true;
			}
			Screenshot(State, TEXT("minimap_hud"));
			if (UTextureRenderTarget2D* Target = Minimap->GetCapture(0))
			{
				// The raw floor capture (what the map image is cut from), for the look review.
				UKismetRenderingLibrary::ExportRenderTarget(World, Target, FPaths::ProjectSavedDir() / TEXT("Screenshots"),
					FString::Printf(TEXT("minimap_capture_%s.png"), *State.MapName));
			}
			Next(State, 83);
			return true;
		case 83: // the screenshot is taken at the end of a later frame: the full map opens only after it
			if (State.StageTime < 1.f)
			{
				return true;
			}
			PC->FullMapKey();
			Next(State, 81);
			return true;
		case 81:
			if (State.StageTime < 2.f)
			{
				return true;
			}
			Screenshot(State, TEXT("minimap_full"));
			Next(State, 82);
			return true;
		case 82:
			if (State.StageTime < 1.f)
			{
				return true;
			}
			PC->FullMapKey();
			Next(State, 2);
			return true;
		case 2:
		{
			TArray<FMinimapIcon> Icons;
			Minimap->BuildIcons(Icons);
			const FMinimapIsoProjection& Projection = Minimap->GetProjection();
			int32 Matched = 0;
			int32 Alive = 0;
			bool bLeaderHighlighted = false;
			bool bFacing = true;
			for (const AOperativeCharacter* Member : Squad->GetMembers())
			{
				++Alive;
				const FMinimapIcon* Icon = Icons.FindByPredicate([Member](const FMinimapIcon& Candidate) { return Candidate.Kind == EMinimapIconKind::Operative && Candidate.Actor.Get() == Member; });
				if (Icon && Icon->MapUV.Equals(Projection.WorldToMap(Member->GetActorLocation()), 1e-4f))
				{
					++Matched;
					bFacing &= !Icon->MapFacing.IsNearlyZero();
					bLeaderHighlighted |= Member == Leader && Icon->bHighlight;
				}
			}
			Check(State, Alive > 0 && Matched == Alive, FString::Printf(TEXT("an icon per operative at his projected spot (%d / %d)"), Matched, Alive));
			Check(State, bLeaderHighlighted, TEXT("the selected operative highlighted"));
			Check(State, bFacing, TEXT("facing arrows"));
			const FMinimapView& View = Minimap->GetPanelView();
			const FVector2D LeaderScreen = View.MapToScreen(Projection.WorldToMap(Leader->GetActorLocation()));
			Check(State, View.Contains(LeaderScreen) && FVector2D::Distance(LeaderScreen, View.ScreenOrigin + View.ScreenSize * 0.5f) < 3.f,
				TEXT("the corner map is centred on the selected operative"));
			Check(State, Minimap->GetCameraFootprint().Num() == 4, TEXT("the camera's view footprint (4 corners)"));
			// Fog: revealed round the squad, dark far away.
			State.FogBefore = Minimap->GetFog().GetRevealedCount(0);
			State.F = Leader->GetActorForwardVector().GetSafeNormal2D();
			State.R = FVector::CrossProduct(FVector::UpVector, State.F);
			State.GroundZ = Feet(Leader).Z;
			Check(State, State.FogBefore > 0 && Minimap->GetFog().IsRevealed(0, FVector2D(Leader->GetActorLocation())), FString::Printf(TEXT("fog revealed round the squad (%d cells)"), State.FogBefore));
			Check(State, !Minimap->GetFog().IsRevealed(0, FVector2D(Leader->GetActorLocation() + State.F * 4000.f)), TEXT("dark 40 m ahead (unexplored)"));
			// LMB 15 m ahead on the corner map: the camera goes there.
			State.Target = SmokeUtils::ClearPoint(World, Leader->GetActorLocation(), Leader->GetActorLocation() + State.F * 1500.f);
			State.Target.Z = State.GroundZ; // the ground spot (ClearPoint keeps the capsule centre's height)
			const FVector2D Screen = View.MapToScreen(Projection.WorldToMap(State.Target));
			Check(State, View.Contains(Screen), TEXT("15 m ahead lies on the corner map"));
			Check(State, PC->HandleMapClick(Screen, false), TEXT("LMB on the map is the map's (not a world click)"));
			// The spot is the first solid surface seen at that map pixel (the inverse of the projection along the view ray): it
			// projects back onto the clicked pixel; here an obstacle may stand in front of the ground point.
			const FVector2D Clicked = View.ScreenToMap(Screen);
			Check(State, Camera->IsLookingAtPoint() && Projection.WorldToMap(Camera->GetLookAtPoint()).Equals(Clicked, 1e-4f),
				FString::Printf(TEXT("... the camera is sent to the clicked map pixel (%s, %.0f cm from the ground spot)"),
					*Camera->GetLookAtPoint().ToCompactString(), FVector::Dist2D(Camera->GetLookAtPoint(), State.Target)));
			// The wheel over the map zooms the map, not the camera.
			const float SpanBefore = Minimap->GetPanelView().SpanU;
			const float DistanceBefore = Camera->GetTargetDistance();
			Check(State, Minimap->HandleWheel(Screen, -1.f) && Minimap->GetPanelView().SpanU < SpanBefore * 0.85f, TEXT("wheel over the map: the map zooms in"));
			Check(State, FMath::IsNearlyEqual(Camera->GetTargetDistance(), DistanceBefore), TEXT("... the camera's zoom unchanged"));
			Minimap->HandleWheel(Screen, +1.f);
			Check(State, !Minimap->HandleWheel(FVector2D(10.f, 600.f), -1.f), TEXT("wheel elsewhere: not the map's"));
			Next(State);
			return true;
		}
		case 3:
			if (State.StageTime < 2.5f)
			{
				return true;
			}
			{
				const bool bStays = Camera->IsLookingAtPoint() && FVector::Dist2D(Camera->GetViewCentre(), Camera->GetLookAtPoint()) < 120.f;
				const FString What = FString::Printf(TEXT("the view is centred on the map spot and stays (%.0f cm)"), FVector::Dist2D(Camera->GetViewCentre(), Camera->GetLookAtPoint()));
				if (State.bShot)
				{
					// A rendered run: the offscreen cursor parked at a screen edge scrolls the view (and a tutorial's camera hints move it).
					UE_LOG(LogCodexTactics, Display, TEXT("Smoke note: %s - %s"), *What, bStays ? TEXT("yes") : TEXT("moved on by the level"));
				}
				else
				{
					Check(State, bStays, What);
				}
			}
			{
				// RMB 8 m to the side: a move order there.
				State.Target = SmokeUtils::ClearPoint(World, Leader->GetActorLocation(), Leader->GetActorLocation() + State.R * 800.f);
				State.Target.Z = Feet(Leader).Z;
				const FMinimapView& View = Minimap->GetPanelView();
				const FVector2D Screen = View.MapToScreen(Minimap->GetProjection().WorldToMap(State.Target));
				Squad->SetSelectedGroup(TArray<AOperativeCharacter*>{ Leader }, false);
				Check(State, PC->HandleMapClick(Screen, true), TEXT("RMB on the map: a move order"));
			}
			Next(State);
			return true;
		case 4:
			if (FVector::Dist2D(Leader->GetActorLocation(), State.Target) > 150.f && State.StageTime < 20.f && !State.bShot)
			{
				return true;
			}
			if (State.bShot && State.StageTime < 0.5f)
			{
				Leader->SetActorLocation(State.Target + FVector(0.f, 0.f, Leader->GetSimpleCollisionHalfHeight() + 5.f));
				return true;
			}
			if (State.StageTime < 0.6f)
			{
				return true; // a fog update
			}
			Check(State, FVector::Dist2D(Leader->GetActorLocation(), State.Target) <= 150.f, FString::Printf(TEXT("the leader walked to the map spot (%.0f cm off)"),
				FVector::Dist2D(Leader->GetActorLocation(), State.Target)));
			Check(State, Minimap->GetFog().GetRevealedCount(0) > State.FogBefore, FString::Printf(TEXT("the fog grew as he walked (%d -> %d)"), State.FogBefore,
				Minimap->GetFog().GetRevealedCount(0)));
			Tap(PC, EKeys::Tab);
			Next(State);
			return true;
		case 5:
		{
			if (State.StageTime < 0.2f)
			{
				return true;
			}
			Check(State, Minimap->IsFullMapOpen(), TEXT("Tab (the real key): the full map opens"));
			const FMinimapView& Full = Minimap->GetFullView();
			Check(State, Full.ScreenSize.X > Minimap->GetViewportSize().X * 0.8f, TEXT("... nearly the whole screen"));
			Check(State, Full.Contains(Full.MapToScreen(FVector2D(0.02f, 0.02f))) && Full.Contains(Full.MapToScreen(FVector2D(0.98f, 0.98f))), TEXT("... the whole level fits"));
			Check(State, PC->HandleMapClick(Full.MapToScreen(Minimap->GetProjection().WorldToMap(Leader->GetActorLocation())), false) && Camera->IsLookingAtPoint(),
				TEXT("... LMB on it moves the camera"));
			Tap(PC, EKeys::Tab);
			Next(State);
			return true;
		}
		case 6:
			if (State.StageTime < 0.2f)
			{
				return true;
			}
			Check(State, !Minimap->IsFullMapOpen(), TEXT("Tab again: closed"));
			Tap(PC, EKeys::Tab);
			Next(State);
			return true;
		case 7:
			if (State.StageTime < 0.2f)
			{
				return true;
			}
			Check(State, Minimap->IsFullMapOpen(), TEXT("open again"));
			Tap(PC, EKeys::Escape);
			Next(State);
			return true;
		case 8:
		{
			if (State.StageTime < 0.2f)
			{
				return true;
			}
			const ACodexTacticsHUD* Hud = PC->GetHUD<ACodexTacticsHUD>();
			Check(State, !Minimap->IsFullMapOpen() && !(Hud && Hud->IsPauseMenuOpen()), TEXT("Esc closes the full map (the pause menu stays shut)"));
			if (State.bShot && !State.bMovementTest)
			{
				// The runtime two-storey box needs flat ground and a runtime navmesh under it (L_MovementTest); elsewhere the
				// stills of the corner map and the full map are the result.
				return Finish(State, true);
			}
			// A two-storey box 9 m to the side: the ramp climbs along F from the ground to the slab's near edge (4 m up).
			const FVector Base = SmokeUtils::ClearPoint(World, Leader->GetActorLocation(), Leader->GetActorLocation() - State.R * 900.f);
			const FVector Ground(Base.X, Base.Y, Feet(Leader).Z);
			const FRotator Yaw = State.F.Rotation();
			const float Rise = 400.f;
			const float Run = 1000.f;
			const float Pitch = FMath::RadiansToDegrees(FMath::Atan2(Rise, Run));
			const FVector Edge = Ground + State.F * 600.f; // the ramp's top end = the slab's near edge
			const FVector RampNormal = (-State.F * FMath::Sin(FMath::DegreesToRadians(Pitch)) + FVector::UpVector * FMath::Cos(FMath::DegreesToRadians(Pitch)));
			const FVector RampTopMid = Edge - State.F * (Run * 0.5f) + FVector(0.f, 0.f, Rise * 0.5f);
			State.Spawned.Add(SpawnBlock(World, RampTopMid - RampNormal * 10.f, FVector(FMath::Sqrt(Run * Run + Rise * Rise) + 40.f, 220.f, 20.f), FRotator(Pitch, Yaw.Yaw, 0.f)));
			const FVector SlabCentre = Edge + State.F * 400.f;
			AStaticMeshActor* Slab = SpawnBlock(World, SlabCentre + FVector(0.f, 0.f, Rise - 10.f), FVector(800.f, 800.f, 20.f), Yaw);
			// Upper floor wall (on the slab, far edge), the roof 3 m over the upper floor, a ground-floor wall under the slab.
			AStaticMeshActor* UpperWall = SpawnBlock(World, SlabCentre + State.F * 390.f + FVector(0.f, 0.f, Rise + 125.f), FVector(20.f, 800.f, 250.f), Yaw);
			AStaticMeshActor* Roof = SpawnBlock(World, SlabCentre + FVector(0.f, 0.f, Rise + 300.f), FVector(820.f, 820.f, 20.f), Yaw);
			AStaticMeshActor* GroundWall = SpawnBlock(World, SlabCentre + State.F * 390.f + FVector(0.f, 0.f, 125.f), FVector(20.f, 800.f, 250.f), Yaw);
			State.Spawned.Append({ Slab, UpperWall, Roof, GroundWall });
			State.Slab = Slab;
			State.UpperWall = UpperWall;
			State.Roof = Roof;
			State.GroundWall = GroundWall;
			State.SlabTop = SlabCentre + FVector(0.f, 0.f, Rise);
			State.Down = Ground + State.R * 400.f; // beside the ramp, outside the slab's footprint
			Check(State, Slab && UpperWall && Roof && GroundWall && State.Spawned[0].IsValid(), TEXT("a two-storey box with a ramp spawned at runtime"));
			Next(State);
			return true;
		}
		case 9:
		{
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			bool bPath = false;
			if (Nav && !Nav->IsNavigationBuildInProgress() && State.StageTime > 1.f)
			{
				FNavLocation OnNav;
				if (Nav->ProjectPointToNavigation(State.SlabTop + FVector(-State.F * 100.f), OnNav, FVector(60.f, 60.f, 100.f)))
				{
					const UNavigationPath* Path = Nav->FindPathToLocationSynchronously(World, Leader->GetActorLocation(), OnNav.Location);
					bPath = Path && Path->IsValid() && !Path->IsPartial();
				}
			}
			if (!bPath && State.StageTime < 40.f)
			{
				return true;
			}
			Check(State, bPath, TEXT("navmesh rebuilt: a path up the ramp onto the slab"));
			GEngine->Exec(World, TEXT("CodexTactics.MinimapRecapture")); // the debug re-capture (level changed)
			const TArray<float>& Bases = Minimap->GetUpperFloorBases();
			Check(State, Minimap->GetFloorCount() == 2 && !Minimap->AreFloorsFromData() && FMath::IsNearlyEqual(Bases.IsEmpty() ? 0.f : Bases[0], State.SlabTop.Z, 15.f),
				FString::Printf(TEXT("re-captured: 2 floors, the upper one at the slab (%s vs %.0f) - the unreachable roof is no floor"),
					*FString::JoinBy(Bases, TEXT(", "), [](float Z) { return FString::Printf(TEXT("%.0f"), Z); }), State.SlabTop.Z));
			Check(State, Minimap->GetCapture(1) != nullptr, TEXT("a capture for the 2nd floor"));
			const auto Mesh = [](const TWeakObjectPtr<AStaticMeshActor>& Actor) { return Actor.IsValid() ? Actor->GetStaticMeshComponent() : nullptr; };
			Check(State, Minimap->IsHiddenOnFloor(0, Mesh(State.UpperWall)) && Minimap->IsHiddenOnFloor(0, Mesh(State.Roof)) && Minimap->IsHiddenOnFloor(0, Mesh(State.Slab)),
				TEXT("ground capture: the upper floor (slab, walls) and the roof cut away"));
			Check(State, !Minimap->IsHiddenOnFloor(0, Mesh(State.GroundWall)), TEXT("ground capture: the ground-floor wall kept"));
			Check(State, Minimap->IsHiddenOnFloor(1, Mesh(State.Roof)), TEXT("2nd floor capture: the roof cut away"));
			Check(State, !Minimap->IsHiddenOnFloor(1, Mesh(State.UpperWall)) && !Minimap->IsHiddenOnFloor(1, Mesh(State.Slab)), TEXT("2nd floor capture: its slab and walls kept"));
			SendLeader(World, State, Squad, State.SlabTop - State.F * 150.f);
			Next(State);
			return true;
		}
		case 10:
			if (!(Minimap->GetOperativeFloor(Leader) == 1 && Feet(Leader).Z > State.SlabTop.Z - 40.f && FVector::Dist2D(Leader->GetActorLocation(), State.SlabTop - State.F * 150.f) < 200.f)
				&& State.StageTime < 40.f)
			{
				return true;
			}
			if (State.StageTime < 0.6f)
			{
				return true;
			}
			Check(State, Minimap->GetOperativeFloor(Leader) == 1 && Minimap->GetDisplayedFloor() == 1,
				FString::Printf(TEXT("up the ramp: the leader on the 2nd floor, the map shows it (feet %.0f, floor %d)"), Feet(Leader).Z, Minimap->GetDisplayedFloor()));
			Check(State, Minimap->GetFog().GetRevealedCount(1) > 0, FString::Printf(TEXT("the 2nd floor's fog reveals round him (%d cells)"), Minimap->GetFog().GetRevealedCount(1)));
			State.Saved = Minimap->CaptureSave();
			State.SavedFog0 = Minimap->GetFog().GetRevealedCount(0);
			if (State.bShot)
			{
				Next(State, 90);
				return true;
			}
			SendLeader(World, State, Squad, State.Down);
			Next(State, 11);
			return true;
		case 90: // Shot: the 2nd floor on the corner map and the full map
			if (State.StageTime < 4.f)
			{
				return true;
			}
			Screenshot(State, TEXT("minimap_floor2"));
			Next(State, 93);
			return true;
		case 93:
			if (State.StageTime < 1.f)
			{
				return true;
			}
			PC->FullMapKey();
			Next(State, 91);
			return true;
		case 91:
			if (State.StageTime < 2.f)
			{
				return true;
			}
			Screenshot(State, TEXT("minimap_floor2_full"));
			Next(State, 92);
			return true;
		case 92:
			if (State.StageTime < 1.f)
			{
				return true;
			}
			PC->FullMapKey();
			SendLeader(World, State, Squad, State.Down);
			Next(State, 11);
			return true;
		case 11:
		{
			if (!(Minimap->GetOperativeFloor(Leader) == 0 && FVector::Dist2D(Leader->GetActorLocation(), State.Down) < 200.f) && State.StageTime < 40.f)
			{
				return true;
			}
			if (State.StageTime < 0.6f)
			{
				return true;
			}
			Check(State, Minimap->GetDisplayedFloor() == 0, TEXT("back down: the map shows the ground again"));
			// Save state: the minimap's JSON brings the fog and the panel back.
			Minimap->SetPanelMode(EMinimapPanelMode::Messages);
			const int32 Grown = Minimap->GetFog().GetRevealedCount(0);
			if (State.Saved.IsValid())
			{
				Minimap->RestoreSave(*State.Saved);
			}
			Check(State, Minimap->GetPanelMode() == EMinimapPanelMode::Map, TEXT("save: the panel choice restored"));
			Check(State, Minimap->GetFog().GetRevealedCount(0) == State.SavedFog0 && State.SavedFog0 <= Grown,
				FString::Printf(TEXT("save: the fog restored to the saved state (%d, now %d before the load)"), State.SavedFog0, Grown));
			if (USaveGameSubsystem* Saves = World->GetSubsystem<USaveGameSubsystem>())
			{
				const TSharedRef<FJsonObject> Data = Saves->CaptureWorldState();
				const TSharedPtr<FJsonObject>* WorldState = nullptr;
				const bool bInSave = Data->HasField(TEXT("minimap"))
					|| (Data->TryGetObjectField(TEXT("world_state"), WorldState) && WorldState && (*WorldState)->HasField(TEXT("minimap")));
				Check(State, bInSave, TEXT("the world save carries the minimap state"));
			}
			UE_LOG(LogCodexTactics, Display, TEXT("Minimap perf: setup %.1f ms, update %.1f us (every %.2f s), icons %.1f us, draw %.1f us, captures %.2f / %.2f ms, hidden %d / %d"),
				Minimap->GetSetupMs(), Minimap->GetAverageTickUs(), UMinimapSubsystem::UpdateInterval, Minimap->GetAverageIconUs(), Minimap->GetAverageDrawUs(),
				Minimap->GetCaptureInfo(0) ? Minimap->GetCaptureInfo(0)->CaptureMs : 0.0, Minimap->GetCaptureInfo(1) ? Minimap->GetCaptureInfo(1)->CaptureMs : 0.0,
				Minimap->GetHiddenPieceCount(0), Minimap->GetHiddenPieceCount(1));
			for (const TWeakObjectPtr<AActor>& Actor : State.Spawned)
			{
				if (Actor.IsValid())
				{
					Actor->Destroy();
				}
			}
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
		State->bShot = Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("Shot"), ESearchCase::IgnoreCase); });
		State->bMovementTest = World->GetMapName().Contains(TEXT("L_MovementTest"));
		State->MapName = UWorld::RemovePIEPrefix(World->GetMapName());
		if (State->bMovementTest)
		{
			FTimerHandle PlaceHandle;
			TWeakObjectPtr<UWorld> PlaceWorld(World);
			World->GetTimerManager().SetTimer(PlaceHandle, FTimerDelegate::CreateLambda([PlaceWorld]()
			{
				SmokeUtils::PlaceSquadAtTestStart(PlaceWorld.Get());
			}), 0.5f, false);
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	void Recapture(const TArray<FString>& Args, UWorld* World)
	{
		if (UMinimapSubsystem* Minimap = World ? World->GetSubsystem<UMinimapSubsystem>() : nullptr)
		{
			Minimap->Rebuild();
		}
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.MinimapSmoke"),
		TEXT("Dev check (2026-10-18): minimap capture per floor, F2 panel switch, icons at their projected spots, LMB camera / RMB move on the map, fog, Tab full map, floor switch on a ramp, save state. 'Shot' saves minimap_*.png."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));

	static FAutoConsoleCommandWithWorldAndArgs RecaptureCommand(
		TEXT("CodexTactics.MinimapRecapture"),
		TEXT("Dev: redetect the minimap floors and re-capture every floor (after the level geometry changed)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Recapture));
}

#endif // !UE_BUILD_SHIPPING
