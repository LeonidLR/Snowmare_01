#include "Tutorial/TutorialStateMachine.h"
#include "Core/PlayerCommands.h"
#include "Tutorial/TutorialRules.h"

void FTutorialStateMachine::Load(const FTutorialStepsData& InData)
{
	Data = InData;
	ResetProgress();
}

void FTutorialStateMachine::ResetProgress()
{
	Runtime.Reset();
	Runtime.SetNum(Data.Steps.Num());
	Queue.Reset();
	CurrentBeatIndex = INDEX_NONE;
	CardBreatherLeft = 0.f;
	RemedialCooldownLeft = 0.f;
	UsedCommands.Reset();
	Transitions.Reset();
}

int32 FTutorialStateMachine::IndexOf(FName StepId) const
{
	return Data.FindStepIndex(StepId);
}

bool FTutorialStateMachine::CanTrigger(int32 Index) const
{
	if (!Runtime.IsValidIndex(Index))
	{
		return false;
	}
	const FRuntime& State = Runtime[Index];
	if (State.State == ETutorialStepState::Idle)
	{
		return true;
	}
	return TutorialRules::IsFinished(State.State) && !Data.Steps[Index].bOnce;
}

void FTutorialStateMachine::SetState(int32 Index, ETutorialStepState NewState, bool bSilent)
{
	FRuntime& State = Runtime[Index];
	if (State.State == NewState)
	{
		return;
	}
	FTutorialTransition& Transition = Transitions.AddDefaulted_GetRef();
	Transition.Type = FTutorialTransition::EType::StepState;
	Transition.StepId = Data.Steps[Index].Id;
	Transition.BeatId = Data.Steps[Index].Beat;
	Transition.From = State.State;
	Transition.To = NewState;
	Transition.bSilent = bSilent;
	Transition.ActiveSeconds = State.State == ETutorialStepState::Active ? State.StateSeconds : 0.f;
	State.State = NewState;
	State.StateSeconds = 0.f;
}

void FTutorialStateMachine::Fire(int32 Index)
{
	if (!CanTrigger(Index))
	{
		return;
	}
	const FTutorialStep& Step = Data.Steps[Index];
	// A step of a later beat starts that beat first (the beats run in file order).
	const int32 BeatIndex = Data.FindBeatIndex(Step.Beat);
	if (BeatIndex != INDEX_NONE && BeatIndex > CurrentBeatIndex)
	{
		StartBeat(Step.Beat);
		if (!CanTrigger(Index))
		{
			return; // the beat start fired it already
		}
	}
	FRuntime& State = Runtime[Index];
	State.Progress = 0;
	State.TriggerHits = 0;
	State.CountedSubjects.Reset();
	if (Step.Trigger.DelaySeconds > 0.f)
	{
		State.DelayLeft = Step.Trigger.DelaySeconds;
		SetState(Index, ETutorialStepState::Waiting);
		return;
	}
	Enqueue(Index);
}

void FTutorialStateMachine::Enqueue(int32 Index)
{
	SetState(Index, ETutorialStepState::Queued);
	Queue.AddUnique(Index);
	PumpQueues();
}

void FTutorialStateMachine::Activate(int32 Index)
{
	Queue.Remove(Index);
	SetState(Index, ETutorialStepState::Active);
}

void FTutorialStateMachine::Finish(int32 Index, ETutorialStepState FinalState)
{
	if (!Runtime.IsValidIndex(Index) || !TutorialRules::IsRunning(Runtime[Index].State))
	{
		return;
	}
	const bool bWasActive = Runtime[Index].State == ETutorialStepState::Active;
	const FTutorialStep& Step = Data.Steps[Index];
	Queue.Remove(Index);
	if (bWasActive && Step.Kind == ETutorialStepKind::HintCard)
	{
		CardBreatherLeft = CardBreatherSeconds;
	}
	Runtime[Index].bEverFinished = true;
	SetState(Index, FinalState, /*bSilent*/ !bWasActive);
	const FName FinishedId = Step.Id;
	for (int32 Other = 0; Other < Data.Steps.Num(); ++Other)
	{
		const FTutorialTrigger& Trigger = Data.Steps[Other].Trigger;
		if (Trigger.Type == ETutorialTriggerType::AfterStep && Trigger.Step == FinishedId)
		{
			Fire(Other);
		}
	}
	PumpQueues();
}

void FTutorialStateMachine::AddProgress(int32 Index, int32 Amount, int32 Needed)
{
	FRuntime& State = Runtime[Index];
	State.Progress += Amount;
	if (State.Progress >= FMath::Max(1, Needed))
	{
		Finish(Index, ETutorialStepState::Completed);
	}
}

void FTutorialStateMachine::PumpQueues()
{
	bool bCardActive = false;
	bool bSpeechActive = false;
	int32 ActiveObjectives = 0;
	for (int32 Index = 0; Index < Runtime.Num(); ++Index)
	{
		if (Runtime[Index].State != ETutorialStepState::Active)
		{
			continue;
		}
		switch (Data.Steps[Index].Kind)
		{
		case ETutorialStepKind::HintCard: bCardActive = true; break;
		case ETutorialStepKind::Objective: ++ActiveObjectives; break;
		default: bSpeechActive = true; break;
		}
	}
	const TArray<int32> Snapshot = Queue;
	for (const int32 Index : Snapshot)
	{
		switch (Data.Steps[Index].Kind)
		{
		case ETutorialStepKind::HintCard:
			if (!bCardActive && CardBreatherLeft <= 0.f)
			{
				Activate(Index);
				bCardActive = true;
			}
			break;
		case ETutorialStepKind::Objective:
			if (ActiveObjectives < MaxObjectives)
			{
				Activate(Index);
				++ActiveObjectives;
			}
			break;
		default:
			if (!bSpeechActive)
			{
				Activate(Index);
				bSpeechActive = true;
			}
			break;
		}
	}
}

void FTutorialStateMachine::FireBeatStartSteps(FName BeatId)
{
	for (int32 Index = 0; Index < Data.Steps.Num(); ++Index)
	{
		const FTutorialStep& Step = Data.Steps[Index];
		if (Step.Trigger.Type == ETutorialTriggerType::BeatStart && Step.Beat == BeatId)
		{
			Fire(Index);
		}
	}
}

void FTutorialStateMachine::HandleCommand(FName CommandId)
{
	if (CommandId.IsNone() || !IsLoaded())
	{
		return;
	}
	for (int32 Index = 0; Index < Data.Steps.Num(); ++Index)
	{
		if (TutorialRules::IsRunning(Runtime[Index].State) && TutorialRules::CountsCommand(Data.Steps[Index], CommandId))
		{
			AddProgress(Index, 1, Data.Steps[Index].Complete.Count);
		}
	}
	bool bFirstUse = false;
	UsedCommands.Add(CommandId, &bFirstUse);
	bFirstUse = !bFirstUse;
	if (bFirstUse)
	{
		for (int32 Index = 0; Index < Data.Steps.Num(); ++Index)
		{
			const FTutorialTrigger& Trigger = Data.Steps[Index].Trigger;
			if (Trigger.Type == ETutorialTriggerType::FirstUse && PlayerCommands::Matches(Trigger.Command, CommandId))
			{
				Fire(Index);
			}
		}
	}
}

void FTutorialStateMachine::HandleEvent(FName EventName, FName SubjectId)
{
	if (EventName.IsNone() || !IsLoaded())
	{
		return;
	}
	for (int32 Index = 0; Index < Data.Steps.Num(); ++Index)
	{
		const FTutorialCompletion& Complete = Data.Steps[Index].Complete;
		if (TutorialRules::IsRunning(Runtime[Index].State) && !Complete.Event.IsEmpty() && TutorialRules::MatchesName(Complete.Event, EventName))
		{
			if (Complete.bDistinctSubjects)
			{
				bool bAlreadyCounted = true;
				if (!SubjectId.IsNone())
				{
					Runtime[Index].CountedSubjects.Add(SubjectId, &bAlreadyCounted);
				}
				if (bAlreadyCounted)
				{
					continue; // no subject, or this operative already counted
				}
			}
			AddProgress(Index, 1, Complete.Count);
		}
	}
	for (int32 Index = 0; Index < Data.Steps.Num(); ++Index)
	{
		const FTutorialTrigger& Trigger = Data.Steps[Index].Trigger;
		const bool bFailure = Trigger.Type == ETutorialTriggerType::FailureRetry;
		if ((Trigger.Type != ETutorialTriggerType::Event && !bFailure) || !TutorialRules::MatchesName(Trigger.Event, EventName) || !CanTrigger(Index))
		{
			continue;
		}
		FRuntime& State = Runtime[Index];
		if (++State.TriggerHits < Trigger.Count)
		{
			continue;
		}
		if (bFailure && RemedialCooldownLeft > 0.f)
		{
			continue; // one remedial hint per cooldown; the hits stay counted
		}
		Fire(Index);
		if (bFailure)
		{
			RemedialCooldownLeft = RemedialCooldownSeconds;
		}
	}
}

void FTutorialStateMachine::HandleVolume(FName VolumeId, FName StepId, FName BeatId)
{
	if (!IsLoaded())
	{
		return;
	}
	if (!BeatId.IsNone())
	{
		StartBeat(BeatId);
	}
	if (!VolumeId.IsNone())
	{
		for (int32 Index = 0; Index < Data.Steps.Num(); ++Index)
		{
			if (TutorialRules::IsRunning(Runtime[Index].State) && Data.Steps[Index].Complete.Volume == VolumeId)
			{
				AddProgress(Index, 1, 1);
			}
		}
		for (int32 Index = 0; Index < Data.Steps.Num(); ++Index)
		{
			const FTutorialTrigger& Trigger = Data.Steps[Index].Trigger;
			if (Trigger.Type == ETutorialTriggerType::Volume && Trigger.Volume == VolumeId)
			{
				Fire(Index);
			}
		}
	}
	if (!StepId.IsNone())
	{
		TriggerStep(StepId);
	}
}

bool FTutorialStateMachine::StartBeat(FName BeatId, bool bAllowBackwards)
{
	const int32 BeatIndex = Data.FindBeatIndex(BeatId);
	if (BeatIndex == INDEX_NONE || BeatIndex == CurrentBeatIndex || (BeatIndex < CurrentBeatIndex && !bAllowBackwards))
	{
		return false;
	}
	// Objectives of the earlier beats that wait for nothing in particular are done when the story moves on.
	for (int32 Index = 0; Index < Data.Steps.Num(); ++Index)
	{
		const FTutorialStep& Step = Data.Steps[Index];
		if (Step.Kind == ETutorialStepKind::Objective && !Step.Complete.HasCondition() && Step.Beat != BeatId
			&& TutorialRules::IsRunning(Runtime[Index].State))
		{
			Finish(Index, ETutorialStepState::Completed);
		}
	}
	CurrentBeatIndex = BeatIndex;
	FTutorialTransition& Transition = Transitions.AddDefaulted_GetRef();
	Transition.Type = FTutorialTransition::EType::BeatStarted;
	Transition.BeatId = BeatId;
	FireBeatStartSteps(BeatId);
	return true;
}

bool FTutorialStateMachine::TriggerStep(FName StepId)
{
	const int32 Index = IndexOf(StepId);
	if (!CanTrigger(Index))
	{
		return false;
	}
	Fire(Index);
	return true;
}

bool FTutorialStateMachine::SkipStep(FName StepId)
{
	const int32 Index = IndexOf(StepId);
	if (!Runtime.IsValidIndex(Index) || !TutorialRules::IsRunning(Runtime[Index].State) || !Data.Steps[Index].bSkippable)
	{
		return false;
	}
	Finish(Index, ETutorialStepState::Skipped);
	return true;
}

bool FTutorialStateMachine::CompleteStep(FName StepId)
{
	const int32 Index = IndexOf(StepId);
	if (!Runtime.IsValidIndex(Index) || !TutorialRules::IsRunning(Runtime[Index].State))
	{
		return false;
	}
	Finish(Index, ETutorialStepState::Completed);
	return true;
}

bool FTutorialStateMachine::ReplayStep(FName StepId)
{
	const int32 Index = IndexOf(StepId);
	if (!Runtime.IsValidIndex(Index))
	{
		return false;
	}
	if (TutorialRules::IsRunning(Runtime[Index].State))
	{
		return false;
	}
	Runtime[Index] = FRuntime();
	Fire(Index);
	return true;
}

void FTutorialStateMachine::Tick(float DeltaSeconds)
{
	if (!IsLoaded())
	{
		return;
	}
	CardBreatherLeft = FMath::Max(0.f, CardBreatherLeft - DeltaSeconds);
	RemedialCooldownLeft = FMath::Max(0.f, RemedialCooldownLeft - DeltaSeconds);
	for (int32 Index = 0; Index < Runtime.Num(); ++Index)
	{
		FRuntime& State = Runtime[Index];
		State.StateSeconds += DeltaSeconds;
		if (State.State == ETutorialStepState::Waiting)
		{
			State.DelayLeft -= DeltaSeconds;
			if (State.DelayLeft <= 0.f)
			{
				Enqueue(Index);
			}
		}
		else if (State.State == ETutorialStepState::Active)
		{
			const float Timeout = Data.Steps[Index].Complete.TimeoutSeconds;
			if (Timeout > 0.f && State.StateSeconds >= Timeout)
			{
				Finish(Index, ETutorialStepState::TimedOut);
			}
		}
	}
	PumpQueues();
}

ETutorialStepState FTutorialStateMachine::GetState(FName StepId) const
{
	const int32 Index = IndexOf(StepId);
	return Runtime.IsValidIndex(Index) ? Runtime[Index].State : ETutorialStepState::Idle;
}

int32 FTutorialStateMachine::GetProgress(FName StepId) const
{
	const int32 Index = IndexOf(StepId);
	return Runtime.IsValidIndex(Index) ? Runtime[Index].Progress : 0;
}

float FTutorialStateMachine::GetStateSeconds(FName StepId) const
{
	const int32 Index = IndexOf(StepId);
	return Runtime.IsValidIndex(Index) ? Runtime[Index].StateSeconds : 0.f;
}

FName FTutorialStateMachine::GetActiveCard() const
{
	for (int32 Index = 0; Index < Runtime.Num(); ++Index)
	{
		if (Runtime[Index].State == ETutorialStepState::Active && Data.Steps[Index].Kind == ETutorialStepKind::HintCard)
		{
			return Data.Steps[Index].Id;
		}
	}
	return NAME_None;
}

TArray<FName> FTutorialStateMachine::GetActiveObjectives() const
{
	TArray<FName> Out;
	for (int32 Index = 0; Index < Runtime.Num(); ++Index)
	{
		if (Runtime[Index].State == ETutorialStepState::Active && Data.Steps[Index].Kind == ETutorialStepKind::Objective)
		{
			Out.Add(Data.Steps[Index].Id);
		}
	}
	return Out;
}

FName FTutorialStateMachine::GetActiveSpeech() const
{
	for (int32 Index = 0; Index < Runtime.Num(); ++Index)
	{
		const ETutorialStepKind Kind = Data.Steps[Index].Kind;
		if (Runtime[Index].State == ETutorialStepState::Active && (Kind == ETutorialStepKind::Radio || Kind == ETutorialStepKind::WalkTalk))
		{
			return Data.Steps[Index].Id;
		}
	}
	return NAME_None;
}

FName FTutorialStateMachine::GetCurrentBeat() const
{
	return Data.Beats.IsValidIndex(CurrentBeatIndex) ? Data.Beats[CurrentBeatIndex].Id : NAME_None;
}

const FTutorialBeat* FTutorialStateMachine::GetCurrentBeatData() const
{
	return Data.Beats.IsValidIndex(CurrentBeatIndex) ? &Data.Beats[CurrentBeatIndex] : nullptr;
}

bool FTutorialStateMachine::IsLessonBeat() const
{
	const FTutorialBeat* Beat = GetCurrentBeatData();
	return Beat && Beat->bLesson;
}

void FTutorialStateMachine::GetCardProgress(FName StepId, int32& OutIndex, int32& OutCount) const
{
	OutIndex = 0;
	OutCount = 0;
	const FTutorialStep* Step = Data.FindStep(StepId);
	if (!Step)
	{
		return;
	}
	for (const FTutorialStep& Other : Data.Steps)
	{
		if (Other.Kind == ETutorialStepKind::HintCard && Other.Beat == Step->Beat)
		{
			++OutCount;
			if (Other.Id == StepId)
			{
				OutIndex = OutCount;
			}
		}
	}
}

FTutorialProgress FTutorialStateMachine::ExportProgress() const
{
	FTutorialProgress Progress;
	Progress.CurrentBeat = GetCurrentBeat();
	for (int32 Index = 0; Index < Runtime.Num(); ++Index)
	{
		const FRuntime& State = Runtime[Index];
		if (TutorialRules::IsFinished(State.State))
		{
			Progress.Finished.Add(Data.Steps[Index].Id, State.State);
		}
		else if (TutorialRules::IsRunning(State.State))
		{
			Progress.Pending.Add(Data.Steps[Index].Id);
		}
	}
	return Progress;
}

void FTutorialStateMachine::ImportProgress(const FTutorialProgress& Progress)
{
	ResetProgress();
	CurrentBeatIndex = Data.FindBeatIndex(Progress.CurrentBeat);
	for (const TPair<FName, ETutorialStepState>& Entry : Progress.Finished)
	{
		const int32 Index = IndexOf(Entry.Key);
		if (Runtime.IsValidIndex(Index) && TutorialRules::IsFinished(Entry.Value))
		{
			Runtime[Index].State = Entry.Value;
			Runtime[Index].bEverFinished = true;
		}
	}
	for (const FName StepId : Progress.Pending)
	{
		const int32 Index = IndexOf(StepId);
		if (Runtime.IsValidIndex(Index) && Runtime[Index].State == ETutorialStepState::Idle)
		{
			Enqueue(Index);
		}
	}
}

TArray<FTutorialTransition> FTutorialStateMachine::ConsumeTransitions()
{
	TArray<FTutorialTransition> Out = MoveTemp(Transitions);
	Transitions.Reset();
	return Out;
}
