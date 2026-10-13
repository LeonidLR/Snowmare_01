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
	Locks.Reset();
	History.Reset();
}

FTutorialCommandUse FTutorialStateMachine::GetCommandUse(FName CommandId) const
{
	const FTutorialCommandUse* Use = History.Find(CommandId);
	return Use ? *Use : FTutorialCommandUse();
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

bool FTutorialStateMachine::AreAfterStepsFinished(int32 Index) const
{
	if (!Data.Steps.IsValidIndex(Index))
	{
		return false;
	}
	for (const FName AfterStep : Data.Steps[Index].Trigger.AfterSteps)
	{
		const int32 AfterIndex = IndexOf(AfterStep);
		if (!Runtime.IsValidIndex(AfterIndex) || !Runtime[AfterIndex].bEverFinished)
		{
			return false; // "step": [ ... ]: the last of them to finish fires the follower
		}
	}
	return true;
}

bool FTutorialStateMachine::IsArmed(int32 Index) const
{
	if (!Data.Steps.IsValidIndex(Index))
	{
		return false;
	}
	for (const FName Required : Data.Steps[Index].Trigger.Requires)
	{
		const int32 RequiredIndex = IndexOf(Required);
		if (!Runtime.IsValidIndex(RequiredIndex)
			|| (Runtime[RequiredIndex].State == ETutorialStepState::Idle && !Runtime[RequiredIndex].bEverFinished))
		{
			return false;
		}
	}
	return true;
}

bool FTutorialStateMachine::IsSubjectAllowed(int32 Index, FName SubjectId) const
{
	const TArray<FName>& DistinctFrom = Data.Steps[Index].Complete.DistinctFrom;
	if (DistinctFrom.IsEmpty())
	{
		return true;
	}
	if (SubjectId.IsNone())
	{
		return false; // "distinct_from" needs to know who did it
	}
	for (const FName Other : DistinctFrom)
	{
		const int32 OtherIndex = IndexOf(Other);
		if (Runtime.IsValidIndex(OtherIndex) && Runtime[OtherIndex].Contributors.Contains(SubjectId))
		{
			return false; // he already did the other part (stood left of the door: he is not the one who goes in)
		}
	}
	return true;
}

const TSet<FName>* FTutorialStateMachine::GetContributors(FName StepId) const
{
	const int32 Index = IndexOf(StepId);
	return Runtime.IsValidIndex(Index) ? &Runtime[Index].Contributors : nullptr;
}

float FTutorialStateMachine::ReadStat(FName Stat) const
{
	return StatProvider ? StatProvider(Stat) : 0.f;
}

bool FTutorialStateMachine::AreConditionsMet(int32 Index) const
{
	return TutorialRules::IsConditionMet(Data.Steps[Index].Conditions, [this](FName Stat) { return ReadStat(Stat); });
}

void FTutorialStateMachine::ApplyStepLocks(int32 Index)
{
	FRuntime& State = Runtime[Index];
	if (State.bLocksApplied)
	{
		return;
	}
	State.bLocksApplied = true;
	const FTutorialStep& Step = Data.Steps[Index];
	TutorialRules::ApplyLockChanges(Locks, Step.Lock, Step.Unlock);
}

TArray<bool> FTutorialStateMachine::RunningSnapshot() const
{
	TArray<bool> Out;
	Out.Reserve(Runtime.Num());
	for (const FRuntime& State : Runtime)
	{
		Out.Add(TutorialRules::IsRunning(State.State));
	}
	return Out;
}

bool FTutorialStateMachine::UnlockAll()
{
	const bool bHad = !Locks.IsEmpty();
	Locks.Reset();
	return bHad;
}

bool FTutorialStateMachine::IsCommandLocked(FName CommandId) const
{
	return TutorialRules::IsCommandLocked(Locks, CommandId);
}

void FTutorialStateMachine::PollConditionTriggers()
{
	for (int32 Index = 0; Index < Data.Steps.Num(); ++Index)
	{
		if (Data.Steps[Index].Trigger.Type == ETutorialTriggerType::Condition && CanTrigger(Index) && IsArmed(Index) && AreConditionsMet(Index))
		{
			Fire(Index);
		}
	}
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

void FTutorialStateMachine::Fire(int32 Index, bool bForce)
{
	if (!CanTrigger(Index) || (!bForce && !IsArmed(Index)))
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
	// Partial precredit: uses the player already made count towards the card (one Q press before the Q/E card = 1 of 2); hold
	// time is never seeded (it must be collected while the card is up).
	State.Progress = TutorialRules::CanPrecredit(Step) ? FMath::Min(TutorialRules::CountHistoryUses(Step, History), Step.Complete.Count) : 0;
	State.HeldSeconds = 0.f;
	State.TriggerHits = 0;
	State.CountedSubjects.Reset();
	State.Contributors.Reset();
	State.bLocksApplied = false;
	if (Step.Trigger.DelaySeconds > 0.f)
	{
		State.DelayLeft = Step.Trigger.DelaySeconds;
		SetState(Index, ETutorialStepState::Waiting);
		return;
	}
	Enqueue(Index);
}

bool FTutorialStateMachine::IsEarlierBeatCard(int32 Index) const
{
	if (!Data.Steps.IsValidIndex(Index) || Data.Steps[Index].Kind != ETutorialStepKind::HintCard)
	{
		return false;
	}
	const int32 BeatIndex = Data.FindBeatIndex(Data.Steps[Index].Beat);
	return BeatIndex != INDEX_NONE && CurrentBeatIndex != INDEX_NONE && BeatIndex < CurrentBeatIndex;
}

void FTutorialStateMachine::DropEarlierBeatCard(int32 Index)
{
	if (TutorialRules::IsPrecreditSatisfied(Data.Steps[Index], History))
	{
		FinishPrecredited(Index);
		return;
	}
	Finish(Index, ETutorialStepState::Skipped);
}

void FTutorialStateMachine::FinishPrecredited(int32 Index)
{
	const FName StepId = Data.Steps[Index].Id;
	Finish(Index, ETutorialStepState::Completed);
	for (int32 Back = Transitions.Num() - 1; Back >= 0; --Back)
	{
		if (Transitions[Back].StepId == StepId && Transitions[Back].To == ETutorialStepState::Completed)
		{
			Transitions[Back].bPrecredited = true;
			break;
		}
	}
}

void FTutorialStateMachine::Enqueue(int32 Index)
{
	SetState(Index, ETutorialStepState::Queued);
	if (Data.Steps[Index].Trigger.Type == ETutorialTriggerType::AfterStep && IsEarlierBeatCard(Index))
	{
		// The next card of a chain whose beat is over (beacon 1 reached during the camera lesson): never shown now.
		DropEarlierBeatCard(Index);
		return;
	}
	if (!AreConditionsMet(Index))
	{
		// Due but its condition does not hold (e.g. the gust when someone is already cold): skipped silently, followers go on.
		Finish(Index, ETutorialStepState::Skipped);
		return;
	}
	if (TutorialRules::IsPrecreditSatisfied(Data.Steps[Index], History))
	{
		// The player already did this (panned the camera during the HQ intro, ...): done without the card, followers go on.
		FinishPrecredited(Index);
		return;
	}
	Queue.AddUnique(Index);
	PumpQueues();
}

void FTutorialStateMachine::Activate(int32 Index)
{
	Queue.Remove(Index);
	ApplyStepLocks(Index);
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
	ApplyStepLocks(Index); // a step that never showed still applies its locks (never lost)
	SetState(Index, FinalState, /*bSilent*/ !bWasActive);
	const FName FinishedId = Step.Id;
	for (int32 Other = 0; Other < Data.Steps.Num(); ++Other)
	{
		const FTutorialTrigger& Trigger = Data.Steps[Other].Trigger;
		if (Trigger.Type == ETutorialTriggerType::AfterStep && (Trigger.Step == FinishedId || Trigger.AfterSteps.Contains(FinishedId))
			&& AreAfterStepsFinished(Other))
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
	if (TutorialRules::IsProgressMet(State.Progress, Needed, State.HeldSeconds, Data.Steps[Index].Complete.HoldSeconds))
	{
		Finish(Index, ETutorialStepState::Completed);
	}
}

void FTutorialStateMachine::HandleCommandHold(FName CommandId, float DeltaSeconds)
{
	if (CommandId.IsNone() || DeltaSeconds <= 0.f || !IsLoaded())
	{
		return;
	}
	History.FindOrAdd(CommandId).HeldSeconds += DeltaSeconds;
	for (int32 Index = 0; Index < Data.Steps.Num(); ++Index)
	{
		const FTutorialCompletion& Complete = Data.Steps[Index].Complete;
		// Only a SHOWN step collects hold time: the player must do it while the card is up (not during the breather).
		if (Complete.HoldSeconds > 0.f && Runtime[Index].State == ETutorialStepState::Active && TutorialRules::CountsCommand(Data.Steps[Index], CommandId))
		{
			Runtime[Index].HeldSeconds += DeltaSeconds;
			AddProgress(Index, 0, Complete.Count);
		}
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
		case ETutorialStepKind::Feed: break;
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
		case ETutorialStepKind::Feed:
			Activate(Index); // no slot: the runtime posts the line and completes it at once
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
	++History.FindOrAdd(CommandId).Count;
	// Only steps already running when this input came count it (a follower started by a completion must not take the same input).
	const TArray<bool> WasRunning = RunningSnapshot();
	for (int32 Index = 0; Index < Data.Steps.Num(); ++Index)
	{
		if (WasRunning[Index] && TutorialRules::IsRunning(Runtime[Index].State) && TutorialRules::CountsCommand(Data.Steps[Index], CommandId))
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

void FTutorialStateMachine::HandleEvent(FName EventName, FName SubjectId, const TArray<FName>& SubjectVolumes)
{
	if (EventName.IsNone() || !IsLoaded())
	{
		return;
	}
	// Only steps already running when this input came count it (a follower started by a completion must not take the same input).
	const TArray<bool> WasRunning = RunningSnapshot();
	for (int32 Index = 0; Index < Data.Steps.Num(); ++Index)
	{
		const FTutorialCompletion& Complete = Data.Steps[Index].Complete;
		if (WasRunning[Index] && TutorialRules::IsRunning(Runtime[Index].State) && !Complete.Event.IsEmpty() && TutorialRules::MatchesName(Complete.Event, EventName))
		{
			if (!Complete.InVolume.IsNone() && !SubjectVolumes.Contains(Complete.InVolume))
			{
				continue; // "in_volume": not where the lesson asks for it (e.g. cover entered, but not beside the door)
			}
			if (!IsSubjectAllowed(Index, SubjectId))
			{
				continue; // "distinct_from": this operative already did another part of the lesson
			}
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
			if (!SubjectId.IsNone())
			{
				Runtime[Index].Contributors.Add(SubjectId);
			}
			AddProgress(Index, 1, Complete.Count);
		}
	}
	for (int32 Index = 0; Index < Data.Steps.Num(); ++Index)
	{
		const FTutorialTrigger& Trigger = Data.Steps[Index].Trigger;
		const bool bFailure = Trigger.Type == ETutorialTriggerType::FailureRetry;
		if ((Trigger.Type != ETutorialTriggerType::Event && !bFailure) || !TutorialRules::MatchesName(Trigger.Event, EventName) || !CanTrigger(Index)
			|| !IsArmed(Index))
		{
			continue; // not armed yet ("requires"): the event is not stored
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

bool FTutorialStateMachine::CountVolumeCompletions(FName VolumeId, FName SubjectId, const TArray<bool>& WasRunning)
{
	bool bCounted = false;
	for (int32 Index = 0; Index < Data.Steps.Num(); ++Index)
	{
		if (WasRunning[Index] && TutorialRules::IsRunning(Runtime[Index].State) && Data.Steps[Index].Complete.Volume == VolumeId
			&& IsSubjectAllowed(Index, SubjectId))
		{
			if (!SubjectId.IsNone())
			{
				Runtime[Index].Contributors.Add(SubjectId);
			}
			AddProgress(Index, 1, 1);
			bCounted = true;
		}
	}
	return bCounted;
}

bool FTutorialStateMachine::HandleVolumePresence(FName VolumeId, FName SubjectId)
{
	if (!IsLoaded() || VolumeId.IsNone())
	{
		return false;
	}
	const bool bCounted = CountVolumeCompletions(VolumeId, SubjectId, RunningSnapshot());
	PumpQueues();
	return bCounted;
}

void FTutorialStateMachine::HandleVolume(FName VolumeId, FName StepId, FName BeatId, FName SubjectId)
{
	if (!IsLoaded())
	{
		return;
	}
	if (!VolumeId.IsNone())
	{
		// Completions first, before a beat this volume starts drops the earlier beat's leftover cards (the walk card that asked
		// for this very place is completed, not dropped). Only steps already running when this input came count it.
		CountVolumeCompletions(VolumeId, SubjectId, RunningSnapshot());
	}
	if (!BeatId.IsNone())
	{
		StartBeat(BeatId);
	}
	if (!VolumeId.IsNone())
	{
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
	TutorialRules::ApplyLockChanges(Locks, Data.Beats[BeatIndex].Lock, Data.Beats[BeatIndex].Unlock);
	// Leftover hint cards of the beats the squad has left (waiting, queued or shown) are dropped; their chains follow.
	for (int32 Index = 0; Index < Data.Steps.Num(); ++Index)
	{
		if (IsEarlierBeatCard(Index) && TutorialRules::IsRunning(Runtime[Index].State))
		{
			DropEarlierBeatCard(Index);
		}
	}
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
	Fire(Index, /*bForce*/ true);
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
	Fire(Index, /*bForce*/ true);
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
	PollConditionTriggers();
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

float FTutorialStateMachine::GetHeldSeconds(FName StepId) const
{
	const int32 Index = IndexOf(StepId);
	return Runtime.IsValidIndex(Index) ? Runtime[Index].HeldSeconds : 0.f;
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
	Progress.Locks = Locks;
	Progress.bHasLocks = true;
	Progress.History = History;
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
	History = Progress.History;
	for (const TPair<FName, ETutorialStepState>& Entry : Progress.Finished)
	{
		const int32 Index = IndexOf(Entry.Key);
		if (Runtime.IsValidIndex(Index) && TutorialRules::IsFinished(Entry.Value))
		{
			Runtime[Index].State = Entry.Value;
			Runtime[Index].bEverFinished = true;
			Runtime[Index].bLocksApplied = true;
		}
	}
	if (Progress.bHasLocks)
	{
		Locks = Progress.Locks;
	}
	else
	{
		// An older save without "locks": rebuild them from the beats up to the current one, then the finished steps.
		for (int32 BeatIndex = 0; BeatIndex <= CurrentBeatIndex && Data.Beats.IsValidIndex(BeatIndex); ++BeatIndex)
		{
			TutorialRules::ApplyLockChanges(Locks, Data.Beats[BeatIndex].Lock, Data.Beats[BeatIndex].Unlock);
		}
		for (int32 Index = 0; Index < Data.Steps.Num(); ++Index)
		{
			if (Runtime[Index].bEverFinished)
			{
				TutorialRules::ApplyLockChanges(Locks, Data.Steps[Index].Lock, Data.Steps[Index].Unlock);
			}
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
