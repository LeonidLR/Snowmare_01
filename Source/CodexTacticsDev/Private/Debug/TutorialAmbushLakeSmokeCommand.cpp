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
#include "Core/PlayerCommands.h"
#include "Core/SaveGameSubsystem.h"
#include "Debug/EventBusProbe.h"
#include "Debug/SmokeUtils.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
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

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		bool bShots = false;
		int32 ShotIndex = 0;
		/** A requested frame is taken this many seconds after the camera was pointed (0 = none pending). */
		float ShotAt = -1.f;
		FString ShotName;
		TWeakObjectPtr<AEnemyCharacter> Perimeter;
		TWeakObjectPtr<AEnemyCharacter> Lured;
		TWeakObjectPtr<AOperativeCharacter> Medic;
		TWeakObjectPtr<ATripwireActor> Wire;
		FVector WireA = FVector::ZeroVector;
		FVector WireB = FVector::ZeroVector;
		FVector CoverSpot = FVector::ZeroVector;
		bool bPart1Shot = false;
		bool bOpenShot = false;
		float LastShotAt = -10.f;
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
	void RequestFrame(UWorld* World, FState& State, const FVector& Where, const TCHAR* Name, float Distance = 1700.f, float Delay = 0.7f, bool bFocus = true)
	{
		if (!State.bShots || State.ShotAt >= 0.f)
		{
			return;
		}
		if (bFocus)
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
		if (State.Time > 400.f)
		{
			Check(State, false, FString::Printf(TEXT("time out in stage %d"), State.Stage));
			return Finish(State, false);
		}
		AEnemyCharacter* Perimeter = State.Perimeter.Get();
		AEnemyCharacter* Lured = State.Lured.Get();
		AOperativeCharacter* Medic = State.Medic.Get();
		// Cards that queue in between (the overlook posture twin behind the wire card, the out-of-sight card): the player does what they ask.
		if (Tutorial->GetStepState(TEXT("A4_posture")) == ETutorialStepState::Active)
		{
			Tutorial->HandlePlayerCommand(PlayerCommands::PosturePassive, FPlayerCommandContext());
			Squad->SetSquadPosture(ESquadFirePosture::Passive);
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
			Perimeter->GetHealthComponent()->SetMaxHealth(1000.f); // it survives the wire (140 damage): shot later (the negative check of part 2)
			Squad->SetSquadPosture(ESquadFirePosture::Passive);
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
				Tutorial->HandlePlayerCommand(PlayerCommands::PosturePassive, FPlayerCommandContext());
				Squad->SetSquadPosture(ESquadFirePosture::Passive);
			}
			Next(State);
			return true;
		}
		case 3:
		{
			if (Tutorial->GetStepState(TEXT("A4_tripwire")) != ETutorialStepState::Active && State.StageTime < 8.f)
			{
				return true;
			}
			Medic = Tutorial->GetFocusOperative(TEXT("A4_tripwire"));
			State.Medic = Medic;
			Check(State, Tutorial->GetStepState(TEXT("A4_tripwire")) == ETutorialStepState::Active && Medic && Medic->SquadRole == EOperativeRole::MedicSapper
				&& Squad->IsSoloMode() && Squad->GetLeader() == Medic && Tutorial->IsConfining(),
				TEXT("PART 1: the tripwire card - the Medic-Sapper alone (Solo, selected), the other two held"));
			if (!Medic || !Perimeter->AssignedPatrolRoute)
			{
				return Finish(State, false);
			}
			const TArray<FVector> Loop = RoutePoints(*Perimeter->AssignedPatrolRoute);
			const FVector OnLoop = TutorialRules::NearestPointOnRoute(Loop, Perimeter->AssignedPatrolRoute->bIsLoop, Medic->GetActorLocation());
			Tutorial->RefreshGuideNow();
			const FTutorialGuideView& Guide = Tutorial->GetGuideView();
			const APatrolRouteActor* FarRoute = Lured->AssignedPatrolRoute.Get();
			const float FromFarRoute = FarRoute ? TutorialRules::DistanceToRoute2D(RoutePoints(*FarRoute), FarRoute->bIsLoop, Guide.Location) : -1.f;
			Check(State, Guide.bValid && Guide.Guide.Id == FName(TEXT("@patrol_route.A4_PERIMETER")) && FVector::Dist2D(Guide.Location, OnLoop) < 50.f && FromFarRoute > 500.f,
				FString::Printf(TEXT("the current target is on the PERIMETER zombie's loop (%s at %s, %.0f cm from the loop point nearest to her, %.1f m from the far route)"),
					*UTutorialSubsystem::DescribeGuide(Guide.Guide), *Guide.Location.ToCompactString(), FVector::Dist2D(Guide.Location, OnLoop), FromFarRoute / 100.f));
			// The wire: across the loop's first leg at its middle (perpendicular, 3 m), the REAL two-click placement.
			const FVector LegA = Loop[0];
			const FVector LegB = Loop.Num() > 1 ? Loop[1] : Loop[0] + FVector(100.f, 0.f, 0.f);
			const FVector Mid = (LegA + LegB) * 0.5f;
			const FVector Across = FVector::CrossProduct(FVector::UpVector, (LegB - LegA).GetSafeNormal2D());
			State.WireA = Ground(World, Mid + Across * 150.f);
			State.WireB = Ground(World, Mid - Across * 150.f);
			Medic->GrenadesCount = FMath::Max(Medic->GrenadesCount, 2);
			URelocationSubsystem* Relocation = World->GetSubsystem<URelocationSubsystem>();
			Check(State, Relocation && Relocation->StartTripwirePlacement(Medic) && Relocation->IsPlacingTripwire(), TEXT("she opens the tripwire placement"));
			if (Relocation)
			{
				Relocation->UpdatePreview(State.WireA);
				Relocation->ConfirmPlacement(State.WireA);
				Relocation->UpdatePreview(State.WireB);
				Relocation->ConfirmPlacement(State.WireB);
				Check(State, Relocation->GetTripwireTaskCount() == 1, FString::Printf(TEXT("two clicks across his loop at %s: she goes to rig it"), *Mid.ToCompactString()));
			}
			Next(State);
			return true;
		}
		case 4:
		{
			ATripwireActor* Wire = FindSquadWire(World);
			if ((!Wire || !Wire->IsArmed()) && State.StageTime < 45.f)
			{
				return true;
			}
			State.Wire = Wire;
			Check(State, Wire && Wire->IsArmed(), FString::Printf(TEXT("the wire is rigged and armed (%.1f s)"), State.StageTime));
			Check(State, Tutorial->GetStepState(TEXT("A4_tripwire")) == ETutorialStepState::Completed && Tutorial->GetStepState(TEXT("A4_trap_wait")) == ETutorialStepState::Active,
				TEXT("the card completes, the objective 'wait until the zombie on the loop walks into the wire' shows"));
			if (!Wire)
			{
				return Finish(State, false);
			}
			// She goes back behind the low cover (the real order); the far zombie "pulls a pin" - not part 1's zombie.
			Medic->OrderMoveTo(State.CoverSpot + FVector(0.f, -240.f, 0.f), false);
			Tutorial->NotifyEvent(GameplayEvents::TripwireTriggeredByEnemy, Lured->GetFName());
			Check(State, Tutorial->GetStepState(TEXT("A4_trap_wait")) == ETutorialStepState::Active && Tutorial->GetStepState(TEXT("A4_wt_trap")) == ETutorialStepState::Idle,
				TEXT("PART 1: the FAR zombie's TripwireTriggered.Enemy does not complete it (no 'that's one' line)"));
			Next(State);
			return true;
		}
		case 5:
		{
			ATripwireActor* Wire = State.Wire.Get();
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
			Check(State, Wire && Wire->IsTripped() && Tutorial->GetStepState(TEXT("A4_trap_wait")) == ETutorialStepState::Completed,
				FString::Printf(TEXT("PART 1: the perimeter zombie walked its loop into the wire -> the objective is done (%.1f s, %s)"), State.StageTime,
					TutorialRules::StateName(Tutorial->GetStepState(TEXT("A4_trap_wait")))));
			Check(State, Tutorial->GetStepState(TEXT("A4_wt_trap")) != ETutorialStepState::Idle, TEXT("'That's one. The other one is still by the far container.'"));
			int32 PerimeterTrips = 0;
			for (int32 Index = 0; Index < State.Probe->EventNames.Num(); ++Index)
			{
				PerimeterTrips += State.Probe->EventNames[Index] == GameplayEvents::TripwireTriggeredByEnemy && State.Probe->EventSubjects[Index] == Perimeter->GetFName() ? 1 : 0;
			}
			Check(State, PerimeterTrips == 1, FString::Printf(TEXT("the pin was pulled by %s (part 1's zombie)"), *Perimeter->GetName()));
			Next(State);
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
			// The perimeter zombie (alive: it survived the wire) hears a lure: not part 2's zombie.
			Tutorial->NotifyEvent(GameplayEvents::LureHeard, Perimeter->GetFName());
			Check(State, Tutorial->GetStepState(TEXT("A4_lure")) == ETutorialStepState::Active && Tutorial->GetStepState(TEXT("A4_close_shot")) == ETutorialStepState::Idle,
				TEXT("PART 2: the PERIMETER zombie's StealthLure.Heard does not complete the lure (no close-shot card)"));
			// She goes to the spot; he may be anywhere on his route: up to 11 m of him (inside the 12 m whistle) along her line.
			FVector Spot = Guide.bValid ? Guide.Location : Medic->GetActorLocation();
			const FVector ToHim = (Lured->GetActorLocation() - Spot).GetSafeNormal2D();
			const float Far = static_cast<float>(FVector::Dist2D(Spot, Lured->GetActorLocation()));
			if (Far > 1100.f)
			{
				Spot += ToHim * (Far - 1100.f);
			}
			Spot = SmokeUtils::FreeSpot(World, Spot, Medic);
			Place(World, *Medic, Spot);
			Medic->SetStance(EOperativeStance::Crouching);
			UE_LOG(LogCodexTactics, Display, TEXT("[AmbushLake] lure spot %s (target %s), the far zombie %.1f m away at %s, %.1f m off his route"), *Spot.ToCompactString(),
				*Guide.Location.ToCompactString(), FVector::Dist2D(Spot, Lured->GetActorLocation()) / 100.f, *Lured->GetActorLocation().ToCompactString(), OffRouteCm(*Lured) / 100.f);
			Next(State);
			return true;
		}
		case 7:
		{
			if (State.StageTime < 0.8f)
			{
				return true; // she settles (crouched)
			}
			UStealthLureSubsystem* Lure = World->GetSubsystem<UStealthLureSubsystem>();
			if (Tutorial->GetStepState(TEXT("A4_lure")) == ETutorialStepState::Active && Lure && State.Time - State.LastShotAt > 3.f)
			{
				State.LastShotAt = State.Time;
				const bool bWhistled = Lure->Whistle(Medic);
				UE_LOG(LogCodexTactics, Display, TEXT("[AmbushLake] %s whistles (%s), the far zombie %.1f m away"), *Medic->DisplayName.ToString(),
					bWhistled ? TEXT("ok") : TEXT("refused"), FVector::Dist(Medic->GetActorLocation(), Lured->GetActorLocation()) / 100.f);
			}
			if (Tutorial->GetStepState(TEXT("A4_lure")) == ETutorialStepState::Active && State.StageTime < 30.f)
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
			Next(State);
			return true;
		}
		case 12:
		{
			if (State.ShotAt >= 0.f || State.StageTime < 0.5f)
			{
				return true; // the last frame
			}
			return Finish(State, true);
		}
		default:
			return Finish(State, false);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		State->bShots = Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("Shot"), ESearchCase::IgnoreCase); }) && FApp::CanEverRender();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.TutorialAmbushLakeSmoke"),
		TEXT("Dev check (Demo 01 A4, 2026-10-21): the ambush in two parts on the lake layout - part 1 the perimeter zombie walks into a real wire, part 2 the far zombie is lured by a real whistle into the open and shot dead at close range; each part reacts only to its zombie. 'Shot' = rendered frames."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
