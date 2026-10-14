// Dev-only check of the building exit aids (rev 5g item 5, user request 2026-10-14) on the Demo 01 review map (the hut with its
// fixed interior camera, camera zone Trigger_BoatsCabin):
//   Scripts/smoke_parallel.ps1 -Commands InteriorExitSmoke   (map from Scripts/test_map.json)
// 1. all three teleported into the hut: the interior camera is the view, EXIT arrow(s) found at the doorway (the north door of
//    the review hut), hover lights one up;
// 2. a click on the arrow (group mode): the whole squad walks out, the zone switches off, the arrows go, the view is back on the
//    tactical camera;
// 3. back inside: M (a real key event) = the outside view (no arrows, normal camera), M again = the interior camera + arrows;
// 4. Solo: a click on the arrow sends only the selected operative out, the others stay inside.
// "Shot" (rendered: UnrealEditor.exe <map> -game -RenderOffscreen -ExecCmds="CodexTactics.InteriorExitSmoke Shot") saves
// Saved/Screenshots/interior_exit_arrows.png in stage 1 and exits.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/CameraActor.h"
#include "Camera/CameraZoneVolume.h"
#include "Camera/InteriorExitArrowActor.h"
#include "Camera/InteriorExitSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "InputKeyEventArgs.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "UnrealClient.h"

namespace InteriorExitSmoke
{
	/**
	 * The review hut's doorway relative to its camera zone's centre (DEMO01_AUTOLAYOUT §2a: the opening X -2518..-2317 on the north
	 * wall, out = +Y; the whole hut moved south since, the zone moved with it).
	 */
	const FVector2D ReviewHutDoorFromZone(-237.f, 342.f);

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		bool bShot = false;
		TWeakObjectPtr<ACameraZoneVolume> Zone;
		TArray<FVector> SoloStarts;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("InteriorExitSmoke"));
		return false;
	}

	void Next(FState& State)
	{
		++State.Stage;
		State.StageTime = 0.f;
	}

	ACameraZoneVolume* FindHutZone(UWorld* World)
	{
		ACameraZoneVolume* Fallback = nullptr;
		for (TActorIterator<ACameraZoneVolume> It(World); It; ++It)
		{
			if (!It->TargetCamera || !It->bEnableCameraSwitch)
			{
				continue;
			}
			if (It->GetActorNameOrLabel().Contains(TEXT("BoatsCabin")))
			{
				return *It;
			}
			Fallback = Fallback ? Fallback : *It;
		}
		return Fallback;
	}

	void PlaceInside(UWorld* World, const ACameraZoneVolume& Zone, const TArray<AOperativeCharacter*>& Members)
	{
		const FTransform ZoneTransform = Zone.GetZoneTransform();
		const FVector Offsets[] = { FVector(0.f, 0.f, 0.f), FVector(-150.f, -80.f, 0.f), FVector(150.f, -80.f, 0.f) };
		for (int32 Index = 0; Index < Members.Num(); ++Index)
		{
			AOperativeCharacter* Member = Members[Index];
			Member->StopOperative();
			// At the operatives' own height: from the zone centre's height the navmesh projection could prefer the hut roof.
			FVector Desired = ZoneTransform.TransformPosition(Offsets[FMath::Min(Index, 2)]);
			Desired.Z = Member->GetActorLocation().Z;
			// On the navmesh, no overlap test (SmokeUtils::FreeSpot refuses the hut: its tutorial trigger volume overlaps pawns).
			FVector Spot = Desired;
			FNavLocation OnNav;
			if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
				Nav && Nav->ProjectPointToNavigation(Desired, OnNav, FVector(80.f, 80.f, 200.f)))
			{
				Spot = OnNav.Location + FVector(0.f, 0.f, Member->GetSimpleCollisionHalfHeight() + 2.f);
			}
			const bool bMoved = Member->TeleportTo(Spot, Member->GetActorRotation(), false, true);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke place %s at (%.0f, %.0f, %.0f) teleport %d -> (%.0f, %.0f, %.0f); zone (%.0f, %.0f, %.0f) half (%.0f, %.0f, %.0f)"),
				*Member->DisplayName.ToString(), Spot.X, Spot.Y, Spot.Z, bMoved ? 1 : 0, Member->GetActorLocation().X, Member->GetActorLocation().Y,
				Member->GetActorLocation().Z, ZoneTransform.GetLocation().X, ZoneTransform.GetLocation().Y, ZoneTransform.GetLocation().Z,
				Zone.GetZoneHalfExtent().X, Zone.GetZoneHalfExtent().Y, Zone.GetZoneHalfExtent().Z);
		}
	}

	int32 CountInside(const ACameraZoneVolume& Zone, const TArray<AOperativeCharacter*>& Members)
	{
		int32 Inside = 0;
		for (const AOperativeCharacter* Member : Members)
		{
			Inside += Zone.ContainsLocation(Member->GetActorLocation()) ? 1 : 0;
		}
		return Inside;
	}

	void PressKey(APlayerController* PC, const FKey& Key)
	{
		PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Pressed, 1.f));
		PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Released, 0.f));
	}

	bool ClickArrow(ACodexTacticsPlayerController* PC, AInteriorExitArrowActor* Arrow)
	{
		if (!PC || !Arrow)
		{
			return false;
		}
		const FHitResult Hit(Arrow, Cast<UPrimitiveComponent>(Arrow->GetRootComponent()), Arrow->GetActorLocation(), FVector::UpVector);
		PC->HandleWorldHit(Hit);
		return true;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State, float DeltaTime)
	{
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		State.Time += DeltaTime;
		State.StageTime += DeltaTime;
		if (State.Time > 150.f && State.Stage < 99)
		{
			Check(State, false, FString::Printf(TEXT("timeout in stage %d"), State.Stage));
			return Finish(State, false);
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UInteriorExitSubsystem* Exits = World->GetSubsystem<UInteriorExitSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		const TArray<AOperativeCharacter*> Members = Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>();
		ACameraZoneVolume* Zone = State.Zone.Get();
		for (AOperativeCharacter* Member : Members)
		{
			Member->ColdLevel = 0.f;
		}
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 3.f)
			{
				return true;
			}
			// The tutorial would lock / talk: off for this check (camera and exit commands are never locked anyway).
			if (IConsoleVariable* Tutorial = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Tutorial.Enabled")))
			{
				Tutorial->Set(0, ECVF_SetByCode);
			}
			State.Zone = FindHutZone(World);
			if (!State.Zone.IsValid() || Members.Num() < 3 || !Exits || !PC)
			{
				Check(State, false, TEXT("a camera zone with a fixed camera (Trigger_BoatsCabin), three operatives, the exit subsystem"));
				return Finish(State, false);
			}
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke zone %s"), *State.Zone->GetActorNameOrLabel());
			for (AOperativeCharacter* Member : Members)
			{
				Member->HealthComponent->SetMaxHealth(100000.f);
			}
			Squad->SetLeader(Members[0]);
			PlaceInside(World, *State.Zone, Members);
			Next(State);
			return true;
		}
		case 1:
		{
			if ((!Zone->IsZoneCameraShown() || Exits->GetArrows().IsEmpty()) && State.StageTime < 4.f)
			{
				return true;
			}
			Check(State, CountInside(*Zone, Members) == 3 && Zone->IsZoneActive() && Zone->IsZoneCameraShown()
				&& PC->GetViewTarget() == Zone->TargetCamera, FString::Printf(TEXT("all three inside (%d): the interior camera is the view"), CountInside(*Zone, Members)));
			const TArray<AInteriorExitArrowActor*> Arrows = Exits->GetArrows();
			Check(State, !Arrows.IsEmpty(), FString::Printf(TEXT("EXIT arrows at the doorways: %d"), Arrows.Num()));
			bool bAtDoor = false;
			for (const AInteriorExitArrowActor* Arrow : Arrows)
			{
				const float ToDoor = FVector2D::Distance(FVector2D(Arrow->GetActorLocation()), FVector2D(Zone->GetActorLocation()) + ReviewHutDoorFromZone);
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke arrow at (%.0f, %.0f), out (%.2f, %.2f), %.0f cm from the review hut door"),
					Arrow->GetActorLocation().X, Arrow->GetActorLocation().Y, Arrow->GetOutward().X, Arrow->GetOutward().Y, ToDoor);
				bAtDoor |= ToDoor < 250.f && Arrow->GetOutward().Y > 0.7f && !Zone->ContainsLocation(Arrow->GetExitTarget());
			}
			Check(State, bAtDoor, TEXT("an arrow at the hut's north door pointing out, its exit target outside the zone"));
			if (!Arrows.IsEmpty())
			{
				Exits->SetHoveredArrow(Arrows[0]);
				Check(State, Arrows[0]->IsHovered(), TEXT("hover lights the arrow up"));
				Exits->SetHoveredArrow(nullptr);
			}
			if (State.bShot)
			{
				// Shot only: whatever stands between the interior camera and an arrow (a roof slab the review map keeps visible) is
				// hidden for the picture - the map itself is not changed.
				const FVector CameraAt = Zone->TargetCamera->GetActorLocation();
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke interior camera at (%.0f, %.0f, %.0f) rot (%.0f, %.0f)"), CameraAt.X, CameraAt.Y, CameraAt.Z,
					Zone->TargetCamera->GetActorRotation().Pitch, Zone->TargetCamera->GetActorRotation().Yaw);
				for (const AInteriorExitArrowActor* Arrow : Arrows)
				{
					FCollisionQueryParams Params(SCENE_QUERY_STAT(InteriorExitShot), false, Arrow);
					TArray<FHitResult> Hits;
					World->LineTraceMultiByChannel(Hits, CameraAt, Arrow->GetActorLocation(), ECC_Camera, Params);
					for (const FHitResult& Hit : Hits)
					{
						if (AActor* Blocker = Hit.GetActor(); Blocker && !Cast<APawn>(Blocker))
						{
							UE_LOG(LogCodexTactics, Display, TEXT("Smoke shot: %s hides the arrow from the interior camera (hidden for the picture)"), *Blocker->GetActorNameOrLabel());
							Blocker->SetActorHiddenInGame(true);
						}
					}
				}
				State.Stage = 99;
				State.StageTime = 0.f;
				return true;
			}
			Next(State);
			return true;
		}
		case 2:
		{
			// Group mode: a click on the arrow sends the whole squad out.
			AInteriorExitArrowActor* Arrow = Exits->GetArrows().IsEmpty() ? nullptr : Exits->GetArrows()[0];
			Check(State, ClickArrow(PC, Arrow), TEXT("group mode: click on the EXIT arrow"));
			int32 Ordered = 0;
			for (const AOperativeCharacter* Member : Members)
			{
				Ordered += Member->HasOwnMoveOrder() ? 1 : 0;
			}
			Check(State, Ordered == 3, FString::Printf(TEXT("group mode: the whole squad got the exit order (%d)"), Ordered));
			Next(State);
			return true;
		}
		case 3:
		{
			if ((CountInside(*Zone, Members) > 0 || Zone->IsZoneActive()) && State.StageTime < 15.f)
			{
				return true;
			}
			Check(State, CountInside(*Zone, Members) == 0, TEXT("all three walked out of the hut"));
			Check(State, !Zone->IsZoneActive() && !Zone->IsZoneCameraShown() && Exits->GetArrows().IsEmpty() && PC->GetViewTarget() == PC->GetPawn(),
				TEXT("outside: the zone is off, the arrows are gone, the tactical camera is back"));
			PlaceInside(World, *Zone, Members);
			Next(State);
			return true;
		}
		case 4:
		{
			if ((!Zone->IsZoneCameraShown() || Exits->GetArrows().IsEmpty()) && State.StageTime < 4.f)
			{
				return true;
			}
			Check(State, Zone->IsZoneCameraShown(), TEXT("back inside: the interior camera again"));
			PressKey(PC, EKeys::M);
			Next(State);
			return true;
		}
		case 5:
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			Check(State, Zone->IsPlayerOutsideView() && !Zone->IsZoneCameraShown() && PC->GetViewTarget() == PC->GetPawn() && Exits->GetArrows().IsEmpty(),
				TEXT("M: the outside view (tactical camera, no arrows) while still inside"));
			PressKey(PC, EKeys::M);
			Next(State);
			return true;
		}
		case 6:
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			Check(State, !Zone->IsPlayerOutsideView() && Zone->IsZoneCameraShown() && PC->GetViewTarget() == Zone->TargetCamera && !Exits->GetArrows().IsEmpty(),
				TEXT("M again: the interior camera and the arrows are back"));
			// Solo: only the selected operative goes out.
			Squad->EnterSoloMode();
			State.SoloStarts.Reset();
			for (const AOperativeCharacter* Member : Members)
			{
				State.SoloStarts.Add(Member->GetActorLocation());
			}
			Next(State);
			return true;
		}
		case 7:
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			AInteriorExitArrowActor* Arrow = Exits->GetArrows().IsEmpty() ? nullptr : Exits->GetArrows()[0];
			Check(State, ClickArrow(PC, Arrow), TEXT("Solo: click on the EXIT arrow"));
			Check(State, Members[0]->HasOwnMoveOrder() && !Members[1]->HasOwnMoveOrder() && !Members[2]->HasOwnMoveOrder(),
				TEXT("Solo: only the selected operative got the exit order"));
			Next(State);
			return true;
		}
		case 8:
		{
			if (Zone->ContainsLocation(Members[0]->GetActorLocation()) && State.StageTime < 12.f)
			{
				return true;
			}
			if (State.StageTime < 2.f)
			{
				return true;
			}
			const float Moved1 = FVector::Dist2D(Members[1]->GetActorLocation(), State.SoloStarts[1]);
			const float Moved2 = FVector::Dist2D(Members[2]->GetActorLocation(), State.SoloStarts[2]);
			Check(State, !Zone->ContainsLocation(Members[0]->GetActorLocation()), TEXT("Solo: the selected operative walked out"));
			Check(State, Zone->ContainsLocation(Members[1]->GetActorLocation()) && Zone->ContainsLocation(Members[2]->GetActorLocation())
				&& Moved1 < 60.f && Moved2 < 60.f, FString::Printf(TEXT("Solo: the other two stay inside (moved %.0f / %.0f cm)"), Moved1, Moved2));
			Squad->ExitSoloMode(false, 0.f);
			return Finish(State, true);
		}
		case 99:
		{
			// Shot mode: shaders / textures settle first.
			if (State.StageTime < 10.f)
			{
				return true;
			}
			const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("interior_exit_arrows.png");
			FScreenshotRequest::RequestScreenshot(Path, true, false);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke screenshot %s"), *Path);
			Next(State);
			return true;
		}
		case 100:
			// Give the screenshot a few frames, then leave.
			return State.StageTime < 2.f ? true : Finish(State, true);
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
		State->bShot = Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("Shot"), ESearchCase::IgnoreCase); });
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float DeltaTime)
		{
			return Step(WeakWorld, *State, DeltaTime);
		}), 0.f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.InteriorExitSmoke"),
		TEXT("Dev check (Demo 01 review map): EXIT arrows at the hut door with the interior camera, click = walk out (group / Solo), M = outside view; 'Shot' saves interior_exit_arrows.png."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
