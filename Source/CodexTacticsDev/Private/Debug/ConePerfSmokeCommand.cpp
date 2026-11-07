// Dev-only check of bug #38 (user screenshots 2026-11-04, L_Gen_Depot_1: "the cones are broken since the 120 m range" - slow, rebuilt
// visibly, stacked translucent layers, thin shards from a roof). Run (rendered: the frame time includes the GPU):
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands ConePerfSmoke   (map + -LevelJson + -RenderOffscreen from test_map.json)
// From the squad's start on the Depot with the three operatives' cones on (Shift+O) at clear noon (120 m sight):
//  1. ground: the squad turning on the spot 240 frames - the cones' game-thread cost per frame (set-up + wait + mesh upload), the task-thread
//     work per rebuild, the wall frame time against the same frames with the cones off;
//  2. roof: the three on the hall's roof (VisionHeightSmoke's spot) turning - the same numbers, and every drawn triangle of every cone: none
//     slanted (its corners more than 30 cm apart in height: the old ground jumps between rays), none above the feet plane, none floating at
//     the roof's level far out (the old "no ground found" rays drawn at the roof for 120 m - the shards / needles);
//  3. the squad's cones drawn as ONE layer: no point of the merged squad layer covered by two cones' fills (sampled on a grid);
//  4. the weapon zone on the leader's cone: an edge line at the M16's effective range and a band to its max reach.
// Arg Shot -> Saved/Screenshots/ConeFix/ground.png, roof.png (-RenderOffscreen).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PmcEncounterSubsystem.h"
#include "Camera/TacticalCameraPawn.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/CombatPressureSubsystem.h"
#include "Combat/VisionConeComponent.h"
#include "Combat/VisionConeVisibility.h"
#include "Containers/Ticker.h"
#include "Data/WeaponDataAsset.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Environment/EnvironmentSubsystem.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"

#ifndef CODEX_CONE_PERF_BASELINE
#define CODEX_CONE_PERF_BASELINE 0 // 1 = the pre-fix build of 2026-11-07 (no merged layer / weapon zone API): only the cost and the shards
#endif

namespace ConePerfSmoke
{
	constexpr float WarmupSeconds = 3.f;
	constexpr float Timeout = 300.f;
	constexpr int32 PhaseFrames = 240;
	/** The hall B_hall_n's roof on gen_ref_01_02_depot_1 (VisionHeightSmoke: its west edge at x 3200, the roof spreads to +x). */
	const FVector2D RoofSpots[] = { FVector2D(3450.f, -1500.f), FVector2D(3600.f, -1300.f), FVector2D(3600.f, -1700.f) };
	constexpr float RoofYaw = 180.f;

	enum class EStage : uint8 { Setup, Off, Ground, RoofPlace, Roof, RoofSweep, Merge, ZoneGround, ShotGround, ShotGroundTake, ShotRoof, ShotRoofTake, Done };

	struct FPhase
	{
		int32 Frames = 0;
		double WallSeconds = 0.0;
		double MaxFrameSeconds = 0.0;
		FVisionConeBuildStats Start;
		FVisionConeBuildStats End;
	};

	struct FState
	{
		EStage Stage = EStage::Setup;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Failures = 0;
		bool bShot = false;
		bool bShotRequested = false;
		int32 Frame = 0;
		double LastTick = 0.0;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Squad;
		TArray<FVector> Feet;
		TArray<FVector> GroundFeet;
		TArray<FVector> RoofFeet;
		TArray<float> RoofYaws;
		TArray<float> GroundYaws;
		TArray<float> Yaws;
		FPhase Off;
		FPhase Ground;
		FPhase Roof;
		/** The roof sweep: spots on the hall's roof x stances x yaws, and what the cones drew there. */
		TArray<FVector> SweepSpots;
		int32 SweepIndex = -1;
		int32 SweepPoses = 0;
		FVisionConeMeshAudit SweepAudit;
		int32 SweepSlivers = 0;
		int32 SweepSpans = 0;
		FString SweepWorst;
		int32 SweepWorstCount = 0;
	};

	constexpr int32 SweepStances = 2;
	constexpr int32 SweepYaws = 4;

	/** Planar points on the hall's roof (its surface z 850 .. 950, the roof 2 m round each too: no roof hole / parapet next to it). */
	TArray<FVector> FindRoofSpots(UWorld* World)
	{
		TArray<FVector> Spots;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ConePerfSmoke), false);
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			Params.AddIgnoredActor(*It);
		}
		auto RoofZ = [World, &Params](float X, float Y, float& OutZ)
		{
			FHitResult Hit;
			if (!World->LineTraceSingleByChannel(Hit, FVector(X, Y, 3000.f), FVector(X, Y, -500.f), ECC_Visibility, Params))
			{
				return false;
			}
			OutZ = static_cast<float>(Hit.ImpactPoint.Z);
			return OutZ > 850.f && OutZ < 950.f;
		};
		for (float X = 3400.f; X <= 7000.f && Spots.Num() < 6; X += 700.f)
		{
			for (float Y = -3400.f; Y <= 400.f && Spots.Num() < 6; Y += 700.f)
			{
				float Z = 0.f;
				float Around = 0.f;
				if (RoofZ(X, Y, Z) && RoofZ(X + 200.f, Y, Around) && RoofZ(X - 200.f, Y, Around) && RoofZ(X, Y + 200.f, Around) && RoofZ(X, Y - 200.f, Around))
				{
					Spots.Add(FVector(X, Y, Z));
				}
			}
		}
		return Spots;
	}

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	void Enter(FState& State, EStage Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
		State.Frame = 0;
	}

	FVector FeetOf(const ACharacter& Character)
	{
		return Character.GetActorLocation() - FVector(0.f, 0.f, Character.GetSimpleCollisionHalfHeight());
	}

	/** Holds each operative on his spot, facing his yaw (turned by TurnDeg per frame while measuring). */
	void Hold(FState& State, float TurnDeg)
	{
		for (int32 Index = 0; Index < State.Squad.Num(); ++Index)
		{
			AOperativeCharacter* Operative = State.Squad[Index].Get();
			if (!Operative)
			{
				continue;
			}
			State.Yaws[Index] = FRotator::NormalizeAxis(State.Yaws[Index] + TurnDeg);
			if (FVector::Dist2D(FeetOf(*Operative), State.Feet[Index]) > 15.f || FMath::Abs(FeetOf(*Operative).Z - State.Feet[Index].Z) > 30.f)
			{
				Operative->StopOperative();
				Operative->SetActorLocation(State.Feet[Index] + FVector(0.f, 0.f, Operative->GetSimpleCollisionHalfHeight() + 2.f), false, nullptr,
					ETeleportType::TeleportPhysics);
			}
			Operative->SetActorRotation(FRotator(0.f, State.Yaws[Index], 0.f));
		}
	}

	void FocusCamera(UWorld* World, const FVector& Point, float Distance, float Yaw)
	{
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
		{
			if (ATacticalCameraPawn* Camera = PC->GetPawn<ATacticalCameraPawn>())
			{
				Camera->SetCinematicLock(true);
				Camera->SetCinematicYaw(Yaw);
				Camera->SmoothFocusOnPosition(Point, 0.2f, Distance);
			}
			if (PC->MyHUD)
			{
				PC->MyHUD->bShowHUD = false;
			}
		}
	}

	/** Measures one frame of a phase (wall time since the previous ticker call). */
	void Sample(FState& State, FPhase& Phase)
	{
		const double Now = FPlatformTime::Seconds();
		if (Phase.Frames == 0)
		{
			Phase.Start = UVisionConeComponent::GetBuildStats();
		}
		else
		{
			const double Delta = Now - State.LastTick;
			Phase.WallSeconds += Delta;
			Phase.MaxFrameSeconds = FMath::Max(Phase.MaxFrameSeconds, Delta);
		}
		++Phase.Frames;
		Phase.End = UVisionConeComponent::GetBuildStats();
	}

	/** Game-thread cone ms per frame of a phase. */
	double GameMsPerFrame(const FPhase& Phase)
	{
		return Phase.Frames > 1 ? (Phase.End.Seconds - Phase.Start.Seconds) * 1000.0 / (Phase.Frames - 1) : 0.0;
	}

	void LogPhase(const TCHAR* Name, const FPhase& Phase)
	{
		const double Frames = FMath::Max(1, Phase.Frames - 1);
		const double Builds = FMath::Max<double>(1.0, static_cast<double>(Phase.End.Builds - Phase.Start.Builds));
		UE_LOG(LogCodexTactics, Display,
			TEXT("[ConePerf] %s: %d frames, wall %.2f ms / frame (max %.2f); cones game thread %.3f ms / frame (wait %.3f, upload %.3f); %lld rebuilds")
			TEXT(" (%.2f / frame), %.0f rays / %.0f traces / %.3f ms task work per rebuild, deferred %lld, peak %d / frame, off screen %lld, re-cuts %lld, frames in flight %lld"),
			Name, Phase.Frames, Phase.WallSeconds * 1000.0 / Frames, Phase.MaxFrameSeconds * 1000.0, GameMsPerFrame(Phase),
			(Phase.End.WaitSeconds - Phase.Start.WaitSeconds) * 1000.0 / Frames, (Phase.End.ApplySeconds - Phase.Start.ApplySeconds) * 1000.0 / Frames,
			Phase.End.Builds - Phase.Start.Builds, (Phase.End.Builds - Phase.Start.Builds) / Frames,
			static_cast<double>(Phase.End.Rays - Phase.Start.Rays) / Builds, static_cast<double>(Phase.End.Traces - Phase.Start.Traces) / Builds,
			(Phase.End.TaskSeconds - Phase.Start.TaskSeconds) * 1000.0 / Builds, Phase.End.Deferred - Phase.Start.Deferred, Phase.End.PeakPerFrame,
			Phase.End.SkippedOffscreen - Phase.Start.SkippedOffscreen, Phase.End.Reshapes - Phase.Start.Reshapes, Phase.End.FramesInFlight - Phase.Start.FramesInFlight);
	}

	bool Finish(UWorld* World, FState& State, bool bComplete)
	{
		UVisionConeComponent::SetPlayerSquadCones(false);
		if (UEnvironmentSubsystem* Environment = UEnvironmentSubsystem::Get(World))
		{
			Environment->SetClockScale(1.f);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		State.Stage = EStage::Done;
		FPlatformMisc::RequestExit(false, TEXT("ConePerfSmoke"));
		return false;
	}

	bool Shot(FState& State, const TCHAR* File, EStage Next)
	{
		if (!State.bShotRequested)
		{
			State.bShotRequested = true;
			const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("ConeFix") / File;
			FScreenshotRequest::RequestScreenshot(Path, false, false);
			UE_LOG(LogCodexTactics, Display, TEXT("[ConePerf] screenshot requested: %s"), *Path);
			return true;
		}
		if (State.StageTime >= 1.5f)
		{
			State.bShotRequested = false;
			Enter(State, Next);
		}
		return true;
	}

	bool Step(UWorld* World, FState& State, float Dt)
	{
		State.Time += Dt;
		State.StageTime += Dt;
		if (State.Time > Timeout)
		{
			Check(State, false, FString::Printf(TEXT("timeout in stage %d"), static_cast<int32>(State.Stage)));
			return Finish(World, State, false);
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UEnvironmentSubsystem* Environment = UEnvironmentSubsystem::Get(World);
		if (!Squad || !Squad->GetLeader() || !Environment || !Environment->IsInitialised())
		{
			Check(State, false, TEXT("squad with a leader and an initialised environment"));
			return Finish(World, State, false);
		}
		const bool bMeasuring = State.Stage == EStage::Off || State.Stage == EStage::Ground || State.Stage == EStage::Roof;
		if (State.Stage != EStage::Setup)
		{
			Hold(State, bMeasuring ? 1.f : 0.f);
		}
		ON_SCOPE_EXIT { State.LastTick = FPlatformTime::Seconds(); };

		switch (State.Stage)
		{
		case EStage::Setup:
		{
			if (UCombatPressureSubsystem* Pressure = World->GetSubsystem<UCombatPressureSubsystem>())
			{
				Pressure->SetAutoSpawn(false);
				Pressure->DespawnAll();
			}
			if (UPmcEncounterSubsystem* Pmc = UPmcEncounterSubsystem::Get(World))
			{
				Pmc->CancelPending();
			}
			if (IConsoleVariable* Immortal = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Cheat.Immortal")))
			{
				Immortal->Set(1, ECVF_SetByConsole);
			}
			// Uncapped frames: the wall frame time is the real cost (a capped 60 fps hides everything under 16.7 ms).
			for (const TCHAR* Name : { TEXT("t.MaxFPS"), TEXT("r.VSync") })
			{
				if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
				{
					Variable->Set(0, ECVF_SetByConsole);
				}
			}
			Environment->SetClockScale(0.f);
			Environment->ForceWeather(EWeatherState::Clear, true);
			Environment->SetHour(12.f);
			Squad->EnterSoloMode();
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member && State.Squad.Num() < 3)
				{
					Member->StopOperative();
					Member->SetStance(EOperativeStance::Standing);
					State.Squad.Add(Member);
					State.Feet.Add(FeetOf(*Member));
					State.Yaws.Add(static_cast<float>(Member->GetActorRotation().Yaw));
				}
			}
			Check(State, State.Squad.Num() == 3, FString::Printf(TEXT("three operatives (%d)"), State.Squad.Num()));
			if (State.Squad.Num() != 3)
			{
				return Finish(World, State, false);
			}
			State.GroundFeet = State.Feet;
			State.GroundYaws = State.Yaws;
			UVisionConeComponent::SetPlayerSquadCones(false);
			Enter(State, EStage::Off);
			return true;
		}

		case EStage::Off:
		case EStage::Ground:
		case EStage::Roof:
		{
			FPhase& Phase = State.Stage == EStage::Off ? State.Off : (State.Stage == EStage::Ground ? State.Ground : State.Roof);
			if (State.StageTime < 0.1f)
			{
				FocusCamera(World, State.Feet[0], 4500.f, State.Yaws[0] - 90.f); // the squad on screen (off-screen cones are not re-traced)
			}
			if (State.StageTime < 0.5f)
			{
				return true; // settle (the cones' first urgent rebuilds out of the window)
			}
			Sample(State, Phase);
			if (Phase.Frames < PhaseFrames)
			{
				return true;
			}
			if (State.Stage == EStage::Off)
			{
				LogPhase(TEXT("ground, cones OFF"), Phase);
				UVisionConeComponent::SetPlayerSquadCones(true);
				Enter(State, EStage::Ground);
				return true;
			}
			const bool bRoof = State.Stage == EStage::Roof;
			LogPhase(bRoof ? TEXT("roof, 3 cones") : TEXT("ground, 3 cones"), Phase);
			const double OffFrame = State.Off.WallSeconds * 1000.0 / FMath::Max(1, State.Off.Frames - 1);
			const double OnFrame = Phase.WallSeconds * 1000.0 / FMath::Max(1, Phase.Frames - 1);
			Check(State, GameMsPerFrame(Phase) < 1.0, FString::Printf(TEXT("%s: the cones cost %.3f ms / frame on the game thread (< 1 ms; wall frame %.2f ms vs %.2f ms with the cones off)"),
				bRoof ? TEXT("roof") : TEXT("ground"), GameMsPerFrame(Phase), OnFrame, OffFrame));
			if (!bRoof)
			{
				Enter(State, EStage::RoofPlace);
				return true;
			}
			// Shards: every triangle of every cone (the shape of the last rebuild).
			for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Squad)
			{
				const AOperativeCharacter* Operative = Weak.Get();
				if (!Operative || !Operative->VisionCone)
				{
					continue;
				}
				const FVisionConeMeshAudit Audit = Operative->VisionCone->AuditMesh(3000.f);
				Check(State, Operative->VisionCone->IsOccluded() && Audit.Triangles > 0 && Audit.Slanted == 0 && Audit.Above == 0 && Audit.Floating == 0,
					FString::Printf(TEXT("roof, %s's cone (%.0f m): %d triangles - %d slanted, %d above the feet, %d at the roof's level beyond 30 m (all 0)"),
						*Operative->GetName(), Operative->VisionCone->GetConeRangeCm() / 100.f, Audit.Triangles, Audit.Slanted, Audit.Above, Audit.Floating));
			}
			State.SweepSpots = FindRoofSpots(World);
			Check(State, State.SweepSpots.Num() >= 3, FString::Printf(TEXT("roof sweep: %d clear spots on the hall's roof"), State.SweepSpots.Num()));
			State.SweepIndex = -1;
			Enter(State, EStage::RoofSweep);
			return true;
		}

		case EStage::RoofSweep:
		{
			// Every pose: the three round a roof spot, one stance, one yaw; 0.4 s to rebuild, then every cone's mesh and rays.
			const int32 Poses = State.SweepSpots.Num() * SweepStances * SweepYaws;
			if (State.SweepIndex >= 0 && State.StageTime < 0.4f)
			{
				return true;
			}
			if (State.SweepIndex >= 0)
			{
				for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Squad)
				{
					const AOperativeCharacter* Operative = Weak.Get();
					if (!Operative || !Operative->VisionCone || !Operative->VisionCone->IsOccluded())
					{
						continue;
					}
					const FVisionConeMeshAudit Audit = Operative->VisionCone->AuditMesh(3000.f);
					const int32 Slivers = VisionConeVisibility::FindSlivers(Operative->VisionCone->GetBuiltRays(), 60.f, 1500.f);
					State.SweepAudit.Triangles += Audit.Triangles;
					State.SweepAudit.Slanted += Audit.Slanted;
					State.SweepAudit.Above += Audit.Above;
					State.SweepAudit.Floating += Audit.Floating;
					State.SweepSlivers += Slivers;
					for (const FVisionConeRay& Ray : Operative->VisionCone->GetBuiltRays())
					{
						State.SweepSpans += Ray.Spans.Num();
					}
					const int32 Bad = Audit.Slanted + Audit.Above + Audit.Floating + Slivers;
					if (Bad > State.SweepWorstCount)
					{
						State.SweepWorstCount = Bad;
						State.SweepWorst = FString::Printf(TEXT("spot (%.0f, %.0f) %s yaw %.0f: %d slanted, %d above, %d floating, %d slivers of %d rays"),
							State.Feet[0].X, State.Feet[0].Y, Operative->GetStance() == EOperativeStance::Crouching ? TEXT("crouched") : TEXT("standing"),
							State.Yaws[0], Audit.Slanted, Audit.Above, Audit.Floating, Slivers, Operative->VisionCone->GetBuiltRays().Num());
					}
				}
				++State.SweepPoses;
			}
			if (++State.SweepIndex >= Poses)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[ConePerf] roof sweep: %d poses, %d triangles, %d spans; worst: %s"), State.SweepPoses,
					State.SweepAudit.Triangles, State.SweepSpans, State.SweepWorst.IsEmpty() ? TEXT("none") : *State.SweepWorst);
				// Slivers: the filter's passes leave a stray one in ~100 000 spans (a chain of ever thinner stretches); the old sampler drew ~3 800.
				Check(State, State.SweepPoses > 0 && State.SweepAudit.Slanted == 0 && State.SweepAudit.Above == 0 && State.SweepAudit.Floating == 0
					&& State.SweepSlivers <= FMath::Max(1, State.SweepSpans / 10000),
					FString::Printf(TEXT("roof sweep (%d poses x 3 cones): %d slanted triangles, %d above the feet, %d at the roof's level far out (all 0), %d slivers narrower than 60 cm beyond 15 m of %d spans (<= 0.01 %%)"),
						State.SweepPoses, State.SweepAudit.Slanted, State.SweepAudit.Above, State.SweepAudit.Floating, State.SweepSlivers, State.SweepSpans));
				Enter(State, EStage::Merge);
				return true;
			}
			const int32 Spot = State.SweepIndex / (SweepStances * SweepYaws);
			const int32 Stance = (State.SweepIndex / SweepYaws) % SweepStances;
			const float Yaw = 90.f * (State.SweepIndex % SweepYaws) + 30.f;
			const FVector Offsets[] = { FVector::ZeroVector, FVector(-150.f, 120.f, 0.f), FVector(-150.f, -120.f, 0.f) };
			for (int32 Index = 0; Index < State.Squad.Num(); ++Index)
			{
				State.Feet[Index] = State.SweepSpots[Spot] + FRotator(0.f, Yaw, 0.f).RotateVector(Offsets[Index]);
				State.Yaws[Index] = Yaw;
				if (AOperativeCharacter* Operative = State.Squad[Index].Get())
				{
					Operative->SetStance(Stance == 0 ? EOperativeStance::Standing : EOperativeStance::Crouching);
				}
			}
			State.StageTime = 0.f;
			return true;
		}

		case EStage::RoofPlace:
		{
			// The three on the hall's roof, facing out over its west edge (the cones look down into the yard).
			for (int32 Index = 0; Index < State.Squad.Num(); ++Index)
			{
				FHitResult Hit;
				FCollisionQueryParams Params(SCENE_QUERY_STAT(ConePerfSmoke), false);
				for (TActorIterator<APawn> It(World); It; ++It)
				{
					Params.AddIgnoredActor(*It);
				}
				const FVector2D Spot = RoofSpots[Index];
				if (!World->LineTraceSingleByChannel(Hit, FVector(Spot, 3000.f), FVector(Spot, -500.f), ECC_Visibility, Params) || Hit.ImpactPoint.Z < 250.f)
				{
					Check(State, false, FString::Printf(TEXT("the hall's roof under (%.0f, %.0f)"), Spot.X, Spot.Y));
					return Finish(World, State, false);
				}
				State.Feet[Index] = Hit.ImpactPoint;
				State.Yaws[Index] = RoofYaw + (Index - 1) * 20.f;
				if (AOperativeCharacter* Operative = State.Squad[Index].Get())
				{
					Operative->SetStance(EOperativeStance::Crouching); // the user's roof scene (cone_48): the squad crouched
				}
			}
			Check(State, true, FString::Printf(TEXT("the squad on the hall's roof (z %.0f)"), State.Feet[0].Z));
			State.RoofFeet = State.Feet;
			State.RoofYaws = State.Yaws;
			Enter(State, EStage::Roof);
			return true;
		}

		case EStage::Merge:
		{
			if (State.StageTime < 0.6f)
			{
				return true; // the still squad: every cone rebuilt / merged once more
			}
#if !CODEX_CONE_PERF_BASELINE
			// One layer: a 2 m grid over the squad's cones - no point drawn by two cones' fills (the merged squad layer draws each once).
			TArray<const UVisionConeComponent*> Cones;
			for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Squad)
			{
				if (const AOperativeCharacter* Operative = Weak.Get(); Operative && Operative->VisionCone && Operative->VisionCone->IsConeShown())
				{
					Cones.Add(Operative->VisionCone);
				}
			}
			int32 Covered = 0;
			int32 Double = 0;
			const FVector Centre = State.Feet[0];
			for (float X = -9000.f; X <= 9000.f; X += 200.f)
			{
				for (float Y = -9000.f; Y <= 9000.f; Y += 200.f)
				{
					const FVector Point = Centre + FVector(X, Y, 0.f);
					int32 Drawn = 0;
					for (const UVisionConeComponent* Cone : Cones)
					{
						Drawn += Cone->GetDrawnMergedLevelAt(Point) > 0 ? 1 : 0;
					}
					Covered += Drawn > 0 ? 1 : 0;
					Double += Drawn > 1 ? 1 : 0;
				}
			}
			Check(State, Cones.Num() == 3 && Covered > 50 && Double <= Covered / 200,
				FString::Printf(TEXT("the squad's %d cones are one layer: %d grid points drawn, %d drawn twice (<= 0.5 %%: the seams between rays)"), Cones.Num(), Covered, Double));
#endif
			// Back at the squad's start, standing: the weapon zone on the leader's cone (from a roof the ground 28 - 60 m out can lie in the dead zone).
			State.Feet = State.GroundFeet;
			State.Yaws = State.GroundYaws;
			for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Squad)
			{
				if (AOperativeCharacter* Operative = Weak.Get())
				{
					Operative->SetStance(EOperativeStance::Standing);
				}
			}
			Enter(State, EStage::ZoneGround);
			return true;
		}

		case EStage::ZoneGround:
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
#if !CODEX_CONE_PERF_BASELINE
			// The weapon zone on the leader's cone (M16: effective 28 m, max 60 m).
			if (const AOperativeCharacter* Leader = Squad->GetLeader(); Leader && Leader->VisionCone && Leader->CurrentWeapon)
			{
				const UVisionConeComponent& Cone = *Leader->VisionCone;
				Check(State, FMath::IsNearlyEqual(Cone.GetWeaponEffectiveCm(), Leader->CurrentWeapon->GetEffectiveRangeCm(), 1.f)
					&& FMath::IsNearlyEqual(Cone.GetWeaponMaxCm(), Leader->CurrentWeapon->AttackRangeCm, 1.f) && Cone.GetWeaponZoneVertexCount() > 0,
					FString::Printf(TEXT("the leader's cone shows the %s zone: edge %.0f m, band to %.0f m (%d vertices)"), *Leader->CurrentWeapon->WeaponId,
						Cone.GetWeaponEffectiveCm() / 100.f, Cone.GetWeaponMaxCm() / 100.f, Cone.GetWeaponZoneVertexCount()));
			}
#endif
			if (!State.bShot)
			{
				return Finish(World, State, true);
			}
			State.Feet = State.RoofFeet; // the user's roof scene for the still: crouched at the hall's west edge, looking into the yard
			State.Yaws = State.RoofYaws;
			for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Squad)
			{
				if (AOperativeCharacter* Operative = Weak.Get())
				{
					Operative->SetStance(EOperativeStance::Crouching);
				}
			}
			Enter(State, EStage::ShotRoof);
			return true;
		}

		case EStage::ShotRoof:
		{
			const FVector Facing = FRotator(0.f, RoofYaw, 0.f).Vector();
			FocusCamera(World, State.Feet[0] + Facing * 1500.f, 5500.f, RoofYaw - 90.f);
			if (State.StageTime >= 5.f)
			{
				Enter(State, EStage::ShotRoofTake);
			}
			return true;
		}

		case EStage::ShotRoofTake:
			Shot(State, TEXT("roof.png"), EStage::ShotGround);
			if (State.Stage == EStage::ShotGround)
			{
				State.Feet = State.GroundFeet; // back at the squad's start for the ground still
				State.Yaws = State.GroundYaws;
			}
			return true;

		case EStage::ShotGround:
		{
			const FVector Facing = FRotator(0.f, State.Yaws[0], 0.f).Vector();
			FocusCamera(World, State.Feet[0] + Facing * 2000.f, 6000.f, State.Yaws[0] - 90.f);
			if (State.StageTime >= 5.f)
			{
				Enter(State, EStage::ShotGroundTake);
			}
			return true;
		}

		case EStage::ShotGroundTake:
			Shot(State, TEXT("ground.png"), EStage::Done);
			return State.Stage != EStage::Done || Finish(World, State, true);

		default:
			return false;
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TSharedRef<FState> State = MakeShared<FState>();
		State->bShot = Args.Num() > 0 && Args[0].Equals(TEXT("Shot"), ESearchCase::IgnoreCase);
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld, State]()
		{
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
			{
				UWorld* W = WeakWorld.Get();
				return W && State->Stage != EStage::Done && Step(W, *State, W->IsPaused() ? 0.f : W->GetDeltaSeconds());
			}), 0.f);
		}), WarmupSeconds, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.ConePerfSmoke"),
		TEXT("Dev check (bug #38, L_Gen_Depot_1): the squad's three 120 m cones - game-thread cost per frame on the ground and on a roof (< 1 ms), ")
		TEXT("no shards (slanted / upward / roof-level far triangles) from the roof, the squad's cones one layer, the weapon zone on the leader's cone. ")
		TEXT("Arg Shot saves Saved/Screenshots/ConeFix/roof.png, ground.png. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
