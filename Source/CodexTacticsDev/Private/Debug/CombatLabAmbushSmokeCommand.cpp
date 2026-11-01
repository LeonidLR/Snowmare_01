// Dev-only headless check of the combat lab's ambushes after the game designer's answers of 2026-11-01 (docs/design/COMBAT_LAB.md
// "Ambushes v2") on /Game/Maps/L_FormationLabCombat (level JSON L_FormationLabCombat.json):
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands CombatLabAmbushSmoke
//  1. the first group comes at 30 s (answer 6): the level's own timer, untouched - not before 30 s, the first event at 30 s;
//  2. hiding spots (answer 1): the level's candidates (wall ends, doorways, the 8 authored points of the level JSON); three placements of
//     the squad, a seeded single ambush each - every ambusher at a concealed candidate spot clear of the walls (its record + the smoke's own
//     rays), the three kinds (authored, doorway, wall end) all chosen;
//  3. coordinated ambush (answer 2): a plan of 6 round the squad (flank left hound, flank right frostbitten, front cutter + marksman, rear
//     hound + spitter) - four parties at concealed spots in their own sectors, 6 alive above max_alive 4 (the plans' own cap 6); watched all
//     round it HOLDS; the flanks unwatched -> the FLANK BLOCK (both flank parties strike and the ranged ones open suppressive fire while the
//     melee front / rear still wait), then the ASSAULT (rear + front) within assault_min..assault_max; a ranged suppressor's deliberate
//     rounds suppress an operative while the others flank;
//  4. relocation (answer 3; the HYBRID of decision 1, 2026-11-01 night): a single ambush the squad leaves behind (placed 52 m away, its back
//     turned) sets off after relocate.after_sec along a CONCEALED ROUTE (no sight sample seen) and RUNS it on foot - no step longer than a
//     run's, never spotted on the way - to a concealed spot AHEAD of the squad: the same enemy, still hidden, never despawned;
//  (decision 3) every wall-end / doorway hiding spot stands 0.8 m off its wall's face, the ambushers hug their wall;
//  5. the watch (answer 4): an operative aiming at ANOTHER enemy (a frozen decoy he fires at in a skirmish) keeps an ambusher inside his
//     aim cone holding (>= 2.5 s, found by his look).
// "shot" (rendered: UnrealEditor.exe <uproject> /Game/Maps/L_FormationLabCombat -game -RenderOffscreen -ResX=1280 -ResY=720
// "-LevelJson=L_FormationLabCombat.json" "-ExecCmds=CodexTactics.CombatLabAmbushSmoke shot") saves stills to Saved/Screenshots/CombatLabV2/
// (01_coordinated_plan, 02_flank_block, 03_suppressor) with every enemy revealed and the plan marked: a ring under each plan member (red =
// flank parties, yellow = front, blue = rear; thick = struck, thin = still hidden) and a magenta line from each suppressor to its target.
// Godot reference: none (UE-only).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/AmbushWatchRules.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/FirePostureRules.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/AmbushPlanRules.h"
#include "Combat/AmbushSpotRules.h"
#include "Combat/CombatPressureRules.h"
#include "Combat/CombatPressureSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/SuppressionSubsystem.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFlow/LevelEncounterSubsystem.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "ShaderCompiler.h"
#include "SmokeUtils.h"
#include "TimerManager.h"
#include "UnrealClient.h"

namespace CombatLabAmbushSmoke
{
	constexpr float NavWarmupSeconds = 3.f;
	constexpr float Timeout = 900.f;

	enum class EStage : uint8
	{
		FirstEvent,
		FirstSettle,
		SpotsPlace,
		SpotsSpawn,
		PlanPlace,
		PlanHold,
		PlanFlank,
		PlanAssault,
		PlanSuppress,
		PlanSettle,
		RelocPlace,
		RelocLeave,
		RelocWait,
		RelocRun,
		AimPlace,
		AimWait,
		AimHold,
		Done
	};

	struct FPhoto
	{
		FString Name;
		FVector Target = FVector::ZeroVector;
		FRotator View = FRotator::ZeroRotator;
		float Distance = 1500.f;
		int32 Frames = 0;
		int32 TakenFrame = 0;
		bool bTaken = false;
	};

	/** A squad placement and the hiding-spot kind its seeded single ambush should pick (the lab's geometry). */
	struct FPlacement
	{
		FVector2D Lead;
		float Yaw;
		EAmbushSpotKind Expect;
	};

	struct FState
	{
		EStage Stage = EStage::FirstEvent;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Failures = 0;
		bool bShot = false;
		TArray<FPhoto> Photos;
		TWeakObjectPtr<ACameraActor> Camera;
		/** The stage placed the squad already. */
		bool bPlaced = false;
		int32 Placement = 0;
		TSet<EAmbushSpotKind> KindsSeen;
		int32 PlanId = 0;
		TArray<TWeakObjectPtr<AEnemyCharacter>> PlanMembers;
		bool bMeleeSprungEarly = false;
		int32 DeliberateBefore = 0;
		/** Deliberate rounds counted while the plan was still in its flank block (the melee rear / front waiting). */
		int32 DeliberateInFlankBlock = 0;
		float MaxOperativeLevel = 0.f;
		bool bFlankPhotoQueued = false;
		TWeakObjectPtr<AEnemyCharacter> Relocator;
		int32 RelocationsBefore = 0;
		int32 RecordsBefore = 0;
		float LeftAtClock = 0.f;
		/** The relocation run: its record, the most any runner moved in one frame, whether an operative ever saw it on the way. */
		int32 RunRecord = INDEX_NONE;
		float MaxFrameStepCm = 0.f;
		FVector LastFeet = FVector::ZeroVector;
		bool bSeenOnRun = false;
		TWeakObjectPtr<AEnemyCharacter> Decoy;
		TWeakObjectPtr<AOperativeCharacter> Aimer;
		TWeakObjectPtr<AEnemyCharacter> AimAmbusher;
		bool bAimDiscriminating = false;
	};

	const FPlacement Placements[] = {
		{ FVector2D(2750.0, -4200.0), 90.f, EAmbushSpotKind::Authored },  // W_B_3's authored point straight ahead behind the wall
		{ FVector2D(-2500.0, 4200.0), 90.f, EAmbushSpotKind::Doorway },   // the V2 gap run: beside its 4.5 m doorway
		{ FVector2D(3000.0, -600.0), 90.f, EAmbushSpotKind::WallEnd } };  // behind W_D_12's east end

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		if (UWorld* World = GEngine ? GEngine->GetCurrentPlayWorld() : nullptr)
		{
			UGameplayStatics::SetGlobalTimeDilation(World, 1.f);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		State.Stage = EStage::Done;
		FPlatformMisc::RequestExit(false, TEXT("CombatLabAmbushSmoke"));
		return false;
	}

	void Enter(FState& State, EStage Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
		State.bPlaced = false;
		UE_LOG(LogCodexTactics, Display, TEXT("[CombatLabAmbush] stage %d at %.1f s"), static_cast<int32>(Stage), State.Time);
	}

	FVector FeetOf(const ACharacter& Character)
	{
		return Character.GetActorLocation() - FVector(0.f, 0.f, Character.GetSimpleCollisionHalfHeight());
	}

	/** A navmesh point at ground level near Point (the ground under it), or unset. */
	TOptional<FVector> GroundNav(UWorld* World, const FVector& Point)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		FHitResult Ground;
		if (!Nav || !World->LineTraceSingleByObjectType(Ground, Point + FVector(0.f, 0.f, 1500.f), Point - FVector(0.f, 0.f, 1500.f),
			FCollisionObjectQueryParams(ECC_WorldStatic)))
		{
			return {};
		}
		FNavLocation OnNav;
		if (!Nav->ProjectPointToNavigation(Ground.ImpactPoint, OnNav, FVector(60.f, 60.f, 100.f)))
		{
			return {};
		}
		return OnNav.Location;
	}

	/** The smoke's own clearance check: no world-static hit on 36 horizontal rays of RadiusCm at 50 and 150 cm over the feet, on the floor. */
	bool RaysClear(UWorld* World, const FVector& Feet, float RadiusCm, FString& OutWhy)
	{
		const FCollisionObjectQueryParams Statics(ECC_WorldStatic);
		const FCollisionQueryParams Params(SCENE_QUERY_STAT(CombatLabAmbushRays), false);
		for (const float Height : { 50.f, 150.f })
		{
			for (int32 Step = 0; Step < 36; ++Step)
			{
				const FVector From = Feet + FVector(0.f, 0.f, Height);
				const FVector To = From + FVector(RadiusCm, 0.f, 0.f).RotateAngleAxis(Step * 10.f, FVector::UpVector);
				FHitResult Hit;
				if (World->LineTraceSingleByObjectType(Hit, From, To, Statics, Params))
				{
					OutWhy = FString::Printf(TEXT("%s %.0f cm away at %d deg"), *GetNameSafe(Hit.GetActor()), Hit.Distance, Step * 10);
					return false;
				}
			}
		}
		FHitResult Ground;
		if (!World->LineTraceSingleByObjectType(Ground, Feet + FVector(0.f, 0.f, 600.f), Feet - FVector(0.f, 0.f, 200.f), Statics, Params)
			|| FMath::Abs(Ground.ImpactPoint.Z - Feet.Z) > 40.f)
		{
			OutWhy = FString::Printf(TEXT("the ground under it is %s"), *GetNameSafe(Ground.GetActor()));
			return false;
		}
		return true;
	}

	/** Teleports the squad (Solo while placed): the leader at Lead facing Yaw, the others in pairs behind him. */
	void PlaceSquad(UWorld* World, USquadSubsystem& Squad, const FVector2D& Lead, float Yaw)
	{
		AOperativeCharacter* Leader = Squad.GetLeader();
		Squad.EnterSoloMode();
		const FRotator Rotation(0.f, Yaw, 0.f);
		const FVector Forward = Rotation.Vector();
		const FVector Right = FRotator(0.f, Yaw + 90.f, 0.f).Vector();
		int32 Follower = 0;
		for (AOperativeCharacter* Each : Squad.GetMembers())
		{
			if (!Each)
			{
				continue;
			}
			Each->LeaveCover(TEXT("smoke reset"));
			Each->SetFormationWallPose(false);
			Each->ClearPendingCover();
			Each->StopOperative();
			Each->SetStance(EOperativeStance::Standing);
			FVector Offset = FVector::ZeroVector;
			if (Each != Leader)
			{
				Offset = (Follower % 2 == 0 ? Right : -Right) * 150.f - Forward * (150.f * static_cast<float>(1 + Follower / 2));
				++Follower;
			}
			const FVector Want(Lead.X + Offset.X, Lead.Y + Offset.Y, 0.f);
			const TOptional<FVector> Ground = GroundNav(World, Want);
			const FVector Spot = SmokeUtils::FreeSpot(World, Ground.Get(Want), Each);
			Each->TeleportTo(Spot + FVector(0.f, 0.f, 4.f), Rotation, false, true);
		}
		Squad.SetLeader(Leader);
		Squad.ExitSoloMode(false, 0.f);
		Squad.ResetFormationState(Forward);
		UE_LOG(LogCodexTactics, Display, TEXT("[CombatLabAmbush] squad placed at %s facing %.0f"), *Lead.ToString(), Yaw);
	}

	/** A top-down still over the whole plan: the middle of the leader and every living plan member, far enough to hold them all. */
	void QueuePlanPhoto(FState& State, const TCHAR* Name, const AOperativeCharacter& Leader, float HeadingYawDeg);

	void QueuePhoto(FState& State, const TCHAR* Name, const FVector& Target, const FRotator& View, float Distance)
	{
		if (!State.bShot)
		{
			return;
		}
		FPhoto& Photo = State.Photos.AddDefaulted_GetRef();
		Photo.Name = Name;
		Photo.Target = Target;
		Photo.View = View;
		Photo.Distance = Distance;
	}

	void DrawPlanMarkers(UWorld* World, FState& State);

	/** Rendered stills ("shot"): time almost stopped, every enemy revealed, a camera on the target. True while a photo is pending. */
	bool StepPhotos(UWorld* World, FState& State)
	{
		if (State.Photos.IsEmpty())
		{
			return false;
		}
		APlayerController* PC = World->GetFirstPlayerController();
		UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>();
		FPhoto& Photo = State.Photos[0];
		if (!PC)
		{
			State.Photos.RemoveAt(0);
			return true;
		}
		if (Photo.Frames == 0)
		{
			UGameplayStatics::SetGlobalTimeDilation(World, 0.01f);
			if (AHUD* Hud = PC->GetHUD())
			{
				Hud->bShowHUD = false;
			}
			GEngine->Exec(World, TEXT("DisableAllScreenMessages"));
			if (Sight)
			{
				Sight->SetCinematicReveal(true);
			}
			if (!State.Camera.IsValid())
			{
				FActorSpawnParameters Params;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				State.Camera = World->SpawnActor<ACameraActor>(Photo.Target, FRotator::ZeroRotator, Params);
			}
		}
		if (ACameraActor* Camera = State.Camera.Get())
		{
			Camera->GetCameraComponent()->SetFieldOfView(60.f);
			Camera->GetCameraComponent()->bConstrainAspectRatio = false;
			Camera->SetActorLocationAndRotation(Photo.Target - Photo.View.Vector() * Photo.Distance, Photo.View);
			PC->SetViewTarget(Camera);
		}
		DrawPlanMarkers(World, State);
		++Photo.Frames;
		const bool bShadersDone = !GShaderCompilingManager || GShaderCompilingManager->GetNumRemainingJobs() == 0;
		if (!Photo.bTaken && Photo.Frames >= 30 && (bShadersDone || Photo.Frames >= 900))
		{
			Photo.bTaken = true;
			Photo.TakenFrame = Photo.Frames;
			const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("CombatLabV2") / (Photo.Name + TEXT(".png"));
			FScreenshotRequest::RequestScreenshot(Path, false, false);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke screenshot %s"), *Path);
		}
		if (Photo.bTaken && Photo.Frames >= Photo.TakenFrame + 15)
		{
			State.Photos.RemoveAt(0);
			if (State.Photos.IsEmpty())
			{
				UGameplayStatics::SetGlobalTimeDilation(World, 1.f);
				if (AHUD* Hud = PC->GetHUD())
				{
					Hud->bShowHUD = true;
				}
				if (Sight)
				{
					Sight->SetCinematicReveal(false);
				}
				PC->SetViewTarget(PC->GetPawn());
			}
		}
		return true;
	}

	EPressureAmbushState StateOf(UCombatPressureSubsystem& Pressure, const AEnemyCharacter* Enemy);

	/** The stills' plan markers (debug lines, this frame): party rings under the members, suppressor -> target lines. */
	void DrawPlanMarkers(UWorld* World, FState& State)
	{
		UCombatPressureSubsystem* Pressure = World->GetSubsystem<UCombatPressureSubsystem>();
		const USuppressionSubsystem* Suppression = World->GetSubsystem<USuppressionSubsystem>();
		if (!Pressure)
		{
			return;
		}
		for (const TWeakObjectPtr<AEnemyCharacter>& Each : State.PlanMembers)
		{
			const AEnemyCharacter* Enemy = Each.Get();
			int32 Group = 0;
			int32 Plan = 0;
			EAmbushParty Party = EAmbushParty::Single;
			bool bSuppressor = false;
			if (!Enemy || Enemy->IsDying() || !Pressure->GetMemberInfo(Enemy, Group, Plan, Party, bSuppressor))
			{
				continue;
			}
			const FColor Colour = AmbushPlanRules::IsFlank(Party) ? FColor(255, 40, 40) : Party == EAmbushParty::Front ? FColor(255, 220, 0) : FColor(60, 140, 255);
			const bool bSprung = StateOf(*Pressure, Enemy) == EPressureAmbushState::Sprung;
			const FVector Feet = FeetOf(*Enemy) + FVector(0.f, 0.f, 8.f);
			DrawDebugCircle(World, Feet, 170.f, 32, Colour, false, 0.02f, SDPG_Foreground, bSprung ? 22.f : 9.f, FVector(1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), false);
			const AActor* Intent = Suppression ? Suppression->GetSuppressIntent(Enemy) : nullptr;
			if (bSuppressor && Intent)
			{
				DrawDebugLine(World, Enemy->GetActorLocation() + FVector(0.f, 0.f, 40.f), Intent->GetActorLocation() + FVector(0.f, 0.f, 40.f), FColor::Magenta, false,
					0.02f, SDPG_Foreground, 8.f);
			}
		}
	}

	void QueuePlanPhoto(FState& State, const TCHAR* Name, const AOperativeCharacter& Leader, float HeadingYawDeg)
	{
		FBox Bounds(ForceInit);
		Bounds += FeetOf(Leader);
		for (const TWeakObjectPtr<AEnemyCharacter>& Each : State.PlanMembers)
		{
			if (const AEnemyCharacter* Enemy = Each.Get())
			{
				Bounds += FeetOf(*Enemy);
			}
		}
		// The heading up the frame: the depth (along the heading) fits the 16:9 frame's height, the width its width (FOV 60).
		const FVector Extent = Bounds.GetExtent();
		const float Rad = FMath::DegreesToRadians(HeadingYawDeg);
		const float Along = FMath::Abs(Extent.X * FMath::Cos(Rad)) + FMath::Abs(Extent.Y * FMath::Sin(Rad));
		const float Across = FMath::Abs(Extent.X * FMath::Sin(Rad)) + FMath::Abs(Extent.Y * FMath::Cos(Rad));
		const float Distance = FMath::Clamp(FMath::Max((Along + 500.f) / FMath::Tan(FMath::DegreesToRadians(17.f)), (Across + 500.f) / FMath::Tan(FMath::DegreesToRadians(29.f))),
			2500.f, 9000.f);
		QueuePhoto(State, Name, Bounds.GetCenter(), FRotator(-84.f, HeadingYawDeg, 0.f), Distance);
	}

	/** The plan member's state (unknown when it is gone). */
	EPressureAmbushState StateOf(UCombatPressureSubsystem& Pressure, const AEnemyCharacter* Enemy)
	{
		EPressureAmbushState Out = EPressureAmbushState::Sprung;
		float Held = 0.f;
		return Enemy && Pressure.GetAmbushState(Enemy, Out, Held) ? Out : EPressureAmbushState::Sprung;
	}

	bool Step(UWorld* World, FState& State, float Dt)
	{
		if (StepPhotos(World, State))
		{
			return true;
		}
		State.Time += Dt;
		State.StageTime += Dt;
		if (State.Time > Timeout)
		{
			Check(State, false, FString::Printf(TEXT("timeout in stage %d"), static_cast<int32>(State.Stage)));
			return Finish(State, false);
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		ULevelEncounterSubsystem* Encounter = World->GetSubsystem<ULevelEncounterSubsystem>();
		UCombatPressureSubsystem* Pressure = World->GetSubsystem<UCombatPressureSubsystem>();
		USuppressionSubsystem* Suppression = World->GetSubsystem<USuppressionSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Squad || !Flow || !Encounter || !Pressure || !Suppression || !Leader)
		{
			Check(State, false, TEXT("squad, game flow, encounter, pressure, suppression subsystems and a leader"));
			return Finish(State, false);
		}
		const FCombatPressureConfig& Config = Pressure->GetConfig();

		switch (State.Stage)
		{
		case EStage::FirstEvent:
		{
			// Answer 6: the level's own timer, untouched from the level start.
			if (Pressure->GetFirstEventClock() < 0.f && Pressure->GetClock() < 45.f)
			{
				return true;
			}
			Check(State, Config.FirstEventSeconds == 30.f, FString::Printf(TEXT("the level's first event at %.0f s (answer 6: 30 s)"), Config.FirstEventSeconds));
			Check(State, Pressure->GetFirstEventClock() >= 30.f && Pressure->GetFirstEventClock() < 30.6f && Pressure->GetEventsFired() == 1,
				FString::Printf(TEXT("the first group came at %.2f s of the pressure clock (events %d, next in %.0f s)"), Pressure->GetFirstEventClock(),
					Pressure->GetEventsFired(), Pressure->GetSecondsToNextEvent()));
			Check(State, Pressure->GetSecondsToNextEvent() > 110.f, FString::Printf(TEXT("then every 2 minutes (next in %.0f s)"), Pressure->GetSecondsToNextEvent()));
			Pressure->SetAutoSpawn(false);
			Pressure->DespawnAll();
			Squad->SetSquadPosture(ESquadFirePosture::Passive);
			Enter(State, EStage::FirstSettle);
			return true;
		}

		case EStage::FirstSettle:
		{
			if ((Flow->GetPhase() != ECodexGamePhase::Exploration || State.StageTime < 1.f) && State.StageTime < 25.f)
			{
				return true;
			}
			Check(State, Flow->GetPhase() == ECodexGamePhase::Exploration, TEXT("exploring after the first group"));
			// The hiding-spot candidates (answer 1).
			int32 Counts[4] = { 0, 0, 0, 0 };
			for (const FAmbushSpot& Spot : Pressure->GetHideSpots())
			{
				++Counts[FMath::Clamp(static_cast<int32>(Spot.Kind), 0, 3)];
			}
			const int32 Ends = Counts[static_cast<int32>(EAmbushSpotKind::WallEnd)];
			const int32 Doorways = Counts[static_cast<int32>(EAmbushSpotKind::Doorway)];
			const int32 Authored = Counts[static_cast<int32>(EAmbushSpotKind::Authored)];
			Check(State, Ends >= 40 && Doorways >= 16 && Authored == Config.AuthoredPoints.Num() && Authored == 8,
				FString::Printf(TEXT("hiding spots read from the level's walls: %d wall ends, %d doorways, %d authored (level JSON %d)"), Ends, Doorways, Authored,
					Config.AuthoredPoints.Num()));
			// Decision 3 (2026-11-01 night): the ambushers hug the wall - every wall-end / doorway spot 0.8 m off its wall's face.
			const TArray<FPressureBox>& Boxes = Pressure->GetObstacleBoxes();
			int32 OffStandoff = 0;
			float Farthest = 0.f;
			for (const FAmbushSpot& Spot : Pressure->GetHideSpots())
			{
				if (Spot.Kind != EAmbushSpotKind::Authored && Boxes.IsValidIndex(Spot.Box))
				{
					const float Off = CombatPressureRules::DistanceToBox(Spot.Position, Boxes[Spot.Box]);
					Farthest = FMath::Max(Farthest, Off);
					OffStandoff += FMath::Abs(Off - Config.Spots.WallStandoffCm) > 1.f ? 1 : 0;
				}
			}
			Check(State, Config.Spots.WallStandoffCm == 80.f && OffStandoff == 0 && Farthest <= 81.f,
				FString::Printf(TEXT("every wall-end / doorway spot hugs its wall: standoff %.0f cm (decision 3: 80), farthest %.0f cm, %d off"), Config.Spots.WallStandoffCm,
					Farthest, OffStandoff));
			Enter(State, EStage::SpotsPlace);
			return true;
		}

		case EStage::SpotsPlace:
		{
			const FPlacement& Placement = Placements[State.Placement];
			PlaceSquad(World, *Squad, Placement.Lead, Placement.Yaw);
			Enter(State, EStage::SpotsSpawn);
			return true;
		}

		case EStage::SpotsSpawn:
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			const FPlacement& Placement = Placements[State.Placement];
			const int32 Before = Pressure->GetSpawnRecords().Num();
			const TArray<AEnemyCharacter*> Spawned = Pressure->TriggerEvent(EPressureSpawnKind::Ambush, 1, true);
			const TArray<FPressureSpawnRecord>& Records = Pressure->GetSpawnRecords();
			const FPressureSpawnRecord* Record = Records.Num() > Before ? &Records[Before] : nullptr;
			if (!Record || Spawned.IsEmpty())
			{
				Check(State, false, FString::Printf(TEXT("placement %d: a single ambush spawned"), State.Placement));
				return Finish(State, false);
			}
			const float Clearance = AmbushSpotRules::HideClearanceCm(Config.WallClearanceCm, Config.Spots);
			FString Why;
			const bool bRays = RaysClear(World, Record->Location, Clearance - 15.f, Why);
			const bool bAtCandidate = Pressure->GetHideSpots().ContainsByPredicate([Record](const FAmbushSpot& Spot)
			{
				return FVector2D::Distance(Spot.Position, FVector2D(Record->Location)) <= 60.0;
			});
			const float Distance = static_cast<float>(FVector::Dist2D(Record->Location, FeetOf(*Leader)));
			State.KindsSeen.Add(Record->SpotKind);
			// Decision 3: at a wall spot the ambusher stands ~0.8 m off the face (the navmesh projection may shift it a little).
			const bool bHugs = Record->SpotKind == EAmbushSpotKind::Authored || Record->ClearanceCm <= Config.Spots.WallStandoffCm + 40.f;
			Check(State, bHugs, FString::Printf(TEXT("placement %d: the %s ambusher hugs its wall (%.0f cm off it, ~%.0f)"), State.Placement,
				AmbushSpotRules::KindName(Record->SpotKind), Record->ClearanceCm, Config.Spots.WallStandoffCm));
			Check(State, Record->Kind == EPressureSpawnKind::Ambush && Record->bConcealed && bAtCandidate && bRays && Record->ClearanceCm >= Clearance - 1.f
				&& Distance >= Config.AmbushMinDistanceCm - 50.f && Distance <= Config.AmbushMaxDistanceCm + 50.f,
				FString::Printf(TEXT("placement %d (%s): the ambusher hides at a %s spot %s - concealed %d, a candidate %d, clearance %.0f cm, rays %s, %.0f m from the leader"),
					State.Placement, *Placement.Lead.ToString(), AmbushSpotRules::KindName(Record->SpotKind), *Record->Location.ToCompactString(),
					Record->bConcealed ? 1 : 0, bAtCandidate ? 1 : 0, Record->ClearanceCm, bRays ? TEXT("clear") : *Why, Distance / 100.f));
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: placement %d expected a %s spot, got a %s spot"), State.Placement,
				AmbushSpotRules::KindName(Placement.Expect), AmbushSpotRules::KindName(Record->SpotKind));
			Pressure->DespawnAll();
			if (++State.Placement < static_cast<int32>(UE_ARRAY_COUNT(Placements)))
			{
				Enter(State, EStage::SpotsPlace);
				return true;
			}
			Check(State, State.KindsSeen.Contains(EAmbushSpotKind::Authored) && State.KindsSeen.Contains(EAmbushSpotKind::Doorway)
				&& State.KindsSeen.Contains(EAmbushSpotKind::WallEnd), TEXT("the selector chose an authored point, a doorway and a wall end"));
			Enter(State, EStage::PlanPlace);
			return true;
		}

		case EStage::PlanPlace:
		{
			if (!State.bPlaced)
			{
				State.bPlaced = true;
				PlaceSquad(World, *Squad, FVector2D(-1500.0, -2000.0), 90.f);
				return true;
			}
			if (State.StageTime < 1.2f)
			{
				return true;
			}
			// Watched all round first (the hold), the plans' own types.
			Pressure->SetWatchHalfAngleOverride(180.f);
			const TArray<AEnemyCharacter*> Spawned = Pressure->TriggerCoordinated({ EEnemyArchetype::FrostHound, EEnemyArchetype::Frostbitten,
				EEnemyArchetype::Cutter, EEnemyArchetype::FrostHound, EEnemyArchetype::Marksman, EEnemyArchetype::Spitter }, false);
			FPressurePlanInfo Info;
			if (!Pressure->GetPlanInfo(Pressure->GetLastPlanId(), Info) || Spawned.Num() < 4)
			{
				Check(State, false, FString::Printf(TEXT("a coordinated plan round the squad (%d spawned)"), Spawned.Num()));
				return Finish(State, false);
			}
			State.PlanId = Info.Id;
			for (AEnemyCharacter* Enemy : Spawned)
			{
				State.PlanMembers.Add(Enemy);
			}
			Check(State, Spawned.Num() == 6 && Pressure->GetAliveCount() == 6 && Pressure->GetAliveCount() > Config.MaxAlive
				&& Pressure->GetAliveCount() <= Config.Coordinated.MaxAlive,
				FString::Printf(TEXT("plan %d: 6 bodies, %d alive - above max_alive %d, within the plans' own cap %d"), Info.Id, Pressure->GetAliveCount(),
					Config.MaxAlive, Config.Coordinated.MaxAlive));
			for (int32 Slot = 0; Slot < 4; ++Slot)
			{
				const EAmbushParty Party = AmbushPlanRules::PlanParty(Slot);
				const EAmbushParty Sector = AmbushPlanRules::SectorOf(Info.Anchor, Info.HeadingYawDeg, FVector2D(Info.PartySpots[Slot]));
				Check(State, Sector == Party && Info.PartyKinds[Slot] != EAmbushSpotKind::None,
					FString::Printf(TEXT("party %s hides at a %s spot %s in its own sector (%s)"), AmbushPlanRules::PartyName(Party),
						AmbushSpotRules::KindName(Info.PartyKinds[Slot]), *Info.PartySpots[Slot].ToCompactString(), AmbushPlanRules::PartyName(Sector)));
			}
			int32 Concealed = 0;
			int32 Members = 0;
			for (const FPressureSpawnRecord& Record : Pressure->GetSpawnRecords())
			{
				if (Record.Plan == Info.Id)
				{
					++Members;
					Concealed += Record.bConcealed ? 1 : 0;
				}
			}
			Check(State, Members == 6 && Concealed == Members, FString::Printf(TEXT("every plan member out of the squad's sight (%d of %d)"), Concealed, Members));
			QueuePlanPhoto(State, TEXT("01_coordinated_plan"), *Leader, Info.HeadingYawDeg);
			Enter(State, EStage::PlanHold);
			return true;
		}

		case EStage::PlanHold:
		{
			FPressurePlanInfo Info;
			Pressure->GetPlanInfo(State.PlanId, Info);
			if (State.StageTime < 2.6f)
			{
				return true;
			}
			int32 Sprung = 0;
			for (const TWeakObjectPtr<AEnemyCharacter>& Each : State.PlanMembers)
			{
				Sprung += StateOf(*Pressure, Each.Get()) == EPressureAmbushState::Sprung ? 1 : 0;
			}
			Check(State, Info.Phase == EAmbushPlanPhase::Holding && Info.HoldSeconds >= 2.f && Sprung == 0 && Flow->GetPhase() == ECodexGamePhase::Exploration,
				FString::Printf(TEXT("watched all round, the plan HOLDS (%s, %.1f s held, %d sprung, phase %s)"), AmbushPlanRules::PhaseName(Info.Phase),
					Info.HoldSeconds, Sprung, *UEnum::GetValueAsString(Flow->GetPhase())));
			// The squad's looks narrowed to nothing: both flank sides unwatched.
			Pressure->SetWatchHalfAngleOverride(1.f);
			State.DeliberateBefore = Suppression->GetStats().DeliberateRounds;
			Enter(State, EStage::PlanFlank);
			return true;
		}

		case EStage::PlanFlank:
		{
			FPressurePlanInfo Info;
			Pressure->GetPlanInfo(State.PlanId, Info);
			if (Info.Phase != EAmbushPlanPhase::FlankBlock && Info.Phase != EAmbushPlanPhase::Assault && State.StageTime < 1.f)
			{
				return true;
			}
			Check(State, Info.Phase == EAmbushPlanPhase::FlankBlock && Info.FlankAtClock >= 0.f,
				FString::Printf(TEXT("the flanks unwatched: the FLANK BLOCK begins (%s after %.2f s)"), AmbushPlanRules::PhaseName(Info.Phase), State.StageTime));
			int32 FlankSprung = 0;
			int32 Suppressors = 0;
			int32 MeleeWaiting = 0;
			for (const TWeakObjectPtr<AEnemyCharacter>& Each : State.PlanMembers)
			{
				AEnemyCharacter* Enemy = Each.Get();
				int32 Group = 0;
				int32 Plan = 0;
				EAmbushParty Party = EAmbushParty::Single;
				bool bSuppressor = false;
				if (!Enemy || !Pressure->GetMemberInfo(Enemy, Group, Plan, Party, bSuppressor))
				{
					continue;
				}
				const bool bSprung = StateOf(*Pressure, Enemy) == EPressureAmbushState::Sprung;
				if (AmbushPlanRules::IsFlank(Party))
				{
					FlankSprung += bSprung ? 1 : 0;
				}
				else if (AmbushPlanRules::IsRanged(Enemy->GetArchetype()))
				{
					const AActor* Intent = Suppression->GetSuppressIntent(Enemy);
					Suppressors += bSprung && bSuppressor && Intent && Intent->IsA<AOperativeCharacter>() ? 1 : 0;
				}
				else
				{
					MeleeWaiting += !bSprung ? 1 : 0;
				}
			}
			Check(State, FlankSprung == 2, FString::Printf(TEXT("both flank parties strike first (%d of 2)"), FlankSprung));
			Check(State, Suppressors == 2, FString::Printf(TEXT("the marksman and the spitter open suppressive fire with them (intent on an operative: %d of 2)"), Suppressors));
			Check(State, MeleeWaiting == 2, FString::Printf(TEXT("the melee front (cutter) and rear (hound) still wait hidden (%d of 2)"), MeleeWaiting));
			Enter(State, EStage::PlanAssault);
			return true;
		}

		case EStage::PlanAssault:
		{
			FPressurePlanInfo Info;
			Pressure->GetPlanInfo(State.PlanId, Info);
			if (Info.Phase == EAmbushPlanPhase::FlankBlock)
			{
				State.DeliberateInFlankBlock = Suppression->GetStats().DeliberateRounds - State.DeliberateBefore;
				for (const TWeakObjectPtr<AEnemyCharacter>& Each : State.PlanMembers)
				{
					int32 Group = 0;
					int32 Plan = 0;
					EAmbushParty Party = EAmbushParty::Single;
					bool bSuppressor = false;
					const AEnemyCharacter* Enemy = Each.Get();
					if (Enemy && Pressure->GetMemberInfo(Enemy, Group, Plan, Party, bSuppressor) && !AmbushPlanRules::IsFlank(Party)
						&& !AmbushPlanRules::IsRanged(Enemy->GetArchetype()))
					{
						State.bMeleeSprungEarly |= StateOf(*Pressure, Enemy) == EPressureAmbushState::Sprung;
					}
				}
				if (!State.bFlankPhotoQueued && State.StageTime >= 1.f)
				{
					State.bFlankPhotoQueued = true;
					QueuePlanPhoto(State, TEXT("02_flank_block"), *Leader, Info.HeadingYawDeg);
				}
				if (State.StageTime < 8.f)
				{
					return true;
				}
			}
			const float Gap = Info.AssaultAtClock - Info.FlankAtClock;
			Check(State, Info.Phase == EAmbushPlanPhase::Assault && Gap >= Config.Coordinated.AssaultMinSeconds - 0.15f
				&& Gap <= Config.Coordinated.AssaultMaxSeconds + 0.25f && !State.bMeleeSprungEarly,
				FString::Printf(TEXT("then the ASSAULT: rear + front strike %.2f s after the flanks (%.1f..%.1f s; none of them early: %d)"), Gap,
					Config.Coordinated.AssaultMinSeconds, Config.Coordinated.AssaultMaxSeconds, State.bMeleeSprungEarly ? 0 : 1));
			int32 Sprung = 0;
			for (const TWeakObjectPtr<AEnemyCharacter>& Each : State.PlanMembers)
			{
				Sprung += !Each.IsValid() || StateOf(*Pressure, Each.Get()) == EPressureAmbushState::Sprung ? 1 : 0;
			}
			Check(State, Sprung == State.PlanMembers.Num(), FString::Printf(TEXT("every party strikes now (%d of %d)"), Sprung, State.PlanMembers.Num()));
			Check(State, Flow->GetPhase() == ECodexGamePhase::WaveCombat && Flow->IsSkirmish(), TEXT("the ambush started the real-time skirmish"));
			Enter(State, EStage::PlanSuppress);
			return true;
		}

		case EStage::PlanSuppress:
		{
			for (const AOperativeCharacter* Member : Squad->GetMembers())
			{
				State.MaxOperativeLevel = FMath::Max(State.MaxOperativeLevel, USuppressionSubsystem::GetLevelOf(Member));
			}
			const int32 Deliberate = Suppression->GetStats().DeliberateRounds - State.DeliberateBefore;
			if ((Deliberate < 1 || State.MaxOperativeLevel <= 0.f) && State.StageTime < 45.f)
			{
				return true;
			}
			Check(State, Deliberate >= 1 && State.MaxOperativeLevel > 0.f,
				FString::Printf(TEXT("a ranged ambusher SUPPRESSES the squad: %d deliberate rounds, an operative's suppression level up to %.2f (%.1f s after the assault)"),
					Deliberate, State.MaxOperativeLevel, State.StageTime));
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: %d of the deliberate rounds came during the flank block (before the rear / front struck)"),
				State.DeliberateInFlankBlock);
			// The suppressor that fired, for the still.
			for (const TWeakObjectPtr<AEnemyCharacter>& Each : State.PlanMembers)
			{
				const AEnemyCharacter* Enemy = Each.Get();
				const AActor* Intent = Enemy ? Suppression->GetSuppressIntent(Enemy) : nullptr;
				if (Enemy && !Enemy->IsDying() && Intent)
				{
					// From behind the suppressor, over his shoulder towards the squad.
					const FVector Toward = (Intent->GetActorLocation() - Enemy->GetActorLocation()).GetSafeNormal2D();
					const float Yaw = static_cast<float>(Toward.Rotation().Yaw);
					const float Span = static_cast<float>(FVector::Dist2D(Intent->GetActorLocation(), Enemy->GetActorLocation()));
					QueuePhoto(State, TEXT("03_suppressor"), Enemy->GetActorLocation() + Toward * (Span * 0.5f), FRotator(-48.f, Yaw - 20.f, 0.f), 900.f + Span * 1.0f);
					break;
				}
			}
			Enter(State, EStage::PlanSettle);
			return true;
		}

		case EStage::PlanSettle:
		{
			if (!State.bPlaced)
			{
				// After the suppressor's still (photos run before the stage logic): the plan is cleared, the fight ends.
				State.bPlaced = true;
				Pressure->SetWatchHalfAngleOverride(-1.f);
				Squad->SetSquadPosture(ESquadFirePosture::Aggressive);
				Pressure->DespawnAll();
				return true;
			}
			if ((Flow->GetPhase() != ECodexGamePhase::Exploration || State.StageTime < 1.f) && State.StageTime < 25.f)
			{
				return true;
			}
			Check(State, Flow->GetPhase() == ECodexGamePhase::Exploration, TEXT("the plan's skirmish over: exploring"));
			Squad->SetSquadPosture(ESquadFirePosture::Passive);
			Enter(State, EStage::RelocPlace);
			return true;
		}

		case EStage::RelocPlace:
		{
			// The squad has walked west past an ambush hidden NORTH of the 43 m wall W_E_13 (y 2500): it is behind them now (bypassed). A concealed
			// route exists along the wall's north side to the wall-end spot ahead of them (the decision 1 run; no line of sight over a 3 m wall).
			if (!State.bPlaced)
			{
				State.bPlaced = true;
				PlaceSquad(World, *Squad, FVector2D(-2500.0, 1800.0), 180.f);
				return true;
			}
			if (State.StageTime < 1.2f)
			{
				return true;
			}
			const TOptional<FVector> Hide = GroundNav(World, FVector(-500.f, 2900.f, 0.f));
			const TArray<AEnemyCharacter*> Spawned = Hide.IsSet() ? Pressure->SpawnAmbushGroupAt(Hide.GetValue(), { EEnemyArchetype::Frostbitten },
				Hide.GetValue() - FVector(0.f, 500.f, 0.f)) : TArray<AEnemyCharacter*>();
			State.Relocator = Spawned.IsEmpty() ? nullptr : Spawned[0];
			Check(State, State.Relocator.IsValid() && !Pressure->IsVisibleToSquad(FeetOf(*State.Relocator)),
				TEXT("a single ambush north of W_E_13, behind the squad, out of its sight"));
			if (!State.Relocator.IsValid())
			{
				return Finish(State, false);
			}
			State.RelocationsBefore = Pressure->GetRelocations().Num();
			State.RecordsBefore = Pressure->GetSpawnRecords().Num();
			State.LeftAtClock = Pressure->GetClock();
			Enter(State, EStage::RelocWait);
			return true;
		}

		case EStage::RelocWait:
		{
			AEnemyCharacter* Ambusher = State.Relocator.Get();
			const bool bStarted = Pressure->GetRelocations().Num() > State.RelocationsBefore;
			if (!bStarted && Ambusher && State.StageTime < Config.Relocate.AfterSeconds + 12.f)
			{
				return true;
			}
			if (!bStarted || !Ambusher)
			{
				Check(State, false, FString::Printf(TEXT("the bypassed ambush sets off along a concealed route (started %d, still there %d, %.1f s)"), bStarted ? 1 : 0,
					Ambusher ? 1 : 0, State.StageTime));
				return Finish(State, false);
			}
			State.RunRecord = Pressure->GetRelocations().Num() - 1;
			const FPressureRelocationRecord& Move = Pressure->GetRelocations()[State.RunRecord];
			const float Waited = Move.Clock - State.LeftAtClock;
			Check(State, Waited >= Config.Relocate.AfterSeconds - 0.25f && Waited <= Config.Relocate.AfterSeconds + 3.f,
				FString::Printf(TEXT("left behind, it sets off after %.1f s (relocate.after_sec %.0f)"), Waited, Config.Relocate.AfterSeconds));
			Check(State, !Move.bSeenAtMove && Move.bDestinationConcealed && Move.SeenSamples == 0 && Move.Samples >= 5 && Move.PathCm > 500.f
				&& Move.Path.Num() >= 2 && FVector::Dist2D(Move.From, FeetOf(*Ambusher)) <= 60.f,
				FString::Printf(TEXT("a CONCEALED ROUTE on foot (decision 1): %.0f m, %d sight samples, none seen (%d), unseen at the start (%d), the new spot out of sight (%d), %d routes tried"),
					Move.PathCm / 100.f, Move.Samples, Move.SeenSamples, Move.bSeenAtMove ? 0 : 1, Move.bDestinationConcealed ? 1 : 0, Move.RoutesTried));
			EPressureAmbushState AmbushState = EPressureAmbushState::Hidden;
			float Held = 0.f;
			Check(State, Pressure->GetAmbushState(Ambusher, AmbushState, Held) && AmbushState == EPressureAmbushState::Relocating,
				FString::Printf(TEXT("the ambusher is running (state %d)"), static_cast<int32>(AmbushState)));
			State.LastFeet = FeetOf(*Ambusher);
			State.MaxFrameStepCm = 0.f;
			State.bSeenOnRun = false;
			Enter(State, EStage::RelocRun);
			return true;
		}

		case EStage::RelocRun:
		{
			AEnemyCharacter* Ambusher = State.Relocator.Get();
			if (!Ambusher || !Pressure->GetRelocations().IsValidIndex(State.RunRecord))
			{
				Check(State, false, TEXT("the runner is there"));
				return Finish(State, false);
			}
			const FPressureRelocationRecord& Move = Pressure->GetRelocations()[State.RunRecord];
			const FVector Feet = FeetOf(*Ambusher);
			State.MaxFrameStepCm = FMath::Max(State.MaxFrameStepCm, static_cast<float>(FVector::Dist2D(Feet, State.LastFeet)));
			State.LastFeet = Feet;
			State.bSeenOnRun |= Pressure->IsSpottedBySquad(*Ambusher);
			if (Move.Outcome == EAmbushRunStep::Running && State.StageTime < 60.f)
			{
				return true;
			}
			const float Distance = static_cast<float>(FVector::Dist2D(Feet, FeetOf(*Leader)));
			Check(State, Move.Outcome == EAmbushRunStep::Arrived && !State.bSeenOnRun,
				FString::Printf(TEXT("it RAN there unseen: %s after %.1f s, an operative saw it on the way %d"), AmbushRelocateRules::RunStepName(Move.Outcome),
					Move.EndClock - Move.Clock, State.bSeenOnRun ? 1 : 0));
			Check(State, State.MaxFrameStepCm <= 30.f && Move.MaxStepCm <= 100.f,
				FString::Printf(TEXT("on foot, no teleport: largest step %.1f cm a frame (%.0f cm per 0.1 s check)"), State.MaxFrameStepCm, Move.MaxStepCm));
			Check(State, Move.AheadCos >= Config.Relocate.MinAheadCos && Distance >= Config.AmbushMinDistanceCm - 250.f && Distance <= Config.AmbushMaxDistanceCm + 250.f
				&& Move.Kind != EAmbushSpotKind::None && FVector::Dist2D(Feet, Move.To) <= 150.f && !Pressure->IsVisibleToSquad(Feet),
				FString::Printf(TEXT("AHEAD of the squad again: %s -> %s, a %s spot, ahead %.2f, %.0f m from the leader, hidden"), *Move.From.ToCompactString(),
					*Feet.ToCompactString(), AmbushSpotRules::KindName(Move.Kind), Move.AheadCos, Distance / 100.f));
			Check(State, StateOf(*Pressure, Ambusher) != EPressureAmbushState::Sprung && Ambusher->IsOnPatrol() && Ambusher->IsGuardingPost()
				&& Pressure->GetSpawnRecords().Num() == State.RecordsBefore && Flow->GetPhase() == ECodexGamePhase::Exploration,
				TEXT("the same enemy, still hidden on its new post - not despawned, nothing new spawned, no fight"));
			Pressure->DespawnAll();
			Enter(State, EStage::AimPlace);
			return true;
		}

		case EStage::AimPlace:
		{
			if (!State.bPlaced)
			{
				State.bPlaced = true;
				PlaceSquad(World, *Squad, Placements[2].Lead, Placements[2].Yaw);
				return true;
			}
			if (State.StageTime < 1.2f)
			{
				return true;
			}
			// A decoy - ANOTHER enemy - 10 m east, frozen and tough; the leader fires at it in a skirmish.
			UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
			const TOptional<FVector> DecoyFeet = GroundNav(World, FeetOf(*Leader) + FVector(1000.f, 0.f, 0.f));
			AEnemyCharacter* Decoy = Waves && DecoyFeet.IsSet()
				? Waves->SpawnLevelEnemy(EEnemyArchetype::Frostbitten, DecoyFeet.GetValue() + FVector(0.f, 0.f, 95.f), FRotator(0.f, 180.f, 0.f)) : nullptr;
			if (!Decoy)
			{
				Check(State, false, TEXT("a decoy enemy to fight"));
				return Finish(State, false);
			}
			Encounter->MarkLevelEnemy(Decoy);
			Decoy->CustomTimeDilation = 0.f;
			if (UHealthComponent* Health = Decoy->GetHealthComponent())
			{
				Health->SetMaxHealth(1000000.f, true);
			}
			State.Decoy = Decoy;
			State.Aimer = Leader;
			Leader->SetManualPriorityTarget(Decoy);
			ULevelEncounterSubsystem::NotifyHostileContactIn(World, EAmbushTrigger::SquadAutoFire, Decoy);
			Enter(State, EStage::AimWait);
			return true;
		}

		case EStage::AimWait:
		{
			AOperativeCharacter* Aimer = State.Aimer.Get();
			AEnemyCharacter* Decoy = State.Decoy.Get();
			FVector AimPoint;
			const bool bAiming = Aimer && Decoy && Aimer->GetAimTargetPoint(AimPoint) && FVector::Dist2D(AimPoint, Decoy->GetActorLocation()) <= 200.f;
			if (!bAiming && State.StageTime < 10.f)
			{
				return true;
			}
			Check(State, bAiming && Flow->GetPhase() == ECodexGamePhase::WaveCombat,
				FString::Printf(TEXT("the leader fights another enemy: aims at the decoy (%d) in the skirmish (%s)"), bAiming ? 1 : 0, *UEnum::GetValueAsString(Flow->GetPhase())));
			if (!bAiming)
			{
				return Finish(State, false);
			}
			// A spot in his aim cone (+-45 deg) 10-14 m out; preferably one only his aim covers (nobody else's look, not his body's).
			const FVector2D Here(Aimer->GetActorLocation());
			const float AimYaw = AmbushWatchRules::BearingDeg(Here, FVector2D(AimPoint));
			const TArray<FAmbushWatcher> Watchers = Pressure->GatherWatchers();
			TArray<FAmbushWatcher> His;
			TArray<FAmbushWatcher> Others;
			for (const FAmbushWatcher& Look : Watchers)
			{
				(FVector2D::Distance(Look.Position, Here) < 1.0 ? His : Others).Add(Look);
			}
			TArray<FAmbushWatcher> HisWithoutAim = His;
			HisWithoutAim.RemoveAll([](const FAmbushWatcher& Look) { return Look.Source == EAmbushLookSource::Aim; });
			TOptional<FVector> Spot;
			for (int32 Pass = 0; Pass < 3 && !Spot.IsSet(); ++Pass)
			{
				for (const float Distance : { 1200.f, 1000.f, 1400.f })
				{
					for (const float Turn : { 25.f, -25.f, 35.f, -35.f, 15.f, -15.f, 40.f, -40.f, 0.f })
					{
						const TOptional<FVector> Candidate = GroundNav(World, FeetOf(*Aimer) + FRotator(0.f, AimYaw + Turn, 0.f).Vector() * Distance);
						if (!Candidate.IsSet())
						{
							continue;
						}
						const FVector2D Point(Candidate.GetValue());
						const bool bHis = AmbushWatchRules::IsWatched(His, Point, Config.Watch);
						const bool bOthers = AmbushWatchRules::IsWatched(Others, Point, Config.Watch);
						const bool bHisBody = AmbushWatchRules::IsWatched(HisWithoutAim, Point, Config.Watch);
						const bool bOk = bHis && AmbushWatchRules::NearestDistance(Pressure->GatherSquadPositions(), Point) < Config.Watch.StrikeRangeCm - 200.f
							&& (Pass > 1 || !bOthers) && (Pass > 0 || !bHisBody);
						if (bOk && FVector2D::Distance(Point, FVector2D(Decoy->GetActorLocation())) > 300.0)
						{
							Spot = Candidate;
							State.bAimDiscriminating = !bOthers && !bHisBody;
							break;
						}
					}
					if (Spot.IsSet())
					{
						break;
					}
				}
			}
			Check(State, Spot.IsSet(), FString::Printf(TEXT("a spot inside the leader's aim cone (aim %.0f deg, his looks %d, only his aim covers it: %d)"), AimYaw, His.Num(),
				State.bAimDiscriminating ? 1 : 0));
			if (!Spot.IsSet())
			{
				return Finish(State, false);
			}
			const TArray<AEnemyCharacter*> Group = Pressure->SpawnAmbushGroupAt(Spot.GetValue(), { EEnemyArchetype::Frostbitten }, FeetOf(*Aimer));
			State.AimAmbusher = Group.IsEmpty() ? nullptr : Group[0];
			Enter(State, EStage::AimHold);
			return true;
		}

		case EStage::AimHold:
		{
			AEnemyCharacter* Ambusher = State.AimAmbusher.Get();
			AOperativeCharacter* Aimer = State.Aimer.Get();
			EPressureAmbushState AmbushState = EPressureAmbushState::Hidden;
			float Held = 0.f;
			if (!Ambusher || !Aimer || !Pressure->GetAmbushState(Ambusher, AmbushState, Held))
			{
				Check(State, false, TEXT("the ambusher in the aim cone is there"));
				return Finish(State, false);
			}
			if (AmbushState != EPressureAmbushState::Sprung && State.StageTime < 3.f)
			{
				return true;
			}
			const TArray<FAmbushWatcher> Watchers = Pressure->GatherWatchers();
			const int32 Index = AmbushWatchRules::FindWatcher(Watchers, FVector2D(Ambusher->GetActorLocation()), Config.Watch);
			const bool bByHim = Watchers.IsValidIndex(Index) && FVector2D::Distance(Watchers[Index].Position, FVector2D(Aimer->GetActorLocation())) < 1.0;
			FVector AimPoint;
			const bool bStillAiming = Aimer->GetAimTargetPoint(AimPoint) && State.Decoy.IsValid()
				&& FVector::Dist2D(AimPoint, State.Decoy->GetActorLocation()) <= 200.f;
			Check(State, AmbushState == EPressureAmbushState::Holding && Held >= 2.5f && Ambusher->IsOnPatrol(),
				FString::Printf(TEXT("aiming at another enemy he still watches: the ambusher in his aim cone HOLDS (%.1f s, state %d)"), Held, static_cast<int32>(AmbushState)));
			Check(State, bByHim && bStillAiming, FString::Printf(TEXT("... by the leader's look (%s, still aiming at the decoy %d; only his aim covers it: %d)"),
				Watchers.IsValidIndex(Index) ? (Watchers[Index].Source == EAmbushLookSource::Aim ? TEXT("aim") : Watchers[Index].Source == EAmbushLookSource::Body
					? TEXT("body") : TEXT("sector")) : TEXT("none"), bStillAiming ? 1 : 0, State.bAimDiscriminating ? 1 : 0));
			Pressure->DespawnAll();
			if (AEnemyCharacter* Decoy = State.Decoy.Get())
			{
				Decoy->Destroy();
			}
			Aimer->SetManualPriorityTarget(nullptr);
			return Finish(State, true);
		}

		default:
			return false;
		}
	}

	void Start(UWorld* World, bool bShot)
	{
		TSharedRef<FState> State = MakeShared<FState>();
		State->bShot = bShot;
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			UWorld* W = WeakWorld.Get();
			return W && State->Stage != EStage::Done && Step(W, *State, W->IsPaused() ? 0.f : W->GetDeltaSeconds());
		}), 0.f);
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		const bool bShot = Args.Contains(TEXT("shot"));
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld, bShot]()
		{
			if (UWorld* W = WeakWorld.Get())
			{
				Start(W, bShot);
			}
		}), NavWarmupSeconds, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.CombatLabAmbushSmoke"),
		TEXT("Dev check (combat lab ambushes v2, game designer 2026-11-01, L_FormationLabCombat): the first group at 30 s, hiding spots at wall ends / doorways / ")
		TEXT("authored points (0.8 m off the wall), a coordinated ambush (flanks first, ranged ones suppress, then rear + front), a bypassed ambush runs a concealed route ahead, an operative ")
		TEXT("aiming at another enemy still watches; [shot]. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
