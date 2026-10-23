// Dev-only: mode "tracks" of CodexTactics.EnvironmentShots (dispatched from EnvironmentShotsCommand.cpp) - rendered stills + a 10 s clip of the
// interactive snow (docs/systems/environment-weather.md section 14, phases 1 + 3) on the snow ground overlay (section 12.d). Run on the map it
// starts on (Frozen Factory recommended), offscreen, fixed 30 fps:
//   UnrealEditor.exe CodexTactics.uproject /Game/Maps/L_Demo01_B_FrozenFactory -game -RenderOffscreen -ResX=1920 -ResY=1080 -nosound
//     -benchmark -fps=30 -ExecCmds="CodexTactics.EnvironmentShots tracks [nomovie]"
// Output (Saved/Screenshots/Environment/tracks/), Clear 12:00, SnowAmount 0.85, the squad made safe, enemies cleared:
//   squad_walk.png       the squad walks 9 m (boot prints of three operatives)
//   prone_crawl.png      the leader crawls 5 m prone (a body furrow)
//   grenade_crater.png   a grenade detonated beside the walk (AGrenadeActor::Detonate -> GameplayEvents::Explosion: bowl, rim, thrown snow)
//   barrel_drag.png      the engineer pushes a fuel barrel 5 m (URelocationSubsystem: the real-time drag trail)
//   bullet_dents.png     16 missed shots (UCombatFeedbackSubsystem::SpawnTracer ending short of the floor -> dents)
//   tracks_overview.png  all of it from the tactical distance; tracks_off.png the same with Codex.Env.SnowTracks 0
//   movie/frame_NNNN.png 10 s at 15 fps: the squad walks, a grenade goes off, a burst of misses, a barrel is pushed
//                        (encode: ffmpeg -y -framerate 15 -i frame_%04d.png -vf scale=960:-2 -c:v libx264 -pix_fmt yuv420p -crf 23 tracks_10s.mp4)
// A label on the debug canvas names the shot and the subsystem's counters. UE-only, no Godot reference.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "CanvasItem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/OperativeMovementRules.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "CollisionQueryParams.h"
#include "Combat/CombatFeedbackSubsystem.h"
#include "Combat/GrenadeActor.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Debug/DebugDrawService.h"
#include "Debug/FrontendSmokeUtils.h"
#include "Debug/SmokeUtils.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "Environment/EnvironmentSubsystem.h"
#include "Environment/SnowInteractionSubsystem.h"
#include "Environment/WeatherPresenterActor.h"
#include "GameFramework/HUD.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/BarrelActor.h"
#include "Interactables/RelocationSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

namespace EnvironmentTracksShots
{
	constexpr int32 MaxFrames = 30 * 300;
	constexpr int32 WarmupFrames = 150;
	constexpr int32 QuietFrames = 45;
	constexpr int32 SettleFrames = 40;
	constexpr int32 WalkFrames = 330;
	constexpr int32 DragFrames = 600;
	constexpr int32 MovieFrames = 300; // 10 s at -fps=30
	constexpr int32 MovieEvery = 2;    // 15 fps
	constexpr float SnowAmount = 0.85f;
	constexpr int32 DentShots = 16;

	enum class EStep : uint8
	{
		Warmup,
		SquadWalk,
		Crawl,
		Grenade,
		Drag,
		Dents,
		Overview,
		MovieSetup,
		Movie,
		Done
	};

	struct FShotView
	{
		FVector Focus = FVector::ZeroVector;
		float Yaw = 0.f;
		float Pitch = -35.26f;
		float Distance = 2600.f;
		float HorizontalFov = 60.f;
	};

	struct FTracksRunner
	{
		EStep Step = EStep::Warmup;
		int32 Frame = 0;
		int32 StepFrame = 0;
		int32 Quiet = 0;
		int32 ClipCount = 0;
		int32 Sub = 0;
		bool bMovie = true;
		FString Dir;
		FVector Home = FVector::ZeroVector;
		FVector Forward = FVector::ForwardVector;
		FVector Side = FVector::RightVector;
		float ViewYaw = 0.f;
		FVector CraterPoint = FVector::ZeroVector;
		FVector DentPoint = FVector::ZeroVector;
		FVector BarrelGoal = FVector::ZeroVector;
		FVector CrawlGoal = FVector::ZeroVector;
		TWeakObjectPtr<ABarrelActor> Barrel;
		bool bDragStarted = false;
		FShotView View;
		TArray<FString> Lines;
		TArray<FString> Report;
		TWeakObjectPtr<ACameraActor> Camera;
		FDelegateHandle EndFrameHandle;
		FDelegateHandle DrawHandle;
	};

	TSharedPtr<FTracksRunner> KRunner;

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
		if (UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>())
		{
			if (Waves->GetAliveEnemyCount() > 0)
			{
				Waves->ClearAllEnemies();
			}
		}
	}

	void ApplyView(UWorld* World)
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
		const FShotView& View = KRunner->View;
		const FRotator Rotation(View.Pitch, View.Yaw, 0.f);
		const FVector Eye = View.Focus - Rotation.Vector() * View.Distance;
		if (!KRunner->Camera.IsValid())
		{
			KRunner->Camera = World->SpawnActor<ACameraActor>(Eye, Rotation);
		}
		if (ACameraActor* Camera = KRunner->Camera.Get())
		{
			Camera->SetActorLocationAndRotation(Eye, Rotation);
			Camera->GetCameraComponent()->SetFieldOfView(View.HorizontalFov);
			Camera->GetCameraComponent()->bConstrainAspectRatio = false;
			if (PC->GetViewTarget() != Camera)
			{
				PC->SetViewTarget(Camera);
			}
		}
	}

	/** The tactical camera's numbers (pitch -35.26, vertical FOV 30) on Focus from Distance. */
	FShotView TacticalView(UWorld* World, const FVector& Focus, float Yaw, float Distance, float Pitch = -35.26f)
	{
		const FVector2D Size = UWidgetLayoutLibrary::GetViewportSize(World);
		const float Aspect = Size.Y > 1.f ? Size.X / Size.Y : 16.f / 9.f;
		FShotView View;
		View.Focus = Focus;
		View.Yaw = Yaw;
		View.Pitch = Pitch;
		View.Distance = Distance;
		View.HorizontalFov = FMath::RadiansToDegrees(2.f * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(30.f) * 0.5f) * Aspect));
		return View;
	}

	void Label(USnowInteractionSubsystem* Tracks, const FString& What)
	{
		KRunner->Lines.Reset();
		KRunner->Lines.Add(FString::Printf(TEXT("%s - interactive snow: %s"), *FPaths::GetBaseFilename(GWorld ? GWorld->GetMapName() : FString()), *What));
		if (Tracks)
		{
			const USnowInteractionSubsystem::FMetrics& M = Tracks->GetMetrics();
			KRunner->Lines.Add(FString::Printf(TEXT("stamps %lld  craters %lld  dents %lld  drag segments %lld  window %s"), M.StampsDrawn, M.Craters, M.Dents,
				M.DragSegments, Tracks->IsActive() ? TEXT("on") : TEXT("off")));
		}
	}

	void OnDraw(UCanvas* Canvas, APlayerController* /*Controller*/)
	{
		if (!KRunner.IsValid() || !Canvas || !GEngine || KRunner->Lines.IsEmpty())
		{
			return;
		}
		UFont* Font = GEngine->GetMediumFont();
		const float Scale = FMath::Clamp(Canvas->SizeX / 1280.f, 0.6f, 1.6f);
		float Y = 12.f;
		FCanvasTileItem Tile(FVector2D(10.f, 8.f), FVector2D(Canvas->SizeX - 20.f, 22.f * Scale * KRunner->Lines.Num() + 10.f), FLinearColor(0.f, 0.f, 0.f, 0.55f));
		Tile.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Tile);
		for (const FString& Line : KRunner->Lines)
		{
			FCanvasTextItem Item(FVector2D(18.f, Y), FText::FromString(Line), Font, FLinearColor(1.f, 0.9f, 0.35f));
			Item.Scale = FVector2D(Scale, Scale);
			Item.EnableShadow(FLinearColor::Black);
			Canvas->DrawItem(Item);
			Y += 22.f * Scale;
		}
	}

	void Capture(const FString& File)
	{
		FScreenshotRequest::RequestScreenshot(KRunner->Dir / File, /*bInShowUI*/ true, /*bAddFilenameSuffix*/ false);
	}

	void Finish()
	{
		for (const FString& Line : KRunner->Report)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[TracksShots] %s"), *Line);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots tracks: done (%d movie frames) -> %s"), KRunner->ClipCount, *KRunner->Dir);
		SetCVar(TEXT("Codex.Env.SnowTracks"), 1);
		FCoreDelegates::OnEndFrame.Remove(KRunner->EndFrameHandle);
		UDebugDrawService::Unregister(KRunner->DrawHandle);
		FPlatformMisc::RequestExit(false, TEXT("EnvironmentShots tracks"));
	}

	void SetConditions(UEnvironmentSubsystem& Environment)
	{
		if (Environment.GetClockScale() != 0.f)
		{
			Environment.SetClockScale(0.f);
		}
		if (Environment.GetWeather() != EWeatherState::Clear || Environment.GetTransitionBlend() > 0.f)
		{
			Environment.ForceWeather(EWeatherState::Clear, true);
		}
		if (!FMath::IsNearlyEqual(Environment.GetHour(), 12.f, 0.01f))
		{
			Environment.SetHour(12.f);
		}
		if (!FMath::IsNearlyEqual(Environment.GetSnowAmount(), SnowAmount, 0.0005f))
		{
			Environment.SetSnowAmount(SnowAmount);
		}
	}

	FVector GroundBelow(UWorld* World, const FVector& Point)
	{
		FHitResult Hit;
		const FCollisionQueryParams Params(SCENE_QUERY_STAT(TracksShotsGround), false);
		if (World->LineTraceSingleByObjectType(Hit, Point + FVector(0., 0., 300.), Point - FVector(0., 0., 600.), FCollisionObjectQueryParams(ECC_WorldStatic), Params))
		{
			return Hit.ImpactPoint;
		}
		return Point;
	}

	/** A missed shot from Muzzle at the floor spot Ground: the tracer ends 1.2 m above the floor short of it; its extended ray meets the floor there. */
	void FireMiss(UWorld* World, const FVector& Muzzle, const FVector& Ground)
	{
		UCombatFeedbackSubsystem* Feedback = World->GetSubsystem<UCombatFeedbackSubsystem>();
		const FVector Slope = (Ground - Muzzle).GetSafeNormal();
		if (!Feedback || Slope.Z > -0.02)
		{
			return;
		}
		const FVector End = Ground - Slope * FMath::Min(120. / -Slope.Z, FVector::Dist(Muzzle, Ground) * 0.6);
		Feedback->SpawnTracer(Muzzle, End, UCombatFeedbackSubsystem::DefaultTracerColor(), EDamageType::Kinetic);
	}

	void Detonate(UWorld* World, const FVector& Point)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AGrenadeActor* Grenade = World->SpawnActor<AGrenadeActor>(Point + FVector(0., 0., 15.), FRotator::ZeroRotator, Params))
		{
			Grenade->Detonate();
		}
	}

	ABarrelActor* SpawnBarrel(UWorld* World, const FVector& Point)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		return World->SpawnActor<ABarrelActor>(GroundBelow(World, Point) + FVector(0., 0., 5.), FRotator::ZeroRotator, Params);
	}

	void OrderSquad(USquadSubsystem& Squad, const FVector& Goal, const FVector& Side)
	{
		int32 Index = 0;
		for (AOperativeCharacter* Each : Squad.GetMembers())
		{
			Each->SetStance(EOperativeStance::Standing);
			Each->OrderMoveTo(Goal + Side * ((Index % 3) - 1) * 130.f - FVector(0., 0., 0.), false);
			++Index;
		}
	}

	bool SquadArrived(USquadSubsystem& Squad, const FVector& Goal)
	{
		for (AOperativeCharacter* Each : Squad.GetMembers())
		{
			if (FVector::Dist2D(Each->GetActorLocation(), Goal) > 320.f)
			{
				return false;
			}
		}
		return true;
	}

	AOperativeCharacter* Worker(USquadSubsystem& Squad)
	{
		const TArray<AOperativeCharacter*> Members = Squad.GetMembers();
		return Members.Num() > 1 ? Members[1] : Squad.GetLeader();
	}

	/** Advances the step after Settle frames past Ready, capturing File on the settle frame. */
	bool ShotAndNext(const FString& File, int32 ReadyFrame, EStep Next, USnowInteractionSubsystem* Tracks)
	{
		FTracksRunner& R = *KRunner;
		if (R.StepFrame == ReadyFrame + SettleFrames)
		{
			Capture(File);
			R.Report.Add(FString::Printf(TEXT("%s (stamps %lld, craters %lld, dents %lld, drag segments %lld)"), *File, Tracks ? Tracks->GetMetrics().StampsDrawn : 0ll,
				Tracks ? Tracks->GetMetrics().Craters : 0ll, Tracks ? Tracks->GetMetrics().Dents : 0ll, Tracks ? Tracks->GetMetrics().DragSegments : 0ll));
		}
		if (R.StepFrame >= ReadyFrame + SettleFrames + 8)
		{
			R.Step = Next;
			R.StepFrame = 0;
			R.Sub = 0;
			return true;
		}
		return false;
	}

	void Tick()
	{
		FTracksRunner& R = *KRunner;
		++R.Frame;
		++R.StepFrame;
		UWorld* World = FrontendSmokeUtils::FindGameWorld();
		if (R.Frame > MaxFrames)
		{
			R.Report.Add(FString::Printf(TEXT("timeout in step %d"), static_cast<int32>(R.Step)));
			Finish();
			return;
		}
		UEnvironmentSubsystem* Environment = World ? UEnvironmentSubsystem::Get(World) : nullptr;
		USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!World || !Environment || !Environment->IsInitialised() || !Leader)
		{
			return;
		}
		KeepSquadSafe(World);
		UWidgetLayoutLibrary::RemoveAllWidgets(World);
		SetConditions(*Environment);
		const AWeatherPresenterActor* Presenter = AWeatherPresenterActor::Find(World);
		USnowInteractionSubsystem* Tracks = USnowInteractionSubsystem::Get(World);
		R.Quiet = ShadersPending() == 0 ? R.Quiet + 1 : 0;
		const FVector WalkGoal = R.Home + R.Forward * 900.f;

		switch (R.Step)
		{
		case EStep::Warmup:
			R.View = TacticalView(World, Leader->GetActorLocation(), 30.f, 2600.f);
			ApplyView(World);
			Label(Tracks, TEXT("compiling shaders"));
			if (R.StepFrame >= WarmupFrames && Presenter && Presenter->IsApplying() && R.Quiet >= QuietFrames)
			{
				if (!Tracks || !Tracks->IsActive())
				{
					R.Report.Add(TEXT("USnowInteractionSubsystem not active: nothing to shoot"));
					Finish();
					return;
				}
				R.Home = Leader->GetActorLocation();
				R.Forward = Leader->GetActorForwardVector().GetSafeNormal2D();
				R.Forward = R.Forward.IsNearlyZero() ? FVector::ForwardVector : R.Forward;
				// A clear walk (navmesh): the forward goal may turn.
				const FVector Goal = SmokeUtils::ClearPoint(World, R.Home, R.Home + R.Forward * 900.f);
				R.Forward = (Goal - R.Home).GetSafeNormal2D();
				R.Side = FVector::CrossProduct(FVector::UpVector, R.Forward);
				R.ViewYaw = static_cast<float>(R.Forward.Rotation().Yaw) + 60.f;
				R.Report.Add(FString::Printf(TEXT("ready after %d frames; home %s, forward %s; presenter %s"), R.Frame, *R.Home.ToCompactString(),
					*R.Forward.ToCompactString(), *Presenter->DescribeAdopted()));
				Tracks->ClearTracks();
				Tracks->ResetMetrics();
				R.Step = EStep::SquadWalk;
				R.StepFrame = 0;
			}
			break;

		case EStep::SquadWalk:
			R.View = TacticalView(World, R.Home + R.Forward * 450.f, R.ViewYaw, 1700.f, -45.f);
			ApplyView(World);
			Label(Tracks, TEXT("the squad walks 9 m (boot prints)"));
			if (R.StepFrame == 1)
			{
				OrderSquad(*Squad, WalkGoal, R.Side);
			}
			if (R.Sub == 0 && (SquadArrived(*Squad, WalkGoal) || R.StepFrame > WalkFrames))
			{
				R.Sub = R.StepFrame;
			}
			if (R.Sub > 0)
			{
				ShotAndNext(TEXT("squad_walk.png"), R.Sub, EStep::Crawl, Tracks);
			}
			break;

		case EStep::Crawl:
		{
			if (R.StepFrame == 1)
			{
				R.CrawlGoal = SmokeUtils::ClearPoint(World, Leader->GetActorLocation(), Leader->GetActorLocation() + R.Side * 500.f);
			}
			const FVector CrawlGoal = R.CrawlGoal;
			R.View = TacticalView(World, (WalkGoal + CrawlGoal) * 0.5, R.ViewYaw, 1100.f, -50.f);
			ApplyView(World);
			Label(Tracks, TEXT("the leader crawls 5 m prone (a body furrow)"));
			if (R.StepFrame == 1)
			{
				Leader->SetStance(EOperativeStance::Prone);
			}
			if (R.StepFrame == 30)
			{
				Leader->OrderMoveTo(CrawlGoal, false);
			}
			if (R.Sub == 0 && R.StepFrame > 30 && (FVector::Dist2D(Leader->GetActorLocation(), CrawlGoal) < 80.f || R.StepFrame > 600))
			{
				R.Sub = R.StepFrame;
			}
			if (R.Sub > 0 && ShotAndNext(TEXT("prone_crawl.png"), R.Sub, EStep::Grenade, Tracks))
			{
				Leader->SetStance(EOperativeStance::Standing);
			}
			break;
		}

		case EStep::Grenade:
			R.CraterPoint = GroundBelow(World, R.Home + R.Forward * 450.f - R.Side * 650.f);
			R.View = TacticalView(World, R.CraterPoint, R.ViewYaw, 1000.f, -55.f);
			ApplyView(World);
			Label(Tracks, TEXT("a grenade went off (crater, rim, thrown snow)"));
			if (R.StepFrame == 10)
			{
				Detonate(World, R.CraterPoint);
			}
			ShotAndNext(TEXT("grenade_crater.png"), 30, EStep::Drag, Tracks);
			break;

		case EStep::Drag:
		{
			URelocationSubsystem* Relocation = World->GetSubsystem<URelocationSubsystem>();
			AOperativeCharacter* Engineer = Worker(*Squad);
			if (R.StepFrame == 1)
			{
				const FVector Start = SmokeUtils::ClearPoint(World, WalkGoal, WalkGoal - R.Side * 350.f);
				R.BarrelGoal = SmokeUtils::ClearPoint(World, Start, Start - R.Forward * 500.f);
				R.Barrel = SpawnBarrel(World, Start);
				if (R.Barrel.IsValid() && Relocation && Engineer)
				{
					Relocation->ExecuteRelocate(Engineer, R.Barrel.Get(), FVector(R.BarrelGoal.X, R.BarrelGoal.Y, R.Barrel->GetActorLocation().Z), 0.f);
				}
				R.bDragStarted = false;
			}
			const FVector Middle = R.Barrel.IsValid() ? (R.Barrel->GetActorLocation() + R.BarrelGoal) * 0.5 : R.BarrelGoal;
			R.View = TacticalView(World, R.StepFrame < 3 ? Middle : R.View.Focus, R.ViewYaw, 1300.f, -48.f);
			ApplyView(World);
			Label(Tracks, TEXT("the engineer pushes a fuel barrel 5 m (drag trail)"));
			const int32 TaskCount = Relocation ? Relocation->GetActiveTaskCount() : 0;
			R.bDragStarted |= TaskCount > 0;
			if (R.Sub == 0 && R.StepFrame > 5 && ((R.bDragStarted && TaskCount == 0) || R.StepFrame > DragFrames))
			{
				R.Sub = R.StepFrame;
			}
			if (R.Sub > 0)
			{
				ShotAndNext(TEXT("barrel_drag.png"), R.Sub, EStep::Dents, Tracks);
			}
			break;
		}

		case EStep::Dents:
		{
			R.DentPoint = GroundBelow(World, R.Home + R.Forward * 250.f + R.Side * 350.f);
			R.View = TacticalView(World, R.DentPoint, R.ViewYaw, 750.f, -58.f);
			ApplyView(World);
			Label(Tracks, TEXT("missed shots hit the snow (bullet dents)"));
			if (R.StepFrame >= 10 && R.StepFrame < 10 + DentShots)
			{
				const int32 Index = R.StepFrame - 10;
				const TArray<AOperativeCharacter*> Members = Squad->GetMembers();
				AOperativeCharacter* Shooter = Members.IsEmpty() ? Leader : Members[Index % Members.Num()];
				const FVector Spread(((Index * 37) % 11 - 5) * 14., ((Index * 53) % 11 - 5) * 14., 0.);
				FireMiss(World, Shooter->GetActorLocation() + FVector(0., 0., 150.), R.DentPoint + Spread);
			}
			ShotAndNext(TEXT("bullet_dents.png"), 10 + DentShots + 10, EStep::Overview, Tracks);
			break;
		}

		case EStep::Overview:
		{
			R.View = TacticalView(World, R.Home + R.Forward * 450.f, R.ViewYaw, 2600.f);
			ApplyView(World);
			const bool bOff = R.Sub == 1;
			Label(Tracks, bOff ? TEXT("the same view with Codex.Env.SnowTracks 0") : TEXT("overview: walk, crawl, crater, drag, dents"));
			if (R.StepFrame == SettleFrames)
			{
				Capture(bOff ? TEXT("tracks_off.png") : TEXT("tracks_overview.png"));
				R.Report.Add(FString::Printf(TEXT("%s"), bOff ? TEXT("tracks_off.png") : TEXT("tracks_overview.png")));
			}
			if (R.StepFrame >= SettleFrames + 8)
			{
				if (!bOff)
				{
					SetCVar(TEXT("Codex.Env.SnowTracks"), 0);
					R.Sub = 1;
					R.StepFrame = 0;
				}
				else
				{
					SetCVar(TEXT("Codex.Env.SnowTracks"), 1);
					R.Step = R.bMovie ? EStep::MovieSetup : EStep::Done;
					R.StepFrame = 0;
					R.Sub = 0;
				}
			}
			break;
		}

		case EStep::MovieSetup:
		{
			// Back to the start: the squad gathers at the home spot while the window comes back on (cleared).
			R.View = TacticalView(World, R.Home + R.Forward * 450.f, R.ViewYaw - 20.f, 2100.f, -40.f);
			ApplyView(World);
			Label(Tracks, TEXT("10 s: the squad advances, a grenade, a burst of misses, a barrel pushed"));
			if (R.StepFrame == 1)
			{
				OrderSquad(*Squad, R.Home, R.Side);
			}
			if (R.StepFrame > 60 && (SquadArrived(*Squad, R.Home) || R.StepFrame > 420))
			{
				if (Tracks)
				{
					Tracks->ClearTracks();
					Tracks->ResetMetrics();
				}
				R.Barrel = SpawnBarrel(World, SmokeUtils::ClearPoint(World, R.Home, R.Home - R.Side * 300.f));
				R.Step = EStep::Movie;
				R.StepFrame = 0;
				R.ClipCount = 0;
			}
			break;
		}

		case EStep::Movie:
		{
			const float T = FMath::Clamp(static_cast<float>(R.StepFrame) / MovieFrames, 0.f, 1.f);
			R.View = TacticalView(World, R.Home + R.Forward * 450.f, R.ViewYaw - 20.f + 25.f * T, 2100.f, -40.f);
			ApplyView(World);
			if (R.StepFrame == 1)
			{
				OrderSquad(*Squad, WalkGoal, R.Side);
				if (URelocationSubsystem* Relocation = World->GetSubsystem<URelocationSubsystem>(); Relocation && R.Barrel.IsValid() && Worker(*Squad))
				{
					const FVector Goal = SmokeUtils::ClearPoint(World, R.Barrel->GetActorLocation(), R.Barrel->GetActorLocation() + R.Forward * 500.f);
					Relocation->ExecuteRelocate(Worker(*Squad), R.Barrel.Get(), FVector(Goal.X, Goal.Y, R.Barrel->GetActorLocation().Z), 0.f);
				}
			}
			if (R.StepFrame == 75)
			{
				Detonate(World, GroundBelow(World, R.Home + R.Forward * 600.f - R.Side * 700.f));
			}
			if (R.StepFrame >= 140 && R.StepFrame < 170)
			{
				const FVector Spot = GroundBelow(World, R.Home + R.Forward * 650.f + R.Side * 600.f);
				const int32 Index = R.StepFrame - 140;
				const FVector Spread(((Index * 37) % 11 - 5) * 16., ((Index * 53) % 11 - 5) * 16., 0.);
				FireMiss(World, Leader->GetActorLocation() + FVector(0., 0., 150.), Spot + Spread);
			}
			Label(Tracks, FString::Printf(TEXT("10 s action: walk, grenade (3 s), a burst of misses (5 s), barrel push - %.1f s"), R.StepFrame / 30.f));
			if (R.StepFrame % MovieEvery == 0 && R.StepFrame <= MovieFrames)
			{
				Capture(FString::Printf(TEXT("movie/frame_%04d.png"), R.ClipCount++));
			}
			if (R.StepFrame > MovieFrames + 5)
			{
				R.Step = EStep::Done;
				R.StepFrame = 0;
			}
			break;
		}

		case EStep::Done:
			if (R.StepFrame >= 10)
			{
				if (Tracks)
				{
					const USnowInteractionSubsystem::FMetrics& M = Tracks->GetMetrics();
					R.Report.Add(FString::Printf(TEXT("movie metrics: stamps %lld, explosions %lld craters %lld, shots %lld traced %lld dents %lld, drag segments %lld"),
						M.StampsDrawn, M.Explosions, M.Craters, M.ShotsQueued, M.ShotTraces, M.Dents, M.DragSegments));
				}
				Finish();
			}
			break;
		}
	}

	void Run(const TArray<FString>& Args)
	{
		KRunner = MakeShared<FTracksRunner>();
		KRunner->bMovie = !Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("nomovie"), ESearchCase::IgnoreCase); });
		KRunner->Dir = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / TEXT("..") / TEXT("Environment") / TEXT("tracks"));
		FPaths::CollapseRelativeDirectories(KRunner->Dir);
		IFileManager::Get().DeleteDirectory(*(KRunner->Dir / TEXT("movie")), false, true);
		IFileManager::Get().MakeDirectory(*KRunner->Dir, true);
		KRunner->DrawHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateStatic(&OnDraw));
		KRunner->EndFrameHandle = FCoreDelegates::OnEndFrame.AddStatic(&Tick);
		UE_LOG(LogCodexTactics, Display, TEXT("EnvironmentShots tracks: stills%s -> %s"), KRunner->bMovie ? TEXT(" + movie") : TEXT(""), *KRunner->Dir);
	}
}

#endif // !UE_BUILD_SHIPPING
