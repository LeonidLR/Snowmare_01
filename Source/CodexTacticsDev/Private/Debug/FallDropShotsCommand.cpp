// Dev-only RENDERED pictures of the running drops (game designer items 6 / 10 / 11a, 2026-11-03; docs/systems/stances-and-movement.md
// "Falls and drops", smoke FallDropSmoke):
//   UnrealEditor.exe <uproject> /Game/Maps/Generated/L_Gen_Depot_1 -game -RenderOffscreen -ResX=1280 -ResY=720 -nosound -benchmark -fps=30
//     -LevelJson=L_Gen_Depot_1.json -ExecCmds="CodexTactics.FallDropShots"
// In Clear 13:00, from the game's own rendered view (an own camera, never a desktop capture), into Saved/Screenshots/FallsDrops/:
//  1. drop_marker.png - the squad on the cut's rim ordered onto the cut floor: the HUD's drop marker on the leader's path off the X_ne bridge;
//  2. drop_midair.png - the leader sprinting off the ruin roof (+2) onto the annex roof (+1): in mid-air, the fall clip playing;
//  3. drop_landed.png - the same a moment later, landed on the annex roof.
// Labels drawn in-engine. Exits when done. Godot reference: none (UE-only).

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
#include "Components/SkeletalMeshComponent.h"
#include "Debug/DebugDrawService.h"
#include "Debug/FrontendSmokeUtils.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "Environment/EnvironmentRules.h"
#include "Environment/EnvironmentSubsystem.h"
#include "Falls/DropLinkSubsystem.h"
#include "Falls/LedgeFallComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/HUD.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "UnrealClient.h"

namespace FallDropShots
{
	constexpr int32 WarmupFrames = 240;
	constexpr int32 MaxWaitFrames = 30 * 40;
	constexpr int32 MaxFrames = 30 * 300;

	enum class EShot : uint8 { Marker, MidAir, Landed };
	enum class EStep : uint8 { Warmup, Setup, Wait, Shot, Write, Done };

	const EShot Shots[] = { EShot::Marker, EShot::MidAir, EShot::Landed };
	const TCHAR* Files[] = { TEXT("drop_marker.png"), TEXT("drop_midair.png"), TEXT("drop_landed.png") };

	struct FRunner
	{
		EStep Step = EStep::Warmup;
		int32 Frame = 0;
		int32 StepFrame = 0;
		int32 ShotIndex = 0;
		int32 Failures = 0;
		FString Dir;
		FString Label;
		FDelegateHandle EndFrameHandle;
		FDelegateHandle DrawHandle;
		TWeakObjectPtr<ACameraActor> Camera;
		FVector Eye = FVector::ZeroVector;
		FVector Look = FVector::ZeroVector;
		float Fov = 55.f;
		int32 RoofLink = INDEX_NONE;
		int32 LandingsAtSetup = 0;
		int32 AirFrames = 0;
	};

	TSharedPtr<FRunner> Runner;

	void Check(bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("FallDropShots %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		Runner->Failures += bOk ? 0 : 1;
	}

	void Finish()
	{
		UE_LOG(LogCodexTactics, Display, TEXT("FallDropShots RESULT: %s -> %s"), Runner->Failures == 0 ? TEXT("PASS") : TEXT("FAIL"), *Runner->Dir);
		FCoreDelegates::OnEndFrame.Remove(Runner->EndFrameHandle);
		UDebugDrawService::Unregister(Runner->DrawHandle);
		FPlatformMisc::RequestExit(false, TEXT("FallDropShots"));
	}

	FVector OnNav(UWorld* World, const FVector& P)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		FNavLocation Out;
		return Nav && Nav->ProjectPointToNavigation(P, Out, FVector(80.f, 80.f, 60.f)) ? Out.Location : P;
	}

	void PutAt(AOperativeCharacter& Member, const FVector& Feet, float Yaw)
	{
		Member.TeleportTo(Feet + FVector(0.f, 0.f, Member.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 3.f), FRotator(0.f, Yaw, 0.f), false, true);
		Member.GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		if (AAIController* AI = Cast<AAIController>(Member.GetController()))
		{
			AI->StopMovement();
		}
	}

	void View(UWorld* World, bool bHud)
	{
		APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		if (!PC)
		{
			return;
		}
		if (AHUD* Hud = PC->GetHUD())
		{
			Hud->bShowHUD = bHud;
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
		FCanvasTextItem Shadow(FVector2D(26.f, 52.f), FText::FromString(Runner->Label), Font, FLinearColor::Black);
		Canvas->DrawItem(Shadow);
		FCanvasTextItem Item(FVector2D(24.f, 50.f), FText::FromString(Runner->Label), Font, FLinearColor(1.f, 0.85f, 0.4f));
		Canvas->DrawItem(Item);
	}

	/** The ruin roof -> annex roof link (the smoke's stage 3 pick: start on the +2 roof, end on the +1 roof). */
	int32 FindRoofLink(const UDropLinkSubsystem& Links)
	{
		int32 Best = INDEX_NONE;
		float BestScore = -1e9f;
		for (int32 Index = 0; Index < Links.GetLinks().Num(); ++Index)
		{
			const FDropLink& Link = Links.GetLinks()[Index];
			// The ruin's roof is the only +2 roof with a +1 roof 3 m below across a gap (B_ruin -> B_ruin_annex).
			if (FMath::Abs(Link.Start.Z - 600.f) < 60.f && FMath::Abs(Link.End.Z - 300.f) < 60.f && Link.Start.X < -3000.f && Link.Start.X > -3800.f)
			{
				const float Score = -FMath::Abs(static_cast<float>(Link.Start.X) + 3400.f);
				if (Score > BestScore)
				{
					BestScore = Score;
					Best = Index;
				}
			}
		}
		return Best;
	}

	bool Setup(UWorld* World, USquadSubsystem& Squad, int32 Index)
	{
		FRunner& R = *Runner;
		AOperativeCharacter* Leader = Squad.GetLeader();
		const UDropLinkSubsystem* Links = World->GetSubsystem<UDropLinkSubsystem>();
		const EShot Shot = Shots[Index];
		if (Shot == EShot::Landed)
		{
			return true; // the mid-air shot's drop goes on
		}
		if (Shot == EShot::Marker)
		{
			if (Squad.IsSoloMode())
			{
				Squad.ExitSoloMode();
			}
			Squad.SetFollowersHolding(false);
			// The cut's north rim by the X_ne bridge (Y 7769..8169, the north face of the cut wall at X 433) -> the cut floor east of it.
			const FVector Rim(733.f, 7969.f, 0.f);
			int32 Slot = 0;
			for (AOperativeCharacter* Member : Squad.GetMembers())
			{
				if (Member)
				{
					PutAt(*Member, OnNav(World, Member == Leader ? Rim : Rim + FVector(250.f, (Slot++ % 2 == 0 ? 1.f : -1.f) * 150.f, 0.f)), 180.f);
				}
			}
			Leader->OrderMoveTo(FVector(0.f, 8569.f, -300.f), false);
			// Along the cut from the east, over its floor: the bridge, its open side and the drop into the cut.
			R.Look = FVector(150.f, 8150.f, -120.f);
			R.Eye = FVector(650.f, 9600.f, 650.f);
			R.Fov = 60.f;
			R.Label = TEXT("L_Gen_Depot_1: an order into the cut - the path's drop off the bridge is marked (DROP 3.0 m)");
			return true;
		}
		R.RoofLink = Links ? FindRoofLink(*Links) : INDEX_NONE;
		const FDropLink* Link = Links ? Links->GetLink(R.RoofLink) : nullptr;
		Check(Link != nullptr, TEXT("the ruin roof -> annex roof drop link"));
		if (!Link)
		{
			return false;
		}
		if (!Squad.IsSoloMode())
		{
			Squad.EnterSoloMode();
		}
		Squad.SetFollowersHolding(true);
		int32 Slot = 0;
		for (AOperativeCharacter* Member : Squad.GetMembers())
		{
			if (Member && Member != Leader)
			{
				PutAt(*Member, OnNav(World, FVector(-1500.f + 200.f * Slot++, -6000.f, 0.f)), 0.f);
			}
		}
		PutAt(*Leader, OnNav(World, Link->Start - Link->Dir * 450.f), Link->Dir.Rotation().Yaw);
		Leader->OrderMoveTo(Link->End + Link->Dir * 150.f, /*bSprint*/ true);
		R.LandingsAtSetup = Leader->LedgeFall->GetLandingCount();
		R.AirFrames = 0;
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Link->Dir).GetSafeNormal();
		R.Look = (Link->Start + Link->End) * 0.5f + FVector(0.f, 0.f, 80.f);
		R.Eye = R.Look + Side * 1150.f - Link->Dir * 100.f + FVector(0.f, 0.f, 90.f);
		R.Fov = 55.f;
		R.Label = TEXT("L_Gen_Depot_1: a sprint off the ruin roof (+2) onto the annex roof (+1), 2.5 m across, 3 m down");
		return true;
	}

	bool Ready(UWorld* World, USquadSubsystem& Squad, int32 Index)
	{
		FRunner& R = *Runner;
		const AOperativeCharacter* Leader = Squad.GetLeader();
		const ULedgeFallComponent* Fall = Leader ? Leader->LedgeFall.Get() : nullptr;
		if (!Fall)
		{
			return false;
		}
		switch (Shots[Index])
		{
		case EShot::Marker:
		{
			TArray<FDropMarker> Markers;
			World->GetSubsystem<UDropLinkSubsystem>()->GatherSquadMarkers(Markers);
			return !Markers.IsEmpty() && R.StepFrame > 20;
		}
		case EShot::MidAir:
			R.AirFrames = Fall->IsAirborne() ? R.AirFrames + 1 : 0;
			return R.AirFrames >= 9; // ~0.3 s into the fall at 30 fps
		case EShot::Landed:
			return Fall->GetLandingCount() > R.LandingsAtSetup && R.StepFrame > 15;
		default:
			return true;
		}
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
		const UDropLinkSubsystem* Links = World ? World->GetSubsystem<UDropLinkSubsystem>() : nullptr;
		if (!World || !Environment || !Environment->IsInitialised() || !Squad || !Squad->GetLeader() || !Links || !Links->HasGenerated())
		{
			return;
		}
		UWidgetLayoutLibrary::RemoveAllWidgets(World);
		const bool bHud = Shots[FMath::Min(R.ShotIndex, static_cast<int32>(UE_ARRAY_COUNT(Shots)) - 1)] == EShot::Marker;
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
			View(World, bHud);
			R.Step = EStep::Wait;
			R.StepFrame = 0;
			break;
		case EStep::Wait:
			View(World, bHud);
			if (Ready(World, *Squad, R.ShotIndex) || R.StepFrame > MaxWaitFrames)
			{
				Check(R.StepFrame <= MaxWaitFrames, FString::Printf(TEXT("%s: the moment came (%d frames)"), Files[R.ShotIndex], R.StepFrame));
				R.Step = EStep::Shot;
				R.StepFrame = 0;
			}
			break;
		case EStep::Shot:
			View(World, bHud);
			FScreenshotRequest::RequestScreenshot(R.Dir / Files[R.ShotIndex], /*bInShowUI*/ true, /*bAddFilenameSuffix*/ false);
			UE_LOG(LogCodexTactics, Display, TEXT("FallDropShots: %s (eye %s, look %s)"), Files[R.ShotIndex], *R.Eye.ToCompactString(), *R.Look.ToCompactString());
			R.Step = EStep::Write;
			R.StepFrame = 0;
			break;
		case EStep::Write:
			View(World, bHud);
			if (R.StepFrame >= 6)
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

	void Run(const TArray<FString>& /*Args*/, UWorld* /*World*/)
	{
		Runner = MakeShared<FRunner>();
		if (IConsoleVariable* Pressure = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Pressure.Enabled")))
		{
			Pressure->Set(0, ECVF_SetByConsole);
		}
		Runner->Dir = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / TEXT("..") / TEXT("FallsDrops"));
		FPaths::CollapseRelativeDirectories(Runner->Dir);
		IFileManager::Get().MakeDirectory(*Runner->Dir, true);
		Runner->DrawHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateStatic(&OnDraw));
		Runner->EndFrameHandle = FCoreDelegates::OnEndFrame.AddStatic(&Tick);
		UE_LOG(LogCodexTactics, Display, TEXT("FallDropShots: the drop marker, a roof-to-roof drop in mid-air and landed -> %s"), *Runner->Dir);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.FallDropShots"),
		TEXT("Dev: rendered pictures of the running drops on L_Gen_Depot_1 (the order path's drop marker into the cut, a sprint off the ruin roof onto the annex ")
		TEXT("roof in mid-air and landed) into Saved/Screenshots/FallsDrops; needs rendering, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
