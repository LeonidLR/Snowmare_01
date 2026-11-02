// Dev-only RENDERED pictures of the two fixes of the game designer's bug report 2026-11-02 on L_Gen_Depot_1 (item 7: the squad stuck at a
// ramp's foot; item 8: the roof cutaway followed the wrong operative; smokes LevelGenRampSmoke / RoofCutawayFocusSmoke):
//   UnrealEditor.exe <uproject> /Game/Maps/Generated/L_Gen_Depot_1 -game -RenderOffscreen -ResX=1280 -ResY=720 -nosound -benchmark -fps=30
//     -LevelJson=L_Gen_Depot_1.json -ExecCmds="CodexTactics.RampRoofShots"
// In Clear 13:00, from the game's own rendered view (never a desktop capture), into Saved/Screenshots/RampRoof/:
//  1. ramp_squad.png - squad mode, the man at the back sent onto the ruin's roof: the squad single file up the external stair STAIR_ext_7
//     (an own camera beside the stair);
//  2. cutaway_roofman.png - two men on B_hall_n's 9 m roof, one under it; the roof man selected: the roof stays;
//  3. cutaway_groundman.png - the man under the roof selected: the roof above him is cut away (2 / 3: an own camera high over the hall like the
//     tactical camera, the same view for both; the cutaway is the selection's, not the camera's).
// Labels drawn in-engine. Exits when done. Godot reference: none (UE-only).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AIController.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/RoofCutawaySubsystem.h"
#include "CanvasItem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/CapsuleComponent.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/DebugDrawService.h"
#include "Debug/FrontendSmokeUtils.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "Environment/EnvironmentRules.h"
#include "Environment/EnvironmentSubsystem.h"
#include "GameFramework/HUD.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "UnrealClient.h"

namespace RampRoofShots
{
	constexpr int32 WarmupFrames = 240;
	constexpr int32 MaxWaitFrames = 30 * 40;
	constexpr int32 MaxFrames = 30 * 300;

	enum class EShot : uint8 { Ramp, RoofMan, GroundMan };
	enum class EStep : uint8 { Warmup, Setup, Wait, Shot, Write, Done };

	const EShot Shots[] = { EShot::Ramp, EShot::RoofMan, EShot::GroundMan };
	const TCHAR* Files[] = { TEXT("ramp_squad.png"), TEXT("cutaway_roofman.png"), TEXT("cutaway_groundman.png") };

	// L_Gen_Depot_1 (the accepted layout gen_ref_01_02_depot_1): STAIR_ext_7 from (-3920, 1622) north to (-3920, 2480) at +6 m, onto the
	// ruin's roof; B_hall_n's footprint (3200, -3250) - (5700, 550), roof +3 = 9 m.
	const FVector2D StairFoot(-3920.0, 1622.0);
	const FVector RoofGoal(-3400.f, 2000.f, 620.f);
	const FVector2D HallCentre(4450.0, -1350.0);
	constexpr float HallRoofZ = 900.f;

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
		/** The game's own view (the tactical camera) before the own camera took over. */
		TWeakObjectPtr<AActor> GameView;
		TWeakObjectPtr<AOperativeCharacter> Back;
		TWeakObjectPtr<AOperativeCharacter> RoofMan;
		TWeakObjectPtr<AOperativeCharacter> GroundMan;
		bool bOwnCamera = false;
		FVector Eye = FVector::ZeroVector;
		FVector Look = FVector::ZeroVector;
	};

	TSharedPtr<FRunner> Runner;

	void Check(bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("RampRoofShots %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		Runner->Failures += bOk ? 0 : 1;
	}

	void Finish()
	{
		UE_LOG(LogCodexTactics, Display, TEXT("RampRoofShots RESULT: %s -> %s"), Runner->Failures == 0 ? TEXT("PASS") : TEXT("FAIL"), *Runner->Dir);
		FCoreDelegates::OnEndFrame.Remove(Runner->EndFrameHandle);
		UDebugDrawService::Unregister(Runner->DrawHandle);
		FPlatformMisc::RequestExit(false, TEXT("RampRoofShots"));
	}

	float FeetZ(const AOperativeCharacter& Operative)
	{
		return static_cast<float>(Operative.GetActorLocation().Z) - Operative.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	}

	void Put(UWorld* World, AOperativeCharacter& Member, const FVector2D& At, float Z, float Yaw)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		FNavLocation OnNav;
		FVector Spot(At.X, At.Y, Z + 30.f);
		if (Nav && Nav->ProjectPointToNavigation(Spot, OnNav, FVector(120.f, 120.f, 100.f)))
		{
			Spot = OnNav.Location;
		}
		Member.TeleportTo(Spot + FVector(0.f, 0.f, Member.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f), FRotator(0.f, Yaw, 0.f), false, true);
		Member.GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		if (AAIController* AI = Cast<AAIController>(Member.GetController()))
		{
			AI->StopMovement();
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
		if (!Runner->GameView.IsValid() && PC->GetViewTarget() != Runner->Camera.Get())
		{
			Runner->GameView = PC->GetViewTarget();
		}
		if (!Runner->bOwnCamera)
		{
			// The game's own tactical camera (it follows the selected man): the view goes back to it.
			if (AActor* GameView = Runner->GameView.Get(); GameView && PC->GetViewTarget() != GameView)
			{
				PC->SetViewTarget(GameView);
			}
			return;
		}
		const FRotator Rotation = (Runner->Look - Runner->Eye).Rotation();
		if (!Runner->Camera.IsValid())
		{
			Runner->Camera = World->SpawnActor<ACameraActor>(Runner->Eye, Rotation);
		}
		if (ACameraActor* Camera = Runner->Camera.Get())
		{
			Camera->SetActorLocationAndRotation(Runner->Eye, Rotation);
			Camera->GetCameraComponent()->SetFieldOfView(55.f);
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

	/** The player's click on Goal (HandleWorldHit). */
	void Click(UWorld* World, const FVector& Goal)
	{
		if (ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController()))
		{
			FHitResult Hit;
			Hit.ImpactPoint = Goal;
			Hit.Location = Goal;
			Hit.ImpactNormal = FVector::UpVector;
			Hit.Normal = FVector::UpVector;
			Hit.bBlockingHit = true;
			PC->HandleWorldHit(Hit);
		}
	}

	bool Setup(UWorld* World, USquadSubsystem& Squad, int32 Index)
	{
		FRunner& R = *Runner;
		TArray<AOperativeCharacter*> Members = Squad.GetMembers();
		Members.Remove(nullptr);
		if (Members.Num() < 3)
		{
			Check(false, TEXT("3 operatives"));
			return false;
		}
		switch (Shots[Index])
		{
		case EShot::Ramp:
		{
			if (Squad.IsSoloMode())
			{
				Squad.ExitSoloMode();
			}
			Squad.SetFollowersHolding(false);
			// Bunched in the stair's mouth (LevelGenRampSmoke's squad leg): the man at the back selected and clicked onto the roof.
			Put(World, *Members[0], StairFoot - FVector2D(0.0, 260.0), 0.f, 90.f);
			Put(World, *Members[1], StairFoot + FVector2D(60.0, -120.0), 0.f, 90.f);
			Put(World, *Members[2], StairFoot + FVector2D(-60.0, -120.0), 0.f, 90.f);
			R.Back = Members[0];
			Squad.SetLeader(Members[0]);
			Click(World, RoofGoal);
			// West of the stair, level with its middle, looking at it and the ruin's wall behind it.
			R.Look = FVector(-3910.f, 2020.f, 260.f);
			R.Eye = FVector(-4900.f, 1500.f, 820.f);
			R.bOwnCamera = true;
			R.Label = TEXT("L_Gen_Depot_1 STAIR_ext_7: the squad goes up single file (the man at the back sent onto the roof)");
			return true;
		}
		case EShot::RoofMan:
		{
			if (!Squad.IsSoloMode())
			{
				Squad.EnterSoloMode();
			}
			Squad.SetFollowersHolding(true);
			Put(World, *Members[0], HallCentre - FVector2D(300.0, 0.0), HallRoofZ, 0.f);
			Put(World, *Members[1], HallCentre + FVector2D(300.0, 0.0), HallRoofZ, 180.f);
			Put(World, *Members[2], HallCentre, 0.f, 90.f);
			R.RoofMan = Members[0];
			R.GroundMan = Members[2];
			Squad.SetLeader(Members[0]);
			// High over the hall's south-west corner, looking down at its middle like the tactical camera (the same view for both shots).
			R.Look = FVector(HallCentre.X, HallCentre.Y, 300.f);
			R.Eye = R.Look + FVector(-1100.f, -900.f, 1600.f);
			R.bOwnCamera = true;
			R.Label = FString::Printf(TEXT("B_hall_n: %s selected ON the roof - the roof stays (2 on the roof, %s under it)"), *Members[0]->DisplayName.ToString(),
				*Members[2]->DisplayName.ToString());
			return true;
		}
		case EShot::GroundMan:
		{
			AOperativeCharacter* Ground = R.GroundMan.Get();
			if (!Ground)
			{
				return false;
			}
			Squad.SetLeader(Ground);
			R.bOwnCamera = true;
			R.Label = FString::Printf(TEXT("B_hall_n: %s selected UNDER the roof - the roof above him is cut away"), *Ground->DisplayName.ToString());
			return true;
		}
		}
		return false;
	}

	/** The moment: the selected man halfway up the stair with a man behind him on it; the cutaway shots once the view and the cutaway settled. */
	bool Ready(UWorld* World, USquadSubsystem& Squad, int32 Index)
	{
		const FRunner& R = *Runner;
		if (Shots[Index] == EShot::Ramp)
		{
			const AOperativeCharacter* Back = R.Back.Get();
			int32 OnStair = 0;
			for (const AOperativeCharacter* Member : Squad.GetMembers())
			{
				OnStair += Member && FeetZ(*Member) > 40.f && FeetZ(*Member) < 560.f ? 1 : 0;
			}
			return Back && FeetZ(*Back) > 300.f && OnStair >= 2;
		}
		const URoofCutawaySubsystem* Roofs = World->GetSubsystem<URoofCutawaySubsystem>();
		const bool bCut = Roofs && Roofs->GetCutAwayCount() > 0;
		return Runner->StepFrame > 75 && (Shots[Index] == EShot::GroundMan ? bCut : !bCut);
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
			if (Ready(World, *Squad, R.ShotIndex) || R.StepFrame > MaxWaitFrames)
			{
				Check(R.StepFrame <= MaxWaitFrames, FString::Printf(TEXT("%s: the moment came (%d frames)"), Files[R.ShotIndex], R.StepFrame));
				R.Step = EStep::Shot;
				R.StepFrame = 0;
			}
			break;
		case EStep::Shot:
			View(World);
			FScreenshotRequest::RequestScreenshot(R.Dir / Files[R.ShotIndex], /*bInShowUI*/ true, /*bAddFilenameSuffix*/ false);
			UE_LOG(LogCodexTactics, Display, TEXT("RampRoofShots: %s (leader %s)"), Files[R.ShotIndex], *Squad->GetLeader()->DisplayName.ToString());
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

	void Run(const TArray<FString>& /*Args*/, UWorld* /*World*/)
	{
		Runner = MakeShared<FRunner>();
		if (IConsoleVariable* Pressure = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Pressure.Enabled")))
		{
			Pressure->Set(0, ECVF_SetByConsole);
		}
		Runner->Dir = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / TEXT("..") / TEXT("RampRoof"));
		FPaths::CollapseRelativeDirectories(Runner->Dir);
		IFileManager::Get().MakeDirectory(*Runner->Dir, true);
		Runner->DrawHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateStatic(&OnDraw));
		Runner->EndFrameHandle = FCoreDelegates::OnEndFrame.AddStatic(&Tick);
		UE_LOG(LogCodexTactics, Display, TEXT("RampRoofShots: the squad up the ruin's stair, the hall roof with the roof man / the ground man selected -> %s"), *Runner->Dir);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.RampRoofShots"),
		TEXT("Dev: rendered pictures of L_Gen_Depot_1 (bug report 2026-11-02 items 7 / 8): the squad single file up STAIR_ext_7, the B_hall_n roof cutaway with the roof ")
		TEXT("man / the man under it selected, into Saved/Screenshots/RampRoof; needs rendering, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
