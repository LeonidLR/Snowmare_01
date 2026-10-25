// Dev-only console commands for headless checks of the environment VISUALS (AWeatherPresenterActor, environment phase 3, UE-only;
// docs/systems/environment-weather.md §11). Work under -nullrhi (nothing is rendered; the component values are checked):
//   Scripts/smoke.ps1 -Command CodexTactics.WeatherSmoke     -Map /Game/Maps/L_MovementTest
//   Scripts/smoke.ps1 -Command CodexTactics.WeatherLakeSmoke -Map /Game/Maps/L_Demo01_A_FrozenLake -Extra "-LevelJson=L_Demo01_A_FrozenLake.json"
// WeatherSmoke:
//   1. the presenter was spawned (no map placement) and adopted the map's lights (logged: sun, sky atmosphere, sky light, height fog,
//      post-process, moon own / adopted), the volumetric clouds are switched off;
//   2. SetHour 12 -> the sun is above the horizon (rotation pitch < 0), lit, the moon hidden; SetHour 23 -> the sun hidden below the
//      horizon, the moon lit above it;
//   3. ForceWeather Clear / Blizzard (instant) -> the fog density follows the presets, Blizzard > Clear, volumetric fog on in Blizzard;
//   S. phase 4 snow cover: the presenter has its snow components and MPC_Environment; a forced Blizzard at ClockScale 20 raises
//      FEnvironmentState::SnowAmount by the Blizzard rate, a forced Clear holds it (never recedes), the MPC SnowAmount equals the state;
//      section 15 directional snow (objects, world axes): with the wind FROM +X the blizzard fills the top + the +X pocket by the rate x the
//      wind strength and leaves -X / +Y / -Y bare (MPC SnowPX = the state); in Clear at 12:00 the top melts down to exactly
//      object_snow_melt_floor x the ground cover and the +X pocket to 0 while the ground cover stays (MPC SnowTop = the state);
//   4. Codex.Env.Visuals 0 -> every authored value is back (sun, moon, fog, post-process off, clouds as authored, MPC SnowAmount 0);
//      1 -> applied again.
// WeatherLakeSmoke: the Demo 01 lake locked at 23:00 Snowfall -> night (sun hidden, moon lit, full night grade) + the Snowfall fog,
// snow cover 0.6 (level JSON "snow_cover") in the state and the MPC.
// Both print "[WeatherSmoke] PASS/FAIL ..." and "Smoke RESULT: PASS|FAIL", then exit.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "CodexTactics.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Containers/Ticker.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Environment/EnvironmentRules.h"
#include "Environment/EnvironmentSubsystem.h"
#include "Environment/WeatherPresenterActor.h"
#include "Environment/WeatherSnowFxComponent.h"
#include "Environment/WeatherSnowOverlayComponent.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialParameterCollection.h"

namespace WeatherSmoke
{
	constexpr float StepSeconds = 0.05f;
	/** Game seconds to wait for the presenter's first application (the minimap hold is at most 4 s). */
	constexpr float ApplyTimeoutSeconds = 12.f;
	/** Game seconds after a Codex.Env.Visuals toggle (the presenter polls it every frame). */
	constexpr float ToggleSettleSeconds = 0.25f;
	/** Debug clock speed and game seconds of the snow cover stages (x20: 1.5 s = 30 design seconds of Blizzard). */
	constexpr float SnowClockScale = 20.f;
	constexpr float SnowGrowSeconds = 1.5f;
	constexpr float SnowHoldSeconds = 1.f;
	const FName MpcSnowAmount(TEXT("SnowAmount"));
	const FName MpcIsSnowing(TEXT("IsSnowing"));

	enum class EStage : uint8
	{
		WaitApplying,
		SnowGrow,
		SnowHold,
		VisualsOff,
		VisualsOn,
	};

	struct FState
	{
		EStage Stage = EStage::WaitApplying;
		float RunStart = -1.f;
		float StageStart = 0.f;
		float StageTime = 0.f;
		float TotalTime = 0.f;
		int32 Failures = 0;
		float BlizzardFogDensity = 0.f;
		float SnowStart = 0.f;
		float SnowAdvancedStart = 0.f;
		float SnowHeld = 0.f;
		FDirectionalSnow ObjectStart;
		FDirectionalSnow MeltStart;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[WeatherSmoke] %s: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	void Finish(FState& State, const TCHAR* Name)
	{
		const bool bPass = State.Failures == 0;
		UE_LOG(LogCodexTactics, Display, TEXT("[WeatherSmoke] RESULT %s (%d failed checks)"), bPass ? TEXT("PASS") : TEXT("FAIL"), State.Failures);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), bPass ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, Name);
	}

	void UpdateTime(FState& State, const UWorld& World)
	{
		const float Now = World.GetTimeSeconds();
		if (State.RunStart < 0.f)
		{
			State.RunStart = Now;
			State.StageStart = Now;
		}
		State.StageTime = Now - State.StageStart;
		State.TotalTime = Now - State.RunStart;
	}

	void NextStage(FState& State, EStage Stage)
	{
		State.Stage = Stage;
		State.StageStart += State.StageTime;
		State.StageTime = 0.f;
	}

	/** Waits for the presenter's first application; false while waiting. Logs + checks a timeout. */
	AWeatherPresenterActor* WaitForPresenter(FState& State, UWorld& World, bool& bOutTimedOut)
	{
		bOutTimedOut = false;
		AWeatherPresenterActor* Presenter = AWeatherPresenterActor::Find(&World);
		if (Presenter && Presenter->IsApplying() && Presenter->GetApplyCount() > 0)
		{
			return Presenter;
		}
		if (State.TotalTime > ApplyTimeoutSeconds)
		{
			bOutTimedOut = true;
			Check(State, false, FString::Printf(TEXT("presenter spawned and applying within %.0f game s (found %d, applying %d)"), ApplyTimeoutSeconds,
				Presenter ? 1 : 0, Presenter && Presenter->IsApplying() ? 1 : 0));
		}
		return nullptr;
	}

	float FogDensityOf(const AWeatherPresenterActor& Presenter)
	{
		const AExponentialHeightFog* Fog = Presenter.GetHeightFog();
		return Fog ? Fog->GetComponent()->FogDensity : Presenter.GetLastFrame().Preset.FogDensity;
	}

	bool CloudsOff(const UWorld& World)
	{
		for (TActorIterator<AVolumetricCloud> It(const_cast<UWorld*>(&World)); It; ++It)
		{
			const UVolumetricCloudComponent* Component = It->FindComponentByClass<UVolumetricCloudComponent>();
			if (!It->IsHidden() || (Component && (Component->IsVisible() || Component->IsActive())))
			{
				return false;
			}
		}
		return true;
	}

	/** Stage 1: what was adopted. */
	void CheckAdoption(FState& State, const UWorld& World, const AWeatherPresenterActor& Presenter)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[WeatherSmoke] adopted: %s"), *Presenter.DescribeAdopted());
		int32 Placed = 0;
		for (TActorIterator<AWeatherPresenterActor> It(const_cast<UWorld*>(&World)); It; ++It)
		{
			++Placed;
		}
		Check(State, Placed == 1, FString::Printf(TEXT("1 exactly one presenter in the world (%d)"), Placed));
		Check(State, Presenter.GetSun() != nullptr, TEXT("1 the map's sun (directional light) adopted"));
		Check(State, Presenter.GetSkyAtmosphere() != nullptr, TEXT("1 the map's sky atmosphere adopted"));
		Check(State, Presenter.GetSkyLight() != nullptr, TEXT("1 the map's sky light adopted"));
		Check(State, Presenter.GetHeightFog() != nullptr, TEXT("1 the map's height fog adopted"));
		Check(State, Presenter.GetActiveMoon() != nullptr, FString::Printf(TEXT("1 a moon light (%s)"), Presenter.IsMoonOwn() ? TEXT("own") : TEXT("adopted")));
		Check(State, Presenter.GetAdoptedPostProcess() || (Presenter.GetOwnPostProcess() && Presenter.GetOwnPostProcess()->bEnabled),
			TEXT("1 a post-process is driven (adopted unbound volume or the own component, enabled)"));
		Check(State, Presenter.GetCloudCount() == 0 || CloudsOff(World),
			FString::Printf(TEXT("1 the map's volumetric clouds are off (%d cloud actor(s))"), Presenter.GetCloudCount()));
	}

	/** Stage 2 + 3 (synchronous: SetHour / ForceWeather broadcast OnStateSet, the presenter applies at once). */
	void CheckHoursAndWeather(FState& State, UEnvironmentSubsystem& Env, AWeatherPresenterActor& Presenter)
	{
		Env.SetClockScale(0.f);
		const UDirectionalLightComponent* Sun = Presenter.GetSun() ? Presenter.GetSun()->GetComponent() : nullptr;
		const UDirectionalLightComponent* Moon = Presenter.GetActiveMoon();

		Env.SetHour(12.f);
		const FWeatherVisualFrame Noon = Presenter.GetLastFrame();
		Check(State, Noon.SunElevation > 20.f && FMath::IsNearlyEqual(Noon.Hour, 12.f, 0.01f),
			FString::Printf(TEXT("2 12:00: sun %.1f deg above the horizon"), Noon.SunElevation));
		if (Sun)
		{
			const bool bMovable = Sun->Mobility == EComponentMobility::Movable;
			Check(State, !bMovable || Sun->GetComponentRotation().Pitch < -20.f,
				FString::Printf(TEXT("2 12:00: the sun light points down (pitch %.1f, %s)"), Sun->GetComponentRotation().Pitch, bMovable ? TEXT("movable") : TEXT("not movable: not rotated")));
			Check(State, Sun->IsVisible() && Sun->Intensity > 0.f, FString::Printf(TEXT("2 12:00: the sun is lit (%.2f, authored %.2f)"), Sun->Intensity, Presenter.GetAuthored().SunIntensity));
		}
		Check(State, Moon && !Moon->IsVisible(), TEXT("2 12:00: the moon is hidden"));

		Env.SetHour(23.f);
		const FWeatherVisualFrame Night = Presenter.GetLastFrame();
		Check(State, Night.SunElevation < 0.f && !Night.bSunVisible, FString::Printf(TEXT("2 23:00: sun %.1f deg, below the horizon, hidden"), Night.SunElevation));
		if (Sun)
		{
			Check(State, !Sun->IsVisible(), TEXT("2 23:00: the sun light is off"));
		}
		Check(State, Night.bMoonVisible && Night.MoonElevation > 0.f && Moon && Moon->IsVisible() && Moon->Intensity > 0.f,
			FString::Printf(TEXT("2 23:00: the moon is lit (%.2f) at %.1f deg"), Moon ? Moon->Intensity : 0.f, Night.MoonElevation));
		Check(State, !Moon || Moon->GetComponentRotation().Pitch < -10.f, FString::Printf(TEXT("2 23:00: the moon light points down (pitch %.1f)"),
			Moon ? Moon->GetComponentRotation().Pitch : 0.f));
		Check(State, Night.NightAmount > 0.99f, FString::Printf(TEXT("2 23:00: full night grade (%.2f)"), Night.NightAmount));

		Env.ForceWeather(EWeatherState::Clear, true);
		const float ClearFog = FogDensityOf(Presenter);
		Check(State, FMath::IsNearlyEqual(ClearFog, Presenter.GetPreset(EWeatherState::Clear).FogDensity, 1.e-4f),
			FString::Printf(TEXT("3 Clear: fog density %.4f = the Clear preset (%s)"), ClearFog, Presenter.IsPresetFromAsset(EWeatherState::Clear) ? TEXT("asset") : TEXT("C++ default")));
		Env.ForceWeather(EWeatherState::Blizzard, true);
		State.BlizzardFogDensity = FogDensityOf(Presenter);
		Check(State, State.BlizzardFogDensity > ClearFog, FString::Printf(TEXT("3 Blizzard: fog density %.4f > Clear %.4f"), State.BlizzardFogDensity, ClearFog));
		if (const AExponentialHeightFog* Fog = Presenter.GetHeightFog())
		{
			Check(State, Fog->GetComponent()->bEnableVolumetricFog == Presenter.GetPreset(EWeatherState::Blizzard).bVolumetricFog,
				TEXT("3 Blizzard: volumetric fog as the preset says"));
		}
		Env.SetHour(12.f);
		Check(State, Presenter.GetLastFrame().SunIntensityScale < Noon.SunIntensityScale,
			FString::Printf(TEXT("3 Blizzard noon sun x%.2f < Clear noon x%.2f"), Presenter.GetLastFrame().SunIntensityScale, Noon.SunIntensityScale));
	}

	/** The MPC_Environment SnowAmount right after an application equals the state (skipped with a FAIL when the MPC is missing). */
	void CheckMpcSnow(FState& State, AWeatherPresenterActor& Presenter, float Expected, const TCHAR* Label)
	{
		Presenter.ApplyNow();
		float Value = -1.f;
		const bool bRead = Presenter.GetEnvironmentScalar(MpcSnowAmount, Value);
		Check(State, bRead && FMath::IsNearlyEqual(Value, Expected, 1.e-4f),
			FString::Printf(TEXT("%s MPC_Environment.SnowAmount %.4f = %.4f (read %d)"), Label, Value, Expected, bRead ? 1 : 0));
	}

	/** Stage S (start): components + MPC, then a forced Blizzard at x20. */
	void StartSnowCover(FState& State, UEnvironmentSubsystem& Env, AWeatherPresenterActor& Presenter)
	{
		Check(State, Presenter.GetSnowFx() != nullptr && Presenter.GetSnowOverlay() != nullptr, TEXT("S the presenter has its snow fx + snow overlay components"));
		Check(State, Presenter.GetEnvironmentCollection() != nullptr, TEXT("S MPC_Environment loaded"));
		if (const UMaterialParameterCollection* Collection = Presenter.GetEnvironmentCollection())
		{
			// Created by Scripts/Editor/create_environment_mpc.py (its ids are not readable from Python): every parameter present, ids valid + distinct.
			TSet<FGuid> Ids;
			int32 Found = 0;
			for (const TCHAR* Name : { TEXT("SnowAmount"), TEXT("FrostAmount"), TEXT("WindStrength"), TEXT("IsSnowing") })
			{
				if (const FCollectionScalarParameter* Param = Collection->GetScalarParameterByName(FName(Name)))
				{
					++Found;
					Ids.Add(Param->Id);
				}
			}
			if (const FCollectionVectorParameter* Param = Collection->GetVectorParameterByName(FName(TEXT("WindDirection"))))
			{
				++Found;
				Ids.Add(Param->Id);
			}
			Check(State, Found == 5 && Ids.Num() == 5 && !Ids.Contains(FGuid()),
				FString::Printf(TEXT("S MPC_Environment: 4 scalars + WindDirection, distinct valid ids (%d found, %d ids)"), Found, Ids.Num()));
		}
		Check(State, Env.GetSnowAmount() >= 0.f && Env.GetSnowAmount() <= 1.f,
			FString::Printf(TEXT("S start snow cover %.3f (neutral start: snow_cover_start %.2f)"), Env.GetSnowAmount(), Env.GetConfig().SnowCoverStart));
		if (const UMaterialParameterCollection* Collection = Presenter.GetEnvironmentCollection())
		{
			TSet<FGuid> Ids;
			for (const TCHAR* Name : { TEXT("SnowTop"), TEXT("SnowPX"), TEXT("SnowNX"), TEXT("SnowPY"), TEXT("SnowNY") })
			{
				if (const FCollectionScalarParameter* Param = Collection->GetScalarParameterByName(FName(Name)))
				{
					Ids.Add(Param->Id);
				}
			}
			Check(State, Ids.Num() == 5 && !Ids.Contains(FGuid()), FString::Printf(TEXT("S MPC_Environment: the 5 directional snow scalars with distinct ids (%d)"), Ids.Num()));
		}
		Env.ForceWeather(EWeatherState::Blizzard, true);
		if (Env.GetSnowAmount() > 0.8f)
		{
			Env.SetSnowAmount(0.3f); // room to grow
		}
		// Section 15: the wind FROM +X (it travels -X); the objects start with the ground cover on top, bare sides.
		Env.SetWindDirectionOverride(FVector(-1.f, 0.f, 0.f));
		FDirectionalSnow Objects;
		Objects.Top = Env.GetSnowAmount();
		Env.SetObjectSnow(Objects);
		State.ObjectStart = Env.GetObjectSnow();
		State.SnowStart = Env.GetSnowAmount();
		State.SnowAdvancedStart = Env.GetAdvancedSeconds();
		Env.SetClockScale(SnowClockScale);
	}

	/** Stage S (grow): the cover rose by the Blizzard rate x the game seconds that ran. */
	void CheckSnowGrew(FState& State, UEnvironmentSubsystem& Env, AWeatherPresenterActor& Presenter)
	{
		const float Ran = Env.GetAdvancedSeconds() - State.SnowAdvancedStart;
		const float Expected = FMath::Min(1.f, State.SnowStart + Ran * EnvironmentRules::GetSnowCoverRate(Env.GetConfig(), EWeatherState::Blizzard));
		const float Now = Env.GetSnowAmount();
		Check(State, Env.GetWeather() == EWeatherState::Blizzard && Ran > 10.f, FString::Printf(TEXT("S Blizzard ran %.1f design s at x%.0f"), Ran, SnowClockScale));
		Check(State, Now > State.SnowStart + 0.05f, FString::Printf(TEXT("S Blizzard: the snow cover rose %.3f -> %.3f"), State.SnowStart, Now));
		Check(State, FMath::IsNearlyEqual(Now, Expected, 0.01f), FString::Printf(TEXT("S the rise follows the Blizzard rate (%.3f, expected %.3f)"), Now, Expected));
		CheckMpcSnow(State, Presenter, Now, TEXT("S Blizzard:"));
		float Snowing = -1.f;
		const bool bSnowingRead = Presenter.GetEnvironmentScalar(MpcIsSnowing, Snowing); // read before the message is formatted
		Check(State, bSnowingRead && Snowing > 0.99f, FString::Printf(TEXT("S Blizzard: MPC IsSnowing %.2f"), Snowing));
		// Section 15: the objects' pockets - the top and the windward +X side grew, the other sides stayed bare.
		const FDirectionalSnow Objects = Env.GetObjectSnow();
		const float Rate = EnvironmentRules::GetSnowCoverRate(Env.GetConfig(), EWeatherState::Blizzard);
		const float Side = FMath::Min(1.f, Ran * Rate * Env.GetConfig().ObjectSnowSideRateScale * Env.GetWindStrength());
		Check(State, FMath::IsNearlyEqual(Objects.Top, FMath::Min(1.f, State.ObjectStart.Top + Ran * Rate), 0.01f),
			FString::Printf(TEXT("S objects: the top grew with the cover rate (%.3f)"), Objects.Top));
		Check(State, Objects.PX > 0.05f && FMath::IsNearlyEqual(Objects.PX, Side, 0.01f),
			FString::Printf(TEXT("S objects: the windward +X pocket filled %.3f (rate x wind %.2f: %.3f)"), Objects.PX, Env.GetWindStrength(), Side));
		Check(State, Objects.NX == 0.f && Objects.PY == 0.f && Objects.NY == 0.f,
			FString::Printf(TEXT("S objects: -X / +Y / -Y stay bare (%.3f %.3f %.3f)"), Objects.NX, Objects.PY, Objects.NY));
		float MpcPX = -1.f;
		const bool bPXRead = Presenter.GetEnvironmentScalar(FName(TEXT("SnowPX")), MpcPX);
		Check(State, bPXRead && FMath::IsNearlyEqual(MpcPX, Objects.PX, 0.002f), FString::Printf(TEXT("S MPC_Environment.SnowPX %.3f = %.3f"), MpcPX, Objects.PX));
		Env.ForceWeather(EWeatherState::Clear, true);
		State.SnowHeld = Env.GetSnowAmount();
		// The melt: Clear at 12:00, the top just above its floor (0.5 x the ground), a little on +X.
		Env.SetHour(12.f);
		FDirectionalSnow Melt;
		Melt.Top = FMath::Min(1.f, Env.GetConfig().ObjectSnowMeltFloor * State.SnowHeld + 0.02f);
		Melt.PX = 0.03f;
		Env.SetObjectSnow(Melt);
		State.MeltStart = Env.GetObjectSnow();
		State.SnowAdvancedStart = Env.GetAdvancedSeconds();
	}

	/** Stage S (hold): Clear never takes the snow away. */
	void CheckSnowHeld(FState& State, UEnvironmentSubsystem& Env, AWeatherPresenterActor& Presenter)
	{
		const float Ran = Env.GetAdvancedSeconds() - State.SnowAdvancedStart;
		Check(State, Env.GetWeather() == EWeatherState::Clear && Ran > 5.f && Env.GetSnowAmount() == State.SnowHeld,
			FString::Printf(TEXT("S Clear: the cover holds at %.3f over %.1f design s (now %.3f)"), State.SnowHeld, Ran, Env.GetSnowAmount()));
		CheckMpcSnow(State, Presenter, Env.GetSnowAmount(), TEXT("S Clear:"));
		float Snowing = -1.f;
		const bool bSnowingRead = Presenter.GetEnvironmentScalar(MpcIsSnowing, Snowing);
		Check(State, bSnowingRead && Snowing < 0.01f, FString::Printf(TEXT("S Clear: MPC IsSnowing %.2f"), Snowing));
		// Section 15: in Clear daylight the objects' snow melts - the top down to the floor, the side to 0; the ground keeps its cover.
		const FDirectionalSnow Objects = Env.GetObjectSnow();
		const float Floor = Env.GetConfig().ObjectSnowMeltFloor * Env.GetSnowAmount();
		const float Melted = Ran * EnvironmentRules::GetObjectSnowMeltRate(Env.GetConfig(), Env.GetStateRef(), Env.GetSunElevation());
		Check(State, Env.GetSunElevation() > 0.f && Melted > 0.03f,
			FString::Printf(TEXT("S Clear 12:00: the sun is up (%.1f deg), %.3f melted over %.1f design s"), Env.GetSunElevation(), Melted, Ran));
		Check(State, FMath::IsNearlyEqual(Objects.Top, FMath::Max(Floor, State.MeltStart.Top - Melted), 1.e-3f) && FMath::IsNearlyEqual(Objects.Top, Floor, 1.e-4f),
			FString::Printf(TEXT("S Clear: the objects' top melted %.3f -> %.3f = the floor %.2f x ground %.3f"), State.MeltStart.Top, Objects.Top,
				Env.GetConfig().ObjectSnowMeltFloor, Env.GetSnowAmount()));
		Check(State, Objects.PX == 0.f, FString::Printf(TEXT("S Clear: the +X pocket melted away (%.3f)"), Objects.PX));
		Check(State, Env.GetSnowAmount() == State.SnowHeld, FString::Printf(TEXT("S Clear: the ground cover is unchanged by the melt (%.3f)"), Env.GetSnowAmount()));
		float MpcTop = -1.f;
		const bool bTopRead = Presenter.GetEnvironmentScalar(FName(TEXT("SnowTop")), MpcTop);
		Check(State, bTopRead && FMath::IsNearlyEqual(MpcTop, Objects.Top, 0.002f), FString::Printf(TEXT("S MPC_Environment.SnowTop %.3f = %.3f"), MpcTop, Objects.Top));
		Env.ClearWindDirectionOverride();
		Env.SetClockScale(0.f);
		// Back to the Blizzard look the visuals-on check of stage 4 expects.
		Env.ForceWeather(EWeatherState::Blizzard, true);
	}

	/** Stage 4a: Codex.Env.Visuals 0 restored the authored values. */
	void CheckRestored(FState& State, const UWorld& World, const AWeatherPresenterActor& Presenter)
	{
		const FWeatherAuthoredLighting& Authored = Presenter.GetAuthored();
		Check(State, !Presenter.IsApplying(), TEXT("4 Codex.Env.Visuals 0: the presenter stopped"));
		if (const ADirectionalLight* SunActor = Presenter.GetSun())
		{
			const UDirectionalLightComponent* Sun = SunActor->GetComponent();
			Check(State, FMath::IsNearlyEqual(Sun->Intensity, Authored.SunIntensity, 1.e-3f) && Sun->IsVisible() == Authored.bSunVisible,
				FString::Printf(TEXT("4 sun intensity %.3f = authored %.3f, visibility authored"), Sun->Intensity, Authored.SunIntensity));
			Check(State, Sun->GetComponentRotation().Equals(Authored.SunRotation, 0.01f),
				FString::Printf(TEXT("4 sun rotation %s = authored %s"), *Sun->GetComponentRotation().ToCompactString(), *Authored.SunRotation.ToCompactString()));
			Check(State, (Sun->bUseTemperature != 0) == Authored.bSunUseTemperature && FMath::IsNearlyEqual(Sun->LightSourceAngle, Authored.SunSourceAngle, 1.e-3f),
				TEXT("4 sun temperature flag / source angle authored"));
		}
		if (const AExponentialHeightFog* FogActor = Presenter.GetHeightFog())
		{
			const UExponentialHeightFogComponent* Fog = FogActor->GetComponent();
			Check(State, FMath::IsNearlyEqual(Fog->FogDensity, Authored.FogDensity, 1.e-5f) && FMath::IsNearlyEqual(Fog->FogHeightFalloff, Authored.FogHeightFalloff, 1.e-5f)
				&& Fog->FogInscatteringLuminance.Equals(Authored.FogInscattering, 1.e-4f) && Fog->bEnableVolumetricFog == Authored.bVolumetricFog,
				FString::Printf(TEXT("4 fog density %.4f = authored %.4f (falloff, colour, volumetric too)"), Fog->FogDensity, Authored.FogDensity));
		}
		if (Presenter.IsMoonOwn())
		{
			Check(State, !Presenter.GetActiveMoon()->IsVisible(), TEXT("4 the own moon is hidden"));
		}
		Check(State, Presenter.GetAdoptedPostProcess() || !Presenter.GetOwnPostProcess()->bEnabled, TEXT("4 the own post-process is off"));
		bool bCloudsAuthored = true;
		for (TActorIterator<AVolumetricCloud> It(const_cast<UWorld*>(&World)); It; ++It)
		{
			bCloudsAuthored &= !It->IsHidden();
		}
		Check(State, Presenter.GetCloudCount() == 0 || bCloudsAuthored, TEXT("4 the volumetric clouds are back as authored"));
		float Snow = -1.f;
		const bool bSnowRead = Presenter.GetEnvironmentScalar(MpcSnowAmount, Snow);
		Check(State, !Presenter.GetEnvironmentCollection() || (bSnowRead && Snow == 0.f),
			FString::Printf(TEXT("4 MPC_Environment.SnowAmount back to 0 (%.3f)"), Snow));
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		UpdateTime(State, *World);
		UEnvironmentSubsystem* Env = UEnvironmentSubsystem::Get(World);
		if (!Env)
		{
			Check(State, false, TEXT("environment subsystem present"));
			Finish(State, TEXT("WeatherSmoke"));
			return false;
		}
		switch (State.Stage)
		{
		case EStage::WaitApplying:
		{
			bool bTimedOut = false;
			AWeatherPresenterActor* Presenter = WaitForPresenter(State, *World, bTimedOut);
			if (bTimedOut)
			{
				Finish(State, TEXT("WeatherSmoke"));
				return false;
			}
			if (!Presenter)
			{
				return true;
			}
			UE_LOG(LogCodexTactics, Display, TEXT("[WeatherSmoke] start: %s; first application after %.2f game s"), *Env->Describe(), State.TotalTime);
			CheckAdoption(State, *World, *Presenter);
			CheckHoursAndWeather(State, *Env, *Presenter);
			StartSnowCover(State, *Env, *Presenter);
			NextStage(State, EStage::SnowGrow);
			return true;
		}
		case EStage::SnowGrow:
			if (State.StageTime >= SnowGrowSeconds)
			{
				if (AWeatherPresenterActor* Presenter = AWeatherPresenterActor::Find(World))
				{
					CheckSnowGrew(State, *Env, *Presenter);
				}
				NextStage(State, EStage::SnowHold);
			}
			return true;
		case EStage::SnowHold:
			if (State.StageTime >= SnowHoldSeconds)
			{
				if (AWeatherPresenterActor* Presenter = AWeatherPresenterActor::Find(World))
				{
					CheckSnowHeld(State, *Env, *Presenter);
				}
				AWeatherPresenterActor::SetVisualsEnabled(false);
				NextStage(State, EStage::VisualsOff);
			}
			return true;
		case EStage::VisualsOff:
			if (State.StageTime >= ToggleSettleSeconds)
			{
				if (const AWeatherPresenterActor* Presenter = AWeatherPresenterActor::Find(World))
				{
					CheckRestored(State, *World, *Presenter);
				}
				AWeatherPresenterActor::SetVisualsEnabled(true);
				NextStage(State, EStage::VisualsOn);
			}
			return true;
		case EStage::VisualsOn:
			if (State.StageTime >= ToggleSettleSeconds)
			{
				if (const AWeatherPresenterActor* Presenter = AWeatherPresenterActor::Find(World))
				{
					Check(State, Presenter->IsApplying() && FMath::IsNearlyEqual(FogDensityOf(*Presenter), State.BlizzardFogDensity, 1.e-4f),
						FString::Printf(TEXT("4 Codex.Env.Visuals 1: applied again (fog %.4f = Blizzard)"), FogDensityOf(*Presenter)));
					Check(State, Presenter->GetCloudCount() == 0 || CloudsOff(*World), TEXT("4 the volumetric clouds are off again"));
				}
				Env->SetClockScale(1.f);
				Finish(State, TEXT("WeatherSmoke"));
				return false;
			}
			return true;
		}
		return true;
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.WeatherSmoke"),
		TEXT("Dev check: the weather presenter adopts the map's lights, sun / moon by hour, fog by weather, Codex.Env.Visuals 0 restores the authored lighting; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

namespace WeatherLakeSmoke
{
	using namespace WeatherSmoke;

	bool LakeStep(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		UpdateTime(State, *World);
		const UEnvironmentSubsystem* Env = UEnvironmentSubsystem::Get(World);
		bool bTimedOut = false;
		AWeatherPresenterActor* Presenter = Env ? WaitForPresenter(State, *World, bTimedOut) : nullptr;
		if (!Env || bTimedOut)
		{
			Check(State, Env != nullptr, TEXT("lake: environment subsystem present"));
			Finish(State, TEXT("WeatherLakeSmoke"));
			return false;
		}
		if (!Presenter)
		{
			return true;
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[WeatherSmoke] lake start: %s; adopted: %s"), *Env->Describe(), *Presenter->DescribeAdopted());
		Check(State, Env->IsLocked() && Env->GetWeather() == EWeatherState::Snowfall && FMath::IsNearlyEqual(Env->GetHour(), 23.f, 0.01f),
			TEXT("L1 the lake is locked at 23:00 Snowfall"));
		const FWeatherVisualFrame& Frame = Presenter->GetLastFrame();
		Check(State, Frame.Weather == EWeatherState::Snowfall && FMath::IsNearlyEqual(Frame.Hour, 23.f, 0.01f),
			FString::Printf(TEXT("L1 the applied frame is 23:00 Snowfall (%.2f, %s)"), Frame.Hour, *EnvironmentRules::WeatherToString(Frame.Weather)));
		const UDirectionalLightComponent* Sun = Presenter->GetSun() ? Presenter->GetSun()->GetComponent() : nullptr;
		Check(State, Frame.SunElevation < 0.f && (!Sun || !Sun->IsVisible()), FString::Printf(TEXT("L2 night: the sun is below the horizon (%.1f deg) and off"), Frame.SunElevation));
		const UDirectionalLightComponent* Moon = Presenter->GetActiveMoon();
		Check(State, Moon && Moon->IsVisible() && Moon->Intensity > 0.f, FString::Printf(TEXT("L2 night: the moon lights the lake (%.2f)"), Moon ? Moon->Intensity : 0.f));
		Check(State, Frame.NightAmount > 0.99f, TEXT("L2 night: full night grade"));
		const float Fog = FogDensityOf(*Presenter);
		Check(State, Presenter->GetHeightFog() != nullptr, TEXT("L3 the lake's height fog adopted"));
		Check(State, FMath::IsNearlyEqual(Fog, Presenter->GetPreset(EWeatherState::Snowfall).FogDensity, 1.e-4f)
			&& Fog > Presenter->GetPreset(EWeatherState::Clear).FogDensity,
			FString::Printf(TEXT("L3 Snowfall fog %.4f (> Clear %.4f)"), Fog, Presenter->GetPreset(EWeatherState::Clear).FogDensity));
		Check(State, Presenter->GetAdoptedPostProcess() || Presenter->GetOwnPostProcess()->bEnabled, TEXT("L3 the night / snowfall grade is applied (post-process on)"));
		Check(State, Presenter->GetCloudCount() == 0 || CloudsOff(*World), FString::Printf(TEXT("L3 volumetric clouds off (%d)"), Presenter->GetCloudCount()));
		Check(State, FMath::IsNearlyEqual(Env->GetSnowAmount(), 0.6f, 1.e-4f), FString::Printf(TEXT("L4 the lake starts with snow cover 0.6 (%.3f)"), Env->GetSnowAmount()));
		CheckMpcSnow(State, *Presenter, Env->GetSnowAmount(), TEXT("L4 lake:"));
		Finish(State, TEXT("WeatherLakeSmoke"));
		return false;
	}

	void LakeRun(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return LakeStep(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.WeatherLakeSmoke"),
		TEXT("Dev check: on the Demo 01 lake (locked 23:00 Snowfall) the weather presenter shows night (moon, no sun) and the Snowfall fog; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&LakeRun));
}

#endif // !UE_BUILD_SHIPPING
