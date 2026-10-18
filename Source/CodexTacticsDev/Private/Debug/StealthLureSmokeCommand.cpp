// Dev-only headless check of the stealth lure (user decisions 2026-10-18; UStealthLureSubsystem, docs/design/STEALTH_LURE.md)
// on L_PatrolTest (nothing saved; everything spawned at runtime):
//   Scripts/smoke_parallel.ps1 -Commands StealthLureSmoke
//   rendered stills: UnrealEditor.exe <uproject> /Game/Maps/L_PatrolTest -game -RenderOffscreen -ResX=1280 -ResY=720
//       -ExecCmds="CodexTactics.StealthLureSmoke Shot"  -> Saved/Screenshots/stealth_lure_investigate.png (enemies walking to the
//       whistle with their «?») and stealth_lure_throw_aim.png (the hearing circle of the stone aim, «HEARS» over the enemies in it)
// A. WHISTLE behind a 3 m wall: a marksman patrolling a spline route 4 m beyond the wall with his hound escort (smell off for
//    the check: a hound in whistle range would otherwise smell the whistler - by design) and a spitter guard 9 m to the side in
//    the open come and investigate the whistle point; a frostbitten guard 8 m beyond the wall (heard in the open, but the wall
//    halves the 12 m) and a brute guard 15 m away do not. No fight, nobody hunts the squad. A second whistle at once is
//    refused (10 s cooldown). The squad slips away (prone, 30 m); every lured enemy reaches the point, investigates (shortened
//    to 12 s here), then the marksman walks his route again, the hound escorts him and the spitter walks back to its post.
// B. THROW: the engineer (THROW key -> the stone aim: lure colours, range by stance, the enemies inside the 8 m circle are the
//    preview listeners) throws a stone 13 m next to a brute + a frostbitten guard (both facing away): both investigate the
//    impact point, the thrower is not detected, his second throw is refused (6 s), the commander's throw is still ready
//    (per operative, per kind); both walk back to their posts after the search.
// C. Detected (an enemy hunting the squad): WHISTLE / THROW refused, nothing happens; ready again once it is gone.
// D. A fight starts (a placed guard breaks its patrol -> the ambush fight): the stone aim ends, both are refused (combat).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/LureStoneActor.h"
#include "AI/PatrolRouteActor.h"
#include "AI/StealthLureSubsystem.h"
#include "Camera/TacticalCameraPawn.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/GrenadeSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Components/SplineComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Data/EnemyPerception.h"
#include "Debug/SmokeUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFlow/LevelEncounterSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "UI/ActionBarWidget.h"
#include "UI/CodexTacticsHUD.h"
#include "UnrealClient.h"

namespace StealthLureSmoke
{
	constexpr float InvestigateSeconds = 12.f;

	struct FState
	{
		float Time = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		bool bShot = false;
		bool bShotTaken = false;
		FVector P = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		FVector WhistlePoint = FVector::ZeroVector;
		FVector ThrowPoint = FVector::ZeroVector;
		FVector Hide = FVector::ZeroVector;
		FStealthLureParams SavedLure;
		TWeakObjectPtr<AStaticMeshActor> Wall;
		TWeakObjectPtr<APatrolRouteActor> Route;
		TWeakObjectPtr<AEnemyCharacter> Marksman;
		TWeakObjectPtr<AEnemyCharacter> Hound;
		TWeakObjectPtr<AEnemyCharacter> Spitter;
		TWeakObjectPtr<AEnemyCharacter> Frostbitten;
		TWeakObjectPtr<AEnemyCharacter> FarBrute;
		TWeakObjectPtr<AEnemyCharacter> Brute;
		TWeakObjectPtr<AEnemyCharacter> Frostbitten2;
		TWeakObjectPtr<AEnemyCharacter> Hunter;
		TWeakObjectPtr<ALureStoneActor> Stone;
		TArray<FVector> Posts;
		TMap<TWeakObjectPtr<AEnemyCharacter>, float> Closest;
		TMap<TWeakObjectPtr<AEnemyCharacter>, float> ArrivedAt;
		/** Fastest planar speed of each lured enemy on its way to the noise (hounds fast, Brutes slow). */
		TMap<TWeakObjectPtr<AEnemyCharacter>, float> TopSpeed;
		float HoundTopSpeed = 0.f;
		/** Camera anchor of the rendered stills (the camera follows an actor). */
		TWeakObjectPtr<AActor> ShotAnchor;
	};

	/** The HUD's action bar (null headless without a HUD), refreshed. */
	UActionBarWidget* GetActionBar(APlayerController* PC)
	{
		const ACodexTacticsHUD* Hud = PC ? Cast<ACodexTacticsHUD>(PC->GetHUD()) : nullptr;
		UActionBarWidget* Bar = Hud ? Hud->GetActionBar() : nullptr;
		if (Bar)
		{
			Bar->Refresh();
		}
		return Bar;
	}

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State)
	{
		EnemyPerception::SetLure(State.SavedLure);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("StealthLureSmoke"));
		return false;
	}

	void Next(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.Time = 0.f;
	}

	AStaticMeshActor* SpawnWall(UWorld* World, const FVector& Centre, const FRotator& Rotation, const FVector& Scale)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (!Cube)
		{
			return nullptr;
		}
		AStaticMeshActor* Block = World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform(Rotation, Centre, Scale),
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Block)
		{
			return nullptr;
		}
		Block->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Block->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Block->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
		Block->GetStaticMeshComponent()->SetCanEverAffectNavigation(true);
		Block->FinishSpawning(FTransform(Rotation, Centre, Scale));
		return Block;
	}

	/** A guard / patrol "placed in the map" (ULevelEncounterSubsystem::MarkLevelEnemy), facing Facing. */
	AEnemyCharacter* SpawnPlaced(UWorld* World, EEnemyArchetype Type, const FVector& Ground, const FVector& Facing, bool bMark = true)
	{
		UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
		const FVector Spot = SmokeUtils::FreeSpot(World, Ground);
		AEnemyCharacter* Enemy = Waves ? Waves->SpawnEnemy(Type, Spot + FVector(0.f, 0.f, 10.f), Facing.Rotation()) : nullptr;
		if (Enemy && bMark)
		{
			if (ULevelEncounterSubsystem* Encounter = World->GetSubsystem<ULevelEncounterSubsystem>())
			{
				Encounter->MarkLevelEnemy(Enemy);
			}
		}
		return Enemy;
	}

	FVector Feet(const AActor* Actor)
	{
		const ACharacter* Character = Cast<ACharacter>(Actor);
		return Actor->GetActorLocation() - FVector(0.f, 0.f, Character ? Character->GetSimpleCollisionHalfHeight() : 0.f);
	}

	/** The whole squad to Ground (side by side), stopped, in Stance. */
	void MoveSquad(UWorld* World, USquadSubsystem& Squad, const FVector& Ground, const FVector& Side, EOperativeStance Stance)
	{
		int32 Index = 0;
		for (AOperativeCharacter* Member : Squad.GetMembers())
		{
			if (!Member)
			{
				continue;
			}
			const FVector Spot = SmokeUtils::FreeSpot(World, Ground + Side * (Index * 150.f), Member);
			Member->StopOperative();
			Member->SetActorLocation(Spot, false, nullptr, ETeleportType::TeleportPhysics);
			Member->SetStance(Stance);
			++Index;
		}
	}

	bool AnyHunting(UWorld* World)
	{
		const UStealthLureSubsystem* Lure = World->GetSubsystem<UStealthLureSubsystem>();
		return Lure && Lure->IsSquadDetected();
	}

	ATacticalCameraPawn* GetCamera(UWorld* World)
	{
		APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		return PC ? PC->GetPawn<ATacticalCameraPawn>() : nullptr;
	}

	/** Rendered stills: the camera follows an anchor actor at Point, DistanceCm away. */
	void FocusShot(UWorld* World, FState& State, const FVector& Point, float DistanceCm)
	{
		ATacticalCameraPawn* Camera = GetCamera(World);
		if (!Camera)
		{
			return;
		}
		AActor* Anchor = State.ShotAnchor.Get();
		if (!Anchor)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Anchor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Point), Params);
			if (Anchor && !Anchor->GetRootComponent())
			{
				USceneComponent* Root = NewObject<USceneComponent>(Anchor, TEXT("Root"));
				Anchor->SetRootComponent(Root);
				Root->RegisterComponent();
			}
			State.ShotAnchor = Anchor;
		}
		if (Anchor)
		{
			Anchor->SetActorLocation(Point);
			Camera->SnapFocusOnTarget(Anchor, DistanceCm);
		}
	}

	void RequestShot(const TCHAR* Name)
	{
		const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots") / Name;
		FScreenshotRequest::RequestScreenshot(Path, true, false);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke screenshot %s"), *Path);
	}

	/** Tracks how close each enemy came to Point (planar) and when it first got within ArriveCm. */
	void TrackApproach(FState& State, const TArray<TWeakObjectPtr<AEnemyCharacter>>& Enemies, const FVector& Point, float Elapsed)
	{
		for (const TWeakObjectPtr<AEnemyCharacter>& Weak : Enemies)
		{
			if (const AEnemyCharacter* Enemy = Weak.Get())
			{
				const float Distance = FVector::Dist2D(Enemy->GetActorLocation(), Point);
				float& Best = State.Closest.FindOrAdd(Weak, TNumericLimits<float>::Max());
				Best = FMath::Min(Best, Distance);
				if (Distance <= 200.f && !State.ArrivedAt.Contains(Weak))
				{
					State.ArrivedAt.Add(Weak, Elapsed);
				}
				if (!State.ArrivedAt.Contains(Weak) && Enemy->IsInvestigatingLure() && !Enemy->HasReachedSearchOrigin())
				{
					float& Top = State.TopSpeed.FindOrAdd(Weak, 0.f);
					Top = FMath::Max(Top, static_cast<float>(Enemy->GetVelocity().Size2D()));
				}
			}
		}
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += 0.1f;
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		ULevelEncounterSubsystem* Encounter = World->GetSubsystem<ULevelEncounterSubsystem>();
		UStealthLureSubsystem* Lure = World->GetSubsystem<UStealthLureSubsystem>();
		UGrenadeSubsystem* Grenades = World->GetSubsystem<UGrenadeSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		if (!Squad || !Flow || !Encounter || !Lure || !Grenades || !PC)
		{
			Check(State, false, TEXT("squad, flow, encounter, lure, grenade subsystems and the player controller"));
			return Finish(State);
		}
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->ColdLevel = 0.f;
		}
		const TArray<AOperativeCharacter*> Members = Squad->GetMembers();
		AOperativeCharacter* Commander = Members.IsValidIndex(0) ? Members[0] : nullptr;
		AOperativeCharacter* Engineer = Members.IsValidIndex(1) ? Members[1] : nullptr;
		AOperativeCharacter* Medic = Members.IsValidIndex(2) ? Members[2] : nullptr;
		switch (State.Stage)
		{
		case 0:
		{
			if (State.Time < 3.f)
			{
				return true;
			}
			if (!Commander || !Engineer || !Medic)
			{
				Check(State, false, TEXT("three operatives"));
				return Finish(State);
			}
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->Destroy(); // the user's map may hold its own enemies: start from a known set
			}
			State.SavedLure = EnemyPerception::GetLure();
			FStealthLureParams Shortened = State.SavedLure;
			Shortened.InvestigateSeconds = InvestigateSeconds;
			EnemyPerception::SetLure(Shortened);
			Lure->ResetCooldowns();
			Squad->SetLeader(Commander);
			Squad->SetSquadPosture(ESquadFirePosture::Passive); // nobody opens fire on an enemy he sees
			State.F = Commander->GetActorForwardVector().GetSafeNormal2D();
			State.R = FVector::CrossProduct(FVector::UpVector, State.F);
			State.P = Feet(Commander);
			const FVector P = State.P;
			const FVector F = State.F;
			const FVector R = State.R;
			MoveSquad(World, *Squad, P, -F, EOperativeStance::Standing);
			// A 3 m high, 12 m wide wall 2.5 m ahead: the whistler stands behind it.
			State.Wall = SpawnWall(World, P + F * 250.f + FVector(0.f, 0.f, 150.f), F.Rotation(), FVector(0.4f, 12.f, 3.f));
			// The marksman's route 4.2 m beyond the wall, a short loop with long pauses (he stays within the 6 m the wall leaves).
			const FVector A = P + F * 420.f - R * 150.f;
			const FVector B = P + F * 420.f + R * 150.f;
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			APatrolRouteActor* Route = World->SpawnActor<APatrolRouteActor>(A, FRotator::ZeroRotator, Params);
			USplineComponent* Spline = Route ? Route->GetRouteSpline() : nullptr;
			if (!State.Wall.IsValid() || !Spline)
			{
				Check(State, false, TEXT("wall and route spawned"));
				return Finish(State);
			}
			Spline->ClearSplinePoints(false);
			for (const FVector& Point : { A, B })
			{
				Spline->AddSplinePoint(Point, ESplineCoordinateSpace::World, false);
			}
			Spline->UpdateSpline();
			Route->bIsLoop = true;
			Route->DefaultWaitTimeSeconds = 4.f;
			State.Route = Route;
			AEnemyCharacter* Marksman = SpawnPlaced(World, EEnemyArchetype::Marksman, A, F, false);
			AEnemyCharacter* Hound = SpawnPlaced(World, EEnemyArchetype::FrostHound, A + F * 250.f, F, false);
			AEnemyCharacter* Spitter = SpawnPlaced(World, EEnemyArchetype::Spitter, P + R * 900.f, R);
			AEnemyCharacter* Frostbitten = SpawnPlaced(World, EEnemyArchetype::Frostbitten, P + F * 800.f, F);
			AEnemyCharacter* FarBrute = SpawnPlaced(World, EEnemyArchetype::Brute, P + R * 1500.f, R);
			if (!Marksman || !Hound || !Spitter || !Frostbitten || !FarBrute)
			{
				Check(State, false, TEXT("marksman, hound, spitter, frostbitten and brute spawned"));
				return Finish(State);
			}
			Marksman->StartPatrol(Route, nullptr);
			Hound->StartPatrol(nullptr, Marksman);
			Encounter->MarkLevelEnemy(Marksman);
			Encounter->MarkLevelEnemy(Hound);
			// A hound 4-8 m from the whistler would smell him (12 m) - by design; the lure itself is checked here.
			Hound->PerceptionOverride = Hound->GetPerception();
			Hound->PerceptionOverride.SmellRadiusCm = 0.f;
			Hound->bOverridePerception = true;
			State.Marksman = Marksman;
			State.Hound = Hound;
			State.Spitter = Spitter;
			State.Frostbitten = Frostbitten;
			State.FarBrute = FarBrute;
			State.Posts = { Spitter->GetActorLocation(), Frostbitten->GetActorLocation(), FarBrute->GetActorLocation() };
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: whistler %s, route %s - %s"), *P.ToCompactString(), *A.ToCompactString(), *B.ToCompactString());
			Next(State, 1);
			return true;
		}
		case 1:
		{
			// The navmesh round the new wall, the patrol walking.
			if (State.Time < 2.5f)
			{
				return true;
			}
			AEnemyCharacter* Marksman = State.Marksman.Get();
			AEnemyCharacter* Hound = State.Hound.Get();
			AEnemyCharacter* Spitter = State.Spitter.Get();
			AEnemyCharacter* Frostbitten = State.Frostbitten.Get();
			AEnemyCharacter* FarBrute = State.FarBrute.Get();
			if (!Marksman || !Hound || !Spitter || !Frostbitten || !FarBrute)
			{
				Check(State, false, TEXT("stage A enemies alive"));
				return Finish(State);
			}
			Check(State, Flow->GetPhase() == ECodexGamePhase::Exploration && !AnyHunting(World), TEXT("exploring, nobody hunts the squad"));
			Check(State, Marksman->IsOnPatrol() && Hound->IsOnPatrol() && Hound->GetEscortLeader() == Marksman && Spitter->IsGuardingPost()
				&& Frostbitten->IsGuardingPost() && FarBrute->IsGuardingPost(), TEXT("marksman patrols with his hound, three guards at their posts"));
			Check(State, Lure->GetAvailability(Commander, EStealthLureKind::Whistle) == EStealthLureRefusal::None
				&& Lure->GetAvailability(Commander, EStealthLureKind::Throw) == EStealthLureRefusal::None, TEXT("whistle and throw available"));
			// Hover preview: the hearing circle + who would hear it (walls counted).
			State.WhistlePoint = Feet(Commander);
			Lure->UpdatePreview(State.WhistlePoint, EStealthLureKind::Whistle, true);
			const TArray<AEnemyCharacter*> Preview = Lure->GetPreviewListeners();
			Check(State, Lure->IsPreviewActive() && Preview.Contains(Marksman) && Preview.Contains(Spitter) && !Preview.Contains(Frostbitten)
				&& !Preview.Contains(FarBrute), FString::Printf(TEXT("whistle preview: marksman + spitter would hear, frostbitten (wall) / brute (15 m) not (%d listed)"),
					Preview.Num()));
			const float WallsToFrostbitten = Frostbitten->CountHearingWallsTo(State.WhistlePoint + FVector(0.f, 0.f, 60.f));
			Check(State, WallsToFrostbitten >= 1, FString::Printf(TEXT("the wall counts between the frostbitten and the whistle (%d)"), static_cast<int32>(WallsToFrostbitten)));
			// WHISTLE (the real key handler).
			PC->StealthWhistleKey();
			const TArray<TWeakObjectPtr<AEnemyCharacter>>& Heard = Lure->GetLastListeners();
			Check(State, Heard.Contains(Marksman) && Heard.Contains(Spitter), FString::Printf(TEXT("the marksman (beyond the wall) and the spitter (open air) heard it (%d, hound directly: %d)"),
				Heard.Num(), Heard.Contains(Hound) ? 1 : 0));
			Check(State, Marksman->IsInvestigatingLure() && Hound->IsInvestigatingLure() && Spitter->IsInvestigatingLure(),
				TEXT("marksman, his hound and the spitter investigate (the «?» marker)"));
			Check(State, FVector::Dist2D(Marksman->GetSearchOrigin(), State.WhistlePoint) < 50.f && FVector::Dist2D(Spitter->GetSearchOrigin(), State.WhistlePoint) < 50.f,
				TEXT("they investigate the whistle point"));
			Check(State, !Frostbitten->IsSearching() && Frostbitten->IsGuardingPost(), TEXT("the frostbitten 8 m beyond the wall did not hear it (the wall halves 12 m)"));
			Check(State, !FarBrute->IsSearching() && FarBrute->IsGuardingPost(), TEXT("the brute 15 m away did not hear it"));
			Check(State, Flow->GetPhase() == ECodexGamePhase::Exploration && Encounter->GetAmbushStarts() == 0 && !AnyHunting(World),
				TEXT("the whistle reveals nobody: no fight, nobody hunts the squad"));
			const float Cooldown = Lure->GetCooldownLeft(Commander, EStealthLureKind::Whistle);
			Check(State, Cooldown > 9.f && Cooldown <= 10.f, FString::Printf(TEXT("whistle cooldown %.1f s"), Cooldown));
			// A second whistle at once: refused, nobody new sent.
			Check(State, !Lure->Whistle(Commander) && Lure->GetAvailability(Commander, EStealthLureKind::Whistle) == EStealthLureRefusal::Cooldown,
				TEXT("a second whistle at once is refused (cooldown)"));
			Check(State, Lure->GetAvailability(Commander, EStealthLureKind::Throw) == EStealthLureRefusal::None, TEXT("his throw is still ready (per kind)"));
			if (UActionBarWidget* Bar = GetActionBar(PC))
			{
				const FString Whistle = Bar->GetStealthButtonText(false).ToString();
				const FString Throw = Bar->GetStealthButtonText(true).ToString();
				Check(State, !Bar->IsStealthButtonEnabled(false) && Whistle.StartsWith(TEXT("WHISTLE")) && Whistle.EndsWith(TEXT("s")),
					FString::Printf(TEXT("action bar: WHISTLE off with its cooldown (%s)"), *Whistle.Replace(TEXT("\n"), TEXT(" "))));
				Check(State, Bar->IsStealthButtonEnabled(true) && Throw.Contains(TEXT("[6]")), FString::Printf(TEXT("action bar: THROW ready (%s)"),
					*Throw.Replace(TEXT("\n"), TEXT(" "))));
			}
			else
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke: no action bar in this run (headless) - its buttons are not checked"));
			}
			// The whistler slips away: prone, 30 m to the side.
			State.Hide = State.P - State.R * 3000.f;
			MoveSquad(World, *Squad, State.Hide, -State.F, EOperativeStance::Prone);
			if (State.bShot)
			{
				FocusShot(World, State, State.WhistlePoint + State.F * 150.f, 2400.f);
			}
			State.Closest.Reset();
			State.ArrivedAt.Reset();
			Next(State, 2);
			return true;
		}
		case 2:
		{
			// They come: every lured enemy reaches the whistle point (round the wall).
			const TArray<TWeakObjectPtr<AEnemyCharacter>> Lured = { State.Marksman, State.Hound, State.Spitter };
			TrackApproach(State, Lured, State.WhistlePoint, State.Time);
			if (State.bShot && !State.bShotTaken && State.ArrivedAt.Num() >= 1 && State.Time > 3.f)
			{
				RequestShot(TEXT("stealth_lure_investigate.png"));
				State.bShotTaken = true;
			}
			if (State.ArrivedAt.Num() < Lured.Num() && State.Time < InvestigateSeconds && !AnyHunting(World))
			{
				return true;
			}
			for (const TWeakObjectPtr<AEnemyCharacter>& Weak : Lured)
			{
				const float* Arrived = State.ArrivedAt.Find(Weak);
				const float* Best = State.Closest.Find(Weak);
				Check(State, Arrived != nullptr, FString::Printf(TEXT("%s comes to the whistle point (closest %.0f cm, at %.1f s)"),
					Weak.IsValid() ? *Weak->GetName() : TEXT("?"), Best ? *Best : -1.f, Arrived ? *Arrived : -1.f));
			}
			Check(State, Flow->GetPhase() == ECodexGamePhase::Exploration && !AnyHunting(World), TEXT("the investigation found nobody: no fight"));
			State.HoundTopSpeed = State.TopSpeed.FindRef(State.Hound);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: approach speed hound %.0f, marksman %.0f, spitter %.0f cm/s"), State.HoundTopSpeed,
				State.TopSpeed.FindRef(State.Marksman), State.TopSpeed.FindRef(State.Spitter));
			Check(State, State.Frostbitten.IsValid() && !State.Frostbitten->IsSearching() && State.FarBrute.IsValid() && !State.FarBrute->IsSearching(),
				TEXT("the enemies out of earshot stayed put"));
			Next(State, 3);
			return true;
		}
		case 3:
		{
			// Back to the route / post after the (shortened) investigation.
			const AEnemyCharacter* Marksman = State.Marksman.Get();
			const AEnemyCharacter* Hound = State.Hound.Get();
			const AEnemyCharacter* Spitter = State.Spitter.Get();
			const bool bOver = Marksman && Hound && Spitter && !Marksman->IsSearching() && !Hound->IsSearching() && !Spitter->IsSearching();
			const float SpitterOff = Spitter ? FVector::Dist2D(Spitter->GetActorLocation(), State.Posts[0]) : -1.f;
			const float MarksmanOffRoute = Marksman ? FMath::Min(FVector::Dist2D(Marksman->GetActorLocation(), State.P + State.F * 420.f - State.R * 150.f),
				FVector::Dist2D(Marksman->GetActorLocation(), State.P + State.F * 420.f + State.R * 150.f)) : -1.f;
			const bool bBack = bOver && SpitterOff < 150.f && Spitter->IsGuardingPost() && MarksmanOffRoute < 200.f;
			if (!bBack && State.Time < 45.f && !AnyHunting(World))
			{
				return true;
			}
			Check(State, bOver, TEXT("the investigation ends"));
			Check(State, Marksman && Marksman->IsOnPatrol() && MarksmanOffRoute < 200.f, FString::Printf(TEXT("the marksman walks his route again (%.0f cm from a waypoint)"), MarksmanOffRoute));
			Check(State, Hound && Hound->IsOnPatrol() && Hound->GetEscortLeader() == Marksman, TEXT("the hound escorts him again"));
			Check(State, Spitter && Spitter->IsGuardingPost() && SpitterOff < 150.f, FString::Printf(TEXT("the spitter walked back to its post (%.0f cm off)"), SpitterOff));
			Check(State, Flow->GetPhase() == ECodexGamePhase::Exploration && Encounter->GetAmbushStarts() == 0, TEXT("still no fight"));
			const float Left = Lure->GetCooldownLeft(Commander, EStealthLureKind::Whistle);
			Check(State, Left <= 0.f && Lure->GetAvailability(Commander, EStealthLureKind::Whistle) == EStealthLureRefusal::None,
				FString::Printf(TEXT("the whistle is ready again after its cooldown (%.1f s left)"), Left));
			// Stage B on a clean field: the stage A enemies out of the picture.
			for (const TWeakObjectPtr<AEnemyCharacter>& Weak : { State.Marksman, State.Hound, State.Spitter, State.Frostbitten, State.FarBrute })
			{
				if (AEnemyCharacter* Enemy = Weak.Get())
				{
					Enemy->SetActorTickEnabled(false);
					SmokeUtils::ParkEnemyFarAway(Enemy);
				}
			}
			// THROW: a brute + a frostbitten guard 18 m to the right of the whistle spot, both facing away.
			const FVector Brute = State.P + State.R * 1800.f + State.F * 600.f;
			AEnemyCharacter* BruteActor = SpawnPlaced(World, EEnemyArchetype::Brute, Brute, State.R);
			AEnemyCharacter* Frost2 = SpawnPlaced(World, EEnemyArchetype::Frostbitten, Brute + State.F * 350.f, State.R);
			if (!BruteActor || !Frost2)
			{
				Check(State, false, TEXT("stage B guards spawned"));
				return Finish(State);
			}
			State.Brute = BruteActor;
			State.Frostbitten2 = Frost2;
			State.Posts.Add(BruteActor->GetActorLocation());
			State.Posts.Add(Frost2->GetActorLocation());
			State.ThrowPoint = Feet(BruteActor) - State.R * 500.f;
			// The engineer 13 m from the impact, behind the guards' backs.
			Squad->SetLeader(Engineer);
			MoveSquad(World, *Squad, State.ThrowPoint - State.R * 1300.f, -State.F, EOperativeStance::Standing);
			Next(State, 4);
			return true;
		}
		case 4:
		{
			if (State.Time < 1.5f)
			{
				return true;
			}
			const AEnemyCharacter* Brute = State.Brute.Get();
			const AEnemyCharacter* Frost2 = State.Frostbitten2.Get();
			Check(State, Brute && Frost2 && Brute->IsGuardingPost() && Frost2->IsGuardingPost() && !AnyHunting(World), TEXT("brute + frostbitten guard, nobody saw the engineer"));
			// THROW key: the stone aim.
			PC->StealthThrowKey();
			Check(State, Grenades->IsAimingLure() && Grenades->GetThrower() == Engineer, TEXT("THROW starts the stone aim for the engineer"));
			Grenades->UpdateAim(State.ThrowPoint);
			const FGrenadeAimInfo Info = Grenades->GetAimInfo(State.ThrowPoint);
			Check(State, Info.bInRange && FMath::IsNearlyEqual(Info.MaxRange, 1500.f, 1.f), FString::Printf(TEXT("13 m in range (max %.0f cm standing)"), Info.MaxRange));
			const AGrenadeAimActor* Aim = Grenades->GetAimActor();
			Check(State, Aim && Aim->IsLureStyle() && !Aim->IsBlastZoneShown() && Aim->GetSegmentCount() > 0, TEXT("hearing circle drawn in the lure colours, no blast zone"));
			const TArray<AEnemyCharacter*> Preview = Lure->GetPreviewListeners();
			Check(State, Preview.Contains(Brute) && Preview.Contains(Frost2), FString::Printf(TEXT("the brute and the frostbitten are highlighted as listeners (%d)"), Preview.Num()));
			if (State.bShot)
			{
				if (ATacticalCameraPawn* Camera = GetCamera(World))
				{
					Camera->SnapFocusOnTarget(State.Frostbitten2.Get(), 3000.f); // the guards, the circle and the thrower in one frame
				}
				Next(State, 40);
				return true;
			}
			Next(State, 5);
			return true;
		}
		case 40:
		{
			// Rendered still of the aim: keep the preview fresh while the camera settles.
			Grenades->UpdateAim(State.ThrowPoint);
			if (State.Time > 2.f && State.Time < 2.15f)
			{
				RequestShot(TEXT("stealth_lure_throw_aim.png"));
			}
			if (State.Time < 3.f)
			{
				return true;
			}
			Next(State, 5);
			return true;
		}
		case 5:
		{
			// The click: the stone flies (real throw path of the aim).
			const bool bThrown = Grenades->ThrowLureAtCursor(State.ThrowPoint);
			Check(State, bThrown && !Grenades->IsAiming(), TEXT("the click throws the stone, the aim ends"));
			for (TActorIterator<ALureStoneActor> It(World); It; ++It)
			{
				State.Stone = *It;
			}
			Check(State, State.Stone.IsValid() && State.Stone->IsFlying() && State.Stone->GetThrower() == Engineer, TEXT("a stone in flight from the engineer's hand"));
			const float Cooldown = Lure->GetCooldownLeft(Engineer, EStealthLureKind::Throw);
			Check(State, Cooldown > 5.f && Cooldown <= 6.f, FString::Printf(TEXT("throw cooldown %.1f s"), Cooldown));
			Check(State, !Grenades->StartLureAim(Engineer) && !Grenades->IsAiming(), TEXT("a second throw is refused (cooldown)"));
			Check(State, Lure->GetAvailability(Commander, EStealthLureKind::Throw) == EStealthLureRefusal::None, TEXT("the commander's throw is ready (per operative)"));
			Next(State, 6);
			return true;
		}
		case 6:
		{
			const bool bLanded = !State.Stone.IsValid() || !State.Stone->IsFlying();
			if (!bLanded && State.Time < 6.f)
			{
				return true;
			}
			const AEnemyCharacter* Brute = State.Brute.Get();
			const AEnemyCharacter* Frost2 = State.Frostbitten2.Get();
			Check(State, bLanded, FString::Printf(TEXT("the stone landed (%.1f s)"), State.Time));
			Check(State, Brute && Frost2 && Brute->IsInvestigatingLure() && Frost2->IsInvestigatingLure(), TEXT("both guards investigate the impact"));
			Check(State, Brute && FVector::Dist2D(Brute->GetSearchOrigin(), State.ThrowPoint) < 100.f, TEXT("at the impact point"));
			Check(State, Flow->GetPhase() == ECodexGamePhase::Exploration && Encounter->GetAmbushStarts() == 0 && !AnyHunting(World),
				TEXT("the thrower stays undetected"));
			MoveSquad(World, *Squad, State.Hide, -State.F, EOperativeStance::Prone);
			State.Closest.Reset();
			State.ArrivedAt.Reset();
			Next(State, 7);
			return true;
		}
		case 7:
		{
			const TArray<TWeakObjectPtr<AEnemyCharacter>> Lured = { State.Brute, State.Frostbitten2 };
			TrackApproach(State, Lured, State.ThrowPoint, State.Time);
			const AEnemyCharacter* Brute = State.Brute.Get();
			const AEnemyCharacter* Frost2 = State.Frostbitten2.Get();
			const bool bOver = Brute && Frost2 && !Brute->IsSearching() && !Frost2->IsSearching();
			const float BruteOff = Brute ? FVector::Dist2D(Brute->GetActorLocation(), State.Posts[3]) : -1.f;
			const float FrostOff = Frost2 ? FVector::Dist2D(Frost2->GetActorLocation(), State.Posts[4]) : -1.f;
			const bool bBack = bOver && BruteOff < 150.f && FrostOff < 150.f;
			if (!bBack && State.Time < 50.f && !AnyHunting(World))
			{
				return true;
			}
			for (const TWeakObjectPtr<AEnemyCharacter>& Weak : Lured)
			{
				const float* Arrived = State.ArrivedAt.Find(Weak);
				Check(State, Arrived != nullptr, FString::Printf(TEXT("%s came to the stone (at %.1f s)"), Weak.IsValid() ? *Weak->GetName() : TEXT("?"), Arrived ? *Arrived : -1.f));
			}
			const float BruteTop = State.TopSpeed.FindRef(State.Brute);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: approach speed brute %.0f, frostbitten %.0f cm/s (hound %.0f)"), BruteTop,
				State.TopSpeed.FindRef(State.Frostbitten2), State.HoundTopSpeed);
			Check(State, State.HoundTopSpeed > BruteTop * 1.3f && BruteTop > 0.f,
				FString::Printf(TEXT("the hound came much faster than the brute (%.0f vs %.0f cm/s)"), State.HoundTopSpeed, BruteTop));
			Check(State, bBack && Brute->IsGuardingPost() && Frost2->IsGuardingPost(),
				FString::Printf(TEXT("search over: both walk back to their posts (%.0f / %.0f cm off)"), BruteOff, FrostOff));
			Check(State, Flow->GetPhase() == ECodexGamePhase::Exploration && !AnyHunting(World), TEXT("still undetected"));
			// C. Detected: a hunter that knows the squad (frozen far away, so it never reaches anyone).
			Squad->SetLeader(Medic);
			AEnemyCharacter* Hunter = SpawnPlaced(World, EEnemyArchetype::Frostbitten, State.Hide + State.F * 3500.f, -State.F, false);
			if (!Hunter)
			{
				Check(State, false, TEXT("hunter spawned"));
				return Finish(State);
			}
			Hunter->CustomTimeDilation = 0.f;
			Hunter->SetActorTickEnabled(false);
			Hunter->SetCurrentTargetForTesting(Medic);
			State.Hunter = Hunter;
			Next(State, 8);
			return true;
		}
		case 8:
		{
			Check(State, AnyHunting(World), TEXT("an enemy hunting the squad = detected"));
			Check(State, Lure->GetAvailability(Medic, EStealthLureKind::Whistle) == EStealthLureRefusal::Detected
				&& Lure->GetAvailability(Medic, EStealthLureKind::Throw) == EStealthLureRefusal::Detected, TEXT("whistle and throw refused (detected)"));
			PC->StealthWhistleKey();
			PC->StealthThrowKey();
			Check(State, Lure->GetCooldownLeft(Medic, EStealthLureKind::Whistle) <= 0.f && !Grenades->IsAiming(), TEXT("the keys do nothing (no whistle, no aim)"));
			if (AEnemyCharacter* Hunter = State.Hunter.Get())
			{
				Hunter->Destroy();
			}
			Next(State, 9);
			return true;
		}
		case 9:
		{
			if (State.Time < 0.3f)
			{
				return true;
			}
			Check(State, !AnyHunting(World) && Lure->GetAvailability(Medic, EStealthLureKind::Whistle) == EStealthLureRefusal::None,
				TEXT("the hunter gone: available again"));
			// D. The fight: the stone aim is up when a placed guard spots the squad.
			PC->StealthThrowKey();
			Check(State, Grenades->IsAimingLure(), TEXT("the medic aims a stone"));
			if (AEnemyCharacter* Brute = State.Brute.Get())
			{
				Brute->BreakPatrol(EPatrolAlertCause::Sight, Feet(Medic));
			}
			Next(State, 10);
			return true;
		}
		case 10:
		{
			if (Flow->GetPhase() != ECodexGamePhase::WaveCombat && State.Time < 5.f)
			{
				return true;
			}
			Check(State, Flow->GetPhase() == ECodexGamePhase::WaveCombat, FString::Printf(TEXT("the guard's detection starts the fight (%s)"),
				*UEnum::GetValueAsString(Flow->GetPhase())));
			Check(State, !Grenades->IsAiming(), TEXT("the fight ends the stone aim"));
			Check(State, Lure->GetAvailability(Medic, EStealthLureKind::Whistle) == EStealthLureRefusal::Combat
				&& Lure->GetAvailability(Medic, EStealthLureKind::Throw) == EStealthLureRefusal::Combat, TEXT("whistle and throw refused in combat"));
			PC->StealthWhistleKey();
			Check(State, Lure->GetCooldownLeft(Medic, EStealthLureKind::Whistle) <= 0.f, TEXT("no whistle in combat"));
			if (UActionBarWidget* Bar = GetActionBar(PC))
			{
				Check(State, !Bar->IsStealthButtonEnabled(false) && !Bar->IsStealthButtonEnabled(true)
					&& Bar->GetStealthButtonText(false).ToString().EndsWith(TEXT("--")), TEXT("action bar: both lure buttons disabled in combat"));
			}
			return Finish(State);
		}
		default:
			return Finish(State);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		State->bShot = Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("Shot"), ESearchCase::IgnoreCase); });
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), 0.1f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.StealthLureSmoke"),
		TEXT("Dev check (L_PatrolTest): whistle behind a wall / stone throw lure every enemy in earshot to investigate, nobody detects the squad, cooldowns, refused when detected / in combat, back to route / post. 'Shot' saves stealth_lure_*.png."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
