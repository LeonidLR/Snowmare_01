// Dev-only in-engine video capture (game designer 2026-10-24: short clips of the tutorial level's start, the zombie encounter and the
// final fight). Renders every frame to a PNG (the game's own view with its HUD; never a desktop capture) while a "director" keeps the
// tactical camera on the action; ffmpeg turns the frames into a clip. Run rendered, on a fixed step so the frames play at real speed:
//   UnrealEditor.exe <uproject> /Game/Maps/L_Demo01_A_FrozenLake -game -RenderOffscreen -ResX=1280 -ResY=720 -nosound -benchmark -fps=30
//     -LevelJson=L_Demo01_A_FrozenLake.json -EnvWeather=Snowfall -EnvHour=13
//     -ExecCmds="CodexTactics.ClipRecorder start intro seconds=40"                       (the level start, driven by the recorder)
//     -ExecCmds="CodexTactics.ClipRecorder a4,CodexTactics.TutorialAmbushLakeSmoke"     (another check drives the game; it exits it)
// Frames: Saved/Screenshots/Clips/<name>/f_NNNNN.png; every frame logs "ClipRecorder <name> f=N" (to cut a clip at a logged event).
// Args: <name> [intro] [seconds=N] (stop + exit after N game seconds; default: until the game exits) [nodirector].
// "intro": reads the start dialogue at a comfortable pace (a line every 3.5 s), then walks the squad (the leader's move orders, the
// others follow) to the tutorial's current navigation target, re-ordering as the target moves on.
// Director: the camera looks at the operative closest to an awake enemy within 35 m, two fifths of the way to that enemy (else the
// leader), smoothed. UE-only, no Godot reference.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/TacticalCameraPawn.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Tutorial/TutorialSubsystem.h"
#include "UI/DialogueSubsystem.h"
#include "UnrealClient.h"

namespace ClipRecorder
{
	struct FState
	{
		FString Name;
		FString Dir;
		bool bIntro = false;
		bool bDirector = true;
		float MaxSeconds = -1.f;
		float Time = 0.f;
		int32 Frame = 0;
		float LineTimer = 0.f;
		float OrderTimer = 0.f;
		FVector LastGuide = FVector::ZeroVector;
		bool bHasLook = false;
		FVector Look = FVector::ZeroVector;
		TWeakObjectPtr<UWorld> World;
	};

	bool IsAwakeEnemy(const AEnemyCharacter* Enemy)
	{
		const UHealthComponent* Health = Enemy ? Enemy->GetHealthComponent() : nullptr;
		return Enemy && !Enemy->IsHidden() && (!Health || Health->IsAlive());
	}

	void Direct(FState& State, UWorld* World, float Delta)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		APlayerController* PC = World->GetFirstPlayerController();
		ATacticalCameraPawn* Camera = PC ? Cast<ATacticalCameraPawn>(PC->GetPawn()) : nullptr;
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Camera || !Leader)
		{
			return;
		}
		const AOperativeCharacter* Hero = Leader;
		const AEnemyCharacter* Foe = nullptr;
		float Best = 3500.f;
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			if (!Member || (Member->HealthComponent && !Member->HealthComponent->IsAlive()))
			{
				continue;
			}
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				const float Distance = IsAwakeEnemy(*It) ? FVector::Dist2D(Member->GetActorLocation(), It->GetActorLocation()) : TNumericLimits<float>::Max();
				if (Distance < Best)
				{
					Best = Distance;
					Hero = Member;
					Foe = *It;
				}
			}
		}
		const FVector Target = Foe ? FMath::Lerp(Hero->GetActorLocation(), Foe->GetActorLocation(), 0.4f) : Hero->GetActorLocation();
		State.Look = State.bHasLook ? FMath::Lerp(State.Look, Target, FMath::Min(1.f, Delta * 2.f)) : Target;
		State.bHasLook = true;
		Camera->LookAtPoint(State.Look);
	}

	void DriveIntro(FState& State, UWorld* World, float Delta)
	{
		UDialogueSubsystem* Dialogue = World->GetSubsystem<UDialogueSubsystem>();
		if (Dialogue && Dialogue->IsDialogueOpen())
		{
			State.LineTimer += Delta;
			if (State.LineTimer >= 3.5f)
			{
				State.LineTimer = 0.f;
				Dialogue->AdvanceLine();
			}
			return;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UTutorialSubsystem* Tutorial = World->GetSubsystem<UTutorialSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Leader || !Tutorial || !Tutorial->GetGuideView().bValid)
		{
			return;
		}
		State.OrderTimer -= Delta;
		const FVector Guide = Tutorial->GetGuideView().Location;
		if (State.OrderTimer <= 0.f || FVector::Dist2D(Guide, State.LastGuide) > 100.f)
		{
			State.OrderTimer = 4.f;
			State.LastGuide = Guide;
			if (FVector::Dist2D(Leader->GetActorLocation(), Guide) > 250.f)
			{
				Leader->OrderMoveTo(Guide, false);
			}
		}
	}

	bool Tick(FState& State, float Delta)
	{
		UWorld* World = State.World.Get();
		if (!World)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("ClipRecorder %s: world gone, %d frames"), *State.Name, State.Frame);
			return false;
		}
		State.Time += Delta;
		if (State.MaxSeconds > 0.f && State.Time > State.MaxSeconds)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("ClipRecorder %s: done, %d frames -> %s"), *State.Name, State.Frame, *State.Dir);
			FPlatformMisc::RequestExit(false, TEXT("ClipRecorder"));
			return false;
		}
		if (State.bIntro)
		{
			DriveIntro(State, World, Delta);
		}
		if (State.bDirector)
		{
			Direct(State, World, Delta);
		}
		FScreenshotRequest::RequestScreenshot(State.Dir / FString::Printf(TEXT("f_%05d.png"), State.Frame), /*bShowUI*/ true, false);
		UE_LOG(LogCodexTactics, Display, TEXT("ClipRecorder %s f=%d t=%.2f"), *State.Name, State.Frame, State.Time);
		++State.Frame;
		return true;
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.IsEmpty() || !FApp::CanEverRender())
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("ClipRecorder: needs a name and a rendered game (no -nullrhi)"));
			return;
		}
		TSharedRef<FState> State = MakeShared<FState>();
		State->Name = Args[0];
		State->Dir = FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("Clips") / State->Name;
		State->World = World;
		for (const FString& Arg : Args)
		{
			State->bIntro |= Arg == TEXT("intro");
			State->bDirector &= Arg != TEXT("nodirector");
			if (Arg.StartsWith(TEXT("seconds=")))
			{
				State->MaxSeconds = FCString::Atof(*Arg.RightChop(8));
			}
		}
		UE_LOG(LogCodexTactics, Display, TEXT("ClipRecorder %s: recording -> %s (intro %d, director %d, %.0f s)"), *State->Name, *State->Dir,
			State->bIntro ? 1 : 0, State->bDirector ? 1 : 0, State->MaxSeconds);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float Delta)
		{
			return Tick(*State, Delta);
		}));
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.ClipRecorder"),
		TEXT("Dev (2026-10-24): renders every frame to Saved/Screenshots/Clips/<name>/f_NNNNN.png with a director camera on the action. "
			"Args: <name> [intro] [seconds=N] [nodirector]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
