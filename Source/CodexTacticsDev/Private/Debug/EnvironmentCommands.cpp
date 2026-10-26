#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "CodexTactics.h"
#include "Engine/World.h"
#include "Environment/EnvironmentRules.h"
#include "Environment/EnvironmentSubsystem.h"
#include "HAL/IConsoleManager.h"

// Dev console of the day cycle / weather (docs/systems/environment-weather.md, phase 1): Codex.Env.Dump / SetWeather / SetHour /
// ClockScale / Seed / SnowCover / ObjectSnow / WindFrom; section 17: Climate (and SetWeather takes the temperate names). Not smokes (nothing is verified); test_map.json lists them under nonSmokeDebugCommands.
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
		UEnvironmentSubsystem* Environment = Env(World, TEXT("Codex.Env.SetWeather"));
		// Section 17: on a temperate level its own names too (Sunny / SunnyDay / Rain / Storm).
		const EClimateProfile Climate = Environment ? Environment->GetClimate() : EClimateProfile::Arctic;
		EWeatherState Weather = EWeatherState::Overcast;
		if (Args.Num() < 1 || !EnvironmentRules::WeatherFromString(Climate, Args[0], Weather))
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("Codex.Env.SetWeather <Clear|Overcast|Snowfall|Blizzard> [instant]; on a temperate level also <Sunny|SunnyDay|Rain|Storm> (climate now %s)"),
				*EnvironmentRules::ClimateToString(Climate));
			return;
		}
		const bool bInstant = Args.Num() >= 2 && (Args[1].Equals(TEXT("instant"), ESearchCase::IgnoreCase) || Args[1] == TEXT("1") || Args[1].Equals(TEXT("true"), ESearchCase::IgnoreCase));
		if (Environment)
		{
			Environment->ForceWeather(Weather, bInstant);
		}
	}

	void SwitchClimate(const TArray<FString>& Args, UWorld* World)
	{
		EClimateProfile Climate = EClimateProfile::Arctic;
		if (Args.Num() < 1 || !EnvironmentRules::ClimateFromString(Args[0], Climate))
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("Codex.Env.Climate <arctic|temperate>: switches the climate profile (debug; the weather starts over, the hour continues)"));
			return;
		}
		if (UEnvironmentSubsystem* Environment = Env(World, TEXT("Codex.Env.Climate")))
		{
			Environment->SetClimate(Climate);
			UE_LOG(LogCodexTactics, Display, TEXT("Codex.Env.Climate: %s"), *Environment->Describe());
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

	void ObjectSnow(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 5)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("Codex.Env.ObjectSnow <top> <px> <nx> <py> <ny>: sets the objects' directional snow pockets (0..1, world axes)"));
			return;
		}
		if (UEnvironmentSubsystem* Environment = Env(World, TEXT("Codex.Env.ObjectSnow")))
		{
			FDirectionalSnow Pockets;
			for (int32 Index = 0; Index < FDirectionalSnow::PocketCount; ++Index)
			{
				Pockets.Set(Index, FCString::Atof(*Args[Index]));
			}
			Environment->SetObjectSnow(Pockets);
			UE_LOG(LogCodexTactics, Display, TEXT("Codex.Env.ObjectSnow: %s"), *Environment->Describe());
		}
	}

	void WindFrom(const TArray<FString>& Args, UWorld* World)
	{
		UEnvironmentSubsystem* Environment = Env(World, TEXT("Codex.Env.WindFrom"));
		if (!Environment)
		{
			return;
		}
		if (Args.Num() < 1 || Args[0].Equals(TEXT("off"), ESearchCase::IgnoreCase))
		{
			Environment->ClearWindDirectionOverride();
			UE_LOG(LogCodexTactics, Display, TEXT("Codex.Env.WindFrom: off (the presenter's drifting wind)"));
			return;
		}
		// The wind comes FROM this side: +X / -X / +Y / -Y or a yaw in degrees; it travels the opposite way.
		FVector From = FVector::ZeroVector;
		const FString& Arg = Args[0];
		if (Arg.Equals(TEXT("+X"), ESearchCase::IgnoreCase) || Arg.Equals(TEXT("X"), ESearchCase::IgnoreCase)) { From = FVector(1.0, 0.0, 0.0); }
		else if (Arg.Equals(TEXT("-X"), ESearchCase::IgnoreCase)) { From = FVector(-1.0, 0.0, 0.0); }
		else if (Arg.Equals(TEXT("+Y"), ESearchCase::IgnoreCase) || Arg.Equals(TEXT("Y"), ESearchCase::IgnoreCase)) { From = FVector(0.0, 1.0, 0.0); }
		else if (Arg.Equals(TEXT("-Y"), ESearchCase::IgnoreCase)) { From = FVector(0.0, -1.0, 0.0); }
		else if (Arg.IsNumeric()) { From = FRotator(0.f, FCString::Atof(*Arg), 0.f).Vector(); }
		if (From.IsNearlyZero())
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("Codex.Env.WindFrom <+X|-X|+Y|-Y|yaw|off>: the side the wind comes from (debug override of the drifting wind)"));
			return;
		}
		Environment->SetWindDirectionOverride(-From);
		UE_LOG(LogCodexTactics, Display, TEXT("Codex.Env.WindFrom %s: wind travels %s"), *Arg, *Environment->GetWindDirection().ToCompactString());
	}

	static FAutoConsoleCommandWithWorldAndArgs CommandObjectSnow(TEXT("Codex.Env.ObjectSnow"),
		TEXT("Codex.Env.ObjectSnow <top> <px> <nx> <py> <ny>: sets the objects' directional snow pockets (FEnvironmentState::ObjectSnow, world axes, 0..1)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ObjectSnow));

	static FAutoConsoleCommandWithWorldAndArgs CommandWindFrom(TEXT("Codex.Env.WindFrom"),
		TEXT("Codex.Env.WindFrom <+X|-X|+Y|-Y|yaw deg|off>: the wind comes from that side (debug override of the presenter's drifting wind; directional snow, flakes, MPC)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&WindFrom));

	static FAutoConsoleCommandWithWorldAndArgs CommandWetness(TEXT("Codex.Env.Wetness"),
		TEXT("Codex.Env.Wetness <0..1> [puddles 0..1]: sets the ground wetness and the puddles of a temperate level (section 18, preview / tuning of the wet look; the weather keeps stepping them)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() < 1)
			{
				UE_LOG(LogCodexTactics, Warning, TEXT("Codex.Env.Wetness <0..1> [puddles 0..1]"));
				return;
			}
			if (UEnvironmentSubsystem* Environment = Env(World, TEXT("Codex.Env.Wetness")))
			{
				const float Wetness = FCString::Atof(*Args[0]);
				Environment->SetWetness(Wetness, Args.Num() > 1 ? FCString::Atof(*Args[1]) : Environment->GetPuddleAmount());
				UE_LOG(LogCodexTactics, Display, TEXT("Codex.Env.Wetness: %s"), *Environment->Describe());
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs CommandSnowCover(TEXT("Codex.Env.SnowCover"),
		TEXT("Codex.Env.SnowCover <0..1>: sets the persistent snow cover (FEnvironmentState::SnowAmount; it then grows while it snows, never recedes)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SnowCover));

	static FAutoConsoleCommandWithWorldAndArgs CommandDump(TEXT("Codex.Env.Dump"),
		TEXT("Logs the environment state: hour, phase, weather -> next, blend, dwell, seed, locked, clock scale."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Dump));

	static FAutoConsoleCommandWithWorldAndArgs CommandSetWeather(TEXT("Codex.Env.SetWeather"),
		TEXT("Codex.Env.SetWeather <Clear|Overcast|Snowfall|Blizzard> [instant]: blends to that weather now (instant: in force at once). A temperate level also takes Sunny|SunnyDay|Rain|Storm."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetWeather));

	static FAutoConsoleCommandWithWorldAndArgs CommandClimate(TEXT("Codex.Env.Climate"),
		TEXT("Codex.Env.Climate <arctic|temperate>: switches the climate profile at runtime (debug, section 17): the weather / snow / wetness start over for it, the hour continues."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SwitchClimate));

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
