// Dev-only console command for a headless check of the turn-based walk on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.TurnWalkSmoke
// The speed the anim instances read (UTurnBasedCombatSubsystem::GetTacticalMoveSpeed) must follow the body's real speed
// over the whole path: starting from rest (no running on the spot), no stop at the cells, arriving at rest in Godot's
// total time (sum of tactical_step_duration, diagonals x1.414). Then (deviation, user decision 2026-10-01) the operative
// lies down and is ordered two cells on: he rises to crouching first (the walk waits for the rising clip) and walks the
// crouched steps slower by the real-time crouch / walk speed ratio.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/OperativeMovementRules.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Tactics/GorkyGridManager.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "Tactics/TurnBasedRules.h"
#include "TimerManager.h"

namespace TurnWalkSmoke
{
	constexpr float StepSeconds = 0.05f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		bool bPrepared = false;
		TWeakObjectPtr<AOperativeCharacter> Walker;
		FVector LastLocation = FVector::ZeroVector;
		double LastWorldTime = 0.0;
		double WalkStart = 0.0;
		float MaxMismatch = 0.f;
		float EarlyReported = 0.f;
		float MinMidSpeed = 1.e6f;
		int32 Samples = 0;
		float ExpectedSeconds = 0.f;
		// Prone walk.
		float RiseDelay = 0.f;
		FVector ProneStart = FVector::ZeroVector;
		float MovedDuringRise = 0.f;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("TurnWalkSmoke"));
		return false;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 40.f)
		{
			return World ? Finish(State, false) : false;
		}
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		UTurnBasedCombatSubsystem* TurnBased = World->GetSubsystem<UTurnBasedCombatSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 3.f)
			{
				return true;
			}
			if (!State.bPrepared)
			{
				Flow->TriggerCombatZone();
				Flow->FinishCutscene();
				Flow->FinishPreparation();
				// Keep one wave enemy (moved in front) and drop the rest: an empty wave would be cleared at once.
				AEnemyCharacter* Kept = nullptr;
				for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
				{
					if (!Kept && !It->IsDying())
					{
						Kept = *It;
					}
					else
					{
						It->Destroy();
					}
				}
				if (Kept)
				{
					// Parked far behind the squad and frozen: outside the 15 m fight, never reaching the squad.
					Kept->SetActorLocation(Squad->GetLeader()->GetActorLocation() - Squad->GetLeader()->GetActorForwardVector() * 4000.f);
					Kept->CustomTimeDilation = 0.f;
				}
				AOperativeCharacter* Leader = Squad->GetLeader();
				World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound,
					Leader->GetActorLocation() + Leader->GetActorForwardVector() * 700.f + FVector(0.f, 0.f, 20.f));
				State.bPrepared = true;
				State.StageTime = 2.5f;
				return true;
			}
			Check(State, Flow->RequestEnterTurnBased(true) == EGameFlowResult::Ok && TurnBased->IsActive(), TEXT("turn-based combat started"));
			AOperativeCharacter* Unit = TurnBased->GetActiveUnit();
			const FTurnUnitState* UnitState = TurnBased->GetUnitState(Unit);
			if (!Unit || !UnitState)
			{
				return Finish(State, false);
			}
			// The farthest free reachable cell (a path of several cells), keeping AP for the prone walk below.
			UGorkyGridManager* Grid = TurnBased->GetGrid();
			FIntPoint Best = UnitState->GridPos;
			int32 BestCost = 0;
			for (const TPair<FIntPoint, int32>& Entry : Grid->GetReachableCells(UnitState->GridPos, UnitState->AP))
			{
				if (Entry.Value > BestCost && Entry.Value <= UnitState->AP - 3 && Grid->GetOccupantType(Entry.Key) == EGorkyOccupantType::None)
				{
					Best = Entry.Key;
					BestCost = Entry.Value;
				}
			}
			const TArray<FIntPoint> Path = Grid->FindPath(UnitState->GridPos, Best, UnitState->AP);
			FIntPoint Previous = UnitState->GridPos;
			for (const FIntPoint& Cell : Path)
			{
				const FIntPoint Delta = Cell - Previous;
				State.ExpectedSeconds += TurnBased->SquadStepDuration * (Delta.X != 0 && Delta.Y != 0 ? 1.414f : 1.f);
				Previous = Cell;
			}
			Check(State, Path.Num() >= 2, FString::Printf(TEXT("a path of %d cells"), Path.Num()));
			State.Walker = Unit;
			State.LastLocation = Unit->GetActorLocation();
			State.LastWorldTime = World->GetTimeSeconds();
			State.WalkStart = State.LastWorldTime;
			UE_LOG(LogCodexTactics, Display, TEXT("Walk: %d cells from (%d, %d) to (%d, %d), step %.2f s, cell %.0f cm, unit at (%.0f, %.0f)"),
				Path.Num(), UnitState->GridPos.X, UnitState->GridPos.Y, Best.X, Best.Y, TurnBased->SquadStepDuration, Grid->CellSize,
				Unit->GetActorLocation().X, Unit->GetActorLocation().Y);
			TurnBased->HandleWorldClick(Grid->GridToWorld(Best), nullptr, false);
			State.Stage = 1;
			State.StageTime = 0.f;
			return true;
		}
		case 1:
		{
			AOperativeCharacter* Unit = State.Walker.Get();
			if (!Unit)
			{
				return Finish(State, false);
			}
			const double Now = World->GetTimeSeconds();
			const float Dt = static_cast<float>(Now - State.LastWorldTime);
			const float Reported = TurnBased->GetTacticalMoveSpeed(Unit);
			if (Dt > 0.02f && Reported >= 0.f)
			{
				const float Measured = FVector::Dist2D(Unit->GetActorLocation(), State.LastLocation) / Dt;
				const float Elapsed = static_cast<float>(Now - State.WalkStart);
				// Reported is this frame's speed, Measured the average since the last sample: compare loosely.
				State.MaxMismatch = FMath::Max(State.MaxMismatch, FMath::Abs(Reported - Measured));
				if (Elapsed < 0.15f)
				{
					State.EarlyReported = FMath::Max(State.EarlyReported, Reported);
				}
				if (Elapsed > 0.3f * State.ExpectedSeconds && Elapsed < 0.7f * State.ExpectedSeconds)
				{
					State.MinMidSpeed = FMath::Min(State.MinMidSpeed, Measured);
				}
				if (State.Samples < 12 || Reported < 1.f)
				{
					UE_LOG(LogCodexTactics, Display, TEXT("Walk sample %.3f s: reported %.0f, measured %.0f, dt %.3f, at (%.0f, %.0f)"),
						Elapsed, Reported, Measured, Dt, Unit->GetActorLocation().X, Unit->GetActorLocation().Y);
				}
				++State.Samples;
			}
			State.LastLocation = Unit->GetActorLocation();
			State.LastWorldTime = Now;
			if (TurnBased->IsUnitMoving())
			{
				return true;
			}
			const float Took = static_cast<float>(Now - State.WalkStart);
			Check(State, State.Samples >= 5, FString::Printf(TEXT("%d speed samples"), State.Samples));
			Check(State, State.EarlyReported < 150.f, FString::Printf(TEXT("starts from rest (%.0f cm/s in the first 0.15 s)"), State.EarlyReported));
			Check(State, State.MaxMismatch < 120.f, FString::Printf(TEXT("anim speed follows the body (max gap %.0f cm/s)"), State.MaxMismatch));
			Check(State, State.MinMidSpeed > 150.f, FString::Printf(TEXT("no stop at the cells mid-path (min %.0f cm/s)"), State.MinMidSpeed));
			Check(State, FMath::Abs(Took - State.ExpectedSeconds) < 0.25f, FString::Printf(TEXT("Godot total time %.2f s (took %.2f s)"),
				State.ExpectedSeconds, Took));
			Check(State, TurnBased->GetTacticalMoveSpeed(Unit) < 0.f, TEXT("idle after the walk"));

			// Lie down (1 AP), then a two-cell order.
			Check(State, TurnBased->SetActiveUnitStance(EOperativeStance::Prone) && Unit->GetStance() == EOperativeStance::Prone, TEXT("lay down"));
			const FTurnUnitState* UnitState = TurnBased->GetUnitState(Unit);
			UGorkyGridManager* Grid = TurnBased->GetGrid();
			// Prone / crouched a step costs double (user decision 2026-10-04): two cells when the AP allow, else one.
			FIntPoint Target(-999, -999);
			const int32 StepBudget = UnitState->AP / TurnBasedRules::MoveCostMultiplier(EOperativeStance::Prone, FTurnBasedBalance());
			const int32 Cells = FMath::Clamp(StepBudget, 1, 2);
			for (const TPair<FIntPoint, int32>& Entry : Grid->GetReachableCells(UnitState->GridPos, StepBudget))
			{
				if (Entry.Value == Cells && Grid->GetOccupantType(Entry.Key) == EGorkyOccupantType::None
					&& Grid->FindPath(UnitState->GridPos, Entry.Key, StepBudget).Num() == Cells)
				{
					Target = Entry.Key;
					break;
				}
			}
			if (Target.X == -999)
			{
				Check(State, false, TEXT("a free two-cell walk for the prone check"));
				return Finish(State, false);
			}
			const UOperativeAnimInstance* Anim = Cast<UOperativeAnimInstance>(Unit->GetMesh()->GetAnimInstance());
			State.RiseDelay = Anim && Anim->ProneToCrouchAnimation
				? FMath::Max(0.f, Anim->ProneToCrouchAnimation->GetPlayLength() / Anim->StanceTransitionPlayRate - Anim->StanceTransitionBlendTime) : 0.f;
			const float Step = TurnBased->SquadStepDuration
				/ FMath::Clamp(OperativeMovementRules::GetStanceSpeedMultiplier(Unit->MovementConfig, EOperativeStance::Crouching), 0.2f, 1.f);
			State.ExpectedSeconds = State.RiseDelay;
			FIntPoint Previous = UnitState->GridPos;
			for (const FIntPoint& Cell : Grid->FindPath(UnitState->GridPos, Target, StepBudget))
			{
				const FIntPoint Delta = Cell - Previous;
				State.ExpectedSeconds += Step * (Delta.X != 0 && Delta.Y != 0 ? 1.414f : 1.f);
				Previous = Cell;
			}
			UE_LOG(LogCodexTactics, Display, TEXT("Prone walk: rise %.2f s, crouched step %.2f s (standing %.2f), expected %.2f s"),
				State.RiseDelay, Step, TurnBased->SquadStepDuration, State.ExpectedSeconds);
			State.ProneStart = Unit->GetActorLocation();
			State.WalkStart = World->GetTimeSeconds();
			TurnBased->HandleWorldClick(Grid->GridToWorld(Target), nullptr, false);
			State.Stage = 2;
			State.StageTime = 0.f;
			return true;
		}
		case 2:
		{
			AOperativeCharacter* Unit = State.Walker.Get();
			if (!Unit)
			{
				return Finish(State, false);
			}
			const float Elapsed = static_cast<float>(World->GetTimeSeconds() - State.WalkStart);
			if (Elapsed < State.RiseDelay - 0.1f)
			{
				State.MovedDuringRise = FMath::Max(State.MovedDuringRise, static_cast<float>(FVector::Dist2D(Unit->GetActorLocation(), State.ProneStart)));
			}
			if (TurnBased->IsUnitMoving())
			{
				return true;
			}
			const FTurnUnitState* UnitState = TurnBased->GetUnitState(Unit);
			Check(State, Unit->GetStance() == EOperativeStance::Crouching && UnitState && UnitState->Stance == EOperativeStance::Crouching,
				TEXT("prone order: rose to crouching (operative and turn state)"));
			Check(State, State.MovedDuringRise < 5.f, FString::Printf(TEXT("prone order: stays put while rising (moved %.0f cm)"), State.MovedDuringRise));
			Check(State, FMath::Abs(Elapsed - State.ExpectedSeconds) < 0.35f, FString::Printf(TEXT("prone order: rise + slower crouched steps %.2f s (took %.2f s)"),
				State.ExpectedSeconds, Elapsed));
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
		{
			FTimerHandle PlaceHandle;
			TWeakObjectPtr<UWorld> PlaceWorld(World);
			World->GetTimerManager().SetTimer(PlaceHandle, FTimerDelegate::CreateLambda([PlaceWorld]()
			{
				SmokeUtils::PlaceSquadAtTestStart(PlaceWorld.Get());
			}), 0.5f, false);
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.TurnWalkSmoke"),
		TEXT("Dev check: the turn-based walk speed seen by the animation follows the body over the whole path; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
