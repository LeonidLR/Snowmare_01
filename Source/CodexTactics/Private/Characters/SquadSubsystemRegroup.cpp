// USquadSubsystem: the fast regroup on B (the game designer's bug 2026-11-10). The game starts in Solo with no distance limit (2026-11-09); B
// used to put every member straight into the formation, so a man tens of metres away walked to his slot at the formation's catch-up pace
// (FormationRuntime::FollowerMove: his walk x move.catchup_mult, ~275 cm/s, its arrival slow-down on top) and the squad took very long to join
// up. Now ExitSoloMode arms the members far from the leader; the first formation frame with their slot known decides who regroups fast
// (SquadRegroupRules::ShouldStartFastRegroup). Those run / sprint straight to the leader's LIVE position along the nav path (no formation
// pacing, no slot shuffling - ReassignFormationPlaces keeps the places meanwhile; the leader is never stopped) and join the formation within
// squad_regroup.json join_m. Ladders / ramps keep their own rules: FollowTo refuses on a ladder / at a drop (the queue holds him), a ramp-file
// traffic hold returns before this in UpdateFollower.
// Pure rules + tuning: SquadRegroupRules (tests CodexTactics.Squad.Regroup.*). Smoke: CodexTactics.FastRegroupSmoke.
// Godot reference: none (UE-only; docs/systems/squad-and-orders.md "Fast regroup on B").

#include "Characters/SquadSubsystem.h"

#include "Characters/OperativeCharacter.h"
#include "Characters/OperativeMovementRules.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/SuppressionSubsystem.h"
#include "Engine/World.h"
#include "Formation/FormationTuning.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"

namespace SquadSubsystemRegroupLocal
{
	TAutoConsoleVariable<int32> CVarSquadFastRegroup(TEXT("Codex.Squad.FastRegroup"), 1,
		TEXT("1 (default, the game designer's bug 2026-11-10): on B a member far from the formation (Content/Data/AI/squad_regroup.json start_m) first runs / sprints straight to the leader and joins the formation close to it (join_m); 0: the old rule (everyone straight into the formation's walking catch-up)."));
}

bool USquadSubsystem::IsFastRegroupEnabled()
{
	return SquadSubsystemRegroupLocal::CVarSquadFastRegroup.GetValueOnGameThread() != 0;
}

bool USquadSubsystem::IsFastRegrouping(const AOperativeCharacter* Operative) const
{
	const FFastRegroupState* State = Operative ? FastRegroups.Find(TWeakObjectPtr<AOperativeCharacter>(const_cast<AOperativeCharacter*>(Operative))) : nullptr;
	return State && (State->bActive || State->bArmed);
}

bool USquadSubsystem::ArmFastRegroup(AOperativeCharacter& Operative, const AOperativeCharacter& LeaderRef)
{
	FastRegroups.Remove(&Operative);
	if (!IsFastRegroupEnabled() || &Operative == &LeaderRef)
	{
		return false;
	}
	const FSquadRegroupTuning& Tuning = SquadRegroupTuning::Get();
	const UHealthComponent* Health = Operative.HealthComponent;
	const bool bCritical = Health && SquadRegroupRules::IsCriticallyWounded(Health->GetCurrentHealth(), Health->GetMaxHealth(), Tuning);
	// The slot is not known in Solo (no formation frame): the leader's distance arms him, his slot's decides on the first formation frame.
	const float ToLeader = static_cast<float>(FVector::Dist2D(Operative.GetActorLocation(), LeaderRef.GetActorLocation()));
	if (!SquadRegroupRules::ShouldStartFastRegroup(ToLeader, bCritical, Tuning))
	{
		return false;
	}
	FFastRegroupState& State = FastRegroups.Add(&Operative);
	State.StartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	State.bStancePending = Tuning.bStandUpToRun && LeaderRef.GetStance() != EOperativeStance::Standing;
	UE_LOG(LogCodexTactics, Display, TEXT("[Regroup] %s is %.0f cm from %s - fast regroup armed%s"), *Operative.DisplayName.ToString(), ToLeader,
		*LeaderRef.DisplayName.ToString(), State.bStancePending ? TEXT(" (stands up to run, takes the leader's stance on joining)") : TEXT(""));
	return true;
}

float USquadSubsystem::FastRegroupGapCm(const FFollowerState& Follower, const AOperativeCharacter& Operative, const AOperativeCharacter& LeaderRef) const
{
	const FVector2D Here(Operative.GetActorLocation());
	const float ToLeader = static_cast<float>(FVector2D::Distance(Here, FVector2D(LeaderRef.GetActorLocation())));
	FVector2D Slot;
	const TOptional<EFormationRole> Role = Sectors.bActiveThisFrame ? SectorRoleOf(Follower) : TOptional<EFormationRole>();
	if (const FFormationSlot* Frame = Role.IsSet() ? FindFrameSlot(Role.GetValue()) : nullptr)
	{
		Slot = Frame->Position;
	}
	else
	{
		Slot = FVector2D(SquadFormation::ComputeSlotPosition(FormationConfig, LeaderRef.GetActorLocation(), FormationHeading, Follower.Slot, false));
	}
	return SquadRegroupRules::FormationGapCm(static_cast<float>(FVector2D::Distance(Here, Slot)), ToLeader);
}

bool USquadSubsystem::IsRegroupCombat() const
{
	const UWorld* World = GetWorld();
	const UGameFlowSubsystem* Flow = World ? World->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	if (Flow && Flow->GetPhase() == ECodexGamePhase::WaveCombat)
	{
		return true; // a fight
	}
	const FFormationConfig& Config = FormationTuning::Get();
	for (const FFormationThreat& Threat : GatherFormationThreats())
	{
		if (Threat.AgeS >= 0.f && Threat.AgeS <= Config.BypassThreatMemoryS)
		{
			return true; // an enemy the squad sees / heard lately
		}
	}
	for (const TWeakObjectPtr<AOperativeCharacter>& Member : Members)
	{
		AOperativeCharacter* Operative = Member.Get();
		AOperativeCharacter* Shooter = nullptr;
		FVector Flash;
		if (Operative && (Operative->GetUnderFireThreat(Shooter, Flash) || USuppressionSubsystem::GetTierOf(Operative) != ESuppressionTier::None))
		{
			return true; // a member under fire / suppressed
		}
	}
	return false;
}

bool USquadSubsystem::UpdateFastRegroup(FFollowerState& Follower, AOperativeCharacter& Operative, AOperativeCharacter& LeaderRef, float DeltaTime)
{
	FFastRegroupState* State = FastRegroups.Find(Follower.Operative);
	if (!State)
	{
		return false;
	}
	const FSquadRegroupTuning& Tuning = SquadRegroupTuning::Get();
	const UHealthComponent* Health = Operative.HealthComponent;
	const bool bCritical = Health && SquadRegroupRules::IsCriticallyWounded(Health->GetCurrentHealth(), Health->GetMaxHealth(), Tuning);
	const float Gap = FastRegroupGapCm(Follower, Operative, LeaderRef);
	const double Now = GetWorld()->GetTimeSeconds();
	if (State->bArmed)
	{
		State->bArmed = false;
		State->bActive = IsFastRegroupEnabled() && SquadRegroupRules::ShouldStartFastRegroup(Gap, bCritical, Tuning);
		if (State->bActive)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[Regroup] %s: %.0f cm from the formation - runs to %s first"), *Operative.DisplayName.ToString(), Gap,
				*LeaderRef.DisplayName.ToString());
		}
	}
	else if (State->bActive)
	{
		State->bActive = IsFastRegroupEnabled() && SquadRegroupRules::KeepsFastRegroup(Gap, bCritical, Tuning);
	}
	if (!State->bActive)
	{
		// He joins the formation: the leader's stance now when he was stood up to run (unless the player gave him one meanwhile).
		UE_LOG(LogCodexTactics, Display, TEXT("[Regroup] %s joins the formation %.0f cm from it after %.1f s%s"), *Operative.DisplayName.ToString(), Gap,
			Now - State->StartTime, bCritical ? TEXT(" (critically wounded - no fast regroup)") : TEXT(""));
		if (State->bStancePending && Operative.IsStanceFromSquad() && Operative.GetStance() != LeaderRef.GetStance())
		{
			Operative.SetStance(LeaderRef.GetStance());
			Operative.MarkStanceFromSquad();
		}
		FastRegroups.Remove(Follower.Operative);
		Follower.bParked = false;
		Follower.RepathTimeRemaining = 0.f; // the formation step at once (UpdateFollower goes on this frame)
		return false;
	}

	// The pace: sprint out of combat (fit, standing), run in combat, the wounded run; in a crouch / prone (the leader's stance kept) his stance's.
	const ESquadRegroupPace Pace = SquadRegroupRules::ChoosePace(bFrameRegroupCombat, Operative.IsWounded(), Operative.CanSprint(), Tuning);
	const bool bStanding = Operative.GetStance() == EOperativeStance::Standing;
	Operative.SetSprinting(bStanding && Pace == ESquadRegroupPace::Sprint);
	if (!State->LoggedPace.IsSet() || State->LoggedPace.GetValue() != Pace)
	{
		State->LoggedPace = Pace;
		UE_LOG(LogCodexTactics, Display, TEXT("[Regroup] %s: %s to %s (%.0f cm from the formation%s)"), *Operative.DisplayName.ToString(),
			SquadRegroupRules::PaceName(Pace), *LeaderRef.DisplayName.ToString(), Gap, bFrameRegroupCombat ? TEXT(", combat") : TEXT(""));
	}
	// A stance change in progress (stood up to run) roots him: the run starts when the clip ends.
	if (const float MoveHold = Operative.GetMoveHoldSeconds(); OperativeMovementRules::ShouldDeferMove(MoveHold))
	{
		if (Operative.GetVelocity().SizeSquared2D() > 25.f && !Operative.IsProneDiving())
		{
			Operative.StopOperative();
		}
		Follower.RepathTimeRemaining = MoveHold;
		return true;
	}
	Follower.RepathTimeRemaining -= DeltaTime;
	if (Follower.RepathTimeRemaining > 0.f)
	{
		return true;
	}
	Follower.RepathTimeRemaining = Tuning.RepathSeconds;
	const float Speed = bStanding
		? SquadRegroupRules::PaceSpeed(Pace, Operative.GetMaxSpeed(), Operative.MovementConfig.WoundedSpeedMultiplier, Operative.GetColdSpeedMultiplier(), Tuning)
		: Operative.GetMaxSpeed();
	Follower.bParked = false;
	Follower.bCatchUp = true;
	// The leader's live position (he may walk on): the nav path's own way there - a ramp, a stair, a ladder (its queue holds him: FollowTo refuses).
	Operative.FollowTo(LeaderRef.GetActorLocation(), Speed);
	return true;
}
