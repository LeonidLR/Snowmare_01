// Dev-only RENDERED pictures of a generated map's fixed ladders and cut entry (the game designer's bug report 2026-11-02 on L_Gen_Depot_1:
// "ladders sunk into the walls, the cut cannot be entered"; docs/design/levelgen/LEVEL_GENERATOR.md 7 / 8, smoke LevelGenLaddersSmoke):
//   UnrealEditor.exe <uproject> /Game/Maps/Generated/L_Gen_Depot_1 -game -RenderOffscreen -ResX=1280 -ResY=720 -nosound -benchmark -fps=30
//     -LevelJson=L_Gen_Depot_1.json -ExecCmds="CodexTactics.LevelGenLaddersShots"
// In Clear 13:00, from the game's own rendered view (an own camera, never a desktop capture), into Saved/Screenshots/LevelGenLadders/:
//  1. ladder_gallery.png - the leader halfway up the gallery's wall ladder (LAD_B_gallery): rungs outside the wall, rails against its face;
//  2. ladder_cut.png - the leader halfway up the cut's ladder (LAD_cut_3, the V3 exit) to the opening in the cut wall above;
//  3. cut_ramp.png - the squad walking down the ramp from the X_centre bridge onto the trench floor.
// Arguments: gallery=<ladder id> cut=<ladder id> (defaults above). Labels drawn in-engine. Exits when done. Godot reference: none (UE-only).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AIController.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "CanvasItem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/CapsuleComponent.h"
#include "Debug/DebugDrawService.h"
#include "Debug/FrontendSmokeUtils.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Environment/EnvironmentRules.h"
#include "Environment/EnvironmentSubsystem.h"
#include "GameFramework/HUD.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Ladders/LadderActor.h"
#include "Ladders/LadderClimbComponent.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "UnrealClient.h"

namespace LevelGenLaddersShots
{
	constexpr int32 WarmupFrames = 240;
	constexpr int32 MaxWaitFrames = 30 * 40;
	constexpr int32 MaxFrames = 30 * 400;

	enum class EShot : uint8 { Gallery, Cut, Ramp };
	enum class EStep : uint8 { Warmup, Setup, Wait, Shot, Write, Done };

	struct FRunner
	{
		EStep Step = EStep::Warmup;
		int32 Frame = 0;
		int32 StepFrame = 0;
		int32 ShotIndex = 0;
		int32 Failures = 0;
		FString Dir;
		FString Label;
		FString GalleryId = TEXT("LAD_B_gallery");
		FString CutId = TEXT("LAD_cut_3");
		FDelegateHandle EndFrameHandle;
		FDelegateHandle DrawHandle;
		TWeakObjectPtr<ACameraActor> Camera;
		TWeakObjectPtr<ALadderActor> Ladder;
		FVector Eye = FVector::ZeroVector;
		FVector Look = FVector::ZeroVector;
		float Fov = 55.f;
	};

	const EShot Shots[] = { EShot::Gallery, EShot::Cut, EShot::Ramp };
	const TCHAR* Files[] = { TEXT("ladder_gallery.png"), TEXT("ladder_cut.png"), TEXT("cut_ramp.png") };

	TSharedPtr<FRunner> Runner;

	void Check(bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("LevelGenLaddersShots %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		Runner->Failures += bOk ? 0 : 1;
	}

	void Finish()
	{
		UE_LOG(LogCodexTactics, Display, TEXT("LevelGenLaddersShots RESULT: %s -> %s"), Runner->Failures == 0 ? TEXT("PASS") : TEXT("FAIL"), *Runner->Dir);
		FCoreDelegates::OnEndFrame.Remove(Runner->EndFrameHandle);
		UDebugDrawService::Unregister(Runner->DrawHandle);
		FPlatformMisc::RequestExit(false, TEXT("LevelGenLaddersShots"));
	}

	ALadderActor* FindLadder(UWorld* World, const FString& Id)
	{
		const FName Tag(*FString::Printf(TEXT("ladder=%s"), *Id));
		for (TActorIterator<ALadderActor> It(World); It; ++It)
		{
			if (It->Tags.Contains(Tag))
			{
				return *It;
			}
		}
		return nullptr;
	}

	FVector OnNav(UWorld* World, const FVector& P)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		FNavLocation Out;
		return Nav && Nav->ProjectPointToNavigation(P, Out, FVector(120.f, 120.f, 150.f)) ? Out.Location : P;
	}

	/** The leader at LeaderAt, the others along Axis round OthersAt (projected on the navmesh). */
	void PlaceSquad(UWorld* World, USquadSubsystem& Squad, const FVector& LeaderAt, const FVector& OthersAt, const FVector& Axis)
	{
		int32 Slot = 0;
		for (AOperativeCharacter* Member : Squad.GetMembers())
		{
			if (!Member)
			{
				continue;
			}
			if (Member->LadderClimb && Member->LadderClimb->IsBusy())
			{
				Member->LadderClimb->AbortClimb(ELadderAbortReason::Teleported);
			}
			const bool bLeader = Member == Squad.GetLeader();
			FVector At = LeaderAt;
			if (!bLeader)
			{
				const float Sign = Slot % 2 == 0 ? 1.f : -1.f;
				At = OthersAt + Axis * Sign * (150.f + 150.f * static_cast<float>(Slot / 2));
				++Slot;
			}
			At = OnNav(World, At);
			Member->TeleportTo(At + FVector(0.f, 0.f, Member->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f), Member->GetActorRotation(), false, true);
			Member->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			if (AAIController* AI = Cast<AAIController>(Member->GetController()))
			{
				AI->StopMovement();
			}
		}
	}

	void View(UWorld* World)
	{
		APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		if (!PC)
		{
			return;
		}
		if (AHUD* Hud = PC->GetHUD())
		{
			Hud->bShowHUD = false;
		}
		const FRotator Rotation = (Runner->Look - Runner->Eye).Rotation();
		if (!Runner->Camera.IsValid())
		{
			Runner->Camera = World->SpawnActor<ACameraActor>(Runner->Eye, Rotation);
		}
		if (ACameraActor* Camera = Runner->Camera.Get())
		{
			Camera->SetActorLocationAndRotation(Runner->Eye, Rotation);
			Camera->GetCameraComponent()->SetFieldOfView(Runner->Fov);
			Camera->GetCameraComponent()->bConstrainAspectRatio = false;
			if (PC->GetViewTarget() != Camera)
			{
				PC->SetViewTarget(Camera);
			}
		}
	}

	void OnDraw(UCanvas* Canvas, APlayerController* /*Controller*/)
	{
		if (!Runner.IsValid() || !Canvas || !GEngine || Runner->Label.IsEmpty() || Runner->Step == EStep::Warmup)
		{
			return;
		}
		UFont* Font = GEngine->GetLargeFont();
		if (!Font)
		{
			return;
		}
		FCanvasTextItem Shadow(FVector2D(26.f, 22.f), FText::FromString(Runner->Label), Font, FLinearColor::Black);
		Shadow.Scale = FVector2D(1.2f, 1.2f);
		Canvas->DrawItem(Shadow);
		FCanvasTextItem Item(FVector2D(24.f, 20.f), FText::FromString(Runner->Label), Font, FLinearColor(1.f, 0.85f, 0.4f));
		Item.Scale = FVector2D(1.2f, 1.2f);
		Canvas->DrawItem(Item);
	}

	/** Sets up shot Index: the squad placed, the order given, the camera framed. False = nothing to shoot (skipped, a FAIL line). */
	bool Setup(UWorld* World, USquadSubsystem& Squad, int32 Index)
	{
		FRunner& R = *Runner;
		AOperativeCharacter* Leader = Squad.GetLeader();
		const EShot Shot = Shots[Index];
		if (Shot == EShot::Ramp)
		{
			if (Squad.IsSoloMode())
			{
				Squad.ExitSoloMode();
			}
			Squad.SetFollowersHolding(false);
			// The X_centre bridge's north rim -> the trench floor 12 m past it (the smoke's CutIn leg on L_Gen_Depot_1).
			const FVector Rim(683.f, -3312.f, 0.f);
			PlaceSquad(World, Squad, Rim, Rim + FVector(150.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f));
			Leader->OrderMoveTo(FVector(-17.f, -1862.f, -250.f), false);
			// Above the trench's middle, looking back along it at the ramp (the cut walls on both rims would hide it from the side).
			R.Look = FVector(-17.f, -2800.f, -180.f);
			R.Eye = FVector(-17.f, -950.f, 560.f);
			R.Fov = 60.f;
			R.Label = TEXT("L_Gen_Depot_1: the squad walks down the X_centre ramp into the cut");
			return true;
		}
		ALadderActor* Ladder = FindLadder(World, Shot == EShot::Gallery ? R.GalleryId : R.CutId);
		R.Ladder = Ladder;
		Check(Ladder != nullptr, FString::Printf(TEXT("ladder %s found (tag ladder=<id>)"), Shot == EShot::Gallery ? *R.GalleryId : *R.CutId));
		if (!Ladder)
		{
			return false;
		}
		if (!Squad.IsSoloMode())
		{
			Squad.EnterSoloMode();
		}
		Squad.SetFollowersHolding(true);
		const FVector Out = Ladder->GetOutward();
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Out).GetSafeNormal();
		const FVector Foot = Ladder->GetBottomLinkWorld();
		PlaceSquad(World, Squad, Foot + Out * 120.f, Foot + Out * 380.f + Side * 250.f, Side);
		Leader->OrderMoveTo(Ladder->GetTopLinkWorld() - Out * 150.f + FVector(0.f, 0.f, 50.f), false);
		const float Height = Ladder->GetTopWorldZ() - Ladder->GetFloorZ();
		R.Look = Ladder->GetActorLocation() + FVector(0.f, 0.f, Height * 0.6f) - Out * 30.f;
		if (Shot == EShot::Gallery)
		{
			R.Eye = R.Look + Out * 520.f + Side * 380.f + FVector(0.f, 0.f, 140.f);
			R.Label = FString::Printf(TEXT("L_Gen_Depot_1: %s - the leader climbs the gallery's wall ladder"), *R.GalleryId);
		}
		else
		{
			// From inside the trench, along it, a little above the rim: the ladder on the trench face and the opening in the cut wall above it.
			R.Eye = R.Look + Out * 240.f + Side * 650.f + FVector(0.f, 0.f, 260.f);
			R.Label = FString::Printf(TEXT("L_Gen_Depot_1: %s - the leader climbs out of the cut to the opening in the cut wall"), *R.CutId);
		}
		R.Fov = 55.f;
		return true;
	}

	/** The moment to shoot: halfway up the ladder (feet ~1.2 m over its floor while climbing); the ramp: the leader on it. */
	bool Ready(USquadSubsystem& Squad, int32 Index)
	{
		const AOperativeCharacter* Leader = Squad.GetLeader();
		if (!Leader)
		{
			return false;
		}
		const float Feet = static_cast<float>(Leader->GetActorLocation().Z) - Leader->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		if (Shots[Index] == EShot::Ramp)
		{
			return Feet < -60.f && Feet > -240.f;
		}
		const ALadderActor* Ladder = Runner->Ladder.Get();
		const ULadderClimbComponent* Climb = Leader->LadderClimb.Get();
		return Ladder && Climb && Climb->IsClimbing() && Feet > Ladder->GetFloorZ() + 110.f;
	}

	void Tick()
	{
		FRunner& R = *Runner;
		++R.Frame;
		++R.StepFrame;
		if (R.Frame > MaxFrames)
		{
			Check(false, FString::Printf(TEXT("timeout in step %d"), static_cast<int32>(R.Step)));
			Finish();
			return;
		}
		UWorld* World = FrontendSmokeUtils::FindGameWorld();
		UEnvironmentSubsystem* Environment = World ? UEnvironmentSubsystem::Get(World) : nullptr;
		USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
		if (!World || !Environment || !Environment->IsInitialised() || !Squad || !Squad->GetLeader())
		{
			return;
		}
		UWidgetLayoutLibrary::RemoveAllWidgets(World);
		switch (R.Step)
		{
		case EStep::Warmup:
			if (R.StepFrame >= WarmupFrames)
			{
				Environment->SetClockScale(0.f);
				Environment->ForceWeather(EWeatherState::Clear, true);
				Environment->SetHour(13.f);
				GEngine->Exec(World, TEXT("DisableAllScreenMessages"));
				R.Step = EStep::Setup;
				R.StepFrame = 0;
			}
			break;
		case EStep::Setup:
			if (!Setup(World, *Squad, R.ShotIndex))
			{
				++R.ShotIndex;
				R.Step = R.ShotIndex < UE_ARRAY_COUNT(Shots) ? EStep::Setup : EStep::Done;
				break;
			}
			View(World);
			R.Step = EStep::Wait;
			R.StepFrame = 0;
			break;
		case EStep::Wait:
			View(World);
			if (Ready(*Squad, R.ShotIndex) || R.StepFrame > MaxWaitFrames)
			{
				Check(R.StepFrame <= MaxWaitFrames, FString::Printf(TEXT("%s: the moment came (%d frames)"), Files[R.ShotIndex], R.StepFrame));
				R.Step = EStep::Shot;
				R.StepFrame = 0;
			}
			break;
		case EStep::Shot:
			View(World);
			FScreenshotRequest::RequestScreenshot(R.Dir / Files[R.ShotIndex], /*bInShowUI*/ true, /*bAddFilenameSuffix*/ false);
			UE_LOG(LogCodexTactics, Display, TEXT("LevelGenLaddersShots: %s (eye %s, look %s)"), Files[R.ShotIndex], *R.Eye.ToCompactString(), *R.Look.ToCompactString());
			R.Step = EStep::Write;
			R.StepFrame = 0;
			break;
		case EStep::Write:
			View(World);
			if (R.StepFrame >= 10)
			{
				++R.ShotIndex;
				R.Step = R.ShotIndex < UE_ARRAY_COUNT(Shots) ? EStep::Setup : EStep::Done;
				R.StepFrame = 0;
			}
			break;
		case EStep::Done:
			for (const TCHAR* File : Files)
			{
				Check(IFileManager::Get().FileSize(*(R.Dir / File)) > 10000, FString::Printf(TEXT("%s written"), File));
			}
			Finish();
			break;
		}
	}

	void Run(const TArray<FString>& Args, UWorld* /*World*/)
	{
		Runner = MakeShared<FRunner>();
		for (const FString& Arg : Args)
		{
			if (Arg.StartsWith(TEXT("gallery=")))
			{
				Runner->GalleryId = Arg.Mid(8);
			}
			else if (Arg.StartsWith(TEXT("cut=")))
			{
				Runner->CutId = Arg.Mid(4);
			}
		}
		if (IConsoleVariable* Pressure = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Pressure.Enabled")))
		{
			Pressure->Set(0, ECVF_SetByConsole);
		}
		Runner->Dir = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / TEXT("..") / TEXT("LevelGenLadders"));
		FPaths::CollapseRelativeDirectories(Runner->Dir);
		IFileManager::Get().MakeDirectory(*Runner->Dir, true);
		Runner->DrawHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateStatic(&OnDraw));
		Runner->EndFrameHandle = FCoreDelegates::OnEndFrame.AddStatic(&Tick);
		UE_LOG(LogCodexTactics, Display, TEXT("LevelGenLaddersShots: the gallery ladder, the cut ladder, the cut ramp -> %s"), *Runner->Dir);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.LevelGenLaddersShots"),
		TEXT("Dev: rendered pictures of L_Gen_Depot_1's fixed ladders (the leader halfway up the gallery and the cut ladder) and the squad on the cut ramp ")
		TEXT("into Saved/Screenshots/LevelGenLadders; [gallery=<ladder id>] [cut=<ladder id>]; needs rendering, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
