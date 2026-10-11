#include "AI/RepathRules.h"

ERepathFollowState RepathRules::ClassifyFollowState(bool bHasRequest, bool bIdle, bool bCurrentRequestIsOurs, bool bRequestFailed,
	bool bFinishedOurs, bool bFinishSuccess, bool bFinishInterrupted)
{
	if (!bHasRequest)
	{
		return ERepathFollowState::None;
	}
	if (!bIdle)
	{
		// Walking: ours, or another move (a patrol walk, a marksman run, a script) replaced it.
		return bCurrentRequestIsOurs ? ERepathFollowState::Following : ERepathFollowState::Stopped;
	}
	if (bRequestFailed)
	{
		return ERepathFollowState::Failed;
	}
	if (!bFinishedOurs)
	{
		return ERepathFollowState::Stopped; // idle without a recorded end of our move: treat it as stopped
	}
	if (bFinishSuccess)
	{
		return ERepathFollowState::Reached;
	}
	return bFinishInterrupted ? ERepathFollowState::Stopped : ERepathFollowState::Failed;
}

bool RepathRules::ShouldRepath(const FRepathGoal* Last, const FRepathGoal& Wanted, ERepathFollowState State, double SecondsSinceLast,
	bool bAlreadyAtGoal, const FRepathSettings& Settings)
{
	// At the goal: the MoveTo is a cheap already-at-goal finish that stops the walk where the per-tick ask stopped it.
	if (!Settings.bEnabled || bAlreadyAtGoal || !Last || State == ERepathFollowState::None || State == ERepathFollowState::Stopped)
	{
		return true;
	}
	if (Last->GoalActorId != Wanted.GoalActorId || !FMath::IsNearlyEqual(Last->AcceptanceRadius, Wanted.AcceptanceRadius, 1.f))
	{
		return true; // another goal
	}
	const bool bGoalMoved = FVector::DistSquared(Last->Location, Wanted.Location) > FMath::Square(Settings.MinGoalDeltaCm);
	const bool bIntervalDue = SecondsSinceLast + UE_KINDA_SMALL_NUMBER >= Settings.IntervalSeconds; // 0.3 (double) vs 0.3f
	switch (State)
	{
	case ERepathFollowState::Following:
		// The engine keeps a path to a goal actor up to date itself.
		return !Wanted.IsActorGoal() && bGoalMoved && bIntervalDue;
	case ERepathFollowState::Reached:
		return bGoalMoved || bIntervalDue;
	case ERepathFollowState::Failed:
		return bIntervalDue;
	default:
		return true;
	}
}
