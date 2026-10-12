#include "Tutorial/TutorialSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/KnockdownComponent.h"
#include "Core/DemoModeSubsystem.h"
#include "Core/SaveGameSubsystem.h"
#include "Data/NarrativeManifest.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
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
#include "Engine/Engine.h"

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
	Machine.Load(Data);
	LockedRefusals = 0;
	FocusOperatives.Reset();
	SpeechStep = NAME_None;
	SpeechLines.Reset();
	FlashCard = NAME_None;
	FlashObjectives.Reset();
	bRetryPrompt = false;
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
	return Machine.IsLoaded() && IsTutorialEnabled();
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
	TickSpeech(RealDelta);
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
	if (Subject && Subject->GetWorld() != GetWorld())
	{
		return;
	}
	NotifyEvent(EventName, Subject ? Subject->GetFName() : NAME_None);
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
	NotifyEvent(GameplayEvents::ItemUsed);
	NotifyEvent(FName(*(GameplayEvents::ItemUsed.ToString() + TEXT(".") + ItemId)));
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
	Machine.HandleVolume(Volume->GetEffectiveVolumeId(), Volume->StepId, Volume->BeatId);
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
	const bool bSkipped = !StepId.IsNone() && Machine.SkipStep(StepId);
	ProcessTransitions();
	return bSkipped;
}

bool UTutorialSubsystem::ReplayStep(FName StepId)
{
	const bool bReplayed = IsTutorialActive() && Machine.ReplayStep(StepId);
	ProcessTransitions();
	return bReplayed;
}

void UTutorialSubsystem::ResetTutorial()
{
	Machine.ResetProgress();
	FocusOperatives.Reset();
	SpeechStep = NAME_None;
	SpeechLines.Reset();
	FlashCard = NAME_None;
	FlashObjectives.Reset();
	bRetryPrompt = false;
	WriteTelemetry(TEXT("reset"), NAME_None, NAME_None, 0.f);
	StartTutorial();
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
			UE_LOG(LogCodexTactics, Display, TEXT("Tutorial: %s %s -> %s%s"), *Transition.StepId.ToString(), TutorialRules::StateName(Transition.From),
				TutorialRules::StateName(Transition.To), Transition.bSilent ? TEXT(" (silent)") : TEXT(""));
			if (Transition.To == ETutorialStepState::Active)
			{
				WriteTelemetry(TEXT("shown"), Step->Id, Step->Beat, 0.f);
				HandleStepShown(*Step);
				if (Step->Kind == ETutorialStepKind::Radio || Step->Kind == ETutorialStepKind::WalkTalk)
				{
					StartSpeech(Step->Id);
				}
			}
			else if (TutorialRules::IsFinished(Transition.To))
			{
				const TCHAR* Event = Transition.To == ETutorialStepState::Skipped ? TEXT("skipped")
					: (Transition.To == ETutorialStepState::TimedOut ? TEXT("timed_out") : (Transition.bSilent ? TEXT("completed_silently") : TEXT("completed")));
				WriteTelemetry(Event, Step->Id, Step->Beat, Transition.ActiveSeconds);
				if (Transition.From == ETutorialStepState::Active && Transition.To == ETutorialStepState::Completed)
				{
					if (Step->Kind == ETutorialStepKind::HintCard)
					{
						FlashCard = Step->Id;
						FlashCardLeft = FMath::Min(0.8f, Machine.CardBreatherSeconds);
					}
					else if (Step->Kind == ETutorialStepKind::Objective)
					{
						FlashObjectives.Add(Step->Id, 1.2f);
					}
				}
				if (SpeechStep == Step->Id)
				{
					SpeechStep = NAME_None;
					SpeechLines.Reset();
				}
			}
		}
	}
	RefreshMarkers();
}

void UTutorialSubsystem::HandleStepShown(const FTutorialStep& Step)
{
	if (!Step.Focus.IsNone())
	{
		AOperativeCharacter* Focus = FindColdestMember();
		FocusOperatives.Add(Step.Id, Focus);
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
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	for (const FTutorialAction& Action : Step.Actions)
	{
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
		const bool bAlive = Member && (!Member->HealthComponent || Member->HealthComponent->IsAlive());
		if (bAlive && (!Coldest || Member->ColdLevel > Coldest->ColdLevel))
		{
			Coldest = Member;
		}
	}
	return Coldest;
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
	return Text.Replace(TEXT("{focus}"), Focus ? *Focus->DisplayName.ToString() : TEXT("the coldest operative"));
}

float UTutorialSubsystem::GetConditionStat(FName Stat) const
{
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	if (!Squad)
	{
		return 0.f;
	}
	if (Stat == FName(TEXT("leader_cold")))
	{
		const AOperativeCharacter* Leader = Squad->GetLeader();
		return Leader ? Leader->ColdLevel : 0.f;
	}
	float ColdMax = 0.f;
	float ColdMin = 0.f;
	int32 HeatPacks = 0;
	int32 Alive = 0;
	for (const AOperativeCharacter* Member : Squad->GetMembers())
	{
		if (!Member || (Member->HealthComponent && !Member->HealthComponent->IsAlive()))
		{
			continue;
		}
		ColdMax = Alive == 0 ? Member->ColdLevel : FMath::Max(ColdMax, Member->ColdLevel);
		ColdMin = Alive == 0 ? Member->ColdLevel : FMath::Min(ColdMin, Member->ColdLevel);
		HeatPacks += Member->HeatPacksCount;
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
	return 0.f;
}

bool UTutorialSubsystem::IsCommandLocked(FName CommandId) const
{
	return IsTutorialActive() && Machine.IsCommandLocked(CommandId);
}

bool UTutorialSubsystem::CheckCommandLocked(const UObject* WorldContext, FName CommandId)
{
	UWorld* World = GEngine && WorldContext ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	UTutorialSubsystem* Tutorial = World ? World->GetSubsystem<UTutorialSubsystem>() : nullptr;
	if (!Tutorial || !Tutorial->IsCommandLocked(CommandId))
	{
		return false;
	}
	++Tutorial->LockedRefusals;
	UE_LOG(LogCodexTactics, Display, TEXT("Tutorial: command %s is locked (beat %s) - degraded"), *CommandId.ToString(),
		*Tutorial->GetCurrentBeat().ToString());
	Tutorial->WriteTelemetry(TEXT("command_locked"), NAME_None, Tutorial->GetCurrentBeat(), 0.f);
	GameplayEvents::Raise(World, GameplayEvents::TutorialLocked);
	GameplayEvents::Raise(World, TutorialRules::MakeLockedEventName(CommandId));
	return true;
}

FName UTutorialSubsystem::FilterCommand(const UObject* WorldContext, FName CommandId)
{
	if (!CheckCommandLocked(WorldContext, CommandId))
	{
		return CommandId;
	}
	UWorld* World = GEngine && WorldContext ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UTutorialSubsystem* Tutorial = World ? World->GetSubsystem<UTutorialSubsystem>() : nullptr;
	return Tutorial ? TutorialRules::ResolveLockedCommand(Tutorial->GetMachine().GetLocks(), CommandId) : CommandId;
}

void UTutorialSubsystem::RefreshMarkers()
{
	const bool bActive = IsTutorialActive();
	const FName Beat = Machine.GetCurrentBeat();
	Markers.RemoveAll([](const TWeakObjectPtr<ATutorialMarkerActor>& Marker) { return !Marker.IsValid(); });
	for (const TWeakObjectPtr<ATutorialMarkerActor>& Weak : Markers)
	{
		ATutorialMarkerActor* Marker = Weak.Get();
		bool bShow = false;
		if (bActive)
		{
			bShow = !Marker->StepId.IsNone() ? Machine.GetState(Marker->StepId) == ETutorialStepState::Active
				: (!Marker->BeatId.IsNone() && Marker->BeatId == Beat);
		}
		Marker->SetMarkerShown(bShow);
	}
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
		View.Title = FText::FromString(ResolveFocusToken(Text.Title, StepId));
		View.Body = FText::FromString(PlayerCommands::ResolveKeyTokens(ResolveFocusToken(Text.Text, StepId)));
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
	return FText::FromString(PlayerCommands::ResolveKeyTokens(Text.Text.IsEmpty() ? Text.Title : Text.Text));
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
	const FName Done = SpeechStep;
	SpeechStep = NAME_None;
	SpeechLines.Reset();
	const FTutorialStep* Step = Machine.GetData().FindStep(Done);
	// Lines played out: the step is done unless it waits for something else (then it stays until that / its timeout).
	if (Step && !Step->Complete.HasCondition())
	{
		Machine.CompleteStep(Done);
	}
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
	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(LOCTEXT("TrainingSpeaker", "TRAINING"), FText::Format(LOCTEXT("LessonDowned",
			"{0} is down - in a real fight that would have been fatal. Retry from the checkpoint or carry on."), Operative->DisplayName));
	}
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

bool UTutorialSubsystem::SaveCheckpoint()
{
	USaveGameSubsystem* Saves = GetWorld() ? GetWorld()->GetSubsystem<USaveGameSubsystem>() : nullptr;
	if (!Saves || !Saves->bAutosaveEnabled)
	{
		return false;
	}
	FText Reason;
	if (!Saves->CanSaveNow(&Reason))
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Tutorial: no checkpoint at beat %s (%s)"), *Machine.GetCurrentBeat().ToString(), *Reason.ToString());
		return false;
	}
	const bool bSaved = Saves->SaveGame(CheckpointSlot, FString::Printf(TEXT("Checkpoint: %s"), *Machine.GetCurrentBeat().ToString()));
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
	const bool bLoaded = Saves->LoadGame(CheckpointSlot);
	LessonDownCount = 0;
	return bLoaded;
}

// --- Save -----------------------------------------------------------------------------------------------------------

TSharedPtr<FJsonObject> UTutorialSubsystem::CaptureSaveState() const
{
	if (!Machine.IsLoaded() && DeferredProgress.IsSet())
	{
		return TutorialRules::SaveProgress(DeferredProgress.GetValue(), IsTutorialEnabled());
	}
	const FTutorialProgress Progress = Machine.ExportProgress();
	if (!Machine.IsLoaded() && Progress.IsEmpty())
	{
		return nullptr;
	}
	return TutorialRules::SaveProgress(Progress, IsTutorialEnabled());
}

void UTutorialSubsystem::RestoreSaveState(const FJsonObject& Object)
{
	FTutorialProgress Progress;
	if (!TutorialRules::LoadProgress(Object, Progress))
	{
		return;
	}
	if (!Machine.IsLoaded())
	{
		DeferredProgress = Progress;
		return;
	}
	SpeechStep = NAME_None;
	SpeechLines.Reset();
	FlashCard = NAME_None;
	FlashObjectives.Reset();
	bRetryPrompt = false;
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
