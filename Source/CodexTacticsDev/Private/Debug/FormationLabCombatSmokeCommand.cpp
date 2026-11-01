// Dev-only headless check of the combat lab (game designer decisions 2026-11-01, docs/design/COMBAT_LAB.md) on /Game/Maps/L_FormationLabCombat
// (the formation lab + 4 towers with ladders, built by Scripts/Editor/build_formation_lab.py from Content/Data/FormationLab/Combat/
// formation_lab_combat_01.json; level JSON L_FormationLabCombat.json):
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands FormationLabCombatSmoke
//   (map /Game/Maps/L_FormationLabCombat, "-LevelJson=L_FormationLabCombat.json" - Scripts/test_map.json smokeOptions)
//  1. setup: the pressure is on (its timer stopped for the checks), both cheats on from the level JSON, the fight starts by contact, the 4
//     towers' ladders are climbable (>= 7 rungs, nav link, both ends on the navmesh), immortality holds against direct damage;
//  2. spawn points: 8 seeded events (patrols / ambushes alternately, groups of 3) round the squad - every spawned enemy clear of every wall /
//     tower by the level's clearance (its record, and the smoke's own 36 horizontal rays at two heights + a ground trace: not on a roof), on the
//     navmesh, away from the squad; every ambusher concealed (no operative's line of sight);
//  3. endless timer: the interval cut to 3 s - events keep coming, never more than max_alive alive, an event waits while the cap is full;
//  4. ambush, watched: an ambusher placed 12 m out where the leader looks HOLDS (no fight, its senses muted) for >= 3 s; then moved to the
//     squad's widest unwatched side it SPRINGS within 1 s - the real-time skirmish starts with it as the contact (Patrol -> Engage);
//  5. patrol contact while moving: a 3-enemy patrol 30 m ahead, the squad walks towards / past it by a real click - the contact starts the
//     fight while the leader moves; a move order in the fight (12 m to the side) is carried out in formation (the followers within 10 m);
//     both fights end straight in exploration; through both the squad loses no health and no clip empties (immortal + infinite ammo);
//  6. the leader (solo) climbs the nearest tower by a click on its roof and stands on top.
// "shot" (rendered: UnrealEditor.exe <uproject> /Game/Maps/L_FormationLabCombat -game -RenderOffscreen -ResX=1280 -ResY=720
// "-LevelJson=L_FormationLabCombat.json" "-ExecCmds=CodexTactics.FormationLabCombatSmoke shot") also saves stills to Saved/Screenshots/CombatLab/
// (01_overview_towers, 02_patrol, 03_ambush_springs).
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
#include "Combat/CombatCheats.h"
#include "Combat/CombatPressureRules.h"
#include "Combat/CombatPressureSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFlow/LevelEncounterSubsystem.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Ladders/LadderActor.h"
#include "Ladders/LadderClimbComponent.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "NavLinkCustomComponent.h"
#include "ShaderCompiler.h"
#include "TimerManager.h"
#include "UnrealClient.h"

namespace FormationLabCombatSmoke
{
	constexpr float NavWarmupSeconds = 3.f;
	constexpr float Timeout = 900.f;
	constexpr int32 SpawnEvents = 8;

	enum class EStage : uint8
	{
		Setup,
		Spawns,
		Auto,
		AutoSettle,
		AmbushPlace,
		AmbushHold,
		AmbushSpring,
		AmbushFight,
		PatrolPlace,
		PatrolWalk,
		PatrolFight,
		TowerWalk,
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

	struct FState
	{
		EStage Stage = EStage::Setup;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Failures = 0;
		bool bShot = false;
		TArray<FPhoto> Photos;
		TWeakObjectPtr<ACameraActor> Camera;
		int32 SpawnEvent = 0;
		int32 SpawnChecked = 0;
		int32 MaxAliveSeen = 0;
		TWeakObjectPtr<AEnemyCharacter> Ambusher;
		int32 AmbushStartsBefore = 0;
		bool bSprungWhileWatched = false;
		float MaxHoldSeconds = 0.f;
		TArray<TWeakObjectPtr<AEnemyCharacter>> Patrol;
		FVector PatrolGoal = FVector::ZeroVector;
		bool bMovingAtContact = false;
		float LeaderSpeedAtContact = 0.f;
		FVector FightStart = FVector::ZeroVector;
		float FightTravel = 0.f;
		FVector LastLeader = FVector::ZeroVector;
		float MaxFollowerGap = 0.f;
		bool bSideMoveIssued = false;
		/** Per member: health / clip / reserve when the checks began; the worst seen since. */
		TMap<TWeakObjectPtr<AOperativeCharacter>, float> HealthAtStart;
		TMap<TWeakObjectPtr<AOperativeCharacter>, int32> ClipAtStart;
		float WorstHealthLoss = 0.f;
		int32 WorstClipLoss = 0;
		int32 FightsEnded = 0;
		TWeakObjectPtr<ALadderActor> Tower;
	};

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
		FPlatformMisc::RequestExit(false, TEXT("FormationLabCombatSmoke"));
		return false;
	}

	void Enter(FState& State, EStage Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
		UE_LOG(LogCodexTactics, Display, TEXT("[CombatLab] stage %d at %.1f s"), static_cast<int32>(Stage), State.Time);
	}

	bool IsLiving(const AOperativeCharacter* Member)
	{
		return Member && Member->HealthComponent && Member->HealthComponent->IsAlive();
	}

	/** A real click: a Visibility trace down onto Point, handed to the controller like the mouse's. */
	bool ClickAt(UWorld* World, const FVector& Point)
	{
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController());
		FHitResult Hit;
		if (!PC || !World->LineTraceSingleByChannel(Hit, Point + FVector(0.f, 0.f, 300.f), Point - FVector(0.f, 0.f, 300.f), ECC_Visibility))
		{
			return false;
		}
		PC->HandleWorldHit(Hit);
		UE_LOG(LogCodexTactics, Display, TEXT("[CombatLab] click on %s at %s"), *GetNameSafe(Hit.GetActor()), *Hit.ImpactPoint.ToCompactString());
		return true;
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

	/**
	 * The smoke's own clearance check of a spawn spot (independent of the subsystem's box math): no world-static hit on 36 horizontal rays
	 * of RadiusCm at 50 and 150 cm over the feet, and the ground under the feet is the floor (not a wall's / tower's top).
	 */
	bool RaysClear(UWorld* World, const FVector& Feet, float RadiusCm, FString& OutWhy)
	{
		const FCollisionObjectQueryParams Statics(ECC_WorldStatic);
		const FCollisionQueryParams Params(SCENE_QUERY_STAT(CombatLabSmokeRays), false);
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

	/** Records the squad's worst health loss / clip loss since the checks began (immortality, infinite ammo). */
	void SampleCheats(FState& State, USquadSubsystem& Squad)
	{
		for (AOperativeCharacter* Member : Squad.GetMembers())
		{
			if (!Member || !Member->HealthComponent)
			{
				continue;
			}
			const float* Health = State.HealthAtStart.Find(Member);
			const int32* Clip = State.ClipAtStart.Find(Member);
			if (!Health || !Clip)
			{
				State.HealthAtStart.Add(Member, Member->HealthComponent->GetCurrentHealth());
				State.ClipAtStart.Add(Member, Member->CurrentClip);
				continue;
			}
			State.WorstHealthLoss = FMath::Max(State.WorstHealthLoss, *Health - Member->HealthComponent->GetCurrentHealth());
			State.WorstClipLoss = FMath::Max(State.WorstClipLoss, *Clip - Member->CurrentClip);
		}
	}

	int32 BlockedHits(USquadSubsystem& Squad)
	{
		int32 Hits = 0;
		for (const AOperativeCharacter* Member : Squad.GetMembers())
		{
			Hits += Member && Member->HealthComponent ? Member->HealthComponent->GetCheatBlockedHits() : 0;
		}
		return Hits;
	}

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

	/** Rendered stills (the "shot" run): time almost stopped, a camera on the target, one screenshot each. True while a photo is pending. */
	bool StepPhotos(UWorld* World, FState& State)
	{
		if (State.Photos.IsEmpty())
		{
			return false;
		}
		APlayerController* PC = World->GetFirstPlayerController();
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
		++Photo.Frames;
		const bool bShadersDone = !GShaderCompilingManager || GShaderCompilingManager->GetNumRemainingJobs() == 0;
		if (!Photo.bTaken && Photo.Frames >= 30 && (bShadersDone || Photo.Frames >= 900))
		{
			Photo.bTaken = true;
			Photo.TakenFrame = Photo.Frames;
			const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("CombatLab") / (Photo.Name + TEXT(".png"));
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
				PC->SetViewTarget(PC->GetPawn());
			}
		}
		return true;
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
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Squad || !Flow || !Encounter || !Pressure || !Leader)
		{
			Check(State, false, TEXT("squad, game flow, encounter, pressure subsystems and a leader"));
			return Finish(State, false);
		}
		if (State.Stage > EStage::Setup)
		{
			SampleCheats(State, *Squad);
			State.MaxAliveSeen = FMath::Max(State.MaxAliveSeen, Pressure->GetAliveCount());
		}
		const FCombatPressureConfig& Config = Pressure->GetConfig();

		switch (State.Stage)
		{
		case EStage::Setup:
		{
			Pressure->SetAutoSpawn(false);
			Pressure->DespawnAll();
			Check(State, Pressure->IsActive(), TEXT("the level's enemy pressure is on (level JSON \"pressure\")"));
			Check(State, Config.IntervalSeconds == 120.f && Config.GroupMin == 1 && Config.GroupMax == 3 && Config.MaxAlive == 4,
				FString::Printf(TEXT("the designer's numbers: every %.0f s, groups %d-%d, max alive %d"), Config.IntervalSeconds, Config.GroupMin, Config.GroupMax,
					Config.MaxAlive));
			Check(State, CombatCheats::IsImmortal(World) && CombatCheats::IsInfiniteAmmo(World), TEXT("immortality + infinite ammo on by default (level JSON \"cheats\")"));
			Check(State, Encounter->IsAmbushCombatStart() && Flow->GetPhase() == ECodexGamePhase::Exploration, TEXT("exploring; the fight starts by contact"));
			int32 Ladders = 0;
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			for (TActorIterator<ALadderActor> It(World); It; ++It)
			{
				++Ladders;
				FNavLocation Bottom;
				FNavLocation Top;
				const bool bBottom = Nav && Nav->ProjectPointToNavigation(It->GetBottomLinkWorld(), Bottom, FVector(40.f, 40.f, 60.f));
				const bool bTop = Nav && Nav->ProjectPointToNavigation(It->GetTopLinkWorld(), Top, FVector(40.f, 40.f, 60.f));
				Check(State, It->IsClimbable() && It->GetRungCount() >= 7 && It->GetNavLink() && It->GetNavLink()->IsEnabled() && bBottom && bTop,
					FString::Printf(TEXT("tower ladder %s: %d rungs, climbable %d, link ends on the navmesh %d / %d, top %.0f cm"), *It->GetName(), It->GetRungCount(),
						It->IsClimbable() ? 1 : 0, bBottom ? 1 : 0, bTop ? 1 : 0, It->GetTopWorldZ() - It->GetFloorZ()));
			}
			Check(State, Ladders == 4, FString::Printf(TEXT("4 towers with ladders (%d)"), Ladders));
			const int32 Boxes = Pressure->GetObstacleBoxes().Num();
			Check(State, Boxes >= 24, FString::Printf(TEXT("the walls + towers read as obstacle boxes (%d)"), Boxes));
			if (Squad->IsSoloMode())
			{
				Squad->ExitSoloMode();
			}
			// Immortality against direct damage (the shots / blows / blasts of the fight are sampled from here on).
			UHealthComponent* Health = Leader->HealthComponent;
			const float Before = Health ? Health->GetCurrentHealth() : 0.f;
			const int32 BlockedBefore = Health ? Health->GetCheatBlockedHits() : 0;
			if (Health)
			{
				FDamageSpec Spec;
				Spec.Amount = 45.f;
				Health->TakeDamage(Spec);
				Health->ApplyDirectHealthLoss(30.f, TEXT("CombatLabSmoke"));
			}
			Check(State, Health && Health->GetCurrentHealth() == Before && Health->IsAlive() && Health->GetCheatBlockedHits() == BlockedBefore + 2,
				FString::Printf(TEXT("immortal: 45 + 30 damage taken, health %.0f -> %.0f (%d hits blocked)"), Before, Health ? Health->GetCurrentHealth() : -1.f,
					Health ? Health->GetCheatBlockedHits() - BlockedBefore : -1));
			Check(State, Squad->GetMembers().Num() >= 2, FString::Printf(TEXT("a squad of %d"), Squad->GetMembers().Num()));
			QueuePhoto(State, TEXT("01_overview_towers"), FVector(0.f, 300.f, 0.f), FRotator(-68.f, 90.f, 0.f), 12500.f);
			Enter(State, EStage::Spawns);
			return true;
		}

		case EStage::Spawns:
		{
			// One event per tick, checked and removed in the same tick (nobody perceives anybody in between).
			const EPressureSpawnKind Kind = State.SpawnEvent % 2 == 0 ? EPressureSpawnKind::Patrol : EPressureSpawnKind::Ambush;
			const int32 RecordsBefore = Pressure->GetSpawnRecords().Num();
			const TArray<AEnemyCharacter*> Spawned = Pressure->TriggerEvent(Kind, 3, true);
			Check(State, !Spawned.IsEmpty(), FString::Printf(TEXT("event %d (%s): %d spawned"), State.SpawnEvent, Kind == EPressureSpawnKind::Patrol ? TEXT("patrol") : TEXT("ambush"),
				Spawned.Num()));
			const TArray<FVector2D> SquadPositions = Pressure->GatherSquadPositions();
			const TArray<FPressureSpawnRecord>& Records = Pressure->GetSpawnRecords();
			for (int32 Index = RecordsBefore; Index < Records.Num(); ++Index)
			{
				const FPressureSpawnRecord& Record = Records[Index];
				const AEnemyCharacter* Enemy = Record.Enemy.Get();
				FString Why;
				const bool bRays = RaysClear(World, Record.Location, Config.WallClearanceCm - 15.f, Why);
				FNavLocation OnNav;
				UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
				const bool bNav = Nav && Nav->ProjectPointToNavigation(Record.Location, OnNav, FVector(50.f, 50.f, 100.f));
				const float SquadDistance = AmbushWatchRules::NearestDistance(SquadPositions, FVector2D(Record.Location));
				const float MinDistance = (Record.Kind == EPressureSpawnKind::Patrol ? Config.MinSquadDistanceCm : Config.AmbushMinDistanceCm) - 2.f * 220.f;
				const bool bAmbushHidden = Record.Kind != EPressureSpawnKind::Ambush || Record.bConcealed;
				Check(State, Enemy && !Record.bPlacedByCaller && Record.ClearanceCm >= Config.WallClearanceCm - 1.f && bRays && bNav && SquadDistance >= MinDistance && bAmbushHidden
					&& CombatPressureRules::IsAllowedType(Record.Type),
					FString::Printf(TEXT("spawn %d %s %s at %s: clearance %.0f cm (>= %.0f), rays %s, navmesh %d, squad %.0f cm, concealed %d"), Index,
						Record.Kind == EPressureSpawnKind::Patrol ? TEXT("patrol") : TEXT("ambusher"), *UEnum::GetValueAsString(Record.Type),
						*Record.Location.ToCompactString(), Record.ClearanceCm, Config.WallClearanceCm, bRays ? TEXT("clear") : *Why, bNav ? 1 : 0, SquadDistance,
						Record.bConcealed ? 1 : 0));
				++State.SpawnChecked;
			}
			Pressure->DespawnAll();
			if (++State.SpawnEvent < SpawnEvents)
			{
				return true;
			}
			Check(State, State.SpawnChecked >= SpawnEvents, FString::Printf(TEXT("%d spawned enemies checked"), State.SpawnChecked));
			// The endless timer: events every 3 s from now on.
			IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Pressure.IntervalSec"))->Set(3.f);
			Pressure->SetAutoSpawn(true);
			State.MaxAliveSeen = 0;
			Enter(State, EStage::Auto);
			return true;
		}

		case EStage::Auto:
		{
			const bool bDone = Pressure->GetEventsFired() >= 2 && Pressure->GetEventsDelayed() >= 1;
			if (!bDone && State.StageTime < 60.f)
			{
				return true;
			}
			Check(State, Pressure->GetEventsFired() >= 2, FString::Printf(TEXT("the timer keeps spawning (%d events)"), Pressure->GetEventsFired()));
			Check(State, State.MaxAliveSeen <= Config.MaxAlive, FString::Printf(TEXT("never more than max alive (%d of %d)"), State.MaxAliveSeen, Config.MaxAlive));
			Check(State, Pressure->GetEventsDelayed() >= 1, FString::Printf(TEXT("an event waited while the cap was full (%d)"), Pressure->GetEventsDelayed()));
			// Every timer spawn was a valid spot too.
			for (const FPressureSpawnRecord& Record : Pressure->GetSpawnRecords())
			{
				if (!Record.bPlacedByCaller && Record.ClearanceCm < Config.WallClearanceCm - 1.f)
				{
					Check(State, false, FString::Printf(TEXT("timer spawn at %s too close to a wall (%.0f cm)"), *Record.Location.ToCompactString(), Record.ClearanceCm));
				}
			}
			Pressure->SetAutoSpawn(false);
			IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Pressure.IntervalSec"))->Set(0.f);
			Pressure->DespawnAll();
			Enter(State, EStage::AutoSettle);
			return true;
		}

		case EStage::AutoSettle:
		{
			// A patrol of the timer may have started a skirmish: with its enemies gone it ends in exploration.
			if ((Flow->GetPhase() != ECodexGamePhase::Exploration || State.StageTime < 2.f) && State.StageTime < 20.f)
			{
				return true;
			}
			Check(State, Flow->GetPhase() == ECodexGamePhase::Exploration, TEXT("back in exploration after the timer stage"));
			Enter(State, EStage::AmbushPlace);
			return true;
		}

		case EStage::AmbushPlace:
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			// The squad holds fire (Passive): an ambusher it sees in exploration is not shot at (the hold is the ambusher's choice, not a fight).
			Squad->SetSquadPosture(ESquadFirePosture::Passive);
			const TArray<FAmbushWatcher> Watchers = Pressure->GatherWatchers();
			const FVector LeaderFeet = FeetOf(*Leader);
			const float Look = static_cast<float>(Leader->GetActorRotation().Yaw);
			TOptional<FVector> Spot;
			for (const float Distance : { 1200.f, 1000.f, 1400.f, 900.f })
			{
				for (const float Turn : { 0.f, 10.f, -10.f, 20.f, -20.f })
				{
					const TOptional<FVector> Candidate = GroundNav(World, LeaderFeet + FRotator(0.f, Look + Turn, 0.f).Vector() * Distance);
					if (Candidate.IsSet() && AmbushWatchRules::IsWatched(Watchers, FVector2D(Candidate.GetValue()), Config.Watch)
						&& FVector::Dist2D(Candidate.GetValue(), LeaderFeet) < Config.Watch.StrikeRangeCm - 200.f)
					{
						Spot = Candidate;
						break;
					}
				}
				if (Spot.IsSet())
				{
					break;
				}
			}
			Check(State, Spot.IsSet(), TEXT("a watched spot 9-14 m out where the leader looks"));
			if (!Spot.IsSet())
			{
				return Finish(State, false);
			}
			const TArray<AEnemyCharacter*> Group = Pressure->SpawnAmbushGroupAt(Spot.GetValue(), { EEnemyArchetype::Frostbitten }, LeaderFeet);
			State.Ambusher = Group.IsEmpty() ? nullptr : Group[0];
			State.AmbushStartsBefore = Encounter->GetAmbushStarts();
			Check(State, State.Ambusher.IsValid() && State.Ambusher->IsGuardingPost(), TEXT("the ambusher waits (on guard, its senses muted)"));
			Enter(State, EStage::AmbushHold);
			return true;
		}

		case EStage::AmbushHold:
		{
			AEnemyCharacter* Ambusher = State.Ambusher.Get();
			EPressureAmbushState AmbushState = EPressureAmbushState::Hidden;
			float Held = 0.f;
			if (!Ambusher || !Pressure->GetAmbushState(Ambusher, AmbushState, Held))
			{
				Check(State, false, TEXT("the ambusher is still there"));
				return Finish(State, false);
			}
			const bool bWatched = AmbushWatchRules::IsWatched(Pressure->GatherWatchers(), FVector2D(Ambusher->GetActorLocation()), Config.Watch);
			State.bSprungWhileWatched |= AmbushState == EPressureAmbushState::Sprung && bWatched;
			State.MaxHoldSeconds = FMath::Max(State.MaxHoldSeconds, Held);
			if (AmbushState != EPressureAmbushState::Sprung && State.StageTime < 4.f)
			{
				return true;
			}
			Check(State, AmbushState == EPressureAmbushState::Holding && State.MaxHoldSeconds >= 3.f && !State.bSprungWhileWatched,
				FString::Printf(TEXT("watched, the ambusher HOLDS (%.1f s held, state %d, watched now %d)"), State.MaxHoldSeconds, static_cast<int32>(AmbushState), bWatched ? 1 : 0));
			Check(State, Ambusher->IsOnPatrol() && Flow->GetPhase() == ECodexGamePhase::Exploration && Encounter->GetAmbushStarts() == State.AmbushStartsBefore,
				TEXT("no fight while it holds"));
			// Its side unwatched: moved to the middle of the squad's widest unwatched arc, 12 m from the leader.
			TArray<FAmbushWatcher> Watchers = Pressure->GatherWatchers();
			TOptional<float> Gap = AmbushWatchRules::WidestUnwatchedBearing(Watchers, Config.Watch);
			if (!Gap.IsSet())
			{
				Pressure->SetWatchHalfAngleOverride(25.f); // the squad watches all round with the level's sectors: narrower ones for the forced case
				Gap = AmbushWatchRules::WidestUnwatchedBearing(Watchers, Pressure->GetConfig().Watch);
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: the squad watches all round at %.0f deg - the forced case narrows the sectors to 25 deg"),
					Config.Watch.WatchHalfAngleDeg);
			}
			const FVector LeaderFeet = FeetOf(*Leader);
			TOptional<FVector> Unwatched;
			for (const float Distance : { 1200.f, 1000.f, 1400.f, 800.f })
			{
				for (const float Turn : { 0.f, 8.f, -8.f, 16.f, -16.f })
				{
					const TOptional<FVector> Candidate = Gap.IsSet() ? GroundNav(World, LeaderFeet + FRotator(0.f, Gap.GetValue() + Turn, 0.f).Vector() * Distance) : TOptional<FVector>();
					if (Candidate.IsSet() && !AmbushWatchRules::IsWatched(Watchers, FVector2D(Candidate.GetValue()), Pressure->GetConfig().Watch))
					{
						Unwatched = Candidate;
						break;
					}
				}
				if (Unwatched.IsSet())
				{
					break;
				}
			}
			Check(State, Unwatched.IsSet(), FString::Printf(TEXT("an unwatched spot behind the squad's looks (gap at %.0f deg)"), Gap.Get(0.f)));
			if (!Unwatched.IsSet())
			{
				return Finish(State, false);
			}
			Ambusher->SetActorLocation(Unwatched.GetValue() + FVector(0.f, 0.f, Ambusher->GetSimpleCollisionHalfHeight()), false, nullptr, ETeleportType::TeleportPhysics);
			QueuePhoto(State, TEXT("03_ambush_springs"), Unwatched.GetValue() * 0.5f + LeaderFeet * 0.5f + FVector(0.f, 0.f, 100.f), FRotator(-50.f, Gap.Get(0.f) + 150.f, 0.f), 1800.f);
			Enter(State, EStage::AmbushSpring);
			return true;
		}

		case EStage::AmbushSpring:
		{
			AEnemyCharacter* Ambusher = State.Ambusher.Get();
			EPressureAmbushState AmbushState = EPressureAmbushState::Hidden;
			float Held = 0.f;
			const bool bKnown = Ambusher && Pressure->GetAmbushState(Ambusher, AmbushState, Held);
			if (bKnown && AmbushState != EPressureAmbushState::Sprung && State.StageTime < 1.f)
			{
				return true;
			}
			Check(State, bKnown && AmbushState == EPressureAmbushState::Sprung && Pressure->GetSprungByWatchCount() == 1,
				FString::Printf(TEXT("unwatched, it SPRINGS within %.2f s (sprung by the watch rule %d)"), State.StageTime, Pressure->GetSprungByWatchCount()));
			Check(State, Flow->GetPhase() == ECodexGamePhase::WaveCombat && Flow->IsSkirmish() && Encounter->GetAmbushStarts() == State.AmbushStartsBefore + 1
				&& Encounter->GetLastContactSource() == Ambusher && Encounter->GetLastTrigger() == EAmbushTrigger::PatrolDetection,
				FString::Printf(TEXT("the ambush starts the real-time skirmish (phase %s, trigger %s)"), *UEnum::GetValueAsString(Flow->GetPhase()),
					*UEnum::GetValueAsString(Encounter->GetLastTrigger())));
			Check(State, Ambusher && !Ambusher->IsOnPatrol() && !Ambusher->bOverridePerception, TEXT("the ambusher engages with its own senses again"));
			Pressure->SetWatchHalfAngleOverride(-1.f);
			Squad->SetSquadPosture(ESquadFirePosture::Aggressive);
			Enter(State, EStage::AmbushFight);
			return true;
		}

		case EStage::AmbushFight:
		{
			if (Flow->GetPhase() != ECodexGamePhase::Exploration && State.StageTime < 90.f)
			{
				return true;
			}
			const AEnemyCharacter* Ambusher = State.Ambusher.Get();
			Check(State, Flow->GetPhase() == ECodexGamePhase::Exploration && (!Ambusher || Ambusher->IsDying()),
				FString::Printf(TEXT("the squad kills the ambusher; the skirmish ends in exploration (%.1f s)"), State.StageTime));
			++State.FightsEnded;
			Enter(State, EStage::PatrolPlace);
			return true;
		}

		case EStage::PatrolPlace:
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			// Towards the lab's middle from the leader: the patrol 30 m that way, the squad's goal 45 m that way.
			const FVector LeaderFeet = FeetOf(*Leader);
			FVector Direction = (FVector(0.f, 0.f, LeaderFeet.Z) - LeaderFeet).GetSafeNormal2D();
			TOptional<FVector> PatrolSpot;
			TOptional<FVector> Goal;
			const TArray<FPressureBox>& Boxes = Pressure->GetObstacleBoxes();
			for (const float Turn : { 0.f, 20.f, -20.f, 40.f, -40.f, 60.f, -60.f, 80.f, -80.f })
			{
				const FVector Dir = Direction.RotateAngleAxis(Turn, FVector::UpVector);
				const TOptional<FVector> Here = GroundNav(World, LeaderFeet + Dir * 3000.f);
				const TOptional<FVector> There = GroundNav(World, LeaderFeet + Dir * 4500.f);
				if (Here.IsSet() && There.IsSet() && CombatPressureRules::Clearance(FVector2D(Here.GetValue()), Boxes) >= Config.WallClearanceCm
					&& CombatPressureRules::Clearance(FVector2D(There.GetValue()), Boxes) >= 150.f)
				{
					PatrolSpot = Here;
					Goal = There;
					break;
				}
			}
			Check(State, PatrolSpot.IsSet(), TEXT("an open spot 30 m ahead for the patrol"));
			if (!PatrolSpot.IsSet())
			{
				return Finish(State, false);
			}
			const TArray<AEnemyCharacter*> Group = Pressure->SpawnPatrolGroupAt(PatrolSpot.GetValue(),
				{ EEnemyArchetype::Frostbitten, EEnemyArchetype::Frostbitten, EEnemyArchetype::Spitter });
			for (AEnemyCharacter* Enemy : Group)
			{
				State.Patrol.Add(Enemy);
			}
			Check(State, Group.Num() == 3 && Group[0]->IsOnPatrol() && Group[0]->AssignedPatrolRoute && Group[1]->GetEscortLeader() == Group[0],
				FString::Printf(TEXT("a patrol of %d: the leader walks its loop, the others escort him"), Group.Num()));
			State.PatrolGoal = Goal.GetValue();
			State.AmbushStartsBefore = Encounter->GetAmbushStarts();
			Check(State, ClickAt(World, State.PatrolGoal), TEXT("the squad is ordered towards / past the patrol"));
			QueuePhoto(State, TEXT("02_patrol"), PatrolSpot.GetValue() * 0.6f + LeaderFeet * 0.4f + FVector(0.f, 0.f, 100.f),
				FRotator(-45.f, static_cast<float>(Direction.Rotation().Yaw) + 120.f, 0.f), 2600.f);
			Enter(State, EStage::PatrolWalk);
			return true;
		}

		case EStage::PatrolWalk:
		{
			if (Flow->GetPhase() == ECodexGamePhase::Exploration)
			{
				State.LeaderSpeedAtContact = static_cast<float>(Leader->GetVelocity().Size2D());
				State.bMovingAtContact = Leader->IsMoving() || State.LeaderSpeedAtContact > 50.f;
				if (State.StageTime < 60.f)
				{
					return true;
				}
			}
			const AActor* Source = Encounter->GetLastContactSource();
			const bool bFromPatrol = State.Patrol.ContainsByPredicate([Source](const TWeakObjectPtr<AEnemyCharacter>& Each) { return Each.Get() == Source; });
			Check(State, Flow->GetPhase() == ECodexGamePhase::WaveCombat && Flow->IsSkirmish() && Encounter->GetAmbushStarts() == State.AmbushStartsBefore + 1 && bFromPatrol,
				FString::Printf(TEXT("the patrol contact starts the skirmish (after %.1f s, trigger %s, source %s)"), State.StageTime,
					*UEnum::GetValueAsString(Encounter->GetLastTrigger()), *GetNameSafe(Source)));
			Check(State, State.bMovingAtContact, FString::Printf(TEXT("the squad was moving at the contact (leader %.0f cm/s)"), State.LeaderSpeedAtContact));
			// A move order in the fight: 12 m to the side of the way.
			const FVector Way = (State.PatrolGoal - Leader->GetActorLocation()).GetSafeNormal2D();
			const FVector Side = FVector::CrossProduct(FVector::UpVector, Way);
			TOptional<FVector> SideGoal;
			for (const float Sign : { 1.f, -1.f })
			{
				SideGoal = GroundNav(World, FeetOf(*Leader) + Side * Sign * 1200.f);
				if (SideGoal.IsSet() && CombatPressureRules::Clearance(FVector2D(SideGoal.GetValue()), Pressure->GetObstacleBoxes()) >= 150.f)
				{
					break;
				}
				SideGoal.Reset();
			}
			State.bSideMoveIssued = SideGoal.IsSet() && ClickAt(World, SideGoal.GetValue());
			Check(State, State.bSideMoveIssued, TEXT("a move order in the fight (12 m to the side)"));
			State.FightStart = Leader->GetActorLocation();
			State.LastLeader = State.FightStart;
			State.FightTravel = 0.f;
			State.MaxFollowerGap = 0.f;
			Squad->SetSquadPosture(ESquadFirePosture::Aggressive);
			Enter(State, EStage::PatrolFight);
			return true;
		}

		case EStage::PatrolFight:
		{
			if (Flow->GetPhase() == ECodexGamePhase::WaveCombat)
			{
				State.FightTravel += static_cast<float>(FVector::Dist2D(Leader->GetActorLocation(), State.LastLeader));
				if (Leader->IsMoving())
				{
					for (const AOperativeCharacter* Member : Squad->GetMembers())
					{
						if (IsLiving(Member) && Member != Leader && !Member->IsKnockedDown())
						{
							State.MaxFollowerGap = FMath::Max(State.MaxFollowerGap, static_cast<float>(FVector::Dist2D(Member->GetActorLocation(), Leader->GetActorLocation())));
						}
					}
				}
			}
			State.LastLeader = Leader->GetActorLocation();
			if (Flow->GetPhase() != ECodexGamePhase::Exploration && State.StageTime < 120.f)
			{
				return true;
			}
			Check(State, Flow->GetPhase() == ECodexGamePhase::Exploration, FString::Printf(TEXT("the patrol fight ends in exploration (%.1f s)"), State.StageTime));
			Check(State, State.FightTravel >= 300.f, FString::Printf(TEXT("the squad kept moving in the fight (the leader %.0f cm)"), State.FightTravel));
			Check(State, State.MaxFollowerGap <= 1000.f, FString::Printf(TEXT("in formation while moving in the fight (followers at most %.0f cm from the leader)"), State.MaxFollowerGap));
			int32 Dead = 0;
			for (const TWeakObjectPtr<AEnemyCharacter>& Each : State.Patrol)
			{
				Dead += !Each.IsValid() || Each->IsDying() ? 1 : 0;
			}
			Check(State, Dead == State.Patrol.Num(), FString::Printf(TEXT("the patrol is down (%d of %d)"), Dead, State.Patrol.Num()));
			++State.FightsEnded;
			// Immortal + infinite ammo through both fights.
			Check(State, State.WorstHealthLoss <= 0.01f, FString::Printf(TEXT("immortal through the fights: no health lost (worst %.1f, %d hits blocked)"), State.WorstHealthLoss,
				BlockedHits(*Squad)));
			Check(State, State.WorstClipLoss <= 0, FString::Printf(TEXT("infinite ammo through the fights: no clip ever dropped (worst %d rounds)"), State.WorstClipLoss));
			int32 Alive = 0;
			for (const AOperativeCharacter* Member : Squad->GetMembers())
			{
				Alive += IsLiving(Member) ? 1 : 0;
			}
			Check(State, Alive == Squad->GetMembers().Num() && Flow->GetPhase() != ECodexGamePhase::GameOver, FString::Printf(TEXT("everyone alive (%d)"), Alive));
			// The nearest tower: the leader alone climbs it.
			float Best = MAX_flt;
			for (TActorIterator<ALadderActor> It(World); It; ++It)
			{
				const float Distance = static_cast<float>(FVector::Dist2D(It->GetBottomLinkWorld(), Leader->GetActorLocation()));
				if (Distance < Best)
				{
					Best = Distance;
					State.Tower = *It;
				}
			}
			ALadderActor* Tower = State.Tower.Get();
			if (!Tower)
			{
				Check(State, false, TEXT("a tower to climb"));
				return Finish(State, false);
			}
			Squad->EnterSoloMode();
			Check(State, ClickAt(World, Tower->GetTopLinkWorld() - Tower->GetOutward() * 100.f),
				FString::Printf(TEXT("solo: a click on tower %s's roof (%.0f m away)"), *Tower->GetName(), Best / 100.f));
			Enter(State, EStage::TowerWalk);
			return true;
		}

		case EStage::TowerWalk:
		{
			const ALadderActor* Tower = State.Tower.Get();
			const ULadderClimbComponent* Climb = Leader->LadderClimb.Get();
			const float FeetOver = Tower ? static_cast<float>(FeetOf(*Leader).Z) - Tower->GetTopWorldZ() : -1e6f;
			const float FromTop = Tower ? static_cast<float>(FVector::Dist2D(Leader->GetActorLocation(), Tower->GetTopLinkWorld() - Tower->GetOutward() * 100.f)) : 1e6f;
			const bool bOnTop = Tower && FMath::Abs(FeetOver) < 20.f && FromTop < 150.f && Climb && !Climb->IsOnLadder() && !Leader->IsMoving();
			if (!bOnTop && State.StageTime < 150.f)
			{
				return true;
			}
			Check(State, bOnTop, FString::Printf(TEXT("the leader climbed the tower and stands on top (feet %.0f cm over the roof, %.0f cm from the spot, %.1f s)"), FeetOver,
				FromTop, State.StageTime));
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
		TEXT("CodexTactics.FormationLabCombatSmoke"),
		TEXT("Dev check (combat lab 2026-11-01, L_FormationLabCombat): spawn points clear of walls, the endless timer under the cap, an ambusher holds while watched / ")
		TEXT("springs when unwatched, a patrol contact starts the fight while the squad moves, immortal + infinite ammo, a tower climbed; [shot]. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
