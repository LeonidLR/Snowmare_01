#include "Tutorial/TutorialStateMachine.h"
#include "Core/PlayerCommands.h"
#include "Tutorial/TutorialRules.h"

void FTutorialStateMachine::Load(const FTutorialStepsData& InData)
{
	Data = InData;
	RebuildRoute();
	ResetProgress();
}

// --- Rev 5f: the route (monotonic progress by location) -------------------------------------------------------------

namespace TutorialRoute
{
	// Keys: the start of a beat = 3 * (its first step index); a volume TRIGGER of step i = 3 * i + 1; a volume COMPLETION of
	// step i = 3 * i + 2 (the trigger volume of a step lies before the volume it completes in).
	int32 BeatStartKey(int32 FirstStepIndex) { return 3 * FirstStepIndex; }
	int32 TriggerKey(int32 StepIndex) { return 3 * StepIndex + 1; }
	int32 CompletionKey(int32 StepIndex) { return 3 * StepIndex + 2; }
}

void FTutorialStateMachine::RebuildRoute()
{
	StepRoute.Reset();
	auto Offer = [this](FName VolumeId, int32 Key, int32 BeatIndex)
	{
		if (VolumeId.IsNone() || Key == INDEX_NONE)
		{
			return;
		}
		FRoutePoint& Point = StepRoute.FindOrAdd(VolumeId);
		if (!Point.IsValid() || Key < Point.Key)
		{
			Point.Key = Key;
			Point.BeatIndex = BeatIndex;
		}
	};
	for (int32 Index = 0; Index < Data.Steps.Num(); ++Index)
	{
		const FTutorialStep& Step = Data.Steps[Index];
		const int32 BeatIndex = Data.FindBeatIndex(Step.Beat);
		if (Step.Trigger.Type == ETutorialTriggerType::Volume)
		{
			Offer(Step.Trigger.Volume, TutorialRoute::TriggerKey(Index), BeatIndex);
		}
		// A completion of a step that runs from its beat start ("go to the camp" shown when the beat begins) names the place the
		// beat leads to: the END of that beat, not its start.
		int32 CompletionIndex = Index;
		if (Step.Trigger.Type == ETutorialTriggerType::BeatStart)
		{
			for (int32 Other = Index + 1; Other < Data.Steps.Num(); ++Other)
			{
				CompletionIndex = Data.Steps[Other].Beat == Step.Beat ? Other : CompletionIndex;
			}
		}
		Offer(Step.Complete.Volume, TutorialRoute::CompletionKey(CompletionIndex), BeatIndex);
		Offer(Step.Complete.InVolume, TutorialRoute::CompletionKey(CompletionIndex), BeatIndex);
	}
}

void FTutorialStateMachine::RegisterVolumeBeat(FName VolumeId, FName BeatId)
{
	if (!VolumeId.IsNone() && !BeatId.IsNone())
	{
		VolumeBeats.Add(VolumeId, BeatId);
	}
}

FTutorialStateMachine::FRoutePoint FTutorialStateMachine::GetBeatStartPoint(int32 BeatIndex) const
{
	FRoutePoint Point;
	if (!Data.Beats.IsValidIndex(BeatIndex))
	{
		return Point;
	}
	Point.BeatIndex = BeatIndex;
	Point.Key = TutorialRoute::BeatStartKey(Data.Steps.Num());
	for (int32 Index = 0; Index < Data.Steps.Num(); ++Index)
	{
		if (Data.FindBeatIndex(Data.Steps[Index].Beat) >= BeatIndex)
		{
			Point.Key = TutorialRoute::BeatStartKey(Index);
			break;
		}
	}
	return Point;
}

FTutorialStateMachine::FRoutePoint FTutorialStateMachine::GetVolumeRoutePoint(FName VolumeId) const
{
	FRoutePoint Point;
	if (const FRoutePoint* FromSteps = StepRoute.Find(VolumeId))
	{
		Point = *FromSteps;
	}
	if (const FName* Beat = VolumeBeats.Find(VolumeId))
	{
		const FRoutePoint BeatStart = GetBeatStartPoint(Data.FindBeatIndex(*Beat));
		if (BeatStart.IsValid() && (!Point.IsValid() || BeatStart.Key > Point.Key || (BeatStart.Key == Point.Key && BeatStart.BeatIndex > Point.BeatIndex)))
		{
			Point = BeatStart;
		}
	}
	return Point;
}

bool FTutorialStateMachine::IsPassableVolumeStep(int32 Index) const
{
	const FTutorialStep& Step = Data.Steps[Index];
	const FRuntime& State = Runtime[Index];
	return State.State == ETutorialStepState::Idle && !State.bEverFinished && Step.bOnce && Step.Trigger.Type == ETutorialTriggerType::Volume;
}

void FTutorialStateMachine::FinishFlagged(int32 Index, ETutorialStepState FinalState, bool FTutorialTransition::* Flag)
{
	const FName StepId = Data.Steps[Index].Id;
	const int32 Mark = Transitions.Num();
	Finish(Index, FinalState);
	// The step's own finishing transition is the first one for it after Mark (followers it fires come later).
	for (int32 Back = Mark; Back < Transitions.Num(); ++Back)
	{
		if (Transitions[Back].StepId == StepId && Transitions[Back].To == FinalState)
		{
			Transitions[Back].*Flag = true;
			break;
		}
	}
}

void FTutorialStateMachine::PassStep(int32 Index)
{
	if (!Runtime.IsValidIndex(Index) || Runtime[Index].State != ETutorialStepState::Idle)
	{
		return;
	}
	// Straight to Queued without a transition: Finish reports one silent Idle-less change Queued -> Skipped.
	Runtime[Index].State = ETutorialStepState::Queued;
	Runtime[Index].StateSeconds = 0.f;
	FinishFlagged(Index, ETutorialStepState::Skipped, &FTutorialTransition::bPassed);
}

void FTutorialStateMachine::PassRouteBefore(const FRoutePoint& Point)
{
	if (!Point.IsValid())
	{
		return;
	}
	for (int32 Index = 0; Index < Data.Steps.Num(); ++Index)
	{
		const FTutorialStep& Step = Data.Steps[Index];
		if (Data.FindBeatIndex(Step.Beat) > Point.BeatIndex)
		{
			continue; // a later beat's step is never "behind"
		}
		if (IsPassableVolumeStep(Index))
		{
			const FRoutePoint Trigger = GetVolumeRoutePoint(Step.Trigger.Volume);
			if (Trigger.IsValid() && Trigger.Key < Point.Key)
			{
				// The squad walked past that place without setting it off (a skipped beacon): the step never starts, but its
				// unlocks apply and whatever follows it goes on.
				PassStep(Index);
			}
		}
		else if (TutorialRules::IsRunning(Runtime[Index].State))
		{
			// "Go to X" and the squad is already past X: an objective is done, a card is dropped.
			FRoutePoint Behind = Step.Complete.Volume.IsNone() ? FRoutePoint() : GetVolumeRoutePoint(Step.Complete.Volume);
			if (!Behind.IsValid() && (Step.Kind == ETutorialStepKind::Objective || !Step.Guide.IsNone()))
			{
				// 2026-10-20 (game designer: the bridge objective called the squad back from the hut): an objective - or a step with a
				// "guide" - tied to a place the squad has left behind is over too (the bridge crossing objective once the squad reached
				// the hut); its marker never shows again.
				Behind = GetStepPlaceAt(Index);
			}
			if (Behind.IsValid() && Behind.Key < Point.Key)
			{
				FinishFlagged(Index, Step.Kind == ETutorialStepKind::Objective ? ETutorialStepState::Completed : ETutorialStepState::Skipped,
					&FTutorialTransition::bPassed);
			}
		}
	}
}

// --- 2026-10-20: the current navigation target ("guide") and "beyond" ----------------------------------------------

FTutorialStateMachine::FRoutePoint FTutorialStateMachine::GetStepPlaceAt(int32 Index, FName* OutVolume) const
{
	if (!Data.Steps.IsValidIndex(Index))
	{
		return FRoutePoint();
	}
	const FTutorialStep& Step = Data.Steps[Index];
	const FName GuideVolume = !Step.Guide.IsNone() && Data.FindStepIndex(Step.Guide) == INDEX_NONE && !Step.Guide.ToString().StartsWith(TEXT("@"))
		? Step.Guide : NAME_None;
	for (const FName Volume : { GuideVolume, Step.Complete.Volume, Step.Complete.InVolume, Step.Trigger.Volume })
	{
		const FRoutePoint Point = Volume.IsNone() ? FRoutePoint() : GetVolumeRoutePoint(Volume);
		if (Point.IsValid())
		{
			if (OutVolume)
			{
				*OutVolume = Volume;
			}
			return Point;
		}
	}
	return FRoutePoint();
}

FTutorialStateMachine::FRoutePoint FTutorialStateMachine::GetStepPlace(FName StepId) const
{
	return GetStepPlaceAt(IndexOf(StepId));
}

int32 FTutorialStateMachine::GetRouteProgressKey() const
{
	int32 Key = RouteProgressKey;
	if (!RouteProgressVolume.IsNone())
	{
		Key = FMath::Max(Key, GetVolumeRoutePoint(RouteProgressVolume).Key); // after a load only the volume is known
	}
	if (Data.Beats.IsValidIndex(CurrentBeatIndex))
	{
		Key = FMath::Max(Key, GetBeatStartPoint(CurrentBeatIndex).Key);
	}
	return Key;
}

void FTutorialStateMachine::NoteRouteProgress(const FRoutePoint& Point, FName VolumeId)
{
	// The places reached so far (not the beat start: a beat volume lies AT its beat's start and must still become the furthest place).
	const int32 Reached = FMath::Max(RouteProgressKey, RouteProgressVolume.IsNone() ? INDEX_NONE : GetVolumeRoutePoint(RouteProgressVolume).Key);
	if (!Point.IsValid() || Point.Key < Reached || (Point.Key == Reached && VolumeId.IsNone()))
	{
		return; // progress never goes back
	}
	RouteProgressKey = Point.Key;
	if (!VolumeId.IsNone())
	{
		RouteProgressVolume = VolumeId;
	}
}

bool FTutorialStateMachine::HasVolume(FName VolumeId) const
{
	return !VolumeId.IsNone() && (!VolumeProvider || VolumeProvider(VolumeId));
}

bool FTutorialStateMachine::IsVolumeStillUsed(FName VolumeId) const
{
	if (const FName* Beat = VolumeBeats.Find(VolumeId); Beat && Data.FindBeatIndex(*Beat) > CurrentBeatIndex)
	{
		return true; // it starts a beat still ahead
	}
	for (int32 Index = 0; Index < Data.Steps.Num(); ++Index)
	{
		const FTutorialStep& Step = Data.Steps[Index];
		const bool bUses = Step.Trigger.Volume == VolumeId || Step.Complete.Volume == VolumeId || Step.Complete.InVolume == VolumeId
			|| Step.Guide == VolumeId;
		const FRuntime& State = Runtime[Index];
		if (bUses && (TutorialRules::IsRunning(State.State) || (State.State == ETutorialStepState::Idle && !State.bEverFinished)))
		{
			return true;
		}
	}
	return false;
}

bool FTutorialStateMachine::GetStepGuide(int32 Index, bool bInHand, int32 Progress, FTutorialGuide& Out) const
{
	const FTutorialStep& Step = Data.Steps[Index];
	if (Step.bNoGuide)
	{
		return false;
	}
	if (!bInHand)
	{
		// A place the squad has left behind never calls it back (the card in hand is the lesson of the moment: always shown).
		const FRoutePoint Place = GetStepPlaceAt(Index);
		if (Place.IsValid() && Progress != INDEX_NONE && Place.Key < Progress)
		{
			return false;
		}
	}
	if (!Step.Guide.IsNone())
	{
		if (Step.Guide.ToString().StartsWith(TEXT("@")))
		{
			if (SpotProvider && SpotProvider(Step.Guide, Step.Id))
			{
				Out = { ETutorialGuideKind::Spot, Step.Guide, Step.Id }; // a spot the runtime computes (the ambusher's wire spot)
				return true;
			}
		}
		else if (Data.FindStepIndex(Step.Guide) != INDEX_NONE)
		{
			if (StepMarkerProvider && StepMarkerProvider(Step.Guide))
			{
				Out = { ETutorialGuideKind::StepMarkers, Step.Guide, Step.Id };
				return true;
			}
		}
		else if (HasVolume(Step.Guide))
		{
			const FRoutePoint Point = GetVolumeRoutePoint(Step.Guide);
			if (bInHand || !Point.IsValid() || Progress == INDEX_NONE || Point.Key >= Progress)
			{
				Out = { ETutorialGuideKind::Volume, Step.Guide, Step.Id };
				return true;
			}
		}
		// An explicit guide the level cannot show: the automatic choice below.
	}
	if (StepMarkerProvider && StepMarkerProvider(Step.Id))
	{
		Out = { ETutorialGuideKind::StepMarkers, Step.Id, Step.Id };
		return true;
	}
	if (bInHand)
	{
		for (const FName Volume : { Step.Complete.Volume, Step.Complete.InVolume })
		{
			if (HasVolume(Volume))
			{
				Out = { ETutorialGuideKind::Volume, Volume, Step.Id };
				return true;
			}
		}
	}
	return false;
}

FTutorialGuide FTutorialStateMachine::GetGuide() const
{
	FTutorialGuide Out;
	if (!IsLoaded() || Runtime.Num() != Data.Steps.Num())
	{
		return Out;
	}
	const int32 Progress = GetRouteProgressKey();
	// 1. The lesson in hand: the shown hint card.
	for (int32 Index = 0; Index < Data.Steps.Num(); ++Index)
	{
		if (Runtime[Index].State == ETutorialStepState::Active && Data.Steps[Index].Kind == ETutorialStepKind::HintCard)
		{
			if (GetStepGuide(Index, /*bInHand*/ true, Progress, Out))
			{
				return Out;
			}
			break;
		}
	}
	// 2. The shown objectives / talks that name a spot (markers, a "guide") not behind the squad - the nearest place first.
	int32 BestKey = MAX_int32;
	for (int32 Index = 0; Index < Data.Steps.Num(); ++Index)
	{
		const ETutorialStepKind Kind = Data.Steps[Index].Kind;
		if (Runtime[Index].State != ETutorialStepState::Active || Kind == ETutorialStepKind::HintCard || Kind == ETutorialStepKind::Feed)
		{
			continue;
		}
		FTutorialGuide Candidate;
		if (!GetStepGuide(Index, /*bInHand*/ false, Progress, Candidate))
		{
			continue;
		}
		const FRoutePoint Place = GetStepPlaceAt(Index);
		const int32 Key = Place.IsValid() ? Place.Key : MAX_int32 - 1; // no place: after the placed ones, in file order
		if (!Out.IsSet() || Key < BestKey)
		{
			Out = Candidate;
			BestKey = Key;
		}
	}
	if (Out.IsSet())
	{
		return Out;
	}
	// 3. The route: the next place ahead that the level has and the data still uses (the next beacon, the next beat's start).
	FName Next;
	int32 NextKey = MAX_int32;
	auto Consider = [this, Progress, &Next, &NextKey](FName VolumeId)
	{
		const FRoutePoint Point = GetVolumeRoutePoint(VolumeId);
		if (!Point.IsValid() || Point.Key <= Progress || Point.Key > NextKey || (Point.Key == NextKey && !VolumeId.LexicalLess(Next)))
		{
			return;
		}
		if (HasVolume(VolumeId) && IsVolumeStillUsed(VolumeId))
		{
			Next = VolumeId;
			NextKey = Point.Key;
		}
	};
	for (const TPair<FName, FRoutePoint>& Entry : StepRoute)
	{
		Consider(Entry.Key);
	}
	for (const TPair<FName, FName>& Entry : VolumeBeats)
	{
		Consider(Entry.Key);
	}
	if (!Next.IsNone())
	{
		Out = { ETutorialGuideKind::Volume, Next, NAME_None };
		return Out;
	}
	// 4. Nothing further in the data: the place reached last (hold there).
	if (HasVolume(RouteProgressVolume))
	{
		Out = { ETutorialGuideKind::Volume, RouteProgressVolume, NAME_None };
	}
	return Out;
}

void FTutorialStateMachine::PollBeyond()
{
	if (IsConfined())
	{
		return; // rev 5i: the route does not move while the squad is held
	}
	for (int32 Index = 0; Index < Data.Steps.Num(); ++Index)
	{
		const FTutorialStep& Step = Data.Steps[Index];
		if (Step.Beyond.IsEmpty() || TutorialRules::IsFinished(Runtime[Index].State)
			|| (Runtime[Index].State == ETutorialStepState::Idle && Runtime[Index].bEverFinished))
		{
			continue;
		}
		const int32 BeatIndex = Data.FindBeatIndex(Step.Beat);
		if (BeatIndex != INDEX_NONE && BeatIndex > CurrentBeatIndex)
		{
			continue; // a later beat's place: the squad is not there yet
		}
		if (!TutorialRules::IsConditionMet(Step.Beyond, [this](FName Stat) { return ReadStat(Stat); }))
		{
			continue;
		}
		FName PlaceVolume;
		const FRoutePoint Place = GetStepPlaceAt(Index, &PlaceVolume);
		if (Runtime[Index].State == ETutorialStepState::Idle)
		{
			PassStep(Index); // never started: skipped (its unlocks apply, its followers fire), never shown
		}
		else
		{
			FinishFlagged(Index, Step.Kind == ETutorialStepKind::Objective ? ETutorialStepState::Completed : ETutorialStepState::Skipped,
				&FTutorialTransition::bPassed);
		}
		if (Place.IsValid())
		{
			// The squad is beyond that place: the route moves past it (what waits there - the bridge lines - is passed too).
			NoteRouteProgress(Place, PlaceVolume);
			FRoutePoint Past = Place;
			Past.Key += 1;
			PassRouteBefore(Past);
		}
	}
}

void FTutorialStateMachine::ApplyArrival(FName VolumeId, FName BeatId)
{
	RegisterVolumeBeat(VolumeId, BeatId);
	const FRoutePoint Point = VolumeId.IsNone() ? GetBeatStartPoint(Data.FindBeatIndex(BeatId)) : GetVolumeRoutePoint(VolumeId);
	if (!Point.IsValid())
	{
		return;
	}
	if (Data.Beats.IsValidIndex(Point.BeatIndex) && Point.BeatIndex > CurrentBeatIndex)
	{
		StartBeat(Data.Beats[Point.BeatIndex].Id); // a later beat's place reached from any earlier beat
	}
	NoteRouteProgress(Point, VolumeId); // 2026-10-20: the current navigation target is the next place beyond this one
	PassRouteBefore(Point);
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
	Confines.Reset();
	++ConfineSerial;
	PlaceLocks.Reset();
	++PlaceLockSerial;
	RouteProgressKey = INDEX_NONE;
	RouteProgressVolume = NAME_None;
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
	// 2026-10-22: "step_shown.<id>" is the machine's own: 1 once that step was on screen (a silently skipped one never was).
	if (const FName Shown = TutorialRules::ParseStepShownStat(Stat); !Shown.IsNone())
	{
		const int32 ShownIndex = IndexOf(Shown);
		return Runtime.IsValidIndex(ShownIndex) && Runtime[ShownIndex].bEverShown ? 1.f : 0.f;
	}
	return StatProvider ? StatProvider(Stat) : 0.f;
}

bool FTutorialStateMachine::AreConditionsMet(int32 Index) const
{
	return TutorialRules::IsConditionMet(Data.Steps[Index].Conditions, [this](FName Stat) { return ReadStat(Stat); });
}

bool FTutorialStateMachine::IsCompletionConditionMet(int32 Index) const
{
	const TArray<FTutorialCondition>& Clauses = Data.Steps[Index].Complete.Conditions;
	return !Clauses.IsEmpty() && TutorialRules::IsConditionMet(Clauses, [this](FName Stat) { return ReadStat(Stat); });
}

void FTutorialStateMachine::PollConditionCompletions()
{
	for (int32 Index = 0; Index < Data.Steps.Num(); ++Index)
	{
		const ETutorialStepState State = Runtime[Index].State;
		if ((State == ETutorialStepState::Queued || State == ETutorialStepState::Active) && IsCompletionConditionMet(Index))
		{
			Finish(Index, ETutorialStepState::Completed); // queued: silently (done before the card came up)
		}
	}
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
	State.bEverShown |= NewState == ETutorialStepState::Active;
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
	if (Step.Confine.IsSet())
	{
		StartConfine(Index); // rev 5i: the squad is held from the moment the lesson is triggered (it arrived)
	}
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
	// Rev 5g (game designer 2026-10-14, "waiting for instructions" on arrival): a card that a PLACE starts (a volume / a beat start)
	// shows at once - the breather after the card the player just finished does not hold it back.
	// Forced starts (replay / dev trigger) keep the breather.
	if (!bForce && Step.Kind == ETutorialStepKind::HintCard
		&& (Step.Trigger.Type == ETutorialTriggerType::Volume || Step.Trigger.Type == ETutorialTriggerType::BeatStart))
	{
		CardBreatherLeft = 0.f;
	}
	Enqueue(Index);
}

bool FTutorialStateMachine::IsEarlierBeatStep(int32 Index) const
{
	if (!Data.Steps.IsValidIndex(Index))
	{
		return false;
	}
	const int32 BeatIndex = Data.FindBeatIndex(Data.Steps[Index].Beat);
	return BeatIndex != INDEX_NONE && CurrentBeatIndex != INDEX_NONE && BeatIndex < CurrentBeatIndex;
}

bool FTutorialStateMachine::IsEarlierBeatCard(int32 Index) const
{
	return IsEarlierBeatStep(Index) && Data.Steps[Index].Kind == ETutorialStepKind::HintCard;
}

void FTutorialStateMachine::DropEarlierBeatStep(int32 Index)
{
	if (Data.Steps[Index].Kind == ETutorialStepKind::HintCard)
	{
		DropEarlierBeatCard(Index);
		return;
	}
	Finish(Index, ETutorialStepState::Skipped);
}

void FTutorialStateMachine::DropEarlierBeatCard(int32 Index)
{
	TGuardValue<bool> NoBreather(bNoBreatherOnFinish, true); // rev 5g: a new beat's place - its card shows at once
	if (TutorialRules::IsPrecreditSatisfied(Data.Steps[Index], History))
	{
		FinishPrecredited(Index);
		return;
	}
	Finish(Index, ETutorialStepState::Skipped);
}

void FTutorialStateMachine::FinishPrecredited(int32 Index)
{
	FinishFlagged(Index, ETutorialStepState::Completed, &FTutorialTransition::bPrecredited);
}

bool FTutorialStateMachine::IsCancelDue(int32 Index) const
{
	for (const FName Other : Data.Steps[Index].CancelOn.Steps)
	{
		const int32 OtherIndex = IndexOf(Other);
		if (Runtime.IsValidIndex(OtherIndex) && Runtime[OtherIndex].bEverFinished && !TutorialRules::IsRunning(Runtime[OtherIndex].State))
		{
			return true;
		}
	}
	return IsCancelConditionMet(Index);
}

bool FTutorialStateMachine::IsCancelConditionMet(int32 Index) const
{
	const TArray<FTutorialCondition>& Clauses = Data.Steps[Index].CancelOn.Conditions;
	return !Clauses.IsEmpty() && TutorialRules::IsConditionMet(Clauses, [this](FName Stat) { return ReadStat(Stat); });
}

void FTutorialStateMachine::PollCancelConditions()
{
	// 2026-10-22: "cancel_on": { "condition": ... } - the moment a lesson is about is gone (its zombie is dead): dropped at once.
	for (int32 Index = 0; Index < Data.Steps.Num(); ++Index)
	{
		if (TutorialRules::IsRunning(Runtime[Index].State) && IsCancelConditionMet(Index))
		{
			CancelStep(Index);
		}
	}
}

void FTutorialStateMachine::CancelStep(int32 Index)
{
	if (!Runtime.IsValidIndex(Index) || !TutorialRules::IsRunning(Runtime[Index].State))
	{
		return;
	}
	TGuardValue<bool> NoBreather(bNoBreatherOnFinish, true); // rev 5g: what replaces it (the arrival's card) shows at once
	FinishFlagged(Index, ETutorialStepState::Skipped, &FTutorialTransition::bCancelled);
}

bool FTutorialStateMachine::PassesEnemyFilter(const FTutorialEnemyFilter& Filter, FName SubjectId) const
{
	if (!Filter.IsSet())
	{
		return true;
	}
	// 2026-10-21: only the bound enemy's event counts (each A4 part reacts to ITS zombie); no runtime to ask = never.
	return !SubjectId.IsNone() && EnemyFilterProvider && EnemyFilterProvider(Filter, SubjectId);
}

void FTutorialStateMachine::CancelMatching(FName FinishedStep, FName EventName, FName VolumeId, FName SubjectId)
{
	for (int32 Index = 0; Index < Data.Steps.Num(); ++Index)
	{
		const FTutorialCancelOn& Cancel = Data.Steps[Index].CancelOn;
		if (!Cancel.IsSet() || !TutorialRules::IsRunning(Runtime[Index].State))
		{
			continue;
		}
		const bool bMatch = (!FinishedStep.IsNone() && Cancel.Steps.Contains(FinishedStep))
			|| (!EventName.IsNone() && !Cancel.Event.IsEmpty() && TutorialRules::MatchesName(Cancel.Event, EventName)
				&& PassesEnemyFilter(Cancel.EnemyFilter, SubjectId))
			|| (!VolumeId.IsNone() && Cancel.Volumes.Contains(VolumeId));
		if (bMatch)
		{
			CancelStep(Index);
		}
	}
}

void FTutorialStateMachine::Enqueue(int32 Index)
{
	SetState(Index, ETutorialStepState::Queued);
	const ETutorialTriggerType TriggerType = Data.Steps[Index].Trigger.Type;
	if ((TriggerType == ETutorialTriggerType::AfterStep
		|| (TriggerType == ETutorialTriggerType::Condition && Data.Steps[Index].Kind == ETutorialStepKind::HintCard)) && IsEarlierBeatStep(Index))
	{
		// 2026-10-20 (pacing): also a condition card of a beat that is over (the A2 remedial heat pack card when the cold rises at the
		// A3 fence) - it would stand in front of the lesson of the place the squad is at. Event / volume remedial cards still show.
		// The next step of a chain whose beat is over (beacon 1 reached during the camera lesson; rev 5f: also the walk-and-talks /
		// objectives that would follow a step passed on the way): never shown now.
		DropEarlierBeatStep(Index);
		return;
	}
	if (IsCancelDue(Index))
	{
		// Rev 5e: what it talks about is already over (the bridge line due after the crossing): never played.
		CancelStep(Index);
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
	if (Data.Steps[Index].bPrecredit && IsCompletionConditionMet(Index))
	{
		// Rev 5d: the world already is as the step asks (the looter handed the heat packs out before the card came): done.
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
	if (bWasActive && Step.Kind == ETutorialStepKind::HintCard && !bNoBreatherOnFinish)
	{
		CardBreatherLeft = CardBreatherSeconds;
	}
	Runtime[Index].bEverFinished = true;
	ApplyStepLocks(Index); // a step that never showed still applies its locks (never lost)
	SetState(Index, FinalState, /*bSilent*/ !bWasActive);
	if (FinalState == ETutorialStepState::Completed)
	{
		LockStepContributors(Index); // 2026-10-23: before the confinement it may follow ends
	}
	ReleasePlaceLocksUntil(Step.Id);
	ReleaseConfinesUntil(Index); // rev 5i: the lesson that held the squad is over
	const FName FinishedId = Step.Id;
	CancelMatching(FinishedId, NAME_None, NAME_None); // rev 5e: talk about this step's place / moment is over
	for (int32 Other = 0; Other < Data.Steps.Num(); ++Other)
	{
		const FTutorialTrigger& Trigger = Data.Steps[Other].Trigger;
		if (Trigger.Type == ETutorialTriggerType::AfterStep && Trigger.AfterSteps.Contains(FinishedId) && AreAfterStepsFinished(Other))
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
			if (Complete.bFocusSubject && FocusSubjectProvider)
			{
				const FName Focus = FocusSubjectProvider(Data.Steps[Index].Id);
				if (!Focus.IsNone() && Focus != SubjectId)
				{
					continue; // "subject": "focus": someone else's event (the medkit healed another operative)
				}
			}
			if (!PassesEnemyFilter(Complete.EnemyFilter, SubjectId))
			{
				continue; // "enemy": another enemy's event (the far zombie walked into the near zombie's wire)
			}
			TryCountEventSubject(Index, SubjectId);
		}
	}
	CancelMatching(NAME_None, EventName, NAME_None, SubjectId);
	NoteConfineEvent(EventName, SubjectId);
	for (int32 Index = 0; Index < Data.Steps.Num(); ++Index)
	{
		const FTutorialTrigger& Trigger = Data.Steps[Index].Trigger;
		const bool bFailure = Trigger.Type == ETutorialTriggerType::FailureRetry;
		if ((Trigger.Type != ETutorialTriggerType::Event && !bFailure) || !TutorialRules::MatchesName(Trigger.Event, EventName) || !CanTrigger(Index)
			|| !IsArmed(Index) || !PassesEnemyFilter(Trigger.EnemyFilter, SubjectId))
		{
			continue; // not armed yet ("requires") / another enemy's event: the event is not stored
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

void FTutorialStateMachine::CountContribution(int32 Index, FName SubjectId, int32 Needed)
{
	if (!SubjectId.IsNone())
	{
		Runtime[Index].Contributors.Add(SubjectId);
	}
	AddProgress(Index, 1, Needed);
}

bool FTutorialStateMachine::TryCountEventSubject(int32 Index, FName SubjectId)
{
	const FTutorialCompletion& Complete = Data.Steps[Index].Complete;
	if (Complete.bRoleSpots && RoleSpotProvider && !RoleSpotProvider(Data.Steps[Index].Id, SubjectId))
	{
		return false; // "role_spots": not at HIS role's spot (the Medic-Sapper in the Commander's cover)
	}
	if (IsCompletionHeldByConfine(Index))
	{
		return false; // rev 5i: a later part of a confined lesson counts only once its card is up
	}
	if (!IsSubjectAllowed(Index, SubjectId))
	{
		return false; // "distinct_from": this operative already did another part of the lesson
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
			return false; // no subject, or this operative already counted
		}
	}
	CountContribution(Index, SubjectId, Complete.Count);
	return true;
}

bool FTutorialStateMachine::CountVolumeCompletions(FName VolumeId, FName SubjectId, const TArray<bool>& WasRunning)
{
	bool bCounted = false;
	for (int32 Index = 0; Index < Data.Steps.Num(); ++Index)
	{
		if (WasRunning[Index] && TutorialRules::IsRunning(Runtime[Index].State) && Data.Steps[Index].Complete.Volume == VolumeId
			&& IsSubjectAllowed(Index, SubjectId) && !IsCompletionHeldByConfine(Index))
		{
			CountContribution(Index, SubjectId, 1); // one entry completes a "go to X" step
			bCounted = true;
		}
	}
	return bCounted;
}

bool FTutorialStateMachine::HandleRoleSpotPresence(FName SubjectId)
{
	if (!IsLoaded() || SubjectId.IsNone())
	{
		return false;
	}
	bool bCounted = false;
	for (int32 Index = 0; Index < Data.Steps.Num(); ++Index)
	{
		const FTutorialCompletion& Complete = Data.Steps[Index].Complete;
		// Already counted for this step (his "CoverEntered" or an earlier presence tick): nothing new. The rest of the filters are
		// those of the event (TryCountEventSubject), so presence never counts where the event would not.
		if (!Complete.bRoleSpots || Complete.Event.IsEmpty() || !TutorialRules::IsRunning(Runtime[Index].State)
			|| Runtime[Index].Contributors.Contains(SubjectId))
		{
			continue;
		}
		bCounted |= TryCountEventSubject(Index, SubjectId);
	}
	PumpQueues();
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
	// Rev 5f: monotonic progress by location - a later beat's / later step's place starts that beat and passes what lies behind.
	// Rev 5i: while a confinement holds the squad, the route does not move (an operative in the hut before the entry lesson let
	// him in must not pass the lesson); the volume's own triggers still fire. Completions above may have released it already.
	if ((!VolumeId.IsNone() || !BeatId.IsNone()) && !IsConfined())
	{
		ApplyArrival(VolumeId, BeatId);
	}
	if (!VolumeId.IsNone())
	{
		CancelMatching(NAME_None, NAME_None, VolumeId); // after the beat start: its own talk about this place is dropped too
		const bool bHeld = IsConfined();
		for (int32 Index = 0; Index < Data.Steps.Num(); ++Index)
		{
			const FTutorialTrigger& Trigger = Data.Steps[Index].Trigger;
			if (bHeld && Data.FindBeatIndex(Data.Steps[Index].Beat) > CurrentBeatIndex)
			{
				continue; // rev 5i: held - a later beat's place does not start that beat (nor end the confinement)
			}
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
	// Rev 5i: a confinement of an earlier beat ends when the story moves on.
	for (FConfineRuntime& Confine : Confines)
	{
		if (!Confine.bReleased && Data.Steps.IsValidIndex(Confine.Owner) && Data.FindBeatIndex(Data.Steps[Confine.Owner].Beat) < BeatIndex)
		{
			Confine.bReleased = true;
			++ConfineSerial;
		}
	}
	// 2026-10-23: so do the place locks of an earlier beat's steps (beat-less steps: only their "until" / UnlockAll).
	const int32 LocksBefore = PlaceLocks.Num();
	PlaceLocks.RemoveAll([this, BeatIndex](const FTutorialPlaceLockState& Lock)
	{
		const FTutorialStep* Owner = Data.FindStep(Lock.Step);
		const int32 OwnerBeat = Owner && !Owner->Beat.IsNone() ? Data.FindBeatIndex(Owner->Beat) : INDEX_NONE;
		return OwnerBeat != INDEX_NONE && OwnerBeat < BeatIndex;
	});
	if (PlaceLocks.Num() != LocksBefore)
	{
		++PlaceLockSerial;
	}
	// Leftover hint cards of the beats the squad has left (waiting, queued or shown) are dropped; their chains follow.
	for (int32 Index = 0; Index < Data.Steps.Num(); ++Index)
	{
		if (IsEarlierBeatCard(Index) && TutorialRules::IsRunning(Runtime[Index].State))
		{
			DropEarlierBeatCard(Index);
		}
	}
	// Rev 5f (monotonic route): what the earlier beats still wait for at a PLACE is behind the squad now. Volume steps that never
	// started are passed (their unlocks apply), "go to X" objectives are done, walk-and-talks still waiting for the speech slot
	// are dropped (never played later). A radio / walk-and-talk on screen plays out; interaction objectives (an event / condition:
	// share the heat packs, light the barrel) keep running in parallel - they block nothing of the new beat.
	for (int32 Index = 0; Index < Data.Steps.Num(); ++Index)
	{
		if (!IsEarlierBeatStep(Index))
		{
			continue;
		}
		const FTutorialStep& Step = Data.Steps[Index];
		const ETutorialStepState State = Runtime[Index].State;
		if (IsPassableVolumeStep(Index))
		{
			PassStep(Index);
		}
		else if (TutorialRules::IsRunning(State) && Step.Kind == ETutorialStepKind::Objective && !Step.Complete.Volume.IsNone())
		{
			Finish(Index, ETutorialStepState::Completed);
		}
		else if ((State == ETutorialStepState::Waiting || State == ETutorialStepState::Queued) && Step.Kind == ETutorialStepKind::WalkTalk)
		{
			CancelStep(Index);
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
	PollCancelConditions();
	PollConditionCompletions();
	PollBeyond();
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
	Progress.RouteVolume = RouteProgressVolume;
	Progress.PlaceLocks = PlaceLocks;
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
		// Rev 5f: how far a running step got, and who counted for any step (distinct_from of the others).
		const bool bRunning = TutorialRules::IsRunning(State.State);
		if ((bRunning && (State.Progress > 0 || State.HeldSeconds > 0.f || !State.CountedSubjects.IsEmpty())) || !State.Contributors.IsEmpty())
		{
			FTutorialStepProgress& Step = Progress.StepProgress.Add(Data.Steps[Index].Id);
			Step.Progress = bRunning ? State.Progress : 0;
			Step.HeldSeconds = bRunning ? State.HeldSeconds : 0.f;
			Step.Subjects = bRunning ? State.CountedSubjects.Array() : TArray<FName>();
			Step.Contributors = State.Contributors.Array();
			Step.Subjects.Sort(FNameLexicalLess());
			Step.Contributors.Sort(FNameLexicalLess());
		}
	}
	for (const FConfineRuntime& Confine : Confines)
	{
		if (Data.Steps.IsValidIndex(Confine.Owner))
		{
			FTutorialConfineProgress& Saved = Progress.Confines.AddDefaulted_GetRef();
			Saved.Owner = Data.Steps[Confine.Owner].Id;
			Saved.bReleased = Confine.bReleased;
			Saved.Passed = Confine.Passed;
			Saved.Free = Confine.Free;
		}
	}
	return Progress;
}

void FTutorialStateMachine::ImportProgress(const FTutorialProgress& Progress)
{
	ResetProgress();
	CurrentBeatIndex = Data.FindBeatIndex(Progress.CurrentBeat);
	History = Progress.History;
	RouteProgressVolume = Progress.RouteVolume; // 2026-10-20: the progress key follows from it (GetRouteProgressKey)
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
	// Rev 5i: the confinements as saved (a released one stays released; the one in force holds the squad again).
	for (const FTutorialConfineProgress& Saved : Progress.Confines)
	{
		const int32 Owner = IndexOf(Saved.Owner);
		if (Runtime.IsValidIndex(Owner) && Data.Steps[Owner].Confine.IsSet())
		{
			FConfineRuntime& Confine = Confines.AddDefaulted_GetRef();
			Confine.Owner = Owner;
			Confine.bReleased = Saved.bReleased;
			Confine.Passed = Saved.Passed;
			Confine.Free = Saved.Free;
		}
	}
	++ConfineSerial;
	// 2026-10-23: the operatives held in place stay held after the load (the locks of steps the file no longer has are dropped).
	PlaceLocks = TutorialRules::FilterPlaceLocks(Data, Progress.PlaceLocks);
	++PlaceLockSerial;
	// Rev 5f: who counted for which step (finished ones too: "distinct_from" of the steps still running).
	for (const TPair<FName, FTutorialStepProgress>& Entry : Progress.StepProgress)
	{
		const int32 Index = IndexOf(Entry.Key);
		if (Runtime.IsValidIndex(Index))
		{
			Runtime[Index].Contributors = TSet<FName>(Entry.Value.Contributors);
		}
	}
	for (const FName StepId : Progress.Pending)
	{
		const int32 Index = IndexOf(StepId);
		if (Runtime.IsValidIndex(Index) && Runtime[Index].State == ETutorialStepState::Idle)
		{
			// Rev 5f: the step goes on from where it was (two of three across the bridge stay counted), not from 0.
			if (const FTutorialStepProgress* Saved = Progress.StepProgress.Find(StepId))
			{
				Runtime[Index].Progress = Saved->Progress;
				Runtime[Index].HeldSeconds = Saved->HeldSeconds;
				Runtime[Index].CountedSubjects = TSet<FName>(Saved->Subjects);
			}
			Enqueue(Index);
		}
	}
}

// --- Rev 5i: confine ------------------------------------------------------------------------------------------------

int32 FTutorialStateMachine::FindActiveConfine() const
{
	for (int32 Index = Confines.Num() - 1; Index >= 0; --Index)
	{
		if (!Confines[Index].bReleased)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

bool FTutorialStateMachine::IsConfined() const
{
	return FindActiveConfine() != INDEX_NONE;
}

int32 FTutorialStateMachine::GetConfineUntilIndex(int32 Owner) const
{
	if (!Data.Steps.IsValidIndex(Owner))
	{
		return INDEX_NONE;
	}
	const FName Until = Data.Steps[Owner].Confine.Until;
	return Until.IsNone() ? Owner : IndexOf(Until);
}

bool FTutorialStateMachine::IsConfineAreaOpen(FName VolumeId) const
{
	bool bGated = false;
	for (int32 Index = 0; Index < Data.Steps.Num(); ++Index)
	{
		const FTutorialCompletion& Complete = Data.Steps[Index].Complete;
		if (Complete.Volume != VolumeId && Complete.InVolume != VolumeId)
		{
			continue;
		}
		bGated = true;
		if (Runtime[Index].State == ETutorialStepState::Active || Runtime[Index].bEverFinished)
		{
			return true; // its lesson is on screen (or done): the place may be used
		}
	}
	return !bGated;
}

bool FTutorialStateMachine::IsCompletionHeldByConfine(int32 Index) const
{
	const int32 Active = FindActiveConfine();
	if (Active == INDEX_NONE || !Data.Steps.IsValidIndex(Index) || Runtime[Index].State == ETutorialStepState::Active)
	{
		return false;
	}
	const FTutorialConfine& Spec = Data.Steps[Confines[Active].Owner].Confine;
	const FTutorialCompletion& Complete = Data.Steps[Index].Complete;
	return (!Complete.Volume.IsNone() && Spec.Allow.Contains(Complete.Volume)) || (!Complete.InVolume.IsNone() && Spec.Allow.Contains(Complete.InVolume));
}

void FTutorialStateMachine::StartConfine(int32 Index)
{
	const int32 Until = GetConfineUntilIndex(Index);
	if (Until != Index && Runtime.IsValidIndex(Until) && Runtime[Until].bEverFinished && !TutorialRules::IsRunning(Runtime[Until].State))
	{
		return; // the lesson it waits for is over already
	}
	for (FConfineRuntime& Other : Confines)
	{
		Other.bReleased = true; // one confinement at a time
	}
	Confines.RemoveAll([Index](const FConfineRuntime& Other) { return Other.Owner == Index; });
	FConfineRuntime& Confine = Confines.AddDefaulted_GetRef();
	Confine.Owner = Index;
	++ConfineSerial;
}

void FTutorialStateMachine::ReleaseConfinesUntil(int32 FinishedIndex)
{
	for (FConfineRuntime& Confine : Confines)
	{
		if (!Confine.bReleased && GetConfineUntilIndex(Confine.Owner) == FinishedIndex)
		{
			Confine.bReleased = true;
			++ConfineSerial;
		}
	}
}

bool FTutorialStateMachine::ClaimConfineFree(FName OperativeId)
{
	const int32 Active = FindActiveConfine();
	if (Active == INDEX_NONE || OperativeId.IsNone() || !Data.Steps[Confines[Active].Owner].Confine.bFreeFirst || !Confines[Active].Free.IsNone())
	{
		return false;
	}
	Confines[Active].Free = OperativeId;
	return true;
}

bool FTutorialStateMachine::ReleaseConfines()
{
	bool bReleased = false;
	for (FConfineRuntime& Confine : Confines)
	{
		bReleased |= !Confine.bReleased;
		Confine.bReleased = true;
	}
	if (bReleased)
	{
		++ConfineSerial;
	}
	return bReleased;
}

bool FTutorialStateMachine::GetActiveConfine(FTutorialConfineView& Out) const
{
	const int32 Active = FindActiveConfine();
	if (Active == INDEX_NONE)
	{
		return false;
	}
	const FConfineRuntime& Confine = Confines[Active];
	const FTutorialStep& Owner = Data.Steps[Confine.Owner];
	Out = FTutorialConfineView();
	Out.Owner = Owner.Id;
	Out.Spec = Owner.Confine;
	Out.Areas.Add(Owner.Confine.Volume);
	for (const FName Area : Owner.Confine.Allow)
	{
		if (IsConfineAreaOpen(Area))
		{
			Out.Areas.AddUnique(Area);
		}
	}
	Out.Passed = Confine.Passed;
	if (Owner.Confine.bFreeFocus && FocusSubjectProvider)
	{
		Out.FreeOperative = FocusSubjectProvider(Owner.Id); // 2026-10-20: the operative who does the lesson alone
	}
	else if (Owner.Confine.bFreeFirst)
	{
		Out.FreeOperative = Confine.Free; // 2026-10-23: the one who took it (none yet: the first ordered outside will)
	}
	return true;
}

void FTutorialStateMachine::NoteConfineEvent(FName EventName, FName SubjectId)
{
	const int32 Active = FindActiveConfine();
	if (Active == INDEX_NONE || SubjectId.IsNone())
	{
		return;
	}
	FConfineRuntime& Confine = Confines[Active];
	const FString& ThroughEvent = Data.Steps[Confine.Owner].Confine.ThroughEvent;
	if (!ThroughEvent.IsEmpty() && TutorialRules::MatchesName(ThroughEvent, EventName))
	{
		Confine.Passed.AddUnique(SubjectId); // through: free to move on
	}
}

// --- 2026-10-23: lock in place ---------------------------------------------------------------------------------------

bool FTutorialStateMachine::IsLockedInPlace(FName OperativeId, FName* OutStep) const
{
	if (OperativeId.IsNone())
	{
		return false;
	}
	const FTutorialPlaceLockState* Lock = PlaceLocks.FindByPredicate([OperativeId](const FTutorialPlaceLockState& Entry) { return Entry.Operative == OperativeId; });
	if (Lock && OutStep)
	{
		*OutStep = Lock->Step;
	}
	return Lock != nullptr;
}

bool FTutorialStateMachine::ReleasePlaceLocks()
{
	if (PlaceLocks.IsEmpty())
	{
		return false;
	}
	PlaceLocks.Reset();
	++PlaceLockSerial;
	return true;
}

void FTutorialStateMachine::LockStepContributors(int32 Index)
{
	const FTutorialStep& Step = Data.Steps[Index];
	if (!Step.LockInPlace.IsSet())
	{
		return;
	}
	// Released by its own "until", else by the "until" of the confinement in force (the lesson it belongs to), else by the next beat.
	FName Until = Step.LockInPlace.Until;
	if (Until.IsNone())
	{
		const int32 Active = FindActiveConfine();
		const int32 ConfineUntil = Active != INDEX_NONE ? GetConfineUntilIndex(Confines[Active].Owner) : INDEX_NONE;
		Until = Data.Steps.IsValidIndex(ConfineUntil) ? Data.Steps[ConfineUntil].Id : NAME_None;
	}
	const int32 UntilIndex = IndexOf(Until);
	if (UntilIndex == Index || (Runtime.IsValidIndex(UntilIndex) && TutorialRules::IsFinished(Runtime[UntilIndex].State)))
	{
		return; // the lesson is already over: nothing to hold him for
	}
	TArray<FName> Operatives = Runtime[Index].Contributors.Array();
	Operatives.Sort(FNameLexicalLess());
	for (const FName Operative : Operatives)
	{
		PlaceLocks.RemoveAll([Operative](const FTutorialPlaceLockState& Lock) { return Lock.Operative == Operative; });
		FTutorialPlaceLockState& Lock = PlaceLocks.AddDefaulted_GetRef();
		Lock.Operative = Operative;
		Lock.Step = Step.Id;
		Lock.Until = Until;
	}
	if (!Operatives.IsEmpty())
	{
		++PlaceLockSerial;
	}
}

void FTutorialStateMachine::ReleasePlaceLocksUntil(FName FinishedStep)
{
	if (PlaceLocks.RemoveAll([FinishedStep](const FTutorialPlaceLockState& Lock) { return !Lock.Until.IsNone() && Lock.Until == FinishedStep; }) > 0)
	{
		++PlaceLockSerial;
	}
}

TArray<FTutorialTransition> FTutorialStateMachine::ConsumeTransitions()
{
	TArray<FTutorialTransition> Out = MoveTemp(Transitions);
	Transitions.Reset();
	return Out;
}
