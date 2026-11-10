// Dev-only in-engine video capture (game designer 2026-10-24: short clips of the tutorial level's start, the zombie encounter and the
// final fight). Renders every frame to a PNG (the game's own view with its HUD; never a desktop capture) while a "director" keeps the
// tactical camera on the action; ffmpeg turns the frames into a clip. Run rendered, on a fixed step so the frames play at real speed:
//   UnrealEditor.exe <uproject> /Game/Maps/L_Demo01_A_FrozenLake -game -RenderOffscreen -ResX=1280 -ResY=720 -nosound -benchmark -fps=30
//     -LevelJson=L_Demo01_A_FrozenLake.json -EnvWeather=Snowfall -EnvHour=13
//     -ExecCmds="CodexTactics.ClipRecorder start intro seconds=40"                       (the level start, driven by the recorder)
//     -ExecCmds="CodexTactics.ClipRecorder a4,CodexTactics.TutorialAmbushLakeSmoke"     (another check drives the game; it exits it)
// Frames: Saved/Screenshots/Clips/<name>/f_NNNNN.jpg; every frame logs "ClipRecorder <name> f=N" (to cut a clip at a logged event).
// Args: <name> [intro] [seconds=N] (stop + exit after N game seconds; default: until the game exits) [nodirector] [format=jpg|png].
// format (fix/arena-clip-readability 2026-11-10, the designer approved cheaper clips): jpg (default) = JPEG quality 90, at most 1280x720
// (a larger viewport is scaled down), written from UGameViewportClient::OnScreenshotCaptured (the engine writes no PNG meanwhile; any
// other screenshot request while recording is still saved under its own name); png = the engine's own PNG writer, full size, as before.
// "intro": reads the start dialogue at a comfortable pace (a line every 3.5 s), then walks the squad (the leader's move orders, the
// others follow) to the tutorial's current navigation target, re-ordering as the target moves on.
// Director: the camera looks at the operative closest to an awake enemy within 35 m, two fifths of the way to that enemy but at most 7 m
// off him (2026-11-02: a far foe pushed the squad off the frame; else the leader), smoothed.
// The level generator's bot runs (2026-11-02): Scripts/levelgen_record_run.ps1 runs it with the bot's route mode (-CodexBot -BotRoute=1)
// and Scripts/Tools/clip_assemble.py encodes the frames while the game records (the full run sped up + fight clips cut at the bot's
// "[Bot] Route: fight at" lines placed by the f=N lines; clips.json). UE-only, no Godot reference.

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
#include "Engine/GameViewportClient.h"
#include "ImageUtils.h"
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
		/** format=jpg (default): the frames go through OnScreenshotCaptured as JPEG; png: the engine writes them. */
		bool bJpeg = true;
		FDelegateHandle CaptureHandle;
	};

	/** JPEG frames: quality 90, at most this size (a larger viewport is scaled down, the aspect kept). */
	constexpr int32 JpegQuality = 90;
	constexpr int32 MaxWidth = 1280;
	constexpr int32 MaxHeight = 720;

	/**
	 * OnScreenshotCaptured while recording in jpg: the requested file (its own extension decides the format: a frame is .jpg) written
	 * from the pixels, scaled down to MaxWidth x MaxHeight when larger. Fires instead of the engine's file writer.
	 */
	void SaveCaptured(int32 Width, int32 Height, const TArray<FColor>& Pixels)
	{
		const FString File = FScreenshotRequest::GetFilename();
		if (File.IsEmpty() || Width <= 0 || Height <= 0 || Pixels.Num() < Width * Height)
		{
			return;
		}
		const float Scale = FMath::Min3(1.f, static_cast<float>(MaxWidth) / Width, static_cast<float>(MaxHeight) / Height);
		if (Scale < 1.f)
		{
			const int32 OutWidth = FMath::Max(2, FMath::RoundToInt(Width * Scale) & ~1);
			const int32 OutHeight = FMath::Max(2, FMath::RoundToInt(Height * Scale) & ~1);
			TArray<FColor> Scaled;
			Scaled.SetNumUninitialized(OutWidth * OutHeight);
			FImageUtils::ImageResize(Width, Height, Pixels, OutWidth, OutHeight, Scaled, false, true);
			FImageUtils::SaveImageByExtension(*File, FImageView(Scaled.GetData(), OutWidth, OutHeight), JpegQuality);
			return;
		}
		FImageUtils::SaveImageByExtension(*File, FImageView(Pixels.GetData(), Width, Height), JpegQuality);
	}

	void StopCapture(FState& State)
	{
		if (State.CaptureHandle.IsValid())
		{
			UGameViewportClient::OnScreenshotCaptured().Remove(State.CaptureHandle);
			State.CaptureHandle.Reset();
		}
	}

	/** The director's look leads from the operative towards the foe at most this far, cm. */
	constexpr float MaxLeadCm = 700.f;

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
		// Offscreen there is no real cursor: GetMousePosition reports (0, 0), a screen edge, and the camera edge-scrolls away.
		Camera->Config.EdgeScrollMargin = -1.f;
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
		// Two fifths of the way to the foe, but never more than MaxLeadCm off the operative: a foe 25-35 m away pushed the squad off the frame.
		FVector Target = Hero->GetActorLocation();
		if (Foe)
		{
			const FVector Lead = (Foe->GetActorLocation() - Hero->GetActorLocation()) * 0.4f;
			Target += Lead.GetClampedToMaxSize(MaxLeadCm);
		}
		State.Look = State.bHasLook ? FMath::Lerp(State.Look, Target, FMath::Min(1.f, Delta * 2.f)) : Target;
		State.bHasLook = true;
		Camera->LookAtPoint(State.Look);
		if (State.Frame % 60 == 0)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("ClipRecorder %s director: look %s, camera %s, view centre %s, leader %s, foe %s"), *State.Name,
				*State.Look.ToCompactString(), *Camera->GetActorLocation().ToCompactString(), *Camera->GetViewCentre().ToCompactString(),
				*Leader->GetActorLocation().ToCompactString(), Foe ? *Foe->GetName() : TEXT("none"));
		}
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
			StopCapture(State);
			return false;
		}
		State.Time += Delta;
		if (State.MaxSeconds > 0.f && State.Time > State.MaxSeconds)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("ClipRecorder %s: done, %d frames -> %s"), *State.Name, State.Frame, *State.Dir);
			StopCapture(State);
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
		FScreenshotRequest::RequestScreenshot(State.Dir / FString::Printf(TEXT("f_%05d.%s"), State.Frame, State.bJpeg ? TEXT("jpg") : TEXT("png")), /*bShowUI*/ true, false);
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
			if (Arg.StartsWith(TEXT("format="), ESearchCase::IgnoreCase))
			{
				State->bJpeg = !Arg.RightChop(7).Equals(TEXT("png"), ESearchCase::IgnoreCase);
			}
			if (Arg.StartsWith(TEXT("seconds=")))
			{
				State->MaxSeconds = FCString::Atof(*Arg.RightChop(8));
			}
		}
		if (State->bJpeg)
		{
			IFileManager::Get().MakeDirectory(*State->Dir, true);
			State->CaptureHandle = UGameViewportClient::OnScreenshotCaptured().AddStatic(&SaveCaptured);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("ClipRecorder %s: recording -> %s (intro %d, director %d, %.0f s, %s)"), *State->Name, *State->Dir,
			State->bIntro ? 1 : 0, State->bDirector ? 1 : 0, State->MaxSeconds, State->bJpeg ? TEXT("jpg q90 <= 1280x720") : TEXT("png"));
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float Delta)
		{
			return Tick(*State, Delta);
		}));
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.ClipRecorder"),
		TEXT("Dev (2026-10-24): renders every frame to Saved/Screenshots/Clips/<name>/f_NNNNN.jpg (format=png: .png) with a director camera on the action. "
			"Args: <name> [intro] [seconds=N] [nodirector] [format=jpg|png]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
