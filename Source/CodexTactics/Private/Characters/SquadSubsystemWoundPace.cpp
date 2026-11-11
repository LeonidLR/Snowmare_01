// USquadSubsystem: the formation walks at its most wounded member's pace (the game designer's bug + decision "a" 2026-11-11). A wounded follower
// fell far behind (the slots moved at the leader's patrol walk, faster than his allowed walk), the follow law pushed him past his own speed (his
// injured clip looked hurried) and, lagging beyond move.rear_turn_lag as the rear guard, he turned round facing his run instead of his look.
// Each formation frame UpdateFormationWoundPace finds the slowest wounded (light / heavy) member in the formation; the leader's pace multiplier
// is then capped (UpdateFormationSectors), a wounded follower's formation move is capped at his allowed walk (UpdateSectorFollower) and the
// leader's sprint ends. The critically wounded (crawling) member never sets the pace.
// Pure rules + tuning: SquadWoundPaceRules (tests CodexTactics.Squad.WoundPace.*). Smoke: CodexTactics.WoundedFollowerSmoke.
// Godot reference: none (UE-only; docs/systems/squad-and-orders.md "Wounded member in the formation").

#include "Characters/SquadSubsystem.h"

#include "Characters/OperativeCharacter.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Vehicles/VehicleSubsystem.h"

bool USquadSubsystem::IsWoundPaceCapOn() const
{
	const FSquadWoundPaceTuning& Tuning = SquadWoundPaceTuning::Get();
	return SquadWoundPaceRules::IsEnabled(Tuning) && Tuning.bCapWoundedFollower;
}

bool USquadSubsystem::GetFormationWoundPace(float& OutCapSpeed, AOperativeCharacter*& OutSlowest) const
{
	if (!FrameWoundPace.IsSet() || !Sectors.bActiveThisFrame)
	{
		return false;
	}
	OutCapSpeed = FrameWoundPace->CapSpeed;
	OutSlowest = FrameWoundPaceBy.Get();
	return true;
}

void USquadSubsystem::UpdateFormationWoundPace(AOperativeCharacter& LeaderRef)
{
	const FSquadWoundPaceTuning& Tuning = SquadWoundPaceTuning::Get();
	const bool bEnabled = SquadWoundPaceRules::IsEnabled(Tuning);
	TArray<FSquadWoundPaceMember> Inputs;
	TArray<AOperativeCharacter*> Who;
	const auto AddMember = [&Inputs, &Who](AOperativeCharacter& Operative, bool bInFormation)
	{
		FSquadWoundPaceMember& Member = Inputs.AddDefaulted_GetRef();
		Member.AllowedSpeed = Operative.GetAllowedWalkSpeed();
		Member.bWounded = Operative.IsWounded();
		Member.bCritical = Operative.GetWoundTier() == CriticalWoundRules::EWoundTier::Critical;
		Member.bInFormation = bInFormation && !Operative.IsPanicking() && !Operative.IsKnockedDown() && !VehicleQueries::IsMounted(&Operative);
		Who.Add(&Operative);
	};
	if (bEnabled)
	{
		AddMember(LeaderRef, true);
		for (const FFollowerState& Follower : Followers)
		{
			AOperativeCharacter* Operative = Follower.Operative.Get();
			if (Operative && Operative != &LeaderRef)
			{
				// Not following yet / not now: a fast regroup on B runs to the leader on its own rules, an order of his own is his.
				AddMember(*Operative, !IsFastRegrouping(Operative) && !IsOnOwnOrder(Operative));
			}
		}
	}
	FrameWoundPace = SquadWoundPaceRules::SquadPace(Inputs, Tuning, bEnabled);
	FrameWoundPaceBy = FrameWoundPace.IsSet() && Who.IsValidIndex(FrameWoundPace->SlowestIndex) ? Who[FrameWoundPace->SlowestIndex] : nullptr;

	const float Cap = FrameWoundPace.IsSet() ? FrameWoundPace->CapSpeed : -1.f;
	if (!FMath::IsNearlyEqual(Cap, LoggedWoundPaceCap, 1.f) || FrameWoundPaceBy != LoggedWoundPaceBy)
	{
		LoggedWoundPaceCap = Cap;
		LoggedWoundPaceBy = FrameWoundPaceBy;
		if (const AOperativeCharacter* Slowest = FrameWoundPaceBy.Get())
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[Formation] wound pace: the squad walks at %.0f cm/s - %s (%.0f %% HP, allowed walk %.0f cm/s) x pace_share %.2f"),
				Cap, *Slowest->DisplayName.ToString(), Slowest->HealthComponent && Slowest->HealthComponent->GetMaxHealth() > 0.f
					? 100.f * Slowest->HealthComponent->GetCurrentHealth() / Slowest->HealthComponent->GetMaxHealth() : -1.f,
				Slowest->GetAllowedWalkSpeed(), Tuning.PaceShare);
		}
		else
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[Formation] wound pace: off (%s)"), bEnabled ? TEXT("nobody wounded walks in the formation") : TEXT("Codex.Squad.WoundPace 0 / enabled 0"));
		}
	}
	if (SquadWoundPaceRules::EndsLeaderSprint(LeaderRef.IsSprinting(), FrameWoundPace, Tuning))
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Formation] wound pace: %s's sprint ends - the squad walks with %s"), *LeaderRef.DisplayName.ToString(),
			FrameWoundPaceBy.IsValid() ? *FrameWoundPaceBy->DisplayName.ToString() : TEXT("the wounded"));
		LeaderRef.SetSprinting(false);
	}
}
