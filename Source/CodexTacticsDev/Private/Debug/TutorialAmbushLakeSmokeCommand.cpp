// Dev-only check of the Demo 01 A4 ambush lesson in TWO parts on the designer's lake layout (game designer 2026-10-21: "part 1 - a zombie
// walking a perimeter round ONE building, we set a tripwire for it; part 2 - LURE a zombie out into open ground and shoot it at close
// range"; the two patrols split onto two zombies and two buildings in the map, placement kept as it is):
//   Scripts/smoke_parallel.ps1 -Commands TutorialAmbushLakeSmoke   (map /Game/Maps/L_Demo01_A_FrozenLake + -LevelJson, Scripts/test_map.json)
//   rendered frames: UnrealEditor.exe <uproject> /Game/Maps/L_Demo01_A_FrozenLake -game -windowed -ResX=1600 -ResY=900
//       -LevelJson=L_Demo01_A_FrozenLake.json -ExecCmds="CodexTactics.TutorialAmbushLakeSmoke Shot"  -> Saved/Screenshots/AmbushLake/NN_*.png
// Needs the user's 2026-10-21 lake layout (two Military Zombies, Container_A4_North moved, the far route re-laid).
// The tutorial runs from the level (demo_01_steps.json, its "enemies" bindings: A4_PERIMETER = the zombie on the route labelled
// PatrolRoute_A4_Hounds, A4_LURE = the one on PatrolRoute_A4_Frostbitten). Nothing is saved (checkpoints off).
//  0. both bindings find exactly one placed enemy each, different ones; part 1's route is a closed loop round ONE container (its centre
//     inside the loop), part 2's route passes the other container. Both zombies' sight / footstep hearing are off (the lesson flow is
//     checked here, not the stealth odds - LakeLureProbe measures those); the perimeter zombie survives the wire (it is shot later).
//  1. the squad is put behind the overlook's low cover (VOL_A4_OVERLOOK) -> A4: the cone card (O), the posture card (,);
//  2. PART 1: the tripwire card - the Medic-Sapper, Solo; the current target lies on the PERIMETER zombie's loop; she rigs a REAL wire
//     (two clicks of the placement) across the loop and comes back; the FAR zombie's TripwireTriggered.Enemy does NOT complete part 1;
//     the perimeter zombie walks its loop into the wire -> the wait objective completes, "That's one";
//  3. PART 2: the lure card - the current target is the lure spot 10 m short of the far zombie's path; the PERIMETER zombie's
//     StealthLure.Heard does NOT complete it; she whistles for real from there (moved up to 11 m of him) -> the FAR zombie investigates ->
//     the close-shot card, its target follows him;
//  4. the PERIMETER zombie is shot dead for real 6 m from her, off its loop: the close shot is NOT completed (nor the "not up close" line);
//  5. the far zombie comes off his route (>= 4.5 m) within 9.5 m of her: she shoots him dead for real -> EnemyKilled.Squad -> the
//     close-shot card completes, the "not up close" line never shows.
// 2026-10-21 b (user play-test): part 2 starts with the approach along the NEAR container - she takes cover at its wall for real
// (OrderTakeCover -> CoverEntered), then rounds it in cover for real (the controller's wall-following, TryOrderCoverWrap). The lake is
// "ambush_fight": "skirmish": every fight above is a skirmish of the zombie that engaged (the other keeps patrolling) and ends straight
// in exploration (no WaveCleared / PostCombat, no teleport, no cleanup).
//   CodexTactics.TutorialAmbushLakeSpottedSmoke     = PART 1 FAILS: the perimeter zombie spots her at the wire card (BreakPatrol, Sight):
//     the wire card is dropped, the SPOTTED card + the free tactical pause, a skirmish of that zombie alone (the far one keeps patrolling);
//     AGGRESSIVE completes the card, Space resumes, she shoots him dead -> straight back to exploring (no wave panel, the squad not
//     teleported, the far zombie still there and patrolling) -> part 2 goes on (wall, corner, lure, close shot).
//   CodexTactics.TutorialAmbushLakeLureSpottedSmoke = PART 2 FAILS: after the wire, the far zombie spots her at the wall card: the SPOTTED
//     card + pause (first time on the level), the corner / lure cards skip themselves, a skirmish of the far zombie alone, killed -> exploring.
// 2026-10-22 (game designer play-test): the posture lesson asks for DEFENSIVE (the squad is DEFENSIVE through the lessons; the SPOTTED
// runs set PASSIVE themselves - the card only comes for a PASSIVE squad); the SPOTTED run also shows the TACTICAL PAUSE lesson (card 1:
// time stops / orders, two orders; card 2: resume and the limits; Space) - rendered frames of both cards.
// 2026-10-23 (game designer redesign of part 1, "zombie #1 spots the squad while they run to the cover"): the perimeter zombie is DORMANT
// from the level start (hidden, no collision, no AI tick) while the squad approaches; only when all three are IN COVER behind the
// overlook's low cover (real cover orders) does it wake - on its own loop, hidden from every operative (sight traces from each one's eye),
// patrolling; the card TAKE HIM QUIETLY: ANY one operative (the Engineer here, Solo, selected) rigs the wire (the others held: "Hold here
// - <Name> handles it."); the zombie walks into it and DIES by the squad's trap (EnemyKilled.SquadTrap) -> part 1 done.
//   CodexTactics.TutorialAmbushLakeMineSmoke = the same with a MINE (the Engineer carries one: the card names it) planted on the loop.
//   CodexTactics.TutorialAmbushLakeEarlyKillSmoke = the lake's PMC wires and mines listed (what the horde prep can use); the first PMC wire
//     the squad meets: the TRIPWIRE card shows (beat-less, not precredited by the fence crawl's prone history); at the overlook BOTH zombies
//     are killed while the wire card is up -> every A4 step bound to them closes (wire card, wait, wall, corner, lure), the hold is released,
//     the current target leaves the dead zombie's loop (no stale beacon); A7: the generator runs -> the console card -> PREPARE THE GROUND.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PatrolRouteActor.h"
#include "AI/StealthLureSubsystem.h"
#include "Camera/TacticalCameraPawn.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Containers/Ticker.h"
#include "Characters/FirePostureRules.h"
#include "AI/PatrolRouteRules.h"
#include "Tactics/CoverTraceRules.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Combat/WaveSubsystem.h"
#include "Core/PlayerCommands.h"
#include "Core/SaveGameSubsystem.h"
#include "Debug/EventBusProbe.h"
#include "Debug/SmokeUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/ProximityMineActor.h"
#include "Interactables/RelocationSubsystem.h"
#include "Interactables/TripwireActor.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "Subsystems/CodexEventBus.h"
#include "Tutorial/TutorialRules.h"
#include "Tutorial/TutorialSubsystem.h"
#include "Tutorial/TutorialTriggerVolume.h"
#include "UnrealClient.h"
#include "UObject/StrongObjectPtr.h"

namespace TutorialAmbushLakeSmoke
{
	constexpr float StepSeconds = 0.1f;
	const FName PerimeterBinding(TEXT("A4_PERIMETER"));
	const FName LureBinding(TEXT("A4_LURE"));

	enum class EMode : uint8
	{
		Success,
		/** Part 1 fails: the perimeter zombie spots the squad at the wire card. */
		SpottedPart1,
		/** Part 2 fails: the far zombie spots her at the wall card. */
		SpottedPart2,
		/** 2026-10-22: the wire card on the first PMC wire; both zombies killed early; the horde prep card. */
		EarlyKill,
		/** 2026-10-23: part 1 by a MINE the Engineer plants on the loop. */
		MineKill
	};

	struct FState
	{
		EMode Mode = EMode::Success;
		FCoverSlot ContainerSlot;
		int32 CoverRetries = 0;
		FVector LureSpot = FVector::ZeroVector;
		/** The flow went through WaveCleared / PostCombat during a skirmish (it must not). */
		bool bSawWavePanel = false;
		FVector BeforeKill = FVector::ZeroVector;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		bool bShots = false;
		int32 ShotIndex = 0;
		/** A requested frame is taken this many seconds after the camera was pointed (0 = none pending). */
		float ShotAt = -1.f;
		/** When the last frame was requested from the renderer (later stages wait a moment so it shows that moment). */
		float FrameTakenAt = -10.f;
		/** The camera follows another actor for a frame; back to the leader afterwards. */
		bool bRestoreFollow = false;
		FString ShotName;
		TWeakObjectPtr<AEnemyCharacter> Perimeter;
		TWeakObjectPtr<AEnemyCharacter> Lured;
		TWeakObjectPtr<AOperativeCharacter> Medic;
		/** 2026-10-23: the operative who takes part 1 (any one: the Engineer here). */
		TWeakObjectPtr<AOperativeCharacter> Rigger;
		TWeakObjectPtr<ATripwireActor> Wire;
		/** 2026-10-23 (MineKill): the squad's mine on the loop. */
		TWeakObjectPtr<AProximityMineActor> Mine;
		FVector WireA = FVector::ZeroVector;
		FVector WireB = FVector::ZeroVector;
		FVector CoverSpot = FVector::ZeroVector;
		bool bPart1Shot = false;
		bool bOpenShot = false;
		float LastShotAt = -10.f;
		/** 2026-10-22 (EarlyKill): the level's PMC wire the squad meets first. */
		TWeakObjectPtr<ATripwireActor> PmcWire;
		TStrongObjectPtr<UEventBusProbe> Probe;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		State.Probe.Reset();
		FPlatformMisc::RequestExit(false, TEXT("TutorialAmbushLakeSmoke"));
		return false;
	}

	void Next(FState& State)
	{
		++State.Stage;
		State.StageTime = 0.f;
	}

	TArray<FVector> RoutePoints(const APatrolRouteActor& Route)
	{
		TArray<FVector> Points;
		for (int32 Index = 0; Index < Route.GetNumberOfWaypoints(); ++Index)
		{
			Points.Add(Route.GetWaypointWorldLocation(Index));
		}
		return Points;
	}

	float OffRouteCm(const AEnemyCharacter& Enemy)
	{
		const APatrolRouteActor* Route = Enemy.AssignedPatrolRoute.Get();
		return Route ? TutorialRules::DistanceToRoute2D(RoutePoints(*Route), Route->bIsLoop, Enemy.GetActorLocation()) : -1.f;
	}

	/** 2D point-in-polygon (crossing rule). */
	bool IsInsideLoop(const TArray<FVector>& Loop, const FVector& Point)
	{
		bool bInside = false;
		for (int32 I = 0, J = Loop.Num() - 1; I < Loop.Num(); J = I++)
		{
			if (((Loop[I].Y > Point.Y) != (Loop[J].Y > Point.Y))
				&& (Point.X < (Loop[J].X - Loop[I].X) * (Point.Y - Loop[I].Y) / (Loop[J].Y - Loop[I].Y) + Loop[I].X))
			{
				bInside = !bInside;
			}
		}
		return bInside;
	}

	FVector Ground(UWorld* World, const FVector& Point)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(AmbushLakeGround), false);
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			Params.AddIgnoredActor(*It);
		}
		return World->LineTraceSingleByChannel(Hit, Point + FVector(0.f, 0.f, 800.f), Point - FVector(0.f, 0.f, 2000.f), ECC_Visibility, Params)
			? Hit.ImpactPoint : Point;
	}

	/** Puts Operative standing on the ground at Spot (capsule above it), stopped. */
	void Place(UWorld* World, AOperativeCharacter& Operative, const FVector& Spot)
	{
		Operative.StopOperative();
		const FVector Feet = Ground(World, Spot);
		Operative.SetActorLocation(Feet + FVector(0.f, 0.f, Operative.GetSimpleCollisionHalfHeight() + 2.f), false, nullptr, ETeleportType::TeleportPhysics);
	}

	void Deafen(AEnemyCharacter& Enemy)
	{
		// The lesson flow is under test, not the stealth odds: only a lure / a shot moves it (LakeLureProbe measures the senses).
		Enemy.bOverridePerception = true;
		Enemy.PerceptionOverride.SightRangeCm = 0.f;
		Enemy.PerceptionOverride.ProximityCm = 0.f;
		Enemy.PerceptionOverride.HearWalkCm = Enemy.PerceptionOverride.HearRunCm = 0.f;
		Enemy.PerceptionOverride.HearCrouchWalkCm = Enemy.PerceptionOverride.HearCrawlCm = 0.f;
		Enemy.PerceptionOverride.SmellRadiusCm = 0.f;
		// The fight's gunshots too (a zombie that hears the fight joins the skirmish - real, but each smoke checks its own zombie's).
		Enemy.PerceptionOverride.HearGunshotCm = Enemy.PerceptionOverride.HearExplosionCm = 0.f;
	}

	/** "Shot" runs: points the tactical camera at Where. */
	void PointCamera(UWorld* World, const FState& State, const FVector& Where, float Distance)
	{
		APlayerController* PC = State.bShots ? World->GetFirstPlayerController() : nullptr;
		if (ATacticalCameraPawn* Camera = PC ? Cast<ATacticalCameraPawn>(PC->GetPawn()) : nullptr)
		{
			Camera->SmoothFocusOnPosition(Where, 0.2f, Distance);
		}
	}

	/** "Shot" runs: points the tactical camera at Where (unless bFocus is off) and takes a frame Delay seconds later. */
	void RequestFrame(UWorld* World, FState& State, const FVector& Where, const TCHAR* Name, float Distance = 1700.f, float Delay = 0.7f, bool bFocus = true,
		AActor* Follow = nullptr)
	{
		if (!State.bShots || State.ShotAt >= 0.f)
		{
			return;
		}
		APlayerController* PC = World->GetFirstPlayerController();
		ATacticalCameraPawn* Camera = PC ? Cast<ATacticalCameraPawn>(PC->GetPawn()) : nullptr;
		if (Follow && Camera)
		{
			Camera->SetFollowTarget(Follow); // the camera follows the leader otherwise: frame this actor
			Camera->SnapToFollowTarget();
			State.bRestoreFollow = true;
		}
		else if (bFocus)
		{
			PointCamera(World, State, Where, Distance);
		}
		State.ShotAt = State.Time + Delay;
		State.ShotName = FString::Printf(TEXT("%02d_%s.png"), ++State.ShotIndex, Name);
	}

	void TickFrame(FState& State)
	{
		if (State.ShotAt >= 0.f && State.Time >= State.ShotAt)
		{
			const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("AmbushLake") / State.ShotName;
			FScreenshotRequest::RequestScreenshot(Path, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
			UE_LOG(LogCodexTactics, Display, TEXT("[AmbushLake] frame -> %s"), *Path);
			State.ShotAt = -1.f;
			State.FrameTakenAt = State.Time;
		}
	}

	/** The squad's tripwire (not the level's PMC one). */
	ATripwireActor* FindSquadWire(UWorld* World)
	{
		for (TActorIterator<ATripwireActor> It(World); It; ++It)
		{
			if (!It->IsHostile() && !It->IsPreview())
			{
				return *It;
			}
		}
		return nullptr;
	}

	/** 2026-10-23: the tutorial save's "dormant_enemies" entry of a binding (null when missing). */
	TSharedPtr<FJsonObject> SavedDormant(const UTutorialSubsystem& Tutorial, FName Binding)
	{
		const TSharedPtr<FJsonObject> Saved = Tutorial.CaptureSaveState();
		const TSharedPtr<FJsonObject>* Dormant = nullptr;
		const TSharedPtr<FJsonObject>* Entry = nullptr;
		return Saved.IsValid() && Saved->TryGetObjectField(TEXT("dormant_enemies"), Dormant) && (*Dormant)->TryGetObjectField(Binding.ToString(), Entry)
			? *Entry : nullptr;
	}

	float NearestOperativeCm(const USquadSubsystem& Squad, const AActor& To)
	{
		float Best = TNumericLimits<float>::Max();
		for (const AOperativeCharacter* Member : Squad.GetMembers())
		{
			if (Member && (!Member->HealthComponent || Member->HealthComponent->IsAlive()))
			{
				Best = FMath::Min(Best, static_cast<float>(FVector::Dist(Member->GetActorLocation(), To.GetActorLocation())));
			}
		}
		return Best;
	}

	/** A squad mate of the Medic-Sapper (the fire support that ends a skirmish; she walks on afterwards). */
	AOperativeCharacter* FireSupport(const USquadSubsystem& Squad, const AOperativeCharacter* Medic)
	{
		for (AOperativeCharacter* Member : Squad.GetMembers())
		{
			if (Member && Member != Medic && (!Member->HealthComponent || Member->HealthComponent->IsAlive()))
			{
				return Member;
			}
		}
		return nullptr;
	}

	bool IsDead(const AEnemyCharacter* Enemy)
	{
		return !Enemy || Enemy->IsDying() || (Enemy->GetHealthComponent() && !Enemy->GetHealthComponent()->IsAlive());
	}

	/** She fires at Target every 0.6 s (the real shot: hit roll, damage, the fight it starts). */
	void FireAt(FState& State, AOperativeCharacter& Shooter, AEnemyCharacter& Target)
	{
		if (State.Time - State.LastShotAt < 0.6f)
		{
			return;
		}
		State.LastShotAt = State.Time;
		const bool bShot = Shooter.ShootAtTarget(&Target);
		UE_LOG(LogCodexTactics, Display, TEXT("[AmbushLake] %s fires at %s (%.1f m): %s, HP %.0f"), *Shooter.DisplayName.ToString(), *Target.GetName(),
			FVector::Dist(Shooter.GetActorLocation(), Target.GetActorLocation()) / 100.f, bShot ? TEXT("shot") : TEXT("no shot"),
			Target.GetHealthComponent() ? Target.GetHealthComponent()->GetCurrentHealth() : -1.f);
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		TickFrame(State);
		if (State.bRestoreFollow && State.ShotAt < 0.f && State.Time > State.FrameTakenAt + 0.3f)
		{
			State.bRestoreFollow = false;
			APlayerController* PC = World->GetFirstPlayerController();
			const USquadSubsystem* LeaderSquad = World->GetSubsystem<USquadSubsystem>();
			if (ATacticalCameraPawn* Camera = PC ? Cast<ATacticalCameraPawn>(PC->GetPawn()) : nullptr; Camera && LeaderSquad && LeaderSquad->GetLeader())
			{
				Camera->SetFollowTarget(LeaderSquad->GetLeader());
			}
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UTutorialSubsystem* Tutorial = World->GetSubsystem<UTutorialSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Squad || !Tutorial || !Leader)
		{
			if (State.Time > 120.f)
			{
				Check(State, false, TEXT("a squad and the tutorial in the world"));
				return Finish(State, false);
			}
			return true;
		}
		if (State.Time > 600.f)
		{
			Check(State, false, FString::Printf(TEXT("time out in stage %d"), State.Stage));
			return Finish(State, false);
		}
		AEnemyCharacter* Perimeter = State.Perimeter.Get();
		AEnemyCharacter* Lured = State.Lured.Get();
		AOperativeCharacter* Medic = State.Medic.Get();
		if (const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
			Flow && (Flow->GetPhase() == ECodexGamePhase::WaveCleared || Flow->GetPhase() == ECodexGamePhase::PostCombat))
		{
			State.bSawWavePanel = true;
		}
		// Cards that queue in between (the overlook posture twin behind the wire card, the out-of-sight card): the player does what they ask.
		if (Tutorial->GetStepState(TEXT("A4_posture")) == ETutorialStepState::Active)
		{
			if (State.Mode == EMode::SpottedPart1 || State.Mode == EMode::SpottedPart2)
			{
				// The SPOTTED runs play a player who keeps the squad PASSIVE (the card's condition): he skips the posture card.
				Tutorial->SkipStep(TEXT("A4_posture"));
			}
			else
			{
				Tutorial->HandlePlayerCommand(PlayerCommands::PostureDefensive, FPlayerCommandContext()); // 2026-10-22: DEFENSIVE, not PASSIVE
				Squad->SetSquadPosture(ESquadFirePosture::Defensive);
				Check(State, Tutorial->GetStepState(TEXT("A4_posture")) == ETutorialStepState::Completed, TEXT("2026-10-22: DEFENSIVE (Alt + .) completes the posture card"));
			}
		}
		if (Tutorial->GetStepState(TEXT("A4_hidden")) == ETutorialStepState::Active)
		{
			Tutorial->HandlePlayerCommand(PlayerCommands::StanceCrouch, FPlayerCommandContext());
		}

		switch (State.Stage)
		{
		case 0:
		{
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			FNavLocation OnNav;
			if ((!Tutorial->GetMachine().IsLoaded() || Squad->GetMembers().Num() < 3
				|| !(Nav && Nav->ProjectPointToNavigation(Leader->GetActorLocation(), OnNav, FVector(60.f, 60.f, 200.f)))) && State.StageTime < 120.f)
			{
				return true;
			}
			if (USaveGameSubsystem* Saves = World->GetSubsystem<USaveGameSubsystem>())
			{
				Saves->bCheckpointsEnabled = false; // the beat checkpoints would overwrite the player's slot
			}
			State.Probe.Reset(NewObject<UEventBusProbe>());
			State.Probe->ListenGameplayEvents(UCodexEventBus::Get(World));
			const TArray<AEnemyCharacter*> PerimeterZombies = Tutorial->FindBoundEnemies(PerimeterBinding);
			const TArray<AEnemyCharacter*> LureZombies = Tutorial->FindBoundEnemies(LureBinding);
			Check(State, Tutorial->IsTutorialActive() && Tutorial->GetMachine().GetData().FindEnemy(PerimeterBinding) && Tutorial->GetMachine().GetData().FindEnemy(LureBinding),
				TEXT("the lake's tutorial runs with the two A4 enemy bindings"));
			Check(State, PerimeterZombies.Num() == 1 && LureZombies.Num() == 1 && PerimeterZombies[0] != LureZombies[0],
				FString::Printf(TEXT("each binding finds exactly one placed zombie (A4_PERIMETER %d, A4_LURE %d)"), PerimeterZombies.Num(), LureZombies.Num()));
			if (PerimeterZombies.Num() != 1 || LureZombies.Num() != 1)
			{
				return Finish(State, false);
			}
			State.Perimeter = PerimeterZombies[0];
			State.Lured = LureZombies[0];
			Perimeter = PerimeterZombies[0];
			Lured = LureZombies[0];
			for (const AEnemyCharacter* Zombie : { Perimeter, Lured })
			{
				const APatrolRouteActor* Route = Zombie->AssignedPatrolRoute.Get();
				FString Points;
				for (const FVector& Point : Route ? RoutePoints(*Route) : TArray<FVector>())
				{
					Points += FString::Printf(TEXT(" (%.0f, %.0f, %.0f)"), Point.X, Point.Y, Point.Z);
				}
				UE_LOG(LogCodexTactics, Display, TEXT("[AmbushLake] %s %s at %s, route %s (loop %d):%s"), Zombie == Perimeter ? TEXT("PART 1") : TEXT("PART 2"),
					*Zombie->GetName(), *Zombie->GetActorLocation().ToCompactString(), Route ? *Route->GetName() : TEXT("-"), Route && Route->bIsLoop ? 1 : 0, *Points);
			}
			// Part 1's route is a closed perimeter round ONE container, part 2's runs past the other one.
			const AStaticMeshActor* NearContainer = nullptr;
			const AStaticMeshActor* FarContainer = nullptr;
			for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
			{
				const FString Name = It->GetActorNameOrLabel();
				NearContainer = Name == TEXT("Container_A4_South") ? *It : NearContainer;
				FarContainer = Name == TEXT("Container_A4_North") ? *It : FarContainer;
			}
			const APatrolRouteActor* Loop = Perimeter->AssignedPatrolRoute.Get();
			const APatrolRouteActor* FarRoute = Lured->AssignedPatrolRoute.Get();
			Check(State, Loop && Loop->bIsLoop && NearContainer && IsInsideLoop(RoutePoints(*Loop), NearContainer->GetActorLocation())
				&& (!FarContainer || !IsInsideLoop(RoutePoints(*Loop), FarContainer->GetActorLocation())),
				TEXT("part 1: the zombie's route is a closed loop round ONE building (Container_A4_South inside it, Container_A4_North not)"));
			Check(State, FarRoute && FarContainer && TutorialRules::DistanceToRoute2D(RoutePoints(*FarRoute), FarRoute->bIsLoop, FarContainer->GetActorLocation()) < 800.f,
				TEXT("part 2: the zombie's route passes the other building (Container_A4_North within 8 m)"));
			Deafen(*Perimeter);
			Deafen(*Lured);
			// 2026-10-23: part 1 is done when the squad's trap KILLS it - a little under the Military Zombie's 60 HP so the blast's falloff
			// (it walks on during the 1.5 s fuse) cannot leave it standing in the check.
			Perimeter->GetHealthComponent()->SetMaxHealth(40.f);
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member && Member->SquadRole == EOperativeRole::Engineer)
				{
					State.Rigger = Member;
					Member->MinesCount = State.Mode == EMode::MineKill ? 1 : 0; // the mine comes from the A3 PMC mines (here: given)
					Member->GrenadesCount = FMath::Max(Member->GrenadesCount, 2);
				}
				if (Member && Member->SquadRole == EOperativeRole::MedicSapper)
				{
					State.Medic = Member;
					Member->MinesCount = 0;
				}
			}
			Squad->SetSquadPosture(ESquadFirePosture::Defensive); // 2026-10-22: the lessons keep the squad DEFENSIVE
			// Behind the overlook's low cover (inside VOL_A4_OVERLOOK), crouched.
			const ATutorialTriggerVolume* Overlook = nullptr;
			for (TActorIterator<ATutorialTriggerVolume> It(World); It; ++It)
			{
				Overlook = It->GetEffectiveVolumeId() == FName(TEXT("VOL_A4_OVERLOOK")) ? *It : Overlook;
			}
			Check(State, Overlook != nullptr, TEXT("the overlook volume"));
			if (!Overlook)
			{
				return Finish(State, false);
			}
			const FVector Centre = Overlook->GetActorLocation();
			State.CoverSpot = FVector(Centre.X + 250.f, Centre.Y, Centre.Z);
			int32 Slot = 0;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Place(World, *Member, State.CoverSpot + FVector(0.f, (Slot++ - 1) * 120.f, 0.f));
				Member->SetStance(EOperativeStance::Crouching);
			}
			Next(State);
			return true;
		}
		case 1:
		{
			if (Tutorial->GetStepState(TEXT("A4_cones")) != ETutorialStepState::Active && State.StageTime < 10.f)
			{
				return true;
			}
			Check(State, Tutorial->GetCurrentBeat() == FName(TEXT("A4")) && Tutorial->GetStepState(TEXT("A4_cones")) == ETutorialStepState::Active,
				FString::Printf(TEXT("the overlook starts A4: the cone card (beat %s)"), *Tutorial->GetCurrentBeat().ToString()));
			Check(State, Tutorial->IsEnemyDormant(*Perimeter) && !Tutorial->IsBindingAwake(PerimeterBinding) && Perimeter->IsHidden() && !Perimeter->IsActorTickEnabled()
				&& !Perimeter->GetActorEnableCollision() && FVector::Dist2D(Perimeter->GetActorLocation(), Leader->GetActorLocation()) > 0.f
				&& Perimeter->GetActorLocation().Z < Leader->GetActorLocation().Z - 10000.f,
				FString::Printf(TEXT("2026-10-23: zombie #1 is DORMANT while the squad approaches - not visible, no collision, no AI tick, out of the level (z %.0f)"),
					Perimeter->GetActorLocation().Z));
			{
				const TSharedPtr<FJsonObject> Saved = SavedDormant(*Tutorial, PerimeterBinding);
				bool bWoken = true;
				const TSharedPtr<FJsonObject>* Homes = nullptr;
				Check(State, Saved.IsValid() && Saved->TryGetBoolField(TEXT("woken"), bWoken) && !bWoken && Saved->TryGetObjectField(TEXT("homes"), Homes)
					&& (*Homes)->HasField(Perimeter->GetName()), TEXT("the save keeps zombie #1 dormant (dormant_enemies: not woken, where it was placed)"));
			}
			Check(State, !Tutorial->IsEnemyDormant(*Lured) && Lured->GetActorLocation().Z > Leader->GetActorLocation().Z - 1000.f,
				FString::Printf(TEXT("zombie #2 is there as placed (dormant %d, hidden %d, z %.0f, leader z %.0f)"), Tutorial->IsEnemyDormant(*Lured) ? 1 : 0,
					Lured->IsHidden() ? 1 : 0, Lured->GetActorLocation().Z, Leader->GetActorLocation().Z));
			Tutorial->HandlePlayerCommand(PlayerCommands::ToggleVisionCones, FPlayerCommandContext());
			Next(State);
			return true;
		}
		case 2:
		{
			const ETutorialStepState Posture = Tutorial->GetStepState(TEXT("A4_posture"));
			if (Posture != ETutorialStepState::Active && !TutorialRules::IsFinished(Posture) && State.StageTime < 8.f)
			{
				return true;
			}
			if (Posture == ETutorialStepState::Active)
			{
				Tutorial->HandlePlayerCommand(PlayerCommands::PostureDefensive, FPlayerCommandContext());
				Squad->SetSquadPosture(ESquadFirePosture::Defensive);
				Check(State, Tutorial->GetStepState(TEXT("A4_posture")) == ETutorialStepState::Completed, TEXT("2026-10-22: DEFENSIVE (Alt + .) completes the posture card"));
			}
			Check(State, Tutorial->IsEnemyDormant(*Perimeter) && Perimeter->IsHidden() && Tutorial->GetStepState(TEXT("A4_tripwire")) == ETutorialStepState::Idle,
				TEXT("2026-10-23: at the overlook, not in cover yet: zombie #1 still dormant, no part 1 card"));
			State.Stage = 90;
			State.StageTime = 0.f;
			return true;
		}
		case 90:
		{
			// 2026-10-23: every operative into cover behind the overlook's low cover (the REAL cover order at its nearest face).
			const AStaticMeshActor* LowCover = nullptr;
			for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
			{
				LowCover = It->GetActorNameOrLabel() == TEXT("Overlook_LowCover") ? *It : LowCover;
			}
			Check(State, LowCover != nullptr, TEXT("the overlook's low cover (Overlook_LowCover)"));
			if (!LowCover)
			{
				return Finish(State, false);
			}
			const FBox Box = LowCover->GetComponentsBoundingBox();
			int32 Ordered = 0;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				const FVector At = Member->GetActorLocation();
				FVector Face = Box.GetClosestPointTo(At);
				const FVector Dir = (Face - At).GetSafeNormal2D();
				Face = Ground(World, Face - Dir * 5.f) + FVector(0.f, 0.f, 40.f) + Dir * 5.f;
				FCoverSlot Slot;
				const bool bSlot = CoverTraceRules::FindCoverSlotAt(World, Face, Dir, Slot);
				const EOperativeOrderResult Result = bSlot ? Member->OrderTakeCover(Slot, false) : EOperativeOrderResult::Refused;
				Ordered += Result == EOperativeOrderResult::Accepted ? 1 : 0;
				UE_LOG(LogCodexTactics, Display, TEXT("[AmbushLake] %s into cover at the low cover: face %s, slot %d, order %d"), *Member->DisplayName.ToString(),
					*Face.ToCompactString(), bSlot ? 1 : 0, static_cast<int32>(Result));
			}
			Check(State, Ordered == Squad->GetMembers().Num(), FString::Printf(TEXT("all three ordered into cover at the low cover (%d)"), Ordered));
			Next(State);
			return true;
		}
		case 91:
		{
			int32 InCover = 0;
			for (const AOperativeCharacter* Member : Squad->GetMembers())
			{
				InCover += Member->bInCover && !Member->IsMoving() ? 1 : 0;
			}
			if (InCover < Squad->GetMembers().Num())
			{
				if (InCover > 0 && Tutorial->IsBindingAwake(PerimeterBinding))
				{
					Check(State, false, FString::Printf(TEXT("zombie #1 woke with only %d of 3 in cover"), InCover));
					return Finish(State, false);
				}
				if (State.StageTime < 30.f)
				{
					return true;
				}
			}
			if (!Tutorial->IsBindingAwake(PerimeterBinding) && State.StageTime < 32.f)
			{
				return true;
			}
			Check(State, InCover == Squad->GetMembers().Num() && Tutorial->IsBindingAwake(PerimeterBinding),
				FString::Printf(TEXT("all three in cover at the overlook -> zombie #1 wakes (%d in cover, %.1f s)"), InCover, State.StageTime));
			FVector Spawn = FVector::ZeroVector;
			const bool bSpawn = Tutorial->GetWakeSpawn(PerimeterBinding, Spawn);
			Check(State, !Perimeter->IsHidden() && Perimeter->IsActorTickEnabled() && Perimeter->GetActorEnableCollision() && Perimeter->IsOnPatrol()
				&& bSpawn && FVector::Dist2D(Perimeter->GetActorLocation(), Spawn) < 300.f && OffRouteCm(*Perimeter) < 150.f,
				FString::Printf(TEXT("... visible, its AI on, patrolling from %s on its own loop (%.0f cm off it)"), *Spawn.ToCompactString(), OffRouteCm(*Perimeter)));
			// Hidden from every operative: each one's eye (his stance) to the zombie's chest and knees at the wake point is blocked.
			FCollisionQueryParams Params(SCENE_QUERY_STAT(AmbushLakeWakeSight), false);
			Params.AddIgnoredActor(Perimeter);
			for (const AOperativeCharacter* Member : Squad->GetMembers())
			{
				Params.AddIgnoredActor(Member);
			}
			int32 SeenBy = 0;
			FString Seers;
			for (const AOperativeCharacter* Member : Squad->GetMembers())
			{
				const float EyeZ = Member->GetStance() == EOperativeStance::Prone ? 35.f : Member->GetStance() == EOperativeStance::Crouching ? 110.f : 165.f;
				const FVector Eye = Member->GetActorLocation() - FVector(0.f, 0.f, Member->GetSimpleCollisionHalfHeight() - EyeZ);
				for (const float Height : { 130.f, 50.f })
				{
					if (!World->LineTraceTestByChannel(Eye, Spawn + FVector(0.f, 0.f, Height), ECC_Visibility, Params))
					{
						++SeenBy;
						Seers += Member->DisplayName.ToString() + TEXT(" ");
					}
				}
			}
			Check(State, bSpawn && SeenBy == 0, FString::Printf(TEXT("... the wake point is hidden from every operative (seen by: %s)"), Seers.IsEmpty() ? TEXT("nobody") : *Seers));
			Check(State, State.Probe->EventNames.Contains(FName(TEXT("EnemyWoke.A4_PERIMETER"))), TEXT("... EnemyWoke.A4_PERIMETER raised"));
			{
				const TSharedPtr<FJsonObject> Saved = SavedDormant(*Tutorial, PerimeterBinding);
				bool bWoken = false;
				const TArray<TSharedPtr<FJsonValue>>* SavedSpawn = nullptr;
				Check(State, Saved.IsValid() && Saved->TryGetBoolField(TEXT("woken"), bWoken) && bWoken && Saved->TryGetArrayField(TEXT("spawn"), SavedSpawn)
					&& SavedSpawn->Num() == 3 && FMath::IsNearlyEqual((*SavedSpawn)[0]->AsNumber(), Spawn.X, 1.0) && FMath::IsNearlyEqual((*SavedSpawn)[1]->AsNumber(), Spawn.Y, 1.0),
					TEXT("... the save keeps it awake and where it woke (dormant_enemies)"));
			}
			RequestFrame(World, State, Spawn, TEXT("part1_zombie_wakes_hidden"), 2200.f);
			State.Stage = 3;
			State.StageTime = 0.f;
			return true;
		}
		case 3:
		{
			if (Tutorial->GetStepState(TEXT("A4_tripwire")) != ETutorialStepState::Active && State.StageTime < 8.f)
			{
				return true;
			}
			// 2026-10-23: ANY one operative takes part 1 (Solo, selected) - the Engineer here; the Spotted / EarlyKill runs keep her.
			AOperativeCharacter* Rigger = State.Mode == EMode::Success || State.Mode == EMode::MineKill ? State.Rigger.Get() : Medic;
			State.Rigger = Rigger;
			const FString Body = Tutorial->GetCardView().Body.ToString();
			Check(State, Tutorial->GetStepState(TEXT("A4_tripwire")) == ETutorialStepState::Active && Tutorial->IsConfining()
				&& Tutorial->GetCardView().Title.ToString().Contains(TEXT("QUIETLY")),
				FString::Printf(TEXT("PART 1: TAKE HIM QUIETLY - the squad held at the overlook ('%s')"), *Tutorial->GetCardView().Title.ToString()));
			Check(State, State.Mode == EMode::MineKill ? Body.Contains(TEXT("plant a mine")) : !Body.Contains(TEXT("mine")),
				FString::Printf(TEXT("the card names the mine only when the squad carries one (%s)"), State.Mode == EMode::MineKill ? TEXT("1 mine") : TEXT("none")));
			if (!Rigger || !Medic || !Perimeter->AssignedPatrolRoute)
			{
				return Finish(State, false);
			}
			Squad->EnterSoloMode();
			Squad->SetLeader(Rigger);
			const TArray<FVector> Loop = RoutePoints(*Perimeter->AssignedPatrolRoute);
			const FVector OnLoop = TutorialRules::NearestPointOnRoute(Loop, Perimeter->AssignedPatrolRoute->bIsLoop, Rigger->GetActorLocation());
			Tutorial->RefreshGuideNow();
			const FTutorialGuideView& Guide = Tutorial->GetGuideView();
			const APatrolRouteActor* FarRoute = Lured->AssignedPatrolRoute.Get();
			const float FromFarRoute = FarRoute ? TutorialRules::DistanceToRoute2D(RoutePoints(*FarRoute), FarRoute->bIsLoop, Guide.Location) : -1.f;
			Check(State, Guide.bValid && Guide.Guide.Id == FName(TEXT("@patrol_route.A4_PERIMETER")) && FVector::Dist2D(Guide.Location, OnLoop) < 50.f && FromFarRoute > 500.f,
				FString::Printf(TEXT("the current target is on the PERIMETER zombie's loop (%s at %s, %.0f cm from the loop point nearest to the one selected, %.1f m from the far route)"),
					*UTutorialSubsystem::DescribeGuide(Guide.Guide), *Guide.Location.ToCompactString(), FVector::Dist2D(Guide.Location, OnLoop), FromFarRoute / 100.f));
			if (State.Mode == EMode::SpottedPart1)
			{
				State.Stage = 20; // the perimeter zombie spots her before the wire is rigged
				State.StageTime = 0.f;
				return true;
			}
			if (State.Mode == EMode::EarlyKill)
			{
				State.Stage = 70; // both zombies die before the wire is rigged
				State.StageTime = 0.f;
				return true;
			}
			// The wire: across the loop's first leg at its middle (perpendicular, 3 m), the REAL two-click placement.
			const FVector LegA = Loop[0];
			const FVector LegB = Loop.Num() > 1 ? Loop[1] : Loop[0] + FVector(100.f, 0.f, 0.f);
			const FVector Mid = (LegA + LegB) * 0.5f;
			const FVector Across = FVector::CrossProduct(FVector::UpVector, (LegB - LegA).GetSafeNormal2D());
			State.WireA = Ground(World, Mid + Across * 150.f);
			State.WireB = Ground(World, Mid - Across * 150.f);
			URelocationSubsystem* Relocation = World->GetSubsystem<URelocationSubsystem>();
			if (State.Mode == EMode::MineKill)
			{
				// 2026-10-23: the mine on his path (the leg's middle), the REAL placement (F, a click).
				Check(State, Relocation && Relocation->StartDeployPlacement(EDeployableType::Mine, Rigger), TEXT("the Engineer opens the mine placement"));
				if (Relocation)
				{
					const FVector MinePoint = Ground(World, Mid);
					Relocation->UpdatePreview(MinePoint);
					Relocation->ConfirmPlacement(MinePoint); // 1: the position
					Relocation->UpdatePreview(MinePoint);
					Relocation->ConfirmPlacement(MinePoint); // 2: deploy
					Check(State, Relocation->GetActiveDeployCount() == 1, FString::Printf(TEXT("a click on his loop at %s: he goes to plant it"), *Mid.ToCompactString()));
				}
				Next(State);
				return true;
			}
			Rigger->GrenadesCount = FMath::Max(Rigger->GrenadesCount, 2);
			Check(State, Relocation && Relocation->StartTripwirePlacement(Rigger) && Relocation->IsPlacingTripwire(),
				FString::Printf(TEXT("%s opens the tripwire placement"), *Rigger->DisplayName.ToString()));
			if (Relocation)
			{
				Relocation->UpdatePreview(State.WireA);
				Relocation->ConfirmPlacement(State.WireA);
				Relocation->UpdatePreview(State.WireB);
				Relocation->ConfirmPlacement(State.WireB);
				Check(State, Relocation->GetTripwireTaskCount() == 1, FString::Printf(TEXT("two clicks across his loop at %s: he goes to rig it"), *Mid.ToCompactString()));
			}
			Next(State);
			return true;
		}
		case 4:
		{
			AOperativeCharacter* Rigger = State.Rigger.Get();
			ATripwireActor* Wire = State.Mode == EMode::MineKill ? nullptr : FindSquadWire(World);
			AProximityMineActor* Mine = nullptr;
			for (TActorIterator<AProximityMineActor> It(World); It && State.Mode == EMode::MineKill; ++It)
			{
				Mine = It->bPlacedBySquad ? *It : Mine;
			}
			const bool bArmed = State.Mode == EMode::MineKill ? Mine && Mine->IsArmedNow() : Wire && Wire->IsArmed();
			if (!bArmed && State.StageTime < 45.f)
			{
				return true;
			}
			State.Wire = Wire;
			State.Mine = Mine;
			Check(State, bArmed, FString::Printf(TEXT("the %s is placed and armed (%.1f s)"), State.Mode == EMode::MineKill ? TEXT("mine") : TEXT("wire"), State.StageTime));
			Check(State, Tutorial->GetStepState(TEXT("A4_tripwire")) == ETutorialStepState::Completed && Tutorial->GetStepState(TEXT("A4_trap_wait")) == ETutorialStepState::Active,
				TEXT("the card completes, the objective 'wait until the zombie walks into your trap' shows"));
			if (!bArmed || !Rigger)
			{
				return Finish(State, false);
			}
			// The one who went out took the job: the others are held at the overlook.
			FTutorialConfineView Hold;
			Check(State, Tutorial->GetMachine().GetActiveConfine(Hold) && Hold.FreeOperative == Rigger->GetFName(),
				FString::Printf(TEXT("2026-10-23: %s took part 1 (confine free: first)"), *Rigger->DisplayName.ToString()));
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member && Member != Rigger)
				{
					const FVector Far = Member->GetActorLocation() + (Member->GetActorLocation() - Perimeter->GetActorLocation()).GetSafeNormal2D() * -1500.f;
					Check(State, !UTutorialSubsystem::CheckConfinedOrder(World, Member, Far), FString::Printf(TEXT("... %s may not leave the overlook"), *Member->DisplayName.ToString()));
				}
			}
			// He goes back behind the low cover (the real order); the far zombie "killed by a squad trap" - not part 1's zombie.
			Rigger->OrderMoveTo(State.CoverSpot + FVector(0.f, -240.f, 0.f), false);
			Tutorial->NotifyEvent(TEXT("EnemyKilled.SquadTrap"), Lured->GetFName());
			Check(State, Tutorial->GetStepState(TEXT("A4_trap_wait")) == ETutorialStepState::Active && Tutorial->GetStepState(TEXT("A4_wt_trap")) == ETutorialStepState::Idle,
				TEXT("PART 1: the FAR zombie's EnemyKilled.SquadTrap does not complete it (no 'that's one' line)"));
			Next(State);
			return true;
		}
		case 5:
		{
			const FVector WireMid = (State.WireA + State.WireB) * 0.5f;
			if (!State.bPart1Shot && FVector::Dist2D(Perimeter->GetActorLocation(), WireMid) < 700.f)
			{
				State.bPart1Shot = true;
				RequestFrame(World, State, (Perimeter->GetActorLocation() + WireMid) * 0.5f, TEXT("part1_perimeter_zombie_and_wire"), 1500.f);
			}
			if (Tutorial->GetStepState(TEXT("A4_trap_wait")) == ETutorialStepState::Active && State.StageTime < 150.f)
			{
				return true;
			}
			Check(State, IsDead(Perimeter) && Tutorial->GetStepState(TEXT("A4_trap_wait")) == ETutorialStepState::Completed,
				FString::Printf(TEXT("PART 1: the perimeter zombie walked its loop into the %s and died -> the objective is done (%.1f s, %s)"),
					State.Mode == EMode::MineKill ? TEXT("mine") : TEXT("wire"), State.StageTime, TutorialRules::StateName(Tutorial->GetStepState(TEXT("A4_trap_wait")))));
			Check(State, Tutorial->GetStepState(TEXT("A4_wt_trap")) != ETutorialStepState::Idle, TEXT("'That's one. The other one is still by the far container.'"));
			int32 SquadTrapKills = 0;
			for (int32 Index = 0; Index < State.Probe->EventNames.Num(); ++Index)
			{
				SquadTrapKills += State.Probe->EventNames[Index] == FName(TEXT("EnemyKilled.SquadTrap")) && State.Probe->EventSubjects[Index] == Perimeter->GetFName() ? 1 : 0;
			}
			Check(State, SquadTrapKills == 1, FString::Printf(TEXT("EnemyKilled.SquadTrap for %s (part 1's zombie, killed by the squad's %s)"), *Perimeter->GetName(),
				State.Mode == EMode::MineKill ? TEXT("mine") : TEXT("wire")));
			State.Stage = 40;
			State.StageTime = 0.f;
			return true;
		}
		case 6:
		{
			if (Tutorial->GetStepState(TEXT("A4_lure")) != ETutorialStepState::Active && State.StageTime < 10.f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("A4_lure")) == ETutorialStepState::Active && !Tutorial->IsCommandLocked(PlayerCommands::StealthWhistle),
				TEXT("PART 2: the lure card (whistle / throw unlocked)"));
			const APatrolRouteActor* FarRoute = Lured->AssignedPatrolRoute.Get();
			Tutorial->RefreshGuideNow();
			const FTutorialGuideView& Guide = Tutorial->GetGuideView();
			const float FromFarRoute = FarRoute ? TutorialRules::DistanceToRoute2D(RoutePoints(*FarRoute), FarRoute->bIsLoop, Guide.Location) : -1.f;
			Check(State, Guide.bValid && Guide.Guide.Id == FName(TEXT("@lure_spot.A4_LURE")) && FromFarRoute > 900.f && FromFarRoute < 1100.f,
				FString::Printf(TEXT("the current target is the lure spot 10 m short of the FAR zombie's path (%s, %.1f m from it)"),
					*UTutorialSubsystem::DescribeGuide(Guide.Guide), FromFarRoute / 100.f));
			Check(State, Squad->GetEffectivePosture(Medic) == ESquadFirePosture::Defensive, TEXT("2026-10-22: the lure card keeps the squad DEFENSIVE (not PASSIVE)"));
			if (!IsDead(Perimeter))
			{
				// The perimeter zombie (alive: it survived the wire) hears a lure: not part 2's zombie.
				Tutorial->NotifyEvent(GameplayEvents::LureHeard, Perimeter->GetFName());
				Check(State, Tutorial->GetStepState(TEXT("A4_lure")) == ETutorialStepState::Active && Tutorial->GetStepState(TEXT("A4_close_shot")) == ETutorialStepState::Idle,
					TEXT("PART 2: the PERIMETER zombie's StealthLure.Heard does not complete the lure (no close-shot card)"));
			}
			// She goes to the spot; he may be anywhere on his route: up to 11 m of him (inside the 12 m whistle) along her line.
			// The guide's spot is 10 m off his path: she waits there until he passes it (the off-route rule of the close shot needs it).
			FVector Spot = Guide.bValid ? Guide.Location : Medic->GetActorLocation();
			Spot = SmokeUtils::FreeSpot(World, Spot, Medic);
			State.LureSpot = Spot;
			Medic->OrderMoveTo(Spot, false); // out of her cover, to the lure spot (the real order)
			UE_LOG(LogCodexTactics, Display, TEXT("[AmbushLake] lure spot %s (target %s), the far zombie %.1f m away at %s, %.1f m off his route"), *Spot.ToCompactString(),
				*Guide.Location.ToCompactString(), FVector::Dist2D(Spot, Lured->GetActorLocation()) / 100.f, *Lured->GetActorLocation().ToCompactString(), OffRouteCm(*Lured) / 100.f);
			State.Stage = 44;
			State.StageTime = 0.f;
			return true;
		}
		case 7:
		{
			if (State.StageTime < 0.8f)
			{
				return true; // she settles (crouched)
			}
			UStealthLureSubsystem* Lure = World->GetSubsystem<UStealthLureSubsystem>();
			const float ToHim = static_cast<float>(FVector::Dist2D(Medic->GetActorLocation(), Lured->GetActorLocation()));
			if (Tutorial->GetStepState(TEXT("A4_lure")) == ETutorialStepState::Active && ToHim > 1150.f)
			{
				// He walks his loop: she waits crouched at the lure spot (10 m off his path) until he passes within the 12 m whistle.
				if (FMath::Fmod(State.StageTime, 5.f) < StepSeconds)
				{
					UE_LOG(LogCodexTactics, Display, TEXT("[AmbushLake] waiting at %s: the far zombie %.1f m away at %s (on patrol %d, investigating %d)"),
						*Medic->GetActorLocation().ToCompactString(), ToHim / 100.f, *Lured->GetActorLocation().ToCompactString(), Lured->IsOnPatrol() ? 1 : 0,
						Lured->IsInvestigatingLure() ? 1 : 0);
				}
				if (State.StageTime < 130.f)
				{
					return true;
				}
			}
			if (Tutorial->GetStepState(TEXT("A4_lure")) == ETutorialStepState::Active && Lure && State.Time - State.LastShotAt > 3.f)
			{
				Medic->StopOperative();
				State.LastShotAt = State.Time;
				const bool bWhistled = Lure->Whistle(Medic);
				UE_LOG(LogCodexTactics, Display, TEXT("[AmbushLake] %s whistles (%s), the far zombie %.1f m away"), *Medic->DisplayName.ToString(),
					bWhistled ? TEXT("ok") : TEXT("refused"), FVector::Dist(Medic->GetActorLocation(), Lured->GetActorLocation()) / 100.f);
			}
			if (Tutorial->GetStepState(TEXT("A4_lure")) == ETutorialStepState::Active && State.StageTime < 140.f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("A4_lure")) == ETutorialStepState::Completed && Lured->IsInvestigatingLure(),
				FString::Printf(TEXT("PART 2: the far zombie heard the whistle and investigates -> the lure card completes (%s)"),
					TutorialRules::StateName(Tutorial->GetStepState(TEXT("A4_lure")))));
			State.LastShotAt = -10.f;
			Next(State);
			return true;
		}
		case 8:
		{
			if (Tutorial->GetStepState(TEXT("A4_close_shot")) != ETutorialStepState::Active && State.StageTime < 6.f)
			{
				return true;
			}
			Tutorial->RefreshGuideNow();
			Check(State, Tutorial->GetStepState(TEXT("A4_close_shot")) == ETutorialStepState::Active && Tutorial->GetGuideView().bValid
				&& Tutorial->GetGuideView().Guide.Id == FName(TEXT("@enemy.A4_LURE")) && FVector::Dist2D(Tutorial->GetGuideView().Location, Lured->GetActorLocation()) < 100.f,
				FString::Printf(TEXT("the CLOSE SHOT card, its target follows him (%s)"), *UTutorialSubsystem::DescribeGuide(Tutorial->GetGuideView().Guide)));
			if (IsDead(Perimeter))
			{
				State.Stage = 10; // part 1 failed: the perimeter zombie died in its skirmish
				State.StageTime = 0.f;
				return true;
			}
			// The PERIMETER zombie, shot dead for real 6 m from her, off its loop - not part 2's zombie.
			// 6 m from her, square to her line to the far zombie, on the side away from its loop (the facts of a close shot in the open).
			const FVector Side = FVector::CrossProduct(FVector::UpVector, (Lured->GetActorLocation() - Medic->GetActorLocation()).GetSafeNormal2D());
			const TArray<FVector> Loop = RoutePoints(*Perimeter->AssignedPatrolRoute);
			const FVector Left = Medic->GetActorLocation() + Side * 600.f;
			const FVector Right = Medic->GetActorLocation() - Side * 600.f;
			const FVector Beside = SmokeUtils::FreeSpot(World, TutorialRules::DistanceToRoute2D(Loop, true, Left) > TutorialRules::DistanceToRoute2D(Loop, true, Right)
				? Left : Right, Medic);
			if (AController* Controller = Perimeter->GetController())
			{
				Controller->StopMovement();
			}
			Perimeter->SetActorLocation(Ground(World, Beside) + FVector(0.f, 0.f, Perimeter->GetSimpleCollisionHalfHeight() + 2.f), false, nullptr, ETeleportType::TeleportPhysics);
			Perimeter->GetHealthComponent()->SetMaxHealth(20.f);
			Check(State, FVector::Dist(Perimeter->GetActorLocation(), Medic->GetActorLocation()) < 950.f && OffRouteCm(*Perimeter) >= 450.f,
				FString::Printf(TEXT("the perimeter zombie stands where a close shot in the open would count: %.1f m from her, %.1f m off its loop"),
					FVector::Dist(Perimeter->GetActorLocation(), Medic->GetActorLocation()) / 100.f, OffRouteCm(*Perimeter) / 100.f));
			Next(State);
			return true;
		}
		case 9:
		{
			if (!IsDead(Perimeter))
			{
				FireAt(State, *Medic, *Perimeter);
				if (State.StageTime < 15.f)
				{
					return true;
				}
			}
			if (State.StageTime < 0.3f)
			{
				return true;
			}
			Check(State, IsDead(Perimeter) && State.Probe->EventNames.Contains(FName(TEXT("EnemyKilled.Squad"))),
				TEXT("the perimeter zombie is shot dead (EnemyKilled.Squad raised)"));
			Check(State, World->GetSubsystem<UGameFlowSubsystem>()->GetPhase() == ECodexGamePhase::Exploration && !State.bSawWavePanel && Lured && !IsDead(Lured),
				TEXT("its skirmish ended straight in exploration (no wave panel); the far zombie was not part of it"));
			Check(State, Tutorial->GetStepState(TEXT("A4_close_shot")) == ETutorialStepState::Active && Tutorial->GetStepState(TEXT("A4_close_shot_far")) == ETutorialStepState::Idle,
				FString::Printf(TEXT("PART 2: the PERIMETER zombie killed up close does not complete the close shot, nor the 'not up close' line (%s / %s)"),
					TutorialRules::StateName(Tutorial->GetStepState(TEXT("A4_close_shot"))), TutorialRules::StateName(Tutorial->GetStepState(TEXT("A4_close_shot_far")))));
			State.LastShotAt = -10.f;
			Next(State);
			return true;
		}
		case 10:
		{
			// He comes off his route towards her: the close shot once he is out in the open and close.
			const float Off = OffRouteCm(*Lured);
			const float Near = NearestOperativeCm(*Squad, *Lured);
			if (IsDead(Lured) || ((Off < 450.f || Near > 950.f) && State.StageTime < 40.f))
			{
				if (FMath::Fmod(State.StageTime, 2.f) < StepSeconds)
				{
					UE_LOG(LogCodexTactics, Display, TEXT("[AmbushLake] the far zombie at %s: %.1f m off his route, %.1f m from the squad"),
						*Lured->GetActorLocation().ToCompactString(), Off / 100.f, Near / 100.f);
				}
				if (!IsDead(Lured))
				{
					return true;
				}
			}
			Check(State, !IsDead(Lured) && Off >= 450.f && Near <= 950.f, FString::Printf(TEXT("the lured zombie came out into the open: %.1f m off his route, %.1f m from her"),
				Off / 100.f, Near / 100.f));
			if (!State.bOpenShot)
			{
				State.bOpenShot = true;
				RequestFrame(World, State, (Lured->GetActorLocation() + Medic->GetActorLocation()) * 0.5f, TEXT("part2_lured_into_the_open"), 1300.f);
			}
			Lured->GetHealthComponent()->SetMaxHealth(20.f);
			Next(State);
			return true;
		}
		case 11:
		{
			if (State.StageTime < 0.9f)
			{
				return true; // the frame of the lured zombie first
			}
			if (State.StageTime < 1.6f)
			{
				PointCamera(World, State, (Lured->GetActorLocation() + Medic->GetActorLocation()) * 0.5f, 1100.f); // she aims: the camera settles
				return true;
			}
			if (!IsDead(Lured))
			{
				const bool bFirstShot = State.LastShotAt < 0.f;
				FireAt(State, *Medic, *Lured);
				if (bFirstShot)
				{
					RequestFrame(World, State, FVector::ZeroVector, TEXT("part2_close_shot"), 0.f, 0.f, false); // the frame of the shot itself
				}
				if (State.StageTime < 15.f)
				{
					return true;
				}
			}
			if (State.StageTime < 2.2f)
			{
				return true;
			}
			Check(State, IsDead(Lured), TEXT("she shoots the far zombie dead at close range"));
			Check(State, Tutorial->GetStepState(TEXT("A4_close_shot")) == ETutorialStepState::Completed,
				FString::Printf(TEXT("PART 2: the close shot in the open completes the card (%s)"), TutorialRules::StateName(Tutorial->GetStepState(TEXT("A4_close_shot")))));
			Check(State, Tutorial->GetStepState(TEXT("A4_close_shot_far")) == ETutorialStepState::Skipped,
				FString::Printf(TEXT("the 'not up close' line never shows (%s)"), TutorialRules::StateName(Tutorial->GetStepState(TEXT("A4_close_shot_far")))));
			Check(State, World->GetSubsystem<UGameFlowSubsystem>()->GetPhase() == ECodexGamePhase::Exploration && !State.bSawWavePanel,
				TEXT("the last skirmish ended straight in exploration - never a wave panel / post-combat"));
			Next(State);
			return true;
		}
		// --- PART 2's approach along the near container (2026-10-21 b) ---
		case 40:
		{
			if (Tutorial->GetStepState(TEXT("A4_hug_wall")) != ETutorialStepState::Active && State.StageTime < 8.f)
			{
				return true;
			}
			Medic = Medic ? Medic : Tutorial->GetFocusOperative(TEXT("A4_hug_wall"));
			State.Medic = Medic;
			Tutorial->RefreshGuideNow();
			Check(State, Tutorial->GetStepState(TEXT("A4_hug_wall")) == ETutorialStepState::Active && Tutorial->GetGuideView().bValid
				&& Tutorial->GetGuideView().Guide.Id == FName(TEXT("@patrol_route.A4_PERIMETER")),
				FString::Printf(TEXT("PART 2 starts with the wall card, its target at the near container (%s)"), *UTutorialSubsystem::DescribeGuide(Tutorial->GetGuideView().Guide)));
			if (State.Mode == EMode::SpottedPart2)
			{
				State.Stage = 30; // the far zombie spots her on her way to the wall
				State.StageTime = 0.f;
				return true;
			}
			// The REAL cover order at the near container's face towards the overlook.
			const AStaticMeshActor* Container = nullptr;
			for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
			{
				Container = It->GetActorNameOrLabel() == TEXT("Container_A4_South") ? *It : Container;
			}
			FCoverSlot Slot;
			bool bOrdered = false;
			if (Container && Medic)
			{
				// Standing: crouched right behind the overlook's low cover she stood frozen against it in the failure run (the navmesh path
				// crosses the low cover and the crouched vault did not trigger - reported).
				Medic->SetStance(EOperativeStance::Standing);
				const FBox Box = Container->GetComponentsBoundingBox();
				const FVector Outside(Box.Min.X - 30.f, Box.GetCenter().Y, Box.GetCenter().Z);
				const FVector Face = Ground(World, Outside) + FVector(0.f, 0.f, 60.f); // the ground beside the wall, not the container's roof
				const bool bSlot = CoverTraceRules::FindCoverSlotAt(World, Face, FVector(1.f, 0.f, 0.f), Slot);
				State.ContainerSlot = Slot;
				const EOperativeOrderResult Result = bSlot ? Medic->OrderTakeCover(Slot, false) : EOperativeOrderResult::Refused;
				bOrdered = Result == EOperativeOrderResult::Accepted;
				UE_LOG(LogCodexTactics, Display, TEXT("[AmbushLake] cover at the near container: face %s, slot %d, order %d"), *Face.ToCompactString(), bSlot ? 1 : 0,
					static_cast<int32>(Result));
			}
			Check(State, bOrdered, TEXT("she runs into cover at the near container's wall (the real cover order)"));
			Next(State);
			return true;
		}
		case 41:
		{
			// A cover walk given up right after the fight (her shot's clip still holds her): the player clicks again.
			if (!Medic->bInCover && Medic->GetVelocity().Size2D() < 5.f && State.StageTime > 2.f && State.CoverRetries < 3
				&& Tutorial->GetStepState(TEXT("A4_hug_wall")) == ETutorialStepState::Active && FMath::Fmod(State.StageTime, 2.f) < StepSeconds)
			{
				++State.CoverRetries;
				// She stands against the overlook's low cover (the navmesh path runs straight across it and no vault came): round its end
				// first, as the player would, then the cover click again (stage 45).
				UE_LOG(LogCodexTactics, Display, TEXT("[AmbushLake] she stands still at %s short of the container (path across the overlook's low cover): round its end, click again (%d)"),
					*Medic->GetActorLocation().ToCompactString(), State.CoverRetries);
				if (State.CoverRetries >= 2)
				{
					// Still frozen against the low cover (her crowd agent off the eroded navmesh there - reported): a step back onto it.
					Place(World, *Medic, State.CoverSpot);
				}
				Medic->OrderMoveTo(SmokeUtils::ClearPoint(World, Medic->GetActorLocation(), Medic->GetActorLocation() + FVector(0.f, -450.f, 0.f)), false);
				State.Stage = 45;
				State.StageTime = 0.f;
				return true;
			}
			if ((Tutorial->GetStepState(TEXT("A4_hug_wall")) == ETutorialStepState::Active || Tutorial->GetStepState(TEXT("A4_round_corner")) != ETutorialStepState::Active)
				&& State.StageTime < 30.f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("A4_hug_wall")) == ETutorialStepState::Completed && Medic->bInCover
				&& Tutorial->GetStepState(TEXT("A4_round_corner")) == ETutorialStepState::Active,
				FString::Printf(TEXT("in cover at the container (CoverEntered): the wall card completes, the round-the-corner card (%.1f s)"), State.StageTime));
			RequestFrame(World, State, Medic->GetActorLocation(), TEXT("part2_wall_hug"), 1300.f);
			Next(State);
			return true;
		}
		case 45:
		{
			if (Medic->GetVelocity().Size2D() > 5.f || State.StageTime < 1.f)
			{
				if (State.StageTime < 6.f)
				{
					return true;
				}
			}
			Medic->OrderTakeCover(State.ContainerSlot, false);
			State.Stage = 41;
			State.StageTime = 0.f;
			return true;
		}
		case 42:
		{
			if (State.ShotAt >= 0.f || State.StageTime < 0.5f)
			{
				return true; // the frame first
			}
			// The REAL wall-following: a click on the ground beyond the container's far face (the controller's click path).
			ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController());
			const AStaticMeshActor* Container = nullptr;
			for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
			{
				Container = It->GetActorNameOrLabel() == TEXT("Container_A4_South") ? *It : Container;
			}
			bool bWrap = false;
			if (PC && Container)
			{
				const FBox Box = Container->GetComponentsBoundingBox();
				bWrap = PC->TryOrderCoverWrap(Medic, Ground(World, FVector(Box.Max.X + 70.f, Box.GetCenter().Y, Box.GetCenter().Z)));
			}
			Check(State, bWrap && Tutorial->GetStepState(TEXT("A4_round_corner")) == ETutorialStepState::Completed,
				FString::Printf(TEXT("she follows the container's wall round its corners (the real wall-following click) -> the card completes (%s)"),
					TutorialRules::StateName(Tutorial->GetStepState(TEXT("A4_round_corner")))));
			Next(State);
			return true;
		}
		case 43:
		{
			if (Medic->IsCoverWrapping() && State.StageTime < 20.f)
			{
				return true; // she rounds the container in cover
			}
			Check(State, !Medic->IsCoverWrapping() && Medic->GetCoverWrapCount() >= 1 && Medic->bInCover,
				FString::Printf(TEXT("she rounded the container along its wall, in cover (%d wrap(s), %.1f s)"), Medic->GetCoverWrapCount(), State.StageTime));
			State.Stage = 6;
			State.StageTime = 0.f;
			return true;
		}
		case 44:
		{
			if (FVector::Dist2D(Medic->GetActorLocation(), State.LureSpot) > 150.f && State.StageTime < 25.f)
			{
				if (Medic->GetVelocity().Size2D() < 5.f && FMath::Fmod(State.StageTime, 2.f) < StepSeconds && State.StageTime > 1.f)
				{
					Medic->OrderMoveTo(State.LureSpot, false);
				}
				return true;
			}
			if (FVector::Dist2D(Medic->GetActorLocation(), State.LureSpot) > 150.f)
			{
				Place(World, *Medic, State.LureSpot); // the walk did not get there: put her there (the lure is the point here)
			}
			Medic->SetStance(EOperativeStance::Crouching);
			State.Stage = 7;
			State.StageTime = 0.f;
			return true;
		}
		// --- PART 1 FAILS (TutorialAmbushLakeSpottedSmoke) ---
		case 20:
		{
			// She sets off towards his loop (the wire spot) - and he spots her on the way (the user's play-test).
			if (State.StageTime < StepSeconds * 1.5f)
			{
				const TArray<FVector> Loop = RoutePoints(*Perimeter->AssignedPatrolRoute);
				const FVector OnLoop = TutorialRules::NearestPointOnRoute(Loop, true, Medic->GetActorLocation());
				Medic->SetStance(EOperativeStance::Standing);
				Medic->OrderMoveTo(SmokeUtils::ClearPoint(World, Medic->GetActorLocation(), OnLoop + (Medic->GetActorLocation() - OnLoop).GetSafeNormal2D() * 300.f), false);
				return true;
			}
			if (FVector::Dist2D(Medic->GetActorLocation(), State.CoverSpot) < 350.f && State.StageTime < 8.f)
			{
				return true; // on her way, clear of the overlook's low cover
			}
			State.BeforeKill = Medic->GetActorLocation();
			Squad->SetSquadPosture(ESquadFirePosture::Passive); // 2026-10-22: the SPOTTED card is for a squad the player made PASSIVE
			Perimeter->BreakPatrol(EPatrolAlertCause::Sight, Medic->GetActorLocation()); // he sees her: the patrol breaks (the real reaction)
			Next(State);
			return true;
		}
		case 21:
		{
			const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
			if ((Tutorial->GetStepState(TEXT("A4_spotted")) != ETutorialStepState::Active || Flow->GetCombatMode() != ECodexCombatMode::TacticalPause)
				&& State.StageTime < 5.f)
			{
				return true;
			}
			Check(State, State.Probe->EventNames.Contains(GameplayEvents::EnemyAlerted), TEXT("EnemyAlerted raised by the perimeter zombie"));
			Check(State, Tutorial->GetStepState(TEXT("A4_tripwire")) == ETutorialStepState::Skipped, TEXT("PART 1 FAILS: the wire card is dropped"));
			Check(State, Tutorial->GetStepState(TEXT("A4_spotted")) == ETutorialStepState::Active && Flow->GetCombatMode() == ECodexCombatMode::TacticalPause
				&& Flow->IsFreePause(), TEXT("... the SPOTTED card and the FREE tactical pause"));
			UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
			Check(State, Flow->IsSkirmish() && Waves && Waves->GetAliveEnemyCount() == 1 && Lured->IsOnPatrol(),
				FString::Printf(TEXT("... a SKIRMISH of that zombie alone (%d enemies); the far zombie keeps patrolling"), Waves ? Waves->GetAliveEnemyCount() : -1));
			RequestFrame(World, State, (Medic->GetActorLocation() + Perimeter->GetActorLocation()) * 0.5f, TEXT("part1_spotted_pause"), 1500.f);
			Next(State);
			return true;
		}
		case 22:
		{
			if (State.ShotAt >= 0.f || State.StageTime < 0.5f || State.Time < State.FrameTakenAt + 0.3f)
			{
				return true; // the frame of the paused moment first (rendered before the posture key)
			}
			Tutorial->HandlePlayerCommand(PlayerCommands::PostureAggressive, FPlayerCommandContext());
			Squad->SetSquadPosture(ESquadFirePosture::Aggressive); // Alt + /
			Check(State, Tutorial->GetStepState(TEXT("A4_spotted")) == ETutorialStepState::Completed, TEXT("AGGRESSIVE (Alt + /) completes the card"));
			State.Stage = 50; // 2026-10-22: the tactical pause lesson before Space
			State.StageTime = 0.f;
			return true;
		}
		// --- 2026-10-22: the TACTICAL PAUSE lesson (the first pause of the level) ---
		case 50:
		{
			const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
			if (Tutorial->GetStepState(TEXT("X_pause")) != ETutorialStepState::Active && State.StageTime < 4.f)
			{
				return true;
			}
			const FTutorialCardView Card = Tutorial->GetCardView();
			Check(State, Tutorial->GetStepState(TEXT("X_pause")) == ETutorialStepState::Active && Flow->GetCombatMode() == ECodexCombatMode::TacticalPause
				&& Card.Title.ToString().Contains(TEXT("TACTICAL PAUSE")), FString::Printf(TEXT("PAUSE LESSON 1/2 in the pause: '%s' (%.1f s after the spotted card)"),
					*Card.Title.ToString(), State.StageTime));
			RequestFrame(World, State, Medic->GetActorLocation(), TEXT("pause_lesson_1_orders"), 1500.f, 0.6f, true);
			Next(State);
			return true;
		}
		case 51:
		{
			if (State.ShotAt >= 0.f || State.Time < State.FrameTakenAt + 0.4f)
			{
				return true;
			}
			// Two orders in the pause (the real reports: a move, a targeted shot) -> card 2.
			Tutorial->HandlePlayerCommand(PlayerCommands::Move, FPlayerCommandContext());
			Tutorial->HandlePlayerCommand(PlayerCommands::TargetedShot, FPlayerCommandContext());
			Check(State, Tutorial->GetStepState(TEXT("X_pause")) == ETutorialStepState::Completed, TEXT("two orders in the pause complete card 1"));
			Next(State);
			return true;
		}
		case 52:
		{
			if (Tutorial->GetStepState(TEXT("X_pause_resume")) != ETutorialStepState::Active && State.StageTime < 4.f)
			{
				return true;
			}
			const FTutorialCardView Card = Tutorial->GetCardView();
			const FString Body = Card.Body.ToString();
			Check(State, Tutorial->GetStepState(TEXT("X_pause_resume")) == ETutorialStepState::Active && Body.Contains(TEXT("pauses per wave"))
				&& !Body.Contains(TEXT("{pause_")), FString::Printf(TEXT("PAUSE LESSON 2/2: resume + the limits, tokens resolved ('%s')"), *Body.Left(160)));
			RequestFrame(World, State, Medic->GetActorLocation(), TEXT("pause_lesson_2_resume"), 1500.f, 0.6f, true);
			Next(State);
			return true;
		}
		case 53:
		{
			if (State.ShotAt >= 0.f || State.Time < State.FrameTakenAt + 0.4f)
			{
				return true;
			}
			UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
			Check(State, Flow->ToggleTacticalPause() == EGameFlowResult::Ok && Flow->GetCombatMode() == ECodexCombatMode::RealTime, TEXT("Space resumes the fight"));
			Tutorial->HandlePlayerCommand(PlayerCommands::PauseRelease, FPlayerCommandContext()); // what the controller reports for the Space tap
			Check(State, Tutorial->GetStepState(TEXT("X_pause_resume")) == ETutorialStepState::Completed, TEXT("the resume (Space) completes card 2"));
			Perimeter->GetHealthComponent()->SetMaxHealth(20.f);
			State.LastShotAt = -10.f;
			State.Stage = 23;
			State.StageTime = 0.f;
			return true;
		}
		case 23:
		{
			// The fire support kills him (2026-10-21: after the Medic-Sapper's own sniper shots she stood frozen on the next walk in this smoke - see the report).
			AOperativeCharacter* Support = FireSupport(*Squad, Medic);
			if (!IsDead(Perimeter) && Support)
			{
				FireAt(State, *Support, *Perimeter);
				if (State.StageTime < 20.f)
				{
					return true;
				}
			}
			State.BeforeKill = Medic->GetActorLocation();
			Next(State);
			return true;
		}
		case 24:
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
			Check(State, IsDead(Perimeter), TEXT("the fire support shoots the perimeter zombie dead"));
			Check(State, Flow->GetPhase() == ECodexGamePhase::Exploration && !Flow->IsSkirmish() && !State.bSawWavePanel && Flow->GetWaveIndex() == 0,
				TEXT("the skirmish is over: straight back to exploring - no wave cleared panel, no post-combat, no wave"));
			Check(State, Lured && !IsDead(Lured) && Lured->IsOnPatrol() && FVector::Dist2D(Medic->GetActorLocation(), State.BeforeKill) < 300.f,
				TEXT("... the far zombie is still there and patrolling, the squad was not teleported"));
			Check(State, Tutorial->GetStepState(TEXT("A4_trap_wait")) == ETutorialStepState::Skipped, TEXT("part 1 is over (its zombie died another way)"));
			RequestFrame(World, State, Lured->GetActorLocation(), TEXT("part1_skirmish_over_far_zombie_patrolling"), 1800.f, 0.7f, true, Lured);

			Squad->SetSquadPosture(ESquadFirePosture::Defensive); // 2026-10-22: back to the lesson posture
			Next(State);
			return true;
		}
		case 25:
		{
			if (State.ShotAt >= 0.f || State.bRestoreFollow)
			{
				return true;
			}
			State.Stage = 40; // the game goes on: part 2
			State.StageTime = 0.f;
			return true;
		}
		// --- 2026-10-22: TutorialAmbushLakeEarlyKillSmoke ---
		case 60:
		{
			// The level's traps (what the wire card meets, what the horde prep can use) and the squad's kit, for the report.
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			FNavLocation OnNav;
			if ((!Tutorial->GetMachine().IsLoaded() || Squad->GetMembers().Num() < 3
				|| !(Nav && Nav->ProjectPointToNavigation(Leader->GetActorLocation(), OnNav, FVector(60.f, 60.f, 200.f)))) && State.StageTime < 120.f)
			{
				return true;
			}
			if (USaveGameSubsystem* Saves = World->GetSubsystem<USaveGameSubsystem>())
			{
				Saves->bCheckpointsEnabled = false;
			}
			ATripwireActor* First = nullptr;
			for (TActorIterator<ATripwireActor> It(World); It; ++It)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[AmbushLake] level wire %s at %s: %s, hidden until spotted %d, spotted %d, armed %d, %.0f m from the squad"), *It->GetName(),
					*It->GetActorLocation().ToCompactString(), It->IsHostile() ? TEXT("PMC") : TEXT("squad"), It->bHiddenUntilSpotted ? 1 : 0, It->IsSpotted() ? 1 : 0,
					It->IsArmed() ? 1 : 0, FVector::Dist2D(It->GetActorLocation(), Leader->GetActorLocation()) / 100.f);
				if (It->IsHostile() && It->IsArmed() && (!First || FVector::Dist2D(It->GetActorLocation(), Leader->GetActorLocation())
					< FVector::Dist2D(First->GetActorLocation(), Leader->GetActorLocation())))
				{
					First = *It;
				}
			}
			int32 PickupMines = 0;
			for (TActorIterator<AProximityMineActor> It(World); It; ++It)
			{
				PickupMines += !It->bTrapped && It->bDeployable ? 1 : 0;
				UE_LOG(LogCodexTactics, Display, TEXT("[AmbushLake] level mine %s at %s: live %d, revealed %d, deployable (into the kit when disarmed / picked up) %d, squad's %d, spot step '%s', tag '%s'"),
					*It->GetName(), *It->GetActorLocation().ToCompactString(), It->bTrapped ? 1 : 0, It->IsRevealed() ? 1 : 0, It->bDeployable ? 1 : 0,
					It->bPlacedBySquad ? 1 : 0, *It->SpotRequiresStep.ToString(), *It->SpotEventTag.ToString());
			}
			int32 OtherDeployables = 0;
			for (TActorIterator<ADeployableActor> It(World); It; ++It)
			{
				if (!It->IsA<AProximityMineActor>())
				{
					++OtherDeployables;
					UE_LOG(LogCodexTactics, Display, TEXT("[AmbushLake] level deployable %s (%s) at %s"), *It->GetName(), *It->GetClass()->GetName(), *It->GetActorLocation().ToCompactString());
				}
			}
			for (const AOperativeCharacter* Member : Squad->GetMembers())
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[AmbushLake] kit %s: mines %d, grenades %d, barricades %d, turrets %d"), *Member->DisplayName.ToString(), Member->MinesCount,
					Member->GrenadesCount, Member->BarricadesCount, Member->TurretsCount);
			}
			UE_LOG(LogCodexTactics, Display, TEXT("[AmbushLake] lake traps: %d mine(s) lying around to pick up (not live), %d other deployable(s) placed"), PickupMines, OtherDeployables);
			Check(State, First != nullptr, TEXT("the lake has a PMC wire"));
			if (!First)
			{
				return Finish(State, false);
			}
			State.PmcWire = First;
			// Medic-Sapper crouched 4 m from the wire's middle, on the squad's side: she spots / notices it (6 m).
			AOperativeCharacter* Spotter = nullptr;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Spotter = Member->SquadRole == EOperativeRole::MedicSapper ? Member : Spotter;
			}
			const FVector Mid = First->GetActorLocation();
			const FVector Away = (Leader->GetActorLocation() - Mid).GetSafeNormal2D();
			const FVector Spot = SmokeUtils::FreeSpot(World, Ground(World, Mid + Away * 400.f), Spotter);
			if (Spotter)
			{
				Spotter->SetStance(EOperativeStance::Crouching);
				Place(World, *Spotter, Spot);
			}
			Next(State);
			return true;
		}
		case 61:
		{
			if (Tutorial->GetStepState(TEXT("A3_wire")) != ETutorialStepState::Active && State.StageTime < 8.f)
			{
				return true;
			}
			const FTutorialCardView Card = Tutorial->GetCardView();
			Check(State, Tutorial->GetStepState(TEXT("A3_wire")) == ETutorialStepState::Active && Card.Title.ToString().Contains(TEXT("TRIPWIRE"))
				&& Card.Body.ToString().Contains(TEXT("grenade")), FString::Printf(TEXT("the first PMC wire met: the TRIPWIRE card shows ('%s', beat %s, %.1f s)"),
					*Card.Title.ToString(), *Tutorial->GetCurrentBeat().ToString(), State.StageTime));
			if (ATripwireActor* Wire = State.PmcWire.Get())
			{
				AOperativeCharacter* Spotter = nullptr;
				for (AOperativeCharacter* Member : Squad->GetMembers())
				{
					Spotter = Member->SquadRole == EOperativeRole::MedicSapper ? Member : Spotter;
				}
				RequestFrame(World, State, Wire->GetActorLocation(), TEXT("tripwire_card"), 1300.f, 1.2f, true, Spotter); // the camera on her, the wire in front
			}
			Next(State);
			return true;
		}
		case 62:
		{
			if (State.ShotAt >= 0.f || State.Time < State.FrameTakenAt + 0.4f)
			{
				return true;
			}
			Tutorial->SkipStep(TEXT("A3_wire")); // gone round it: on to the overlook
			Squad->ExitSoloMode();
			State.Stage = 0;
			State.StageTime = 0.f;
			return true;
		}
		case 70:
		{
			// Both zombies die while the wire card is up (shot from the overlook, a mine - any way).
			for (AEnemyCharacter* Zombie : { Perimeter, Lured })
			{
				FDamageSpec Spec;
				Spec.Amount = 100000.f;
				Spec.ArmorPenetration = 1.f;
				Zombie->GetHealthComponent()->TakeDamage(Spec);
			}
			Next(State);
			return true;
		}
		case 71:
		{
			if (TutorialRules::IsRunning(Tutorial->GetStepState(TEXT("A4_tripwire"))) && State.StageTime < 3.f)
			{
				return true;
			}
			Check(State, IsDead(Perimeter) && IsDead(Lured), TEXT("both A4 zombies are dead"));
			for (const TCHAR* Id : { TEXT("A4_tripwire"), TEXT("A4_trap_wait"), TEXT("A4_hug_wall"), TEXT("A4_round_corner"), TEXT("A4_lure") })
			{
				Check(State, Tutorial->GetStepState(Id) == ETutorialStepState::Skipped, FString::Printf(TEXT("both dead: %s closed (%s)"), Id,
					TutorialRules::StateName(Tutorial->GetStepState(Id))));
			}
			Check(State, Tutorial->GetStepState(TEXT("A4_close_shot")) == ETutorialStepState::Idle && Tutorial->GetStepState(TEXT("A4_spotted")) != ETutorialStepState::Active,
				TEXT("... no close-shot / spotted card"));
			Check(State, !Tutorial->IsConfining(), TEXT("... the squad is no longer held at the overlook"));
			Tutorial->RefreshGuideNow();
			const FTutorialGuideView& Guide = Tutorial->GetGuideView();
			const FString GuideName = UTutorialSubsystem::DescribeGuide(Guide.Guide);
			Check(State, !Guide.Guide.Id.ToString().StartsWith(TEXT("@")), FString::Printf(TEXT("... the current target is no longer the dead zombie's loop (%s)"), *GuideName));
			const FTutorialCardView Card = Tutorial->GetCardView();
			const TArray<FName> ZombieCards = { TEXT("A4_tripwire"), TEXT("A4_hug_wall"), TEXT("A4_round_corner"), TEXT("A4_lure"), TEXT("A4_close_shot"), TEXT("A4_spotted") };
			Check(State, !Card.bVisible || !ZombieCards.Contains(Card.StepId), FString::Printf(TEXT("... no card about the dead zombies on screen (%s)"),
				Card.bVisible ? *Card.StepId.ToString() : TEXT("none")));
			// The save remembers that these bindings had their zombies here (a load must not bring the lessons back for the dead).
			const TSharedPtr<FJsonObject> Saved = Tutorial->CaptureSaveState();
			const TArray<TSharedPtr<FJsonValue>>* Seen = nullptr;
			TArray<FString> SeenNames;
			if (Saved.IsValid() && Saved->TryGetArrayField(TEXT("enemy_bindings_seen"), Seen))
			{
				for (const TSharedPtr<FJsonValue>& Value : *Seen)
				{
					SeenNames.Add(Value->AsString());
				}
			}
			Check(State, SeenNames.Contains(TEXT("A4_PERIMETER")) && SeenNames.Contains(TEXT("A4_LURE")),
				FString::Printf(TEXT("... the save's tutorial section keeps enemy_bindings_seen (%s)"), *FString::Join(SeenNames, TEXT(", "))));
			RequestFrame(World, State, State.CoverSpot, TEXT("both_zombies_dead_no_stale_marker"), 2200.f);
			Next(State);
			return true;
		}
		case 72:
		{
			if (State.ShotAt >= 0.f || State.Time < State.FrameTakenAt + 0.4f)
			{
				return true;
			}
			// On to the compound: the yard starts A7; the quest steps up to the generator (the events the quest raises).
			ATutorialTriggerVolume* Yard = nullptr;
			for (TActorIterator<ATutorialTriggerVolume> It(World); It; ++It)
			{
				Yard = It->GetEffectiveVolumeId() == FName(TEXT("VOL_A7_YARD")) ? *It : Yard;
			}
			Check(State, Yard != nullptr, TEXT("the yard volume (A7)"));
			if (!Yard)
			{
				return Finish(State, false);
			}
			Tutorial->NotifyVolumeEntered(Yard, Leader);
			for (const TCHAR* Kind : { TEXT("CanisterTaken"), TEXT("FuelDrained"), TEXT("GeneratorStarted") })
			{
				Tutorial->NotifyEvent(FName(*(FString(TEXT("QuestStep.")) + Kind)), Leader->GetFName());
				Tutorial->NotifyEvent(GameplayEvents::QuestStep, Leader->GetFName());
			}
			Next(State);
			return true;
		}
		case 73:
		{
			if (Tutorial->GetStepState(TEXT("A7_prep")) != ETutorialStepState::Active && State.StageTime < 12.f)
			{
				return true;
			}
			Check(State, Tutorial->GetCurrentBeat() == FName(TEXT("A7")) && Tutorial->GetStepState(TEXT("A7_prep")) == ETutorialStepState::Active,
				FString::Printf(TEXT("A7: the generator runs -> the console card (beat %s)"), *Tutorial->GetCurrentBeat().ToString()));
			Tutorial->SkipStep(TEXT("A7_prep"));
			Next(State);
			return true;
		}
		case 74:
		{
			if (Tutorial->GetStepState(TEXT("A7_traps")) != ETutorialStepState::Active && State.StageTime < 5.f)
			{
				return true;
			}
			const FTutorialCardView Card = Tutorial->GetCardView();
			Check(State, Tutorial->GetStepState(TEXT("A7_traps")) == ETutorialStepState::Active && Card.Title.ToString().Contains(TEXT("PREPARE THE GROUND")),
				FString::Printf(TEXT("then the horde prep hint: '%s'"), *Card.Title.ToString()));
			RequestFrame(World, State, Leader->GetActorLocation(), TEXT("a7_prepare_the_ground"), 1800.f);
			State.Stage = 12; // the last frame, then finish
			State.StageTime = 0.f;
			return true;
		}
		// --- PART 2 FAILS (TutorialAmbushLakeLureSpottedSmoke) ---
		case 30:
		{
			Squad->SetSquadPosture(ESquadFirePosture::Passive); // 2026-10-22: the SPOTTED card is for a squad the player made PASSIVE
			Lured->BreakPatrol(EPatrolAlertCause::Sight, Medic->GetActorLocation());
			Next(State);
			return true;
		}
		case 31:
		{
			const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
			if ((Tutorial->GetStepState(TEXT("A4_spotted")) != ETutorialStepState::Active || Flow->GetCombatMode() != ECodexCombatMode::TacticalPause)
				&& State.StageTime < 5.f)
			{
				return true;
			}
			UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
			Check(State, Tutorial->GetStepState(TEXT("A4_hug_wall")) == ETutorialStepState::Skipped, TEXT("PART 2 FAILS: the wall card is dropped"));
			Check(State, Tutorial->GetStepState(TEXT("A4_spotted")) == ETutorialStepState::Active && Flow->IsFreePause(),
				TEXT("... the SPOTTED card and the free tactical pause (the first time on this level)"));
			Check(State, Flow->IsSkirmish() && Waves && Waves->GetAliveEnemyCount() == 1 && (IsDead(Perimeter) || Perimeter->IsOnPatrol()),
				TEXT("... a skirmish of the far zombie alone (the perimeter zombie died in the trap - 2026-10-23 - or patrols on)"));
			Tutorial->HandlePlayerCommand(PlayerCommands::PostureDefensive, FPlayerCommandContext());
			Squad->SetSquadPosture(ESquadFirePosture::Defensive);
			Check(State, Tutorial->GetStepState(TEXT("A4_spotted")) == ETutorialStepState::Completed, TEXT("DEFENSIVE completes the card"));
			World->GetSubsystem<UGameFlowSubsystem>()->ToggleTacticalPause();
			Lured->GetHealthComponent()->SetMaxHealth(20.f);
			State.LastShotAt = -10.f;
			Next(State);
			return true;
		}
		case 32:
		{
			AOperativeCharacter* Support = FireSupport(*Squad, Medic);
			if (!IsDead(Lured) && Support)
			{
				FireAt(State, *Support, *Lured);
				if (State.StageTime < 20.f)
				{
					return true;
				}
			}
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
			Check(State, IsDead(Lured), TEXT("the fire support shoots the far zombie dead"));
			Check(State, Flow->GetPhase() == ECodexGamePhase::Exploration && !State.bSawWavePanel, TEXT("straight back to exploring - no wave panel"));
			Check(State, Tutorial->GetStepState(TEXT("A4_round_corner")) == ETutorialStepState::Skipped && Tutorial->GetStepState(TEXT("A4_lure")) == ETutorialStepState::Skipped
				&& Tutorial->GetStepState(TEXT("A4_close_shot")) == ETutorialStepState::Idle,
				TEXT("the stealth cards skipped themselves in the fight (no corner / lure / close-shot card) - the game goes on"));
			return Finish(State, true);
		}
		case 12:
		{
			if (State.ShotAt >= 0.f || State.StageTime < 0.5f || State.Time < State.FrameTakenAt + 0.5f)
			{
				return true; // the last frame (rendered before the exit)
			}
			return Finish(State, true);
		}
		default:
			return Finish(State, false);
		}
	}

	void RunMode(const TArray<FString>& Args, UWorld* World, EMode Mode)
	{
		if (!World)
		{
			return;
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		State->Mode = Mode;
		State->Stage = Mode == EMode::EarlyKill ? 60 : 0; // 2026-10-22: the wire card first
		State->bShots = Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("Shot"), ESearchCase::IgnoreCase); }) && FApp::CanEverRender();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		RunMode(Args, World, EMode::Success);
	}

	void RunSpotted(const TArray<FString>& Args, UWorld* World)
	{
		RunMode(Args, World, EMode::SpottedPart1);
	}

	void RunLureSpotted(const TArray<FString>& Args, UWorld* World)
	{
		RunMode(Args, World, EMode::SpottedPart2);
	}

	void RunEarlyKill(const TArray<FString>& Args, UWorld* World)
	{
		RunMode(Args, World, EMode::EarlyKill);
	}

	void RunMine(const TArray<FString>& Args, UWorld* World)
	{
		RunMode(Args, World, EMode::MineKill);
	}

	static FAutoConsoleCommandWithWorldAndArgs MineCommand(
		TEXT("CodexTactics.TutorialAmbushLakeMineSmoke"),
		TEXT("Dev check (Demo 01 A4, 2026-10-23): part 1 by a MINE - zombie #1 dormant until the squad is in cover at the overlook, wakes hidden on its loop, the Engineer (carrying a mine: the card names it) plants it on the loop, the zombie dies in it (EnemyKilled.SquadTrap); part 2 as usual."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunMine));

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.TutorialAmbushLakeSmoke"),
		TEXT("Dev check (Demo 01 A4, 2026-10-21): the ambush in two parts on the lake layout - part 1 the perimeter zombie walks into a real wire, part 2 the far zombie is lured by a real whistle into the open and shot dead at close range; each part reacts only to its zombie. 'Shot' = rendered frames."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));

	static FAutoConsoleCommandWithWorldAndArgs SpottedCommand(
		TEXT("CodexTactics.TutorialAmbushLakeSpottedSmoke"),
		TEXT("Dev check (Demo 01 A4, 2026-10-21 b): part 1 fails - the perimeter zombie spots the squad: the SPOTTED card + free pause, a skirmish of that zombie alone, straight back to exploring, part 2 goes on. 'Shot' = rendered frames."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunSpotted));

	static FAutoConsoleCommandWithWorldAndArgs LureSpottedCommand(
		TEXT("CodexTactics.TutorialAmbushLakeLureSpottedSmoke"),
		TEXT("Dev check (Demo 01 A4, 2026-10-21 b): part 2 fails - the far zombie spots her at the wall: the SPOTTED card + pause, the stealth cards skip, a skirmish of that zombie alone, straight back to exploring."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunLureSpotted));

	static FAutoConsoleCommandWithWorldAndArgs EarlyKillCommand(
		TEXT("CodexTactics.TutorialAmbushLakeEarlyKillSmoke"),
		TEXT("Dev check (Demo 01, 2026-10-22): the first PMC wire shows the TRIPWIRE card; both A4 zombies killed while the wire card is up close every A4 step bound to them (no stale target); A7 shows PREPARE THE GROUND after the console card. 'Shot' = rendered frames."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunEarlyKill));
}

#endif // !UE_BUILD_SHIPPING
