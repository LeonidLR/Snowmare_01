// Dev-only check of the Demo 01 canister quest in the lake's last compound (user decisions 2026-10-21; layout section 5k of
// Scripts/Editor/build_demo01_lake_layout.py; the chain the user built on L_MovementTest):
//   Scripts/smoke_parallel.ps1 -Commands LakeQuestSmoke   (map /Game/Maps/L_Demo01_A_FrozenLake + -LevelJson, Scripts/test_map.json)
//   rendered frames: UnrealEditor.exe <uproject> /Game/Maps/L_Demo01_A_FrozenLake -game -windowed -ResX=1600 -ResY=900
//       -LevelJson=L_Demo01_A_FrozenLake.json -ExecCmds="CodexTactics.LakeQuestSmoke Shot"  -> Saved/Screenshots/LakeQuest/NN_*.png
//  0. the layout: one jerrycan / BTR (the burnt BTR-82 art mesh) / generator / console, one blast door paced by the generator and the
//     waves, VOL_A7_YARD (beat A7, Fire Once off), CombatStart, the spawn points of the lake lanes (N x2, E hounds only, W, the dynamic
//     south breach); NAVMESH PATHS from the yard to every quest object and from every spawn point to the yard; the shut door blocks the
//     way through the east wall (only the 2 m back gap leads round). The two A4 zombies are parked out of the way.
//  1. the squad walks into the yard -> beat A7: the jerrycan objective;
//  2-5. the player's click path (UInteractionSubsystem::RequestInteraction + the action menu) for the jerrycan, the BTR's diesel, the
//     generator, the console: each raises QuestStep.<Kind> (subject = the operative) and moves the A7 objectives on; the generator heats;
//     the console starts A8 and, 1 s later, the sudden wave 1 on the lake's lanes (Frostbitten at the NORTH_ICE points, hounds at
//     EAST_GULLY - never the hard-coded fallback spot);
//  6-8. the blast door: unlocked but shut, it creeps open only while the fight runs and the generator works (Codex.Gate.ProgressPerSecond
//     raised for the check): the generator breaks down (GeneratorDamaged / GeneratorDown) -> the door stops; the Engineer repairs it
//     through its menu (GeneratorRepaired, subject = him) -> it moves again;
//  9. the door opens (OpenGate, the final-wave path is GateGeneratorSmoke's) -> GateOpened, the leaves slide away and the navmesh
//     path through the east wall becomes direct (the exit to the road).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/EnemySpawnPoint.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Core/PlayerCommands.h"
#include "Core/SaveGameSubsystem.h"
#include "Debug/EventBusProbe.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/GateActor.h"
#include "Interactables/HeatSourceComponent.h"
#include "Interactables/InteractableActor.h"
#include "Interactables/InteractionSubsystem.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Quests/QuestSubsystem.h"
#include "Subsystems/CodexEventBus.h"
#include "Tutorial/TutorialSubsystem.h"
#include "Tutorial/TutorialTriggerVolume.h"
#include "UnrealClient.h"
#include "UObject/StrongObjectPtr.h"

namespace LakeQuestSmoke
{
	constexpr float StepSeconds = 0.1f;
	/** The door's creep for the check (the level's 0.004 / s would take minutes). */
	constexpr float FastGateProgress = 0.05f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		bool bShots = false;
		int32 ShotIndex = 0;
		float ShotAt = -1.f;
		/** When the last frame was handed to the renderer (the next steps wait a moment so it shows that moment). */
		float FrameTakenAt = -10.f;
		/** The pending frame is taken without the HUD (the top views). */
		bool bShotWithoutUI = false;
		FString ShotName;
		TWeakObjectPtr<ACameraActor> TopCamera;
		TWeakObjectPtr<AInteractableActor> Canister;
		TWeakObjectPtr<AInteractableActor> Apc;
		TWeakObjectPtr<AInteractableActor> Generator;
		TWeakObjectPtr<AInteractableActor> Terminal;
		TWeakObjectPtr<AGateActor> Gate;
		/** The interaction of the current quest stage was requested / its menu confirmed. */
		bool bRequested = false;
		bool bConfirmed = false;
		float ProgressMark = 0.f;
		TStrongObjectPtr<UEventBusProbe> Probe;
		/** Every wave enemy as it spawned: its kind and where (UWaveSubsystem::OnEnemySpawnedNative). */
		TSharedPtr<TArray<TPair<EEnemyArchetype, FVector>>> Spawns = MakeShared<TArray<TPair<EEnemyArchetype, FVector>>>();
		bool bListening = false;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		if (IConsoleVariable* Rate = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Gate.ProgressPerSecond")))
		{
			Rate->Set(-1.f, ECVF_SetByConsole);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		State.Probe.Reset();
		FPlatformMisc::RequestExit(false, TEXT("LakeQuestSmoke"));
		return false;
	}

	void Next(FState& State)
	{
		++State.Stage;
		State.StageTime = 0.f;
		State.bRequested = false;
		State.bConfirmed = false;
	}

	/** "Shot" runs: a frame Delay seconds later (the camera is pointed by the caller). */
	void RequestFrame(FState& State, const TCHAR* Name, float Delay = 0.8f, bool bWithoutUI = false)
	{
		if (!State.bShots || State.ShotAt >= 0.f)
		{
			return;
		}
		State.bShotWithoutUI = bWithoutUI;
		State.ShotAt = State.Time + Delay;
		State.ShotName = FString::Printf(TEXT("%02d_%s.png"), ++State.ShotIndex, Name);
	}

	void TickFrame(UWorld* World, FState& State)
	{
		if (State.ShotAt >= 0.f && State.Time >= State.ShotAt)
		{
			const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("LakeQuest") / State.ShotName;
			FScreenshotRequest::RequestScreenshot(Path, /*bShowUI*/ !State.bShotWithoutUI, /*bAddFilenameSuffix*/ false);
			UE_LOG(LogCodexTactics, Display, TEXT("[LakeQuest] frame -> %s"), *Path);
			State.ShotAt = -1.f;
			State.FrameTakenAt = State.Time;
		}
	}

	/** "Shot" runs: the view from straight above Centre (Height cm up; +X to the right of the frame). */
	void TopView(UWorld* World, FState& State, const FVector& Centre, float Height)
	{
		APlayerController* PC = State.bShots ? World->GetFirstPlayerController() : nullptr;
		if (!PC)
		{
			return;
		}
		ACameraActor* Camera = State.TopCamera.Get();
		if (!Camera)
		{
			Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity);
			State.TopCamera = Camera;
		}
		Camera->SetActorLocationAndRotation(Centre + FVector(0.f, 0.f, Height), FRotator(-90.f, -90.f, 0.f));
		Camera->GetCameraComponent()->SetFieldOfView(60.f);
		Camera->GetCameraComponent()->bConstrainAspectRatio = false;
		PC->SetViewTarget(Camera);
	}

	/** "Shot" runs: a 3/4 view of Where from the south-west, Distance cm away. */
	void CloseView(UWorld* World, FState& State, const FVector& Where, float Distance)
	{
		const FVector From = Where + FVector(-0.55f, -0.55f, 0.62f).GetSafeNormal() * Distance;
		TopView(World, State, From, 0.f);
		if (ACameraActor* Camera = State.TopCamera.Get())
		{
			Camera->SetActorRotation((Where - From).Rotation());
		}
	}

	FVector Ground(UWorld* World, const FVector& Point)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(LakeQuestGround), false);
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			Params.AddIgnoredActor(*It);
		}
		return World->LineTraceSingleByChannel(Hit, Point + FVector(0.f, 0.f, 600.f), Point - FVector(0.f, 0.f, 2000.f), ECC_Visibility, Params)
			? Hit.ImpactPoint : Point;
	}

	/**
	 * The nearest navmesh spot to Point (rings up to 4 m) where a standing capsule is not BLOCKED (SmokeUtils::FreeSpot also refuses
	 * touching overlaps - inside the yard's tutorial volume every spot "overlaps" and it fell back up to 6 m away, outside the wall).
	 */
	FVector YardSpot(UWorld* World, const FVector& Point, const AActor* Ignore)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(LakeQuestSpot), false, Ignore);
		for (const float Radius : { 0.f, 80.f, 160.f, 250.f, 400.f })
		{
			for (int32 Step = 0; Step < (Radius <= 0.f ? 1 : 12); ++Step)
			{
				const FVector Candidate = Point + FVector(Radius, 0.f, 0.f).RotateAngleAxis(Step * 30.f, FVector::UpVector);
				FNavLocation OnNav;
				if (Nav && Nav->ProjectPointToNavigation(Candidate, OnNav, FVector(40.f, 40.f, 300.f))
					&& !World->OverlapBlockingTestByChannel(OnNav.Location + FVector(0.f, 0.f, 95.f), FQuat::Identity, ECC_Pawn,
						FCollisionShape::MakeCapsule(42.f, 88.f), Params))
				{
					return OnNav.Location + FVector(0.f, 0.f, 92.f);
				}
			}
		}
		return Point + FVector(0.f, 0.f, 92.f);
	}

	/** The squad stands round Spot (the leader on it), stopped. */
	void PlaceSquad(UWorld* World, USquadSubsystem& Squad, const FVector& Spot)
	{
		int32 Slot = 0;
		for (AOperativeCharacter* Member : Squad.GetMembers())
		{
			Member->StopOperative();
			const FVector Offset = Member == Squad.GetLeader() ? FVector::ZeroVector : FVector(-200.f, (Slot++ == 0 ? -1.f : 1.f) * 160.f, 0.f);
			const FVector Free = YardSpot(World, Ground(World, Spot + Offset), Member);
			Member->TeleportTo(Free + FVector(0.f, 0.f, 4.f), Member->GetActorRotation(), false, true);
			UE_LOG(LogCodexTactics, Display, TEXT("[LakeQuest] %s placed at %s (asked %s)"), *Member->DisplayName.ToString(), *Free.ToCompactString(),
				*(Spot + Offset).ToCompactString());
		}
	}

	/** Navmesh path From -> To for the operatives' agent: its length, or -1 when none / partial. */
	float PathLength(UWorld* World, const FVector& From, const FVector& To, AActor* Querier)
	{
		UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, From, To, Querier);
		return Path && Path->IsValid() && !Path->IsPartial() ? Path->GetPathLength() : -1.f;
	}

	/** One quest stage on the player's click path: request, confirm the menu (a frame of it first in Shot runs). Returns true when done. */
	bool RunInteraction(UWorld* World, FState& State, AInteractableActor* Target, const TCHAR* FrameName, TFunctionRef<bool()> IsDone)
	{
		UInteractionSubsystem* Interactions = World->GetSubsystem<UInteractionSubsystem>();
		if (!State.bRequested)
		{
			Interactions->RequestInteraction(Target);
			State.bRequested = true;
			return false;
		}
		if (Interactions->IsActionMenuOpen() && !State.bConfirmed)
		{
			if (State.bShots && State.ShotAt < 0.f && FrameName && State.ShotName.Find(FrameName) == INDEX_NONE)
			{
				CloseView(World, State, Target->GetActorLocation(), 1400.f);
				RequestFrame(State, FrameName, 0.6f);
				return false;
			}
			if (State.ShotAt >= 0.f || State.Time < State.FrameTakenAt + 0.4f)
			{
				return false; // the frame with the menu first (rendered a moment after the request)
			}
			const FActionMenuSpec& Menu = Interactions->GetActionMenu();
			UE_LOG(LogCodexTactics, Display, TEXT("[LakeQuest] menu \"%s\" -> \"%s\"%s"), *Menu.Title.ToString(), *Menu.ConfirmText.ToString(),
				Menu.bConfirmDisabled ? TEXT(" (disabled)") : TEXT(""));
			Interactions->ConfirmActionMenu();
			State.bConfirmed = true;
			return false;
		}
		return IsDone();
	}

	int32 CountEvent(const FState& State, const TCHAR* Name, FName SubjectId = NAME_None)
	{
		return State.Probe ? State.Probe->CountEvent(FName(Name), SubjectId) : 0;
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
		TickFrame(World, State);
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UTutorialSubsystem* Tutorial = World->GetSubsystem<UTutorialSubsystem>();
		UQuestSubsystem* Quests = World->GetSubsystem<UQuestSubsystem>();
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Squad || !Tutorial || !Quests || !Flow || !Leader)
		{
			if (State.Time > 120.f)
			{
				Check(State, false, TEXT("a squad, the tutorial and the quest subsystem in the world"));
				return Finish(State, false);
			}
			return true;
		}
		if (State.Time > 400.f)
		{
			Check(State, false, FString::Printf(TEXT("time out in stage %d"), State.Stage));
			return Finish(State, false);
		}
		if (State.Stage > 0 && State.StageTime > 40.f)
		{
			Check(State, false, FString::Printf(TEXT("stage %d took more than 40 s"), State.Stage));
			return Finish(State, false);
		}
		AInteractableActor* Canister = State.Canister.Get();
		AInteractableActor* Apc = State.Apc.Get();
		AInteractableActor* Generator = State.Generator.Get();
		AInteractableActor* Terminal = State.Terminal.Get();
		AGateActor* Gate = State.Gate.Get();

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
			if (State.StageTime < 3.f)
			{
				return true; // the navmesh of the far end of the lake builds a moment longer
			}
			if (USaveGameSubsystem* Saves = World->GetSubsystem<USaveGameSubsystem>())
			{
				Saves->bCheckpointsEnabled = false; // the beat checkpoints would overwrite the player's slot
			}
			State.Probe.Reset(NewObject<UEventBusProbe>());
			State.Probe->ListenGameplayEvents(UCodexEventBus::Get(World));
			// The quest objects: exactly one of each.
			TMap<EInteractableType, TArray<AInteractableActor*>> Objects;
			for (TActorIterator<AInteractableActor> It(World); It; ++It)
			{
				if (It->GetClass() == AInteractableActor::StaticClass())
				{
					Objects.FindOrAdd(It->ObjectType).Add(*It);
				}
			}
			auto One = [&Objects](EInteractableType Type) { const TArray<AInteractableActor*>* Found = Objects.Find(Type); return Found && Found->Num() == 1 ? (*Found)[0] : nullptr; };
			State.Canister = Canister = One(EInteractableType::Canister);
			State.Apc = Apc = One(EInteractableType::Vehicle);
			State.Generator = Generator = One(EInteractableType::Generator);
			State.Terminal = Terminal = One(EInteractableType::GateTerminal);
			int32 Gates = 0;
			for (TActorIterator<AGateActor> It(World); It; ++It)
			{
				++Gates;
				State.Gate = Gate = *It;
			}
			Check(State, Canister && Apc && Generator && Terminal && Gates == 1,
				FString::Printf(TEXT("one jerrycan / BTR / generator / console and one blast door (%d) on the lake"), Gates));
			if (!Canister || !Apc || !Generator || !Terminal || !Gate)
			{
				return Finish(State, false);
			}
			Check(State, Apc->VisualMesh && Apc->VisualMesh->GetName().Contains(TEXT("BTR82")) && Apc->Mesh->GetStaticMesh() == Apc->VisualMesh
				&& Apc->Mesh->GetRelativeScale3D().Equals(FVector::OneVector), TEXT("the BTR shows the burnt BTR-82 mesh, not stretched"));
			Check(State, Gate->UsesGeneratorPacing() && !Gate->IsProgressUnlocked() && Gate->AreLeavesClosed(), TEXT("the blast door: shut, paced by the generator and the waves"));
			for (const AInteractableActor* Object : { Canister, Apc, Generator, Terminal })
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[LakeQuest] %s %s at %s"), *Object->GetActorNameOrLabel(), *UEnum::GetValueAsString(Object->ObjectType),
					*Object->GetActorLocation().ToCompactString());
			}
			// The A7 yard volume.
			const ATutorialTriggerVolume* Yard = nullptr;
			for (TActorIterator<ATutorialTriggerVolume> It(World); It; ++It)
			{
				Yard = It->GetEffectiveVolumeId() == FName(TEXT("VOL_A7_YARD")) ? *It : Yard;
			}
			Check(State, Yard && Yard->BeatId == FName(TEXT("A7")) && !Yard->bFireOnce, TEXT("VOL_A7_YARD starts A7 (Fire Once off)"));
			// CombatStart + the spawn points of the lake lanes.
			const AActor* CombatStart = nullptr;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				CombatStart = It->ActorHasTag(TEXT("CombatStart")) ? *It : CombatStart;
			}
			Check(State, CombatStart != nullptr, TEXT("the CombatStart point («Start Battle»)"));
			TMap<FString, int32> Lanes;
			const AEnemySpawnPoint* East = nullptr;
			int32 Dynamic = 0;
			TArray<const AEnemySpawnPoint*> Points;
			for (TActorIterator<AEnemySpawnPoint> It(World); It; ++It)
			{
				Points.Add(*It);
				Dynamic += It->bIsDynamic ? 1 : 0;
				if (!It->bIsDynamic)
				{
					++Lanes.FindOrAdd(It->SpawnLane);
					East = It->SpawnLane == TEXT("EAST_GULLY") ? *It : East;
				}
			}
			Check(State, Lanes.FindRef(TEXT("NORTH_ICE")) == 2 && Lanes.FindRef(TEXT("EAST_GULLY")) == 1 && Lanes.FindRef(TEXT("WEST_SHORE")) == 1 && Dynamic == 1,
				FString::Printf(TEXT("spawn points: NORTH_ICE %d, EAST_GULLY %d, WEST_SHORE %d, dynamic breach %d"), Lanes.FindRef(TEXT("NORTH_ICE")),
					Lanes.FindRef(TEXT("EAST_GULLY")), Lanes.FindRef(TEXT("WEST_SHORE")), Dynamic));
			Check(State, East && East->Accepts(EEnemyArchetype::FrostHound) && !East->Accepts(EEnemyArchetype::Frostbitten), TEXT("the east gully takes hounds only"));
			// The placed A4 zombies are not part of this check.
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->SetActorTickEnabled(false);
				SmokeUtils::ParkEnemyFarAway(*It);
			}
			// Navmesh: the yard -> every quest object, every spawn point -> the yard.
			const FVector YardPoint = CombatStart ? CombatStart->GetActorLocation() : Generator->GetActorLocation();
			for (AInteractableActor* Object : { Canister, Apc, Generator, Terminal })
			{
				const float Length = PathLength(World, YardPoint, Object->GetInteractionApproachPoint(YardPoint), Leader);
				Check(State, Length > 0.f, FString::Printf(TEXT("navmesh path yard -> %s (%.1f m)"), *Object->GetActorNameOrLabel(), Length / 100.f));
			}
			for (const AEnemySpawnPoint* Point : Points)
			{
				const float Length = PathLength(World, Point->GetActorLocation(), YardPoint, Leader);
				Check(State, Length > 0.f, FString::Printf(TEXT("navmesh path %s (%s) -> the yard (%.1f m)"), *Point->GetActorNameOrLabel(), *Point->SpawnLane, Length / 100.f));
			}
			if (East)
			{
				const float Length = PathLength(World, East->GetActorLocation(), Generator->GetInteractionApproachPoint(East->GetActorLocation()), Leader);
				Check(State, Length > 0.f, FString::Printf(TEXT("the hidden lane: east gully -> the generator (%.1f m, through the 2 m back gap)"), Length / 100.f));
			}
			// The shut door blocks the way through the east wall: round by the back gap only.
			const FVector Outside = Gate->GetActorTransform().TransformPosition(FVector(300.f, 0.f, 100.f));
			const FVector Inside = Gate->GetActorTransform().TransformPosition(FVector(-300.f, 0.f, 100.f));
			const float Shut = PathLength(World, Outside, Inside, Leader);
			Check(State, Shut < 0.f || Shut > 1500.f, FString::Printf(TEXT("the shut blast door blocks the passage (path %.1f m for 6 m across)"), Shut / 100.f));
			if (State.bShots)
			{
				TopView(World, State, FVector(30000.f, -2300.f, 0.f), 7600.f);
				RequestFrame(State, TEXT("compound_top"), 1.5f, true);
			}
			Next(State);
			return true;
		}
		case 1:
		{
			if (State.ShotAt >= 0.f || State.Time < State.FrameTakenAt + 0.5f)
			{
				return true;
			}
			if (!State.bRequested)
			{
				// Into the yard (the leader enters VOL_A7_YARD).
				PlaceSquad(World, *Squad, FVector(30500.f, -2490.f, 0.f));
				State.bRequested = true;
				State.StageTime = 0.f;
				return true;
			}
			if (Tutorial->GetStepState(TEXT("A7_obj_canister")) != ETutorialStepState::Active && State.StageTime < 15.f)
			{
				return true;
			}
			Check(State, Tutorial->GetCurrentBeat() == FName(TEXT("A7")) && Tutorial->GetStepState(TEXT("A7_obj_canister")) == ETutorialStepState::Active,
				FString::Printf(TEXT("the yard starts A7: the jerrycan objective (beat %s)"), *Tutorial->GetCurrentBeat().ToString()));
			PlaceSquad(World, *Squad, Canister->GetActorLocation() + FVector(350.f, 0.f, 0.f));
			Next(State);
			return true;
		}
		case 2:
		{
			if (!RunInteraction(World, State, Canister, TEXT("take_jerrycan"), [Quests]() { return Quests->HasEmptyCanister(); }))
			{
				return true;
			}
			Check(State, Canister->IsHidden(), TEXT("the jerrycan is taken (hidden)"));
			Check(State, CountEvent(State, TEXT("QuestStep.CanisterTaken"), Leader->GetFName()) == 1 && CountEvent(State, TEXT("QuestStep")) == 1,
				TEXT("QuestStep.CanisterTaken + QuestStep by the leader"));
			PlaceSquad(World, *Squad, Apc->GetInteractionApproachPoint(Apc->GetActorLocation() - FVector(900.f, 0.f, 0.f)) - FVector(150.f, 0.f, 0.f));
			Next(State);
			return true;
		}
		case 3:
		{
			if (!RunInteraction(World, State, Apc, TEXT("drain_diesel"), [Quests]() { return Quests->HasFuelCanister(); }))
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("A7_obj_canister")) == ETutorialStepState::Completed, TEXT("objective 1 (the jerrycan) completed"));
			Check(State, CountEvent(State, TEXT("QuestStep.FuelDrained")) == 1, TEXT("QuestStep.FuelDrained"));
			Check(State, Quests->GetObjective().ToString().Contains(TEXT("generator")), FString::Printf(TEXT("the quest objective: \"%s\""), *Quests->GetObjective().ToString()));
			PlaceSquad(World, *Squad, Generator->GetInteractionApproachPoint(Generator->GetActorLocation() - FVector(0.f, 900.f, 0.f)) - FVector(0.f, 250.f, 0.f));
			Next(State);
			return true;
		}
		case 4:
		{
			if (!RunInteraction(World, State, Generator, TEXT("refuel_generator"), [Quests]() { return Quests->IsGeneratorRunning(); }))
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("A7_obj_drain")) == ETutorialStepState::Completed, TEXT("objective 2 (the diesel) completed"));
			Check(State, CountEvent(State, TEXT("QuestStep.GeneratorStarted")) == 1 && Generator->HeatSource->IsHeatActive(), TEXT("QuestStep.GeneratorStarted, the generator heats"));
			PlaceSquad(World, *Squad, Terminal->GetActorLocation() - FVector(300.f, 0.f, 0.f));
			Next(State);
			return true;
		}
		case 5:
		{
			if (State.StageTime < 1.f)
			{
				return true; // the objective / prep card follow 0.5 s after the generator
			}
			if (!State.bRequested)
			{
				Check(State, Tutorial->GetStepState(TEXT("A7_obj_generator")) == ETutorialStepState::Completed
					&& Tutorial->GetStepState(TEXT("A7_obj_terminal")) == ETutorialStepState::Active,
					TEXT("objective 3 (the generator) completed, objective 4 (the console) up"));
			}
			if (!State.bListening)
			{
				if (UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>())
				{
					TSharedPtr<TArray<TPair<EEnemyArchetype, FVector>>> Spawns = State.Spawns;
					Waves->OnEnemySpawnedNative.AddLambda([Spawns](AEnemyCharacter* Enemy, EEnemyArchetype Archetype)
					{
						Spawns->Add(TPair<EEnemyArchetype, FVector>(Archetype, Enemy ? Enemy->GetActorLocation() : FVector::ZeroVector));
					});
				}
				State.bListening = true;
			}
			if (!RunInteraction(World, State, Terminal, TEXT("blast_door_console"), [Quests]() { return Quests->IsGatePowered(); }))
			{
				return true;
			}
			Check(State, CountEvent(State, TEXT("QuestStep.GatePowered")) == 1 && CountEvent(State, TEXT("QuestStep")) == 4, TEXT("QuestStep.GatePowered (4 steps in all)"));
			Next(State);
			return true;
		}
		case 6:
		{
			if (Flow->GetPhase() == ECodexGamePhase::WaveCombat && (Flow->IsAwaitingHordeIntro() || Flow->IsFreePause()))
			{
				SmokeUtils::ReleaseHordeIntroPause(Flow); // real time: the wave's later spawns (the hounds, 2 s) come on
			}
			if ((Flow->GetPhase() != ECodexGamePhase::WaveCombat || State.Spawns->Num() < 6) && State.StageTime < 20.f)
			{
				return true;
			}
			Check(State, Tutorial->GetCurrentBeat() == FName(TEXT("A8")) && Tutorial->GetStepState(TEXT("A7_obj_terminal")) == ETutorialStepState::Completed
				&& Tutorial->GetStepState(TEXT("A8_radio_horde")) != ETutorialStepState::Idle, TEXT("the console starts A8 (HQ: they heard it)"));
			Check(State, Flow->GetPhase() == ECodexGamePhase::WaveCombat && Flow->GetWaveIndex() == 1, TEXT("the console starts wave 1 at once (sudden)"));
			// Each enemy came from its lane's spawn point (never the hard-coded fallback).
			TArray<const AEnemySpawnPoint*> North;
			const AEnemySpawnPoint* East = nullptr;
			for (TActorIterator<AEnemySpawnPoint> It(World); It; ++It)
			{
				if (It->SpawnLane == TEXT("NORTH_ICE"))
				{
					North.Add(*It);
				}
				East = It->SpawnLane == TEXT("EAST_GULLY") ? *It : East;
			}
			int32 Frost = 0;
			int32 FrostAtNorth = 0;
			int32 Hounds = 0;
			int32 HoundsAtEast = 0;
			for (const TPair<EEnemyArchetype, FVector>& Spawn : *State.Spawns)
			{
				const FVector Where = Spawn.Value;
				auto Near = [&Where](const AEnemySpawnPoint* Point) { return Point && FVector::Dist2D(Point->GetActorLocation(), Where) < 800.f; };
				UE_LOG(LogCodexTactics, Display, TEXT("[LakeQuest] spawned %s at %s"), *UEnum::GetValueAsString(Spawn.Key), *Where.ToCompactString());
				if (Spawn.Key == EEnemyArchetype::Frostbitten)
				{
					++Frost;
					FrostAtNorth += North.ContainsByPredicate(Near) ? 1 : 0;
				}
				else if (Spawn.Key == EEnemyArchetype::FrostHound)
				{
					++Hounds;
					HoundsAtEast += Near(East) ? 1 : 0;
				}
			}
			Check(State, Frost == 4 && FrostAtNorth == 4 && Hounds == 2 && HoundsAtEast == 2,
				FString::Printf(TEXT("wave 1 on the lake's lanes: Frostbitten %d (%d at NORTH_ICE), hounds %d (%d at EAST_GULLY)"), Frost, FrostAtNorth, Hounds, HoundsAtEast));
			Check(State, Gate->IsProgressUnlocked() && !Gate->IsOpen() && !Gate->IsOpening() && Gate->AreLeavesClosed(),
				FString::Printf(TEXT("the console only unlocked the door: shut for the fight (\"%s\")"), *Gate->GetStatusText()));
			SmokeUtils::ReleaseHordeIntroPause(Flow);
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->SetActorTickEnabled(false); // the fight stays on (the wave is alive), nobody reaches the generator
				SmokeUtils::ParkEnemyFarAway(*It);
			}
			if (IConsoleVariable* Rate = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Gate.ProgressPerSecond")))
			{
				Rate->Set(FastGateProgress, ECVF_SetByConsole);
			}
			State.ProgressMark = Gate->GetOpenProgress();
			Next(State);
			return true;
		}
		case 7:
		{
			if (State.StageTime < 3.f)
			{
				return true;
			}
			if (State.bShots && State.ShotName.Find(TEXT("gate_progress")) == INDEX_NONE)
			{
				CloseView(World, State, Gate->GetActorLocation() + FVector(-400.f, 0.f, 200.f), 2200.f);
				RequestFrame(State, TEXT("gate_progress"), 0.6f);
			}
			if (State.ShotAt >= 0.f || State.Time < State.FrameTakenAt + 0.4f)
			{
				return true;
			}
			const float Progress = Gate->GetOpenProgress();
			Check(State, Progress > State.ProgressMark + 0.05f && Gate->IsBeaconSpinning() && Gate->AreLeavesClosed(),
				FString::Printf(TEXT("the door creeps while the fight runs and the generator works: %.0f%% -> %.0f%%, beacon on, leaves shut (\"%s\")"),
					State.ProgressMark * 100.f, Progress * 100.f, *Gate->GetStatusText()));
			Generator->TakeGeneratorDamage(Generator->GeneratorMaxHealth + 10.f);
			Check(State, Generator->bGeneratorBroken && CountEvent(State, TEXT("GeneratorDamaged")) >= 1 && CountEvent(State, TEXT("GeneratorDown")) == 1,
				TEXT("the generator breaks down: GeneratorDamaged + GeneratorDown"));
			State.ProgressMark = Gate->GetOpenProgress();
			Next(State);
			return true;
		}
		case 8:
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			if (!State.bRequested)
			{
				Check(State, FMath::IsNearlyEqual(Gate->GetOpenProgress(), State.ProgressMark, 0.002f) && !Gate->IsBeaconSpinning(),
					FString::Printf(TEXT("generator down: the door stops (%.1f%%, \"%s\")"), Gate->GetOpenProgress() * 100.f, *Gate->GetStatusText()));
				// The Engineer repairs it through its menu.
				for (AOperativeCharacter* Member : Squad->GetMembers())
				{
					if (Member->SquadRole == EOperativeRole::Engineer)
					{
						Squad->SetLeader(Member);
					}
				}
				// In the yard 6 m in front of the shed door (the shed's back faces the compound wall: no way round there).
				PlaceSquad(World, *Squad, Generator->GetActorLocation() - FVector(0.f, 650.f, 0.f));
			}
			AOperativeCharacter* Engineer = Squad->GetLeader();
			if (!State.bRequested && Engineer)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[LakeQuest] repair: %s at %s, approach %s"), *Engineer->DisplayName.ToString(),
					*Engineer->GetActorLocation().ToCompactString(), *Generator->GetInteractionApproachPoint(Engineer->GetActorLocation()).ToCompactString());
			}
			if (!RunInteraction(World, State, Generator, TEXT("generator_down_repair"), [Generator]() { return !Generator->bGeneratorBroken; }))
			{
				return true;
			}
			Check(State, Engineer && Engineer->SquadRole == EOperativeRole::Engineer && CountEvent(State, TEXT("GeneratorRepaired"), Engineer->GetFName()) == 1,
				TEXT("the Engineer repairs it through its menu: GeneratorRepaired (subject = him)"));
			State.ProgressMark = Gate->GetOpenProgress();
			Next(State);
			return true;
		}
		case 9:
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			if (!Gate->IsOpening() && !Gate->IsOpen())
			{
				Check(State, Gate->GetOpenProgress() > State.ProgressMark + 0.03f, FString::Printf(TEXT("repaired: the door moves again (%.0f%%)"), Gate->GetOpenProgress() * 100.f));
				Gate->OpenGate(); // the final-wave path (wave 3 cleared with the generator up) is GateGeneratorSmoke's
				Check(State, CountEvent(State, TEXT("GateOpened")) == 1, TEXT("GateOpened when the leaves start to slide"));
				return true;
			}
			if (!Gate->IsOpen())
			{
				return true;
			}
			const FVector Outside = Gate->GetActorTransform().TransformPosition(FVector(300.f, 0.f, 100.f));
			const FVector Inside = Gate->GetActorTransform().TransformPosition(FVector(-300.f, 0.f, 100.f));
			const float Through = PathLength(World, Outside, Inside, Leader);
			if ((Through < 0.f || Through > 900.f) && State.StageTime < 15.f)
			{
				return true; // the navmesh rebuilds round the opened leaves
			}
			Check(State, Through > 0.f && Through <= 900.f, FString::Printf(TEXT("the open blast door is the way through the east wall (%.1f m for 6 m across)"), Through / 100.f));
			if (State.bShots)
			{
				TopView(World, State, FVector(30000.f, -2300.f, 0.f), 7600.f);
				RequestFrame(State, TEXT("compound_top_door_open"), 1.0f, true);
			}
			Next(State);
			return true;
		}
		case 10:
		{
			if (State.ShotAt >= 0.f || (State.bShots && State.Time < State.FrameTakenAt + 1.f))
			{
				return true; // the last frame is written before the exit
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
		TEXT("CodexTactics.LakeQuestSmoke"),
		TEXT("Dev check (Demo 01 A7, 2026-10-21): the canister quest in the lake's last compound - layout + navmesh paths, the A7 objectives, jerrycan -> BTR diesel -> generator -> console through the click path, wave 1 on the lake lanes, the blast door paced by the generator (breakdown / Engineer repair), the open door as the exit. 'Shot' = rendered frames."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
