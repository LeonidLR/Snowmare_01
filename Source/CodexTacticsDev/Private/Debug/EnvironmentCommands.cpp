#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "CodexTactics.h"
#include "Engine/World.h"
#include "Environment/EnvironmentRules.h"
#include "Environment/EnvironmentSubsystem.h"
#include "HAL/IConsoleManager.h"

// Dev console of the day cycle / weather (docs/systems/environment-weather.md, phase 1): Codex.Env.Dump / SetWeather / SetHour /
// ClockScale / Seed / SnowCover. Not smokes (nothing is verified); test_map.json lists them under nonSmokeDebugCommands.
namespace EnvironmentCommands
{
	UEnvironmentSubsystem* Env(const UWorld* World, const TCHAR* Command)
	{
		UEnvironmentSubsystem* Environment = World ? World->GetSubsystem<UEnvironmentSubsystem>() : nullptr;
		if (!Environment)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("%s: no environment subsystem (not a game world)"), Command);
		}
		return Environment;
	}

	void Dump(const TArray<FString>& Args, UWorld* World)
	{
		if (const UEnvironmentSubsystem* Environment = Env(World, TEXT("Codex.Env.Dump")))
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Codex.Env.Dump: %s; can advance: %s"), *Environment->Describe(), Environment->CanAdvance() ? TEXT("yes") : TEXT("no"));
		}
	}

	void SetWeather(const TArray<FString>& Args, UWorld* World)
	{
		EWeatherState Weather = EWeatherState::Overcast;
		if (Args.Num() < 1 || !EnvironmentRules::WeatherFromString(Args[0], Weather))
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("Codex.Env.SetWeather <Clear|Overcast|Snowfall|Blizzard> [instant]"));
			return;
		}
		const bool bInstant = Args.Num() >= 2 && (Args[1].Equals(TEXT("instant"), ESearchCase::IgnoreCase) || Args[1] == TEXT("1") || Args[1].Equals(TEXT("true"), ESearchCase::IgnoreCase));
		if (UEnvironmentSubsystem* Environment = Env(World, TEXT("Codex.Env.SetWeather")))
		{
			Environment->ForceWeather(Weather, bInstant);
		}
	}

	void SetHour(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 1 || !Args[0].IsNumeric())
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("Codex.Env.SetHour <hour 0-24, e.g. 22.5>"));
			return;
		}
		if (UEnvironmentSubsystem* Environment = Env(World, TEXT("Codex.Env.SetHour")))
		{
			Environment->SetHour(FCString::Atof(*Args[0]));
		}
	}

	void ClockScale(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 1 || !Args[0].IsNumeric())
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("Codex.Env.ClockScale <multiplier, 1 = design speed, 0 = frozen>"));
			return;
		}
		if (UEnvironmentSubsystem* Environment = Env(World, TEXT("Codex.Env.ClockScale")))
		{
			Environment->SetClockScale(FCString::Atof(*Args[0]));
		}
	}

	void Seed(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 1 || !Args[0].IsNumeric())
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("Codex.Env.Seed <n> (0 = random): restarts the weather sequence from that seed at the current hour"));
			return;
		}
		if (UEnvironmentSubsystem* Environment = Env(World, TEXT("Codex.Env.Seed")))
		{
			Environment->RestartWeatherSequence(FCString::Atoi(*Args[0]));
		}
	}

	void SnowCover(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 1 || !Args[0].IsNumeric())
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("Codex.Env.SnowCover <0..1>: sets the persistent snow cover (debug; may lower it)"));
			return;
		}
		if (UEnvironmentSubsystem* Environment = Env(World, TEXT("Codex.Env.SnowCover")))
		{
			Environment->SetSnowAmount(FCString::Atof(*Args[0]));
			UE_LOG(LogCodexTactics, Display, TEXT("Codex.Env.SnowCover: %s"), *Environment->Describe());
		}
	}

	static FAutoConsoleCommandWithWorldAndArgs CommandSnowCover(TEXT("Codex.Env.SnowCover"),
		TEXT("Codex.Env.SnowCover <0..1>: sets the persistent snow cover (FEnvironmentState::SnowAmount; it then grows while it snows, never recedes)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SnowCover));

	static FAutoConsoleCommandWithWorldAndArgs CommandDump(TEXT("Codex.Env.Dump"),
		TEXT("Logs the environment state: hour, phase, weather -> next, blend, dwell, seed, locked, clock scale."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Dump));

	static FAutoConsoleCommandWithWorldAndArgs CommandSetWeather(TEXT("Codex.Env.SetWeather"),
		TEXT("Codex.Env.SetWeather <Clear|Overcast|Snowfall|Blizzard> [instant]: blends to that weather now (instant: in force at once)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetWeather));

	static FAutoConsoleCommandWithWorldAndArgs CommandSetHour(TEXT("Codex.Env.SetHour"),
		TEXT("Codex.Env.SetHour <h>: sets the game clock (0-24)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetHour));

	static FAutoConsoleCommandWithWorldAndArgs CommandClockScale(TEXT("Codex.Env.ClockScale"),
		TEXT("Codex.Env.ClockScale <x>: speeds the clock and the weather up (1 = design speed)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ClockScale));

	static FAutoConsoleCommandWithWorldAndArgs CommandSeed(TEXT("Codex.Env.Seed"),
		TEXT("Codex.Env.Seed <n>: restarts the weather sequence with that seed (0 = random)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Seed));
}

#endif // !UE_BUILD_SHIPPING
