// Dev-only console command: the RENDERED check of the interactive snow, phase 1 (USnowInteractionSubsystem; docs/systems/environment-weather.md
// section 14). Needs a real RHI (the subsystem is not created under -nullrhi): Scripts/smoke_parallel.ps1 runs it offscreen because
// Scripts/test_map.json gives it "rendered": true (-RenderOffscreen instead of -nullrhi). By hand:
//   UnrealEditor-Cmd.exe CodexTactics.uproject /Game/Maps/L_MovementTest -game -RenderOffscreen -ResX=1280 -ResY=720 -nosound
//     -UseFixedTimeStep -FPS=60 -ExecCmds="CodexTactics.SnowTracksSmoke"
// Checks:
//   1. the subsystem is active: RT_SnowInteraction is RG16F 2048^2, MPC_Environment.SnowTrackWindow = (origin, 1 / 6400, 1) and matches
//      the subsystem's window; the RT is empty around the squad;
//   2. the leader walks ~5 m (the rest of the squad frozen): RT read-back (FRenderTarget::ReadLinearColorPixels) shows R > 0 along the
//      path (every metre bin) and R == 0 three metres off it; the prints alternate (both sides of the path line);
//   3. the view moves 20 m (ATacticalCameraPawn::LookAtPoint): the window scrolls (shift pass) and the prints keep their WORLD position
//      (the same values read at the new UV);
//   4. the fade (Codex.Env.SnowTracksFadeScale 300 for 2 s) lowers R;
//   5. phase 3 event stamps through the real hooks: a grenade detonated 8 m beside the path (AGrenadeActor::Detonate ->
//      GameplayEvents::Explosion) leaves a crater (R > 0 at the centre, G > 0 on the rim and in the thrown-snow ring, nothing far out);
//      a missed shot (UCombatFeedbackSubsystem::SpawnTracer ending short of the floor) leaves a dent where its extended ray meets the
//      floor; a tracer ending on the leader (a hit) leaves none; a burst of 20 tracers in one frame is traced at most 8 a frame;
//   6. a real-time push (URelocationSubsystem::ExecuteRelocate of a spawned barrel): GetMovingObjects reports it and it leaves drag-trail
//      segments.
// The offscreen cursor sits on the viewport edge: the camera's edge scroll is switched off for the run (it panned the view off the walk).
// Stills: Saved/Screenshots/SnowTracks/path_rt.png (R of the path region, x4 contrast), walk.png (the scene), crater_rt.png (R red / G green
// of the crater region), events.png, drag.png.
// Mode "perf" (-ExecCmds="CodexTactics.SnowTracksSmoke perf"): MovementTest wave 2 (57 enemies) runs at the squad; A / B / A of
// Codex.Env.SnowTracks 1 / 0 / 1, 4 s each: GPU / render / game ms, the subsystem's own CPU ms per frame, stamps / s, walkers; log
// lines "[SnowTracksPerf]".
// Prints "[SnowTracksSmoke] PASS/FAIL ..." and "Smoke RESULT: PASS|FAIL", then exits.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/PlayerCameraManager.h"
#include "Camera/TacticalCameraPawn.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "CollisionQueryParams.h"
#include "Combat/CombatFeedbackSubsystem.h"
#include "Combat/GrenadeActor.h"
#include "Combat/HealthComponent.h"
#include "Core/PlayerCommands.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Environment/EnvironmentSubsystem.h"
#include "Environment/SnowInteractionRules.h"
#include "Environment/SnowInteractionSubsystem.h"
#include "Environment/WeatherPresenterActor.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "Interactables/BarrelActor.h"
#include "Interactables/RelocationSubsystem.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "RHIStats.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "UnrealClient.h"

namespace SnowTracksSmoke
{
	constexpr float StepSeconds = 0.05f;
	constexpr float ReadyTimeoutSeconds = 25.f;
	constexpr float WalkDistanceCm = 500.f;
	constexpr float WalkTimeoutSeconds = 14.f;
	constexpr float FocusMoveCm = 2000.f;
	constexpr float FadeScale = 300.f;
	constexpr float FadeSeconds = 2.f;
	/** Pixels within this distance of the path line count as "on the path" (prints are +-12 cm off it, 12 cm wide). */
	constexpr double CorridorCm = 35.;
	/** Phase 3: the grenade goes off this far beside the walk (blast radius 4 m: the squad stays clear). */
	constexpr float CraterOffsetCm = 800.f;
	/** The missed shot lands this far beside the walk, on the other side. */
	constexpr float DentOffsetCm = 600.f;
	constexpr int32 BurstTracers = 20;
	constexpr float EventsTimeoutSeconds = 8.f;
	constexpr float DragDistanceCm = 400.f;
	constexpr float DragTimeoutSeconds = 25.f;

	enum class EStage : uint8
	{
		WaitReady,
		Walk,
		Settle,
		MoveFocus,
		FocusSettle,
		Fade,
		EventsSetup,
		EventsSettle,
		Drag,
		Done
	};

	struct FPathSample
	{
		TArray<float> BinMax;
		float Max = 0.f;
		int32 LeftHits = 0;
		int32 RightHits = 0;
		bool bRead = false;
	};

	struct FState
	{
		EStage Stage = EStage::WaitReady;
		double RunStart = -1.;
		double StageStart = 0.;
		double StageTime = 0.;
		double TotalTime = 0.;
		int32 Failures = 0;
		FVector Start = FVector::ZeroVector;
		FVector Goal = FVector::ZeroVector;
		FVector WalkEnd = FVector::ZeroVector;
		FVector2D WindowOriginBefore = FVector2D::ZeroVector;
		FPathSample Walked;
		float FadeBefore = 0.f;
		double SnapTime = -1.;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Frozen;
		// Phase 3.
		FVector CraterPoint = FVector::ZeroVector;
		FVector DentPoint = FVector::ZeroVector;
		FVector BurstPoint = FVector::ZeroVector;
		FVector BarrelStart = FVector::ZeroVector;
		FVector BarrelGoal = FVector::ZeroVector;
		TWeakObjectPtr<ABarrelActor> Barrel;
		bool bDragStarted = false;
		int32 MaxMoving = 0;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[SnowTracksSmoke] %s: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	void SetCVar(const TCHAR* Name, const TCHAR* Value)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Variable->Set(Value, ECVF_SetByConsole);
		}
	}

	void Finish(FState& State)
	{
		SetCVar(TEXT("Codex.Env.SnowTracksFadeScale"), TEXT("1"));
		for (const TWeakObjectPtr<AOperativeCharacter>& Each : State.Frozen)
		{
			if (Each.IsValid())
			{
				Each->CustomTimeDilation = 1.f;
			}
		}
		const bool bPass = State.Failures == 0;
		UE_LOG(LogCodexTactics, Display, TEXT("[SnowTracksSmoke] RESULT %s (%d failed checks)"), bPass ? TEXT("PASS") : TEXT("FAIL"), State.Failures);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), bPass ? TEXT("PASS") : TEXT("FAIL"));
		State.Stage = EStage::Done;
		FPlatformMisc::RequestExit(false, TEXT("SnowTracksSmoke"));
	}

	void NextStage(FState& State, EStage Stage)
	{
		State.Stage = Stage;
		State.StageStart += State.StageTime;
		State.StageTime = 0.;
	}

	FString ShotDir()
	{
		return FPaths::ProjectSavedDir() / TEXT("Screenshots/SnowTracks");
	}

	/** Pixel rect of the RT covering the world box [Min, Max] (clamped to the RT). */
	FIntRect PixelRect(const FSnowTrackWindow& Window, const FVector2D& Min, const FVector2D& Max)
	{
		const FVector2D A = SnowInteractionRules::WorldToUV(Min, Window.Origin, Window.SizeCm) * Window.Resolution;
		const FVector2D B = SnowInteractionRules::WorldToUV(Max, Window.Origin, Window.SizeCm) * Window.Resolution;
		const int32 X0 = FMath::Clamp(FMath::FloorToInt32(FMath::Min(A.X, B.X)), 0, Window.Resolution - 1);
		const int32 Y0 = FMath::Clamp(FMath::FloorToInt32(FMath::Min(A.Y, B.Y)), 0, Window.Resolution - 1);
		const int32 X1 = FMath::Clamp(FMath::CeilToInt32(FMath::Max(A.X, B.X)), X0 + 1, Window.Resolution);
		const int32 Y1 = FMath::Clamp(FMath::CeilToInt32(FMath::Max(A.Y, B.Y)), Y0 + 1, Window.Resolution);
		return FIntRect(X0, Y0, X1, Y1);
	}

	/** Raw RT values (no normalisation) of Rect; flushes the rendering commands first (every queued stamp / pass is in). */
	bool ReadRect(UTextureRenderTarget2D* Target, const FIntRect& Rect, TArray<FLinearColor>& Out)
	{
		FTextureRenderTargetResource* Resource = Target ? Target->GameThread_GetRenderTargetResource() : nullptr;
		if (!Resource)
		{
			return false;
		}
		FlushRenderingCommands();
		FReadSurfaceDataFlags Flags(RCM_MinMax);
		Flags.SetLinearToGamma(false);
		return Resource->ReadLinearColorPixels(Out, Flags, Rect) && Out.Num() == Rect.Area();
	}

	/** World XY of the centre of RT pixel (X, Y). */
	FVector2D PixelWorld(const FSnowTrackWindow& Window, int32 X, int32 Y)
	{
		return SnowInteractionRules::UVToWorld(FVector2D((X + 0.5) / Window.Resolution, (Y + 0.5) / Window.Resolution), Window.Origin, Window.SizeCm);
	}

	/** Max R per metre along A -> B within the corridor, the overall max, and which side of the line the prints are. */
	FPathSample SamplePath(UTextureRenderTarget2D* Target, const FSnowTrackWindow& Window, const FVector2D& A, const FVector2D& B,
		const FString& PngPath = FString())
	{
		FPathSample Sample;
		const double Length = FVector2D::Distance(A, B);
		const int32 Bins = FMath::Max(1, FMath::FloorToInt32(Length / 100.));
		Sample.BinMax.Init(0.f, Bins);
		const FVector2D Margin(CorridorCm + 10., CorridorCm + 10.);
		const FIntRect Rect = PixelRect(Window, FVector2D(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y)) - Margin,
			FVector2D(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y)) + Margin);
		TArray<FLinearColor> Pixels;
		if (!ReadRect(Target, Rect, Pixels))
		{
			return Sample;
		}
		Sample.bRead = true;
		const FVector2D Dir = (B - A) / FMath::Max(Length, 1.);
		const FVector2D Right(-Dir.Y, Dir.X);
		TArray<FColor> Png;
		Png.SetNumZeroed(Pixels.Num());
		for (int32 Y = 0; Y < Rect.Height(); ++Y)
		{
			for (int32 X = 0; X < Rect.Width(); ++X)
			{
				const int32 Index = Y * Rect.Width() + X;
				const float R = Pixels[Index].R;
				const uint8 Grey = static_cast<uint8>(FMath::Clamp(R * 4.f, 0.f, 1.f) * 255.f);
				const uint8 Berm = static_cast<uint8>(FMath::Clamp(Pixels[Index].G * 4.f, 0.f, 1.f) * 255.f);
				Png[Index] = FColor(Grey, FMath::Max(Grey, Berm), Grey, 255);
				const FVector2D World = PixelWorld(Window, Rect.Min.X + X, Rect.Min.Y + Y);
				const FVector2D Rel = World - A;
				const double Along = FVector2D::DotProduct(Rel, Dir);
				const double Across = FVector2D::DotProduct(Rel, Right);
				if (Along < 0. || Along > Length || FMath::Abs(Across) > CorridorCm)
				{
					continue;
				}
				Sample.Max = FMath::Max(Sample.Max, R);
				const int32 Bin = FMath::Clamp(FMath::FloorToInt32(Along / 100.), 0, Bins - 1);
				Sample.BinMax[Bin] = FMath::Max(Sample.BinMax[Bin], R);
				if (R > 0.2f)
				{
					(Across < 0. ? Sample.LeftHits : Sample.RightHits) += 1;
				}
			}
		}
		if (!PngPath.IsEmpty())
		{
			TArray64<uint8> Compressed;
			FImageUtils::PNGCompressImageArray(Rect.Width(), Rect.Height(), TArrayView64<const FColor>(Png.GetData(), Png.Num()), Compressed);
			FFileHelper::SaveArrayToFile(Compressed, *PngPath);
		}
		return Sample;
	}

	/** Max R in a world box. -1 when it could not be read. */
	float MaxInBox(UTextureRenderTarget2D* Target, const FSnowTrackWindow& Window, const FVector2D& Centre, double HalfCm)
	{
		const FIntRect Rect = PixelRect(Window, Centre - FVector2D(HalfCm, HalfCm), Centre + FVector2D(HalfCm, HalfCm));
		TArray<FLinearColor> Pixels;
		if (!ReadRect(Target, Rect, Pixels))
		{
			return -1.f;
		}
		float Max = 0.f;
		for (const FLinearColor& Pixel : Pixels)
		{
			Max = FMath::Max(Max, Pixel.R);
		}
		return Max;
	}

	bool ReadWindowParam(UWorld& World, FLinearColor& Out)
	{
		const UMaterialParameterCollection* Collection = LoadObject<UMaterialParameterCollection>(nullptr,
			TEXT("/Game/Environment/Weather/MPC/MPC_Environment.MPC_Environment"));
		const UMaterialParameterCollectionInstance* Instance = Collection ? World.GetParameterCollectionInstance(Collection) : nullptr;
		return Instance && Instance->GetVectorParameterValue(TEXT("SnowTrackWindow"), Out);
	}

	ATacticalCameraPawn* FindCamera(UWorld& World)
	{
		APlayerController* PC = World.GetFirstPlayerController();
		return PC ? Cast<ATacticalCameraPawn>(PC->GetPawn()) : nullptr;
	}

	/** The floor under Point (static geometry only), Point itself when there is none. */
	FVector GroundBelow(UWorld& World, const FVector& Point)
	{
		FHitResult Hit;
		const FCollisionQueryParams Params(SCENE_QUERY_STAT(SnowTracksSmokeGround), false);
		if (World.LineTraceSingleByObjectType(Hit, Point + FVector(0., 0., 300.), Point - FVector(0., 0., 600.),
			FCollisionObjectQueryParams(ECC_WorldStatic), Params))
		{
			return Hit.ImpactPoint;
		}
		return Point;
	}

	/** The crater read-back, by distance from the centre in crater diameters. */
	struct FCraterSample
	{
		float CentreR = 0.f;
		float RimG = 0.f;
		float RimR = 0.f;
		float SprayG = 0.f;
		float FarMax = 0.f;
		bool bRead = false;
	};

	FCraterSample SampleCrater(UTextureRenderTarget2D* Target, const FSnowTrackWindow& Window, const FVector2D& Centre, double Diameter,
		const FString& PngPath)
	{
		FCraterSample Sample;
		const double Half = Diameter * 1.6;
		const FIntRect Rect = PixelRect(Window, Centre - FVector2D(Half, Half), Centre + FVector2D(Half, Half));
		TArray<FLinearColor> Pixels;
		if (!ReadRect(Target, Rect, Pixels))
		{
			return Sample;
		}
		Sample.bRead = true;
		TArray<FColor> Png;
		Png.SetNumZeroed(Pixels.Num());
		for (int32 Y = 0; Y < Rect.Height(); ++Y)
		{
			for (int32 X = 0; X < Rect.Width(); ++X)
			{
				const int32 Index = Y * Rect.Width() + X;
				const FLinearColor& P = Pixels[Index];
				Png[Index] = FColor(static_cast<uint8>(FMath::Clamp(P.R * 0.5f, 0.f, 1.f) * 255.f), static_cast<uint8>(FMath::Clamp(P.G, 0.f, 1.f) * 255.f), 0, 255);
				const double D = FVector2D::Distance(PixelWorld(Window, Rect.Min.X + X, Rect.Min.Y + Y), Centre) / Diameter;
				if (D < 0.15)
				{
					Sample.CentreR = FMath::Max(Sample.CentreR, P.R);
				}
				else if (D >= 0.5 && D <= 0.75)
				{
					Sample.RimG = FMath::Max(Sample.RimG, P.G);
					Sample.RimR = FMath::Max(Sample.RimR, P.R);
				}
				else if (D >= 0.9 && D <= 1.2)
				{
					Sample.SprayG = FMath::Max(Sample.SprayG, P.G);
				}
				else if (D > 1.5)
				{
					Sample.FarMax = FMath::Max(Sample.FarMax, FMath::Max(P.R, P.G));
				}
			}
		}
		if (!PngPath.IsEmpty())
		{
			TArray64<uint8> Compressed;
			FImageUtils::PNGCompressImageArray(Rect.Width(), Rect.Height(), TArrayView64<const FColor>(Png.GetData(), Png.Num()), Compressed);
			FFileHelper::SaveArrayToFile(Compressed, *PngPath);
		}
		return Sample;
	}

	/** A tracer that misses: from 9 m back and 2.8 m up to 0.8 m above the floor short of Ground - its extended ray meets the floor at Ground. */
	void FireMiss(UCombatFeedbackSubsystem& Feedback, const FVector& Ground, const FVector& Dir2D)
	{
		const FVector Start = Ground - Dir2D * 900. + FVector(0., 0., 280.);
		const FVector Slope = (Ground - Start).GetSafeNormal();
		const FVector End = Ground - Slope * (80. / FMath::Max(-Slope.Z, 0.05));
		Feedback.SpawnTracer(Start, End, UCombatFeedbackSubsystem::DefaultTracerColor(), EDamageType::Kinetic);
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		UWorld* World = WeakWorld.Get();
		if (!World || State.Stage == EStage::Done)
		{
			return false;
		}
		const double Now = World->GetTimeSeconds();
		if (State.RunStart < 0.)
		{
			State.RunStart = Now;
			State.StageStart = Now;
		}
		State.StageTime = Now - State.StageStart;
		State.TotalTime = Now - State.RunStart;

		USnowInteractionSubsystem* Snow = USnowInteractionSubsystem::Get(World);
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Snow)
		{
			Check(State, false, TEXT("USnowInteractionSubsystem exists (needs a real RHI: run it rendered, e.g. smoke_parallel with the test_map \"rendered\" option)"));
			Finish(State);
			return false;
		}

		// Trace once a second: where the window is and what it follows.
		if (FMath::FloorToInt32(State.TotalTime) != FMath::FloorToInt32(State.TotalTime - StepSeconds))
		{
			const FSnowTrackWindow& W = Snow->GetWindow();
			const ATacticalCameraPawn* Camera = FindCamera(*World);
			const APlayerController* PC = World->GetFirstPlayerController();
			UE_LOG(LogCodexTactics, Display, TEXT("[SnowTracksSmoke] t %.1f stage %d: active %d window valid %d origin (%.0f, %.0f) focus (%.0f, %.0f) view centre %s view target %s leader %s"),
				State.TotalTime, static_cast<int32>(State.Stage), Snow->IsActive(), W.bValid, W.Origin.X, W.Origin.Y, Snow->GetLastFocus().X, Snow->GetLastFocus().Y,
				Camera ? *Camera->GetViewCentre().ToCompactString() : TEXT("-"),
				PC && PC->PlayerCameraManager ? *GetNameSafe(PC->PlayerCameraManager->GetViewTarget()) : TEXT("-"),
				Leader ? *Leader->GetActorLocation().ToCompactString() : TEXT("-"));
		}

		switch (State.Stage)
		{
		case EStage::WaitReady:
		{
			ATacticalCameraPawn* ReadyCamera = FindCamera(*World);
			const APlayerController* ReadyPC = World->GetFirstPlayerController();
			const bool bTacticalView = ReadyCamera && ReadyPC && ReadyPC->PlayerCameraManager && ReadyPC->PlayerCameraManager->GetViewTarget() == ReadyCamera;
			const bool bReady = Leader && bTacticalView && Snow->IsActive() && Snow->GetWindow().bValid && State.StageTime > 1.;
			if (bReady && State.SnapTime < 0.)
			{
				// The view may still glide in from the level start (or a start camera): put it on the leader so the window covers the walk.
				// The offscreen cursor rests on the viewport edge: no edge scroll (it panned the view - and the window - off the walk).
				ReadyCamera->Config.EdgeScrollMargin = -1.f;
				ReadyCamera->SnapFocusOnTarget(Leader);
				State.SnapTime = Now;
				return true;
			}
			if (bReady && Now - State.SnapTime < 0.5)
			{
				return true;
			}
			if (!bReady)
			{
				if (State.StageTime > ReadyTimeoutSeconds)
				{
					const AWeatherPresenterActor* Presenter = AWeatherPresenterActor::Find(World);
					Check(State, false, FString::Printf(TEXT("subsystem active within %.0f s (assets %d, presenter applying %d, leader %d)"),
						ReadyTimeoutSeconds, Snow->HasAssets(), Presenter && Presenter->IsApplying(), Leader != nullptr));
					Finish(State);
					return false;
				}
				return true;
			}
			UTextureRenderTarget2D* Target = Snow->GetRenderTarget();
			const FSnowTrackWindow& Window = Snow->GetWindow();
			Check(State, Target && Target->GetFormat() == PF_G16R16F, FString::Printf(TEXT("RT_SnowInteraction is RG16F (format %s)"),
				Target ? GetPixelFormatString(Target->GetFormat()) : TEXT("none")));
			Check(State, Target && Target->SizeX == 2048 && Window.Resolution == 2048 && FMath::IsNearlyEqual(Window.SizeCm, 6400.),
				FString::Printf(TEXT("2048^2 over a 64 m window (RT %d, window %d / %.0f cm)"), Target ? Target->SizeX : 0, Window.Resolution, Window.SizeCm));
			FLinearColor Param(ForceInit);
			const bool bParam = ReadWindowParam(*World, Param);
			Check(State, bParam && Param.A == 1.f && FMath::IsNearlyEqual(Param.B, 1.f / 6400.f, 1e-9f)
				&& FMath::IsNearlyEqual(Param.R, static_cast<float>(Window.Origin.X), 0.5f) && FMath::IsNearlyEqual(Param.G, static_cast<float>(Window.Origin.Y), 0.5f),
				FString::Printf(TEXT("MPC SnowTrackWindow = (%.0f, %.0f, 1/%.0f, %.0f), window origin (%.0f, %.0f)"), Param.R, Param.G,
					Param.B > 0.f ? 1.f / Param.B : 0.f, Param.A, Window.Origin.X, Window.Origin.Y));
			Check(State, !Snow->IsStampSetFromAsset() || Snow->GetStamps().OperativeBoot.SpacingCm > 0.f,
				FString::Printf(TEXT("stamp set (%s)"), Snow->IsStampSetFromAsset() ? TEXT("DA_SnowStamps") : TEXT("C++ defaults")));

			// The walk: the others stand frozen (no prints of theirs near the probes); 5 m along a clear direction.
			for (AOperativeCharacter* Each : Squad->GetMembers())
			{
				if (Each != Leader)
				{
					Each->CustomTimeDilation = 0.f;
					State.Frozen.Add(Each);
				}
			}
			State.Start = Leader->GetActorLocation();
			const FVector Forward = Leader->GetActorForwardVector().GetSafeNormal2D();
			State.Goal = SmokeUtils::ClearPoint(World, State.Start, State.Start + (Forward.IsNearlyZero() ? FVector::ForwardVector : Forward) * WalkDistanceCm);
			const FVector2D Start2D(State.Start.X, State.Start.Y);
			const FVector2D Goal2D(State.Goal.X, State.Goal.Y);
			// The squad's first steps into formation printed too: start from clean snow so the off-path probes mean something.
			Snow->ClearTracks();
			const FPathSample Before = SamplePath(Target, Window, Start2D, Goal2D);
			Check(State, Before.bRead && Before.Max == 0.f, FString::Printf(TEXT("the snow on the path is untouched before the walk (max R %.3f)"), Before.Max));
			Leader->OrderMoveTo(State.Goal, /*bSprint*/ false);
			Snow->ResetMetrics();
			UE_LOG(LogCodexTactics, Display, TEXT("[SnowTracksSmoke] walk %s -> %s (%.0f cm)"), *State.Start.ToCompactString(), *State.Goal.ToCompactString(),
				FVector::Dist2D(State.Start, State.Goal));
			NextStage(State, EStage::Walk);
			return true;
		}

		case EStage::Walk:
			if (!Leader)
			{
				Check(State, false, TEXT("the leader exists during the walk"));
				Finish(State);
				return false;
			}
			if (FVector::Dist2D(Leader->GetActorLocation(), State.Goal) < 60.f || State.StageTime > WalkTimeoutSeconds)
			{
				State.WalkEnd = Leader->GetActorLocation();
				NextStage(State, EStage::Settle);
			}
			return true;

		case EStage::Settle:
		{
			if (State.StageTime < 0.6)
			{
				return true; // a poll + a flush after the stop
			}
			UTextureRenderTarget2D* Target = Snow->GetRenderTarget();
			const FSnowTrackWindow& Window = Snow->GetWindow();
			const FVector2D A(State.Start.X, State.Start.Y);
			const FVector2D B(State.WalkEnd.X, State.WalkEnd.Y);
			const double Walked = FVector2D::Distance(A, B);
			Check(State, Walked > 350., FString::Printf(TEXT("the leader walked %.0f cm"), Walked));
			IFileManager::Get().MakeDirectory(*ShotDir(), true);
			State.Walked = SamplePath(Target, Window, A, B, ShotDir() / TEXT("path_rt.png"));
			FScreenshotRequest::RequestScreenshot(ShotDir() / TEXT("walk.png"), false, false);
			int32 BinsHit = 0;
			FString Bins;
			for (const float Max : State.Walked.BinMax)
			{
				BinsHit += Max > 0.05f ? 1 : 0;
				Bins += FString::Printf(TEXT(" %.2f"), Max);
			}
			const USnowInteractionSubsystem::FMetrics& Metrics = Snow->GetMetrics();
			Check(State, State.Walked.bRead && State.Walked.Max > 0.3f, FString::Printf(TEXT("prints on the path: max R %.3f (stamps drawn %lld)"),
				State.Walked.Max, Metrics.StampsDrawn));
			Check(State, BinsHit == State.Walked.BinMax.Num(), FString::Printf(TEXT("every metre of the path has a print (%d / %d, max R per metre:%s)"),
				BinsHit, State.Walked.BinMax.Num(), *Bins));
			Check(State, State.Walked.LeftHits > 0 && State.Walked.RightHits > 0, FString::Printf(TEXT("prints alternate left / right (%d / %d texels)"),
				State.Walked.LeftHits, State.Walked.RightHits));
			const FVector2D Dir = (B - A).GetSafeNormal();
			const FVector2D Off = (A + B) * 0.5 + FVector2D(-Dir.Y, Dir.X) * 300.;
			const float OffMax = MaxInBox(Target, Window, Off, 50.);
			Check(State, OffMax == 0.f, FString::Printf(TEXT("R == 0 three metres off the path (max %.4f)"), OffMax));
			const float Behind = MaxInBox(Target, Window, A - Dir * 300., 50.);
			Check(State, Behind == 0.f, FString::Printf(TEXT("R == 0 three metres behind the start (max %.4f)"), Behind));

			// Move the view sideways (the path stays in the window).
			State.WindowOriginBefore = Window.Origin;
			ATacticalCameraPawn* Camera = FindCamera(*World);
			const APlayerController* PC = World->GetFirstPlayerController();
			const float Yaw = PC && PC->PlayerCameraManager ? static_cast<float>(PC->PlayerCameraManager->GetCameraRotation().Yaw) : 0.f;
			const FVector Side = FRotator(0.f, Yaw + 90.f, 0.f).Vector();
			if (Camera)
			{
				Camera->LookAtPoint(Camera->GetViewCentre() + Side * FocusMoveCm);
			}
			Check(State, Camera != nullptr, TEXT("tactical camera found"));
			NextStage(State, EStage::MoveFocus);
			return true;
		}

		case EStage::MoveFocus:
		{
			const bool bMoved = !Snow->GetWindow().Origin.Equals(State.WindowOriginBefore, 1.);
			if (bMoved && State.StageTime > 3.)
			{
				NextStage(State, EStage::FocusSettle);
			}
			else if (State.StageTime > 8.)
			{
				Check(State, false, TEXT("the window scrolled after a 20 m view move"));
				NextStage(State, EStage::FocusSettle);
			}
			return true;
		}

		case EStage::FocusSettle:
		{
			UTextureRenderTarget2D* Target = Snow->GetRenderTarget();
			const FSnowTrackWindow& Window = Snow->GetWindow();
			const FVector2D Shift = Window.Origin - State.WindowOriginBefore;
			Check(State, Snow->GetMetrics().ShiftPasses > 0, FString::Printf(TEXT("shift passes ran (%lld), window moved (%.0f, %.0f) cm"),
				Snow->GetMetrics().ShiftPasses, Shift.X, Shift.Y));
			FLinearColor Param(ForceInit);
			const bool bParam = ReadWindowParam(*World, Param);
			Check(State, bParam && FMath::IsNearlyEqual(Param.R, static_cast<float>(Window.Origin.X), 0.5f)
				&& FMath::IsNearlyEqual(Param.G, static_cast<float>(Window.Origin.Y), 0.5f),
				FString::Printf(TEXT("the MPC window follows (%.0f, %.0f)"), Param.R, Param.G));
			const FVector2D A(State.Start.X, State.Start.Y);
			const FVector2D B(State.WalkEnd.X, State.WalkEnd.Y);
			const FPathSample After = SamplePath(Target, Window, A, B, ShotDir() / TEXT("path_rt_after_scroll.png"));
			bool bSame = After.bRead && After.BinMax.Num() == State.Walked.BinMax.Num();
			FString Pairs;
			for (int32 Bin = 0; bSame && Bin < After.BinMax.Num(); ++Bin)
			{
				const float Was = State.Walked.BinMax[Bin];
				const float Is = After.BinMax[Bin];
				Pairs += FString::Printf(TEXT(" %.2f->%.2f"), Was, Is);
				bSame &= FMath::Abs(Is - Was) <= FMath::Max(0.02f, Was * 0.05f);
			}
			Check(State, bSame, FString::Printf(TEXT("the prints keep their world position after the scroll (per metre:%s)"), *Pairs));
			State.FadeBefore = After.Max;
			SetCVar(TEXT("Codex.Env.SnowTracksFadeScale"), *FString::SanitizeFloat(FadeScale));
			NextStage(State, EStage::Fade);
			return true;
		}

		case EStage::Fade:
		{
			if (State.StageTime < FadeSeconds)
			{
				return true;
			}
			SetCVar(TEXT("Codex.Env.SnowTracksFadeScale"), TEXT("1"));
			const FVector2D A(State.Start.X, State.Start.Y);
			const FVector2D B(State.WalkEnd.X, State.WalkEnd.Y);
			const FPathSample Faded = SamplePath(Snow->GetRenderTarget(), Snow->GetWindow(), A, B);
			const float Ratio = State.FadeBefore > 0.f ? Faded.Max / State.FadeBefore : 1.f;
			Check(State, Faded.bRead && Ratio < 0.8f && Faded.Max > 0.f, FString::Printf(TEXT("the fade lowers R: %.3f -> %.3f (x%.2f in %.1f s at fade scale %.0f; %lld fade passes)"),
				State.FadeBefore, Faded.Max, Ratio, FadeSeconds, FadeScale, Snow->GetMetrics().FadePasses));
			const USnowInteractionSubsystem::FMetrics& M = Snow->GetMetrics();
			UE_LOG(LogCodexTactics, Display, TEXT("[SnowTracksSmoke] metrics: frames %lld, polls %lld, stamps %lld (dropped %lld, culled %lld), draws %lld, fade %lld, shift %lld, clamp %lld, CPU %.4f ms / frame"),
				M.Frames, M.Polls, M.StampsDrawn, M.StampsDropped, M.StampsCulled, M.DrawCalls, M.FadePasses, M.ShiftPasses, M.ClampPasses,
				M.Frames > 0 ? M.CpuSeconds * 1000. / M.Frames : 0.);
			// Phase 3: back to the walk; the crater on one side of it, the shots on the other.
			const FVector2D Walk = (B - A).GetSafeNormal();
			const FVector Forward(Walk.X, Walk.Y, 0.);
			const FVector Side(-Walk.Y, Walk.X, 0.);
			const FVector Mid = (State.Start + State.WalkEnd) * 0.5;
			State.CraterPoint = GroundBelow(*World, Mid + Side * CraterOffsetCm);
			State.DentPoint = GroundBelow(*World, Mid - Side * DentOffsetCm);
			State.BurstPoint = GroundBelow(*World, Mid - Side * DentOffsetCm + Forward * 500.);
			if (ATacticalCameraPawn* Camera = FindCamera(*World))
			{
				Camera->LookAtPoint(Mid);
			}
			NextStage(State, EStage::EventsSetup);
			return true;
		}

		case EStage::EventsSetup:
		{
			const FSnowTrackWindow& Window = Snow->GetWindow();
			const auto Inside = [&Window](const FVector& P) { return SnowInteractionRules::IsInsideWindow(FVector2D(P.X, P.Y), Window.Origin, Window.SizeCm, 600.); };
			const bool bInside = Inside(State.CraterPoint) && Inside(State.DentPoint) && Inside(State.BurstPoint);
			if (!bInside || State.StageTime < 1.)
			{
				if (State.StageTime > EventsTimeoutSeconds)
				{
					Check(State, false, TEXT("the event points are inside the window"));
					Finish(State);
					return false;
				}
				return true;
			}
			UCombatFeedbackSubsystem* Feedback = World->GetSubsystem<UCombatFeedbackSubsystem>();
			Check(State, Feedback != nullptr, TEXT("UCombatFeedbackSubsystem exists"));
			Snow->ClearTracks();
			Snow->ResetMetrics();
			// The grenade (the real Detonate: its blast gameplay + GameplayEvents::Explosion).
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AGrenadeActor* Grenade = World->SpawnActor<AGrenadeActor>(State.CraterPoint + FVector(0., 0., 15.), FRotator::ZeroRotator, Params);
			Check(State, Grenade != nullptr, TEXT("grenade spawned"));
			if (Grenade)
			{
				Grenade->Detonate();
			}
			Check(State, Snow->GetMetrics().Explosions == 1 && Snow->GetMetrics().Craters == 1, FString::Printf(TEXT("the explosion event reached the subsystem (%lld events, %lld craters)"),
				Snow->GetMetrics().Explosions, Snow->GetMetrics().Craters));
			if (Feedback && Leader)
			{
				const FVector2D Walk = FVector2D(State.WalkEnd.X - State.Start.X, State.WalkEnd.Y - State.Start.Y).GetSafeNormal();
				const FVector Dir2D(Walk.X, Walk.Y, 0.);
				FireMiss(*Feedback, State.DentPoint, Dir2D);
				// A hit: the tracer ends on the leader - no dent.
				Feedback->SpawnTracer(Leader->GetActorLocation() - Dir2D * 900. + FVector(0., 0., 150.), Leader->GetActorLocation(),
					UCombatFeedbackSubsystem::DefaultTracerColor(), EDamageType::Kinetic);
				// A burst in ONE frame: traced at most 8 a frame.
				for (int32 Index = 0; Index < BurstTracers; ++Index)
				{
					const FVector Offset = FVector(((Index * 37) % 9 - 4) * 25., ((Index * 53) % 9 - 4) * 25., 0.);
					FireMiss(*Feedback, State.BurstPoint + Offset, Dir2D);
				}
				Check(State, Snow->GetMetrics().ShotsQueued == 2 + BurstTracers, FString::Printf(TEXT("tracers queued through OnTracerSpawned (%lld)"),
					Snow->GetMetrics().ShotsQueued));
			}
			UE_LOG(LogCodexTactics, Display, TEXT("[SnowTracksSmoke] events: crater at %s, dent at %s, burst at %s"), *State.CraterPoint.ToCompactString(),
				*State.DentPoint.ToCompactString(), *State.BurstPoint.ToCompactString());
			NextStage(State, EStage::EventsSettle);
			return true;
		}

		case EStage::EventsSettle:
		{
			if (State.StageTime < 0.6)
			{
				return true; // the shot queue drains (8 a frame) and the stamps flush
			}
			UTextureRenderTarget2D* Target = Snow->GetRenderTarget();
			const FSnowTrackWindow& Window = Snow->GetWindow();
			const USnowInteractionSubsystem::FMetrics& M = Snow->GetMetrics();
			const float Diameter = SnowInteractionRules::ComputeCrater(GameplayEvents::ExplosionKinds::Grenade, GetDefault<AGrenadeActor>()->EffectRadius,
				Snow->GetStamps(), Snow->GetTuning()).DiameterCm;
			const FCraterSample Crater = SampleCrater(Target, Window, FVector2D(State.CraterPoint.X, State.CraterPoint.Y), Diameter, ShotDir() / TEXT("crater_rt.png"));
			Check(State, Crater.bRead && Crater.CentreR > 0.5f, FString::Printf(TEXT("crater: R %.2f at the centre (%.0f cm crater)"), Crater.CentreR, Diameter));
			Check(State, Crater.RimG > 0.2f, FString::Printf(TEXT("crater: G %.2f on the rim (R there %.2f)"), Crater.RimG, Crater.RimR));
			Check(State, Crater.SprayG > 0.1f, FString::Printf(TEXT("crater: G %.2f in the thrown-snow ring"), Crater.SprayG));
			Check(State, Crater.FarMax == 0.f, FString::Printf(TEXT("crater: nothing beyond 1.5 diameters (max %.4f)"), Crater.FarMax));
			const float DentR = MaxInBox(Target, Window, FVector2D(State.DentPoint.X, State.DentPoint.Y), 40.);
			Check(State, DentR > 0.15f, FString::Printf(TEXT("dent: R %.2f where the missed shot's ray meets the floor"), DentR));
			const float BurstR = MaxInBox(Target, Window, FVector2D(State.BurstPoint.X, State.BurstPoint.Y), 160.);
			Check(State, BurstR > 0.15f, FString::Printf(TEXT("burst: dents R %.2f"), BurstR));
			Check(State, M.ShotsOnTarget >= 1, FString::Printf(TEXT("the tracer ending on the leader counted as a hit (%lld)"), M.ShotsOnTarget));
			Check(State, M.MaxShotTracesInFrame > 0 && M.MaxShotTracesInFrame <= 8, FString::Printf(TEXT("at most 8 dent traces a frame (max %d, %lld traces)"),
				M.MaxShotTracesInFrame, M.ShotTraces));
			Check(State, M.Dents >= 1 + BurstTracers / 2 && M.Dents <= 1 + BurstTracers, FString::Printf(TEXT("dents %lld of %d misses"), M.Dents, 1 + BurstTracers));
			Check(State, Snow->GetPendingShotCount() == 0, TEXT("the shot queue drained"));
			FScreenshotRequest::RequestScreenshot(ShotDir() / TEXT("events.png"), false, false);
			UE_LOG(LogCodexTactics, Display, TEXT("[SnowTracksSmoke] events metrics: explosions %lld craters %lld, shots %lld (dropped %lld, on target %lld, traced %lld, max %d / frame), dents %lld"),
				M.Explosions, M.Craters, M.ShotsQueued, M.ShotsDropped, M.ShotsOnTarget, M.ShotTraces, M.MaxShotTracesInFrame, M.Dents);

			// A real-time push: the leader walks to a spawned barrel and pushes it 4 m.
			URelocationSubsystem* Relocation = World->GetSubsystem<URelocationSubsystem>();
			const FVector2D Walk = FVector2D(State.WalkEnd.X - State.Start.X, State.WalkEnd.Y - State.Start.Y).GetSafeNormal();
			const FVector Forward(Walk.X, Walk.Y, 0.);
			const FVector Side(-Walk.Y, Walk.X, 0.);
			State.BarrelStart = SmokeUtils::ClearPoint(World, State.WalkEnd, State.WalkEnd - Side * 300.);
			State.BarrelGoal = SmokeUtils::ClearPoint(World, State.BarrelStart, State.BarrelStart - Forward * DragDistanceCm);
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
			State.Barrel = World->SpawnActor<ABarrelActor>(GroundBelow(*World, State.BarrelStart) + FVector(0., 0., 5.), FRotator::ZeroRotator, Params);
			Check(State, State.Barrel.IsValid() && Relocation != nullptr && Leader != nullptr, TEXT("barrel spawned for the push"));
			if (State.Barrel.IsValid() && Relocation && Leader)
			{
				State.BarrelStart = State.Barrel->GetActorLocation();
				Snow->ResetMetrics();
				Relocation->ExecuteRelocate(Leader, State.Barrel.Get(), FVector(State.BarrelGoal.X, State.BarrelGoal.Y, State.BarrelStart.Z), 0.f);
			}
			NextStage(State, EStage::Drag);
			return true;
		}

		case EStage::Drag:
		{
			const URelocationSubsystem* Relocation = World->GetSubsystem<URelocationSubsystem>();
			const int32 Tasks = Relocation ? Relocation->GetActiveTaskCount() : 0;
			State.bDragStarted |= Tasks > 0;
			State.MaxMoving = FMath::Max(State.MaxMoving, Snow->GetMetrics().LastMovingObjects);
			if (ATacticalCameraPawn* Camera = FindCamera(*World))
			{
				Camera->LookAtPoint((State.BarrelStart + State.BarrelGoal) * 0.5);
			}
			const bool bDone = State.bDragStarted && Tasks == 0;
			if (!bDone && State.StageTime < DragTimeoutSeconds)
			{
				return true;
			}
			const FVector BarrelEnd = State.Barrel.IsValid() ? State.Barrel->GetActorLocation() : State.BarrelStart;
			const double Pushed = FVector::Dist2D(State.BarrelStart, BarrelEnd);
			const USnowInteractionSubsystem::FMetrics& M = Snow->GetMetrics();
			Check(State, bDone, FString::Printf(TEXT("the push finished within %.0f s (started %d)"), DragTimeoutSeconds, State.bDragStarted));
			Check(State, Pushed > 200., FString::Printf(TEXT("the barrel moved %.0f cm"), Pushed));
			Check(State, State.MaxMoving >= 1, FString::Printf(TEXT("GetMovingObjects reported the pushed barrel (max %d)"), State.MaxMoving));
			Check(State, M.DragSegments > 0, FString::Printf(TEXT("the push left drag-trail segments (%lld)"), M.DragSegments));
			const FPathSample Trail = SamplePath(Snow->GetRenderTarget(), Snow->GetWindow(), FVector2D(State.BarrelStart.X, State.BarrelStart.Y),
				FVector2D(BarrelEnd.X, BarrelEnd.Y));
			Check(State, Trail.bRead && Trail.Max > 0.3f, FString::Printf(TEXT("drag trail R %.2f along the push"), Trail.Max));
			FScreenshotRequest::RequestScreenshot(ShotDir() / TEXT("drag.png"), false, false);
			Finish(State);
			return false;
		}

		default:
			return false;
		}
	}

	// --- perf ------------------------------------------------------------------------------------------------------------------

	struct FPerfPhase
	{
		const TCHAR* Name;
		bool bTracks;
		/** Stress: synthetic stamps queued every frame, extra fade / shift passes every frame (GPU cost per item = delta / count). */
		int32 ExtraStamps = 0;
		int32 ExtraFades = 0;
		int32 ExtraShifts = 0;
	};

	const FPerfPhase PerfPhases[] = {
		{ TEXT("tracks on"), true },
		{ TEXT("tracks off"), false },
		{ TEXT("tracks on (again)"), true },
		{ TEXT("stress +512 stamps / frame"), true, 512, 0, 0 },
		{ TEXT("stress +10 fade passes / frame"), true, 0, 10, 0 },
		{ TEXT("stress +10 shift passes / frame"), true, 0, 0, 10 },
		{ TEXT("tracks off (again)"), false },
	};
	constexpr int32 PerfWave = 2;
	constexpr float PerfSettleSeconds = 4.f;
	constexpr float PerfPhaseSeconds = 4.f;
	constexpr float PerfPhaseWarmup = 0.5f;

	struct FPerfState
	{
		int32 Phase = -1;
		double PhaseStart = 0.;
		double Start = -1.;
		int32 Frames = 0;
		double Gpu = 0.;
		double Render = 0.;
		double Game = 0.;
		int64 StampsAtStart = 0;
		int64 FramesAtStart = 0;
		double CpuAtStart = 0.;
		int32 Walkers = 0;
		int32 Failures = 0;
		bool bWaveStarted = false;
		TArray<FString> Report;
	};

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

	bool PerfStep(TWeakObjectPtr<UWorld> WeakWorld, FPerfState& S)
	{
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		USnowInteractionSubsystem* Snow = USnowInteractionSubsystem::Get(World);
		UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
		const double Now = World->GetTimeSeconds();
		if (!Snow || !Waves)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[SnowTracksPerf] FAIL: no snow subsystem (needs a real RHI) or no wave subsystem"));
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: FAIL"));
			FPlatformMisc::RequestExit(false, TEXT("SnowTracksSmoke perf"));
			return false;
		}
		if (S.Start < 0.)
		{
			S.Start = Now;
		}
		KeepSquadSafe(World);
		if (!S.bWaveStarted)
		{
			if (Now - S.Start < 2. || !Snow->IsActive())
			{
				return true;
			}
			SmokeUtils::PlaceSquadAtTestStart(World);
			SmokeUtils::PlaceSquadAwayFromWaveSpawns(World, 3000.f);
			Waves->ClearAllEnemies();
			Waves->StartWave(PerfWave);
			S.bWaveStarted = true;
			S.PhaseStart = Now;
			UE_LOG(LogCodexTactics, Display, TEXT("[SnowTracksPerf] wave %d started: %d alive"), PerfWave, Waves->GetAliveEnemyCount());
			return true;
		}
		if (S.Phase < 0)
		{
			if (Now - S.PhaseStart < PerfSettleSeconds)
			{
				return true;
			}
			S.Phase = 0;
			S.PhaseStart = Now;
			SetCVar(TEXT("Codex.Env.SnowTracks"), PerfPhases[0].bTracks ? TEXT("1") : TEXT("0"));
			S.Frames = 0;
		}
		const double InPhase = Now - S.PhaseStart;
		const FPerfPhase& Phase = PerfPhases[S.Phase];
		if (Phase.ExtraStamps > 0)
		{
			const FVector2D Focus = Snow->GetLastFocus();
			FSnowStampSpec Spec = Snow->GetStamps().EnemyBoot;
			for (int32 Index = 0; Index < Phase.ExtraStamps; ++Index)
			{
				const FVector2D Where = Focus + FVector2D(((Index * 37) % 200 - 100) * 20., ((Index * 91) % 200 - 100) * 20.);
				Snow->AddStamp(SnowInteractionRules::MakePrint(Spec, Where, static_cast<float>(Index * 13 % 360)));
			}
		}
		if (Phase.ExtraFades > 0 || Phase.ExtraShifts > 0)
		{
			Snow->DebugRunExtraPasses(Phase.ExtraFades, Phase.ExtraShifts);
		}
		if (InPhase < PerfPhaseWarmup)
		{
			S.StampsAtStart = Snow->GetMetrics().StampsDrawn;
			S.FramesAtStart = Snow->GetMetrics().Frames;
			S.CpuAtStart = Snow->GetMetrics().CpuSeconds;
			return true;
		}
		S.Gpu += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0));
		S.Render += FPlatformTime::ToMilliseconds(GRenderThreadTime);
		S.Game += FPlatformTime::ToMilliseconds(GGameThreadTime);
		S.Walkers = FMath::Max(S.Walkers, Snow->GetMetrics().LastWalkers);
		++S.Frames;
		if (InPhase < PerfPhaseWarmup + PerfPhaseSeconds)
		{
			return true;
		}
		const USnowInteractionSubsystem::FMetrics& M = Snow->GetMetrics();
		const double N = FMath::Max(S.Frames, 1);
		const int64 Stamps = M.StampsDrawn - S.StampsAtStart;
		const int64 SnowFrames = FMath::Max<int64>(M.Frames - S.FramesAtStart, 1);
		const FString Line = FString::Printf(TEXT("%s | %d enemies alive | GPU %.2f ms | render %.2f ms | game %.2f ms | snow CPU %.4f ms/frame | stamps %.0f/s | walkers %d | frames %d"),
			PerfPhases[S.Phase].Name, Waves->GetAliveEnemyCount(), S.Gpu / N, S.Render / N, S.Game / N, (M.CpuSeconds - S.CpuAtStart) * 1000. / SnowFrames,
			Stamps / PerfPhaseSeconds, S.Walkers, S.Frames);
		UE_LOG(LogCodexTactics, Display, TEXT("[SnowTracksPerf] %s"), *Line);
		S.Report.Add(Line);
		if (PerfPhases[S.Phase].bTracks && Stamps <= 0)
		{
			++S.Failures;
			UE_LOG(LogCodexTactics, Display, TEXT("[SnowTracksPerf] FAIL: no stamps drawn while the tracks were on"));
		}
		S.Gpu = S.Render = S.Game = 0.;
		S.Frames = 0;
		S.Walkers = 0;
		++S.Phase;
		if (S.Phase >= static_cast<int32>(UE_ARRAY_COUNT(PerfPhases)))
		{
			SetCVar(TEXT("Codex.Env.SnowTracks"), TEXT("1"));
			UE_LOG(LogCodexTactics, Display, TEXT("[SnowTracksPerf] metrics: stamps %lld (dropped %lld), draws %lld, fade %lld, shift %lld, clamp %lld"),
				M.StampsDrawn, M.StampsDropped, M.DrawCalls, M.FadePasses, M.ShiftPasses, M.ClampPasses);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), S.Failures == 0 ? TEXT("PASS") : TEXT("FAIL"));
			FPlatformMisc::RequestExit(false, TEXT("SnowTracksSmoke perf"));
			return false;
		}
		S.PhaseStart = Now;
		SetCVar(TEXT("Codex.Env.SnowTracks"), PerfPhases[S.Phase].bTracks ? TEXT("1") : TEXT("0"));
		return true;
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		if (Args.Contains(TEXT("perf")))
		{
			TSharedRef<FPerfState> Perf = MakeShared<FPerfState>();
			// Every frame (the GPU / thread times are per frame).
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, Perf](float)
			{
				return PerfStep(WeakWorld, *Perf);
			}), 0.f);
			return;
		}
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.SnowTracksSmoke"),
		TEXT("Dev check (RENDERED): interactive snow - a 5 m walk prints into RT_SnowInteraction (read back), a 20 m view move keeps the prints in place, ")
		TEXT("the fade lowers them; 'perf' = MovementTest wave 2 A/B cost. Logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
