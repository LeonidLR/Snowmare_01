// AOperativeCharacter: the manoeuvre under fire (bug #43 rest, 2026-11-07; UE-only, no Godot reference - Godot had no shooting enemies of
// this kind). Rules: Combat/UnderFireRules (tested in CodexTactics.Combat.UnderFire.*); spec: docs/systems/pmc-squad-ai.md "Under fire".
// Under fire, not in cover, no order of his own: the nearest cover slot shielding him from the muzzle flash, else a short step off the line
// for a clear line to the flash (a rear man of a column, a mate in the way), else prone. The squad's men and the hostile PMC fighters alike.

#include "Characters/OperativeCharacter.h"

#include "AI/PmcCommanderRules.h"
#include "AI/PmcSquadSubsystem.h"
#include "Characters/OperativeCharacterInternal.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/UnderFireRules.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/InteractionSubsystem.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Tactics/CoverTraceRules.h"
#include "Tactics/TurnBasedCombatSubsystem.h"

namespace OperativeUnderFireLocal
{
	/** Cover search rays round him. */
	constexpr int32 CoverRays = 16;
	/** A mate this close to a slot (or walking into one there) holds it. */
	constexpr float SlotTakenCm = 80.f;
	/** A step spot this close to a mate is taken. */
	constexpr float StepClearOfMateCm = 70.f;
	/** A step spot must project onto the navmesh within this (inside the passage, not into a wall). */
	constexpr float StepNavSnapCm = 30.f;
	/** At most this many path queries per cover search. */
	constexpr int32 CoverPathChecks = 3;

	static TAutoConsoleVariable<int32> CVarUnderFireManoeuvre(TEXT("Codex.Combat.UnderFireManoeuvre"), 1,
		TEXT("The manoeuvre under fire (#43, under_fire.json): 1 (default) cover / a side step for a line / prone under fire; 0 off (only the turn and the return fire at the flash)"));

	bool IsLive(const AOperativeCharacter* Operative)
	{
		return Operative && Operative->HealthComponent && Operative->HealthComponent->IsAlive() && !Operative->IsKilledInAction();
	}
}

TArray<AOperativeCharacter*> AOperativeCharacter::GetUnderFireMates() const
{
	TArray<AOperativeCharacter*> Mates;
	const UWorld* World = GetWorld();
	if (!World)
	{
		return Mates;
	}
	if (IsPlayerTeam())
	{
		if (const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>())
		{
			Mates = Squad->GetMembers();
		}
	}
	else if (const UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World))
	{
		Mates = Pmc->GetTeamMembers(TeamId);
	}
	Mates.RemoveAll([this](const AOperativeCharacter* Mate) { return Mate == this || !OperativeUnderFireLocal::IsLive(Mate) || Mate->TeamId != TeamId; });
	return Mates;
}

bool AOperativeCharacter::IsHoldingUnderFireSpot() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	// The leader's own manoeuvre under fire (his cover sprint / step) is no formation order: a follower under fire stays where he is
	// instead of walking after him into the fire (his own manoeuvre comes after the reaction delay).
	const USquadSubsystem* Squad = IsPlayerTeam() ? World->GetSubsystem<USquadSubsystem>() : nullptr;
	const AOperativeCharacter* CurrentLeader = Squad ? Squad->GetLeader() : nullptr;
	AOperativeCharacter* Shooter = nullptr;
	FVector Flash = FVector::ZeroVector;
	if (CurrentLeader && CurrentLeader != this && CurrentLeader->IsOnUnderFireManoeuvreMove() && GetUnderFireThreat(Shooter, Flash))
	{
		return true;
	}
	if (!bUnderFireManoeuvred)
	{
		return false;
	}
	const AOperativeCharacter* Leader = UnderFireLeader.Get();
	// A new order to the leader releases the spot - not his own manoeuvre's walk.
	const bool bLeaderOrdered = Leader && Leader != this && Leader->GetMoveOrderSerial() != UnderFireLeaderSerial && !Leader->IsOnUnderFireManoeuvreMove();
	return UnderFireRules::IsHoldingManoeuvreSpot(UnderFireRules::GetConfig(), true, World->GetTimeSeconds(), UnderFireTime, bLeaderOrdered);
}

bool AOperativeCharacter::HasUnderFireLine(const FVector& Feet, const FVector& Flash, const AOperativeCharacter* Shooter, bool* bOutMateInLine) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	// His muzzle height over his feet, carried to the spot tried.
	const FVector MyFeet = GetActorLocation() - FVector(0.f, 0.f, GetSimpleCollisionHalfHeight());
	const FVector From = Feet + FVector(0.f, 0.f, FMath::Max(30.f, static_cast<float>(GetMuzzleLocation().Z - MyFeet.Z)));
	const FVector End = Flash + (From - Flash).GetSafeNormal() * 15.f; // a hand short of the flash (the muzzle pokes into its corner)
	FCollisionQueryParams Params(SCENE_QUERY_STAT(UnderFireLine), false, this);
	OperativeCharacterInternal::AddShotLineAllies(World, Params);
	FHitResult Hit;
	const bool bClear = !(World->LineTraceSingleByChannel(Hit, From, End, ECC_Visibility, Params) && Hit.GetActor() != Shooter);
	bool bMate = false;
	const float BlockCm = UnderFireRules::GetConfig().MateBlockRadiusCm;
	for (const AOperativeCharacter* Mate : GetUnderFireMates())
	{
		bMate |= UnderFireRules::IsMateInLine(From, End, Mate->GetActorLocation(), BlockCm);
	}
	if (bOutMateInLine)
	{
		*bOutMateInLine = bMate;
	}
	return bClear && !bMate;
}

bool AOperativeCharacter::FindUnderFireCover(const FVector& Flash, FCoverSlot& OutSlot) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	const FUnderFireConfig& Config = UnderFireRules::GetConfig();
	const FVector Chest = GetActorLocation();
	const FVector Feet = Chest - FVector(0.f, 0.f, GetSimpleCollisionHalfHeight());
	const TArray<AOperativeCharacter*> Mates = GetUnderFireMates();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(UnderFireCoverSearch), false, this);
	TArray<const AActor*> Ignored;
	Ignored.Add(this);
	for (const AOperativeCharacter* Mate : Mates)
	{
		Params.AddIgnoredActor(Mate);
		Ignored.Add(Mate);
	}
	struct FCandidate
	{
		FCoverSlot Slot;
		float DistanceCm = 0.f;
	};
	TArray<FCandidate, TInlineAllocator<OperativeUnderFireLocal::CoverRays>> Candidates;
	for (int32 Index = 0; Index < OperativeUnderFireLocal::CoverRays; ++Index)
	{
		const FVector Direction = FRotator(0.f, Index * 360.f / OperativeUnderFireLocal::CoverRays, 0.f).Vector();
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, Chest, Chest + Direction * Config.CoverSeekRadiusCm, ECC_Visibility, Params) || !Hit.GetActor()
			|| Cast<APawn>(Hit.GetActor()))
		{
			continue;
		}
		FCoverSlot Slot;
		if (!CoverTraceRules::FindCoverSlotAt(World, Hit.ImpactPoint, Direction, Slot, FCoverTraceConfig(), Ignored) || !Slot.IsValid())
		{
			continue;
		}
		const float Distance = static_cast<float>(FVector::Dist2D(Feet, Slot.WorldLocation));
		if (Distance > Config.CoverSeekRadiusCm || PmcCommanderRules::CoverQualityAgainst(Slot.WorldLocation, Slot.WallNormal, Flash) < Config.CoverMinQuality)
		{
			continue; // too far, or its wall does not stand between him and the flash
		}
		const bool bTaken = Mates.ContainsByPredicate([&Slot](const AOperativeCharacter* Mate)
		{
			return FVector::Dist2D(Mate->GetActorLocation(), Slot.WorldLocation) < OperativeUnderFireLocal::SlotTakenCm
				|| (Mate->HasPendingCover() && FVector::Dist2D(Mate->GetLastMoveDestination(), Slot.WorldLocation) < OperativeUnderFireLocal::SlotTakenCm);
		});
		if (!bTaken)
		{
			Candidates.Add({ Slot, Distance });
		}
	}
	Candidates.Sort([](const FCandidate& A, const FCandidate& B) { return A.DistanceCm < B.DistanceCm; });
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	int32 Checks = 0;
	for (const FCandidate& Candidate : Candidates)
	{
		if (Nav)
		{
			// The walk there must stay short (no detour round the block into the open).
			if (++Checks > OperativeUnderFireLocal::CoverPathChecks)
			{
				break;
			}
			const UNavigationPath* Path = Nav->FindPathToLocationSynchronously(World, Feet, Candidate.Slot.WorldLocation);
			if (!Path || !Path->IsValid() || Path->IsPartial() || Path->GetPathLength() > 1.5f * Config.CoverSeekRadiusCm)
			{
				continue;
			}
		}
		OutSlot = Candidate.Slot;
		return true;
	}
	return false;
}

void AOperativeCharacter::UpdateUnderFireManoeuvre(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World || DeltaTime <= 0.f)
	{
		return;
	}
	const FUnderFireConfig& Config = UnderFireRules::GetConfig();
	// The spot he manoeuvred to is held until the threat is over or the leader gets a new order; then the prone man crouches again.
	if (bUnderFireManoeuvred && !IsHoldingUnderFireSpot())
	{
		if (UnderFireRules::ShouldRiseAfterThreat(bUnderFireWentProne, Stance == EOperativeStance::Prone, false))
		{
			SetStance(EOperativeStance::Crouching);
		}
		bUnderFireManoeuvred = false;
		bUnderFireWentProne = false;
		bUnderFireIssuedMove = false;
	}
	if (OperativeUnderFireLocal::CVarUnderFireManoeuvre.GetValueOnGameThread() == 0)
	{
		return;
	}
	UnderFireDecisionTimer -= DeltaTime;
	if (UnderFireDecisionTimer > 0.f)
	{
		return;
	}
	UnderFireDecisionTimer = FMath::Max(Config.DecisionSeconds, 0.05f);
	AOperativeCharacter* Shooter = nullptr;
	FVector Flash = FVector::ZeroVector;
	if (!GetUnderFireThreat(Shooter, Flash) || !Shooter)
	{
		return;
	}
	DecideUnderFireManoeuvre(*Shooter, Flash);
}

void AOperativeCharacter::DecideUnderFireManoeuvre(AOperativeCharacter& Shooter, const FVector& Flash)
{
	UWorld* World = GetWorld();
	const FUnderFireConfig& Config = UnderFireRules::GetConfig();
	const double Now = World->GetTimeSeconds();
	const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
	const UTurnBasedCombatSubsystem* TurnBased = World->GetSubsystem<UTurnBasedCombatSubsystem>();
	const USquadSubsystem* Squad = IsPlayerTeam() ? World->GetSubsystem<USquadSubsystem>() : nullptr;
	const UInteractionSubsystem* Interaction = World->GetSubsystem<UInteractionSubsystem>();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;

	UnderFireRules::FManoeuvreFacts Facts;
	Facts.bRealTimeFight = !Flow || (Flow->GetPhase() == ECodexGamePhase::WaveCombat && Flow->GetCombatMode() == ECodexCombatMode::RealTime);
	Facts.bAlive = OperativeUnderFireLocal::IsLive(this);
	Facts.bThreatActive = true;
	Facts.SecondsUnderFire = static_cast<float>(FMath::Max(0.0, Now - UnderFireSpellStart));
	Facts.bInCover = bInCover;
	Facts.bCoverPending = HasPendingCover();
	Facts.bOwnOrder = HasOwnMoveOrder() || (Interaction && Interaction->HasUseOrder(this)) || (Squad && Squad->IsWaitingForGroupCoverLeader(this));
	Facts.bFireOrFacingOrder = HasOwnFacing();
	Facts.bCarrying = bCarrying;
	Facts.bPanicking = IsPanicking();
	Facts.bGridOwnsFacing = TurnBased && TurnBased->IsActive();
	Facts.bFormationFollower = Squad && !Squad->IsSoloMode() && Leader && Leader != this && !bGuarding;
	// The formation walks: the leader on a move order of the player's (not his own manoeuvre under fire) or still moving.
	Facts.bFormationWalking = Facts.bFormationFollower && !Leader->IsOnUnderFireManoeuvreMove()
		&& (Leader->HasOwnMoveOrder() || Leader->GetVelocity().SizeSquared2D() > 50.f * 50.f);
	Facts.bAutonomyOwnsCover = Squad && Squad->IsAutonomousSquadCombat();
	if (!UnderFireRules::MayManoeuvre(Config, Facts))
	{
		return;
	}

	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetSimpleCollisionHalfHeight());
	UnderFireRules::FManoeuvreOptions Options;
	Options.bProne = Stance == EOperativeStance::Prone;
	FCoverSlot CoverSlotFound;
	if (Now >= UnderFireNextCoverSearch)
	{
		Options.bCoverFound = FindUnderFireCover(Flash, CoverSlotFound);
		UnderFireNextCoverSearch = Options.bCoverFound ? Now : Now + Config.CoverRetrySeconds;
	}
	bool bMateInLine = false;
	Options.bWantsLine = !HasUnderFireLine(Feet, Flash, &Shooter, &bMateInLine);
	Options.bSidestepCoolingDown = Now < UnderFireNextSidestep;
	FVector StepTo = FVector::ZeroVector;
	if (!Options.bCoverFound && Options.bWantsLine && !Options.bSidestepCoolingDown)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		const TArray<AOperativeCharacter*> Mates = GetUnderFireMates();
		for (const float Offset : UnderFireRules::SidestepOffsets(Config))
		{
			const FVector Wanted = UnderFireRules::SidestepPoint(Feet, Flash, Offset);
			FNavLocation OnNav;
			FVector NavHit;
			if (!Nav || !Nav->ProjectPointToNavigation(Wanted, OnNav, FVector(OperativeUnderFireLocal::StepNavSnapCm, OperativeUnderFireLocal::StepNavSnapCm, 150.f))
				|| FVector::Dist2D(OnNav.Location, Wanted) > OperativeUnderFireLocal::StepNavSnapCm
				|| UNavigationSystemV1::NavigationRaycast(World, Feet, OnNav.Location, NavHit))
			{
				continue; // off the navmesh (a wall of the passage) or not straight across
			}
			if (Mates.ContainsByPredicate([&OnNav](const AOperativeCharacter* Mate)
				{ return FVector::Dist2D(Mate->GetActorLocation(), OnNav.Location) < OperativeUnderFireLocal::StepClearOfMateCm; }))
			{
				continue;
			}
			if (HasUnderFireLine(OnNav.Location, Flash, &Shooter))
			{
				StepTo = OnNav.Location;
				Options.bSidestepFound = true;
				break;
			}
		}
	}

	const EUnderFireManoeuvre Choice = UnderFireRules::ChooseManoeuvre(Config, Options);
	bool bDone = false;
	switch (Choice)
	{
	case EUnderFireManoeuvre::TakeCover:
		bDone = OrderTakeCover(CoverSlotFound, /*bSprint*/ true) == EOperativeOrderResult::Accepted;
		UnderFireCoverMoves += bDone ? 1 : 0;
		UnderFireNextCoverSearch = bDone ? Now : Now + Config.CoverRetrySeconds;
		break;
	case EUnderFireManoeuvre::Sidestep:
		bDone = OrderMoveTo(StepTo, false) == EOperativeOrderResult::Accepted;
		UnderFireSidesteps += bDone ? 1 : 0;
		UnderFireNextSidestep = Now + Config.SidestepCooldownSeconds;
		break;
	case EUnderFireManoeuvre::GoProne:
		SetStance(EOperativeStance::Prone);
		bDone = Stance == EOperativeStance::Prone;
		bUnderFireWentProne |= bDone;
		UnderFireProneDrops += bDone ? 1 : 0;
		break;
	default:
		break;
	}
	if (bDone)
	{
		if (Choice != EUnderFireManoeuvre::GoProne)
		{
			bUnderFireIssuedMove = true;
			UnderFireIssuedSerial = GetMoveOrderSerial();
		}
		bUnderFireManoeuvred = true;
		UnderFireLeader = Leader;
		UnderFireLeaderSerial = Leader ? Leader->GetMoveOrderSerial() : 0;
		UE_LOG(LogCodexTactics, Display, TEXT("[UnderFire] %s (team %d): %s against the flash %.0f m off (line %s%s)"), *DisplayName.ToString(), TeamId,
			Choice == EUnderFireManoeuvre::TakeCover ? TEXT("takes cover") : (Choice == EUnderFireManoeuvre::Sidestep ? TEXT("steps off the line") : TEXT("goes prone")),
			FVector::Dist2D(Feet, Flash) / 100.f, Options.bWantsLine ? TEXT("blocked") : TEXT("clear"), bMateInLine ? TEXT(", a mate in it") : TEXT(""));
	}
}
