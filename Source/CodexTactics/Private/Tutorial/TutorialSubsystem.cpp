#include "Tutorial/TutorialSubsystem.h"
#include "AI/PatrolRouteActor.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Characters/SquadTransferSubsystem.h"
#include "CodexTactics.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Combat/KnockdownComponent.h"
#include "Core/DemoModeSubsystem.h"
#include "Core/MissionSessionSubsystem.h"
#include "Core/SaveGameRules.h"
#include "Core/SaveGameSubsystem.h"
#include "Data/NarrativeManifest.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Interactables/InteractableActor.h"
#include "Interactables/TrapSpotRules.h"
#include "Level/BridgeCrossingVolume.h"
#include "Level/LevelObstacleRules.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Subsystems/CodexEventBus.h"
#include "Survival/ColdSurvivalComponent.h"
#include "TimerManager.h"
#include "Tutorial/TutorialMarkerActor.h"
#include "Tutorial/TutorialTriggerVolume.h"
#include "UI/DialogueRules.h"
#include "UI/FloatingTextSubsystem.h"
#include "UI/GameMessageSubsystem.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"

#define LOCTEXT_NAMESPACE "TutorialSubsystem"

const TCHAR* UTutorialSubsystem::CheckpointSlot = TEXT("checkpoint");

namespace TutorialConsole
{
	static TAutoConsoleVariable<int32> CVarTutorialEnabled(TEXT("Codex.Tutorial.Enabled"), 1,
		TEXT("Tutorial hints / objectives / radio lines on demo levels: 1 on (default, the player's setting), 0 off."));
	static TAutoConsoleVariable<int32> CVarTutorialTelemetry(TEXT("Codex.Tutorial.Telemetry"), 1,
		TEXT("1: append tutorial step events to Saved/Tutorial/tutorial.jsonl (testers' telemetry); 0: off."));

	FString ProfilePath()
	{
		return FPaths::ProjectSavedDir() / TEXT("Tutorial/profile.json");
	}

	bool bProfileRead = false;

	/** The saved player setting overrides the CVar default once per process (the console still wins afterwards). */
	void ReadProfileOnce()
	{
		if (bProfileRead)
		{
			return;
		}
		bProfileRead = true;
		FString Json;
		TSharedPtr<FJsonObject> Root;
		bool bEnabled = true;
		if (FFileHelper::LoadFileToString(Json, *ProfilePath()) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root)
			&& Root.IsValid() && Root->TryGetBoolField(TEXT("enabled"), bEnabled))
		{
			CVarTutorialEnabled->Set(bEnabled ? 1 : 0, ECVF_SetByCode);
		}
	}

	void WriteProfile(bool bEnabled)
	{
		const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("enabled"), bEnabled);
		FString Json;
		FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Json));
		FFileHelper::SaveStringToFile(Json, *ProfilePath(), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

#if !UE_BUILD_SHIPPING
	static FAutoConsoleCommandWithWorldAndArgs SkipCommand(TEXT("Codex.Tutorial.Skip"), TEXT("Skips the visible tutorial hint card."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			if (UTutorialSubsystem* Tutorial = World ? World->GetSubsystem<UTutorialSubsystem>() : nullptr)
			{
				Tutorial->SkipActiveCard();
			}
		}));
	static FAutoConsoleCommandWithWorldAndArgs SkipTutorialCommand(TEXT("Codex.Tutorial.SkipTutorial"),
		TEXT("SKIP TUTORIAL without the confirm dialog: the tutorial is off for this run, every lock / confinement released."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			if (UTutorialSubsystem* Tutorial = World ? World->GetSubsystem<UTutorialSubsystem>() : nullptr)
			{
				Tutorial->SkipTutorial();
			}
		}));
	static FAutoConsoleCommandWithWorldAndArgs ResetCommand(TEXT("Codex.Tutorial.Reset"), TEXT("Forgets the tutorial progress and starts it again."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			if (UTutorialSubsystem* Tutorial = World ? World->GetSubsystem<UTutorialSubsystem>() : nullptr)
			{
				Tutorial->ResetTutorial();
			}
		}));
	static FAutoConsoleCommandWithWorldAndArgs RetryCommand(TEXT("Codex.Tutorial.Retry"), TEXT("Retry from the tutorial checkpoint (slot \"checkpoint\")."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			if (UTutorialSubsystem* Tutorial = World ? World->GetSubsystem<UTutorialSubsystem>() : nullptr)
			{
				Tutorial->RetryFromCheckpoint();
			}
		}));
	static FAutoConsoleCommandWithWorldAndArgs StepCommand(TEXT("Codex.Tutorial.Step"), TEXT("Codex.Tutorial.Step <StepId>: fires a tutorial step now."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UTutorialSubsystem* Tutorial = World ? World->GetSubsystem<UTutorialSubsystem>() : nullptr;
			if (Tutorial && Args.Num() > 0)
			{
				Tutorial->TriggerStep(FName(*Args[0]));
			}
		}));
#endif
}

namespace TutorialConfineLocal
{
	/** Margin around an allowed volume: a destination projected to the navmesh / a path end may lie just outside the box. */
	constexpr float AreaMarginCm = 50.f;
	/** Height margin: volumes are placed at ground level, clicks hit the floor or a wall face. */
	constexpr float AreaMarginZCm = 300.f;
	/** A formation target pulled into the allowed area stays this far inside its sides. */
	constexpr float ClampInsetCm = 60.f;
	/** "squad_not_low_in.<volume>": an operative within this distance of the volume's box counts as there (followers round the leader). */
	constexpr float NearVolumeCm = 300.f;

	bool IsInsideBox(const UBoxComponent& Box, const FVector& Point, float MarginXY, float MarginZ)
	{
		const FVector Local = Box.GetComponentTransform().InverseTransformPosition(Point);
		const FVector Scale = Box.GetComponentTransform().GetScale3D().GetAbs();
		const FVector Extent = Box.GetUnscaledBoxExtent();
		// The margins are world cm: convert them to the box's local units.
		const float MX = Scale.X > KINDA_SMALL_NUMBER ? MarginXY / Scale.X : MarginXY;
		const float MY = Scale.Y > KINDA_SMALL_NUMBER ? MarginXY / Scale.Y : MarginXY;
		const float MZ = Scale.Z > KINDA_SMALL_NUMBER ? MarginZ / Scale.Z : MarginZ;
		return FMath::Abs(Local.X) <= Extent.X + MX && FMath::Abs(Local.Y) <= Extent.Y + MY && FMath::Abs(Local.Z) <= Extent.Z + MZ;
	}
}

namespace TutorialSubsystemLocal
{
	/** "focus": "wounded_operative" (the medkit lesson); every other focus kind is the coldest operative. */
	bool IsWoundedFocus(FName Focus)
	{
		return Focus == FName(TEXT("wounded_operative"));
	}

	/** 2026-10-20: a focus chosen when the step is TRIGGERED (the wounded one, a role - the A4 ambusher), not when it shows. */
	bool IsTriggerFocus(FName Focus)
	{
		return IsWoundedFocus(Focus) || !TutorialRules::ParseRoleFocus(Focus).IsNone();
	}

	/** The waypoints of a patrol route (world). */
	TArray<FVector> TutorialRoutePoints(const APatrolRouteActor& Route)
	{
		TArray<FVector> Points;
		for (int32 Index = 0; Index < Route.GetNumberOfWaypoints(); ++Index)
		{
			Points.Add(Route.GetWaypointWorldLocation(Index));
		}
		return Points;
	}

	/** An actor's label in editor builds (the "enemies" selectors), empty in cooked builds. */
	FString TutorialActorLabel(const AActor& Actor)
	{
#if WITH_EDITOR
		return Actor.GetActorLabel(false);
#else
		return FString();
#endif
	}

	/** Actor matches any of the "enemies" selectors (label, object name, tag). */
	bool TutorialActorMatchesAny(const AActor& Actor, const TArray<FString>& Selectors)
	{
		const FString Label = TutorialActorLabel(Actor);
		return Selectors.ContainsByPredicate([&Actor, &Label](const FString& Selector)
		{
			return TutorialRules::MatchesActorSelector(Selector, Actor.GetFName(), Label, Actor.Tags);
		});
	}

	/** A squad member that counts for the squad stats: present and not dead (no health component = alive). */
	bool IsLivingOrUntracked(const AOperativeCharacter* Member)
	{
		return Member && (!Member->HealthComponent || Member->HealthComponent->IsAlive());
	}

	/** The tutorial of WorldContext's world (the static choke points), null without one. */
	UTutorialSubsystem* FindTutorialFor(const UObject* WorldContext)
	{
		UWorld* World = GEngine && WorldContext ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
		return World ? World->GetSubsystem<UTutorialSubsystem>() : nullptr;
	}
}

bool UTutorialSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UTutorialSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTutorialSubsystem, STATGROUP_Tickables);
}

void UTutorialSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	TutorialConsole::ReadProfileOnce();
	SessionId = FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(12);
	BindWorldEvents();
}

void UTutorialSubsystem::Deinitialize()
{
	if (UCodexEventBus* Bus = bBusBound ? UCodexEventBus::Get(this) : nullptr)
	{
		Bus->OnPlayerCommand.RemoveDynamic(this, &UTutorialSubsystem::HandleBusCommand);
		Bus->OnGameplayEvent.RemoveDynamic(this, &UTutorialSubsystem::HandleBusEvent);
		Bus->OnMineSpotted.RemoveDynamic(this, &UTutorialSubsystem::HandleMineSpotted);
		Bus->OnItemUsed.RemoveDynamic(this, &UTutorialSubsystem::HandleItemUsed);
		Bus->OnSoldierDowned.RemoveDynamic(this, &UTutorialSubsystem::HandleSoldierDowned);
		Bus->OnDialogueFinished.RemoveDynamic(this, &UTutorialSubsystem::HandleDialogueFinished);
	}
	bBusBound = false;
	for (const TWeakObjectPtr<AOperativeCharacter>& Member : BoundMembers)
	{
		if (AOperativeCharacter* Operative = Member.Get(); Operative && Operative->HealthComponent)
		{
			Operative->HealthComponent->LethalDamageGuard.Unbind();
		}
	}
	BoundMembers.Reset();
	Super::Deinitialize();
}

void UTutorialSubsystem::BindWorldEvents()
{
	if (UCodexEventBus* Bus = UCodexEventBus::Get(this))
	{
		Bus->OnPlayerCommand.AddUniqueDynamic(this, &UTutorialSubsystem::HandleBusCommand);
		Bus->OnGameplayEvent.AddUniqueDynamic(this, &UTutorialSubsystem::HandleBusEvent);
		Bus->OnMineSpotted.AddUniqueDynamic(this, &UTutorialSubsystem::HandleMineSpotted);
		Bus->OnItemUsed.AddUniqueDynamic(this, &UTutorialSubsystem::HandleItemUsed);
		Bus->OnSoldierDowned.AddUniqueDynamic(this, &UTutorialSubsystem::HandleSoldierDowned);
		Bus->OnDialogueFinished.AddUniqueDynamic(this, &UTutorialSubsystem::HandleDialogueFinished);
		bBusBound = true;
	}
	if (UGameFlowSubsystem* Flow = GetWorld() ? GetWorld()->GetSubsystem<UGameFlowSubsystem>() : nullptr)
	{
		Flow->OnGameFlowChanged.AddUniqueDynamic(this, &UTutorialSubsystem::HandleGameFlowChanged);
		LastPhase = Flow->GetPhase();
	}
}

void UTutorialSubsystem::InitFromLevel()
{
	const UDemoModeSubsystem* Demo = GetWorld() ? GetWorld()->GetSubsystem<UDemoModeSubsystem>() : nullptr;
	if (Machine.IsLoaded() || !Demo || !Demo->IsDemo() || !Demo->GetSettings().bTutorial)
	{
		return;
	}
	LoadStepsFile(Demo->GetSettings().TutorialSteps);
}

bool UTutorialSubsystem::LoadStepsFile(const FString& FileName)
{
	FTutorialStepsData Data;
	TArray<FString> Warnings;
	FString Error;
	if (!TutorialRules::LoadStepsFile(FileName, Data, Warnings, Error))
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("Tutorial: %s"), *Error);
		return false;
	}
	for (const FString& Warning : Warnings)
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("Tutorial %s: %s"), *FileName, *Warning);
	}
	StepsFile = FileName;
	LoadSteps(Data);
	return true;
}

void UTutorialSubsystem::LoadSteps(const FTutorialStepsData& Data)
{
	Machine.StatProvider = [this](FName Stat) { return GetConditionStat(Stat); };
	Machine.FocusSubjectProvider = [this](FName StepId)
	{
		const AOperativeCharacter* Focus = GetFocusOperative(StepId);
		return Focus ? Focus->GetFName() : NAME_None;
	};
	Machine.RoleSpotProvider = [this](FName StepId, FName SubjectId)
	{
		const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
		for (const AOperativeCharacter* Member : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
		{
			if (Member && Member->GetFName() == SubjectId)
			{
				return IsAtRoleSpot(StepId, *Member);
			}
		}
		return false; // not a squad member: no role, no spot
	};
	// 2026-10-20: the current navigation target needs to know which steps have markers and which volumes the level has.
	Machine.StepMarkerProvider = [this](FName StepId)
	{
		return Markers.ContainsByPredicate([StepId](const TWeakObjectPtr<ATutorialMarkerActor>& Weak)
		{
			const ATutorialMarkerActor* Marker = Weak.Get();
			return Marker && !Marker->bRuntimeGuide && Marker->StepId == StepId;
		});
	};
	Machine.VolumeProvider = [this](FName VolumeId)
	{
		return GuideVolumes.ContainsByPredicate([VolumeId](const TWeakObjectPtr<ATutorialTriggerVolume>& Weak)
		{
			const ATutorialTriggerVolume* Volume = Weak.Get();
			return Volume && Volume->Box && Volume->GetEffectiveVolumeId() == VolumeId;
		});
	};
	Machine.SpotProvider = [this](FName Spot, FName StepId)
	{
		FVector Location;
		return ResolveGuideSpot(Spot, StepId, Location);
	};
	// 2026-10-21: "enemy" filters - each A4 part reacts only to ITS zombie (the subject actor resolved in the world).
	Machine.EnemyFilterProvider = [this](const FTutorialEnemyFilter& Filter, FName SubjectId) { return PassesEnemyFilterNow(Filter, SubjectId); };
	Machine.Load(Data);
	LogEnemyBindings();
	// Rev 5f (monotonic route): the place of every beat volume of the level (a volume that starts a beat lies at its start).
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<ATutorialTriggerVolume> It(World); It; ++It)
		{
			Machine.RegisterVolumeBeat(It->GetEffectiveVolumeId(), It->BeatId);
		}
	}
	LockedRefusals = 0;
	ConfinedRefusals = 0;
	LastCardDoneAt = -1.0;
	NoInstructionRun = NoInstructionTotal = MaxNextInstructionSeconds = 0.f;
	ConfineLineTimes.Reset();
	PrecreditedCount = 0;
	LockHintFamilies.Reset();
	FocusOperatives.Reset();
	ClearOverlayState();
	UE_LOG(LogCodexTactics, Display, TEXT("Tutorial: %d steps, %d beats loaded (%s), enabled %d"), Data.Steps.Num(), Data.Beats.Num(),
		Data.Id.IsEmpty() ? *StepsFile : *Data.Id, IsTutorialEnabled() ? 1 : 0);
	WriteTelemetry(TEXT("session_start"), NAME_None, NAME_None, 0.f);
	if (DeferredProgress.IsSet())
	{
		// A save was applied before the steps existed (load with travel): continue from it instead of the start beat.
		const FTutorialProgress Progress = DeferredProgress.GetValue();
		DeferredProgress.Reset();
		Machine.ImportProgress(Progress);
		ProcessTransitions();
		RefreshMarkers();
		return;
	}
	StartTutorial();
}

void UTutorialSubsystem::StartTutorial()
{
	const FTutorialStepsData& Data = Machine.GetData();
	if (!Data.StartBeat.IsNone())
	{
		Machine.StartBeat(Data.StartBeat);
	}
	ProcessTransitions();
	RefreshMarkers();
}

bool UTutorialSubsystem::IsTutorialEnabled()
{
	TutorialConsole::ReadProfileOnce();
	return TutorialConsole::CVarTutorialEnabled.GetValueOnGameThread() != 0;
}

bool UTutorialSubsystem::IsTutorialActive() const
{
	return Machine.IsLoaded() && IsTutorialEnabled() && !bSkipped;
}

bool UTutorialSubsystem::SkipTutorial()
{
	if (!Machine.IsLoaded() || bSkipped)
	{
		return false;
	}
	bSkipped = true;
	UnlockAllCommands(TEXT("tutorial skipped")); // locks + the confinement in force
	ClearOverlayState(); // the radio / walk-and-talk strip goes with the rest (story dialogue windows are not the tutorial's)
	UE_LOG(LogCodexTactics, Display, TEXT("Tutorial: SKIPPED by the player at beat %s (off for this run, every control unlocked)"),
		*Machine.GetCurrentBeat().ToString());
	WriteTelemetry(TEXT("tutorial_skipped"), NAME_None, Machine.GetCurrentBeat(), 0.f);
	PlayerCommands::Report(this, PlayerCommands::SkipTutorial);
	PostTrainingLine(LOCTEXT("TutorialSkipped", "Tutorial skipped - all controls unlocked."));
	RefreshMarkers();
	return true;
}

bool UTutorialSubsystem::IsSkipOfferShown() const
{
	if (!IsTutorialActive())
	{
		return false;
	}
	const FTutorialStepsData& Data = Machine.GetData();
	const FName StartBeat = !Data.StartBeat.IsNone() ? Data.StartBeat : (Data.Beats.IsValidIndex(0) ? Data.Beats[0].Id : NAME_None);
	return !StartBeat.IsNone() && Machine.GetCurrentBeat() == StartBeat;
}

void UTutorialSubsystem::SetTutorialEnabled(bool bEnabled)
{
	const bool bWas = IsTutorialEnabled();
	TutorialConsole::CVarTutorialEnabled->Set(bEnabled ? 1 : 0, ECVF_SetByCode);
	TutorialConsole::WriteProfile(bEnabled);
	if (bWas != bEnabled)
	{
		WriteTelemetry(bEnabled ? TEXT("enabled") : TEXT("disabled"), NAME_None, Machine.GetCurrentBeat(), 0.f);
	}
	if (!bEnabled)
	{
		UnlockAllCommands(TEXT("tutorial switched off")); // switching it on again does not lock the taught-later commands back
	}
	RefreshMarkers();
}

// --- Tick -----------------------------------------------------------------------------------------------------------

void UTutorialSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	if (!World || !World->HasBegunPlay())
	{
		return;
	}
	if (!bInitDone)
	{
		if (IsLoadPending())
		{
			// Rev 5f (save bug): a save is about to be applied (F9 / Continue / LOAD reopened the map). Starting the tutorial now
			// would start beat A0 and write its CHECKPOINT over the very slot being loaded (F9 to a checkpoint came back at the
			// level start). Wait: the load hands its progress over (DeferredProgress) and the steps load right after it.
			return;
		}
		bInitDone = true; // the game mode read the level in StartPlay; the demo flags are applied on the first tick
		InitFromLevel();
	}
	SquadBindTimer -= DeltaTime;
	if (SquadBindTimer <= 0.f)
	{
		SquadBindTimer = 1.f;
		BindSquadMembers();
	}
	if (!IsTutorialActive())
	{
		return;
	}
	// Real seconds: hints keep their pace in the tactical pause (0.02 dilation); the pause menu stops the world tick.
	const float RealDelta = FMath::Min(static_cast<float>(FApp::GetDeltaTime()), 0.25f);
	Machine.Tick(RealDelta);
	CountRoleSpotPresence();
	TickSpeech(RealDelta);
	TickPacing(RealDelta);
	FlashCardLeft = FMath::Max(0.f, FlashCardLeft - RealDelta);
	for (auto It = FlashObjectives.CreateIterator(); It; ++It)
	{
		It.Value() -= RealDelta;
		if (It.Value() <= 0.f)
		{
			It.RemoveCurrent();
		}
	}
	ProcessTransitions();
}

void UTutorialSubsystem::BindSquadMembers()
{
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	if (!Squad)
	{
		return;
	}
	BoundMembers.RemoveAll([](const TWeakObjectPtr<AOperativeCharacter>& Member) { return !Member.IsValid(); });
	for (AOperativeCharacter* Member : Squad->GetMembers())
	{
		if (!Member || BoundMembers.Contains(Member))
		{
			continue;
		}
		BoundMembers.Add(Member);
		if (Member->HealthComponent)
		{
			Member->HealthComponent->LethalDamageGuard.BindUObject(this, &UTutorialSubsystem::HandleLethalHit);
		}
		if (Member->ColdSurvival)
		{
			Member->ColdSurvival->OnTierChanged.AddUniqueDynamic(this, &UTutorialSubsystem::HandleColdTierChanged);
		}
	}
}

// --- Inputs ---------------------------------------------------------------------------------------------------------

void UTutorialSubsystem::HandleBusCommand(FName CommandId, const FPlayerCommandContext& Context)
{
	HandlePlayerCommand(CommandId, Context);
}

void UTutorialSubsystem::HandlePlayerCommand(FName CommandId, const FPlayerCommandContext& Context)
{
	if (!IsTutorialActive())
	{
		return;
	}
	// Only this world's commands (PIE with several worlds shares one game instance per world, but be safe).
	if (Context.Operative && Context.Operative->GetWorld() != GetWorld())
	{
		return;
	}
	if (Context.HeldSeconds > 0.f)
	{
		Machine.HandleCommandHold(CommandId, Context.HeldSeconds);
	}
	else
	{
		Machine.HandleCommand(CommandId);
	}
	ProcessTransitions();
}

void UTutorialSubsystem::HandleBusEvent(FName EventName, AActor* Subject)
{
	if ((Subject && Subject->GetWorld() != GetWorld()) || !IsTutorialActive() || EventName.IsNone())
	{
		return;
	}
	// "in_volume" completions (rev 5c): where the subject stands when the event happens (only looked up when a step asks).
	const bool bAnyInVolume = Subject
		&& Machine.GetData().Steps.ContainsByPredicate([](const FTutorialStep& Step) { return !Step.Complete.InVolume.IsNone(); });
	Machine.HandleEvent(EventName, Subject ? Subject->GetFName() : NAME_None, bAnyInVolume ? GetTutorialVolumesAt(Subject->GetActorLocation()) : TArray<FName>());
	ProcessTransitions();
}

TArray<FName> UTutorialSubsystem::GetTutorialVolumesAt(const FVector& Location) const
{
	TArray<FName> Out;
	UWorld* World = GetWorld();
	if (!World)
	{
		return Out;
	}
	for (TActorIterator<ATutorialTriggerVolume> It(World); It; ++It)
	{
		const FName Id = It->GetEffectiveVolumeId();
		if (It->Box && !Id.IsNone() && TutorialConfineLocal::IsInsideBox(*It->Box, Location, 0.f, 0.f))
		{
			Out.AddUnique(Id);
		}
	}
	return Out;
}

void UTutorialSubsystem::CountVolumePresence(const FTutorialStep& Step)
{
	UWorld* World = GetWorld();
	const USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
	if (!World || !Squad || Step.Complete.Volume.IsNone())
	{
		return;
	}
	for (TActorIterator<ATutorialTriggerVolume> It(World); It; ++It)
	{
		if (It->GetEffectiveVolumeId() != Step.Complete.Volume || !It->Box || It->ActivatedBy == ETutorialVolumeFilter::Vehicle)
		{
			continue;
		}
		TArray<AActor*> Inside;
		It->Box->GetOverlappingActors(Inside, AOperativeCharacter::StaticClass());
		for (AActor* Actor : Inside)
		{
			AOperativeCharacter* Operative = Cast<AOperativeCharacter>(Actor);
			if (!Operative || !Squad->GetMembers().Contains(Operative)
				|| (It->ActivatedBy == ETutorialVolumeFilter::Leader && Squad->GetLeader() != Operative))
			{
				continue;
			}
			if (Machine.HandleVolumePresence(Step.Complete.Volume, Operative->GetFName()))
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Tutorial: %s - %s already stands in %s"), *Step.Id.ToString(), *Operative->DisplayName.ToString(),
					*Step.Complete.Volume.ToString());
			}
		}
	}
}

void UTutorialSubsystem::NotifyEvent(FName EventName, FName SubjectId)
{
	if (!IsTutorialActive() || EventName.IsNone())
	{
		return;
	}
	Machine.HandleEvent(EventName, SubjectId);
	ProcessTransitions();
}

void UTutorialSubsystem::HandleMineSpotted(AActor* Mine, AOperativeCharacter* Spotter)
{
	NotifyEvent(GameplayEvents::MineSpotted);
}

void UTutorialSubsystem::HandleItemUsed(AOperativeCharacter* Soldier, const FString& ItemId)
{
	// Subject = the operative the item was used on (rev 2026-10-19: the medkit lesson completes only for its wounded operative).
	const FName SubjectId = Soldier ? Soldier->GetFName() : NAME_None;
	NotifyEvent(GameplayEvents::ItemUsed, SubjectId);
	NotifyEvent(FName(*(GameplayEvents::ItemUsed.ToString() + TEXT(".") + ItemId)), SubjectId);
}

void UTutorialSubsystem::HandleSoldierDowned(AOperativeCharacter* Soldier)
{
	NotifyEvent(GameplayEvents::SoldierDowned);
}

void UTutorialSubsystem::HandleDialogueFinished()
{
	NotifyEvent(GameplayEvents::DialogueFinished);
}

void UTutorialSubsystem::HandleGameFlowChanged(ECodexGamePhase Phase, ECodexCombatMode CombatMode)
{
	if (Phase == LastPhase)
	{
		return;
	}
	const ECodexGamePhase Previous = LastPhase;
	LastPhase = Phase;
	if (Phase == ECodexGamePhase::WaveCombat)
	{
		// Safety (user request 2026-10-13): a wave / horde fight never runs with tutorial locks (no stance / pause / weapons).
		UnlockAllCommands(TEXT("a fight started"));
		NotifyEvent(GameplayEvents::CombatStarted);
	}
	else if (Previous == ECodexGamePhase::WaveCombat)
	{
		NotifyEvent(GameplayEvents::CombatEnded);
	}
}

void UTutorialSubsystem::HandleColdTierChanged(AOperativeCharacter* Operative, EColdTier Tier)
{
	if (Tier == EColdTier::Normal)
	{
		return;
	}
	const FName OperativeId = Operative ? Operative->GetFName() : NAME_None;
	NotifyEvent(GameplayEvents::ColdTierChanged, OperativeId);
	const UEnum* TierEnum = StaticEnum<EColdTier>();
	if (TierEnum)
	{
		NotifyEvent(FName(*(GameplayEvents::ColdTierChanged.ToString() + TEXT(".") + TierEnum->GetNameStringByValue(static_cast<int64>(Tier)))), OperativeId);
	}
}

bool UTutorialSubsystem::NotifyVolumeEntered(ATutorialTriggerVolume* Volume, AOperativeCharacter* Operative)
{
	if (!Volume)
	{
		return false;
	}
	if (!IsTutorialActive())
	{
		UE_LOG(LogCodexTactics, Verbose, TEXT("Tutorial volume %s entered: tutorial %s"), *Volume->GetName(),
			Machine.IsLoaded() ? TEXT("off") : TEXT("has no steps"));
		return false;
	}
	const FTutorialStepsData& Data = Machine.GetData();
	if (!Volume->RequiredBeat.IsNone())
	{
		const int32 Required = Data.FindBeatIndex(Volume->RequiredBeat);
		const int32 Current = Data.FindBeatIndex(Machine.GetCurrentBeat());
		if (Required != INDEX_NONE && Current < Required)
		{
			return false;
		}
	}
	UE_LOG(LogCodexTactics, Display, TEXT("Tutorial volume %s (volume %s, step %s, beat %s) entered by %s"), *Volume->GetName(),
		*Volume->GetEffectiveVolumeId().ToString(), *Volume->StepId.ToString(), *Volume->BeatId.ToString(),
		Operative ? *Operative->DisplayName.ToString() : TEXT("?"));
	Machine.HandleVolume(Volume->GetEffectiveVolumeId(), Volume->StepId, Volume->BeatId, Operative ? Operative->GetFName() : NAME_None);
	ProcessTransitions();
	return true;
}

bool UTutorialSubsystem::StartBeat(FName BeatId)
{
	const bool bStarted = IsTutorialActive() && Machine.StartBeat(BeatId);
	ProcessTransitions();
	return bStarted;
}

bool UTutorialSubsystem::TriggerStep(FName StepId)
{
	const bool bFired = IsTutorialActive() && Machine.TriggerStep(StepId);
	ProcessTransitions();
	return bFired;
}

bool UTutorialSubsystem::SkipActiveCard()
{
	return SkipStep(Machine.GetActiveCard());
}

bool UTutorialSubsystem::SkipStep(FName StepId)
{
	const bool bStepSkipped = !StepId.IsNone() && Machine.SkipStep(StepId);
	ProcessTransitions();
	return bStepSkipped;
}

bool UTutorialSubsystem::ReplayStep(FName StepId)
{
	const bool bReplayed = IsTutorialActive() && Machine.ReplayStep(StepId);
	ProcessTransitions();
	return bReplayed;
}

void UTutorialSubsystem::ResetTutorial()
{
	bSkipped = false; // rev 5i: RESET TUTORIAL starts a skipped tutorial again
	Machine.ResetProgress();
	LockHintFamilies.Reset();
	FocusOperatives.Reset();
	ClearOverlayState();
	WriteTelemetry(TEXT("reset"), NAME_None, NAME_None, 0.f);
	StartTutorial();
}

void UTutorialSubsystem::ClearOverlayState()
{
	SpeechStep = NAME_None;
	SpeechLines.Reset();
	FlashCard = NAME_None;
	FlashObjectives.Reset();
	bRetryPrompt = false;
}

void UTutorialSubsystem::PostTrainingLine(const FText& Line) const
{
	if (UGameMessageSubsystem* Messages = GetWorld() ? GetWorld()->GetSubsystem<UGameMessageSubsystem>() : nullptr)
	{
		Messages->PostMessage(LOCTEXT("TrainingSpeaker", "TRAINING"), Line);
	}
}

// --- Transitions, UI state ------------------------------------------------------------------------------------------

void UTutorialSubsystem::ProcessTransitions()
{
	// A shown step may finish at once (feed lines) and start followers: drain until quiet (bounded).
	for (int32 Round = 0; Round < 16; ++Round)
	{
		const TArray<FTutorialTransition> Transitions = Machine.ConsumeTransitions();
		if (Transitions.IsEmpty())
		{
			break;
		}
		const FTutorialStepsData& Data = Machine.GetData();
		for (const FTutorialTransition& Transition : Transitions)
		{
			if (Transition.Type == FTutorialTransition::EType::BeatStarted)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Tutorial: beat %s started"), *Transition.BeatId.ToString());
				WriteTelemetry(TEXT("beat_started"), NAME_None, Transition.BeatId, 0.f);
				const int32 BeatIndex = Data.FindBeatIndex(Transition.BeatId);
				if (Data.Beats.IsValidIndex(BeatIndex) && Data.Beats[BeatIndex].bCheckpoint)
				{
					SaveCheckpoint();
				}
				continue;
			}
			const FTutorialStep* Step = Data.FindStep(Transition.StepId);
			if (!Step)
			{
				continue;
			}
			UE_LOG(LogCodexTactics, Display, TEXT("Tutorial: %s %s -> %s%s%s"), *Transition.StepId.ToString(), TutorialRules::StateName(Transition.From),
				TutorialRules::StateName(Transition.To), Transition.bSilent ? TEXT(" (silent)") : TEXT(""),
				Transition.bPassed ? TEXT(" (passed on the way: the squad reached a later place first)") : TEXT(""));
			if (TutorialSubsystemLocal::IsTriggerFocus(Step->Focus) && !TutorialRules::IsRunning(Transition.From) && TutorialRules::IsRunning(Transition.To))
			{
				ChooseFocus(*Step); // rev 2026-10-19: the one who got hurt, fixed from the trigger (a medkit on him must count while it queues)
			}
			if ((Transition.To == ETutorialStepState::Queued || Transition.To == ETutorialStepState::Active) && !Step->Complete.Volume.IsNone())
			{
				// A squad member may already stand where the step asks for (rev 5c: the third operative walked into the hut before
				// the "go in" card): counts at once; the completion arrives with the next drain round.
				CountVolumePresence(*Step);
			}
			if (Transition.To == ETutorialStepState::Active)
			{
				WriteTelemetry(TEXT("shown"), Step->Id, Step->Beat, 0.f);
				if ((Step->Kind == ETutorialStepKind::HintCard || Step->Kind == ETutorialStepKind::Objective) && LastCardDoneAt >= 0.0)
				{
					// Pacing: how long the player waited after his last card for the next instruction.
					const float Wait = static_cast<float>(PacingClock - LastCardDoneAt);
					MaxNextInstructionSeconds = FMath::Max(MaxNextInstructionSeconds, Wait);
					WriteTelemetry(TEXT("next_instruction"), Step->Id, Step->Beat, Wait);
					LastCardDoneAt = -1.0;
				}
				HandleStepShown(*Step);
				if (Step->Kind == ETutorialStepKind::Radio || Step->Kind == ETutorialStepKind::WalkTalk)
				{
					StartSpeech(Step->Id);
				}
			}
			else if (TutorialRules::IsFinished(Transition.To))
			{
				const TCHAR* Event = Transition.bPassed ? TEXT("passed") : Transition.To == ETutorialStepState::Skipped ? TEXT("skipped")
					: (Transition.To == ETutorialStepState::TimedOut ? TEXT("timed_out")
					: (Transition.bPrecredited ? TEXT("precredited") : (Transition.bSilent ? TEXT("completed_silently") : TEXT("completed"))));
				WriteTelemetry(Event, Step->Id, Step->Beat, Transition.ActiveSeconds);
				if (Transition.bPrecredited)
				{
					// The lesson was already done before its card was due (user decision 2026-10-13): one unobtrusive tick in the feed.
					++PrecreditedCount;
					const FString Title = Step->TextId.IsNone() ? Step->Id.ToString() : FNarrativeManifest::Get().GetTutorialText(Step->TextId.ToString()).Title;
					UE_LOG(LogCodexTactics, Display, TEXT("Tutorial: %s precredited from the command history (%s)"), *Step->Id.ToString(), *Title);
					PostTrainingLine(FText::Format(LOCTEXT("Precredited", "✓ {0} - already done."), FText::FromString(Title.IsEmpty() ? Step->Id.ToString() : Title)));
				}
				if (Transition.From == ETutorialStepState::Active && Transition.To == ETutorialStepState::Completed)
				{
					if (Step->Kind == ETutorialStepKind::HintCard)
					{
						LastCardDoneAt = PacingClock;
						FlashCard = Step->Id;
						FlashCardLeft = FMath::Min(0.8f, Machine.CardBreatherSeconds);
					}
					else if (Step->Kind == ETutorialStepKind::Objective)
					{
						FlashObjectives.Add(Step->Id, 1.2f);
					}
				}
				if (SpeechStep == Step->Id && Transition.bCancelled && SpeechLines.IsValidIndex(SpeechIndex))
				{
					// Rev 5e "cancel_on": the line on screen runs out, the rest of the conversation is dropped (a new speech may
					// replace it at once - the machine's speech slot is free).
					UE_LOG(LogCodexTactics, Display, TEXT("Tutorial: %s cancelled at line %d / %d (no longer relevant)"), *Step->Id.ToString(),
						SpeechIndex + 1, SpeechLines.Num());
					SpeechLines.SetNum(SpeechIndex + 1);
				}
				else if (SpeechStep == Step->Id)
				{
					SpeechStep = NAME_None;
					SpeechLines.Reset();
				}
				if (Transition.bCancelled)
				{
					UE_LOG(LogCodexTactics, Display, TEXT("Tutorial: %s dropped by its cancel_on"), *Step->Id.ToString());
				}
			}
		}
	}
	if (Machine.GetConfineSerial() != SeenConfineSerial)
	{
		HandleConfineChanged(); // rev 5i: a confinement started / ended
	}
	RefreshMarkers();
}

void UTutorialSubsystem::HandleStepShown(const FTutorialStep& Step)
{
	if (TutorialSubsystemLocal::IsWoundedFocus(Step.Focus))
	{
		AOperativeCharacter* Focus = GetFocusOperative(Step.Id);
		if (!Focus || (Focus->HealthComponent && !Focus->HealthComponent->IsAlive()))
		{
			Focus = ChooseFocus(Step);
		}
		if (Focus && Focus->HealthComponent)
		{
			const float Percent = 100.f * Focus->HealthComponent->GetCurrentHealth() / FMath::Max(1.f, Focus->HealthComponent->GetMaxHealth());
			UFloatingTextSubsystem::SpawnAboveOperative(Focus, FString::Printf(TEXT("🩹 WOUNDED: %d%%"), FMath::RoundToInt(Percent)),
				FLinearColor(1.f, 0.35f, 0.3f));
			UE_LOG(LogCodexTactics, Display, TEXT("Tutorial: %s focuses %s (health %.0f%%, medkits %d)"), *Step.Id.ToString(), *Focus->DisplayName.ToString(),
				Percent, Focus->MedkitsCount);
		}
	}
	else if (!TutorialRules::ParseRoleFocus(Step.Focus).IsNone())
	{
		// 2026-10-20: a role's operative (the A4 ambusher), chosen at the trigger; again only if he is gone.
		AOperativeCharacter* Focus = GetFocusOperative(Step.Id);
		if (!Focus || !TutorialSubsystemLocal::IsLivingOrUntracked(Focus))
		{
			Focus = ChooseFocus(Step);
		}
		if (Focus)
		{
			UFloatingTextSubsystem::SpawnAboveOperative(Focus, FString::Printf(TEXT("%s: YOUR TASK"), *Focus->DisplayName.ToString().ToUpper()), FLinearColor(1.f, 0.7f, 0.2f));
			UE_LOG(LogCodexTactics, Display, TEXT("Tutorial: %s focuses %s (role %s)"), *Step.Id.ToString(), *Focus->DisplayName.ToString(), *GetRoleId(*Focus).ToString());
		}
	}
	else if (!Step.Focus.IsNone())
	{
		AOperativeCharacter* Focus = ChooseFocus(Step);
		if (Focus)
		{
			UFloatingTextSubsystem::SpawnAboveOperative(Focus, FString::Printf(TEXT("🥶 COLDEST: %d%%"), FMath::RoundToInt(Focus->ColdLevel)),
				FLinearColor(0.45f, 0.85f, 1.f));
			UE_LOG(LogCodexTactics, Display, TEXT("Tutorial: %s focuses %s (cold %.0f%%)"), *Step.Id.ToString(), *Focus->DisplayName.ToString(), Focus->ColdLevel);
		}
	}
	RunActions(Step);
	if (Step.Kind == ETutorialStepKind::Feed)
	{
		UGameMessageSubsystem* Messages = GetWorld() ? GetWorld()->GetSubsystem<UGameMessageSubsystem>() : nullptr;
		for (const FSpokenLine& Line : ResolveLines(Step))
		{
			const FString Text = ResolveFocusToken(Line.Text, Step.Id);
			UE_LOG(LogCodexTactics, Display, TEXT("Tutorial feed [%s] %s: %s"), *Step.Id.ToString(), *Line.Speaker, *Text);
			if (Messages)
			{
				Messages->PostMessage(FText::FromString(Line.Speaker), FText::FromString(Text));
			}
		}
		Machine.CompleteStep(Step.Id);
	}
}

void UTutorialSubsystem::RunActions(const FTutorialStep& Step)
{
	USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	for (const FTutorialAction& Action : Step.Actions)
	{
		ESquadFirePosture Posture = ESquadFirePosture::Aggressive;
		if (Action.Type == FName(TEXT("set_posture")) && Squad && FirePostureRules::ParseName(Action.Posture, Posture))
		{
			// 2026-10-19: the Demo 01 squad enters the complex DEFENSIVE (no fire on enemies that have not spotted it) until the posture
			// lesson - an Aggressive squad would start the A4 fight on its own the moment a patrol came into view.
			Squad->SetSquadPosture(Posture);
			UE_LOG(LogCodexTactics, Display, TEXT("Tutorial: %s set_posture %s"), *Step.Id.ToString(), *FirePostureRules::GetLabel(Posture));
			WriteTelemetry(TEXT("action_set_posture"), Step.Id, Step.Beat, 0.f);
			continue;
		}
		if (Action.Type == FName(TEXT("solo_focus")) && Squad)
		{
			// 2026-10-20 (the A4 ambush by ONE operative): Solo ON and the step's focus operative selected - he alone takes the orders.
			if (!Squad->IsSoloMode())
			{
				Squad->EnterSoloMode();
			}
			AOperativeCharacter* Focus = GetFocusOperative(Step.Id);
			if (Focus && Squad->GetLeader() != Focus)
			{
				Squad->SetLeader(Focus);
			}
			UE_LOG(LogCodexTactics, Display, TEXT("Tutorial: %s solo_focus - Solo %d, selected %s"), *Step.Id.ToString(), Squad->IsSoloMode() ? 1 : 0,
				Squad->GetLeader() ? *Squad->GetLeader()->DisplayName.ToString() : TEXT("-"));
			WriteTelemetry(TEXT("action_solo_focus"), Step.Id, Step.Beat, 0.f);
			continue;
		}
		if (Action.Type == FName(TEXT("cold_bump")) && Squad)
		{
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member && Member->ColdSurvival)
				{
					const float Before = Member->ColdLevel;
					Member->ColdSurvival->AddColdShock(Action.Amount, Action.MinLevel);
					UE_LOG(LogCodexTactics, Display, TEXT("Tutorial: %s cold_bump %s %.0f%% -> %.0f%%"), *Step.Id.ToString(), *Member->DisplayName.ToString(),
						Before, Member->ColdLevel);
				}
			}
			WriteTelemetry(TEXT("action_cold_bump"), Step.Id, Step.Beat, 0.f);
		}
	}
}

AOperativeCharacter* UTutorialSubsystem::FindColdestMember() const
{
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	AOperativeCharacter* Coldest = nullptr;
	for (AOperativeCharacter* Member : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
	{
		const bool bAlive = TutorialSubsystemLocal::IsLivingOrUntracked(Member);
		if (bAlive && (!Coldest || Member->ColdLevel > Coldest->ColdLevel))
		{
			Coldest = Member;
		}
	}
	return Coldest;
}

AOperativeCharacter* UTutorialSubsystem::FindMostWoundedMember() const
{
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	AOperativeCharacter* Wounded = nullptr;
	float Lowest = TNumericLimits<float>::Max();
	for (AOperativeCharacter* Member : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
	{
		const UHealthComponent* Health = Member ? Member->HealthComponent.Get() : nullptr;
		if (!Health || !Health->IsAlive())
		{
			continue;
		}
		const float Share = Health->GetCurrentHealth() / FMath::Max(1.f, Health->GetMaxHealth());
		if (Share < Lowest)
		{
			Lowest = Share;
			Wounded = Member;
		}
	}
	return Wounded;
}

AOperativeCharacter* UTutorialSubsystem::ChooseFocus(const FTutorialStep& Step)
{
	AOperativeCharacter* Focus = nullptr;
	if (const FName Role = TutorialRules::ParseRoleFocus(Step.Focus); !Role.IsNone())
	{
		// 2026-10-20: the living operative of that role (the A4 ambusher = the Medic-Sapper: traps are her job), else the leader.
		const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
		for (AOperativeCharacter* Member : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
		{
			if (TutorialSubsystemLocal::IsLivingOrUntracked(Member) && GetRoleId(*Member) == Role)
			{
				Focus = Member;
				break;
			}
		}
		Focus = Focus ? Focus : (Squad ? Squad->GetLeader() : nullptr);
		FocusOperatives.Add(Step.Id, Focus);
		return Focus;
	}
	Focus = TutorialSubsystemLocal::IsWoundedFocus(Step.Focus) ? FindMostWoundedMember() : FindColdestMember();
	FocusOperatives.Add(Step.Id, Focus);
	return Focus;
}

AOperativeCharacter* UTutorialSubsystem::GetFocusOperative(FName StepId) const
{
	const TWeakObjectPtr<AOperativeCharacter>* Found = FocusOperatives.Find(StepId);
	return Found ? Found->Get() : nullptr;
}

FString UTutorialSubsystem::ResolveFocusToken(const FString& Text, FName StepId) const
{
	if (!Text.Contains(TEXT("{focus}")))
	{
		return Text;
	}
	const AOperativeCharacter* Focus = GetFocusOperative(StepId);
	const FTutorialStep* Step = Machine.GetData().FindStep(StepId);
	const TCHAR* Fallback = Step && TutorialSubsystemLocal::IsWoundedFocus(Step->Focus) ? TEXT("the wounded operative")
		: (Step && !TutorialRules::ParseRoleFocus(Step->Focus).IsNone() ? TEXT("the chosen operative") : TEXT("the coldest operative"));
	return Text.Replace(TEXT("{focus}"), Focus ? *Focus->DisplayName.ToString() : Fallback);
}

float UTutorialSubsystem::GetConditionStat(FName Stat) const
{
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	if (!Squad)
	{
		return 0.f;
	}
	if (Stat == FName(TEXT("squad_solo")))
	{
		return Squad->IsSoloMode() ? 1.f : 0.f; // rev 5i: the squad is in Solo (B) now
	}
	if (Stat == FName(TEXT("leader_cold")))
	{
		const AOperativeCharacter* Leader = Squad->GetLeader();
		return Leader ? Leader->ColdLevel : 0.f;
	}
	if (const FName TrapTag = TrapSpotRules::ParseSpottedStatTag(Stat); !TrapTag.IsNone())
	{
		// Rev 5h: the traps of a tagged group (AInteractableActor::SpotEventTag) the squad knows of (spotted; dealt with = known).
		int32 Spotted = 0;
		for (TActorIterator<AInteractableActor> It(GetWorld()); It; ++It)
		{
			Spotted += It->SpotEventTag == TrapTag && !It->IsHiddenTrap() ? 1 : 0;
		}
		return static_cast<float>(Spotted);
	}
	if (const FName AcrossVolume = TutorialRules::ParseNotAcrossStat(Stat); !AcrossVolume.IsNone())
	{
		// 2026-10-20: the living members not yet across the bridge nearest to that volume (still on its side or on the span) - however
		// they crossed. No bridge / no such volume: nobody counts as across.
		const TArray<const ATutorialTriggerVolume*> NearVolumes = FindVolumes(AcrossVolume);
		const FVector NearPoint = NearVolumes.IsEmpty() ? FVector::ZeroVector : NearVolumes[0]->GetActorLocation();
		const ABridgeCrossingVolume* Bridge = nullptr;
		for (TActorIterator<ABridgeCrossingVolume> It(GetWorld()); It && !NearVolumes.IsEmpty(); ++It)
		{
			if (It->Span && (!Bridge || FVector::DistSquared(It->GetActorLocation(), NearPoint) < FVector::DistSquared(Bridge->GetActorLocation(), NearPoint)))
			{
				Bridge = *It;
			}
		}
		int32 NotAcross = 0;
		for (const AOperativeCharacter* Member : Squad->GetMembers())
		{
			if (TutorialSubsystemLocal::IsLivingOrUntracked(Member))
			{
				NotAcross += Bridge && BridgeRules::IsAcrossFrom(Bridge->Span->GetComponentTransform(), Bridge->Span->GetUnscaledBoxExtent().X, NearPoint,
					Member->GetActorLocation()) ? 0 : 1;
			}
		}
		return static_cast<float>(NotAcross);
	}
	if (const FName LowVolume = TutorialRules::ParseNotLowInStat(Stat); !LowVolume.IsNone())
	{
		// Rev 5i: the living members NOT crouched / prone at that volume - inside its box or within 3 m of it, where followers in
		// formation stand round a leader sent into it (0 = the whole squad is down there).
		const TArray<const ATutorialTriggerVolume*> LowVolumes = FindVolumes(LowVolume);
		int32 NotLow = 0;
		for (const AOperativeCharacter* Member : Squad->GetMembers())
		{
			if (!TutorialSubsystemLocal::IsLivingOrUntracked(Member))
			{
				continue;
			}
			const bool bLow = TutorialRules::StanceSatisfies(GetStanceName(Member->GetStance()), FName(TEXT("crouch")));
			const bool bThere = LowVolumes.ContainsByPredicate([Member](const ATutorialTriggerVolume* Volume)
			{
				return TutorialConfineLocal::IsInsideBox(*Volume->Box, Member->GetActorLocation(), TutorialConfineLocal::NearVolumeCm, TutorialConfineLocal::AreaMarginZCm);
			});
			NotLow += bLow && bThere ? 0 : 1;
		}
		return static_cast<float>(NotLow);
	}
	if (Stat == FName(TEXT("squad_injury_max")))
	{
		// Rev 2026-10-19: the medkit lesson - the share of health the most wounded living member has lost, % (0 = all healthy / none alive).
		const AOperativeCharacter* Wounded = FindMostWoundedMember();
		const UHealthComponent* Health = Wounded ? Wounded->HealthComponent.Get() : nullptr;
		return Health ? 100.f * (1.f - Health->GetCurrentHealth() / FMath::Max(1.f, Health->GetMaxHealth())) : 0.f;
	}
	if (Stat == FName(TEXT("squad_prone")))
	{
		// 2026-10-19 b: the symmetric-sight card ("prone behind cover they can't see you - but you can't see them either").
		int32 Prone = 0;
		for (const AOperativeCharacter* Member : Squad->GetMembers())
		{
			Prone += TutorialSubsystemLocal::IsLivingOrUntracked(Member) && Member->GetStance() == EOperativeStance::Prone ? 1 : 0;
		}
		return static_cast<float>(Prone);
	}
	if (Stat == FName(TEXT("enemies_unseen")))
	{
		const UTacticalSightSubsystem* Sight = GetWorld()->GetSubsystem<UTacticalSightSubsystem>();
		if (!Sight || !Sight->IsSquadSightActive())
		{
			return 0.f; // everything shown: nothing is unseen
		}
		int32 Unseen = 0;
		for (const AEnemyCharacter* Enemy : UEnemyRegistrySubsystem::GetEnemyCharacters(GetWorld()))
		{
			const UHealthComponent* Health = Enemy ? Enemy->GetHealthComponent() : nullptr;
			Unseen += Enemy && !Enemy->IsDying() && (!Health || Health->IsAlive()) && !Sight->IsVisibleToSquad(Enemy) ? 1 : 0;
		}
		return static_cast<float>(Unseen);
	}
	if (Stat == FName(TEXT("heat_packs_given")))
	{
		const USquadTransferSubsystem* Transfer = GetWorld()->GetSubsystem<USquadTransferSubsystem>();
		return Transfer ? static_cast<float>(Transfer->GetHandedOverCount(ETransferItem::HeatPack)) : 0.f;
	}
	float ColdMax = 0.f;
	float ColdMin = 0.f;
	int32 HeatPacks = 0;
	int32 MinHeatPacks = 0;
	int32 Alive = 0;
	for (const AOperativeCharacter* Member : Squad->GetMembers())
	{
		if (!TutorialSubsystemLocal::IsLivingOrUntracked(Member))
		{
			continue;
		}
		ColdMax = Alive == 0 ? Member->ColdLevel : FMath::Max(ColdMax, Member->ColdLevel);
		ColdMin = Alive == 0 ? Member->ColdLevel : FMath::Min(ColdMin, Member->ColdLevel);
		HeatPacks += Member->HeatPacksCount;
		MinHeatPacks = Alive == 0 ? Member->HeatPacksCount : FMath::Min(MinHeatPacks, Member->HeatPacksCount);
		++Alive;
	}
	if (Stat == FName(TEXT("squad_cold_max")))
	{
		return ColdMax;
	}
	if (Stat == FName(TEXT("squad_cold_min")))
	{
		return ColdMin;
	}
	if (Stat == FName(TEXT("squad_heat_packs")))
	{
		return static_cast<float>(HeatPacks);
	}
	if (Stat == FName(TEXT("squad_min_heat_packs")))
	{
		return static_cast<float>(MinHeatPacks);
	}
	return 0.f;
}

bool UTutorialSubsystem::IsCommandLocked(FName CommandId) const
{
	return IsTutorialActive() && Machine.IsCommandLocked(CommandId);
}

bool UTutorialSubsystem::CheckCommandLocked(const UObject* WorldContext, FName CommandId)
{
	UTutorialSubsystem* Tutorial = TutorialSubsystemLocal::FindTutorialFor(WorldContext);
	if (!Tutorial || !Tutorial->IsCommandLocked(CommandId))
	{
		return false;
	}
	++Tutorial->LockedRefusals;
	UE_LOG(LogCodexTactics, Display, TEXT("Tutorial: command %s is locked (beat %s) - degraded"), *CommandId.ToString(),
		*Tutorial->GetCurrentBeat().ToString());
	Tutorial->WriteTelemetry(TEXT("command_locked"), NAME_None, Tutorial->GetCurrentBeat(), 0.f);
	GameplayEvents::Raise(Tutorial->GetWorld(), GameplayEvents::TutorialLocked);
	GameplayEvents::Raise(Tutorial->GetWorld(), TutorialRules::MakeLockedEventName(CommandId));
	Tutorial->PostLockHint(CommandId);
	return true;
}

void UTutorialSubsystem::PostLockHint(FName CommandId)
{
	const FName LockedEvent = TutorialRules::MakeLockedEventName(CommandId);
	for (const FTutorialStep& Step : Machine.GetData().Steps)
	{
		if (Step.Trigger.Type == ETutorialTriggerType::Event && TutorialRules::MatchesName(Step.Trigger.Event, LockedEvent))
		{
			return; // the steps file has its own line for this one (TUT_A0_NOINVENTORY)
		}
	}
	const FString Family = TutorialRules::GetCommandFamily(CommandId);
	bool bAlready = false;
	LockHintFamilies.Add(Family, &bAlready);
	if (!bAlready)
	{
		PostTrainingLine(LOCTEXT("LockHint", "Not yet - follow the training."));
	}
}

void UTutorialSubsystem::UnlockAllCommands(const TCHAR* Reason)
{
	if (Machine.UnlockAll())
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Tutorial: every command unlocked (%s)"), Reason);
		WriteTelemetry(TEXT("unlock_all"), NAME_None, Machine.GetCurrentBeat(), 0.f);
	}
	// Rev 5i: the squad is never held in an area in a fight / with the tutorial off.
	if (Machine.ReleaseConfines())
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Tutorial: confinement released (%s)"), Reason);
		WriteTelemetry(TEXT("confine_released"), NAME_None, Machine.GetCurrentBeat(), 0.f);
		SeenConfineSerial = Machine.GetConfineSerial();
	}
}

// --- Confine (rev 5i) -----------------------------------------------------------------------------------------------


TArray<const ATutorialTriggerVolume*> UTutorialSubsystem::FindVolumes(FName VolumeId) const
{
	TArray<const ATutorialTriggerVolume*> Out;
	if (UWorld* World = GetWorld(); World && !VolumeId.IsNone())
	{
		for (TActorIterator<ATutorialTriggerVolume> It(World); It; ++It)
		{
			if (It->Box && It->GetEffectiveVolumeId() == VolumeId)
			{
				Out.Add(*It);
			}
		}
	}
	return Out;
}

FBox UTutorialSubsystem::GetSpanBox(const TArray<FName>& Areas) const
{
	// "span": one box round every area (the corridor between the holding volume and the allowed ones).
	FBox Span(ForceInit);
	for (const FName Area : Areas)
	{
		for (const ATutorialTriggerVolume* Volume : FindVolumes(Area))
		{
			Span += Volume->Box->Bounds.GetBox();
		}
	}
	return Span;
}

bool UTutorialSubsystem::IsConfining() const
{
	return IsTutorialActive() && Machine.IsConfined();
}

FName UTutorialSubsystem::GetStanceName(EOperativeStance Stance)
{
	switch (Stance)
	{
	case EOperativeStance::Crouching: return FName(TEXT("crouch"));
	case EOperativeStance::Prone: return FName(TEXT("prone"));
	default: return FName(TEXT("stand"));
	}
}

FName UTutorialSubsystem::GetConfineStance() const
{
	FTutorialConfineView Confine;
	return IsTutorialActive() && Machine.GetActiveConfine(Confine) ? Confine.Spec.Stance : NAME_None;
}

bool UTutorialSubsystem::IsInsideConfinement(const FVector& Point) const
{
	FTutorialConfineView Confine;
	if (!IsTutorialActive() || !Machine.GetActiveConfine(Confine))
	{
		return true;
	}
	using namespace TutorialConfineLocal;
	if (Confine.Spec.bSpan)
	{
		const FBox Span = GetSpanBox(Confine.Areas);
		return Span.IsValid && Span.ExpandBy(FVector(AreaMarginCm, AreaMarginCm, AreaMarginZCm)).IsInsideOrOn(Point);
	}
	for (const FName Area : Confine.Areas)
	{
		for (const ATutorialTriggerVolume* Volume : FindVolumes(Area))
		{
			if (IsInsideBox(*Volume->Box, Point, AreaMarginCm, AreaMarginZCm))
			{
				return true;
			}
		}
	}
	return false;
}

bool UTutorialSubsystem::ClampIntoConfinement(FVector& InOutDestination) const
{
	FTutorialConfineView Confine;
	if (!IsTutorialActive() || !Machine.GetActiveConfine(Confine) || IsInsideConfinement(InOutDestination))
	{
		return false;
	}
	using namespace TutorialConfineLocal;
	// The nearest point of the allowed area (a formation slot just past the far edge stays at that edge, not back at the start).
	bool bFound = false;
	FVector Best = InOutDestination;
	auto Offer = [&bFound, &Best, &InOutDestination](const FVector& Candidate)
	{
		if (!bFound || FVector::DistSquared2D(Candidate, InOutDestination) < FVector::DistSquared2D(Best, InOutDestination))
		{
			Best = Candidate;
			bFound = true;
		}
	};
	if (Confine.Spec.bSpan)
	{
		const FBox Span = GetSpanBox(Confine.Areas);
		if (Span.IsValid)
		{
			const FVector Min = Span.Min + FVector(ClampInsetCm, ClampInsetCm, 0.f);
			const FVector Max = Span.Max - FVector(ClampInsetCm, ClampInsetCm, 0.f);
			Offer(FVector(FMath::Clamp(InOutDestination.X, Min.X, FMath::Max(Min.X, Max.X)), FMath::Clamp(InOutDestination.Y, Min.Y, FMath::Max(Min.Y, Max.Y)),
				InOutDestination.Z));
		}
	}
	else
	{
		for (const FName Area : Confine.Areas)
		{
			for (const ATutorialTriggerVolume* Volume : FindVolumes(Area))
			{
				const FTransform& Transform = Volume->Box->GetComponentTransform();
				const FVector Scale = Transform.GetScale3D().GetAbs();
				const FVector Extent = Volume->Box->GetUnscaledBoxExtent();
				FVector Local = Transform.InverseTransformPosition(InOutDestination);
				const float HalfX = FMath::Max(0.f, Extent.X - (Scale.X > KINDA_SMALL_NUMBER ? ClampInsetCm / Scale.X : 0.f));
				const float HalfY = FMath::Max(0.f, Extent.Y - (Scale.Y > KINDA_SMALL_NUMBER ? ClampInsetCm / Scale.Y : 0.f));
				Local.X = FMath::Clamp(Local.X, -HalfX, HalfX);
				Local.Y = FMath::Clamp(Local.Y, -HalfY, HalfY);
				const FVector Clamped = Transform.TransformPosition(Local);
				Offer(FVector(Clamped.X, Clamped.Y, InOutDestination.Z));
			}
		}
	}
	if (!bFound)
	{
		return false; // no such volume in this world: nothing to pull towards (orders are still judged against the areas)
	}
	InOutDestination = Best;
	return true;
}

ETutorialConfineVerdict UTutorialSubsystem::JudgeMove(const AOperativeCharacter& Operative, const FVector& Destination)
{
	FTutorialConfineView Confine;
	if (!IsTutorialActive() || !Machine.GetActiveConfine(Confine))
	{
		return ETutorialConfineVerdict::Allowed;
	}
	const bool bInside = IsInsideConfinement(Destination);
	// A move along his own wall (a shimmy, the corner peek) is not a new position: only the area counts for it.
	const FName Stance = Operative.bInCover && !Confine.Spec.Stance.IsNone() ? Confine.Spec.Stance : GetStanceName(Operative.GetStance());
	return TutorialRules::JudgeConfinedMove(Confine, Operative.GetFName(), Stance, bInside);
}

void UTutorialSubsystem::ReportConfinedRefusal(ETutorialConfineVerdict Verdict, const AOperativeCharacter* Operative)
{
	if (Verdict == ETutorialConfineVerdict::Allowed)
	{
		return;
	}
	++ConfinedRefusals;
	FTutorialConfineView Confine;
	Machine.GetActiveConfine(Confine);
	UE_LOG(LogCodexTactics, Display, TEXT("Tutorial: move of %s refused by the confinement of %s (%s)"), Operative ? *Operative->DisplayName.ToString() : TEXT("?"),
		*Confine.Owner.ToString(), TutorialRules::ConfineVerdictName(Verdict));
	WriteTelemetry(TEXT("confine_refused"), Confine.Owner, Machine.GetCurrentBeat(), 0.f);
	GameplayEvents::Raise(this, FName(TEXT("Tutorial.Confined")));
	const double Now = FPlatformTime::Seconds();
	double& Last = ConfineLineTimes.FindOrAdd(static_cast<uint8>(Verdict), -1.0e9);
	if (Now - Last < 1.0)
	{
		return; // a group order refused for each member: one line
	}
	Last = Now;
	const bool bProne = (Verdict == ETutorialConfineVerdict::ExitStance ? Confine.Spec.ExitStance : Confine.Spec.Stance) == FName(TEXT("prone"));
	FText Line;
	switch (Verdict)
	{
	case ETutorialConfineVerdict::Stance:
		Line = bProne ? LOCTEXT("ConfineProne", "Go prone first.") : LOCTEXT("ConfineCrouch", "Crouch first - stay low.");
		break;
	case ETutorialConfineVerdict::ExitStance:
		Line = bProne ? LOCTEXT("ConfineExitProne", "Go prone first - then crawl under.") : LOCTEXT("ConfineExitCrouch", "Crouch first - stay low.");
		break;
	case ETutorialConfineVerdict::HeldBack:
	{
		// 2026-10-20: the A4 ambush - one operative does it, the others hold.
		const AOperativeCharacter* Free = GetFocusOperative(Confine.Owner);
		Line = FText::Format(LOCTEXT("ConfineHeldBack", "Hold here - {0} handles it."), Free ? Free->DisplayName : LOCTEXT("ConfineHeldBackSomeone", "the other one"));
		break;
	}
	default:
		Line = LOCTEXT("ConfineHold", "Hold here - follow the training.");
		break;
	}
	PostTrainingLine(Line);
}

bool UTutorialSubsystem::CheckConfinedOrder(const UObject* WorldContext, const AOperativeCharacter* Operative, const FVector& Destination)
{
	UTutorialSubsystem* Tutorial = TutorialSubsystemLocal::FindTutorialFor(WorldContext);
	if (!Tutorial || !Operative || !Tutorial->IsConfining())
	{
		return true;
	}
	const ETutorialConfineVerdict Verdict = Tutorial->JudgeMove(*Operative, Destination);
	if (Verdict == ETutorialConfineVerdict::Allowed)
	{
		return true;
	}
	Tutorial->ReportConfinedRefusal(Verdict, Operative);
	return false;
}

void UTutorialSubsystem::HandleConfineChanged()
{
	SeenConfineSerial = Machine.GetConfineSerial();
	FTutorialConfineView Confine;
	if (!IsTutorialActive() || !Machine.GetActiveConfine(Confine))
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Tutorial: no confinement in force"));
		return;
	}
	UE_LOG(LogCodexTactics, Display, TEXT("Tutorial: %s holds the squad in %s (areas: %s)%s%s"), *Confine.Owner.ToString(), *Confine.Spec.Volume.ToString(),
		*FString::JoinBy(Confine.Areas, TEXT(", "), [](FName Area) { return Area.ToString(); }), Confine.Spec.bStop ? TEXT(", stop") : TEXT(""),
		Confine.Spec.Stance.IsNone() ? TEXT("") : *FString::Printf(TEXT(", stance %s"), *Confine.Spec.Stance.ToString()));
	WriteTelemetry(TEXT("confine_started"), Confine.Owner, Machine.GetCurrentBeat(), 0.f);
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	for (AOperativeCharacter* Member : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
	{
		if (!Member || !Member->IsMoving())
		{
			continue;
		}
		// "stop": everyone stops where he is; otherwise only a walk that would end outside the area (ordered before the squad arrived).
		const bool bOutside = JudgeMove(*Member, Member->GetLastMoveDestination()) != ETutorialConfineVerdict::Allowed;
		if (Confine.Spec.bStop || bOutside)
		{
			Member->StopOperative();
			UE_LOG(LogCodexTactics, Display, TEXT("Tutorial: %s stopped by the confinement (%s)"), *Member->DisplayName.ToString(),
				Confine.Spec.bStop ? TEXT("stop") : TEXT("his walk led outside"));
		}
	}
}

FName UTutorialSubsystem::FilterCommand(const UObject* WorldContext, FName CommandId)
{
	if (!CheckCommandLocked(WorldContext, CommandId))
	{
		return CommandId;
	}
	const UTutorialSubsystem* Tutorial = TutorialSubsystemLocal::FindTutorialFor(WorldContext);
	return Tutorial ? TutorialRules::ResolveLockedCommand(Tutorial->GetMachine().GetLocks(), CommandId) : CommandId;
}

FName UTutorialSubsystem::GetRoleId(const AOperativeCharacter& Operative)
{
	return FName(*StaticEnum<EOperativeRole>()->GetNameStringByValue(static_cast<int64>(Operative.SquadRole)));
}

bool UTutorialSubsystem::IsAtRoleSpot(FName StepId, const AOperativeCharacter& Operative) const
{
	const FName Role = GetRoleId(Operative);
	bool bAnyRoleMarker = false;
	for (const TWeakObjectPtr<ATutorialMarkerActor>& Weak : Markers)
	{
		const ATutorialMarkerActor* Marker = Weak.Get();
		if (!Marker || Marker->StepId != StepId || Marker->RoleId.IsNone())
		{
			continue;
		}
		bAnyRoleMarker = true;
		if (Marker->RoleId == Role
			&& FVector::Dist2D(Marker->GetActorLocation(), Operative.GetActorLocation()) <= Marker->RingRadiusCm + ATutorialMarkerActor::RoleSpotToleranceCm)
		{
			return true;
		}
	}
	return !bAnyRoleMarker;
}

bool UTutorialSubsystem::IsRoleSpotDone(FName StepId, FName RoleId) const
{
	const TSet<FName>* Contributors = Machine.GetContributors(StepId);
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	if (!Contributors || Contributors->IsEmpty() || !Squad || RoleId.IsNone())
	{
		return false;
	}
	for (const AOperativeCharacter* Member : Squad->GetMembers())
	{
		if (Member && Contributors->Contains(Member->GetFName()) && GetRoleId(*Member) == RoleId)
		{
			return true;
		}
	}
	return false;
}

void UTutorialSubsystem::CountRoleSpotPresence()
{
	const FTutorialStepsData& Data = Machine.GetData();
	const bool bAnyRunning = Data.Steps.ContainsByPredicate([this](const FTutorialStep& Step)
	{
		return Step.Complete.bRoleSpots && TutorialRules::IsRunning(Machine.GetState(Step.Id));
	});
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	if (!bAnyRunning || !Squad)
	{
		return;
	}
	bool bCounted = false;
	for (const AOperativeCharacter* Member : Squad->GetMembers())
	{
		// In cover (not on the way along the wall) at his spot: he took his cover there - also before the card came up.
		if (Member && Member->bInCover && !Member->bShimmying && Member->HealthComponent && Member->HealthComponent->IsAlive()
			&& Machine.HandleRoleSpotPresence(Member->GetFName()))
		{
			bCounted = true;
			UE_LOG(LogCodexTactics, Display, TEXT("Tutorial: %s is in cover at his spot (%s)"), *Member->DisplayName.ToString(), *GetRoleId(*Member).ToString());
		}
	}
	if (bCounted)
	{
		RefreshMarkers();
	}
}

void UTutorialSubsystem::RefreshMarkers()
{
	const bool bActive = IsTutorialActive();
	const FName Beat = Machine.GetCurrentBeat();
	Markers.RemoveAll([](const TWeakObjectPtr<ATutorialMarkerActor>& Marker) { return !Marker.IsValid(); });
	RefreshGuide();
	const ATutorialMarkerActor* GuideMarker = GuideView.bValid ? GuideView.Marker.Get() : nullptr;
	for (const TWeakObjectPtr<ATutorialMarkerActor>& Weak : Markers)
	{
		ATutorialMarkerActor* Marker = Weak.Get();
		bool bShow = false;
		if (bActive && !Marker->bRuntimeGuide)
		{
			bShow = !Marker->StepId.IsNone() ? Machine.GetState(Marker->StepId) == ETutorialStepState::Active
				: (!Marker->BeatId.IsNone() && Marker->BeatId == Beat);
			// A role spot whose operative is in place hides (2026-10-19: the others still show where to go).
			bShow = bShow && !(!Marker->RoleId.IsNone() && !Marker->StepId.IsNone() && IsRoleSpotDone(Marker->StepId, Marker->RoleId));
		}
		// 2026-10-20: the current navigation target always shows (a "guide" may name another step's marker; the beacon at a volume).
		Marker->SetMarkerShown(bActive && (bShow || Marker == GuideMarker));
	}
}

void UTutorialSubsystem::TickPacing(float RealDelta)
{
	PacingClock += RealDelta;
	const bool bNothing = Machine.GetActiveCard().IsNone() && Machine.GetActiveObjectives().IsEmpty() && !GuideView.bValid;
	if (bNothing)
	{
		NoInstructionRun += RealDelta;
		NoInstructionTotal += RealDelta;
		return;
	}
	if (NoInstructionRun > 0.5f)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Tutorial pacing: %.1f s without any instruction on screen (beat %s)"), NoInstructionRun, *Machine.GetCurrentBeat().ToString());
		WriteTelemetry(TEXT("no_instruction"), NAME_None, Machine.GetCurrentBeat(), NoInstructionRun);
	}
	NoInstructionRun = 0.f;
}

FString UTutorialSubsystem::DescribeGuide(const FTutorialGuide& Guide)
{
	if (!Guide.IsSet())
	{
		return FString();
	}
	return (Guide.Kind == ETutorialGuideKind::StepMarkers ? TEXT("markers:") : Guide.Kind == ETutorialGuideKind::Spot ? TEXT("spot:") : TEXT("volume:"))
		+ Guide.Id.ToString();
}

void UTutorialSubsystem::RegisterVolume(ATutorialTriggerVolume* Volume)
{
	if (Volume)
	{
		GuideVolumes.AddUnique(Volume);
	}
}

void UTutorialSubsystem::UnregisterVolume(ATutorialTriggerVolume* Volume)
{
	GuideVolumes.Remove(Volume);
}

const ATutorialTriggerVolume* UTutorialSubsystem::FindGuideVolume(FName VolumeId, const FVector& Near) const
{
	const ATutorialTriggerVolume* Best = nullptr;
	for (const TWeakObjectPtr<ATutorialTriggerVolume>& Weak : GuideVolumes)
	{
		const ATutorialTriggerVolume* Volume = Weak.Get();
		if (Volume && Volume->Box && Volume->GetEffectiveVolumeId() == VolumeId
			&& (!Best || FVector::DistSquared2D(Volume->GetActorLocation(), Near) < FVector::DistSquared2D(Best->GetActorLocation(), Near)))
		{
			Best = Volume;
		}
	}
	return Best;
}

FVector UTutorialSubsystem::GetVolumeGroundPoint(const ATutorialTriggerVolume& Volume) const
{
	const FVector Centre = Volume.Box->Bounds.Origin;
	const float HalfHeight = Volume.Box->Bounds.BoxExtent.Z;
	const FVector Top = Centre + FVector(0.f, 0.f, HalfHeight);
	const FVector Bottom = Centre - FVector(0.f, 0.f, HalfHeight + 300.f);
	FHitResult Hit;
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(TutorialGuideGround), false);
	if (GetWorld() && GetWorld()->LineTraceSingleByObjectType(Hit, Top, Bottom, FCollisionObjectQueryParams(ECC_WorldStatic), Params))
	{
		return Hit.ImpactPoint + FVector(0.f, 0.f, 2.f);
	}
	return Centre - FVector(0.f, 0.f, HalfHeight - 2.f); // volumes stand on the ground: their bottom
}

bool UTutorialSubsystem::ResolveGuideSpot(FName Spot, FName StepId, FVector& OutLocation) const
{
	UWorld* World = GetWorld();
	FString Kind;
	FName Enemy;
	if (!World || !TutorialRules::ParseGuideSpot(Spot, Kind, Enemy) || !TutorialRules::IsKnownGuideSpot(Spot))
	{
		return false;
	}
	// The step's focus operative (the ambusher), else the leader: whom the spot is for.
	const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
	const AOperativeCharacter* From = GetFocusOperative(StepId);
	From = From ? From : (Squad ? Squad->GetLeader() : nullptr);
	if (!From)
	{
		return false;
	}
	const FVector FromLocation = From->GetActorLocation();
	if (Kind == TEXT("enemy"))
	{
		// "@enemy.<enemy>" (2026-10-21): the nearest LIVING bound enemy - the target follows him while he walks.
		bool bFound = false;
		double BestDistance = TNumericLimits<double>::Max();
		for (const AEnemyCharacter* Bound : FindBoundEnemies(Enemy))
		{
			const UHealthComponent* Health = Bound ? Bound->GetHealthComponent() : nullptr;
			if (!Bound || Bound->IsDying() || (Health && !Health->IsAlive()))
			{
				continue;
			}
			const double Distance = FVector::DistSquared2D(Bound->GetActorLocation(), FromLocation);
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				OutLocation = Bound->GetActorLocation() - FVector(0.f, 0.f, Bound->GetSimpleCollisionHalfHeight());
				bFound = true;
			}
		}
		return bFound;
	}
	// The routes to look at: every patrol route of the level ("@patrol_route"), or only the bound enemies' ("@...<enemy>").
	TArray<const APatrolRouteActor*> Routes;
	if (Enemy.IsNone())
	{
		for (TActorIterator<APatrolRouteActor> It(World); It; ++It)
		{
			Routes.Add(*It);
		}
	}
	else
	{
		for (const AEnemyCharacter* Bound : FindBoundEnemies(Enemy))
		{
			if (Bound && Bound->AssignedPatrolRoute)
			{
				Routes.AddUnique(Bound->AssignedPatrolRoute.Get());
			}
		}
	}
	bool bFound = false;
	double BestDistance = TNumericLimits<double>::Max();
	for (const APatrolRouteActor* Route : Routes)
	{
		const TArray<FVector> Points = Route ? TutorialSubsystemLocal::TutorialRoutePoints(*Route) : TArray<FVector>();
		if (Points.IsEmpty())
		{
			continue;
		}
		const FVector Candidate = TutorialRules::NearestPointOnRoute(Points, Route->bIsLoop, FromLocation);
		const double Distance = FVector::DistSquared2D(Candidate, FromLocation);
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			OutLocation = Candidate;
			bFound = true;
		}
	}
	if (bFound && Kind == TEXT("lure_spot"))
	{
		// "@lure_spot.<enemy>": short of the route towards her, inside the whistle / crouched throw reach; on the ground.
		OutLocation = TutorialRules::LureSpotTowards(OutLocation, FromLocation, TutorialRules::LureSpotDistanceCm);
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TutorialLureSpotGround), false);
		Params.AddIgnoredActor(From);
		if (World->LineTraceSingleByObjectType(Hit, OutLocation + FVector(0.f, 0.f, 500.f), OutLocation - FVector(0.f, 0.f, 1500.f),
			FCollisionObjectQueryParams(ECC_WorldStatic), Params))
		{
			OutLocation = Hit.ImpactPoint;
		}
	}
	return bFound;
}

TArray<AEnemyCharacter*> UTutorialSubsystem::FindBoundEnemies(FName BindingId) const
{
	TArray<AEnemyCharacter*> Out;
	const UWorld* World = GetWorld();
	if (!World || !Machine.GetData().FindEnemy(BindingId))
	{
		return Out;
	}
	for (AEnemyCharacter* Enemy : UEnemyRegistrySubsystem::GetEnemyCharacters(World))
	{
		if (Enemy && IsEnemyBound(*Enemy, BindingId))
		{
			Out.Add(Enemy);
		}
	}
	return Out;
}

bool UTutorialSubsystem::IsEnemyBound(const AEnemyCharacter& Enemy, FName BindingId) const
{
	const FTutorialEnemyBinding* Binding = Machine.GetData().FindEnemy(BindingId);
	if (!Binding)
	{
		return false;
	}
	const APatrolRouteActor* Route = Enemy.AssignedPatrolRoute.Get();
	return (Route && TutorialSubsystemLocal::TutorialActorMatchesAny(*Route, Binding->Routes))
		|| TutorialSubsystemLocal::TutorialActorMatchesAny(Enemy, Binding->Actors);
}

bool UTutorialSubsystem::PassesEnemyFilterNow(const FTutorialEnemyFilter& Filter, FName SubjectId) const
{
	const UWorld* World = GetWorld();
	if (!Filter.IsSet())
	{
		return true;
	}
	if (!World || SubjectId.IsNone())
	{
		return false;
	}
	// The subject by its object name (the bus passes Subject->GetFName()); a dying enemy is still in the world when its kill is raised.
	const AEnemyCharacter* Subject = nullptr;
	for (AEnemyCharacter* Enemy : UEnemyRegistrySubsystem::GetEnemyCharacters(World))
	{
		if (Enemy && Enemy->GetFName() == SubjectId)
		{
			Subject = Enemy;
			break;
		}
	}
	if (!Subject)
	{
		return false; // not an enemy (an operative tripped the wire) or gone
	}
	float NearestCm = -1.f;
	if (const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>())
	{
		for (const AOperativeCharacter* Member : Squad->GetMembers())
		{
			if (TutorialSubsystemLocal::IsLivingOrUntracked(Member))
			{
				const float Distance = static_cast<float>(FVector::Dist(Member->GetActorLocation(), Subject->GetActorLocation()));
				NearestCm = NearestCm < 0.f ? Distance : FMath::Min(NearestCm, Distance);
			}
		}
	}
	float OffRouteCm = -1.f;
	if (const APatrolRouteActor* Route = Subject->AssignedPatrolRoute.Get())
	{
		const TArray<FVector> Points = TutorialSubsystemLocal::TutorialRoutePoints(*Route);
		OffRouteCm = Points.IsEmpty() ? -1.f : TutorialRules::DistanceToRoute2D(Points, Route->bIsLoop, Subject->GetActorLocation());
	}
	const bool bBound = IsEnemyBound(*Subject, Filter.Enemy);
	const bool bPasses = TutorialRules::PassesEnemyFilter(Filter, bBound, NearestCm, OffRouteCm);
	UE_LOG(LogCodexTactics, Display, TEXT("Tutorial: enemy filter %s on %s: bound %d, nearest operative %.1f m (within %.0f), off its route %.1f m (at least %.0f) -> %s"),
		*Filter.Enemy.ToString(), *SubjectId.ToString(), bBound ? 1 : 0, NearestCm / 100.f, Filter.WithinMetres, OffRouteCm / 100.f, Filter.OffRouteMetres,
		bPasses ? TEXT("counts") : TEXT("does not count"));
	return bPasses;
}

void UTutorialSubsystem::LogEnemyBindings() const
{
	for (const FTutorialEnemyBinding& Binding : Machine.GetData().Enemies)
	{
		TArray<FString> Names;
		for (const AEnemyCharacter* Enemy : FindBoundEnemies(Binding.Id))
		{
			const APatrolRouteActor* Route = Enemy->AssignedPatrolRoute.Get();
			Names.Add(FString::Printf(TEXT("%s (route %s)"), *Enemy->GetName(), Route ? *Route->GetName() : TEXT("-")));
		}
		if (Names.IsEmpty())
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("Tutorial: enemy binding %s (route %s / actor %s) matches no enemy in this level - its steps end by their timeouts only"),
				*Binding.Id.ToString(), *FString::Join(Binding.Routes, TEXT(", ")), *FString::Join(Binding.Actors, TEXT(", ")));
		}
		else
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Tutorial: enemy binding %s -> %s"), *Binding.Id.ToString(), *FString::Join(Names, TEXT(", ")));
		}
	}
}

void UTutorialSubsystem::RefreshGuide()
{
	if (bRefreshingGuide)
	{
		return; // the beacon's BeginPlay registers it while it is being spawned here
	}
	TGuardValue<bool> Refreshing(bRefreshingGuide, true);
	FTutorialGuideView View;
	View.Guide = IsTutorialActive() ? Machine.GetGuide() : FTutorialGuide();
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	const AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	const FVector From = Leader ? Leader->GetActorLocation() : FVector::ZeroVector;
	if (View.Guide.Kind == ETutorialGuideKind::StepMarkers)
	{
		// The nearest of the step's markers (a role spot already taken does not count: the others still show where to go).
		ATutorialMarkerActor* Best = nullptr;
		for (const TWeakObjectPtr<ATutorialMarkerActor>& Weak : Markers)
		{
			ATutorialMarkerActor* Marker = Weak.Get();
			if (!Marker || Marker->bRuntimeGuide || Marker->StepId != View.Guide.Id
				|| (!Marker->RoleId.IsNone() && IsRoleSpotDone(Marker->StepId, Marker->RoleId)))
			{
				continue;
			}
			if (!Best || FVector::DistSquared2D(Marker->GetActorLocation(), From) < FVector::DistSquared2D(Best->GetActorLocation(), From))
			{
				Best = Marker;
			}
		}
		if (Best)
		{
			View.Marker = Best;
			View.Location = Best->GetActorLocation();
			View.bEdgePointer = Best->bScreenEdgePointer;
			View.bValid = true;
		}
	}
	else if (View.Guide.Kind == ETutorialGuideKind::Volume || View.Guide.Kind == ETutorialGuideKind::Spot)
	{
		const ATutorialTriggerVolume* Volume = View.Guide.Kind == ETutorialGuideKind::Volume ? FindGuideVolume(View.Guide.Id, From) : nullptr;
		FVector SpotLocation;
		const bool bSpot = View.Guide.Kind == ETutorialGuideKind::Spot && ResolveGuideSpot(View.Guide.Id, View.Guide.SourceStep, SpotLocation);
		if (Volume || bSpot)
		{
			const FVector Ground = Volume ? GetVolumeGroundPoint(*Volume) : SpotLocation + FVector(0.f, 0.f, 2.f);
			ATutorialMarkerActor* Beacon = GuideBeacon.Get();
			if (!Beacon && GetWorld())
			{
				// One beacon for every volume target (moved when the target changes); hidden unless it is the target.
				const FTransform Spawn(FRotator::ZeroRotator, Ground);
				Beacon = GetWorld()->SpawnActorDeferred<ATutorialMarkerActor>(ATutorialMarkerActor::StaticClass(), Spawn, nullptr, nullptr,
					ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				if (Beacon)
				{
					Beacon->bRuntimeGuide = true;
					GuideBeacon = Beacon; // before its BeginPlay registers it (RefreshMarkers -> here again: no second beacon)
					Beacon->FinishSpawning(Spawn);
				}
			}
			if (Beacon)
			{
				if (!Beacon->GetActorLocation().Equals(Ground, 1.f))
				{
					Beacon->SetActorLocation(Ground);
				}
				View.Marker = Beacon;
				View.Location = Ground;
				View.bValid = true;
			}
		}
	}
	if (View.Guide != GuideView.Guide)
	{
		const FString Described = DescribeGuide(View.Guide);
		UE_LOG(LogCodexTactics, Display, TEXT("Tutorial: current target -> %s (asked by %s)%s"), Described.IsEmpty() ? TEXT("none") : *Described,
			View.Guide.SourceStep.IsNone() ? TEXT("the route") : *View.Guide.SourceStep.ToString(),
			View.Guide.IsSet() && !View.bValid ? TEXT(" - NOT in the level (no marker / volume to show)") : TEXT(""));
		if (IsTutorialActive())
		{
			WriteTelemetry(TEXT("guide"), FName(*Described), Machine.GetCurrentBeat(), 0.f);
		}
	}
	GuideView = View;
}

void UTutorialSubsystem::RegisterMarker(ATutorialMarkerActor* Marker)
{
	if (Marker)
	{
		Markers.AddUnique(Marker);
		RefreshMarkers();
	}
}

void UTutorialSubsystem::UnregisterMarker(ATutorialMarkerActor* Marker)
{
	Markers.Remove(Marker);
}

TArray<ATutorialMarkerActor*> UTutorialSubsystem::GetVisibleMarkers() const
{
	TArray<ATutorialMarkerActor*> Out;
	for (const TWeakObjectPtr<ATutorialMarkerActor>& Weak : Markers)
	{
		if (ATutorialMarkerActor* Marker = Weak.Get(); Marker && Marker->IsMarkerShown())
		{
			Out.Add(Marker);
		}
	}
	return Out;
}

FText UTutorialSubsystem::GetTutorialText(FName TextId)
{
	if (TextId.IsNone())
	{
		return FText::GetEmpty();
	}
	return FText::FromString(PlayerCommands::ResolveKeyTokens(FNarrativeManifest::Get().GetTutorialText(TextId.ToString()).Text));
}

FTutorialCardView UTutorialSubsystem::BuildCardView(FName StepId, bool bDone) const
{
	FTutorialCardView View;
	const FTutorialStep* Step = Machine.GetData().FindStep(StepId);
	if (!Step)
	{
		return View;
	}
	View.bVisible = true;
	View.StepId = StepId;
	View.bDone = bDone;
	View.bSkippable = Step->bSkippable && !bDone;
	if (!Step->TextId.IsNone())
	{
		const FTutorialTextView Text = FNarrativeManifest::Get().GetTutorialText(Step->TextId.ToString());
		// Rev 5i: {progress} / {count} = how far the card's completion got ("1 / 3 through").
		auto Progress = [this, Step, StepId](const FString& In)
		{
			return In.Replace(TEXT("{progress}"), *FString::FromInt(FMath::Min(Machine.GetProgress(StepId), Step->Complete.Count)))
				.Replace(TEXT("{count}"), *FString::FromInt(Step->Complete.Count));
		};
		View.Title = FText::FromString(Progress(ResolveFocusToken(Text.Title, StepId)));
		View.Body = FText::FromString(PlayerCommands::ResolveKeyTokens(Progress(ResolveFocusToken(Text.Text, StepId))));
	}
	if (AOperativeCharacter* Focus = GetFocusOperative(StepId))
	{
		View.FocusOperative = Focus;
		View.FocusName = Focus->DisplayName;
	}
	View.Keys = FText::FromString(TutorialRules::FormatKeys(Step->Keys));
	int32 Index = 0;
	int32 Count = 0;
	Machine.GetCardProgress(StepId, Index, Count);
	if (Count > 1)
	{
		View.Progress = FText::FromString(FString::Printf(TEXT("%d / %d"), Index, Count));
	}
	return View;
}

FTutorialCardView UTutorialSubsystem::GetCardView() const
{
	if (!IsTutorialActive())
	{
		return FTutorialCardView();
	}
	if (FlashCardLeft > 0.f && !FlashCard.IsNone())
	{
		return BuildCardView(FlashCard, true);
	}
	const FName Active = Machine.GetActiveCard();
	return Active.IsNone() ? FTutorialCardView() : BuildCardView(Active, false);
}

FText UTutorialSubsystem::ObjectiveText(FName StepId) const
{
	const FTutorialStep* Step = Machine.GetData().FindStep(StepId);
	if (!Step || Step->TextId.IsNone())
	{
		return FText::GetEmpty();
	}
	const FTutorialTextView Text = FNarrativeManifest::Get().GetTutorialText(Step->TextId.ToString());
	// 2026-10-20: {focus} in objectives too (the A4 ambusher waits behind the cover).
	return FText::FromString(ResolveFocusToken(PlayerCommands::ResolveKeyTokens(Text.Text.IsEmpty() ? Text.Title : Text.Text), StepId));
}

TArray<FTutorialObjectiveView> UTutorialSubsystem::GetObjectiveViews() const
{
	TArray<FTutorialObjectiveView> Views;
	if (!IsTutorialActive())
	{
		return Views;
	}
	for (const FName StepId : Machine.GetActiveObjectives())
	{
		FTutorialObjectiveView& View = Views.AddDefaulted_GetRef();
		View.StepId = StepId;
		View.Text = ObjectiveText(StepId);
	}
	for (const TPair<FName, float>& Flash : FlashObjectives)
	{
		if (Views.Num() >= Machine.MaxObjectives)
		{
			break;
		}
		FTutorialObjectiveView& View = Views.AddDefaulted_GetRef();
		View.StepId = Flash.Key;
		View.Text = ObjectiveText(Flash.Key);
		View.bDone = true;
	}
	return Views;
}

FTutorialSubtitleView UTutorialSubsystem::GetSubtitleView() const
{
	FTutorialSubtitleView View;
	if (!IsTutorialActive() || SpeechStep.IsNone() || !SpeechLines.IsValidIndex(SpeechIndex))
	{
		return View;
	}
	const FSpokenLine& Line = SpeechLines[SpeechIndex];
	const FDialogueSpeakerStyle Style = DialogueRules::GetSpeakerStyle(Line.Speaker);
	View.bVisible = true;
	View.StepId = SpeechStep;
	View.Speaker = FText::FromString(Line.Speaker);
	View.Role = Style.Role;
	View.Line = FText::FromString(Line.Text);
	View.SpeakerColor = FLinearColor::FromSRGBColor(Style.NameColor.ToFColor(false));
	View.SecondsLeft = SpeechLineLeft;
	View.LineIndex = SpeechIndex;
	View.LineCount = SpeechLines.Num();
	return View;
}

FTutorialRetryView UTutorialSubsystem::GetRetryView() const
{
	FTutorialRetryView View;
	View.bVisible = bRetryPrompt && IsTutorialActive();
	const USaveGameSubsystem* Saves = GetWorld() ? GetWorld()->GetSubsystem<USaveGameSubsystem>() : nullptr;
	View.bHasCheckpoint = Saves && Saves->HasSave(CheckpointSlot);
	View.Text = View.bHasCheckpoint
		? LOCTEXT("RetryText", "Training: that hit would have been fatal. The operative is down, not dead.")
		: LOCTEXT("RetryNoCheckpoint", "Training: that hit would have been fatal. The operative is down, not dead (no checkpoint yet).");
	return View;
}

// --- Radio / walk-and-talk ------------------------------------------------------------------------------------------

TArray<UTutorialSubsystem::FSpokenLine> UTutorialSubsystem::ResolveLines(const FTutorialStep& Step) const
{
	TArray<FSpokenLine> Lines;
	const FNarrativeManifest& Manifest = FNarrativeManifest::Get();
	const FString DefaultSpeaker = Step.Kind == ETutorialStepKind::Radio ? TEXT("HQ") : TEXT("Squad");
	auto AddLine = [&Lines, &Manifest, &DefaultSpeaker](const FString& Speaker, FName TextId, float Duration)
	{
		const FTutorialTextView Text = Manifest.GetTutorialText(TextId.ToString());
		FSpokenLine& Line = Lines.AddDefaulted_GetRef();
		Line.Speaker = !Speaker.IsEmpty() ? Speaker : (!Text.Speaker.IsEmpty() ? Text.Speaker : DefaultSpeaker);
		Line.Text = PlayerCommands::ResolveKeyTokens(!Text.Radio.IsEmpty() ? Text.Radio : Text.Text);
		Line.Duration = TutorialRules::LineDurationSeconds(Line.Text, Duration);
	};
	if (Step.Lines.Num() > 0)
	{
		for (const FTutorialLine& Line : Step.Lines)
		{
			AddLine(Line.Speaker, Line.TextId, Line.DurationSeconds);
		}
		return Lines;
	}
	// A whole narrative sequence with this id (the walk-and-talk sheet rows), else one line.
	if (const TArray<FNarrativeLine>* Sequence = Manifest.Sequences.Find(Step.TextId.ToString()); Sequence && Sequence->Num() > 1)
	{
		for (int32 Index = 0; Index < Sequence->Num(); ++Index)
		{
			const FNarrativeLine& Source = (*Sequence)[Index];
			FSpokenLine& Line = Lines.AddDefaulted_GetRef();
			Line.Speaker = FNarrativeManifest::EnglishOr(Source.Speaker, DefaultSpeaker);
			Line.Text = FNarrativeManifest::EnglishOr(Source.Text, FNarrativeManifest::MakeMissingPlaceholder(Step.TextId.ToString(), Index + 1));
			Line.Duration = TutorialRules::LineDurationSeconds(Line.Text, Source.DelayAfter);
		}
		return Lines;
	}
	if (!Step.TextId.IsNone())
	{
		AddLine(FString(), Step.TextId, 0.f);
	}
	return Lines;
}

void UTutorialSubsystem::StartSpeech(FName StepId)
{
	const FTutorialStep* Step = Machine.GetData().FindStep(StepId);
	if (!Step)
	{
		return;
	}
	SpeechStep = StepId;
	SpeechLines = ResolveLines(*Step);
	SpeechIndex = 0;
	SpeechLineLeft = SpeechLines.IsValidIndex(0) ? SpeechLines[0].Duration : 0.f;
	for (const FSpokenLine& Line : SpeechLines)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Tutorial radio [%s] %s: %s"), *StepId.ToString(), *Line.Speaker, *Line.Text);
	}
	if (SpeechLines.IsEmpty())
	{
		Machine.CompleteStep(StepId);
		SpeechStep = NAME_None;
	}
}

void UTutorialSubsystem::TickSpeech(float DeltaSeconds)
{
	if (SpeechStep.IsNone())
	{
		return;
	}
	SpeechLineLeft -= DeltaSeconds;
	if (SpeechLineLeft > 0.f)
	{
		return;
	}
	++SpeechIndex;
	if (SpeechLines.IsValidIndex(SpeechIndex))
	{
		SpeechLineLeft = SpeechLines[SpeechIndex].Duration;
		return;
	}
	EndSpeech();
}

void UTutorialSubsystem::EndSpeech()
{
	const FName Done = SpeechStep;
	SpeechStep = NAME_None;
	SpeechLines.Reset();
	SpeechIndex = 0;
	SpeechLineLeft = 0.f;
	const FTutorialStep* Step = Machine.GetData().FindStep(Done);
	// Lines played out: the step is done unless it waits for something else (then it stays until that / its timeout).
	if (Step && !Step->Complete.HasCondition())
	{
		Machine.CompleteStep(Done);
	}
}

bool UTutorialSubsystem::IsSpeechActive() const
{
	return IsTutorialActive() && !SpeechStep.IsNone() && SpeechLines.IsValidIndex(SpeechIndex);
}

bool UTutorialSubsystem::AdvanceSpeechLine()
{
	if (!IsSpeechActive())
	{
		return false;
	}
	const FName StepId = SpeechStep;
	UE_LOG(LogCodexTactics, Display, TEXT("Tutorial: %s line %d / %d advanced by the player"), *StepId.ToString(), SpeechIndex + 1, SpeechLines.Num());
	WriteTelemetry(TEXT("line_advanced"), StepId, Machine.GetCurrentBeat(), 0.f);
	SpeechLineLeft = 0.f;
	TickSpeech(0.f); // the same path as a line running out: the next line, or the end of the step
	ProcessTransitions();
	return true;
}

bool UTutorialSubsystem::SkipSpeech()
{
	if (!IsSpeechActive())
	{
		return false;
	}
	const FName StepId = SpeechStep;
	UE_LOG(LogCodexTactics, Display, TEXT("Tutorial: %s skipped by the player at line %d / %d (finished as played out)"), *StepId.ToString(),
		SpeechIndex + 1, SpeechLines.Num());
	WriteTelemetry(TEXT("speech_skipped"), StepId, Machine.GetCurrentBeat(), 0.f);
	EndSpeech();
	ProcessTransitions();
	return true;
}

// --- Lesson downs, checkpoint ---------------------------------------------------------------------------------------

bool UTutorialSubsystem::HandleLethalHit(UHealthComponent* Health, const FString& Source)
{
	if (!TutorialRules::ShouldDownInsteadOfKill(IsTutorialActive(), Machine.IsLessonBeat()))
	{
		return false;
	}
	AOperativeCharacter* Operative = Health ? Cast<AOperativeCharacter>(Health->GetOwner()) : nullptr;
	if (!Operative)
	{
		return false;
	}
	++LessonDownCount;
	bRetryPrompt = true;
	UE_LOG(LogCodexTactics, Display, TEXT("Tutorial: lesson beat %s - lethal hit (%s) on %s downs instead of killing"), *Machine.GetCurrentBeat().ToString(),
		*Source, *Operative->DisplayName.ToString());
	WriteTelemetry(TEXT("lesson_downed"), NAME_None, Machine.GetCurrentBeat(), 0.f);
	PostTrainingLine(FText::Format(LOCTEXT("LessonDowned", "{0} is down - in a real fight that would have been fatal. Retry from the checkpoint or carry on."),
		Operative->DisplayName));
	// Fall after the damage call has unwound (knockdown starts its clip / capsule changes).
	TWeakObjectPtr<AOperativeCharacter> WeakOperative(Operative);
	TWeakObjectPtr<UTutorialSubsystem> WeakThis(this);
	GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [WeakThis, WeakOperative]()
	{
		AOperativeCharacter* Downed = WeakOperative.Get();
		if (Downed && Downed->KnockdownComponent && !Downed->KnockdownComponent->IsDown())
		{
			Downed->KnockdownComponent->ForceKnockDown(EKnockdownDirection::Back, EKnockdownCause::HeavyHit);
		}
		if (UTutorialSubsystem* Self = WeakThis.Get())
		{
			GameplayEvents::Raise(Self, GameplayEvents::LessonDowned, Downed);
		}
	}));
	return true;
}

bool UTutorialSubsystem::IsLoadPending() const
{
	const UWorld* World = GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UMissionSessionSubsystem* Session = GameInstance ? GameInstance->GetSubsystem<UMissionSessionSubsystem>() : nullptr;
	return Session && Session->HasPendingLoad();
}

bool UTutorialSubsystem::SaveCheckpoint()
{
	USaveGameSubsystem* Saves = GetWorld() ? GetWorld()->GetSubsystem<USaveGameSubsystem>() : nullptr;
	if (!Saves || !Saves->bCheckpointsEnabled)
	{
		return false;
	}
	if (IsLoadPending())
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Tutorial: no checkpoint at beat %s (a save is being loaded)"), *Machine.GetCurrentBeat().ToString());
		return false;
	}
	FText Reason;
	if (!Saves->CanSaveNow(&Reason))
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Tutorial: no checkpoint at beat %s (%s)"), *Machine.GetCurrentBeat().ToString(), *Reason.ToString());
		return false;
	}
	const FTutorialBeat* Beat = Machine.GetCurrentBeatData();
	const FString BeatTitle = Beat && !Beat->TitleId.IsNone() ? GetTutorialText(Beat->TitleId).ToString() : FString();
	const FString Title = BeatTitle.IsEmpty() || BeatTitle.StartsWith(TEXT("[EN missing")) ? FString::Printf(TEXT("Checkpoint: %s"), *Machine.GetCurrentBeat().ToString())
		: FString::Printf(TEXT("Checkpoint: %s - %s"), *Machine.GetCurrentBeat().ToString(), *BeatTitle);
	const bool bSaved = Saves->SaveGame(CheckpointSlot, Title);
	UE_LOG(LogCodexTactics, Display, TEXT("Tutorial: checkpoint %s at beat %s"), bSaved ? TEXT("saved") : TEXT("FAILED"), *Machine.GetCurrentBeat().ToString());
	return bSaved;
}

bool UTutorialSubsystem::RetryFromCheckpoint()
{
	USaveGameSubsystem* Saves = GetWorld() ? GetWorld()->GetSubsystem<USaveGameSubsystem>() : nullptr;
	if (!Saves || !Saves->HasSave(CheckpointSlot))
	{
		return false;
	}
	WriteTelemetry(TEXT("retry"), NAME_None, Machine.GetCurrentBeat(), 0.f);
	bRetryPrompt = false;
	LessonDownCount = 0;
	// Rev 2026-10-19 (user: after "Retry from checkpoint" the Commander was still getting up from the knockdown that made him retry, the
	// squad fled instead of fighting, Solo did not hold): the checkpoint reopens the level and is applied to the fresh world (as F9).
	// Steps injected at runtime (a dev command / smoke, not the level's own demo settings) cannot be rebuilt by a reload: those load in
	// place (USaveGameSubsystem::LoadGame resets every operative's transient state first).
	const UDemoModeSubsystem* Demo = GetWorld()->GetSubsystem<UDemoModeSubsystem>();
	const bool bLevelSteps = Demo && Demo->IsDemo() && Demo->GetSettings().bTutorial && Demo->GetSettings().TutorialSteps == StepsFile;
	if (bLevelSteps)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Tutorial: retry - the checkpoint reopens the level"));
		return Saves->LoadGameWithTravel(CheckpointSlot);
	}
	return Saves->LoadGame(CheckpointSlot);
}

// --- Save -----------------------------------------------------------------------------------------------------------

TSharedPtr<FJsonObject> UTutorialSubsystem::CaptureSaveState() const
{
	TSharedPtr<FJsonObject> Object;
	if (!Machine.IsLoaded() && DeferredProgress.IsSet())
	{
		Object = TutorialRules::SaveProgress(DeferredProgress.GetValue(), IsTutorialEnabled());
	}
	else
	{
		const FTutorialProgress Progress = Machine.ExportProgress();
		if (!Machine.IsLoaded() && Progress.IsEmpty())
		{
			return nullptr;
		}
		Object = TutorialRules::SaveProgress(Progress, IsTutorialEnabled());
	}
	// Rev 5f (save round trip): which tutorial volumes are used up (actor names) - before, a load in place (Retry from checkpoint)
	// kept the volumes the squad had walked through after the checkpoint used up, so their steps never fired again; and the
	// command families that already got their "Not yet" line.
	TArray<TSharedPtr<FJsonValue>> Fired;
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<ATutorialTriggerVolume> It(World); It; ++It)
		{
			if (It->HasFired())
			{
				Fired.Add(MakeShared<FJsonValueString>(It->GetName()));
			}
		}
	}
	Object->SetArrayField(TEXT("volumes_fired"), Fired);
	TArray<FString> Families = LockHintFamilies.Array();
	Families.Sort();
	TArray<TSharedPtr<FJsonValue>> Hints;
	for (const FString& Family : Families)
	{
		Hints.Add(MakeShared<FJsonValueString>(Family));
	}
	Object->SetArrayField(TEXT("lock_hints"), Hints);
	Object->SetBoolField(TEXT("skipped"), bSkipped); // rev 5i: SKIP TUTORIAL stays skipped after a load
	// 2026-10-20: the current navigation target (informational: it is derived from the progress + "route" on load).
	Object->SetStringField(TEXT("guide"), DescribeGuide(GuideView.Guide));
	return Object;
}

void UTutorialSubsystem::RestoreSaveState(const FJsonObject& Object)
{
	FTutorialProgress Progress;
	if (!TutorialRules::LoadProgress(Object, Progress))
	{
		return;
	}
	// Rev 5f: the volumes are used up exactly as in the save (an older save without the list: none is, re-entering one is harmless
	// - progress is monotonic by location).
	TSet<FString> Fired;
	const TArray<TSharedPtr<FJsonValue>>* FiredList = nullptr;
	if (Object.TryGetArrayField(TEXT("volumes_fired"), FiredList))
	{
		for (const TSharedPtr<FJsonValue>& Value : *FiredList)
		{
			FString Name;
			if (Value.IsValid() && Value->TryGetString(Name))
			{
				Fired.Add(Name);
			}
		}
	}
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<ATutorialTriggerVolume> It(World); It; ++It)
		{
			It->SetFired(Fired.Contains(It->GetName()));
		}
	}
	bool bSavedSkipped = false;
	bSkipped = Object.TryGetBoolField(TEXT("skipped"), bSavedSkipped) && bSavedSkipped;
	LockHintFamilies.Reset();
	const TArray<TSharedPtr<FJsonValue>>* Hints = nullptr;
	if (Object.TryGetArrayField(TEXT("lock_hints"), Hints))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Hints)
		{
			FString Family;
			if (Value.IsValid() && Value->TryGetString(Family) && !Family.IsEmpty())
			{
				LockHintFamilies.Add(Family);
			}
		}
	}
	if (!Machine.IsLoaded())
	{
		DeferredProgress = Progress;
		return;
	}
	ClearOverlayState();
	Machine.ImportProgress(Progress);
	ProcessTransitions();
	RefreshMarkers();
	UE_LOG(LogCodexTactics, Display, TEXT("Tutorial: progress loaded (beat %s, %d finished, %d pending)"), *Progress.CurrentBeat.ToString(),
		Progress.Finished.Num(), Progress.Pending.Num());
}

// --- Telemetry ------------------------------------------------------------------------------------------------------

FString UTutorialSubsystem::GetTelemetryPath() const
{
	return FPaths::ProjectSavedDir() / TEXT("Tutorial/tutorial.jsonl");
}

void UTutorialSubsystem::WriteTelemetry(const FString& Event, FName StepId, FName BeatId, float ActiveSeconds) const
{
	if (TutorialConsole::CVarTutorialTelemetry.GetValueOnGameThread() == 0)
	{
		return;
	}
	const UWorld* World = GetWorld();
	FTutorialTelemetryEntry Entry;
	Entry.Event = Event;
	Entry.Step = StepId;
	Entry.Beat = BeatId;
	Entry.ActiveSeconds = ActiveSeconds;
	Entry.GameSeconds = World ? World->GetTimeSeconds() : 0.f;
	Entry.Map = World ? UWorld::RemovePIEPrefix(World->GetMapName()) : FString();
	Entry.Session = SessionId;
	Entry.Timestamp = FDateTime::UtcNow().ToIso8601();
	const FString Line = TutorialRules::BuildTelemetryLine(Entry) + LINE_TERMINATOR;
	FFileHelper::SaveStringToFile(Line, *GetTelemetryPath(), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
}

#undef LOCTEXT_NAMESPACE
