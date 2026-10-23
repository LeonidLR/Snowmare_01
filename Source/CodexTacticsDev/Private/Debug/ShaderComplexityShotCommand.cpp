// Dev-only capture: the same view in Lit and in the Shader Complexity view mode, under forced weather (needs rendering, not -nullrhi):
//   UnrealEditor.exe CodexTactics.uproject <map> -game -RenderOffscreen -ResX=1920 -ResY=1080 -nosound -benchmark -fps=30
//     -ExecCmds="CodexTactics.ComplexityShot [Blizzard|Snowfall|Overcast|Clear] [snow 0..1]"
// Writes Saved/Screenshots/Environment/complexity/<map>_<weather>_{lit,complexity}.png and a Clear / no-snow complexity baseline, then exits.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "ShowFlags.h"
#include "Engine/World.h"
#include "Environment/EnvironmentRules.h"
#include "Environment/EnvironmentSubsystem.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace ComplexityShot
{
	struct FStep
	{
		float AtSeconds = 0.f;
		TFunction<void(UWorld*)> Run;
	};

	struct FRun
	{
		TWeakObjectPtr<UWorld> World;
		TArray<FStep> Steps;
		int32 Next = 0;
		double Start = 0.0;
	};

	FString ShotPath(UWorld* World, const FString& Name)
	{
		return FPaths::ProjectSavedDir() / TEXT("Screenshots/Environment/complexity") / (World->GetMapName() + TEXT("_") + Name + TEXT(".png"));
	}

	/** The viewport handles "viewmode", not GEngine->Exec: switch it directly (Lit / ShaderComplexity). */
	void SetViewMode(UWorld* World, EViewModeIndex Mode)
	{
		UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
		if (!Viewport)
		{
			return;
		}
		Viewport->ViewModeIndex = Mode;
		ApplyViewMode(Mode, /*bPerspective*/ true, Viewport->EngineShowFlags);
	}

	void SetConditions(UWorld* World, EWeatherState Weather, float Snow)
	{
		if (UEnvironmentSubsystem* Env = UEnvironmentSubsystem::Get(World))
		{
			Env->ForceWeather(Weather, /*bInstant*/ true);
			Env->SetHour(12.f);
			Env->SetSnowAmount(Snow);
		}
	}

	void Start(const TArray<FString>& Args, UWorld* World)
	{
		EWeatherState Weather = EWeatherState::Blizzard;
		if (Args.Num() > 0)
		{
			EnvironmentRules::WeatherFromString(Args[0], Weather);
		}
		const float Snow = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 0.7f;
		const FString Tag = EnvironmentRules::WeatherToString(Weather);

		TSharedRef<FRun> Run = MakeShared<FRun>();
		Run->World = World;
		Run->Start = FPlatformTime::Seconds();
		// Let the level, the presenter and the snow overlays settle before every capture (overlay rescans run every 3 s).
		Run->Steps.Add({ 6.f, [=](UWorld* W) { SetConditions(W, Weather, Snow); } });
		Run->Steps.Add({ 14.f, [=](UWorld* W) { FScreenshotRequest::RequestScreenshot(ShotPath(W, Tag + TEXT("_lit")), true, false); } });
		Run->Steps.Add({ 16.f, [=](UWorld* W) { SetViewMode(W, VMI_ShaderComplexity); } });
		Run->Steps.Add({ 18.f, [=](UWorld* W) { FScreenshotRequest::RequestScreenshot(ShotPath(W, Tag + TEXT("_complexity")), true, false); } });
		Run->Steps.Add({ 20.f, [=](UWorld* W) { SetViewMode(W, VMI_Lit); SetConditions(W, EWeatherState::Clear, 0.f); } });
		Run->Steps.Add({ 28.f, [=](UWorld* W) { SetViewMode(W, VMI_ShaderComplexity); } });
		Run->Steps.Add({ 30.f, [=](UWorld* W) { FScreenshotRequest::RequestScreenshot(ShotPath(W, TEXT("ClearNoSnow_complexity")), true, false); } });
		Run->Steps.Add({ 33.f, [=](UWorld* W) { UKismetSystemLibrary::QuitGame(W, nullptr, EQuitPreference::Quit, false); } });

		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Run](float) -> bool
		{
			UWorld* W = Run->World.Get();
			if (!W)
			{
				return false;
			}
			const double Elapsed = FPlatformTime::Seconds() - Run->Start;
			while (Run->Steps.IsValidIndex(Run->Next) && Elapsed >= Run->Steps[Run->Next].AtSeconds)
			{
				Run->Steps[Run->Next++].Run(W);
			}
			return Run->Steps.IsValidIndex(Run->Next);
		}));
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.ComplexityShot"),
		TEXT("Captures Lit + Shader Complexity of the current view under forced weather, plus a Clear / no-snow complexity baseline, then quits. Args: [weather] [snow]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Start));
}

#endif
