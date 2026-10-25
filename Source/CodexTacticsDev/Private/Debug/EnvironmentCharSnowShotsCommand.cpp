// Dev-only: mode "charsnow" of CodexTactics.EnvironmentShots - rendered captures of the directional snow on characters and objects
// (docs/systems/environment-weather.md section 15, user decisions 2026-10-23; UE-only, no Godot reference). Needs rendering:
//   UnrealEditor.exe CodexTactics.uproject /Game/Maps/L_MovementTest -game -RenderOffscreen -ResX=1920 -ResY=1080 -nosound -benchmark -fps=30
//     -ExecCmds="CodexTactics.EnvironmentShots charsnow [nomovie] [noperf] [nomelt]"      (also /Game/Maps/L_Demo01_B_FrozenFactory)
//     -ExecCmds="CodexTactics.EnvironmentShots charsnow ab <tag> [snow]"                  (the per-master A/B bodies)
//     -ExecCmds="CodexTactics.EnvironmentShots charsnow startframes"                      (the level start: frames 1 / 2 / 30 / 60 / 150 as the
//       game shows them, no camera / HUD change; start_<map>_fNNN.png + a log line per frame with the presenter state - the level must open
//       in its weather look, user feedback 2026-10-23)
// Output Saved/Screenshots/Environment/charsnow/:
//   scene: <map>_<NN>_<phase>_<view>.png - wave 2 on MovementTest (57 enemies) / wave 1 on the factory, the squad safe and holding fire, 12:00:
//     1 blizzard with the wind FROM +X (x30) -> the windward (+X) sides white; 2 the wind turns, FROM -Y -> -Y fills, +X kept; 3 Clear 12:00
//     held at x20 -> the melt after 2 / 10 / 40 design minutes (tops down to 0.5 x the ground on objects, characters bare). Views: "windward"
//     (camera looking along (-1, +1): the +X and -Y faces), "leeward" (the opposite side, stays bare), "close" (the leader at 4 m).
//     movie_<map>/frame_NNNN.png over phases 1 + 2 at 15 fps (10 s; ffmpeg -framerate 15 ... charsnow_<map>.mp4).
//     Log lines "[CharSnowPerf]": UCharacterSnowSubsystem game-thread ms per frame with the wave alive, and Codex.Env.CharacterSnow 0 / 1 GPU /
//     render / game ms; "[CharSnow]" the leader's / the enemies' average pockets per phase.
//   ab: ab_<tag>/<Master>_<phase>.png - one body per character master (SkeletalMeshActors in the reference pose, 30 m above the squad, the
//     authored lighting: Codex.Env.Visuals 0, no AA, game time dilated to 0.0001 so the sky stands still), per body in ONE run: "zero" (the layer, every
//     pocket 0), "before" (the same MI copied to /Game/_CharSnowBefore/<MI>_Before, parented to the master as it was before the layer - a
//     temporary copy, see docs section 15), "zero2" (the layer at 0 again: the noise floor), with "snow" also "snow" (CPD 8..12 = 1, 0.8, 0,
//     0, 0.5). Scripts/Tools/image_diff.py compares the phases.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Animation/SkeletalMeshActor.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Debug/DebugDrawService.h"
#include "Debug/FrontendSmokeUtils.h"
#include "Debug/SmokeUtils.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Environment/CharacterSnowRules.h"
#include "Environment/CharacterSnowSubsystem.h"
#include "Environment/EnvironmentRules.h"
#include "Environment/EnvironmentSubsystem.h"
#include "Environment/WeatherPresenterActor.h"
#include "GameFramework/HUD.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "RHIStats.h"
#include "UnrealClient.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

namespace EnvironmentCharSnowShots
{
	constexpr int32 MaxFrames = 30 * 600;
	constexpr int32 WarmupFrames = 150;
	constexpr int32 FillFrames = 150;          // 5 s at -fps=30 per wind
	constexpr float FillClockScale = 30.f;     // 5 s x 30 = 150 design s of blizzard per wind (~0.83 on a facing pocket)
	constexpr float MeltClockScale = 20.f;
	const int32 MeltCaptureFrames[] = { 0, 180, 900, 3600 }; // 0 / 2 / 10 / 40 design minutes at x20 and 30 fps
	constexpr int32 MovieEvery = 2;            // 15 fps
	constexpr int32 PerfSettleFrames = 30;
	constexpr int32 PerfFrames = 150;
	constexpr float ViewPitch = -35.26f;       // FTacticalCameraConfig
	constexpr float ViewVerticalFov = 30.f;
	constexpr float ViewDistance = 2200.f;
	constexpr float WindwardYaw = 135.f;       // looking along (-1, +1): the +X and the -Y faces turn to the camera
	constexpr float LeewardYaw = -45.f;

	struct FPerfSample
	{
		double Gpu = 0.0;
		double Render = 0.0;
		double Game = 0.0;
		int32 Count = 0;

		void Add()
		{
			Gpu += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0));
			Render += FPlatformTime::ToMilliseconds(GRenderThreadTime);
			Game += FPlatformTime::ToMilliseconds(GGameThreadTime);
			++Count;
		}

		FString Describe() const
		{
			const double N = FMath::Max(Count, 1);
			return FString::Printf(TEXT("GPU %.2f ms, render %.2f ms, game %.2f ms"), Gpu / N, Render / N, Game / N);
		}
	};

	/** One body per character master (the A/B): the 1-slot body and its own look. */
	struct FAbBody
	{
		const TCHAR* Master;
		const TCHAR* Mesh;
		const TCHAR* Material;
	};

	const FAbBody AbBodies[] = {
		{ TEXT("M_CharacterMaster"), TEXT("/Game/Characters/Operatives/Optimized/Post_Apo_Survivor_a/SK_Post_Apo_Survivor_a_1slot.SK_Post_Apo_Survivor_a_1slot"),
			TEXT("/Game/Characters/Operatives/Optimized/Post_Apo_Survivor_a/MI_Post_Apo_Survivor_a.MI_Post_Apo_Survivor_a") },
		{ TEXT("M_CharacterMaster_Masked_TwoSided"), TEXT("/Game/Characters/Operatives/Optimized/Female_Soldier/SK_Female_Soldier_1slot.SK_Female_Soldier_1slot"),
			TEXT("/Game/Characters/Operatives/Optimized/Female_Soldier/MI_Female_Soldier.MI_Female_Soldier") },
		{ TEXT("M_CharacterMaster_TwoSided"), TEXT("/Game/Characters/Enemies/Optimized/Cutter/SK_Cutter_1slot.SK_Cutter_1slot"),
			TEXT("/Game/Characters/Enemies/Optimized/Cutter/MI_Cutter_Set3.MI_Cutter_Set3") },
		{ TEXT("M_CharacterMaster_Subsurface"), TEXT("/Game/Characters/Enemies/Optimized/Frostbitten/SK_Frostbitten_1slot.SK_Frostbitten_1slot"),
			TEXT("/Game/Characters/Enemies/Optimized/Frostbitten/MI_Frostbitten_Set4.MI_Frostbitten_Set4") },
	};

	enum class EStep : uint8 { Warmup, FillX, ShotsX, FillY, ShotsY, Melt, Perf, AbSetup, AbSettle, AbShots, Done };

	struct FRunner
	{
		EStep Step = EStep::Warmup;
		int32 Frame = 0;
		int32 StepFrame = 0;
		int32 ShotCount = 0;
		int32 MovieCount = 0;
		int32 MeltShot = 0;
		int32 MeltFrame = 0;
		int32 ViewFrame = -1;
		int32 PerfIndex = 0;
		int32 AbIndex = 0;
		bool bMovie = true;
		bool bPerf = true;
		bool bMelt = true;
		bool bAb = false;
		bool bStartFrames = false;
		bool bAbSnow = false;
		FString Tag;
		FString MapName;
		FString Dir;
		FString Label;
		double CpuStart = 0.0;
		int32 TicksStart = 0;
		int32 PushesStart = 0;
		FPerfSample Samples[2];
		TArray<FString> Report;
		TArray<TWeakObjectPtr<ASkeletalMeshActor>> AbActors;
		TWeakObjectPtr<ACameraActor> Camera;
		FDelegateHandle EndFrameHandle;
		FDelegateHandle DrawHandle;
	};

	TSharedPtr<FRunner> Runner;

	int32 ShadersPending()
	{
#if WITH_EDITOR
		return GShaderCompilingManager ? GShaderCompilingManager->GetNumRemainingJobs() : 0;
#else
		return 0;
#endif
	}

	void SetCVar(const TCHAR* Name, int32 Value)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Variable->Set(Value, ECVF_SetByConsole);
		}
	}

	void Capture(const FString& Path)
	{
		FScreenshotRequest::RequestScreenshot(Path, /*bInShowUI*/ true, /*bAddFilenameSuffix*/ false);
	}

	void OnDraw(UCanvas* Canvas, APlayerController* /*Controller*/)
	{
		if (!Runner.IsValid() || Runner->Label.IsEmpty() || !Canvas)
		{
			return;
		}
		UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
		Canvas->SetDrawColor(FColor(0, 0, 0, 160));
		Canvas->DrawTile(Canvas->DefaultTexture, 10.f, 10.f, Canvas->ClipX - 20.f, 30.f, 0.f, 0.f, 1.f, 1.f);
		Canvas->SetDrawColor(FColor(255, 230, 80));
		Canvas->DrawText(Font, Runner->Label, 18.f, 14.f, 1.1f, 1.1f);
	}

	void KeepSquadSafe(UWorld* World)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		for (AOperativeCharacter* Each : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
		{
			Each->HealthComponent->SetMaxHealth(100000.f, false);
			Each->HealthComponent->Heal(100000.f);
			Each->bTacticalCeaseFire = true;
			Each->ColdLevel = 0.f;
		}
	}

	/** An own camera: the tactical camera's numbers (pitch, vertical FOV) on Focus from Distance at Yaw. */
	void View(UWorld* World, const FVector& Focus, float Yaw, float Pitch, float Distance)
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
		const FRotator Rotation(Pitch, Yaw, 0.f);
		const FVector Eye = Focus - Rotation.Vector() * Distance;
		if (!Runner->Camera.IsValid())
		{
			Runner->Camera = World->SpawnActor<ACameraActor>(Eye, Rotation);
		}
		if (ACameraActor* Camera = Runner->Camera.Get())
		{
			const FVector2D Size = UWidgetLayoutLibrary::GetViewportSize(World);
			const float Aspect = Size.Y > 1.f ? Size.X / Size.Y : 16.f / 9.f;
			const float HorizontalFov = FMath::RadiansToDegrees(2.f * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(ViewVerticalFov) * 0.5f) * Aspect));
			Camera->SetActorLocationAndRotation(Eye, Rotation);
			Camera->GetCameraComponent()->SetFieldOfView(HorizontalFov);
			Camera->GetCameraComponent()->bConstrainAspectRatio = false;
			if (PC->GetViewTarget() != Camera)
			{
				PC->SetViewTarget(Camera);
			}
		}
	}

	FString PocketText(const FDirectionalSnow& P)
	{
		return FString::Printf(TEXT("top %.2f +X %.2f -X %.2f +Y %.2f -Y %.2f"), P.Top, P.PX, P.NX, P.PY, P.NY);
	}

	/** The leader's pockets, the enemies' average and the objects' pockets (log + label). */
	FString DescribeSnow(UWorld* World, const UEnvironmentSubsystem& Environment)
	{
		const UCharacterSnowSubsystem* Snow = UCharacterSnowSubsystem::Get(World);
		const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		FDirectionalSnow Leader;
		if (Snow && Squad && Squad->GetLeader())
		{
			Snow->GetPockets(Squad->GetLeader(), Leader);
		}
		FDirectionalSnow Sum;
		int32 Count = 0;
		for (AEnemyCharacter* Enemy : UEnemyRegistrySubsystem::GetEnemyCharacters(World))
		{
			FDirectionalSnow Each;
			if (Snow && Snow->GetPockets(Enemy, Each))
			{
				for (int32 Index = 0; Index < FDirectionalSnow::PocketCount; ++Index)
				{
					Sum.Set(Index, Sum.Get(Index) + Each.Get(Index));
				}
				++Count;
			}
		}
		for (int32 Index = 0; Index < FDirectionalSnow::PocketCount; ++Index)
		{
			Sum.Set(Index, Sum.Get(Index) / FMath::Max(Count, 1));
		}
		return FString::Printf(TEXT("leader (local) %s | %d enemies avg (local) %s | objects (world) %s | ground %.2f"), *PocketText(Leader), Count, *PocketText(Sum),
			*PocketText(Environment.GetObjectSnow()), Environment.GetSnowAmount());
	}

	void SetLabel(UWorld* World, const UEnvironmentSubsystem& Environment, const FString& Phase)
	{
		const FVector Wind = Environment.GetWindDirection();
		Runner->Label = FString::Printf(TEXT("%s - %s, %s %02d:%02d, wind to (%.1f, %.1f) x%.2f - ground %.2f, objects top %.2f +X %.2f -Y %.2f"), *Runner->MapName, *Phase,
			*EnvironmentRules::WeatherToString(Environment.GetWeather()), FMath::FloorToInt(Environment.GetHour()), FMath::FloorToInt(FMath::Frac(Environment.GetHour()) * 60.f),
			Wind.X, Wind.Y, Environment.GetWindStrength(), Environment.GetSnowAmount(), Environment.GetObjectSnow().Top, Environment.GetObjectSnow().PX,
			Environment.GetObjectSnow().NY);
	}

	/** Holds the weather (re-forced before its dwell would blend away) and, for the melt, the hour. */
	void HoldConditions(UEnvironmentSubsystem& Environment, EWeatherState Weather, bool bHoldHour)
	{
		const FEnvironmentState& State = Environment.GetStateRef();
		if (State.Weather != Weather || State.WeatherElapsedSeconds > State.WeatherDwellSeconds * 0.5f)
		{
			Environment.ForceWeather(Weather, true);
		}
		if (bHoldHour && FMath::Abs(Environment.GetHour() - 12.f) > 0.05f)
		{
			Environment.SetHour(12.f);
		}
	}

	void Finish()
	{
		FRunner& R = *Runner;
		for (const FString& Line : R.Report)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[CharSnowReport] %s"), *Line);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots charsnow: done (%d stills, %d movie frames) -> %s"), R.ShotCount, R.MovieCount, *R.Dir);
		FCoreDelegates::OnEndFrame.Remove(R.EndFrameHandle);
		UDebugDrawService::Unregister(R.DrawHandle);
		FPlatformMisc::RequestExit(false, TEXT("EnvironmentShots charsnow"));
	}

	void Next(EStep Step)
	{
		Runner->Step = Step;
		Runner->StepFrame = 0;
	}

	/** Three views over frames F = 1..11 (windward, leeward, close on the leader; captured at F 3 / 6 / 9); true when done. */
	bool ThreeViews(UWorld* World, UEnvironmentSubsystem& Environment, const FVector& Focus, AOperativeCharacter* Leader, const FString& Phase, int32 Number, int32 F)
	{
		FRunner& R = *Runner;
		SetLabel(World, Environment, Phase);
		if (F <= 3)
		{
			View(World, Focus, WindwardYaw, ViewPitch, ViewDistance);
		}
		else if (F <= 6)
		{
			View(World, Focus, LeewardYaw, ViewPitch, ViewDistance);
		}
		else
		{
			View(World, Leader->GetActorLocation() + FVector(0.f, 0.f, 90.f), WindwardYaw, -14.f, 420.f);
		}
		const TCHAR* Names[] = { TEXT("windward"), TEXT("leeward"), TEXT("close") };
		if (F == 3 || F == 6 || F == 9)
		{
			// Captured at the end of the frame the view was set two frames earlier (the camera cut and the screenshot request settle).
			Capture(R.Dir / FString::Printf(TEXT("%s_%02d_%s_%s.png"), *R.MapName, Number, *Phase, Names[F / 3 - 1]));
			++R.ShotCount;
		}
		if (F == 1)
		{
			const FString Snow = DescribeSnow(World, Environment);
			UE_LOG(LogCodexTactics, Display, TEXT("[CharSnow] %s %s: %s"), *R.MapName, *Phase, *Snow);
			R.Report.Add(FString::Printf(TEXT("%s: %s"), *Phase, *Snow));
		}
		return F >= 11;
	}

	// --- A/B bodies ------------------------------------------------------------------------------------------------------------

	void SpawnAbBodies(UWorld* World, const FVector& Base)
	{
		FRunner& R = *Runner;
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(AbBodies); ++Index)
		{
			USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, AbBodies[Index].Mesh);
			UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, AbBodies[Index].Material);
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			// Bodies 3 m apart along +Y, all facing -X (the camera); the mesh's own forward is +Y (UE mannequin convention: yaw -90 to face -X).
			const FVector Location = Base + FVector(0.f, Index * 300.f, 0.f);
			ASkeletalMeshActor* Actor = World->SpawnActor<ASkeletalMeshActor>(Location, FRotator(0.f, 180.f - 90.f, 0.f), Params);
			if (!Actor || !Mesh)
			{
				UE_LOG(LogCodexTactics, Warning, TEXT("EnvironmentShots charsnow ab: %s body %s missing"), AbBodies[Index].Master, AbBodies[Index].Mesh);
				R.AbActors.Add(nullptr);
				continue;
			}
			USkeletalMeshComponent* Component = Actor->GetSkeletalMeshComponent();
			Component->SetMobility(EComponentMobility::Movable);
			Component->SetSkeletalMesh(Mesh);
			if (Material)
			{
				Component->SetMaterial(0, Material);
				TArray<UTexture*> Textures;
				Material->GetUsedTextures(Textures);
				for (UTexture* Texture : Textures)
				{
					if (UTexture2D* Texture2D = Cast<UTexture2D>(Texture))
					{
						Texture2D->SetForceMipLevelsToBeResident(60.f);
					}
				}
			}
			Component->SetForcedLOD(1);
			Component->bPauseAnims = true;
			R.AbActors.Add(Actor);
			UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots charsnow ab: %s = %s with %s (parent %s)"), AbBodies[Index].Master, *GetNameSafe(Mesh), *GetNameSafe(Material),
				Material && Material->GetMaterial() ? *Material->GetMaterial()->GetName() : TEXT("?"));
		}
	}

	/** The A/B phases of one body: the layer at 0, the material WITHOUT the layer (when its copy exists), the layer at 0 again (noise floor). */
	enum class EAbPhase : uint8 { Zero, Before, Zero2, Snow, Count };

	const TCHAR* AbPhaseName(EAbPhase Phase)
	{
		switch (Phase)
		{
		case EAbPhase::Zero: return TEXT("zero");
		case EAbPhase::Before: return TEXT("before");
		case EAbPhase::Zero2: return TEXT("zero2");
		default: return TEXT("snow");
		}
	}

	/** /Game/_CharSnowBefore/<MI>_Before: a temporary copy of the body's MI parented to the master as it was before the snow layer. */
	UMaterialInterface* LoadBeforeMaterial(int32 Index)
	{
		const FString Name = FPaths::GetBaseFilename(FString(AbBodies[Index].Material)) + TEXT("_Before");
		const FString Path = FString::Printf(TEXT("/Game/_CharSnowBefore/%s.%s"), *Name, *Name);
		return LoadObject<UMaterialInterface>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	void ApplyAbPhase(int32 Index, EAbPhase Phase)
	{
		ASkeletalMeshActor* Actor = Runner->AbActors.IsValidIndex(Index) ? Runner->AbActors[Index].Get() : nullptr;
		if (!Actor)
		{
			return;
		}
		USkeletalMeshComponent* Component = Actor->GetSkeletalMeshComponent();
		UMaterialInterface* Material = Phase == EAbPhase::Before ? LoadBeforeMaterial(Index) : LoadObject<UMaterialInterface>(nullptr, AbBodies[Index].Material);
		if (Material)
		{
			Component->SetMaterial(0, Material);
		}
		FDirectionalSnow Pockets;
		if (Phase == EAbPhase::Snow)
		{
			Pockets.Top = 1.f;
			Pockets.PX = 0.8f;
			Pockets.NY = 0.5f;
		}
		const float Data[5] = { Pockets.Top, Pockets.PX, Pockets.NX, Pockets.PY, Pockets.NY };
		Component->SetCustomPrimitiveDataFloatArray(CharacterSnowRules::FirstCustomDataIndex, TConstArrayView<float>(Data, 5));
	}

	/** The next phase of the current body that applies (Before only with its copy, Snow only with "snow"); Count = the body is done. */
	EAbPhase NextAbPhase(int32 Index, EAbPhase Phase)
	{
		for (int32 Next = static_cast<int32>(Phase) + 1; Next < static_cast<int32>(EAbPhase::Count); ++Next)
		{
			const EAbPhase Candidate = static_cast<EAbPhase>(Next);
			if ((Candidate == EAbPhase::Before || Candidate == EAbPhase::Zero2) && !LoadBeforeMaterial(Index))
			{
				continue;
			}
			if (Candidate == EAbPhase::Snow && !Runner->bAbSnow)
			{
				continue;
			}
			return Candidate;
		}
		return EAbPhase::Count;
	}

	void TickAb(UWorld* World, UEnvironmentSubsystem& Environment, AOperativeCharacter* Leader)
	{
		FRunner& R = *Runner;
		static FVector Base = FVector::ZeroVector;
		static EAbPhase Phase = EAbPhase::Zero;
		switch (R.Step)
		{
		case EStep::Warmup:
			if (R.StepFrame >= WarmupFrames && ShadersPending() == 0)
			{
				// The authored lighting (no presenter, no falling snow), no AA history, no on-screen messages, game time almost stopped (a static
				// sky): every capture of a body sees the same scene; only the material / the pockets change between the phases.
				AWeatherPresenterActor::SetVisualsEnabled(false);
				SetCVar(TEXT("r.AntiAliasingMethod"), 0);
				if (GEngine)
				{
					GEngine->Exec(World, TEXT("DisableAllScreenMessages"));
				}
				Base = Leader->GetActorLocation() + FVector(0.f, 0.f, 3000.f);
				SpawnAbBodies(World, Base);
				for (int32 Index = 0; Index < UE_ARRAY_COUNT(AbBodies); ++Index)
				{
					ApplyAbPhase(Index, EAbPhase::Zero);
					UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots charsnow ab: %s before-copy %s"), AbBodies[Index].Master,
						LoadBeforeMaterial(Index) ? *GetNameSafe(LoadBeforeMaterial(Index)->GetMaterial()) : TEXT("none (only the layer at 0 / snow)"));
				}
				// Game time almost stopped (not paused: a paused game keeps the old view target), so the sky stands still between the phases.
				World->GetWorldSettings()->SetTimeDilation(0.0001f);
				Phase = EAbPhase::Zero;
				Next(EStep::AbSetup);
			}
			break;
		case EStep::AbSetup:
		case EStep::AbSettle:
		{
			const FVector Body = Base + FVector(0.f, R.AbIndex * 300.f, 95.f);
			View(World, Body, 0.f, -5.f, 330.f);
			R.Label.Reset();
			if (R.StepFrame >= (R.AbIndex == 0 && Phase == EAbPhase::Zero ? 240 : 90) && ShadersPending() == 0)
			{
				const FString File = FString::Printf(TEXT("%s_%s.png"), AbBodies[R.AbIndex].Master, AbPhaseName(Phase));
				Capture(R.Dir / File);
				++R.ShotCount;
				UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots charsnow ab: %s"), *File);
				Next(EStep::AbShots);
			}
			break;
		}
		case EStep::AbShots:
			if (R.StepFrame >= 5)
			{
				Phase = NextAbPhase(R.AbIndex, Phase);
				if (Phase == EAbPhase::Count)
				{
					++R.AbIndex;
					Phase = EAbPhase::Zero;
				}
				if (R.AbIndex >= UE_ARRAY_COUNT(AbBodies))
				{
					World->GetWorldSettings()->SetTimeDilation(1.f);
					Next(EStep::Done);
				}
				else
				{
					ApplyAbPhase(R.AbIndex, Phase);
					Next(EStep::AbSetup);
				}
			}
			break;
		default:
			Next(EStep::Done);
			break;
		}
		(void)Environment;
	}

	// --- Scene -----------------------------------------------------------------------------------------------------------------

	void Tick()
	{
		FRunner& R = *Runner;
		++R.Frame;
		++R.StepFrame;
		UWorld* World = FrontendSmokeUtils::FindGameWorld();
		if (R.Frame > MaxFrames)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots charsnow: timeout in step %d"), static_cast<int32>(R.Step));
			Finish();
			return;
		}
		UEnvironmentSubsystem* Environment = World ? UEnvironmentSubsystem::Get(World) : nullptr;
		if (R.bStartFrames)
		{
			// The level start as the player sees it: the frames counted from the command (run by -ExecCmds right after the map loaded).
			static const int32 StartCaptureFrames[] = { 1, 2, 30, 60, 150 };
			const AWeatherPresenterActor* StartPresenter = World ? AWeatherPresenterActor::Find(World) : nullptr;
			for (const int32 CaptureFrame : StartCaptureFrames)
			{
				if (R.Frame == CaptureFrame && World)
				{
					FString Map = World->GetMapName();
					Map.RemoveFromStart(World->StreamingLevelsPrefix);
					Capture(R.Dir / FString::Printf(TEXT("start_%s_f%03d.png"), *Map, CaptureFrame));
					++R.ShotCount;
					UE_LOG(LogCodexTactics, Display, TEXT("[CharSnowStart] frame %d (game %.2f s): presenter %s, applying %d, applications %d, %s"), CaptureFrame,
						World->GetTimeSeconds(), StartPresenter ? TEXT("yes") : TEXT("no"), StartPresenter && StartPresenter->IsApplying() ? 1 : 0,
						StartPresenter ? StartPresenter->GetApplyCount() : 0, Environment ? *Environment->Describe() : TEXT("no environment"));
				}
			}
			if (R.Frame > StartCaptureFrames[UE_ARRAY_COUNT(StartCaptureFrames) - 1] + 5)
			{
				Finish();
			}
			return;
		}
		USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
		if (!World || !Environment || !Environment->IsInitialised() || !Squad || !Squad->GetLeader())
		{
			return;
		}
		KeepSquadSafe(World);
		UWidgetLayoutLibrary::RemoveAllWidgets(World);
		AOperativeCharacter* Leader = Squad->GetLeader();
		if (R.Step == EStep::Done)
		{
			Finish();
			return;
		}
		if (R.bAb)
		{
			TickAb(World, *Environment, Leader);
			return;
		}
		const AWeatherPresenterActor* Presenter = AWeatherPresenterActor::Find(World);
		UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
		UCharacterSnowSubsystem* CharacterSnow = UCharacterSnowSubsystem::Get(World);
		const FVector Focus = Leader->GetActorLocation();

		switch (R.Step)
		{
		case EStep::Warmup:
			if (R.StepFrame >= WarmupFrames && Presenter && Presenter->IsApplying() && ShadersPending() == 0)
			{
				R.MapName = World->GetMapName();
				R.MapName.RemoveFromStart(World->StreamingLevelsPrefix);
				const bool bMovementTest = R.MapName.Contains(TEXT("MovementTest"));
				if (bMovementTest)
				{
					SmokeUtils::PlaceSquadAtTestStart(World);
				}
				if (Waves)
				{
					Waves->ClearAllEnemies();
					Waves->StartWave(bMovementTest ? 2 : 1);
				}
				Environment->SetClockScale(FillClockScale);
				Environment->SetHour(12.f);
				Environment->ForceWeather(EWeatherState::Blizzard, true);
				Environment->SetWindDirectionOverride(FVector(-1.f, 0.f, 0.f)); // FROM +X
				Environment->SetObjectSnow([&Environment]() { FDirectionalSnow P; P.Top = Environment->GetSnowAmount(); return P; }());
				if (CharacterSnow)
				{
					CharacterSnow->ClearAll();
				}
				UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots charsnow: %s, wave started (%d alive), character snow %s: %s"), *R.MapName,
					Waves ? Waves->GetAliveEnemyCount() : 0, CharacterSnow ? TEXT("on") : TEXT("MISSING"), *Environment->Describe());
				Next(EStep::FillX);
			}
			break;

		case EStep::FillX:
		case EStep::FillY:
		{
			const bool bX = R.Step == EStep::FillX;
			HoldConditions(*Environment, EWeatherState::Blizzard, true);
			View(World, Focus, WindwardYaw, ViewPitch, ViewDistance);
			SetLabel(World, *Environment, bX ? TEXT("blizzard, wind FROM +X") : TEXT("blizzard, wind FROM -Y"));
			if (R.bMovie && R.StepFrame % MovieEvery == 0)
			{
				Capture(R.Dir / (TEXT("movie_") + R.MapName) / FString::Printf(TEXT("frame_%04d.png"), R.MovieCount++));
			}
			if (R.StepFrame >= FillFrames)
			{
				// The stills: the clock stopped (nothing melts), the blizzard lifted to Clear 12:00 so the snow reads in a clear light.
				Environment->SetClockScale(0.f);
				Environment->ForceWeather(EWeatherState::Clear, true);
				Environment->SetHour(12.f);
				Next(bX ? EStep::ShotsX : EStep::ShotsY);
			}
			break;
		}

		case EStep::ShotsX:
			if (ThreeViews(World, *Environment, Focus, Leader, TEXT("1_windFromPX"), 1, R.StepFrame))
			{
				Environment->SetWindDirectionOverride(FVector(0.f, 1.f, 0.f)); // FROM -Y
				Environment->ForceWeather(EWeatherState::Blizzard, true);
				Environment->SetClockScale(FillClockScale);
				Next(EStep::FillY);
			}
			break;

		case EStep::ShotsY:
			if (ThreeViews(World, *Environment, Focus, Leader, TEXT("2_windFromNY"), 2, R.StepFrame))
			{
				Environment->ClearWindDirectionOverride();
				Environment->ForceWeather(EWeatherState::Clear, true);
				Environment->SetHour(12.f);
				Environment->SetClockScale(R.bMelt ? MeltClockScale : 0.f);
				R.MeltShot = 0;
				R.MeltFrame = 0;
				R.ViewFrame = -1;
				Next(R.bMelt ? EStep::Melt : (R.bPerf ? EStep::Perf : EStep::Done));
			}
			break;

		case EStep::Melt:
		{
			// The melt clock runs only between the captures (MeltFrame counts the frames it ran at x20: 30 frames = 20 design s).
			HoldConditions(*Environment, EWeatherState::Clear, true);
			if (R.ViewFrame < 0)
			{
				View(World, Focus, WindwardYaw, ViewPitch, ViewDistance);
				SetLabel(World, *Environment, FString::Printf(TEXT("Clear 12:00 melt, %d design min"), FMath::RoundToInt(R.MeltFrame / 30.f * MeltClockScale / 60.f)));
				if (R.MeltFrame >= MeltCaptureFrames[R.MeltShot])
				{
					Environment->SetClockScale(0.f);
					R.ViewFrame = 0;
				}
				else
				{
					++R.MeltFrame;
				}
			}
			else
			{
				++R.ViewFrame;
				const int32 Minutes = FMath::RoundToInt(MeltCaptureFrames[R.MeltShot] / 30.f * MeltClockScale / 60.f);
				if (ThreeViews(World, *Environment, Focus, Leader, FString::Printf(TEXT("3_melt%02dmin"), Minutes), 3 + R.MeltShot, R.ViewFrame))
				{
					R.ViewFrame = -1;
					++R.MeltShot;
					if (R.MeltShot >= UE_ARRAY_COUNT(MeltCaptureFrames))
					{
						Environment->SetClockScale(1.f);
						Next(R.bPerf ? EStep::Perf : EStep::Done);
					}
					else
					{
						Environment->SetClockScale(MeltClockScale);
					}
				}
			}
			break;
		}

		// Cost with the wave alive: the subsystem's game-thread time per frame (on), and the frame times off / on.
		case EStep::Perf:
		{
			const bool bOn = (R.PerfIndex % 2) == 1;
			if (R.StepFrame == 1)
			{
				SetCVar(TEXT("Codex.Env.CharacterSnow"), bOn ? 1 : 0);
				HoldConditions(*Environment, EWeatherState::Blizzard, true);
				Environment->SetClockScale(1.f);
			}
			View(World, Focus, WindwardYaw, ViewPitch, ViewDistance);
			SetLabel(World, *Environment, FString::Printf(TEXT("perf, character snow %s"), bOn ? TEXT("ON") : TEXT("OFF")));
			if (R.StepFrame == PerfSettleFrames && CharacterSnow)
			{
				R.CpuStart = CharacterSnow->GetMetrics().CpuSeconds;
				R.TicksStart = CharacterSnow->GetMetrics().Ticks;
				R.PushesStart = CharacterSnow->GetMetrics().Pushes;
			}
			if (R.StepFrame > PerfSettleFrames)
			{
				R.Samples[bOn ? 1 : 0].Add();
			}
			if (R.StepFrame >= PerfSettleFrames + PerfFrames)
			{
				if (bOn && CharacterSnow)
				{
					const FCharacterSnowMetrics& M = CharacterSnow->GetMetrics();
					const int32 Ticks = FMath::Max(1, M.Ticks - R.TicksStart);
					const FString Line = FString::Printf(TEXT("%s | %d enemies alive | subsystem %.4f ms / frame (game thread, %d frames) | %d characters processed, %d tracked | %.1f CPD pushes / s"),
						*R.MapName, Waves ? Waves->GetAliveEnemyCount() : 0, (M.CpuSeconds - R.CpuStart) * 1000.0 / Ticks, Ticks, M.Processed, M.Tracked,
						(M.Pushes - R.PushesStart) / (PerfFrames / 30.f));
					UE_LOG(LogCodexTactics, Display, TEXT("[CharSnowPerf] %s"), *Line);
					R.Report.Add(Line);
				}
				++R.PerfIndex;
				if (R.PerfIndex >= 4)
				{
					const FString Line = FString::Printf(TEXT("%s | Codex.Env.CharacterSnow 0: %s | 1: %s"), *R.MapName, *R.Samples[0].Describe(), *R.Samples[1].Describe());
					UE_LOG(LogCodexTactics, Display, TEXT("[CharSnowPerf] %s"), *Line);
					R.Report.Add(Line);
					SetCVar(TEXT("Codex.Env.CharacterSnow"), 1);
					Next(EStep::Done);
				}
				else
				{
					Next(EStep::Perf);
				}
			}
			break;
		}

		default:
			Next(EStep::Done);
			break;
		}
	}

	void Run(const TArray<FString>& Args)
	{
		auto Has = [&Args](const TCHAR* Word)
		{
			return Args.ContainsByPredicate([Word](const FString& Arg) { return Arg.Equals(Word, ESearchCase::IgnoreCase); });
		};
		Runner = MakeShared<FRunner>();
		Runner->bMovie = !Has(TEXT("nomovie"));
		Runner->bPerf = !Has(TEXT("noperf"));
		Runner->bMelt = !Has(TEXT("nomelt"));
		Runner->bAb = Has(TEXT("ab"));
		Runner->bStartFrames = Has(TEXT("startframes"));
		Runner->bAbSnow = Has(TEXT("snow"));
		FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / TEXT("..") / TEXT("Environment") / TEXT("charsnow"));
		if (Runner->bAb)
		{
			const int32 TagIndex = Args.IndexOfByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("ab"), ESearchCase::IgnoreCase); }) + 1;
			Runner->Tag = Args.IsValidIndex(TagIndex) && !Args[TagIndex].Equals(TEXT("snow"), ESearchCase::IgnoreCase) ? Args[TagIndex] : FString(TEXT("run"));
			Dir /= TEXT("ab_") + Runner->Tag;
		}
		FPaths::CollapseRelativeDirectories(Dir);
		IFileManager::Get().MakeDirectory(*Dir, true);
		Runner->Dir = Dir;
		Runner->DrawHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateStatic(&OnDraw));
		Runner->EndFrameHandle = FCoreDelegates::OnEndFrame.AddStatic(&Tick);
		UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots charsnow: %s -> %s"), Runner->bAb ? *(TEXT("A/B ") + Runner->Tag) : TEXT("scene"), *Dir);
	}
}

#endif // !UE_BUILD_SHIPPING
