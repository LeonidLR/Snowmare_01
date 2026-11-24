// Dev-only pictures of the designer's 2026-11-22 showcase fixes (items 3 and 6): the story / door labels (UStoryLabelSubsystem) over the
// whole street and per house, and the fixed leaf swing at the entrances (UDoorSubsystem::ChooseSwings: an entrance opens outward, no leaf over
// another doorway). Needs rendering:
//   UnrealEditor-Cmd.exe <uproject> /Game/Maps/KitPilot/L_Gen_StoryShowcase_Kit -game -RenderOffscreen -ResX=1600 -ResY=900 -nosound
//     -LevelJson=L_Gen_StoryShowcase_Kit.json -ExecCmds="CodexTactics.StoryShowcaseFixShots dir=<abs>"
// Writes <dir>/<map>_labels_street.png, <map>_labels_<building>.png, <map>_<building>_entrance_swing.png; logs each; exits.
// Godot reference: none (UE-only).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "SmokeUtils.h"
#include "AIController.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/RoofCutawaySubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/CapsuleComponent.h"
#include "Debug/FrontendSmokeUtils.h"
#include "Debug/StoryLabelSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Environment/EnvironmentRules.h"
#include "Environment/EnvironmentSubsystem.h"
#include "GameFramework/HUD.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Interactables/DoorActor.h"
#include "Interactables/DoorSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "UnrealClient.h"

namespace StoryShowcaseFixShots
{
	constexpr int32 WarmupFrames = 240;
	constexpr int32 SettleFrames = 120;

	struct FShot
	{
		FString Name;
		FVector Eye = FVector::ZeroVector;
		FRotator View = FRotator::ZeroRotator;
		/** A swing shot: the leader inside this building at its entrance, its storey-0 doors forced open. */
		TWeakObjectPtr<ADoorActor> Entrance;
	};

	struct FRunner
	{
		int32 Frame = 0;
		int32 StepFrame = 0;
		int32 Index = -1;
		bool bPlanned = false;
		bool bDone = false;
		FString Dir;
		FString MapName;
		TArray<FShot> Shots;
		TArray<FString> Written;
		TWeakObjectPtr<ACameraActor> Camera;
		FDelegateHandle EndFrameHandle;
	};

	TSharedPtr<FRunner> Runner;

	void Finish(bool bOk)
	{
		int32 Ok = 0;
		for (const FString& File : Runner->Written)
		{
			Ok += IFileManager::Get().FileSize(*File) > 10000 ? 1 : 0;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: shot %s (%lld bytes)"), *File, IFileManager::Get().FileSize(*File));
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s StoryShowcaseFixShots (%d / %d pictures)"), bOk && Ok == Runner->Written.Num() && Ok > 0 ? TEXT("PASS") : TEXT("FAIL"),
			Ok, Runner->Written.Num());
		FCoreDelegates::OnEndFrame.Remove(Runner->EndFrameHandle);
		Runner->bDone = true;
		FPlatformMisc::RequestExit(false, TEXT("StoryShowcaseFixShots"));
	}

	void Plan(UWorld* World, URoofCutawaySubsystem& Roofs, UDoorSubsystem& Doors)
	{
		Roofs.RebuildStoreyIndex();
		FBox All(ForceInit);
		TMap<FString, FBox> Houses;
		for (const TPair<FString, int32>& Building : Roofs.GetStoreyBuildings())
		{
			FBox Box(ForceInit);
			for (const FBox& Slab : Roofs.GetStoreySlabs(Building.Key))
			{
				Box += Slab;
			}
			Houses.Add(Building.Key, Box);
			All += Box;
		}
		// The whole street from above the start end, looking along it.
		const FVector Centre = All.GetCenter();
		const float Span = static_cast<float>(FMath::Max(All.Max.X - All.Min.X, All.Max.Y - All.Min.Y));
		const bool bAlongX = All.Max.X - All.Min.X >= All.Max.Y - All.Min.Y;
		const FVector Along = bAlongX ? FVector(1.f, 0.f, 0.f) : FVector(0.f, 1.f, 0.f);
		FShot& Street = Runner->Shots.AddDefaulted_GetRef();
		Street.Name = TEXT("labels_street");
		Street.Eye = Centre - Along * (Span * 0.75f) + FVector(0.f, 0.f, Span * 0.55f);
		Street.View = (Centre - Street.Eye).Rotation();
		for (const TPair<FString, FBox>& House : Houses)
		{
			FShot& Shot = Runner->Shots.AddDefaulted_GetRef();
			Shot.Name = FString::Printf(TEXT("labels_%s"), *House.Key);
			const FVector C = House.Value.GetCenter();
			const float Size = static_cast<float>(FMath::Max(House.Value.Max.X - House.Value.Min.X, House.Value.Max.Y - House.Value.Min.Y));
			Shot.Eye = C + FVector(-0.9f, -0.9f, 1.f).GetSafeNormal() * (Size * 1.6f + 1200.f);
			Shot.View = (C - Shot.Eye).Rotation();
		}
		// The entrances: straight down over each building's entrance doors (the leaves forced open), the leader inside so the storey cutaway shows them.
		for (ADoorActor* Door : Doors.GetDoors())
		{
			if (!Door || Door->GetRole() != TEXT("entry") || Door->GetFloor() != 0)
			{
				continue;
			}
			const FVector Out = Door->GetNormal() * Door->GetFixedSwingSign();
			const FVector Target = Door->GetActorLocation() + FVector(0.f, 0.f, 90.f);
			FShot& Oblique = Runner->Shots.AddDefaulted_GetRef();
			Oblique.Name = FString::Printf(TEXT("%s_%s_entrance_swing"), *Door->GetBuildingId(), *Door->GetOpeningId());
			Oblique.Eye = Target + Out * 520.f + Door->GetTangent() * 260.f + FVector(0.f, 0.f, 330.f);
			Oblique.View = (Target - Oblique.Eye).Rotation();
			Oblique.Entrance = Door;
			FShot& Top = Runner->Shots.AddDefaulted_GetRef();
			Top.Name = Oblique.Name + TEXT("_top");
			Top.Eye = Door->GetActorLocation() + FVector(0.f, 0.f, 1300.f) - Out * 150.f;
			Top.View = FRotator(-89.9f, Door->GetTangent().Rotation().Yaw, 0.f);
			Top.Entrance = Door;
		}
		UE_LOG(LogCodexTactics, Display, TEXT("StoryShowcaseFixShots: %d pictures planned"), Runner->Shots.Num());
	}

	void Prepare(UWorld* World, const FShot& Shot)
	{
		APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		if (!Runner->Camera.IsValid())
		{
			Runner->Camera = World->SpawnActor<ACameraActor>(Shot.Eye, Shot.View);
		}
		ACameraActor* Camera = Runner->Camera.Get();
		Camera->SetActorLocationAndRotation(Shot.Eye, Shot.View);
		Camera->GetCameraComponent()->SetFieldOfView(60.f);
		Camera->GetCameraComponent()->bConstrainAspectRatio = false;
		if (PC && PC->GetViewTarget() != Camera)
		{
			PC->SetViewTarget(Camera);
		}
		ADoorActor* Entrance = Shot.Entrance.Get();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Entrance || !Leader)
		{
			return;
		}
		// Every storey-0 door of that building open (on its chosen side), the leader 2.5 m inside the entrance (the cutaway shows storey 0).
		if (UDoorSubsystem* Doors = World->GetSubsystem<UDoorSubsystem>())
		{
			for (ADoorActor* Door : Doors->GetDoors())
			{
				if (Door && Door->GetBuildingId() == Entrance->GetBuildingId() && Door->GetFloor() == 0 && !Door->IsOpen())
				{
					Door->ForceState(EDoorLeafState::Open);
				}
			}
		}
		const float Outward = Entrance->GetFixedSwingSign();
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		FNavLocation Inside;
		const FVector Try = Entrance->GetActorLocation() - Entrance->GetNormal() * (Outward * 250.f) + FVector(0.f, 0.f, 50.f);
		if (Nav && Nav->ProjectPointToNavigation(Try, Inside, FVector(80.f, 80.f, 120.f)))
		{
			Leader->TeleportTo(Inside.Location + FVector(0.f, 0.f, Leader->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f), Leader->GetActorRotation(), false, true);
			if (AAIController* AI = Cast<AAIController>(Leader->GetController()))
			{
				AI->StopMovement();
			}
		}
	}

	void Tick()
	{
		FRunner& R = *Runner;
		if (R.bDone)
		{
			return;
		}
		++R.Frame;
		++R.StepFrame;
		UWorld* World = FrontendSmokeUtils::FindGameWorld();
		URoofCutawaySubsystem* Roofs = World ? World->GetSubsystem<URoofCutawaySubsystem>() : nullptr;
		UDoorSubsystem* Doors = World ? World->GetSubsystem<UDoorSubsystem>() : nullptr;
		UEnvironmentSubsystem* Environment = World ? UEnvironmentSubsystem::Get(World) : nullptr;
		if (!World || !Roofs || !Doors || !Environment || !Environment->IsInitialised())
		{
			return;
		}
		if (R.Frame > 60 * 600)
		{
			Finish(false);
			return;
		}
		UWidgetLayoutLibrary::RemoveAllWidgets(World);
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0); PC && PC->GetHUD())
		{
			PC->GetHUD()->bShowHUD = false; // the game HUD off; the label overlay (debug draw) stays
		}
		if (!R.bPlanned)
		{
			if (R.StepFrame < WarmupFrames || !Doors->IsActive())
			{
				return;
			}
			Environment->SetClockScale(0.f);
			Environment->ForceWeather(EWeatherState::Clear, true);
			Environment->SetHour(13.f);
			GEngine->Exec(World, TEXT("DisableAllScreenMessages"));
			GEngine->Exec(World, TEXT("r.MotionBlurQuality 0"));
			R.MapName = World->GetMapName().Replace(TEXT("UEDPIE_0_"), TEXT(""));
			if (USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>())
			{
				if (!Squad->IsSoloMode())
				{
					Squad->EnterSoloMode();
				}
				Squad->SetFollowersHolding(true);
			}
			Plan(World, *Roofs, *Doors);
			R.bPlanned = true;
			R.Index = -1;
			R.StepFrame = SettleFrames; // start at once
		}
		if (R.StepFrame == SettleFrames)
		{
			++R.Index;
			if (R.Index >= R.Shots.Num())
			{
				const UStoryLabelSubsystem* Labels = World->GetSubsystem<UStoryLabelSubsystem>();
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: labels %d houses / %d doors: %s"), Labels ? Labels->GetHouseLabelCount() : 0, Labels ? Labels->GetDoorLabelCount() : 0,
					Labels ? *FString::Join(Labels->GetLabelTexts(true), TEXT(" | ")) : TEXT("-"));
				Finish(Labels && Labels->GetHouseLabelCount() > 0);
				return;
			}
			Prepare(World, R.Shots[R.Index]);
			R.StepFrame = 0;
			return;
		}
		if (R.StepFrame == SettleFrames - 10)
		{
			const FString File = R.Dir / FString::Printf(TEXT("%s_%s.png"), *R.MapName, *R.Shots[R.Index].Name);
			FScreenshotRequest::RequestScreenshot(File, /*bInShowUI*/ true, /*bAddFilenameSuffix*/ false);
			R.Written.Add(File);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		Runner = MakeShared<FRunner>();
		Runner->Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("StoryShowcase") / TEXT("fixes"));
		for (const FString& Arg : Args)
		{
			if (Arg.StartsWith(TEXT("dir=")))
			{
				Runner->Dir = Arg.Mid(4); // no spaces (-ExecCmds splits on them): the default is Saved/StoryShowcase/fixes
			}
		}
		IFileManager::Get().MakeDirectory(*Runner->Dir, true);
		SmokeUtils::StopLevelPmc(World);
		if (IConsoleVariable* Pressure = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Pressure.Enabled")))
		{
			Pressure->Set(0, ECVF_SetByConsole);
		}
		if (IConsoleVariable* Doors = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Doors.Openable")))
		{
			Doors->Set(1, ECVF_SetByConsole);
		}
		Runner->EndFrameHandle = FCoreDelegates::OnEndFrame.AddStatic(&Tick);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.StoryShowcaseFixShots"),
		TEXT("Dev pictures (designer 2026-11-22 items 3 / 6, needs rendering): the story / door labels over the street and each house, and every entrance's leaf ")
		TEXT("swing from above (doors on, storey 0 forced open). [dir=<abs>]. Writes PNGs, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
