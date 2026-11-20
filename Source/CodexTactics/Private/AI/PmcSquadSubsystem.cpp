// The hostile PMC squads: registry, team sight, targets and the utility-AI commander (user request 2026-11-03, docs/systems/pmc-squad-ai.md).
// UE-only, no Godot reference.

#include "AI/PmcSquadSubsystem.h"
#include "AI/FiringPositionSubsystem.h"

#include "AI/PmcDefenceRules.h"
#include "AI/PmcOperatorRules.h"
#include "Combat/LauncherTacticsRules.h"
#include "Combat/MortarNestActor.h"
#include "Combat/MortarRules.h"
#include "Combat/MortarSubsystem.h"
#include "AI/SquadOrderCatalog.h"
#include "AIController.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/OperativeMovementRules.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/PeekSightRules.h"
#include "Combat/SightRules.h"
#include "Characters/StealthModeRules.h"
#include "Combat/SuppressionRules.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Combat/WaveSubsystem.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/CodexTacticsGameMode.h"
#include "Data/LevelJsonRules.h"
#include "Data/WeaponDataAsset.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Falls/LedgeFallComponent.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Ladders/LadderClimbComponent.h"
#include "LevelGen/TacticalMapSubsystem.h"
#include "Materials/MaterialInterface.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Tactics/CoverTraceRules.h"

namespace PmcSquadLocal
{
	constexpr float CoverSearchRadiusCm = 1000.f;
	constexpr float ArriveCm = 250.f;
	constexpr float StuckSeconds = 4.f;
	constexpr float StuckProgressCm = 100.f;
	/** A walk that ends farther than this from its goal failed (fix/pmc-arena-stalemate). */
	constexpr float FailShortCm = 400.f;
	/** G008 follow-up: a shot at him within this long counts as "under fire" (the defender's contact gate, the sector shift). */
	constexpr double UnderFireSeconds = 2.0;
	/** G008 follow-up: a defence post walk is re-checked only once he stands still (below this speed, cm/s)... */
	constexpr float StillSpeedCm = 30.f;
	/** ...and the Ambush that walked him is this old (longer than the stalemate smoke's 2 s re-decide window). */
	constexpr double DefenceRetrySeconds = 2.5;
	// The heal walk's reach is FPmcWeights::HealReachCm (pmc_commander.json heal_reach_m), shared with the post gate.
	constexpr float MonsterSightCm = 2500.f;
	/** A demasked shooter (his own fire) is noticed by the teams with a man this close (planar). */
	constexpr float GunfireHearingCm = 6000.f;

	/** Scenario identity, never pointer/GetUniqueID/FName comparison-index order.
	 * SpawnFighter passes 100 * TeamId + Index as SquadIndex; placed non-operatives use their object path. */
	inline FString StableActorId(const AActor* Actor)
	{
		if (const AOperativeCharacter* Fighter = Cast<AOperativeCharacter>(Actor))
		{
			return FString::Printf(TEXT("operative:%d:%d"), Fighter->TeamId, Fighter->SquadIndex);
		}
		return Actor ? Actor->GetPathName() : FString();
	}

	/** A cover candidate is a slot, not a fighter: owner/component identity and exact geometry break score ties.
	 * Lossless double formatting avoids merging distinct nearby slots via a quantization tolerance. */
	inline FString StableCoverSlotId(const FCoverSlot& Slot, bool bElevated)
	{
		const UPrimitiveComponent* Component = Slot.WallComponent.Get();
		return FString::Printf(TEXT("%s|%s|%.17g,%.17g,%.17g|%.17g,%.17g,%.17g|%.17g,%.17g,%.17g|%d|%d"),
			*StableActorId(Slot.WallActor.Get()), Component ? *Component->GetPathName() : TEXT(""),
			Slot.WorldLocation.X, Slot.WorldLocation.Y, Slot.WorldLocation.Z,
			Slot.WallNormal.X, Slot.WallNormal.Y, Slot.WallNormal.Z,
			Slot.WallPoint.X, Slot.WallPoint.Y, Slot.WallPoint.Z, static_cast<int32>(Slot.Height), bElevated ? 1 : 0);
	}

	inline bool IsLiveOperative(const AOperativeCharacter* Operative)
	{
		return IsValid(Operative) && !Operative->IsHidden() && !Operative->IsKilledInAction() && Operative->HealthComponent && Operative->HealthComponent->IsAlive();
	}

	/**
	 * G008: the SquadIndex set the defence posts were built for, sorted and unique. Decide and RefreshDefence both use this
	 * over GetTeamMembers, so the "living set changed" test cannot stay true forever (review finding 5).
	 */
	inline TArray<int32> LivingSquadIndices(TConstArrayView<AOperativeCharacter*> Members)
	{
		TArray<int32> Living;
		for (const AOperativeCharacter* Member : Members)
		{
			if (IsLiveOperative(Member))
			{
				Living.AddUnique(Member->SquadIndex);
			}
		}
		Living.Sort();
		return Living;
	}

	/**
	 * fix/pmc-onpoint-3d-detour: he is on a ladder / a drop link of his walk (queued at the ladder, climbing, running off the edge, landing).
	 * The ladder's StartEntry stops his path following (StopOperative) and resumes the walk after the climb (ResumeMoveAfterLadder), so for
	 * the commander the walk is still running - it used to read as "ended short of its goal", failed the order and re-decided mid-climb.
	 */
	inline bool IsTraversingLink(const AOperativeCharacter& Operative)
	{
		return (Operative.LadderClimb && Operative.LadderClimb->IsBusy()) || (Operative.LedgeFall && Operative.LedgeFall->IsBusy());
	}

	/** His feet: the capsule centre minus its half height (the level tests compare feet with navmesh points; fix/pmc-onpoint-3d-detour). */
	inline FVector FeetOf(const AOperativeCharacter& Operative)
	{
		return Operative.GetActorLocation() - FVector(0.f, 0.f, Operative.GetSimpleCollisionHalfHeight());
	}

	inline float HealthFractionOf(const AOperativeCharacter& Operative)
	{
		return Operative.HealthComponent ? Operative.HealthComponent->GetHealthFraction() : 0.f;
	}

	inline bool HasWeaponId(const AOperativeCharacter& Operative, const TCHAR* WeaponId)
	{
		for (const TObjectPtr<UWeaponDataAsset>& Weapon : Operative.AvailableWeapons)
		{
			if (Weapon && Weapon->WeaponId == WeaponId)
			{
				return true;
			}
		}
		return false;
	}
}

bool UPmcSquadSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UPmcSquadSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UPmcSquadSubsystem, STATGROUP_Tickables);
}

UPmcSquadSubsystem* UPmcSquadSubsystem::Get(const UWorld* World)
{
	return World ? World->GetSubsystem<UPmcSquadSubsystem>() : nullptr;
}

// --- Registry --------------------------------------------------------------------------------------------------------------

UPmcSquadSubsystem::FTeam& UPmcSquadSubsystem::TeamOf(int32 TeamId)
{
	FTeam* Team = Teams.Find(TeamId);
	if (!Team)
	{
		Team = &Teams.Add(TeamId);
		PmcCommanderRules::LoadWeightsFile(FString(), Team->Weights);
	}
	return *Team;
}

const UPmcSquadSubsystem::FTeam* UPmcSquadSubsystem::FindTeam(int32 TeamId) const
{
	return Teams.Find(TeamId);
}

void UPmcSquadSubsystem::RegisterOperative(AOperativeCharacter* Operative)
{
	if (!Operative || Operative->TeamId == PmcTeam::Player)
	{
		return;
	}
	FTeam& Team = TeamOf(Operative->TeamId);
	Team.Members.AddUnique(Operative);
	TeamOf(PmcTeam::Player); // the player's side keeps its own sight of the hostiles
	if (!Listened.Contains(Operative))
	{
		Listened.Add(Operative);
		Operative->OnWeaponFiredNative.AddUObject(this, &UPmcSquadSubsystem::HandleWeaponFired);
	}
}

void UPmcSquadSubsystem::UnregisterOperative(AOperativeCharacter* Operative)
{
	if (!Operative)
	{
		return;
	}
	for (TPair<int32, FTeam>& Pair : Teams)
	{
		Pair.Value.Members.Remove(Operative);
		Pair.Value.Fighters.Remove(Operative);
	}
	Listened.Remove(Operative);
}

TArray<AOperativeCharacter*> UPmcSquadSubsystem::MembersOf(int32 TeamId) const
{
	TArray<AOperativeCharacter*> Out;
	if (TeamId == PmcTeam::Player)
	{
		if (const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr)
		{
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member && Member->TeamId == PmcTeam::Player)
				{
					Out.Add(Member);
				}
			}
		}
		return Out;
	}
	if (const FTeam* Team = FindTeam(TeamId))
	{
		for (const TWeakObjectPtr<AOperativeCharacter>& Member : Team->Members)
		{
			if (AOperativeCharacter* Fighter = Member.Get())
			{
				Out.Add(Fighter);
			}
		}
	}
	return Out;
}

TArray<AOperativeCharacter*> UPmcSquadSubsystem::GetTeamMembers(int32 TeamId) const
{
	TArray<AOperativeCharacter*> Out;
	for (AOperativeCharacter* Member : MembersOf(TeamId))
	{
		if (PmcSquadLocal::IsLiveOperative(Member))
		{
			Out.Add(Member);
		}
	}
	return Out;
}

TArray<AOperativeCharacter*> UPmcSquadSubsystem::GetHostileOperatives() const
{
	TArray<AOperativeCharacter*> Out;
	for (const TPair<int32, FTeam>& Pair : Teams)
	{
		if (Pair.Key == PmcTeam::Player)
		{
			continue;
		}
		for (const TWeakObjectPtr<AOperativeCharacter>& Member : Pair.Value.Members)
		{
			if (AOperativeCharacter* Fighter = Member.Get())
			{
				Out.Add(Fighter);
			}
		}
	}
	return Out;
}

bool UPmcSquadSubsystem::HasLiveHostileOperatives(const UWorld* World)
{
	const UPmcSquadSubsystem* Self = Get(World);
	if (!Self)
	{
		return false;
	}
	for (const AOperativeCharacter* Fighter : Self->GetHostileOperatives())
	{
		if (PmcSquadLocal::IsLiveOperative(Fighter) && Self->IsTeamEngaged(Fighter->TeamId))
		{
			return true;
		}
	}
	return false;
}

bool UPmcSquadSubsystem::IsTeamEngaged(int32 TeamId) const
{
	const FTeam* Team = FindTeam(TeamId);
	if (!Team)
	{
		return false;
	}
	if (Team->bEverContact || TeamId == PmcTeam::Player)
	{
		return Team->bEverContact;
	}
	// The player's squad has seen one of its men (the fight may have started from his side).
	if (const FTeam* Player = FindTeam(PmcTeam::Player))
	{
		for (const TWeakObjectPtr<AOperativeCharacter>& Member : Team->Members)
		{
			if (Member.IsValid() && Player->Known.Contains(TWeakObjectPtr<AActor>(Member.Get())))
			{
				return true;
			}
		}
	}
	return false;
}

void UPmcSquadSubsystem::SetTeamPosture(int32 TeamId, EPmcPosture Posture, const FVector& Anchor, const FVector& PlayerStart, int32 Seed, const FPmcWeights& Weights)
{
	TeamOf(PmcTeam::Player); // the player's side keeps its own sight of the hostiles
	FTeam& Team = TeamOf(TeamId);
	Team.Weights = Weights;
	Team.Posture = Posture;
	Team.PostureAnchor = Anchor;
	Team.PlayerStart = PlayerStart;
	Team.PostureSeed = Seed;
	Team.PostureLeg = 0;
	Team.PostureTimer = 0.f;
	Team.AssaultPoint.Reset();
	Team.bHuntTeam = Posture == EPmcPosture::Hunt;
	Team.HuntResumedAt = -1.0;
	// A patrol walks on its own until contact; defend / hunt run the commander from the start (Ambush / Advance on the search point).
	Team.bCommanded = Posture != EPmcPosture::Patrol;
	Team.DecisionTimer = 0.f;
}

bool UPmcSquadSubsystem::GetTeamPosture(int32 TeamId, EPmcPosture& OutPosture) const
{
	const FTeam* Team = FindTeam(TeamId);
	if (!Team || !Team->Posture.IsSet())
	{
		return false;
	}
	OutPosture = Team->Posture.GetValue();
	return true;
}

void UPmcSquadSubsystem::UpdatePosture(int32 TeamId, FTeam& Team, float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!Team.Posture.IsSet())
	{
		// fix/pmc-hunt-honest-sight (2026-11-09): hunters that lost the player's men (out of sight HuntResumeSeconds after the contact) hunt again -
		// they used to hold in cover at a remembered spot for good (PmcEncounterHuntSmoke: two survivors fell back and never came back).
		bool bAnySeenNow = false;
		for (const TPair<TWeakObjectPtr<AActor>, FKnownTarget>& Pair : Team.Known)
		{
			bAnySeenNow |= Pair.Value.Actor.IsValid() && Pair.Value.bSeenNow;
		}
		const double Now = World ? World->GetTimeSeconds() : 0.0;
		if (!World || !PmcEncounterRules::ShouldResumeHunt(Team.bHuntTeam, Team.bEverContact, static_cast<float>(Now - Team.LastSeenContactTime), bAnySeenNow,
			HuntResumeSeconds))
		{
			return;
		}
		Team.Posture = EPmcPosture::Hunt;
		Team.HuntResumedAt = Now;
		Team.PostureTimer = 0.f;
		Team.PostureLeg = FMath::Max(Team.PostureLeg, 1); // the player start is no search point any more
		UE_LOG(LogCodexTactics, Display, TEXT("[PmcEncounter] team %d lost sight of the squad %.0f s ago: the hunt resumes"), TeamId, Now - Team.LastSeenContactTime);
	}
	const TArray<AOperativeCharacter*> Mates = GetTeamMembers(TeamId);
	if (!World || Mates.IsEmpty())
	{
		return;
	}
	const EPmcPosture Posture = Team.Posture.GetValue();
	// Contact = seen an enemy (sight, touch, a shot heard within GunfireHearingCm - a hit's shooter is demasked) or seen by the squad. A resumed
	// hunt ends on the next SEEN enemy only (honest sight; a heard shot makes the search point, not the fight).
	const bool bContact = Team.HuntResumedAt >= 0.0 ? Team.LastSeenContactTime > Team.HuntResumedAt : IsTeamEngaged(TeamId);
	if (bContact)
	{
		Team.HuntResumedAt = -1.0;
		// The posture ends, the utility commander runs the fight (a patrol stops its roam walk first).
		UE_LOG(LogCodexTactics, Display, TEXT("[PmcEncounter] team %d engaged (%s): the commander takes over"), TeamId, PmcEncounterRules::PostureName(Posture));
		Team.Posture.Reset();
		Team.AssaultPoint.Reset();
		Team.bEverContact = true;
		Team.LastContactTime = World->GetTimeSeconds();
		Team.bCommanded = true;
		Team.DecisionTimer = 0.f;
		if (Posture == EPmcPosture::Patrol)
		{
			for (AOperativeCharacter* Mate : Mates)
			{
				if (Mate->HasOwnMoveOrder())
				{
					FSquadOrderContext Context;
					Context.Operative = Mate;
					SquadOrderCatalog::Execute(ESquadOrder::Hold, Context);
				}
			}
		}
		return;
	}
	if (const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		Flow && (Flow->GetCombatMode() == ECodexCombatMode::TacticalPause || Flow->GetCombatMode() == ECodexCombatMode::TurnBased))
	{
		return; // nothing moves in a tactical pause / the grid fight
	}
	Team.PostureTimer -= DeltaTime;
	if (Posture == EPmcPosture::Hunt && Team.PostureTimer <= 0.f)
	{
		// The freshest belief about the player's men (none before contact today), else the start / radio intel near the squad.
		bool bHasBelief = false;
		FVector Belief = FVector::ZeroVector;
		float Freshest = TNumericLimits<float>::Max();
		FVector SquadCentre = FVector::ZeroVector;
		int32 Living = 0;
		for (AOperativeCharacter* Player : MembersOf(PmcTeam::Player))
		{
			if (!PmcSquadLocal::IsLiveOperative(Player))
			{
				continue;
			}
			SquadCentre += Player->GetActorLocation();
			++Living;
			FVector Location;
			float Since = 0.f;
			// A resumed hunt (2026-11-09): a stale belief is no search point - the radio intel sweep instead.
			if (GetTeamBelief(TeamId, Player, Location, &Since) && Since < Freshest && (Team.HuntResumedAt < 0.0 || Since <= HuntBeliefFreshSeconds))
			{
				bHasBelief = true;
				Belief = Location;
				Freshest = Since;
			}
		}
		SquadCentre = Living > 0 ? SquadCentre / Living : Team.PlayerStart;
		const FVector Wanted = PmcEncounterRules::HuntSearchPoint(bHasBelief, Belief, Team.PostureLeg, Team.PlayerStart, SquadCentre, HuntJitterCm, Team.PostureSeed);
		FVector Point = Wanted;
		ProjectToNav(Wanted, Point);
		Team.AssaultPoint = Point;
		UE_LOG(LogCodexTactics, Display, TEXT("[PmcEncounter] team %d hunts: leg %d -> %s (%s)"), TeamId, Team.PostureLeg, *Point.ToCompactString(),
			bHasBelief ? TEXT("last known") : Team.PostureLeg == 0 ? TEXT("the player start") : TEXT("radio intel"));
		++Team.PostureLeg;
		Team.PostureTimer = HuntLegSeconds;
	}
	else if (Posture == EPmcPosture::Patrol)
	{
		bool bAllIdle = true;
		for (const AOperativeCharacter* Mate : Mates)
		{
			bAllIdle &= !Mate->HasOwnMoveOrder();
		}
		if (bAllIdle && Team.PostureTimer > 4.f && Team.PostureLeg > 0)
		{
			Team.PostureTimer = 4.f; // arrived: a short look round, then the next leg
		}
		if (Team.PostureTimer <= 0.f)
		{
			FVector Point = PmcEncounterRules::PatrolPoint(Team.PostureAnchor, PatrolRadiusCm, Team.PostureSeed, Team.PostureLeg);
			ProjectToNav(Point, Point);
			const FVector Heading = (Point - Mates[0]->GetActorLocation()).GetSafeNormal2D();
			const FVector Right(-Heading.Y, Heading.X, 0.f);
			for (int32 Index = 0; Index < Mates.Num(); ++Index)
			{
				FVector Spot = Point + Right * ((Index - (Mates.Num() - 1) * 0.5f) * 150.f) - Heading * (Index % 2) * 120.f;
				if (!ProjectToNav(Spot, Spot) || FMath::Abs(Spot.Z - Point.Z) > 150.f)
				{
					Spot = Point;
				}
				FSquadOrderContext Context;
				Context.Operative = Mates[Index];
				Context.Location = Spot;
				Context.bSprint = false;
				SquadOrderCatalog::Execute(ESquadOrder::Move, Context);
			}
			++Team.PostureLeg;
			Team.PostureTimer = PatrolLegSeconds;
		}
	}
}

TArray<int32> UPmcSquadSubsystem::GetTeamIds() const
{
	TArray<int32> Out;
	Teams.GetKeys(Out);
	Out.Sort();
	return Out;
}

void UPmcSquadSubsystem::SetTeamAssault(int32 TeamId, const FVector* Point)
{
	FTeam& Team = TeamOf(TeamId);
	if (Point)
	{
		Team.AssaultPoint = *Point;
	}
	else
	{
		Team.AssaultPoint.Reset();
	}
}

void UPmcSquadSubsystem::SetTeamObjective(int32 TeamId, EPmcRole Role, const FVector& Location, float RadiusCm, float LevelToleranceCm)
{
	FTeam& Team = TeamOf(TeamId);
	if (Role == EPmcRole::None || RadiusCm <= 0.f)
	{
		Team.Objective.Reset();
		ClearDefencePosts(Team);
		Team.Defence = FTeam::FDefence();
		return;
	}
	const bool bKeepApproach = Team.Defence.bHasApproach;
	const FVector Approach = Team.Defence.ApproachPoint;
	FPmcObjectiveView Objective;
	Objective.Role = Role;
	Objective.Location = Location;
	Objective.RadiusCm = RadiusCm;
	Objective.LevelToleranceCm = LevelToleranceCm;
	Team.Objective = Objective;
	Team.ObjectiveSetTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (Role == EPmcRole::Defender)
	{
		Team.Defence = FTeam::FDefence();
		Team.Defence.bHasApproach = bKeepApproach;
		Team.Defence.ApproachPoint = Approach;
		// T-08: a new objective is a new round - the fall-back state and the enemies seen start empty.
		Team.EnemiesSeen.Reset();
		for (TPair<TWeakObjectPtr<AOperativeCharacter>, FFighterState>& Pair : Team.Fighters)
		{
			Pair.Value.FallBackPhase = EPmcFallBackPhase::None;
			Pair.Value.FallBackPost = FFiringCandidate();
			Pair.Value.FallBackStartedAt = -1.0;
			Pair.Value.bFallBackHoldLogged = false;
		}
		RefreshDefence(TeamId, Team, true);
	}
	else
	{
		ClearDefencePosts(Team);
		Team.Defence = FTeam::FDefence();
	}
	// Target attention (2026-11-07): the defenders lie in wait with their fire held from the start (the first decision may come after the
	// first sighting; their own Aggressive auto-fire would spring the ambush before the commander could hold it).
	if (Role == EPmcRole::Defender && Team.Weights.AmbushPatience > 0.f)
	{
		for (AOperativeCharacter* Member : MembersOf(TeamId))
		{
			FSquadOrderContext Discipline;
			Discipline.Operative = Member;
			Discipline.Posture = ESquadFirePosture::Passive;
			if (Member && SquadOrderCatalog::Execute(ESquadOrder::FirePosture, Discipline))
			{
				Team.Fighters.FindOrAdd(Member).bHoldingFire = true;
			}
		}
	}
}

void UPmcSquadSubsystem::SetDefenceApproach(int32 TeamId, const FVector& AttackerSpawn)
{
	FTeam& Team = TeamOf(TeamId);
	Team.Defence.ApproachPoint = AttackerSpawn;
	Team.Defence.bHasApproach = true;
	if (Team.Defence.bActive)
	{
		RefreshDefence(TeamId, Team, false);
	}
}

void UPmcSquadSubsystem::UpdateTeamObjective(int32 TeamId, float TimeFrac, float HoldFrac, bool bEnemyOnPoint)
{
	FTeam* Team = Teams.Find(TeamId);
	if (Team && Team->Objective.IsSet())
	{
		Team->Objective->TimeFrac = TimeFrac;
		Team->Objective->HoldFrac = HoldFrac;
		Team->Objective->bEnemyOnPoint = bEnemyOnPoint;
	}
}

bool UPmcSquadSubsystem::HasTeamContact(int32 TeamId) const
{
	const FTeam* Team = FindTeam(TeamId);
	return Team && Team->bEverContact;
}

void UPmcSquadSubsystem::NotifyOperativeDied(AOperativeCharacter* Operative, const FString& AttackerSource)
{
	if (!Operative)
	{
		return;
	}
	// T-08: how a defender died (holder / hammer / falling back / fallen back) - the fall-back statistics read these lines.
	if (FTeam* DeadTeam = Teams.Find(Operative->TeamId); DeadTeam && DeadTeam->Defence.bActive)
	{
		if (const FFighterState* DeadState = DeadTeam->Fighters.Find(Operative))
		{
			const TCHAR* Phase = DeadState->FallBackPhase == EPmcFallBackPhase::Moving ? TEXT("falling back") : (DeadState->FallBackPhase == EPmcFallBackPhase::InZone ? TEXT("fallen back") : TEXT("-"));
			UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: %s died (%s, fall-back %s, %s)"), Operative->TeamId, *Operative->DisplayName.ToString(),
				Operative->SquadIndex == DeadTeam->Defence.HolderIndex ? TEXT("holder") : ((DeadState->bFallBackPlan || DeadState->FallBackPhase != EPmcFallBackPhase::None) ? TEXT("hammer") : TEXT("other")), Phase,
				DeadState->bZonePost ? TEXT("on a zone post") : TEXT("not on a zone post"));
		}
	}
	// The killer's team gets the kill (the attacker source is the shooter's display name).
	for (TPair<int32, FTeam>& Pair : Teams)
	{
		for (AOperativeCharacter* Member : MembersOf(Pair.Key))
		{
			if (Member && Member != Operative && Member->DisplayName.ToString() == AttackerSource)
			{
				++Pair.Value.Stats.Kills;
				break;
			}
		}
	}
	// The last PMC fighter down ends the skirmish like the last monster would.
	if (Operative->TeamId != PmcTeam::Player)
	{
		if (UWaveSubsystem* Waves = GetWorld() ? GetWorld()->GetSubsystem<UWaveSubsystem>() : nullptr)
		{
			Waves->NotifyHostileOperativeDied();
		}
	}
}

// --- Team sight ------------------------------------------------------------------------------------------------------------

void UPmcSquadSubsystem::Tick(float DeltaTime)
{
	if (Teams.IsEmpty())
	{
		return;
	}
	SightTimer -= DeltaTime;
	if (SightTimer <= 0.f)
	{
		SightTimer = SightInterval;
		UpdateSight();
	}
	TArray<int32> TeamIds;
	Teams.GetKeys(TeamIds);
	TeamIds.Sort(); // team decisions must not inherit TMap registration/bucket order
	for (const int32 TeamId : TeamIds)
	{
		FTeam* Team = Teams.Find(TeamId);
		if (!Team)
		{
			continue;
		}
		UpdatePosture(TeamId, *Team, DeltaTime);
		if (!Team->bCommanded)
		{
			continue;
		}
		UpdateTeamRunning(TeamId, *Team, DeltaTime);
		Team->DecisionTimer -= DeltaTime;
		if (Team->DecisionTimer <= 0.f)
		{
			Team->DecisionTimer = Team->Weights.DecisionInterval;
			Decide(TeamId, *Team);
		}
	}
}

void UPmcSquadSubsystem::RefreshSight()
{
	UpdateSight();
}

void UPmcSquadSubsystem::NotifyShotAt(const AActor* Shooter, const AOperativeCharacter* Victim, const FVector* Flash)
{
	UWorld* World = GetWorld();
	FTeam* Team = Victim && Shooter && World ? Teams.Find(Victim->TeamId) : nullptr;
	if (!Team)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	for (auto It = Team->ShotRevealUntil.CreateIterator(); It; ++It)
	{
		if (!It->Key.IsValid() || It->Value < Now)
		{
			Team->ShotRevealFlash.Remove(It->Key);
			It.RemoveCurrent();
		}
	}
	Team->ShotRevealUntil.Add(const_cast<AActor*>(Shooter), Now + SightRules::DemaskSeconds);
	// G008 follow-up: "taking fire" for the defender's contact gate and the sector shift.
	if (FFighterState* VictimState = Team->Fighters.Find(const_cast<AOperativeCharacter*>(Victim)))
	{
		VictimState->LastShotAtTime = Now;
	}
	if (Flash)
	{
		Team->ShotRevealFlash.Add(const_cast<AActor*>(Shooter), *Flash);
	}
}

void UPmcSquadSubsystem::UpdateSight()
{
	UWorld* World = GetWorld();
	const UTacticalSightSubsystem* Sight = World ? World->GetSubsystem<UTacticalSightSubsystem>() : nullptr;
	const bool bRulesActive = Sight && Sight->IsSquadSightActive();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	const FPmcOperatorConfig& Config = PmcOperatorRules::Get();

	// Every living operative of every team (the player's squad included).
	TArray<TPair<int32, AOperativeCharacter*>> Everyone;
	for (const TPair<int32, FTeam>& Pair : Teams)
	{
		for (AOperativeCharacter* Member : MembersOf(Pair.Key))
		{
			if (PmcSquadLocal::IsLiveOperative(Member))
			{
				Everyone.Add({ Pair.Key, Member });
			}
		}
	}

	for (TPair<int32, FTeam>& Pair : Teams)
	{
		FTeam& Team = Pair.Value;
		TArray<AOperativeCharacter*> Observers;
		for (const TPair<int32, AOperativeCharacter*>& Entry : Everyone)
		{
			if (Entry.Key == Pair.Key)
			{
				Observers.Add(Entry.Value);
			}
		}
		for (TPair<TWeakObjectPtr<AActor>, FKnownTarget>& KnownPair : Team.Known)
		{
			KnownPair.Value.bSeenNow = false;
		}
		auto Observe = [&](AActor* Target, const FVector& Profile, bool bAllRound)
		{
			// A shooter demasks himself - to the teams within earshot of the shot (2026-11-03: was the whole map, so one shot at a
			// monster told every PMC squad of the level where the player's men stood). Bug #43: a shooter who fired at one of us is
			// noticed by his flash whatever the range (NotifyShotAt).
			bool bHeard = false;
			if (bRulesActive && Sight && Sight->IsDemasked(Target))
			{
				for (const AOperativeCharacter* Observer : Observers)
				{
					bHeard |= FVector::Dist2D(Observer->GetActorLocation(), Target->GetActorLocation()) <= PmcSquadLocal::GunfireHearingCm;
				}
			}
			if (const double* Until = bRulesActive && !bHeard ? Team.ShotRevealUntil.Find(Target) : nullptr)
			{
				bHeard = *Until > Now;
			}
			bool bEye = !bRulesActive;
			for (AOperativeCharacter* Observer : Observers)
			{
				if (bEye)
				{
					break;
				}
				// A stealth operative must be within the reduced range and visible from this observer.
				AOperativeCharacter* TargetOperative = Cast<AOperativeCharacter>(Target);
				const float Distance = FVector::Dist2D(Observer->GetActorLocation(), Target->GetActorLocation());
				const bool bStealth = TargetOperative && TargetOperative->IsInStealthMode();
				const float Range = Sight->GetSightRangeCm(*Observer) * StealthModeRules::DetectionRangeScale(bStealth, TargetOperative && TargetOperative->IsStealthFast(), StealthModeRules::GetConfig());
				const bool bHidden = TargetOperative && TargetOperative->IsHiddenInCoverFrom(Observer->GetActorLocation());
				bEye = !bHidden && (!bStealth || Distance <= Range)
					&& ((SightRules::IsWithinTouchReveal(Distance) && Sight->HasClearSight(UTacticalSightSubsystem::EyePoint(*Observer), Profile))
						|| (bAllRound ? Sight->HasClearSight(UTacticalSightSubsystem::EyePoint(*Observer), Profile) : Sight->CanOperativeSee(*Observer, Profile)));
				if (bEye && TargetOperative)
				{
					TargetOperative->SetStealthMode(false);
				}
			}
			// Honest sight (fix/pmc-fire-through-wall 2026-11-08, the designer's prone fighter firing into the squad's wall): a heard shot / a
			// flash is SEEN only with a clear line from one of their eyes to the shooter or his muzzle; else the team knows his spot (contact,
			// the hunt, the commander's moves) but nobody may take him as a target. Codex.Combat.HonestFire 0 = the old "heard = seen".
			bool bLine = false;
			if (Sight && bHeard && !bEye && AOperativeCharacter::IsHonestFireOn())
			{
				const AOperativeCharacter* Shooter = Cast<AOperativeCharacter>(Target);
				for (const AOperativeCharacter* Observer : Observers)
				{
					const FVector Eye = UTacticalSightSubsystem::EyePoint(*Observer);
					// The flash of his shot at one of us (a hand short of it, toward the eye: the muzzle may poke into the corner it fires round,
					// as the return fire's line), his muzzle now, or himself.
					const FVector* Flash = Team.ShotRevealFlash.Find(Target);
					auto Clear = [&](const FVector& Point) { return Sight->HasClearSight(Eye, Point + (Eye - Point).GetSafeNormal() * 15.f); };
					if (Sight->HasClearSight(Eye, Profile) || (Flash && Clear(*Flash)) || (Shooter && Clear(Shooter->GetWeaponMuzzleLocation())))
					{
						bLine = true;
						break;
					}
				}
			}
			const SightRules::EContact Contact = AOperativeCharacter::IsHonestFireOn() ? SightRules::ResolveContact(bEye, bHeard, bLine)
				: (bEye || bHeard ? SightRules::EContact::Seen : SightRules::EContact::None);
			if (Contact != SightRules::EContact::None)
			{
				FKnownTarget* Previous = Team.Known.Find(Target);
				const FVector* Flash = Team.ShotRevealFlash.Find(Target);
				const double* RevealUntil = Team.ShotRevealUntil.Find(Target);
				const bool bNewFlash = Flash && RevealUntil && *RevealUntil > Now
					&& (!Previous || *RevealUntil - SightRules::DemaskSeconds > Previous->LastSeenTime);
				const FVector ContactLocation = bEye ? Target->GetActorLocation() : (bNewFlash ? *Flash : (Previous ? Previous->LastKnown : Target->GetActorLocation()));
				FKnownTarget& Known = Team.Known.FindOrAdd(Target);
				Known.Actor = Target;
				Known.LastKnown = ContactLocation;
				if (Cast<AOperativeCharacter>(Target))
				{
					Team.EnemiesSeen.Add(Target); // T-08: Known forgets the dead; the defence counts the attackers it saw go down
				}
				const AOperativeCharacter* TargetOperative = Cast<AOperativeCharacter>(Target);
				Known.bSeenNow = bEye || (Contact == SightRules::EContact::Seen && (!TargetOperative || !TargetOperative->IsInStealthMode()));
				if (Known.bSeenNow || !Previous)
				{
					Known.LastSeenTime = Now;
				}
				Team.LastContactTime = Now;
				if (Known.bSeenNow)
				{
					Team.LastSeenContactTime = Now;
				}
				Team.bEverContact = true;
				if (Team.FirstContactTime < 0.0)
				{
					Team.FirstContactTime = Now;
				}
			}
		};
		if (!Observers.IsEmpty())
		{
			for (const TPair<int32, AOperativeCharacter*>& Entry : Everyone)
			{
				if (PmcTeam::AreHostile(Entry.Key, Pair.Key))
				{
					Observe(Entry.Value, UTacticalSightSubsystem::ProfilePoint(*Entry.Value), false);
				}
			}
			// Hostile teams: the monsters hunting a mate within the self-defence distance.
			if (Pair.Key != PmcTeam::Player)
			{
				for (AEnemyCharacter* Monster : UEnemyRegistrySubsystem::GetEnemyCharacters(World))
				{
					const UHealthComponent* MonsterHealth = IsValid(Monster) ? Monster->FindComponentByClass<UHealthComponent>() : nullptr;
					if (!IsValid(Monster) || Monster->IsDying() || !MonsterHealth || !MonsterHealth->IsAlive())
					{
						continue;
					}
					const AActor* Hunted = Monster->GetCurrentTarget();
					bool bHuntsUs = false;
					for (AOperativeCharacter* Observer : Observers)
					{
						bHuntsUs |= Hunted == Observer && FVector::Dist2D(Observer->GetActorLocation(), Monster->GetActorLocation()) <= Config.SelfDefenceCm;
					}
					if (bHuntsUs)
					{
						Observe(Monster, UTacticalSightSubsystem::ProfilePoint(*Monster), true);
					}
				}
			}
		}
		// Continue a recent automatic engagement at the last seen point, without following the hidden actor.
		if (Pair.Key != PmcTeam::Player)
		{
			for (AOperativeCharacter* Observer : Observers)
			{
				FFighterState& FighterState = Team.Fighters.FindOrAdd(Observer);
				if (FighterState.LostFireUntil >= 0.0)
				{
					AActor* Lost = FighterState.LostFireTarget.Get();
					const FKnownTarget* Known = Lost ? Team.Known.Find(Lost) : nullptr;
					const UHealthComponent* Health = Lost ? Lost->FindComponentByClass<UHealthComponent>() : nullptr;
					const bool bOurOrder = Observer->HasSuppressOrder() && !Observer->GetSuppressTarget()
						&& Observer->GetSuppressPoint().Equals(FighterState.LostFirePoint, 1.f);
					if (!bOurOrder || !Lost || (Health && !Health->IsAlive()) || (Known && Known->bSeenNow) || Now >= FighterState.LostFireUntil)
					{
						if (bOurOrder) { Observer->CancelSuppressFire(TEXT("lost sight memory ended")); }
						FighterState.LostFireTarget.Reset();
						FighterState.LostFireUntil = -1.0;
						if (FighterState.Order == EPmcOrder::Suppress)
						{
							FighterState.Order = EPmcOrder::Hold;
							FighterState.OrderSince = Now;
						}
					}
				}
				AActor* Recent = FighterState.RecentFireTarget.Get();
				const FKnownTarget* Known = Recent ? Team.Known.Find(Recent) : nullptr;
				if (Known && !Known->bSeenNow && FighterState.LostFireUntil < 0.0)
				{
					const FVector Point = Known->LastKnown;
					const bool bRecent = Now - Observer->GetLastShotTime() <= Team.Weights.ContactMemorySeconds
						&& Now - Known->LastSeenTime <= Team.Weights.ContactMemorySeconds;
					if (bRecent && !Observer->HasSuppressOrder()
						&& Observer->HasUsefulFireLine(Point + FVector(0.f, 0.f, SuppressionRules::GetConfig().OrderAimHeightCm))
						&& Observer->OrderSuppress(nullptr, Point))
					{
						FighterState.Order = EPmcOrder::Suppress;
						FighterState.OrderSince = Now;
						FighterState.Goal = Point;
						FighterState.bWalkPending = false;
						FighterState.LostFireTarget = Recent;
						FighterState.LostFirePoint = Point;
						FighterState.LostFireUntil = Known->LastSeenTime + Team.Weights.ContactMemorySeconds;
						OnFighterOrder.Broadcast(Pair.Key, Observer, EPmcOrder::Suppress, Team.Weights.Suppress, EPmcPickReason::Best);
						UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: %s -> Suppress (lost sight memory at %s)"), Pair.Key,
							*Observer->DisplayName.ToString(), *Point.ToCompactString());
					}
					FighterState.RecentFireTarget.Reset();
				}
			}
		}
		// Forget the dead and the destroyed.
		for (auto It = Team.Known.CreateIterator(); It; ++It)
		{
			AActor* Actor = It->Value.Actor.Get();
			const UHealthComponent* Health = Actor ? Actor->FindComponentByClass<UHealthComponent>() : nullptr;
			if (!Actor || (Health && !Health->IsAlive()))
			{
				It.RemoveCurrent();
			}
		}
	}
}

bool UPmcSquadSubsystem::IsVisibleToTeam(int32 TeamId, const AActor* Target) const
{
	const UTacticalSightSubsystem* Sight = GetWorld() ? GetWorld()->GetSubsystem<UTacticalSightSubsystem>() : nullptr;
	if (!Sight || !Sight->IsSquadSightActive())
	{
		return true;
	}
	const FTeam* Team = FindTeam(TeamId);
	const FKnownTarget* Known = Team ? Team->Known.Find(const_cast<AActor*>(Target)) : nullptr;
	return Known && Known->bSeenNow;
}

bool UPmcSquadSubsystem::GetTeamBelief(int32 TeamId, const AActor* Target, FVector& OutLocation, float* OutSecondsSince) const
{
	const FTeam* Team = FindTeam(TeamId);
	const FKnownTarget* Known = Team ? Team->Known.Find(const_cast<AActor*>(Target)) : nullptr;
	if (!Known)
	{
		return false;
	}
	OutLocation = Known->LastKnown;
	if (OutSecondsSince)
	{
		*OutSecondsSince = GetWorld() ? static_cast<float>(GetWorld()->GetTimeSeconds() - Known->LastSeenTime) : 0.f;
	}
	return true;
}

TArray<AActor*> UPmcSquadSubsystem::GetTargetsFor(const AOperativeCharacter& Shooter) const
{
	TArray<AActor*> Out;
	const FTeam* Team = FindTeam(Shooter.TeamId);
	if (!Team)
	{
		return Out;
	}
	for (const TPair<TWeakObjectPtr<AActor>, FKnownTarget>& Pair : Team->Known)
	{
		if (AActor* Actor = Pair.Value.Actor.Get(); Actor && Pair.Value.bSeenNow)
		{
			Out.Add(Actor);
		}
	}
	return Out;
}

bool UPmcSquadSubsystem::IsHostileTo(const AOperativeCharacter& Shooter, const AActor* Actor) const
{
	if (const AOperativeCharacter* Other = Cast<AOperativeCharacter>(Actor))
	{
		return PmcTeam::AreHostile(Shooter.TeamId, Other->TeamId);
	}
	if (Shooter.TeamId == PmcTeam::Player)
	{
		return true; // the player's men: anything tagged "Enemy" (the caller checked the tag)
	}
	const FTeam* Team = FindTeam(Shooter.TeamId);
	return Team && Team->Known.Contains(const_cast<AActor*>(Actor));
}

bool UPmcSquadSubsystem::IsExposedTo(int32 TeamId, const AOperativeCharacter& Fighter) const
{
	for (const TPair<int32, FTeam>& Pair : Teams)
	{
		if (PmcTeam::AreHostile(Pair.Key, TeamId))
		{
			if (const FKnownTarget* Known = Pair.Value.Known.Find(const_cast<AOperativeCharacter*>(&Fighter)); Known && Known->bSeenNow)
			{
				return true;
			}
		}
	}
	return false;
}

// --- Commander -------------------------------------------------------------------------------------------------------------

void UPmcSquadSubsystem::SetTeamCommanded(int32 TeamId, bool bCommanded, const FPmcWeights* Weights)
{
	FTeam& Team = TeamOf(TeamId);
	Team.bCommanded = bCommanded;
	Team.DecisionTimer = 0.f;
	if (Weights)
	{
		Team.Weights = *Weights;
	}
}

bool UPmcSquadSubsystem::IsTeamCommanded(int32 TeamId) const
{
	const FTeam* Team = FindTeam(TeamId);
	return Team && Team->bCommanded;
}

void UPmcSquadSubsystem::SetTeamWeights(int32 TeamId, const FPmcWeights& Weights)
{
	TeamOf(TeamId).Weights = Weights;
}

const FPmcWeights* UPmcSquadSubsystem::GetTeamWeights(int32 TeamId) const
{
	const FTeam* Team = FindTeam(TeamId);
	return Team ? &Team->Weights : nullptr;
}

const FPmcTeamStats* UPmcSquadSubsystem::GetTeamStats(int32 TeamId) const
{
	const FTeam* Team = FindTeam(TeamId);
	return Team ? &Team->Stats : nullptr;
}

void UPmcSquadSubsystem::ResetTeam(int32 TeamId)
{
	FTeam& Team = TeamOf(TeamId);
	Team.Known.Reset();
	Team.EnemiesSeen.Reset();
	Team.Fighters.Reset();
	Team.Stats = FPmcTeamStats();
	Team.bEverContact = false;
	Team.LastContactTime = -1e9;
	Team.FirstContactTime = -1.0;
	Team.bShotsExchanged = false;
	Team.LastShotTime = -1e9;
	Team.DecisionTimer = 0.f;
	Team.AssaultPoint.Reset();
	Team.Objective.Reset();
	Team.ObjectiveSetTime = -1e9;
	Team.Posture.Reset();
	Team.Route.Reset();
}

EPmcOrder UPmcSquadSubsystem::GetFighterOrder(const AOperativeCharacter* Fighter) const
{
	const FTeam* Team = Fighter ? FindTeam(Fighter->TeamId) : nullptr;
	const FFighterState* State = Team ? Team->Fighters.Find(const_cast<AOperativeCharacter*>(Fighter)) : nullptr;
	return State ? State->Order : EPmcOrder::Hold;
}

FVector UPmcSquadSubsystem::GetFighterGoal(const AOperativeCharacter* Fighter) const
{
	const FTeam* Team = Fighter ? FindTeam(Fighter->TeamId) : nullptr;
	const FFighterState* State = Team ? Team->Fighters.Find(const_cast<AOperativeCharacter*>(Fighter)) : nullptr;
	return State ? State->Goal : FVector::ZeroVector;
}

void UPmcSquadSubsystem::RunDecisions(int32 TeamId)
{
	UpdateSight();
	Decide(TeamId, TeamOf(TeamId));
}

bool UPmcSquadSubsystem::IsOrderRunning(const AOperativeCharacter& Fighter, const FFighterState& State) const
{
	switch (State.Order)
	{
	case EPmcOrder::Advance:
	case EPmcOrder::Flank:
	case EPmcOrder::FallBack:
	case EPmcOrder::Relocate:
	case EPmcOrder::TakeCover:
	case EPmcOrder::Objective:
		return Fighter.HasOwnMoveOrder() || Fighter.HasPendingCover() || PmcSquadLocal::IsTraversingLink(Fighter);
	case EPmcOrder::Suppress:
		return Fighter.HasSuppressOrder();
	case EPmcOrder::Heal:
	{
		const bool bPatientValid = State.Patient.IsValid() && PmcSquadLocal::IsLiveOperative(State.Patient.Get())
			&& PmcSquadLocal::HealthFractionOf(*State.Patient) < 1.f && Fighter.MedkitsCount > 0;
		if (!bPatientValid)
		{
			return false;
		}
		// A heal approach is running only while moving towards the patient.
		return Fighter.HasOwnMoveOrder() || PmcSquadLocal::IsTraversingLink(Fighter);
	}
	case EPmcOrder::Reload:
		return Fighter.bIsReloading;
	case EPmcOrder::RpgStrike:
		return Fighter.HasLauncherStrikeOrder() || Fighter.IsLauncherDrawnByRule();
	default:
		return false;
	}
}

void UPmcSquadSubsystem::UpdateTeamRunning(int32 TeamId, FTeam& Team, float DeltaTime)
{
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	for (TPair<TWeakObjectPtr<AOperativeCharacter>, FFighterState>& Pair : Team.Fighters)
	{
		AOperativeCharacter* Fighter = Pair.Key.Get();
		FFighterState& State = Pair.Value;
		if (!PmcSquadLocal::IsLiveOperative(Fighter))
		{
			continue;
		}
		// Suppression time, flank arrival, stuck walks.
		if (Fighter->HasSuppressOrder())
		{
			Team.Stats.SuppressionSeconds += DeltaTime;
		}
		const FVector Location = Fighter->GetActorLocation();
		// fix/pmc-onpoint-3d-detour: the arrival tests compare his FEET with the navmesh goal on its level (a roof goal is not reached from
		// the ground under it).
		const FVector Feet = PmcSquadLocal::FeetOf(*Fighter);
		if (State.Order == EPmcOrder::Flank && !State.bFlankCounted
			&& !PmcCommanderRules::IsWalkShort(Feet, State.Goal, PmcSquadLocal::ArriveCm, Team.Weights.LevelToleranceCm))
		{
			State.bFlankCounted = true;
			++Team.Stats.FlanksSucceeded;
			UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: %s reached his flank point"), TeamId, *Fighter->DisplayName.ToString());
		}
		const bool bWalking = State.Order == EPmcOrder::Advance || State.Order == EPmcOrder::Flank || State.Order == EPmcOrder::FallBack
			|| State.Order == EPmcOrder::Relocate || State.Order == EPmcOrder::TakeCover || State.Order == EPmcOrder::Objective;
		if (bWalking && PmcSquadLocal::IsTraversingLink(*Fighter))
		{
			// Climbing / dropping is progress (vertical); the stall clock restarts after it.
			State.ProgressPoint = Location;
			State.ProgressTime = Now;
		}
		else if (bWalking && Fighter->HasOwnMoveOrder())
		{
			if (FVector::Dist2D(Location, State.ProgressPoint) >= PmcSquadLocal::StuckProgressCm)
			{
				State.ProgressPoint = Location;
				State.ProgressTime = Now;
			}
			else if (Now - State.ProgressTime > Team.Weights.RepathAfterSeconds)
			{
				// fix/pmc-arena-stalemate (the designer 2026-11-08): no progress for 5 s -> another route (a detour waypoint), not standing
				// still. fix/pmc-onpoint-3d-detour (Gemini review 009): a side waypoint, then the other side, then a short step back along
				// the way he came (each step tried at once when the one before finds no waypoint); none left -> the order failed
				// (PickCommitted keeps it out for its cooldown).
				++Team.Stats.StuckEvents;
				bool bDetoured = false;
				for (EPmcDetourStep Step = PmcCommanderRules::NextDetourStep(State.DetourAttempts, State.DetourSide, Team.Weights.DetourMax);
					Step != EPmcDetourStep::None && !bDetoured;
					Step = PmcCommanderRules::NextDetourStep(State.DetourAttempts, State.DetourSide, Team.Weights.DetourMax))
				{
					int32 Side = 0;
					bDetoured = TryDetour(*Fighter, State, Team.Weights, Step, Side);
					++State.DetourAttempts;
					if (Step == EPmcDetourStep::Side)
					{
						State.DetourSide = bDetoured ? Side : 0;
					}
					if (bDetoured)
					{
						++State.Detours;
						++Team.Stats.Detours;
						UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: %s made no progress on %s for %.0f s - a detour (%s)"), TeamId,
							*Fighter->DisplayName.ToString(), PmcCommanderRules::OrderName(State.Order), Team.Weights.RepathAfterSeconds,
							PmcCommanderRules::DetourStepName(Step));
					}
				}
				if (!bDetoured)
				{
					Fighter->StopOperative();
					MarkFailed(TeamId, Team, *Fighter, State, State.Order, TEXT("stuck (no progress, no detour)"));
					State.Order = EPmcOrder::Hold;
				}
				State.ProgressPoint = Location;
				State.ProgressTime = Now;
			}
		}
		// A rocket order that ended (fired, or refused by the launcher rule): the order cools down like a failed one - the smoke's gunner
		// re-took RpgStrike 18 times in a minute.
		if (State.bStrikePending && !IsOrderRunning(*Fighter, State))
		{
			State.bStrikePending = false;
			State.FailedUntil.Add(EPmcOrder::RpgStrike, Now + Team.Weights.FailedOrderCooldownSeconds);
		}
		// A walk that ended short of its goal (no path / dropped on the way): failed - not re-decided twice a second (pmc-v3 2026-11-08).
		if (State.bWalkPending && !IsOrderRunning(*Fighter, State))
		{
			State.bWalkPending = false;
			const bool bFailedShort = bWalking && !State.Goal.IsNearlyZero()
				&& PmcCommanderRules::IsWalkShort(Feet, State.Goal, PmcSquadLocal::FailShortCm, Team.Weights.LevelToleranceCm);
			if (bFailedShort)
			{
				MarkFailed(TeamId, Team, *Fighter, State, State.Order, TEXT("ended short of its goal"));
			}
			else
			{
				// Succeeded walk order: reported DONE to commander -> transition to Hold.
				if (State.Order == EPmcOrder::TakeCover || State.Order == EPmcOrder::Relocate)
				{
					const EPmcOrder CompletedOrder = State.Order;
					if (Fighter->bInCover)
					{
						UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: %s completed %s in cover -> Hold"), TeamId,
							*Fighter->DisplayName.ToString(), PmcCommanderRules::OrderName(CompletedOrder));
						State.Order = EPmcOrder::Hold;
						State.OrderSince = Now;
						State.FailedUntil.Add(CompletedOrder, Now + Team.Weights.CompletedOrderCooldownSeconds);
					}
					else
					{
						UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: %s ended %s out of cover (interrupted approach) -> Hold with cooldown"), TeamId,
							*Fighter->DisplayName.ToString(), PmcCommanderRules::OrderName(CompletedOrder));
						State.Order = EPmcOrder::Hold;
						State.OrderSince = Now;
						State.FailedUntil.Add(CompletedOrder, Now + Team.Weights.CoverInterruptedCooldownSeconds);
					}
				}
				else if (State.Order == EPmcOrder::FallBack)
				{
					UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: %s completed FallBack at %s -> Hold"), TeamId,
						*Fighter->DisplayName.ToString(), *State.Goal.ToCompactString());
					State.Order = EPmcOrder::Hold;
					State.OrderSince = Now;
					State.FailedUntil.Add(EPmcOrder::FallBack, Now + Team.Weights.CompletedOrderCooldownSeconds);
				}
				else if (State.Order == EPmcOrder::Heal)
				{
					const float Dist = State.Patient.IsValid() ? FVector::Dist2D(Feet, State.Patient->GetActorLocation()) : 99999.f;
					if (Dist <= Team.Weights.HealReachCm && Fighter->MedkitsCount > 0 && State.Patient.IsValid()
						&& PmcSquadLocal::IsLiveOperative(State.Patient.Get()) && PmcSquadLocal::HealthFractionOf(*State.Patient) < 1.f)
					{
						FSquadOrderContext Context;
						Context.Operative = Fighter;
						Context.Patient = State.Patient.Get();
						if (SquadOrderCatalog::Execute(ESquadOrder::Heal, Context))
						{
							Team.Stats.Heals += 1;
							UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: %s arrived and healed %s -> Hold"), TeamId,
								*Fighter->DisplayName.ToString(), *State.Patient->DisplayName.ToString());
						}
						State.Order = EPmcOrder::Hold;
						State.OrderSince = Now;
						State.FailedUntil.Add(EPmcOrder::Heal, Now + Team.Weights.CompletedOrderCooldownSeconds);
					}
					else
					{
						UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: %s heal approach ended: patient out of reach (%.0f cm > %.0f cm) -> cancel"), TeamId,
							*Fighter->DisplayName.ToString(), Dist, Team.Weights.HealReachCm);
						State.Order = EPmcOrder::Hold;
						State.OrderSince = Now;
						State.FailedUntil.Add(EPmcOrder::Heal, Now + Team.Weights.FailedOrderCooldownSeconds);
					}
				}
				else if (State.Order == EPmcOrder::Advance)
				{
					UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: %s completed Advance at %s -> Hold"), TeamId,
						*Fighter->DisplayName.ToString(), *State.Goal.ToCompactString());
					State.Order = EPmcOrder::Hold;
					State.OrderSince = Now;
					State.FailedUntil.Add(EPmcOrder::Advance, Now + Team.Weights.CompletedOrderCooldownSeconds);
				}
				else if (State.Order == EPmcOrder::Objective)
				{
					UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: %s arrived at objective point -> Hold"), TeamId,
						*Fighter->DisplayName.ToString());
					State.Order = EPmcOrder::Hold;
					State.OrderSince = Now;
				}

				// Re-engagement: after a move ends, re-target any known threat and restore Aggressive posture
				if (AActor* Target = State.Target.Get())
				{
					if (Fighter->GetManualPriorityTarget() != Target)
					{
						FSquadOrderContext TargetContext;
						TargetContext.Operative = Fighter;
						TargetContext.Target = Target;
						SquadOrderCatalog::Execute(ESquadOrder::PriorityTarget, TargetContext);
					}
				}
				if (State.bHoldingFire)
				{
					State.bHoldingFire = false;
					FSquadOrderContext Discipline;
					Discipline.Operative = Fighter;
					Discipline.Posture = ESquadFirePosture::Aggressive;
					SquadOrderCatalog::Execute(ESquadOrder::FirePosture, Discipline);
				}
			}
		}
		// Exposure at one spot.
		if (FVector::Dist2D(Location, State.Spot) > 150.f)
		{
			State.Spot = Location;
			State.SpotSince = Now;
			State.ExposedSince = -1.0;
		}
		if (IsExposedTo(TeamId, *Fighter))
		{
			if (State.ExposedSince < 0.0)
			{
				State.ExposedSince = Now;
			}
		}
		else
		{
			State.ExposedSince = -1.0;
		}
	}
}

FPmcFighterView UPmcSquadSubsystem::BuildFighterView(int32 TeamId, const FTeam& Team, AOperativeCharacter& Fighter, FFighterState& State,
	const TArray<AOperativeCharacter*>& Mates, FCoverSlot& OutCover, float& OutCoverQuality, AActor*& OutThreatActor) const
{
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	FPmcFighterView View;
	View.Location = Fighter.GetActorLocation();
	View.HealthFrac = PmcSquadLocal::HealthFractionOf(Fighter);
	const int32 ClipSize = Fighter.CurrentWeapon ? FMath::Max(1, Fighter.CurrentWeapon->Stats.Ammo.MagazineSize) : 30;
	const int32 Reserve = Fighter.CurrentWeapon ? FMath::Max(1, Fighter.CurrentWeapon->Stats.Ammo.ReserveAmmo) : 60;
	View.ClipFrac = Fighter.UsesAmmo() ? FMath::Clamp(static_cast<float>(Fighter.CurrentClip) / ClipSize, 0.f, 1.f) : 1.f;
	View.AmmoFrac = Fighter.UsesAmmo() ? FMath::Clamp(static_cast<float>(Fighter.CurrentClip + Fighter.ReserveAmmo) / (ClipSize + Reserve), 0.f, 1.f) : 1.f;
	View.bHasReserve = Fighter.ReserveAmmo > 0;
	// The RPG gunner (2026-11-09): a launcher in hands is no rifle - its one-rocket tube must not read as an empty clip (Reload / Sidearm).
	if (Fighter.IsLauncherEquipped())
	{
		View.ClipFrac = 1.f;
		View.AmmoFrac = 1.f;
		View.bHasReserve = true;
	}
	View.bHasLauncher = Fighter.HasLauncherRockets();
	State.LauncherTarget.Reset();
	if (View.bHasLauncher)
	{
		FLauncherChoice Choice;
		if (AActor* Rocket = Fighter.FindBestLauncherTarget(LauncherTacticsRules::GetConfig().OrderedThreshold, Choice))
		{
			View.LauncherScore = Choice.Score;
			State.LauncherTarget = Rocket;
		}
	}
	View.bInCover = Fighter.bInCover;
	View.bExposed = State.ExposedSince >= 0.0;
	View.bUnderFire = Now - State.LastShotAtTime <= PmcSquadLocal::UnderFireSeconds;
	View.bSuppressed = Fighter.GetSuppressionTier() != ESuppressionTier::None;
	View.SecondsExposedHere = View.bExposed ? static_cast<float>(Now - State.ExposedSince) : 0.f;
	View.Stance = Fighter.GetStance();
	View.bMoving = Fighter.GetVelocity().SizeSquared2D() > 100.f || Fighter.HasOwnMoveOrder();
	View.bHasMedkit = Fighter.MedkitsCount > 0;
	View.bHasSidearm = PmcSquadLocal::HasWeaponId(Fighter, TEXT("pistol")) && Fighter.CurrentWeapon && Fighter.CurrentWeapon->WeaponId != TEXT("pistol");
	View.bKnockedDown = Fighter.IsKnockedDown();
	View.WeaponRangeCm = Fighter.CurrentWeapon ? Fighter.CurrentWeapon->Stats.Range.MaxRangeCm : 1400.f;
	// 2026-11-04: the distance his weapon wants to fight from (riflemen keep their distance, the shotgun closes in).
	View.Band = PmcCommanderRules::ResolveBand(Fighter.CurrentWeapon ? Fighter.CurrentWeapon->WeaponId : FString(), View.WeaponRangeCm, Team.Weights);

	// The nearest known threat.
	OutThreatActor = nullptr;
	float BestDistance = TNumericLimits<float>::Max();
	const AActor* CurrentPriority = Fighter.GetManualPriorityTarget();
	const float ThreatHysteresis = Team.Weights.TargetSwitchHysteresisCm > 0.f ? Team.Weights.TargetSwitchHysteresisCm : 200.f;
	for (const TPair<TWeakObjectPtr<AActor>, FKnownTarget>& Pair : Team.Known)
	{
		AActor* Actor = Pair.Value.Actor.Get();
		if (!Actor)
		{
			continue;
		}
		const float RealDistance = FVector::Dist2D(View.Location, Pair.Value.LastKnown);
		const float EffectiveDistance = (Actor == CurrentPriority) ? FMath::Max(0.f, RealDistance - ThreatHysteresis) : RealDistance;
		if (EffectiveDistance < BestDistance || (EffectiveDistance == BestDistance && OutThreatActor
			&& PmcSquadLocal::StableActorId(Actor).Compare(PmcSquadLocal::StableActorId(OutThreatActor), ESearchCase::CaseSensitive) < 0))
		{
			BestDistance = EffectiveDistance;
			OutThreatActor = Actor;
			View.bHasThreat = true;
			View.Threat.Location = Pair.Value.LastKnown;
			View.Threat.DistanceCm = RealDistance;
			View.Threat.SecondsSince = static_cast<float>(Now - Pair.Value.LastSeenTime);
			View.Threat.bSeenNow = Pair.Value.bSeenNow;
		}
	}
	// The line of fire to the threat's spot from his muzzle at his stance now (2026-11-08): no suppress order into a wall.
	if (View.bHasThreat && OutThreatActor)
	{
		View.bFireLine = Fighter.HasUsefulFireLine(View.Threat.Location + FVector(0.f, 0.f, SuppressionRules::GetConfig().OrderAimHeightCm));
	}
	// Target attention (2026-11-07): how far he stands outside the look sectors of the enemies his team sees now.
	{
		TArray<FVector2D> Positions;
		TArray<float> Yaws;
		SeenWatchers(Team, Positions, Yaws);
		View.Threat.AttentionOff = Positions.IsEmpty() ? 0.5f : PmcCommanderRules::AttentionOff(Positions, Yaws, FVector2D(View.Location));
	}
	// The assault objective stands in for a threat while none is known (the arena's attacking side walks up to the enemy).
	// A hunt (2026-11-09: also the one resumed after losing the player's men) walks to its search point - a remembered threat it no longer sees
	// does not hold it in cover.
	if (View.bHasThreat && !View.Threat.bSeenNow && Team.AssaultPoint.IsSet() && Team.Posture.IsSet() && Team.Posture.GetValue() == EPmcPosture::Hunt)
	{
		View.bHasThreat = false;
		OutThreatActor = nullptr;
	}
	if (!View.bHasThreat && Team.AssaultPoint.IsSet())
	{
		View.bHasThreat = true;
		View.Threat.Location = Team.AssaultPoint.GetValue();
		View.Threat.DistanceCm = FVector::Dist2D(View.Location, View.Threat.Location);
		View.Threat.SecondsSince = 999.f;
		View.Threat.bSeenNow = false;
		View.Threat.bStandIn = true;
	}
	// The tactical map (TACTICAL_MAP.md P2): the danger of his spot for the PMC (inside a chokepoint) and his post (0 / none without a file).
	if (const UTacticalMapSubsystem* Map = GetTacticalMap())
	{
		const FVector Feet = PmcSquadLocal::FeetOf(Fighter);
		View.DangerHere = Map->DangerAt(Feet, ETacticalSide::Pmc).Danger;
		if (State.Post.IsValid())
		{
			View.PostKind = State.Post.Kind;
			View.PostScore = State.Post.Score / PmcTacticalRules::PostKindFactor(State.Post.Kind, Team.Weights);
			View.bAtPost = PmcTacticalRules::IsAtPost(Feet, State.Post.Location, PostArriveCm, Team.Weights.LevelToleranceCm);
		}
	}
	// The round objective (the arena, task #53): his distance to the point.
	if (Team.Objective.IsSet())
	{
		const FVector Feet = PmcSquadLocal::FeetOf(Fighter);
		View.Objective = Team.Objective.GetValue();
		View.Objective.DistanceCm = PmcCommanderRules::ObjectiveDistanceCm(Feet, View.Objective);
		View.Objective.bOnObjectiveLevel = PmcCommanderRules::IsSameLevel(static_cast<float>(Feet.Z), static_cast<float>(View.Objective.Location.Z),
			View.Objective.LevelToleranceCm);
		if (Team.Defence.bActive)
		{
			FPmcDefenceConfig Config;
			Config.GuardDistCm = Team.Weights.GuardDistCm;
			Config.GuardInsetCm = Team.Weights.GuardInsetCm;
			Config.MinSpacingCm = Team.Weights.MinSpacingCm;
			Config.SpacingPenalty = Team.Weights.SpacingPenalty;
			Config.AreaFactor = Team.Weights.AreaFactor;
			Config.ShiftAfterSeconds = Team.Weights.ShiftAfterSeconds;
			Config.SectorArcDeg = Team.Weights.SectorArcDeg;
			Config.LevelToleranceCm = View.Objective.LevelToleranceCm;
			View.Objective.bDefenceAreaSet = true;
			View.Objective.bInsideDefenceArea = PmcDefenceRules::IsInsideDefendedArea(Feet, View.Objective.Location, Team.Defence.LevelZ,
				View.Objective.RadiusCm, Team.Defence.bRoof ? Team.Defence.Spots : TArray<FPmcDefenceSpot>(), Config);
			View.bDefenceOccupier = Fighter.SquadIndex == Team.Defence.OccupierIndex;
			View.bZonePost = State.bZonePost;
			View.bFallingBack = State.bFallingBack;
			if (State.bHasDefencePost)
			{
				View.bHasDefencePost = true;
				// The defence post, not a tactical spawn post. OR-ing those made a man on his
				// spawn post look finished, so he never walked to the access.
				View.bAtPost = PmcTacticalRules::IsAtPost(Feet, State.DefencePost, PostArriveCm, Team.Weights.LevelToleranceCm);
			}
		}
	}
	// G008 defence refresh gives every objective defender a post, including a man already on a ranged tactical post.
	// While his feet are on that post the commander sees it, so the defence walk does not pull him off it.
	// When he leaves it the defence post returns. A defence flag on the same man does not cancel the contact clock.
	{
		const FVector HoldFeet = PmcSquadLocal::FeetOf(Fighter);
		const bool bOnTacticalPost = State.Post.IsValid()
			&& PmcTacticalRules::IsAtPost(HoldFeet, State.Post.Location, PostArriveCm, Team.Weights.LevelToleranceCm);
		const bool bRangedPost = State.Post.Kind == EPmcPostKind::Overwatch || State.Post.Kind == EPmcPostKind::Window
			|| State.Post.Kind == EPmcPostKind::Ambush;
		// G008b / T-06: contact for a ranged post is measured from the post, not from the man. ambush_max_hold_s starts when a
		// known enemy's last position enters the engagement range and restarts if they all leave it, or if he steps off the post.
		// Overwatch: the smaller of the roof's range_m and his weapon reach. Window and ambush: the weapon band's max.
		// The assault stand-in is not in Team.Known, so it never starts this clock.
		if (bOnTacticalPost && bRangedPost)
		{
			const float BandMaxCm = View.Band.IsValid() ? View.Band.MaxCm : 0.f;
			const float EngagementCm = PmcCommanderRules::PostEngagementCm(State.Post.Kind, State.Post.RangeCm, View.WeaponRangeCm, BandMaxCm);
			float NearestCm = TNumericLimits<float>::Max();
			bool bInRange = false;
			for (const TPair<TWeakObjectPtr<AActor>, FKnownTarget>& Pair : Team.Known)
			{
				if (!Pair.Value.Actor.IsValid() || EngagementCm <= 0.f)
				{
					continue;
				}
				const float DistanceCm = FVector::Dist2D(State.Post.Location, Pair.Value.LastKnown);
				if (DistanceCm <= EngagementCm)
				{
					bInRange = true;
					NearestCm = FMath::Min(NearestCm, DistanceCm);
				}
			}
			if (!bInRange)
			{
				State.PostRangeContactAt = -1.0;
			}
			else if (State.PostRangeContactAt < 0.0)
			{
				State.PostRangeContactAt = Now;
				UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: %s %s contact in range (nearest %.0f cm, post range %.0f cm)"),
					TeamId, *Fighter.DisplayName.ToString(), PmcTacticalRules::PostKindName(State.Post.Kind), NearestCm, EngagementCm);
			}
			View.bContactInPostRange = State.PostRangeContactAt >= 0.0;
			View.PostRangeContactSeconds = View.bContactInPostRange ? static_cast<float>(Now - State.PostRangeContactAt) : 0.f;
			const float KindFactor = PmcTacticalRules::PostKindFactor(State.Post.Kind, Team.Weights);
			View.PostKind = State.Post.Kind;
			View.PostScore = KindFactor > 0.f ? State.Post.Score / KindFactor : State.Post.Score;
			View.bAtPost = true;
			View.bOnRangedPost = true;
			View.bOnOverwatchRoof = State.Post.Kind == EPmcPostKind::Overwatch;
			View.bHasDefencePost = false;
		}
		else
		{
			State.PostRangeContactAt = -1.0;
		}
	}
	if (Team.Defence.bActive && State.DefenceRole == EPmcDefenceRole::SectorShooter && State.bHasDefencePost && Team.Defence.Spots.Num() > 0
		&& State.DefenceSector != INDEX_NONE)
	{
		const FVector Feet = PmcSquadLocal::FeetOf(Fighter);
		const bool bAtPost = PmcTacticalRules::IsAtPost(Feet, State.DefencePost, PostArriveCm, Team.Weights.LevelToleranceCm);
		if (bAtPost)
		{
			if (State.DefenceArrivedAt < 0.0)
			{
				State.DefenceArrivedAt = Now;
			}
		}
		else
		{
			State.DefenceArrivedAt = -1.0;
		}
		const int32 NextSpot = PmcDefenceRules::NextEdgeSpot(Team.Defence.Spots, Team.Objective.GetValue().Location, Team.Defence.ApproachYawDeg,
			State.DefenceSector, Team.Defence.SectorCount, Team.Weights.SectorArcDeg, State.DefenceSpotIndex);
		const bool bSomewhereElse = NextSpot != INDEX_NONE && NextSpot != State.DefenceSpotIndex;
		const float SecondsOnPost = State.DefenceArrivedAt >= 0.0 ? static_cast<float>(Now - State.DefenceArrivedAt) : 0.f;
		// No target: none seen now. A remembered one counts from its last sighting; none at all counts as long ago.
		const bool bRealThreat = View.bHasThreat && !View.Threat.bStandIn;
		const float SecondsNoTarget = !bRealThreat ? 1e6f : (View.Threat.bSeenNow ? 0.f : View.Threat.SecondsSince);
		FPmcDefenceConfig Shift;
		Shift.ShiftAfterSeconds = Team.Weights.ShiftAfterSeconds;
		Shift.ShiftNoTargetSeconds = Team.Weights.ShiftNoTargetSeconds;
		Shift.ShiftCooldownSeconds = Team.Weights.ShiftCooldownSeconds;
		View.bSectorShift = bSomewhereElse && PmcDefenceRules::ShouldSectorShift(SecondsOnPost, View.bUnderFire, SecondsNoTarget,
			static_cast<float>(Now - State.LastSectorShiftAt), Shift);
	}
	// Cover: the quality of the slot he holds, and the best slot nearby (for TakeCover / Relocate).
	OutCover = FCoverSlot();
	OutCoverQuality = 0.f;
	if (View.bHasThreat)
	{
		if (Fighter.bInCover)
		{
			View.CoverQuality = PmcCommanderRules::CoverQualityAgainst(Fighter.GetCoverSlot().WorldLocation, Fighter.GetCoverSlot().WallNormal, View.Threat.Location);
		}
		const FVector* Exclude = Fighter.bInCover ? &View.Location : nullptr;
		TArray<FVector> ClaimedPosts;
		TArray<FString> ClaimedKeys;
		CollectDefenceClaims(Team, Fighter, ClaimedPosts, ClaimedKeys);
		// G008 lead review 2 (B): a posted defender's TakeCover / Relocate slot stays on his post's level, so "cover available"
		// never means a slot down the stair.
		const float PostZ = static_cast<float>(State.DefencePost.Z);
		const bool bPostLevel = State.bHasDefencePost && Team.Defence.bActive;
		View.bCoverAvailable = FindCoverNear(Fighter, View.Threat.Location, PmcSquadLocal::CoverSearchRadiusCm, Exclude, Team.Weights, OutCover, OutCoverQuality,
			View.Band, &View.CoverBandFactor, ClaimedPosts, ClaimedKeys, bPostLevel ? &PostZ : nullptr) && OutCoverQuality > View.CoverQuality + 0.2f;
	}
	// The nearest wounded mate (preserve active patient if already executing a running Heal order).
	const bool bKeepActivePatient = State.Order == EPmcOrder::Heal && State.Patient.IsValid()
		&& PmcSquadLocal::IsLiveOperative(State.Patient.Get()) && PmcSquadLocal::HealthFractionOf(*State.Patient) < 1.f;
	if (bKeepActivePatient)
	{
		View.WoundedMateDistanceCm = FVector::Dist2D(View.Location, State.Patient->GetActorLocation());
		View.WoundedMateHealthFrac = PmcSquadLocal::HealthFractionOf(*State.Patient);
	}
	else
	{
		State.Patient.Reset();
		for (AOperativeCharacter* Mate : Mates)
		{
			if (Mate == &Fighter || !PmcSquadLocal::IsLiveOperative(Mate))
			{
				continue;
			}
			const float MateHealth = PmcSquadLocal::HealthFractionOf(*Mate);
			if (MateHealth < Team.Weights.HealBelow)
			{
				const float Distance = FVector::Dist2D(View.Location, Mate->GetActorLocation());
				if (View.WoundedMateDistanceCm < 0.f || Distance < View.WoundedMateDistanceCm)
				{
					View.WoundedMateDistanceCm = Distance;
					View.WoundedMateHealthFrac = MateHealth;
					State.Patient = Mate;
				}
			}
		}
	}
	return View;
}

void UPmcSquadSubsystem::Decide(int32 TeamId, FTeam& Team)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	const TArray<AOperativeCharacter*> Mates = GetTeamMembers(TeamId);
	if (Mates.IsEmpty())
	{
		return;
	}
	if (Team.Objective.IsSet() && Team.Objective->Role == EPmcRole::Defender)
	{
		// Mates is GetTeamMembers(TeamId), the same list RefreshDefence reads.
		const TArray<int32> Living = PmcSquadLocal::LivingSquadIndices(Mates);
		if (!Team.Defence.bActive || Living != Team.Defence.LivingIndices)
		{
			RefreshDefence(TeamId, Team, !Team.Defence.bActive);
		}
	}
	// Nothing moves in a tactical pause / the turn-based grid fight: the commander waits like the player's autonomy does.
	if (const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		Flow && (Flow->GetCombatMode() == ECodexCombatMode::TacticalPause || Flow->GetCombatMode() == ECodexCombatMode::TurnBased))
	{
		return;
	}
	// T-08 (defence_roles): the hammers' fall-back, at the decision rate, before the fighters' orders are scored (the post they are given is read below).
	if (Team.Objective.IsSet() && Team.Objective->Role == EPmcRole::Defender && Team.Defence.bActive)
	{
		UpdateFallBack(TeamId, Team, Mates, Now);
	}

	FPmcSquadView Squad;
	Squad.Alive = Mates.Num();
	for (const TPair<TWeakObjectPtr<AActor>, FKnownTarget>& Pair : Team.Known)
	{
		Squad.EnemiesKnown += Pair.Value.Actor.IsValid() ? 1 : 0;
		Squad.EnemiesSeenNow += Pair.Value.Actor.IsValid() && Pair.Value.bSeenNow ? 1 : 0;
	}
	Squad.bContact = Now - Team.LastContactTime <= Team.Weights.ContactMemorySeconds;
	Squad.bEverContact = Team.bEverContact || Team.AssaultPoint.IsSet(); // an assault never waits in ambush
	Squad.bShotsExchanged = Team.bShotsExchanged;
	Squad.ContactSeconds = Team.FirstContactTime >= 0.0 ? static_cast<float>(Now - Team.FirstContactTime) : 0.f;
	// The quiet clock (fix/pmc-arena-stalemate): since the last SHOT between the team and an enemy (seeing each other out of reach is no
	// fight - the Depot blizzard round watched each other at 55 m for 4 minutes), or since the round's objective was set.
	Squad.QuietSeconds = static_cast<float>(FMath::Max(0.0, Now - FMath::Max(Team.LastShotTime, Team.ObjectiveSetTime)));
	for (AOperativeCharacter* Mate : Mates)
	{
		if (const FFighterState* State = Team.Fighters.Find(Mate); State && IsOrderRunning(*Mate, *State))
		{
			Squad.MatesSuppressing += State->Order == EPmcOrder::Suppress ? 1 : 0;
			Squad.MatesFlanking += State->Order == EPmcOrder::Flank ? 1 : 0;
			Squad.MatesAdvancing += State->Order == EPmcOrder::Advance ? 1 : 0;
			Squad.MatesHealing += State->Order == EPmcOrder::Heal ? 1 : 0;
			Squad.MatesRpgStriking += State->Order == EPmcOrder::RpgStrike ? 1 : 0;
		}
	}
	// The ambush springs: the first contact of a fight while men hold in ambush.
	// Not a defence post walk while he is on the move (G008 lead review 2): since review finding 1 the walk to the post is an
	// Ambush order in contact too, and turning it into Hold here re-issued a walk under way every decision (stalemate smoke: one
	// man's walk re-decided every 0.5 s, 13 quick same-order re-decisions against 6). A walk that stopped (pinned, ended) is still
	// turned into Hold, so the next decision re-issues it, as before. HasOwnMoveOrder alone reads false mid-sprint: speed too.
	if (Team.bEverContact)
	{
		for (AOperativeCharacter* Mate : Mates)
		{
			FFighterState* State = Team.Fighters.Find(Mate);
			if (!State || State->Order != EPmcOrder::Ambush)
			{
				continue;
			}
			const bool bPostWalkMoving = Mate && (Mate->HasOwnMoveOrder() || Mate->GetVelocity().SizeSquared2D() >= FMath::Square(PmcSquadLocal::StillSpeedCm));
			if (State->bHasDefencePost && Team.Defence.bActive && State->DefencePostWalkFromCm >= 0.f && bPostWalkMoving)
			{
				continue;
			}
			// G008b / T-06: a defender standing on a ranged post keeps Ambush until the fighter loop sees the hold close.
			// Springing it here, before his view exists, walked him off while the threat was still outside the post.
			// His G008 defence post does not count: the refresh stamps one on every objective defender.
			const FVector PostFeet = Mate ? PmcSquadLocal::FeetOf(*Mate) : FVector::ZeroVector;
			const bool bRangedKind = State->Post.Kind == EPmcPostKind::Overwatch || State->Post.Kind == EPmcPostKind::Window
				|| State->Post.Kind == EPmcPostKind::Ambush;
			const bool bRangedDefender = Mate && Team.Objective.IsSet() && Team.Objective->Role == EPmcRole::Defender
				&& bRangedKind
				&& PmcTacticalRules::IsAtPost(PostFeet, State->Post.Location, PostArriveCm, Team.Weights.LevelToleranceCm);
			if (bRangedDefender)
			{
				continue;
			}
			++Team.Stats.Ambushes;
			State->Order = EPmcOrder::Hold;
		}
	}

	// Mortar nests (2026-11-09, SQUAD_SIX_FIRE_TEAMS.md 7.3): a manned nest of the team shells the biggest cluster of enemies the team sees now
	// (the spotter rule; PmcCommanderRules::ScoreMortarFire >= mortar_threshold). Its crew stays at the tube (no orders of their own).
	UMortarSubsystem* Mortars = UMortarSubsystem::Get(World);
	if (Mortars)
	{
		TArray<FVector> Seen;
		for (const TPair<TWeakObjectPtr<AActor>, FKnownTarget>& Pair : Team.Known)
		{
			if (const AActor* Target = Pair.Value.Actor.Get(); Target && Pair.Value.bSeenNow)
			{
				Seen.Add(Target->GetActorLocation() - FVector(0.f, 0.f, Target->GetSimpleCollisionHalfHeight()));
			}
		}
		const FMortarNestConfig& MortarConfig = MortarRules::Get();
		for (AMortarNestActor* Nest : Mortars->GetNests(TeamId))
		{
			FVector Centre;
			int32 Count = 0;
			if (Nest->IsFiring() || !MortarRules::BestCluster(Seen, Team.Weights.MortarClusterRadiusCm, Nest->GetActorLocation(), Centre, Count))
			{
				continue;
			}
			const float Distance = static_cast<float>(FVector::Dist2D(Nest->GetActorLocation(), Centre));
			const bool bBand = Distance >= MortarConfig.MinRangeCm && (MortarConfig.MaxRangeCm <= 0.f || Distance <= MortarConfig.MaxRangeCm);
			const float Score = PmcCommanderRules::ScoreMortarFire(Count, Nest->GetCrewAtNest() >= MortarConfig.CrewNeeded, bBand, Team.Weights);
			if (Score >= Team.Weights.MortarThreshold)
			{
				const EMortarRefusal Refusal = Nest->OrderFire(Centre);
				UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: mortar %s at a cluster of %d (score %.2f, %.0f m): %s"), TeamId, *Nest->NestId, Count, Score,
					Distance / 100.f, Refusal == EMortarRefusal::None ? TEXT("fire") : *MortarRules::RefusalText(Refusal));
			}
		}
	}

	for (AOperativeCharacter* Fighter : Mates)
	{
		if (Mortars && Mortars->FindNestOf(Fighter))
		{
			continue; // a mortar crewman stays at his place
		}
		// fix/pmc-onpoint-3d-detour: on a ladder / mid-drop he can do nothing else - the walk goes on after it (no new order mid-climb: a goal
		// behind him turned him round at the next rung, the attackers never got onto the roof).
		if (Fighter->IsClimbingLadder() || Fighter->IsDroppingOffLedge())
		{
			continue;
		}
		FFighterState& State = Team.Fighters.FindOrAdd(Fighter);
		FCoverSlot Cover;
		float CoverQuality = 0.f;
		AActor* ThreatActor = nullptr;
		FPmcFighterView View = BuildFighterView(TeamId, Team, *Fighter, State, Mates, Cover, CoverQuality, ThreatActor);
		if (ThreatActor && ThreatActor != State.LastThreat.Get())
		{
			// A new threat appeared: clear completed cooldowns for the previous threat so the fighter can react.
			State.FailedUntil.Remove(EPmcOrder::TakeCover);
			State.FailedUntil.Remove(EPmcOrder::Relocate);
			State.FailedUntil.Remove(EPmcOrder::FallBack);
			State.LastThreat = ThreatActor;
		}
		const bool bRunning = IsOrderRunning(*Fighter, State);
		View.RunningOrder = bRunning ? State.Order : EPmcOrder::Count;
		// The mates' counters are about the OTHERS: his own running order is not a mate's.
		FPmcSquadView Own = Squad;
		if (bRunning)
		{
			Own.MatesSuppressing -= State.Order == EPmcOrder::Suppress ? 1 : 0;
			Own.MatesFlanking -= State.Order == EPmcOrder::Flank ? 1 : 0;
			Own.MatesAdvancing -= State.Order == EPmcOrder::Advance ? 1 : 0;
			Own.MatesHealing -= State.Order == EPmcOrder::Heal ? 1 : 0;
			Own.MatesRpgStriking -= State.Order == EPmcOrder::RpgStrike ? 1 : 0;
		}
		// Target attention (2026-11-07): an objective defender in ambush holds his fire until the seen target looks away (or is deep in
		// the kill zone / the ambush is blown) - the Passive posture, back to Aggressive when he strikes.
		// G008b: the pre-loop spring skipped this overwatch defender. Once the hold closes (in range for ambush_max_hold_s, or he
		// is hit, suppressed, or the post is untenable) the ambush ends here, once. A moving defence-post walk is still spared.
		if (Team.bEverContact && State.Order == EPmcOrder::Ambush && !PmcCommanderRules::IsAmbushHoldOpen(View, Own, Team.Weights))
		{
			const bool bPostWalkMoving = Fighter->HasOwnMoveOrder() || Fighter->GetVelocity().SizeSquared2D() >= FMath::Square(PmcSquadLocal::StillSpeedCm);
			const bool bDefenceWalk = State.bHasDefencePost && Team.Defence.bActive && State.DefencePostWalkFromCm >= 0.f && bPostWalkMoving;
			if (!bDefenceWalk)
			{
				++Team.Stats.Ambushes;
				State.Order = EPmcOrder::Hold;
			}
		}
		const bool bHoldFire = PmcCommanderRules::ShouldHoldFire(View, Own, Team.Weights);
		if (bHoldFire != State.bHoldingFire)
		{
			FSquadOrderContext Discipline;
			Discipline.Operative = Fighter;
			Discipline.Posture = bHoldFire ? ESquadFirePosture::Passive : ESquadFirePosture::Aggressive;
			SquadOrderCatalog::Execute(ESquadOrder::FirePosture, Discipline);
			State.bHoldingFire = bHoldFire;
			UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: %s %s (threat at %.0f cm, attention off %.2f)"), TeamId, *Fighter->DisplayName.ToString(),
				bHoldFire ? TEXT("holds his fire") : TEXT("opens fire"), View.Threat.DistanceCm, View.Threat.AttentionOff);
		}
		const TArray<FPmcOrderScore> Scored = PmcCommanderRules::ScoreAll(View, Own, Team.Weights);
		// G008 follow-up (finding 3): which way a defender's Objective walk goes (onto the point to retake / occupy, or to his post).
		View.bObjectiveToPoint = PmcCommanderRules::DefenderObjectiveToPoint(View, Own, Team.Weights);
		Team.Stats.PushDecisions += PmcCommanderRules::IsPushing(View, Own, Team.Weights) ? 1 : 0;
		// Order commitment (fix/pmc-arena-stalemate): a running order holds OrderMinHoldSeconds unless invalid, then only a clearly better one
		// breaks it; a failed order sits out its cooldown.
		FPmcCommitment Commitment;
		Commitment.Current = State.Order;
		Commitment.bRunning = bRunning;
		Commitment.AgeSeconds = static_cast<float>(Now - State.OrderSince);
		Commitment.FailedUntil = State.FailedUntil;
		Commitment.NowSeconds = Now;
		EPmcPickReason Reason = EPmcPickReason::Best;
		EPmcOrder Pick = PmcCommanderRules::PickCommitted(Scored, Commitment, Team.Weights, &Reason);
		if (Pick == State.Order && bRunning)
		{
			continue; // the running order keeps going
		}
		// TACTICAL_MAP P2: an Ambush that walked him to his post settles on it (faces its look, crouches) once the walk ended.
		// Not for a defender with a defence post: Ambush walks him to that post first, so his tactical post never settles and this
		// re-ran the defence walk every decision (stalemate smoke: 44 Ambush re-issues for one man).
		const bool bDefencePosted = State.bHasDefencePost && Team.Defence.bActive;
		const bool bSettlePost = Pick == EPmcOrder::Ambush && State.Order == EPmcOrder::Ambush && State.Post.IsValid() && !State.bPostSettled
			&& !bDefencePosted && !Fighter->HasOwnMoveOrder() && !PmcSquadLocal::IsTraversingLink(*Fighter);
		// G008 follow-up (review finding 1): the same for a defence post. Ambush is never "running", so without this a defence walk
		// that ended short of the post was never retried or counted, and he stood there for the rest of the round. Now the walk's
		// end re-executes Ambush: it settles him on the post, or counts the short walk and walks again (defence_post_walks drop it).
		// HasOwnMoveOrder alone is not "the walk ended": it read false mid-sprint (roof smoke: three re-issues a second, every post
		// dropped as "short"). He must stand still, and the Ambush must be older than the stalemate smoke's 2 s re-decide window.
		const bool bStandingStill = Fighter->GetVelocity().SizeSquared2D() < FMath::Square(PmcSquadLocal::StillSpeedCm)
			&& Now - State.OrderSince >= PmcSquadLocal::DefenceRetrySeconds;
		const bool bSettleDefence = Pick == EPmcOrder::Ambush && State.Order == EPmcOrder::Ambush && State.bHasDefencePost && Team.Defence.bActive
			&& (!State.bDefenceSettled || !View.bAtPost) && bStandingStill && !Fighter->HasOwnMoveOrder() && !PmcSquadLocal::IsTraversingLink(*Fighter);
		if (Pick == State.Order && PmcCommanderRules::IsStaticOrder(Pick) && !bSettlePost && !bSettleDefence)
		{
			continue; // nothing new to do
		}
		// An order that cannot be taken now (no path, no target) fails at once: the next best one this decision, not the same one again.
		bool bTaken = ExecuteOrder(TeamId, Team, *Fighter, State, Pick, View, ThreatActor, Cover, Mates);
		for (int32 Retry = 0; !bTaken && Retry < 2 && !PmcCommanderRules::IsStaticOrder(Pick); ++Retry)
		{
			MarkFailed(TeamId, Team, *Fighter, State, Pick, TEXT("could not be taken"));
			Commitment.FailedUntil = State.FailedUntil;
			Commitment.bRunning = false;
			Pick = PmcCommanderRules::PickCommitted(Scored, Commitment, Team.Weights, &Reason);
			if (Pick == State.Order && PmcCommanderRules::IsStaticOrder(Pick))
			{
				break;
			}
			bTaken = ExecuteOrder(TeamId, Team, *Fighter, State, Pick, View, ThreatActor, Cover, Mates);
		}
		if (bTaken)
		{
			++Team.Stats.OrdersIssued;
			Team.Stats.Preempts += Reason == EPmcPickReason::Preempt ? 1 : 0;
			OnFighterOrder.Broadcast(TeamId, Fighter, Pick, Scored[0].Score, Reason);
			UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: %s -> %s (score %.2f; threat %s at %.0f cm, hp %.0f%%, cover %.2f)"), TeamId,
				*Fighter->DisplayName.ToString(), PmcCommanderRules::OrderName(Pick), Scored[0].Score, View.bHasThreat ? (View.Threat.bSeenNow ? TEXT("seen") : TEXT("remembered")) : TEXT("none"),
				View.Threat.DistanceCm, View.HealthFrac * 100.f, View.CoverQuality);
			// Counting the orders the next decision reads as "running" relies on these mate counters being fresh.
			Squad.MatesSuppressing += Pick == EPmcOrder::Suppress ? 1 : 0;
			Squad.MatesFlanking += Pick == EPmcOrder::Flank ? 1 : 0;
			Squad.MatesAdvancing += Pick == EPmcOrder::Advance ? 1 : 0;
			Squad.MatesHealing += Pick == EPmcOrder::Heal ? 1 : 0;
			Squad.MatesRpgStriking += Pick == EPmcOrder::RpgStrike ? 1 : 0;
		}
	}
}

bool UPmcSquadSubsystem::ExecuteOrder(int32 TeamId, FTeam& Team, AOperativeCharacter& Fighter, FFighterState& State, EPmcOrder Order, const FPmcFighterView& View,
	AActor* ThreatActor, const FCoverSlot& Cover, const TArray<AOperativeCharacter*>& Mates)
{
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	FSquadOrderContext Context;
	Context.Operative = &Fighter;
	bool bTaken = false;
	if (Order != EPmcOrder::Ambush)
	{
		// Another order interrupts a post walk: it did not "end short" (TACTICAL_MAP P2, the post's three-tries rule).
		State.PostWalkFromCm = -1.f;
		State.DefencePostWalkFromCm = -1.f;
	}
	FVector Goal = FVector::ZeroVector;
	// Why the walk was not taken (Codex.Debug.MoveTrace logs it; fix/pmc-onpoint-3d-detour).
	const TCHAR* WalkRefusal = nullptr;
	// The Objective walk keeps the point's level (fix/pmc-onpoint-3d-detour): no "same spot on the threat's level" retry (it sent an attacker
	// of a roof point down into the Depot's cut where the threat was) and no reachable spot round it on another level (under the point).
	bool bKeepGoalLevel = false;
	// TACTICAL_MAP P2: the Ambush order's walk onto his post was refused this decision (the plain ambush instead).
	bool bPostWalkRefused = false;
	// ShortCm: the "he already stands at the goal" refusal. A defence post walk passes half of PostArriveCm (lead review 2): with
	// ArriveCm (250) a man 200-250 cm off his post was neither on it (bAtPost, 200) nor allowed to walk there, and three refused
	// walks dropped the post (stalemate smoke: Vance 222 cm off his post, three quick Ambush re-decisions, then the drop).
	auto Walk = [&](const FVector& Point, bool bSprint, float ShortCm = PmcSquadLocal::ArriveCm)
	{
		// Levels (the Depot arena, 2026-11-03): a goal at the fighter's height can hang over a drop (a roof squad's advance point over
		// the street) or lie on a roof part with no way there - then the same spot on the threat's level, walked through the normal
		// pathing (ladders / ramps / drop links).
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		auto HasFullPath = [&](const FVector& To)
		{
			const UNavigationPath* Path = Nav ? Nav->FindPathToLocationSynchronously(GetWorld(), Fighter.GetActorLocation(), To) : nullptr;
			return Path && Path->IsValid() && !Path->IsPartial();
		};
		bool bOnNav = ProjectToNav(Point, Goal);
		// A goal snapped far from the asked point (the roof edge under an advance point over the street) counts as missed too.
		if (!bKeepGoalLevel && View.bHasThreat && FMath::Abs(Point.Z - View.Threat.Location.Z) > 250.f
			&& (!bOnNav || FVector::Dist2D(Goal, Point) > 200.f || !HasFullPath(Goal)))
		{
			FVector ThreatLevel;
			if (ProjectToNav(FVector(Point.X, Point.Y, View.Threat.Location.Z), ThreatLevel) && HasFullPath(ThreatLevel))
			{
				Goal = ThreatLevel;
				bOnNav = true;
			}
		}
		if (!bOnNav)
		{
			WalkRefusal = TEXT("the goal is off the navmesh");
			return false;
		}
		// fix/pmc-arena-stalemate: a goal with no full path from him (an island, a roof part without access) - the walk would end at once and
		// be re-decided twice a second (pmc-v3): a reachable spot round it instead (4 / 8 m), else the order is not taken.
		// fix/pmc-onpoint-3d-detour: a spot on the goal's own level first (a roof goal's ring probe can snap to a lower roof / the street).
		if (Nav && !HasFullPath(Goal))
		{
			bool bFound = false;
			FVector OtherLevel = FVector::ZeroVector;
			bool bOtherLevel = false;
			for (int32 Ring = 1; Ring <= 2 && !bFound; ++Ring)
			{
				for (int32 Step = 0; Step < 8 && !bFound; ++Step)
				{
					FVector Near;
					const FVector Probe = Goal + FRotator(0.f, Step * 45.f, 0.f).Vector() * (400.f * Ring);
					if (ProjectToNav(Probe, Near) && HasFullPath(Near))
					{
						if (PmcCommanderRules::IsSameLevel(static_cast<float>(Near.Z), static_cast<float>(Goal.Z), Team.Weights.LevelToleranceCm))
						{
							Goal = Near;
							bFound = true;
						}
						else if (!bOtherLevel)
						{
							OtherLevel = Near;
							bOtherLevel = true;
						}
					}
				}
			}
			if (!bFound && bOtherLevel && !bKeepGoalLevel)
			{
				Goal = OtherLevel;
				bFound = true;
			}
			if (!bFound)
			{
				WalkRefusal = TEXT("no full path to the goal or a spot 4 / 8 m round it");
				return false;
			}
		}
		// A walk that goes nowhere is no order (the Depot street round re-took an Objective walk 30 times: each "arrived" at once) - on the
		// goal's level only: a roof goal right over his head is a walk up (fix/pmc-onpoint-3d-detour).
		if (!PmcCommanderRules::IsWalkShort(PmcSquadLocal::FeetOf(Fighter), Goal, ShortCm, Team.Weights.LevelToleranceCm))
		{
			WalkRefusal = TEXT("he already stands at the goal");
			return false;
		}
		// A rifleman lying at band distance (the prone Crouch order) gets up to crouch before he walks off (no crawling retreats / advances).
		if (Fighter.GetStance() == EOperativeStance::Prone)
		{
			Context.Stance = EOperativeStance::Crouching;
			SquadOrderCatalog::Execute(ESquadOrder::Stance, Context);
		}
		Context.Location = Goal;
		Context.bSprint = bSprint;
		return SquadOrderCatalog::Execute(ESquadOrder::Move, Context);
	};
	// G008 lead review 2 (B): a posted defender's FallBack / standoff goal stays on his post's level. The goal is projected at
	// the post's height and walked with the level kept (no "same spot on the threat's level", no ring spot on another level).
	// No such spot (the goal hangs over the street past the roof edge) refuses the walk, and the order fails and cools down.
	const bool bPostLevelOnly = State.bHasDefencePost && Team.Defence.bActive;
	const float PostLevelZ = static_cast<float>(State.DefencePost.Z);
	// The strict level projection first (2 m round, 1.5 m up / down), then the plain one's 4 m reach, kept only when it lands on the
	// post's level: a same-level goal the old FallBack reached is still reached (the stalemate smoke's ground posts).
	auto ProjectOnPostLevel = [&](const FVector& Point, FVector& OutPoint) -> bool
	{
		const FVector AtPost(Point.X, Point.Y, PostLevelZ);
		FVector OnNav;
		if ((ProjectToNavAtLevel(AtPost, OnNav) || ProjectToNav(AtPost, OnNav))
			&& PmcDefenceRules::IsGoalOnPostLevel(true, static_cast<float>(OnNav.Z), PostLevelZ, Team.Weights.LevelToleranceCm))
		{
			OutPoint = OnNav;
			return true;
		}
		return false;
	};
	auto ToPostLevel = [&](const FVector& Point, FVector& OutPoint) -> bool
	{
		if (!bPostLevelOnly)
		{
			OutPoint = Point;
			return true;
		}
		if (!ProjectOnPostLevel(Point, OutPoint))
		{
			WalkRefusal = TEXT("the goal is off his defence post's level");
			return false;
		}
		bKeepGoalLevel = true;
		return true;
	};
	switch (Order)
	{
	case EPmcOrder::Hold:
		// 2026-11-04: in the open with a threat he does not see now (e.g. back at his band after the standoff walk, his back to it), the
		// catalog's Hold turns him to face its last known spot - his own fire takes over once it is in his field of view.
		if (Fighter.HasOwnMoveOrder() || (!Fighter.bInCover && View.bHasThreat && !View.Threat.bSeenNow))
		{
			Context.Location = View.bHasThreat ? View.Threat.Location : FVector::ZeroVector;
			bTaken = SquadOrderCatalog::Execute(ESquadOrder::Hold, Context);
		}
		else
		{
			bTaken = true;
		}
		// When holding against a known threat (in cover or in the open), it becomes his priority target:
		// in cover the pose faces it and leans out; in the open he faces it and opens fire.
		if (ThreatActor && Fighter.GetManualPriorityTarget() != ThreatActor)
		{
			Context.Target = ThreatActor;
			SquadOrderCatalog::Execute(ESquadOrder::PriorityTarget, Context);
		}
		break;
	case EPmcOrder::TakeCover:
	case EPmcOrder::Relocate:
		// The wedge shift only when the view allowed it (ShouldSectorShift); any other Relocate is the plain cover move below.
		if (Order == EPmcOrder::Relocate && View.bSectorShift && State.DefenceRole == EPmcDefenceRole::SectorShooter && State.bHasDefencePost
			&& State.DefenceSector != INDEX_NONE && Team.Defence.bActive && Team.Defence.Spots.Num() > 0 && Team.Objective.IsSet())
		{
			const int32 NextSpot = PmcDefenceRules::NextEdgeSpot(Team.Defence.Spots, Team.Objective->Location, Team.Defence.ApproachYawDeg,
				State.DefenceSector, Team.Defence.SectorCount, Team.Weights.SectorArcDeg, State.DefenceSpotIndex);
			if (NextSpot != INDEX_NONE && NextSpot != State.DefenceSpotIndex)
			{
				State.DefenceSpotIndex = NextSpot;
				State.DefencePost = Team.Defence.Spots[NextSpot].Location;
				FVector OnNav;
				if (ProjectToNavAtLevel(State.DefencePost, OnNav))
				{
					State.DefencePost = OnNav;
				}
				State.DefenceYaw = Team.Defence.ApproachYawDeg;
				State.DefenceArrivedAt = -1.0;
				bKeepGoalLevel = true;
				// No sprint while he sees a target, so his own fire keeps going on the way.
				bTaken = Walk(State.DefencePost, !View.Threat.bSeenNow, 0.5f * PostArriveCm);
				Team.Stats.Relocations += bTaken ? 1 : 0;
				// The cooldown counts from the attempt: a refused walk does not retry next decision either.
				State.LastSectorShiftAt = Now;
				break;
			}
		}
		if (Fighter.bInCover && Fighter.GetCoverSlot().WorldLocation.Equals(Cover.WorldLocation, 50.f))
		{
			// Already in this exact cover slot on this spot
			return false;
		}
		Context.Cover = Cover;
		Context.bSprint = !View.Threat.bSeenNow;
		bTaken = SquadOrderCatalog::Execute(ESquadOrder::TakeCover, Context);
		Goal = Cover.WorldLocation;
		State.Slot = Cover;
		if (bTaken)
		{
			(Order == EPmcOrder::Relocate ? Team.Stats.Relocations : Team.Stats.Covers) += 1;
		}
		break;
	case EPmcOrder::Advance:
	{
		// AdvancePoint is planar: a threat on another level (a roof above him, the street below) within the stop range in plan would
		// leave him standing under it - he goes for the threat's own spot instead (up / down through the normal pathing).
		// 2026-11-04: he stops at his weapon's band (a rifleman ~20 m short, a shotgunner ~4 m), not at a fixed fraction of the reach.
		// A stand-in threat (hunt search point / assault point): walk to the search point itself.
		FVector AdvanceGoal = View.Threat.bStandIn ? View.Threat.Location
			: PmcCommanderRules::AdvancePoint(View.Location, View.Threat.Location, View.WeaponRangeCm, Team.Weights, View.Band);
		if (FMath::Abs(View.Threat.Location.Z - View.Location.Z) > 250.f && FVector::Dist2D(AdvanceGoal, View.Location) < 100.f)
		{
			AdvanceGoal = View.Threat.Location;
		}
		bTaken = View.bHasThreat && Walk(AdvanceGoal, !View.Threat.bSeenNow);
		Team.Stats.Advances += bTaken ? 1 : 0;
		break;
	}
	case EPmcOrder::Flank:
	{
		FVector Centre = FVector::ZeroVector;
		for (const AOperativeCharacter* Mate : Mates)
		{
			Centre += Mate->GetActorLocation();
		}
		Centre /= FMath::Max(1, Mates.Num());
		// The flank point lies at his band's distance from the threat (2026-11-04: a rifleman flanks at ~20 m, not 10).
		const float FlankDistance = PmcCommanderRules::FlankDistanceFor(View.Band, Team.Weights);
		FVector FlankGoal = PmcCommanderRules::FlankPoint(View.Threat.Location, Centre, View.Location, Team.Weights, FlankDistance);
		// Target attention (2026-11-07): the other side's flank point when the enemies he sees look clearly less that way (their blind zone).
		if (Team.Weights.Attention > 0.f)
		{
			TArray<FVector2D> Positions;
			TArray<float> Yaws;
			SeenWatchers(Team, Positions, Yaws);
			if (!Positions.IsEmpty())
			{
				const FVector Axis = (Centre - View.Threat.Location).GetSafeNormal2D();
				const FVector Offset = View.Location - View.Threat.Location;
				const FVector Mirrored = View.Threat.Location + Axis * (2.f * FVector::DotProduct(Offset, Axis)) - FVector(Offset.X, Offset.Y, 0.f);
				const FVector OtherGoal = PmcCommanderRules::FlankPoint(View.Threat.Location, Centre, Mirrored, Team.Weights, FlankDistance);
				if (PmcCommanderRules::AttentionOff(Positions, Yaws, FVector2D(OtherGoal)) > PmcCommanderRules::AttentionOff(Positions, Yaws, FVector2D(FlankGoal)) + 0.25f)
				{
					FlankGoal = OtherGoal;
				}
			}
		}
		bTaken = View.bHasThreat && Walk(FlankGoal, !View.Threat.bSeenNow);
		if (bTaken)
		{
			++Team.Stats.FlanksAttempted;
			State.bFlankCounted = false;
		}
		break;
	}
	case EPmcOrder::Suppress:
		Context.Target = ThreatActor;
		Context.Location = View.Threat.Location;
		bTaken = ThreatActor && SquadOrderCatalog::Execute(ESquadOrder::Suppress, Context);
		Team.Stats.Suppressions += bTaken ? 1 : 0;
		break;
	case EPmcOrder::FallBack:
		if (View.bHasThreat && PmcCommanderRules::IsTooClose(View.Threat.DistanceCm, View.Band, Team.Weights))
		{
			// Too close for his weapon (2026-11-04): back to the band - a cover slot round him already at band distance from the threat
			// (he fires from it), else the band's middle straight away from the threat; there the next decisions take the band's cover
			// (TakeCover / Relocate score slots by BandFactor; a wall between that point and the threat shows his rays its far side).
			FCoverSlot BandCover;
			float BandQuality = 0.f;
			float BandSlotFactor = 0.f;
			if (FindCoverNear(Fighter, View.Threat.Location, PmcSquadLocal::CoverSearchRadiusCm, nullptr, Team.Weights, BandCover, BandQuality, View.Band, &BandSlotFactor,
					TConstArrayView<FVector>(), TConstArrayView<FString>(), bPostLevelOnly ? &PostLevelZ : nullptr)
				&& BandSlotFactor >= 0.95f)
			{
				if (Fighter.bInCover && Fighter.GetCoverSlot().WorldLocation.Equals(BandCover.WorldLocation, 50.f))
				{
					bTaken = false;
				}
				else
				{
					Context.Cover = BandCover;
					Context.bSprint = true;
					bTaken = SquadOrderCatalog::Execute(ESquadOrder::TakeCover, Context);
					Goal = BandCover.WorldLocation;
					State.Slot = BandCover;
				}
			}
			if (!bTaken)
			{
				FVector StandoffGoal = PmcCommanderRules::StandoffPoint(View.Location, View.Threat.Location, View.Band);
				if (!ToPostLevel(StandoffGoal, StandoffGoal))
				{
					bTaken = false;
				}
				else if (!PmcCommanderRules::CanReissueOrderAt(EPmcOrder::FallBack, PmcSquadLocal::FeetOf(Fighter), StandoffGoal, PmcSquadLocal::ArriveCm, Team.Weights.LevelToleranceCm))
				{
					bTaken = false;
				}
				else
				{
					bTaken = Walk(StandoffGoal, true);
				}
			}
			Team.Stats.Standoffs += bTaken ? 1 : 0;
		}
		else
		{
			FVector FallBackGoal = PmcCommanderRules::FallBackPoint(View.Location, View.Threat.Location);
			// TACTICAL_MAP P2: round the chokepoint, not into it - five directions off straight away, each goal's PMC-side danger
			// (PmcTacticalRules::PickFallBack, chokepoint_avoid); straight away at equal danger. No tactical file: straight away.
			if (const UTacticalMapSubsystem* Map = GetTacticalMap())
			{
				const TArray<float> Angles = PmcTacticalRules::FallBackAngles();
				TArray<float> Dangers;
				TArray<FVector> Goals;
				for (const float Angle : Angles)
				{
					const FVector Candidate = View.Location + (FallBackGoal - View.Location).RotateAngleAxis(Angle, FVector::UpVector);
					FVector OnNav;
					// A posted defender's candidates are projected at his post's level (lead review 2, B): one over the street is no goal.
					const bool bOnNav = bPostLevelOnly ? ProjectOnPostLevel(Candidate, OnNav) : ProjectToNav(Candidate, OnNav);
					Goals.Add(bOnNav ? OnNav : Candidate);
					// Off the navmesh: no goal at all (the worst score).
					Dangers.Add(bOnNav ? Map->DangerAt(OnNav, ETacticalSide::Pmc).Danger : 1.f);
				}
				const int32 Pick = PmcTacticalRules::PickFallBack(Angles, Dangers, Team.Weights);
				if (Pick > 0)
				{
					FallBackGoal = Goals[Pick];
					++Team.Stats.TacticalFallBacks;
					UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: %s falls back %+.0f deg off straight away (danger %.2f there, %.2f straight)"), TeamId,
						*Fighter.DisplayName.ToString(), Angles[Pick], Dangers[Pick], Dangers[0]);
				}
			}
			if (!ToPostLevel(FallBackGoal, FallBackGoal))
			{
				bTaken = false;
			}
			else if (!PmcCommanderRules::CanReissueOrderAt(EPmcOrder::FallBack, PmcSquadLocal::FeetOf(Fighter), FallBackGoal, PmcSquadLocal::ArriveCm, Team.Weights.LevelToleranceCm))
			{
				bTaken = false;
			}
			else
			{
				bTaken = View.bHasThreat && Walk(FallBackGoal, true);
			}
		}
		Team.Stats.Retreats += bTaken ? 1 : 0;
		break;
	case EPmcOrder::Heal:
	{
		AOperativeCharacter* Patient = State.Patient.Get();
		if (!Patient)
		{
			break;
		}
		if (FVector::Dist2D(View.Location, Patient->GetActorLocation()) <= Team.Weights.HealReachCm)
		{
			Context.Patient = Patient;
			bTaken = SquadOrderCatalog::Execute(ESquadOrder::Heal, Context);
			Team.Stats.Heals += bTaken ? 1 : 0;
			if (bTaken)
			{
				Order = EPmcOrder::Hold;
				State.FailedUntil.Add(EPmcOrder::Heal, Now + Team.Weights.CompletedOrderCooldownSeconds);
			}
		}
		else
		{
			bTaken = Walk(Patient->GetActorLocation(), true);
		}
		break;
	}
	case EPmcOrder::Ambush:
		// G008: a defence post is walked like a tactical post, and it does not need a tactical file (Depot_1 has none).
		// G008b: a man already standing on his overwatch roof keeps that post. The defence refresh still stamps a sector
		// post on him, and walking it (seed 11 before the hold: Lind sprinted off B_hall_n to the ground sector spot)
		// is the leave this order must not take while the hold is what scored Ambush.
		{
			const FVector RoofFeet = PmcSquadLocal::FeetOf(Fighter);
			const bool bRangedKind = State.Post.Kind == EPmcPostKind::Overwatch || State.Post.Kind == EPmcPostKind::Window
				|| State.Post.Kind == EPmcPostKind::Ambush;
			const bool bOnRangedPost = bRangedKind
				&& PmcTacticalRules::IsAtPost(RoofFeet, State.Post.Location, PostArriveCm, Team.Weights.LevelToleranceCm);
			if (State.bHasDefencePost && Team.Defence.bActive && !bOnRangedPost)
			{
			const FVector Feet = PmcSquadLocal::FeetOf(Fighter);
			bool bOnPost = PmcTacticalRules::IsAtPost(Feet, State.DefencePost, PostArriveCm, Team.Weights.LevelToleranceCm);
			// Review finding 1: an unreachable post (a navmesh island, a walk that always ends short) is dropped after
			// defence_post_walks tries, like the tactical posts' three short walks. A walk that made progress is no failure.
			// Pinned is not unreachable: under fire he goes prone against the flash and an accepted sprint makes no progress
			// (roof smoke: the guard dropped the stair post that way and settled 4.1 m from the access). A refused walk still counts.
			if (!bOnPost && !View.bUnderFire
				&& PmcTacticalRules::PostWalkEndedShort(State.DefencePostWalkFromCm, static_cast<float>(FVector::Dist(Feet, State.DefencePost))))
			{
				++State.DefencePostWalks;
			}
			State.DefencePostWalkFromCm = -1.f;
			if (!bOnPost && State.DefencePostWalks >= Team.Weights.DefencePostWalks)
			{
				DropDefencePost(TeamId, Team, Fighter, State);
				bOnPost = PmcTacticalRules::IsAtPost(Feet, State.DefencePost, PostArriveCm, Team.Weights.LevelToleranceCm);
			}
			if (!bOnPost)
			{
				bKeepGoalLevel = true;
				// Sprint unless he is under fire: seed 2's stair is 25 m from the spawn and the attackers die in 18 s, so a quiet walk
				// arrives too late for guard share > 0.6 (and a roof sees the street at once, so "a target in sight" is nearly always
				// true). Shot at, he walks, so his own fire answers on the way. The post stays on this level.
				const float DistanceCm = static_cast<float>(FVector::Dist(Feet, State.DefencePost));
				bTaken = Walk(State.DefencePost, !View.bUnderFire, 0.5f * PostArriveCm);
				// Not taken = unreachable now: counted like a walk that ended short.
				State.DefencePostWalks += bTaken ? 0 : 1;
				State.DefencePostWalkFromCm = bTaken ? DistanceCm : -1.f;
				State.bDefenceSettled = false;
				if (bTaken)
				{
					UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: %s walks to his %s post %s"), TeamId, *Fighter.DisplayName.ToString(),
						PmcDefenceRules::RoleName(State.DefenceRole), *State.DefencePost.ToCompactString());
					break;
				}
			}
			else
			{
				if (State.bFiringPosition && State.bFiringCover)
				{
					// T-05 phase b: a cover-slot firing position is taken (bInCover, the corner lean / over-the-top fire), not stood beside.
					if (!Fighter.bInCover)
					{
						Context.Cover = State.FiringSlot;
						Context.bSprint = false;
						SquadOrderCatalog::Execute(ESquadOrder::TakeCover, Context);
					}
				}
				else
				{
					Context.Location = Feet + FRotator(0.f, State.DefenceYaw, 0.f).Vector() * 1500.f;
					SquadOrderCatalog::Execute(ESquadOrder::Hold, Context);
					// T-05 phase b: a firing position fires from its own stance (a roof edge stands: the parapet is above a kneeling muzzle).
					Context.Stance = State.bFiringPosition ? State.FiringStance : EOperativeStance::Crouching;
					SquadOrderCatalog::Execute(ESquadOrder::Stance, Context);
				}
				bTaken = true;
				Goal = State.DefencePost;
				State.bDefenceSettled = true;
				State.DefencePostWalks = 0;
				break;
			}
			}
		}
		// TACTICAL_MAP P2: a man with a post waits ON it - walks there first, then faces its look direction (crouched; standing at a
		// window, where a crouch would hide him under the sill).
		// A post he cannot reach (no path, three walks that ended short of it) is dropped: the plain ambush below.
		// The previous post walk ended short only when it got him no closer (a walk that made progress is not a failure).
		if (State.Post.IsValid() && GetTacticalMap()
			&& !PmcTacticalRules::IsAtPost(PmcSquadLocal::FeetOf(Fighter), State.Post.Location, PostArriveCm, Team.Weights.LevelToleranceCm)
			&& PmcTacticalRules::PostWalkEndedShort(State.PostWalkFromCm, static_cast<float>(FVector::Dist(PmcSquadLocal::FeetOf(Fighter), State.Post.Location))))
		{
			++State.PostWalks;
		}
		State.PostWalkFromCm = -1.f;
		if (State.Post.IsValid() && GetTacticalMap() && State.PostWalks >= 3
			&& !PmcTacticalRules::IsAtPost(PmcSquadLocal::FeetOf(Fighter), State.Post.Location, PostArriveCm, Team.Weights.LevelToleranceCm))
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: %s gives up his %s post %s (not reached)"), TeamId, *Fighter.DisplayName.ToString(),
				PmcTacticalRules::PostKindName(State.Post.Kind), *State.Post.Location.ToCompactString());
			State.Post = FPmcTacticalPost();
		}
		if (State.Post.IsValid() && GetTacticalMap())
		{
			const FVector Feet = PmcSquadLocal::FeetOf(Fighter);
			if (!PmcTacticalRules::IsAtPost(Feet, State.Post.Location, PostArriveCm, Team.Weights.LevelToleranceCm))
			{
				const float DistanceCm = static_cast<float>(FVector::Dist(Feet, State.Post.Location));
				bKeepGoalLevel = true;
				bTaken = Walk(State.Post.Location, false);
				Team.Stats.PostMoves += bTaken ? 1 : 0;
				State.bPostSettled = false;
				// Not taken = unreachable now: counted like a walk that ended short.
				State.PostWalks += bTaken ? 0 : 1;
				State.PostWalkFromCm = bTaken ? DistanceCm : -1.f;
				if (bTaken)
				{
					UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: %s walks onto his %s post %s"), TeamId, *Fighter.DisplayName.ToString(),
						PmcTacticalRules::PostKindName(State.Post.Kind), *State.Post.Location.ToCompactString());
					break;
				}
				// Refused (no full path now): the plain ambush where he stands; the next decision tries again (three tries drop the post).
				bPostWalkRefused = true;
			}
		}
		if (State.Post.IsValid() && GetTacticalMap() && !bPostWalkRefused)
		{
			Context.Location = PmcSquadLocal::FeetOf(Fighter) + FRotator(0.f, State.Post.Yaw, 0.f).Vector() * 1500.f;
			SquadOrderCatalog::Execute(ESquadOrder::Hold, Context);
			Context.Stance = State.Post.Kind == EPmcPostKind::Window ? EOperativeStance::Standing : EOperativeStance::Crouching;
			SquadOrderCatalog::Execute(ESquadOrder::Stance, Context);
			bTaken = true;
			Goal = State.Post.Location;
			State.bPostSettled = true;
			// On a ranged tactical post the sector post stays assigned, but he is settled here: the defence-walk retry must not
			// start while his feet are still on this post (bSettleDefence reads bDefenceSettled).
			if (State.bHasDefencePost && (State.Post.Kind == EPmcPostKind::Overwatch || State.Post.Kind == EPmcPostKind::Window
				|| State.Post.Kind == EPmcPostKind::Ambush))
			{
				State.bDefenceSettled = true;
				State.DefencePostWalks = 0;
				State.DefencePostWalkFromCm = -1.f;
			}
			break;
		}
		Context.Stance = EOperativeStance::Crouching;
		bTaken = SquadOrderCatalog::Execute(ESquadOrder::Stance, Context);
		if (!Fighter.bInCover)
		{
			FCoverSlot Near;
			float Quality = 0.f;
			TArray<FVector> ClaimedPosts;
			TArray<FString> ClaimedKeys;
			if (Team.Defence.bActive)
			{
				CollectDefenceClaims(Team, Fighter, ClaimedPosts, ClaimedKeys);
			}
			if (FindCoverNear(Fighter, View.Location + Fighter.GetActorForwardVector() * 1000.f, 600.f, nullptr, Team.Weights, Near, Quality,
				FPmcEngagementBand(), nullptr, ClaimedPosts, ClaimedKeys, bPostLevelOnly ? &PostLevelZ : nullptr))
			{
				Context.Cover = Near;
				Context.bSprint = false;
				SquadOrderCatalog::Execute(ESquadOrder::TakeCover, Context);
				Goal = Near.WorldLocation;
			}
		}
		break;
	case EPmcOrder::Reload:
		bTaken = SquadOrderCatalog::Execute(ESquadOrder::Reload, Context);
		break;
	case EPmcOrder::Objective:
	{
		// Onto the point (task #53): each man to his own spot of a small ring round it (they do not stack on one navmesh point); sprint
		// while the enemy does not see him.
		// G008: a defender walks to his defence post. With no post he holds where he is.
		// G008 follow-up (finding 3): to retake the point, or to occupy it on a push or late clock, he goes onto its ring like an
		// attacker (the code below).
		if (View.Objective.Role == EPmcRole::Defender && !View.bObjectiveToPoint)
		{
			if (!State.bHasDefencePost)
			{
				Order = EPmcOrder::Hold;
				bTaken = true;
				break;
			}
			const FVector Feet = PmcSquadLocal::FeetOf(Fighter);
			// On his post: hold and face the defence yaw; off it: walk there. This is the same arrival test as the view's bAtPost
			// (PostArriveCm), so the order and the scoring agree on "there". (IsWalkShort is true while he is still FAR, so the old
			// "!IsWalkShort -> Hold" was not inverted; it only used ArriveCm, 50 cm wider than bAtPost.)
			const bool bOnPost = PmcTacticalRules::IsAtPost(Feet, State.DefencePost, PostArriveCm, Team.Weights.LevelToleranceCm);
			if (bOnPost)
			{
				Context.Location = Feet + FRotator(0.f, State.DefenceYaw, 0.f).Vector() * 1500.f;
				SquadOrderCatalog::Execute(ESquadOrder::Hold, Context);
				Order = EPmcOrder::Hold;
				bTaken = true;
				break;
			}
			bKeepGoalLevel = true;
			bTaken = Walk(State.DefencePost, !(View.bHasThreat && View.Threat.bSeenNow), 0.5f * PostArriveCm);
			Team.Stats.ObjectiveMoves += bTaken ? 1 : 0;
			break;
		}
		if (!View.Objective.IsActive())
		{
			break;
		}
		const int32 Index = FMath::Max(0, Mates.IndexOfByKey(&Fighter));
		const FVector Offset = Index == 0 ? FVector::ZeroVector
			: FRotator(0.f, Index * 120.f, 0.f).Vector() * 0.4f * View.Objective.RadiusCm;
		// fix/pmc-onpoint-3d-detour: his ring spot must lie on the point's level - on a small roof the offset projects past the edge onto
		// the street below (a walk to the ground under the point); then the point itself.
		FVector Spot = View.Objective.Location + Offset;
		FVector SpotOnNav;
		if (!Offset.IsNearlyZero() && (!ProjectToNav(Spot, SpotOnNav) || !PmcCommanderRules::IsSameLevel(static_cast<float>(SpotOnNav.Z),
			static_cast<float>(View.Objective.Location.Z), View.Objective.LevelToleranceCm)))
		{
			Spot = View.Objective.Location;
		}
		if (!PmcCommanderRules::IsWalkShort(PmcSquadLocal::FeetOf(Fighter), Spot, PmcSquadLocal::ArriveCm, Team.Weights.LevelToleranceCm))
		{
			// Already on his spot on the point: hold here (no walk needed, no failure).
			Order = EPmcOrder::Hold;
			bTaken = true;
			break;
		}
		bKeepGoalLevel = true;
		bTaken = Walk(Spot, !(View.bHasThreat && View.Threat.bSeenNow));
		Team.Stats.ObjectiveMoves += bTaken ? 1 : 0;
		break;
	}
	case EPmcOrder::Sidearm:
		Context.WeaponId = TEXT("pistol");
		bTaken = SquadOrderCatalog::Execute(ESquadOrder::SwitchWeapon, Context);
		break;
	case EPmcOrder::RpgStrike:
		// The RPG gunner's rocket (2026-11-09): at the rule's best target this decision (not while an ambush holds its fire).
		Context.Target = State.LauncherTarget.Get();
		// A suppression still running blocks the launcher (the fire loop keeps the rifle for it): the rocket replaces it.
		if (!State.bHoldingFire && Context.Target && Fighter.HasSuppressOrder())
		{
			Fighter.CancelSuppressFire(TEXT("RPG strike"));
		}
		bTaken = !State.bHoldingFire && Context.Target && SquadOrderCatalog::Execute(ESquadOrder::LauncherStrike, Context);
		Team.Stats.RpgStrikes += bTaken ? 1 : 0;
		if (bTaken)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: %s RPG strike at %s (launcher score %.2f)"), TeamId, *Fighter.DisplayName.ToString(),
				*Context.Target->GetName(), View.LauncherScore);
		}
		break;
	case EPmcOrder::Crouch:
		// 2026-11-04: a rifleman / sniper in the open at his band's distance goes prone (a smaller target, the steadier shot); pistols and
		// shotguns, and anybody out of his band, crouch.
		Context.Stance = View.Band.IsValid() && View.Band.MinCm >= Team.Weights.ProneMinBandCm && View.bHasThreat
			&& PmcCommanderRules::BandFactor(View.Threat.DistanceCm, View.Band, Team.Weights) >= 0.999f ? EOperativeStance::Prone : EOperativeStance::Crouching;
		bTaken = SquadOrderCatalog::Execute(ESquadOrder::Stance, Context);
		break;
	default:
		break;
	}
	if (bTaken)
	{
		State.bWalkPending = PmcCommanderRules::IsMoveOrder(Order) && (Fighter.HasOwnMoveOrder() || Fighter.HasPendingCover());
		State.bStrikePending = Order == EPmcOrder::RpgStrike;
		State.Detours = 0;
		State.DetourAttempts = 0;
		State.DetourSide = 0;
		State.Order = Order;
		State.OrderSince = Now;
		State.Goal = Goal;
		State.Target = ThreatActor;
		State.ProgressPoint = Fighter.GetActorLocation();
		State.ProgressTime = Now;
	}
	else if (WalkRefusal)
	{
		static const IConsoleVariable* MoveTrace = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Debug.MoveTrace"));
		if (MoveTrace && MoveTrace->GetInt() != 0)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[MoveTrace] %s: %s walk not taken: %s (goal %s, feet %s)"), *Fighter.DisplayName.ToString(),
				PmcCommanderRules::OrderName(Order), WalkRefusal, *Goal.ToCompactString(), *PmcSquadLocal::FeetOf(Fighter).ToCompactString());
		}
	}
	return bTaken;
}

bool UPmcSquadSubsystem::CoverSlotAlongRay(UWorld* World, const FVector& Origin, const FVector& Direction, float Reach, const FCollisionQueryParams& Params,
	FCoverSlot& OutSlot, const TFunction<bool(const FHitResult&)>& Accept)
{
	FHitResult Hit;
	if (!World || !World->LineTraceSingleByChannel(Hit, Origin, Origin + Direction * Reach, ECC_Visibility, Params) || !Hit.GetActor() || Cast<APawn>(Hit.GetActor()))
	{
		return false;
	}
	if (Accept && !Accept(Hit))
	{
		return false;
	}
	return CoverTraceRules::FindCoverSlotAt(World, Hit.ImpactPoint, Direction, OutSlot) && OutSlot.IsValid();
}

bool UPmcSquadSubsystem::FindCoverNear(const AOperativeCharacter& Fighter, const FVector& Threat, float RadiusCm, const FVector* Exclude, const FPmcWeights& Weights,
	FCoverSlot& OutSlot, float& OutQuality, const FPmcEngagementBand& Band, float* OutBandFactor, TConstArrayView<FVector> ClaimedPosts,
	TConstArrayView<FString> ClaimedKeys, const float* PostLevelZ) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	// Rays from the fighter's chest in 16 directions find the walls around him; each hit is tried as a cover slot and scored by how well
	// its wall shields him from the threat, nearer slots preferred.
	const FVector Chest = Fighter.GetActorLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PmcCoverSearch), false, &Fighter);
	for (const AOperativeCharacter* Other : GetHostileOperatives())
	{
		Params.AddIgnoredActor(Other);
	}
	if (const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>())
	{
		for (const AOperativeCharacter* Member : Squad->GetMembers())
		{
			Params.AddIgnoredActor(Member);
		}
	}
	// High ground (the Depot arena, 2026-11-03): every slot's score grows with its height level (3 m steps, SightRules::HeightLevel)
	// over the ground under the fighter (his eye minus his eye height above that ground, the shared sight's measurement).
	const bool bHighGround = Weights.HighGround > 0.f && Weights.HighGroundMaxLevels > 0;
	const FVector Feet = Fighter.GetActorLocation() - FVector(0.f, 0.f, Fighter.GetSimpleCollisionHalfHeight());
	const UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>();
	const float GroundZ = Sight ? static_cast<float>(UTacticalSightSubsystem::EyePoint(Fighter).Z) - Sight->GetEyeAboveGroundCm(Fighter) : static_cast<float>(Feet.Z);
	const float EyeCm = SightRules::EyeHeight(EOperativeStance::Standing);
	const FTeam* OwnTeam = FindTeam(Fighter.TeamId);
	const FPmcObjectiveView* Objective = OwnTeam && OwnTeam->Objective.IsSet() && OwnTeam->Objective->IsActive() ? &OwnTeam->Objective.GetValue() : nullptr;
	const float WeaponRangeCm = Fighter.CurrentWeapon ? Fighter.CurrentWeapon->Stats.Range.MaxRangeCm : 1400.f;
	// #32 (2026-11-07): a corner is worth its bonus only when his peek round it reaches the threat (his sight range, the cover-and-peek rule).
	const float PeekRangeCm = Fighter.GetPeekSightRangeCm();
	// TACTICAL_MAP P2: a slot inside a chokepoint is worth less, one whose wall faces the squad's route more (near an analysis cover spot).
	const UTacticalMapSubsystem* TacticalMap = GetTacticalMap();
	const TArray<FVector>* Route = OwnTeam && !OwnTeam->Route.IsEmpty() ? &OwnTeam->Route : nullptr;
	struct FCandidate
	{
		FCoverSlot Slot;
		float Quality = 0.f;
		float Score = 0.f;
		bool bElevated = false;
		float BandFactor = 1.f;
		FString StableId;
	};
	TArray<FCandidate, TInlineAllocator<24>> Candidates;
	auto TerrainFactor = [&](const FCoverSlot& Slot)
	{
		if (!TacticalMap)
		{
			return 1.f;
		}
		const float Danger = TacticalMap->DangerAt(Slot.WorldLocation, ETacticalSide::Pmc).Danger;
		float Facing = 0.f;
		bool bNearAnalysisCover = false;
		if (Route)
		{
			Facing = PmcTacticalRules::RouteFacing(Slot.WorldLocation, Slot.WallNormal, PmcTacticalRules::NearestOnRoute(*Route, Slot.WorldLocation));
			// Only the hiding spots near the route (cached with it, SetRouteOf): a slot farther out has none within 2.5 m anyway.
			for (const FVector& Spot : OwnTeam->RouteCoverSpots)
			{
				if (FVector::Dist2D(Spot, Slot.WorldLocation) <= 250.f && FMath::Abs(Spot.Z - Slot.WorldLocation.Z) <= 150.f)
				{
					bNearAnalysisCover = true;
					break;
				}
			}
		}
		return PmcTacticalRules::SlotTerrainFactor(Danger, Facing, bNearAnalysisCover, Weights);
	};
	auto TryRay = [&](const FVector& Origin, const FVector& Direction, float Reach, bool bElevated)
	{
		FCoverSlot Slot;
		if (!CoverSlotAlongRay(World, Origin, Direction, Reach, Params, Slot))
		{
			return;
		}
		if (Exclude && FVector::Dist2D(Slot.WorldLocation, *Exclude) < 300.f)
		{
			return;
		}
		// G008 lead review 2 (B): a posted defender's cover stays on his post's level.
		if (PostLevelZ && !PmcDefenceRules::IsGoalOnPostLevel(true, static_cast<float>(Slot.WorldLocation.Z), *PostLevelZ, Weights.LevelToleranceCm))
		{
			return;
		}
		const float Quality = PmcCommanderRules::CoverQualityAgainst(Slot.WorldLocation, Slot.WallNormal, Threat);
		if (Quality < 0.35f)
		{
			return;
		}
		const float Distance = FVector::Dist2D(Chest, Slot.WorldLocation);
		const int32 Level = bHighGround ? SightRules::HeightLevel(static_cast<float>(Slot.WorldLocation.Z) + EyeCm - GroundZ) : 0;
		// Engagement band (2026-11-04): a slot at his weapon's distance from the threat, at a corner he can fire round, scores best.
		const float SlotThreatCm = static_cast<float>(FVector::Dist2D(Slot.WorldLocation, Threat));
		const float SlotBand = PmcCommanderRules::BandFactor(SlotThreatCm, Band, Weights);
		const bool bCorner = Band.IsValid() && PeekSightRules::IsCornerInPeekReach(Slot.bLeftEdgeExposed || Slot.bRightEdgeExposed, SlotThreatCm, PeekRangeCm);
		// The round objective (task #53): attackers prefer slots on the way to / round the point, defenders slots overwatching it.
		const float ObjectiveFactor = Objective ? PmcCommanderRules::ObjectivePositionFactor(Slot.WorldLocation, Feet, *Objective, Band, WeaponRangeCm, Weights) : 1.f;
		// G008: the spacing term needs claims. Attackers and an undeployed defence have none, so they skip the
		// per-candidate GetPathName and the CoverObjectKey Printf.
		float Spacing = 1.f;
		if (!ClaimedPosts.IsEmpty())
		{
			FPmcDefenceConfig SpacingConfig;
			SpacingConfig.MinSpacingCm = Weights.MinSpacingCm;
			SpacingConfig.SpacingPenalty = Weights.SpacingPenalty;
			const FString SlotKey = PmcDefenceRules::CoverObjectKey(Slot.WallActor.IsValid() ? Slot.WallActor->GetPathName() : FString(), Slot.WallPoint);
			Spacing = PmcDefenceRules::SpacingFactor(Slot.WorldLocation, SlotKey, ClaimedPosts, ClaimedKeys, SpacingConfig);
		}
		const float Score = Quality * (1.f - 0.5f * FMath::Clamp(Distance / RadiusCm, 0.f, 1.f)) * (Slot.Height == ECoverHeight::HighCover ? 1.f : 0.9f)
			* PmcCommanderRules::HighGroundFactor(Level, Weights) * FMath::Lerp(1.f, SlotBand, FMath::Clamp(Weights.BandPosition, 0.f, 1.f))
			* (bCorner ? 1.f + FMath::Max(Weights.CornerBonus, 0.f) : 1.f) * ObjectiveFactor * TerrainFactor(Slot) * Spacing;
		Candidates.Add({ Slot, Quality, Score, bElevated, SlotBand, PmcSquadLocal::StableCoverSlotId(Slot, bElevated) });
	};
	for (int32 Index = 0; Index < 16; ++Index)
	{
		TryRay(Chest, FRotator(0.f, Index * 22.5f, 0.f).Vector(), RadiusCm, false);
	}
	// The levels above him: navmesh spots one to HighGroundMaxLevels floors up round him (roofs, walkways, the far side of a ladder)
	// tried as cover origins too - the walk there takes the normal pathing (ladders / ramps / drop links), checked below.
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (bHighGround && Nav)
	{
		const FVector ToThreat = (Threat - Feet).GetSafeNormal2D();
		for (int32 Bearing = 0; Bearing < 8; ++Bearing)
		{
			const FVector Direction = FRotator(0.f, Bearing * 45.f, 0.f).Vector();
			for (int32 Level = 1; Level <= FMath::Min(3, Weights.HighGroundMaxLevels); ++Level)
			{
				FNavLocation OnNav;
				const FVector Probe = Feet + Direction * RadiusCm * 0.6f + FVector(0.f, 0.f, Level * 300.f + 50.f);
				if (!Nav->ProjectPointToNavigation(Probe, OnNav, FVector(150.f, 150.f, 150.f)) || OnNav.Location.Z - Feet.Z < 250.f)
				{
					continue;
				}
				const FVector Origin = OnNav.Location + FVector(0.f, 0.f, 120.f);
				const FVector Facing = ToThreat.IsNearlyZero() ? Direction : ToThreat;
				for (const float Turn : { 0.f, 45.f, -45.f })
				{
					TryRay(Origin, Facing.RotateAngleAxis(Turn, FVector::UpVector), 400.f, true);
				}
			}
		}
	}
	Candidates.StableSort([](const FCandidate& Left, const FCandidate& Right)
	{
		return Left.Score != Right.Score ? Left.Score > Right.Score
			: Left.StableId.Compare(Right.StableId, ESearchCase::CaseSensitive) < 0;
	});
	int32 PathChecks = 0;
	for (const FCandidate& Candidate : Candidates)
	{
		if (Candidate.bElevated)
		{
			// A slot up a level must be walkable from here without a huge detour (at most two path queries per decision).
			if (!Nav || PathChecks >= 2)
			{
				continue;
			}
			++PathChecks;
			const UNavigationPath* Path = Nav->FindPathToLocationSynchronously(World, Feet, Candidate.Slot.WorldLocation);
			if (!Path || !Path->IsValid() || Path->IsPartial() || Path->GetPathLength() > 3.f * FVector::Dist(Feet, Candidate.Slot.WorldLocation) + 3000.f)
			{
				continue;
			}
		}
		OutSlot = Candidate.Slot;
		OutQuality = Candidate.Quality;
		if (OutBandFactor)
		{
			*OutBandFactor = Candidate.BandFactor;
		}
		return true;
	}
	return false;
}

void UPmcSquadSubsystem::SeenWatchers(const FTeam& Team, TArray<FVector2D>& OutPositions, TArray<float>& OutYaws) const
{
	// Only what the team sees now counts (no cheating): the enemy's spot and his body's facing as seen.
	for (const TPair<TWeakObjectPtr<AActor>, FKnownTarget>& Pair : Team.Known)
	{
		const AOperativeCharacter* Enemy = Cast<AOperativeCharacter>(Pair.Value.Actor.Get());
		if (Enemy && Pair.Value.bSeenNow && PmcSquadLocal::IsLiveOperative(Enemy))
		{
			OutPositions.Add(FVector2D(Enemy->GetActorLocation()));
			OutYaws.Add(static_cast<float>(Enemy->GetActorRotation().Yaw));
		}
	}
}

bool UPmcSquadSubsystem::HasFullNavPath(const FVector& From, const FVector& To) const
{
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	const UNavigationPath* Path = Nav ? Nav->FindPathToLocationSynchronously(GetWorld(), From, To) : nullptr;
	return Path && Path->IsValid() && !Path->IsPartial();
}

bool UPmcSquadSubsystem::TryDetour(AOperativeCharacter& Fighter, const FFighterState& State, const FPmcWeights& Weights, EPmcDetourStep Step,
	int32& OutSide) const
{
	OutSide = 0;
	const FVector From = Fighter.GetActorLocation();
	FVector Dir = (State.Goal - From).GetSafeNormal2D();
	if (Dir.IsNearlyZero())
	{
		Dir = Fighter.GetActorForwardVector().GetSafeNormal2D();
	}
	// The arrival vector: the segment of the path he was walking when he stalled (the step back undoes it), else his facing.
	FVector Arrival = FVector::ZeroVector;
	if (const AAIController* Controller = Cast<AAIController>(Fighter.GetController()))
	{
		if (const UPathFollowingComponent* Following = Controller->GetPathFollowingComponent())
		{
			Arrival = Following->GetCurrentDirection().GetSafeNormal2D();
		}
	}
	if (Arrival.IsNearlyZero())
	{
		Arrival = Fighter.GetActorForwardVector().GetSafeNormal2D();
	}
	TArray<FPmcDetourCandidate> Candidates;
	PmcCommanderRules::DetourCandidates(Dir, Arrival, Step, State.DetourSide, Weights, Candidates);
	const FVector Feet = PmcSquadLocal::FeetOf(Fighter);
	for (const FPmcDetourCandidate& Candidate : Candidates)
	{
		FVector Via;
		if (!ProjectToNav(From + Candidate.Offset, Via) || FVector::Dist2D(Via, From) < Candidate.MinDistanceCm)
		{
			continue;
		}
		// The step back stays on his floor (a 75 cm step that snaps to the street below is no step back).
		if (Step == EPmcDetourStep::StepBack && !PmcCommanderRules::IsSameLevel(static_cast<float>(Via.Z), static_cast<float>(Feet.Z), Weights.LevelToleranceCm))
		{
			continue;
		}
		if (HasFullNavPath(From, Via) && HasFullNavPath(Via, State.Goal) && Fighter.SteerOwnMoveVia(Via))
		{
			OutSide = Candidate.Side;
			return true;
		}
	}
	return false;
}

void UPmcSquadSubsystem::MarkFailed(int32 TeamId, FTeam& Team, AOperativeCharacter& Fighter, FFighterState& State, EPmcOrder Order, const TCHAR* Why)
{
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	State.FailedUntil.Add(Order, Now + Team.Weights.FailedOrderCooldownSeconds);
	State.bWalkPending = false;
	++Team.Stats.OrdersFailed;
	UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: %s's %s failed (%s) - not again for %.0f s"), TeamId, *Fighter.DisplayName.ToString(),
		PmcCommanderRules::OrderName(Order), Why, Team.Weights.FailedOrderCooldownSeconds);
}

bool UPmcSquadSubsystem::ProjectToNav(const FVector& Point, FVector& OutPoint) const
{
	const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	FNavLocation Projected;
	if (Nav && Nav->ProjectPointToNavigation(Point, Projected, FVector(400.f, 400.f, 400.f)))
	{
		OutPoint = Projected.Location;
		return true;
	}
	return false;
}

bool UPmcSquadSubsystem::ProjectToNavAtLevel(const FVector& Point, FVector& OutPoint) const
{
	const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	FNavLocation Projected;
	if (Nav && Nav->ProjectPointToNavigation(Point, Projected, FVector(200.f, 200.f, 150.f)) && FMath::Abs(Projected.Location.Z - Point.Z) <= 150.f)
	{
		OutPoint = Projected.Location;
		return true;
	}
	return false;
}

void UPmcSquadSubsystem::KeepCandidatesOnNav(TArray<FPmcPostCandidate>& Candidates) const
{
	const int32 Before = Candidates.Num();
	for (int32 Index = Candidates.Num() - 1; Index >= 0; --Index)
	{
		FVector OnNav;
		if (ProjectToNavAtLevel(Candidates[Index].Location, OnNav))
		{
			Candidates[Index].Location = OnNav;
		}
		else
		{
			Candidates.RemoveAt(Index);
		}
	}
	if (Candidates.Num() < Before)
	{
		UE_LOG(LogCodexTactics, Verbose, TEXT("[PmcAI] tactical posts: %d of %d candidates off the navmesh at their level dropped"), Before - Candidates.Num(), Before);
	}
}

void UPmcSquadSubsystem::SetRouteOf(FTeam& Team, const TArray<FVector>& Route) const
{
	Team.Route = Route;
	Team.RouteCoverSpots.Reset();
	const UTacticalMapSubsystem* Map = GetTacticalMap();
	if (!Map || Route.IsEmpty())
	{
		return;
	}
	// A slot can only count a spot within 2.5 m of itself, and the slot lies within the cover search of a man near the route.
	const float Reach = FMath::Max(Team.Weights.TacticalReachCm, 100.f) + PmcSquadLocal::CoverSearchRadiusCm + 250.f;
	for (const FTacticalHidingSpot& Spot : Map->GetFile().HidingSpots)
	{
		if (PmcTacticalRules::DistanceToRoute(Route, Spot.Location) <= Reach)
		{
			Team.RouteCoverSpots.Add(Spot.Location);
		}
	}
}

void UPmcSquadSubsystem::HandleWeaponFired(AOperativeCharacter* Shooter, AActor* Target, bool bHit)
{
	// T-08: the defence's first shot of the round (fall-back rule 2 waits for it).
	if (Shooter)
	{
		FTeam* OwnTeam = Teams.Find(Shooter->TeamId);
		if (OwnTeam && OwnTeam->Defence.bActive && OwnTeam->Defence.FirstShotAt < 0.0 && GetWorld())
		{
			OwnTeam->Defence.FirstShotAt = GetWorld()->GetTimeSeconds();
			UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: the defence fires its first shot (%s)"), Shooter->TeamId, *Shooter->DisplayName.ToString());
		}
	}
	const AOperativeCharacter* Victim = Cast<AOperativeCharacter>(Target);
	if (!Shooter || !Victim || Victim == Shooter)
	{
		return;
	}
	if (Victim->TeamId != Shooter->TeamId)
	{
		// The native event names the actual target, avoiding guesses after target switching.
		if (FTeam* ShooterTeam = Teams.Find(Shooter->TeamId))
		{
			const FKnownTarget* Known = ShooterTeam->Known.Find(Target);
			if (Known && Known->bSeenNow && !Shooter->HasSuppressOrder())
			{
				ShooterTeam->Fighters.FindOrAdd(Shooter).RecentFireTarget = Target;
			}
		}
		// A shot between two teams blows any ambush holding its fire on either side (target attention, 2026-11-07).
		for (const int32 TeamId : { Shooter->TeamId, Victim->TeamId })
		{
			if (FTeam* Team = Teams.Find(TeamId))
			{
				Team->bShotsExchanged = true;
				Team->LastShotTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
			}
		}
		return;
	}
	if (!bHit)
	{
		return;
	}
	if (FTeam* Team = Teams.Find(Shooter->TeamId))
	{
		++Team->Stats.FriendlyFireHits;
	}
}

// --- Spawning --------------------------------------------------------------------------------------------------------------

AOperativeCharacter* UPmcSquadSubsystem::SpawnFighter(int32 TeamId, const FVector& Location, const FRotator& Facing, int32 Seed, int32 Index)
{
	UWorld* World = GetWorld();
	ACodexTacticsGameMode* GameMode = World ? World->GetAuthGameMode<ACodexTacticsGameMode>() : nullptr;
	if (!GameMode || TeamId == PmcTeam::Player)
	{
		return nullptr;
	}
	const FPmcOperatorConfig& Config = PmcOperatorRules::Get();
	const FPmcLook& Look = PmcOperatorRules::PickLook(Config, Seed, Index);
	FSquadMemberSpawn Entry;
	Entry.DisplayName = FText::FromString(PmcOperatorRules::PickName(Config, Seed, Index));
	Entry.Color = Look.Tint;
	Entry.Fortitude = Config.Fortitude;
	Entry.Role = EOperativeRole::Engineer; // a plain soldier's stats; the role matters only to the player's squad (deployables, mission rules)
	Entry.Luck = Config.Luck;
	Entry.Accuracy = Config.Accuracy;
	// The PMC body (user decision 2026-11-03): the operative Blueprint's mesh swapped before BeginPlay, ABP_Operative stays
	// (Scripts/Editor/setup_pmc_operator_body.py makes the skeletons compatible); a missing asset keeps the operative's body.
	if (!Config.BodyMesh.IsEmpty())
	{
		Entry.BodyMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(Config.BodyMesh));
		if (Entry.BodyMesh.LoadSynchronous() == nullptr)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[PmcAI] body mesh %s not found - the operative's own body"), *Config.BodyMesh);
			Entry.BodyMesh.Reset();
		}
	}
	AOperativeCharacter* Fighter = GameMode->SpawnOperative(Entry, 100 * TeamId + Index, Location, Facing, true, TeamId);
	if (!Fighter)
	{
		return nullptr;
	}
	Fighter->bExpendable = true;
	Fighter->MedkitsCount = Config.Medkits;
	Fighter->GrenadesCount = Config.Grenades;
	// The look: the coat material instance (when the asset exists) on the look's slot, then the tint through the operative's role-colour
	// path on that slot / parameter.
	if (!Look.CoatSlot.IsEmpty())
	{
		Fighter->RoleColorMaterialSlot = FName(*Look.CoatSlot);
	}
	if (!Look.TintParameter.IsEmpty())
	{
		Fighter->RoleColorParameter = FName(*Look.TintParameter);
	}
	if (!Look.CoatMaterial.IsEmpty())
	{
		if (UMaterialInterface* Coat = LoadObject<UMaterialInterface>(nullptr, *Look.CoatMaterial))
		{
			const int32 Slot = Fighter->GetMesh() ? Fighter->GetMesh()->GetMaterialIndex(Fighter->RoleColorMaterialSlot) : INDEX_NONE;
			if (Slot != INDEX_NONE)
			{
				Fighter->GetMesh()->SetMaterial(Slot, Coat);
			}
			else
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] look %s: no material slot '%s' on %s - coat material skipped"), *Look.Id,
					*Fighter->RoleColorMaterialSlot.ToString(), Fighter->GetMesh() && Fighter->GetMesh()->GetSkeletalMeshAsset() ? *Fighter->GetMesh()->GetSkeletalMeshAsset()->GetName() : TEXT("?"));
			}
		}
		else
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] look %s: coat material %s not found - the tint alone"), *Look.Id, *Look.CoatMaterial);
		}
	}
	Fighter->ApplyBodyColor();
	// The weapon of the pool: added to the arsenal when the starting one lacks it, then in hands.
	const FString WeaponId = PmcOperatorRules::PickWeaponId(Config, Seed, Index);
	if (!PmcSquadLocal::HasWeaponId(*Fighter, *WeaponId))
	{
		if (UWeaponDataAsset* Weapon = LoadObject<UWeaponDataAsset>(nullptr, *PmcOperatorRules::WeaponAssetPath(WeaponId)))
		{
			Fighter->AddArsenalWeapon(Weapon);
		}
	}
	if (PmcSquadLocal::HasWeaponId(*Fighter, *WeaponId))
	{
		Fighter->SwitchToWeaponById(WeaponId);
	}
	// The squad's RPG gunner (the game designer 2026-11-09): the launcher on top of his pool weapon; the shared launcher rule draws it.
	// The trainer tunes the gunner's share per personality (pmc_commander.json rpg_gunner_share; the arena sets the team's weights before
	// the spawn): it overrides the operators file's share.
	FPmcOperatorConfig GunnerConfig = Config;
	if (const FTeam* Team = FindTeam(TeamId); Team && Team->Weights.RpgGunnerShare >= 0.f)
	{
		GunnerConfig.RpgGunner.Share = FMath::Clamp(Team->Weights.RpgGunnerShare, 0.f, 1.f);
	}
	const bool bRpgGunner = PmcOperatorRules::IsRpgGunner(GunnerConfig, Seed, Index);
	if (bRpgGunner && !PmcSquadLocal::HasWeaponId(*Fighter, *Config.RpgGunner.WeaponId))
	{
		if (UWeaponDataAsset* Launcher = LoadObject<UWeaponDataAsset>(nullptr, *PmcOperatorRules::WeaponAssetPath(Config.RpgGunner.WeaponId)))
		{
			Fighter->AddArsenalWeapon(Launcher);
			Fighter->SwitchToWeaponById(WeaponId); // the pool weapon stays in hands
		}
	}
	// Hostile fighters fire at anything they see.
	FSquadOrderContext Posture;
	Posture.Operative = Fighter;
	Posture.Posture = ESquadFirePosture::Aggressive;
	SquadOrderCatalog::Execute(ESquadOrder::FirePosture, Posture);
	RegisterOperative(Fighter); // BeginPlay did it already; harmless twice
	UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: spawned %s (look %s, %s%s) at %s"), TeamId, *Fighter->DisplayName.ToString(), *Look.Id, *WeaponId,
		bRpgGunner ? *FString::Printf(TEXT(" + %s gunner"), *Config.RpgGunner.WeaponId) : TEXT(""), *Location.ToCompactString());
	return Fighter;
}

TArray<AOperativeCharacter*> UPmcSquadSubsystem::SpawnSquad(int32 TeamId, const FVector& Centre, const FRotator& Facing, int32 Count, int32 Seed)
{
	TArray<AOperativeCharacter*> Out;
	const FVector Right = FRotator(0.f, Facing.Yaw, 0.f).RotateVector(FVector::RightVector);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float Offset = (Index - (Count - 1) * 0.5f) * 150.f;
		FVector Spot = Centre + Right * Offset;
		FVector OnNav;
		if (ProjectToNav(Spot, OnNav))
		{
			Spot = OnNav + FVector(0.f, 0.f, 90.f);
		}
		if (AOperativeCharacter* Fighter = SpawnFighter(TeamId, Spot, Facing, Seed, Index))
		{
			Out.Add(Fighter);
		}
	}
	return Out;
}

void UPmcSquadSubsystem::DestroyTeam(int32 TeamId)
{
	if (TeamId == PmcTeam::Player)
	{
		return;
	}
	for (AOperativeCharacter* Fighter : MembersOf(TeamId))
	{
		if (Fighter)
		{
			Fighter->Destroy();
		}
	}
	if (FTeam* Team = Teams.Find(TeamId))
	{
		Team->Members.Reset();
		Team->Fighters.Reset();
		Team->Known.Reset();
	}
}

// --- The tactical map (docs/design/TACTICAL_MAP.md P2, designer 2026-11-17) ------------------------------------------------------------

const UTacticalMapSubsystem* UPmcSquadSubsystem::GetTacticalMap() const
{
	const UWorld* World = GetWorld();
	const UTacticalMapSubsystem* Map = World ? World->GetSubsystem<UTacticalMapSubsystem>() : nullptr;
	return Map && Map->IsLoaded() && !Map->GetFile().IsEmpty() ? Map : nullptr;
}

TArray<FVector> UPmcSquadSubsystem::RouteBetween(const FVector& From, const FVector& To) const
{
	UWorld* World = GetWorld();
	UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	const UNavigationPath* Path = Nav ? Nav->FindPathToLocationSynchronously(World, From, To) : nullptr;
	if (Path && Path->IsValid() && !Path->IsPartial() && Path->PathPoints.Num() >= 2)
	{
		return Path->PathPoints;
	}
	return { From, To };
}

void UPmcSquadSubsystem::SetTeamRoute(int32 TeamId, const TArray<FVector>& Route)
{
	SetRouteOf(TeamOf(TeamId), Route);
}

const TArray<FVector>& UPmcSquadSubsystem::GetTeamRoute(int32 TeamId) const
{
	static const TArray<FVector> None;
	const FTeam* Team = FindTeam(TeamId);
	return Team ? Team->Route : None;
}

const FPmcTacticalPost* UPmcSquadSubsystem::GetFighterPost(const AOperativeCharacter* Fighter) const
{
	if (!Fighter)
	{
		return nullptr;
	}
	const FTeam* Team = FindTeam(Fighter->TeamId);
	const FFighterState* State = Team ? Team->Fighters.Find(const_cast<AOperativeCharacter*>(Fighter)) : nullptr;
	return State && State->Post.IsValid() ? &State->Post : nullptr;
}

TArray<FPmcPostCandidate> UPmcSquadSubsystem::GatherPostCandidates(const TArray<FVector>& Route, int32 Seed, const FPmcWeights& Weights, float MinFromStartCm,
	TArray<int32>* OutLadderRoofsPicked) const
{
	TArray<FPmcPostCandidate> Out;
	const UTacticalMapSubsystem* Map = GetTacticalMap();
	if (OutLadderRoofsPicked)
	{
		OutLadderRoofsPicked->Reset();
	}
	if (!Map || Route.IsEmpty())
	{
		return Out;
	}
	const FTacticalMapFile& File = Map->GetFile();
	const float Reach = FMath::Max(Weights.TacticalReachCm, 100.f);
	auto Near = [&Route, Reach, MinFromStartCm](const FVector& Point, float Scale)
	{
		return PmcTacticalRules::DistanceToRoute(Route, Point) <= Reach * Scale
			&& (MinFromStartCm <= 0.f || FVector::Dist2D(Route[0], Point) >= MinFromStartCm);
	};
	auto Danger = [Map](const FVector& Point) { return Map->DangerAt(Point, ETacticalSide::Pmc).Danger; };
	auto YawToRoute = [&Route](const FVector& Point)
	{
		const FVector To = (PmcTacticalRules::NearestOnRoute(Route, Point) - Point).GetSafeNormal2D();
		return To.IsNearlyZero() ? 0.f : static_cast<float>(To.Rotation().Yaw);
	};
	// The route sampled every 15 m (and its points): the targets of the overwatch / window / drop queries.
	TArray<FVector> Samples;
	for (int32 Index = 0; Index < Route.Num(); ++Index)
	{
		Samples.Add(Route[Index]);
		if (Index + 1 < Route.Num())
		{
			const float Length = static_cast<float>(FVector::Dist(Route[Index], Route[Index + 1]));
			for (float Along = 1500.f; Along < Length; Along += 1500.f)
			{
				Samples.Add(FMath::Lerp(Route[Index], Route[Index + 1], Along / Length));
			}
		}
	}
	// Ambush: the hiding spots biting each leg of the route (BestAmbushSpots: the EXIT of a place first).
	TMap<int32, float> Ambush;
	for (int32 Index = 0; Index < FMath::Max(1, Route.Num() - 1); ++Index)
	{
		const FVector& A = Route[Index];
		const FVector& B = Route[FMath::Min(Index + 1, Route.Num() - 1)];
		for (const FTacticalPick& Pick : Map->BestAmbushSpots(A, B, 8))
		{
			float& Best = Ambush.FindOrAdd(Pick.Index, 0.f);
			Best = FMath::Max(Best, Pick.Score);
		}
	}
	for (const TPair<int32, float>& Entry : Ambush)
	{
		const FTacticalHidingSpot& Spot = File.HidingSpots[Entry.Key];
		if (Near(Spot.Location, 1.f))
		{
			FPmcPostCandidate& Candidate = Out.AddDefaulted_GetRef();
			Candidate.Kind = EPmcPostKind::Ambush;
			Candidate.Location = Spot.Location;
			Candidate.Yaw = Spot.FacingYaw;
			Candidate.Score = Entry.Value;
			Candidate.Danger = Danger(Spot.Location);
			Candidate.Group = Spot.Id;
			Candidate.SourceIndex = Entry.Key;
		}
	}
	// Overwatch roof spots reaching the route, windows covering it, drop edges landing on it.
	TMap<TPair<int32, int32>, float> Roofs;
	TMap<int32, float> Windows;
	TMap<int32, float> Drops;
	for (const FVector& Sample : Samples)
	{
		for (const FTacticalPick& Pick : Map->OverwatchSpots(Sample, 0.f, 4))
		{
			float& Best = Roofs.FindOrAdd(TPair<int32, int32>(Pick.Index, Pick.SpotIndex), 0.f);
			Best = FMath::Max(Best, Pick.Score);
		}
		const TArray<FTacticalPick> WindowPicks = Map->WindowPositions(Sample, 600.f);
		for (int32 Rank = 0; Rank < FMath::Min(4, WindowPicks.Num()); ++Rank)
		{
			float& Best = Windows.FindOrAdd(WindowPicks[Rank].Index, 0.f);
			Best = FMath::Max(Best, WindowPicks[Rank].Score);
		}
		for (const FTacticalPick& Pick : Map->DropSpotsAbove(Sample, 800.f))
		{
			float& Best = Drops.FindOrAdd(Pick.Index, 0.f);
			Best = FMath::Max(Best, Pick.Score);
		}
	}
	for (const TPair<TPair<int32, int32>, float>& Entry : Roofs)
	{
		const FTacticalOverwatchRoof& Roof = File.OverwatchRoofs[Entry.Key.Key];
		if (!Roof.Spots.IsValidIndex(Entry.Key.Value))
		{
			continue;
		}
		const FTacticalOverwatchSpot& Spot = Roof.Spots[Entry.Key.Value];
		// Within the reach of the route like every other post (roofs 1.5 x, as the ladder roofs: a roof sees farther) and away from its start.
		if (!Near(Spot.Location, 1.5f))
		{
			continue;
		}
		FPmcPostCandidate& Candidate = Out.AddDefaulted_GetRef();
		Candidate.Kind = EPmcPostKind::Overwatch;
		Candidate.Location = Spot.Location;
		Candidate.Yaw = YawToRoute(Spot.Location);
		Candidate.Score = Entry.Value;
		Candidate.Danger = Danger(Spot.Location);
		Candidate.Group = Roof.Building;
		Candidate.SourceIndex = Entry.Key.Key;
		Candidate.SpotIndex = Entry.Key.Value;
		Candidate.RangeCm = Roof.RangeM * 100.f;
	}
	for (const TPair<int32, float>& Entry : Windows)
	{
		const FTacticalWindow& Window = File.Windows[Entry.Key];
		if (!Near(Window.Stand, 1.f))
		{
			continue;
		}
		FPmcPostCandidate& Candidate = Out.AddDefaulted_GetRef();
		Candidate.Kind = EPmcPostKind::Window;
		Candidate.Location = Window.Stand;
		Candidate.Yaw = Window.Yaw;
		Candidate.Score = Entry.Value;
		Candidate.Danger = Danger(Window.Stand);
		Candidate.Group = Window.Id;
		Candidate.SourceIndex = Entry.Key;
	}
	for (const TPair<int32, float>& Entry : Drops)
	{
		const FTacticalDropSpot& Drop = File.DropSpots[Entry.Key];
		if (!Near(Drop.Land, 1.f))
		{
			continue;
		}
		FPmcPostCandidate& Candidate = Out.AddDefaulted_GetRef();
		Candidate.Kind = EPmcPostKind::Drop;
		Candidate.Location = Drop.Edge;
		const FVector ToLand = (Drop.Land - Drop.Edge).GetSafeNormal2D();
		Candidate.Yaw = ToLand.IsNearlyZero() ? 0.f : static_cast<float>(ToLand.Rotation().Yaw);
		Candidate.Score = Entry.Value;
		Candidate.Danger = Danger(Drop.Edge);
		Candidate.Group = Drop.Id;
		Candidate.Jumpers = Drop.Jumpers;
		Candidate.SourceIndex = Entry.Key;
	}
	// The ladder-only roofs the seeded picker gives an enemy (PART of them: the file's 0.65), near enough to the route.
	const TArray<int32> Picked = Map->PickLadderOnlyRoofs(Seed);
	if (OutLadderRoofsPicked)
	{
		*OutLadderRoofsPicked = Picked;
	}
	for (const int32 Index : Picked)
	{
		const FTacticalLadderRoof& Roof = File.LadderOnlyRoofs[Index];
		if (!Near(Roof.Center, 1.5f))
		{
			continue;
		}
		FPmcPostCandidate& Candidate = Out.AddDefaulted_GetRef();
		Candidate.Kind = EPmcPostKind::LadderRoof;
		Candidate.Location = Roof.Center;
		Candidate.Yaw = YawToRoute(Roof.Center);
		Candidate.Score = 1.f - FMath::Clamp(PmcTacticalRules::DistanceToRoute(Route, Roof.Center) / (1.5f * Reach), 0.f, 1.f);
		Candidate.Danger = Danger(Roof.Center);
		Candidate.Group = Roof.Building;
		Candidate.SourceIndex = Index;
	}
	// A stable order whatever the maps hash to: kind, then source, then spot.
	Out.StableSort([](const FPmcPostCandidate& Left, const FPmcPostCandidate& Right)
	{
		return Left.Kind != Right.Kind ? Left.Kind < Right.Kind : (Left.SourceIndex != Right.SourceIndex ? Left.SourceIndex < Right.SourceIndex : Left.SpotIndex < Right.SpotIndex);
	});
	return Out;
}

TArray<FPmcPlacementFighter> UPmcSquadSubsystem::PlacementRoles(int32 Count, int32 Seed, const FPmcWeights& Weights) const
{
	TArray<FPmcPlacementFighter> Roles;
	const FPmcOperatorConfig& Config = PmcOperatorRules::Get();
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FPmcPlacementFighter& Role = Roles.AddDefaulted_GetRef();
		const FString WeaponId = PmcOperatorRules::PickWeaponId(Config, Seed, Index);
		if (const FPmcEngagementBand* Band = Weights.Bands.Find(WeaponId))
		{
			Role.BandMaxCm = Band->MaxCm;
			Role.bMarksman = Band->MinCm >= Weights.MarksmanBandMinCm;
		}
	}
	PmcTacticalRules::MarkMarksmen(Roles);
	return Roles;
}

TArray<FPmcPlacementFighter> UPmcSquadSubsystem::PlacementRoles(const TArray<AOperativeCharacter*>& Fighters, const FPmcWeights& Weights) const
{
	TArray<FPmcPlacementFighter> Roles;
	for (const AOperativeCharacter* Fighter : Fighters)
	{
		FPmcPlacementFighter& Role = Roles.AddDefaulted_GetRef();
		const FString WeaponId = Fighter && Fighter->CurrentWeapon ? Fighter->CurrentWeapon->WeaponId : FString();
		const float Range = Fighter && Fighter->CurrentWeapon ? Fighter->CurrentWeapon->Stats.Range.MaxRangeCm : 0.f;
		Role.BandMaxCm = PmcCommanderRules::ResolveBand(WeaponId, Range, Weights).MaxCm;
		const FPmcEngagementBand* Row = Weights.Bands.Find(WeaponId);
		Role.bMarksman = Row && Row->MinCm >= Weights.MarksmanBandMinCm;
	}
	PmcTacticalRules::MarkMarksmen(Roles);
	return Roles;
}

TArray<AOperativeCharacter*> UPmcSquadSubsystem::SpawnSquadTactical(int32 TeamId, const TArray<FVector>& Route, const FVector& Fallback, const FRotator& Facing,
	int32 Count, int32 Seed, TArray<FPmcTacticalPost>* OutPosts, TArray<int32>* OutLadderRoofsPicked)
{
	const FTeam* Existing = FindTeam(TeamId);
	const FPmcWeights Weights = Existing ? Existing->Weights : FPmcWeights();
	TArray<FPmcPostCandidate> Candidates = GatherPostCandidates(Route, Seed, Weights, 0.f, OutLadderRoofsPicked);
	// A post off the navmesh at its own level would leave its man unable to move: such candidates are never planned.
	KeepCandidatesOnNav(Candidates);
	if (OutPosts)
	{
		OutPosts->Reset();
	}
	if (Candidates.IsEmpty())
	{
		// No tactical file (or nothing along this route): the old line spawn.
		TArray<AOperativeCharacter*> Line = SpawnSquad(TeamId, Fallback, Facing, Count, Seed);
		if (OutPosts)
		{
			OutPosts->SetNum(Line.Num());
		}
		return Line;
	}
	const TArray<FPmcTacticalPost> Posts = PmcTacticalRules::PlanPlacement(PlacementRoles(Count, Seed, Weights), Candidates, Weights, Seed);
	TArray<AOperativeCharacter*> Out;
	const FVector Right = FRotator(0.f, Facing.Yaw, 0.f).RotateVector(FVector::RightVector);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FPmcTacticalPost Post = Posts.IsValidIndex(Index) ? Posts[Index] : FPmcTacticalPost();
		FVector Spot;
		FRotator Look = Facing;
		// On the navmesh at the post's own level (a roof spot must not snap to the street under it); a post that fails it is no post.
		FVector OnPostNav = Post.Location;
		if (Post.IsValid() && !ProjectToNavAtLevel(Post.Location, OnPostNav))
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[PmcAI] team %d: the %s post %s is off the navmesh at its level - fighter %d spawns on the line"), TeamId,
				PmcTacticalRules::PostKindName(Post.Kind), *Post.Location.ToCompactString(), Index);
			Post = FPmcTacticalPost();
		}
		if (Post.IsValid())
		{
			Spot = OnPostNav;
			Look = FRotator(0.f, Post.Yaw, 0.f);
		}
		else
		{
			Spot = Fallback + Right * ((Index - (Count - 1) * 0.5f) * 150.f);
			FVector OnNav;
			if (ProjectToNav(Spot, OnNav))
			{
				Spot = OnNav;
			}
		}
		AOperativeCharacter* Fighter = SpawnFighter(TeamId, Spot + FVector(0.f, 0.f, 95.f), Look, Seed, Index);
		if (!Fighter)
		{
			// Neither the fighter nor his post: OutPosts stays index-aligned with the result.
			UE_LOG(LogCodexTactics, Warning, TEXT("[PmcAI] team %d: fighter %d failed to spawn at %s (his %s post is left out)"), TeamId, Index,
				*Spot.ToCompactString(), PmcTacticalRules::PostKindName(Post.Kind));
			continue;
		}
		Out.Add(Fighter);
		if (OutPosts)
		{
			OutPosts->Add(Post);
			ensure(OutPosts->Num() == Out.Num());
		}
		if (Post.IsValid())
		{
			FFighterState& State = TeamOf(TeamId).Fighters.FindOrAdd(Fighter);
			State.Post = Post;
			State.bPostSettled = false;
			State.PostWalks = 0;
			State.PostWalkFromCm = -1.f;
			UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: %s placed on a %s post %s (yaw %.0f, score %.2f, danger %.2f, range %.0f cm, %s)"), TeamId,
				*Fighter->DisplayName.ToString(), PmcTacticalRules::PostKindName(Post.Kind), *Post.Location.ToCompactString(), Post.Yaw, Post.Score, Post.Danger, Post.RangeCm, *Post.Group);
		}
	}
	SetRouteOf(TeamOf(TeamId), Route);
	return Out;
}

int32 UPmcSquadSubsystem::AssignTacticalPosts(int32 TeamId, const TArray<FVector>& Route, int32 Seed, float MinFromStartCm)
{
	FTeam& Team = TeamOf(TeamId);
	SetRouteOf(Team, Route);
	const TArray<AOperativeCharacter*> Fighters = GetTeamMembers(TeamId);
	TArray<FPmcPostCandidate> Candidates = GatherPostCandidates(Route, Seed, Team.Weights, MinFromStartCm);
	KeepCandidatesOnNav(Candidates);
	if (Candidates.IsEmpty() || Fighters.IsEmpty())
	{
		return 0;
	}
	const TArray<FPmcTacticalPost> Posts = PmcTacticalRules::PlanPlacement(PlacementRoles(Fighters, Team.Weights), Candidates, Team.Weights, Seed);
	int32 Posted = 0;
	for (int32 Index = 0; Index < Fighters.Num(); ++Index)
	{
		FFighterState& State = Team.Fighters.FindOrAdd(Fighters[Index]);
		State.Post = Posts[Index];
		State.bPostSettled = false;
		State.PostWalks = 0;
		State.PostWalkFromCm = -1.f;
		if (Posts[Index].IsValid())
		{
			++Posted;
			UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: %s gets a %s post %s (score %.2f, danger %.2f, range %.0f cm)"), TeamId, *Fighters[Index]->DisplayName.ToString(),
				PmcTacticalRules::PostKindName(Posts[Index].Kind), *Posts[Index].Location.ToCompactString(), Posts[Index].Score, Posts[Index].Danger, Posts[Index].RangeCm);
		}
	}
	return Posted;
}

void UPmcSquadSubsystem::ClearDefencePosts(FTeam& Team)
{
	for (TPair<TWeakObjectPtr<AOperativeCharacter>, FFighterState>& Pair : Team.Fighters)
	{
		FFighterState& State = Pair.Value;
		State.DefenceRole = EPmcDefenceRole::None;
		State.DefenceSector = INDEX_NONE;
		State.bHasDefencePost = false;
		State.DefencePost = FVector::ZeroVector;
		State.DefenceYaw = 0.f;
		State.DefenceSpotIndex = INDEX_NONE;
		State.DefenceArrivedAt = -1.0;
		State.DefencePostWalks = 0;
		State.DefencePostWalkFromCm = -1.f;
		State.bDefencePostDropped = false;
		State.bDefenceSettled = false;
		State.bFiringPosition = false;
		State.FiringStance = EOperativeStance::Crouching;
		State.bFiringCover = false;
		State.FiringSlot = FCoverSlot();
		// T-08: rebuilt by the refresh (the fall-back PHASE and post persist for the round).
		State.bZonePost = false;
		State.bFallingBack = false;
		State.bFallBackPlan = false;
		State.FallBackPlanPost = FFiringCandidate();
		State.FallBackPlanPathCm = -1.f;
	}
}

void UPmcSquadSubsystem::AssignFiringPositions(int32 TeamId, FTeam& Team, const TArray<AOperativeCharacter*>& Members, TArray<FPmcDefencePost>& Posts,
	TMap<int32, FFiringPosition>& OutPositions)
{
	UWorld* World = GetWorld();
	UFiringPositionSubsystem* Service = World ? World->GetSubsystem<UFiringPositionSubsystem>() : nullptr;
	// T-08: the holder and the kill zone of THIS refresh (ApplyDefenceRoles reads them).
	Team.Defence.HolderIndex = INDEX_NONE;
	Team.Defence.bHasKillZone = false;
	Team.Defence.KillZoneFromObjectiveCm = 0.f;
	if (!Service || !Service->GetConfig().bDefenceUse || !Team.Objective.IsSet())
	{
		return;
	}
	const FFiringPositionConfig& Config = Service->GetConfig();
	const FPmcObjectiveView& Objective = Team.Objective.GetValue();
	TArray<int32> Shooters;
	for (int32 Index = 0; Index < Posts.Num(); ++Index)
	{
		if (Posts[Index].Role == EPmcDefenceRole::SectorShooter && Posts[Index].bHasPost)
		{
			Shooters.Add(Index);
		}
	}
	if (Shooters.IsEmpty())
	{
		return;
	}
	Shooters.Sort([&Posts](int32 A, int32 B) { return Posts[A].Index < Posts[B].Index; });
	// T-08: a hammer that already fell back into the zone keeps his zone post (RefreshDefence applies it): he takes no firing position and is not the holder.
	Shooters.RemoveAll([&](int32 PostIndex)
		{
			for (AOperativeCharacter* Member : Members)
			{
				if (Member && Member->SquadIndex == Posts[PostIndex].Index)
				{
					const FFighterState* State = Team.Fighters.Find(Member);
					return State && State->FallBackPhase != EPmcFallBackPhase::None;
				}
			}
			return false;
		});
	if (Shooters.IsEmpty())
	{
		return;
	}
	// On the ground the shooter whose G008 post is nearest the objective stays on it as the occupier (keep_occupier; the brief's "others keep
	// their G008 role"): with every shooter seated out on the approach the point stood empty and the attackers won by capture.
	if (!Team.Defence.bRoof && Config.bKeepOccupier)
	{
		int32 Nearest = INDEX_NONE;
		double NearestDistSq = TNumericLimits<double>::Max();
		for (int32 Slot = 0; Slot < Shooters.Num(); ++Slot)
		{
			const double DistSq = FVector::DistSquared(Posts[Shooters[Slot]].Location, Objective.Location);
			if (DistSq < NearestDistSq)
			{
				NearestDistSq = DistSq;
				Nearest = Slot;
			}
		}
		if (Nearest != INDEX_NONE)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: squad index %d keeps his G008 post as the objective occupier (%.0f cm from the point)"), TeamId,
				Posts[Shooters[Nearest]].Index, FMath::Sqrt(NearestDistSq));
			Team.Defence.HolderIndex = Posts[Shooters[Nearest]].Index;
			Shooters.RemoveAt(Nearest);
		}
		if (Shooters.IsEmpty())
		{
			return;
		}
	}
	auto MemberOf = [&Members](int32 SquadIndex) -> AOperativeCharacter*
	{
		for (AOperativeCharacter* Member : Members)
		{
			if (Member && Member->SquadIndex == SquadIndex)
			{
				return Member;
			}
		}
		return nullptr;
	};
	const bool bRoof = Team.Defence.bRoof;
	// The kill zone. A ground objective: the point on the attackers' nav path (spawn -> objective) KillZoneReachFactor x the shooters' median reach before the objective
	// (contact happens out on the approach, so the crossfire pair is judged from there), the objective itself without a path. A roof: the street toward the attackers, 0.45 x the first man's reach out.
	AOperativeCharacter* Lead = MemberOf(Posts[Shooters[0]].Index);
	if (!Lead)
	{
		return;
	}
	FVector KillZone = Objective.Location;
	if (bRoof)
	{
		if (!Team.Defence.bHasApproach)
		{
			return;
		}
		const float LeadReach = Lead->CurrentWeapon ? Lead->CurrentWeapon->Stats.Range.MaxRangeCm : Config.DefaultReachCm;
		const FVector Toward = (Team.Defence.ApproachPoint - Objective.Location).GetSafeNormal2D();
		const float Reach = FMath::Min(static_cast<float>(FVector::Dist2D(Team.Defence.ApproachPoint, Objective.Location)), Config.RoofReachFactor * LeadReach);
		KillZone = FVector(Objective.Location.X, Objective.Location.Y, Team.Defence.ApproachPoint.Z) + Toward * Reach;
	}
	// The attackers' nav path (spawn -> objective): ONE synchronous query per defence refresh (a refresh is an event - new objective, approach, a death - not a tick). It
	// places the kill zone and is the clock of the reach-before-contact rule below.
	TArray<FVector> AttackerPath;
	TArray<float> Reaches;
	TArray<float> EffectiveReaches;
	for (const int32 PostIndex : Shooters)
	{
		const AOperativeCharacter* Member = MemberOf(Posts[PostIndex].Index);
		Reaches.Add(Member && Member->CurrentWeapon ? Member->CurrentWeapon->Stats.Range.MaxRangeCm : Config.DefaultReachCm);
	}
	// The arena's contact test: the median EFFECTIVE reach of the whole squad (every living man with a weapon).
	for (const AOperativeCharacter* Member : Members)
	{
		if (Member && Member->CurrentWeapon && Member->CurrentWeapon->Stats.Range.GetEffectiveRangeCm() > 0.f)
		{
			EffectiveReaches.Add(Member->CurrentWeapon->Stats.Range.GetEffectiveRangeCm());
		}
	}
	const float ContactReachCm = FiringPositionRules::MedianOf(EffectiveReaches);
	if (!bRoof && Team.Defence.bHasApproach && (Config.KillZoneReachFactor > 0.f || Config.bReachBeforeContact))
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		const UNavigationPath* Path = Nav ? Nav->FindPathToLocationSynchronously(World, Team.Defence.ApproachPoint, Objective.Location) : nullptr;
		if (Path && Path->IsValid() && !Path->IsPartial() && Path->PathPoints.Num() >= 2)
		{
			AttackerPath = Path->PathPoints;
		}
		if (AttackerPath.IsEmpty())
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: no full nav path from the approach to the objective: the kill zone stays on the objective, no reach-before-contact filter"), TeamId);
		}
	}
	if (!AttackerPath.IsEmpty() && Config.KillZoneReachFactor > 0.f)
	{
		const float WantedCm = Config.KillZoneReachFactor * FiringPositionRules::MedianOf(Reaches);
		FVector OnApproach = FVector::ZeroVector;
		float UsedCm = 0.f;
		if (FiringPositionRules::PointAlongPathBefore(AttackerPath, WantedCm, OnApproach, UsedCm))
		{
			KillZone = OnApproach;
			UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: kill zone on the approach %s (path %.1f m before the objective, %.1f m of path, wanted %.1f m = %.2f x median reach)"),
				TeamId, *KillZone.ToCompactString(), UsedCm / 100.f, FiringPositionRules::PolylineLength(AttackerPath) / 100.f, WantedCm / 100.f, Config.KillZoneReachFactor);
		}
	}
	// T-08: the ground kill zone (rule 2's yardstick: how far from the objective the ambush is set).
	if (!bRoof)
	{
		Team.Defence.bHasKillZone = true;
		Team.Defence.KillZone = KillZone;
		Team.Defence.KillZoneFromObjectiveCm = static_cast<float>(FVector::Dist2D(KillZone, Objective.Location));
	}
	// Reach before contact: ground defences only (a roof man's walk is a stair / ladder climb, his own G008 rules).
	const bool bReachFilter = Config.bReachBeforeContact && !bRoof && !AttackerPath.IsEmpty() && ContactReachCm > 0.f;
	float AttackerSpeedCm = 0.f;
	const float AreaRadius = FMath::Max(Objective.RadiusCm, Config.AreaRadiusFloorCm);
	FPmcDefenceConfig AreaConfig;
	AreaConfig.LevelToleranceCm = Objective.LevelToleranceCm;

	// Each man's own candidates: his weapon's reach is the band (0.2 .. 0.9 of it), so a shotgun man gets close cover and a rifleman far cover.
	struct FSeat
	{
		int32 PostIndex = INDEX_NONE;
		AOperativeCharacter* Fighter = nullptr;
		TArray<FFiringCandidate> Eligible;
		/** His nav path length (cm) to each stand point asked so far, -1 = no full path; shared by the tiers. */
		TMap<FIntVector, float> PathCm;
		int32 PathQueries = 0;
	};
	TArray<FSeat> Seats;
	UNavigationSystemV1* PathNav = bReachFilter ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	if (bReachFilter)
	{
		// The attackers' pace: the operative WALK speed (the same body, standing, no sprint) x attacker_speed_factor.
		AttackerSpeedCm = Config.AttackerSpeedFactor * OperativeMovementRules::ComputeMaxSpeed(Lead->MovementConfig, EOperativeStance::Standing, false, false, false)
			* Lead->GetColdSpeedMultiplier();
	}
	// ONE cover scan for the whole squad: the widest band of every man's fallback tier, so each man's (narrower) band sees a subset of the slots the wider ones see.
	float WidestBandMax = 0.f;
	for (const int32 PostIndex : Shooters)
	{
		if (const AOperativeCharacter* Member = MemberOf(Posts[PostIndex].Index))
		{
			float Unused = 0.f;
			float BandMaxOfMan = 0.f;
			FiringPositionRules::WeaponBand(Member->CurrentWeapon ? Member->CurrentWeapon->Stats.Range.MaxRangeCm : 0.f, 1, Config, Unused, BandMaxOfMan);
			WidestBandMax = FMath::Max(WidestBandMax, BandMaxOfMan);
		}
	}
	Service->PrepareCoverScan(KillZone, AreaRadius, WidestBandMax);
	// Garrison (ground only): a position must stand within garrison_max_reach_factor x the man's reach of the OBJECTIVE and see its centre (chest height) along a real shot line.
	const bool bGarrisonReach = !bRoof && Config.GarrisonMaxReachFactor > 0.f;
	const bool bGarrisonLos = !bRoof && Config.bGarrisonPointLos;
	const FCollisionQueryParams GarrisonParams = bGarrisonLos ? Service->MakeShotLineParams() : FCollisionQueryParams();
	const FVector GarrisonAim = Objective.Location + FVector(0.f, 0.f, Config.SampleHeightCm);
	for (const int32 PostIndex : Shooters)
	{
		FSeat Seat;
		Seat.PostIndex = PostIndex;
		Seat.Fighter = MemberOf(Posts[PostIndex].Index);
		if (!Seat.Fighter)
		{
			continue;
		}
		const float Reach = Seat.Fighter->CurrentWeapon ? Seat.Fighter->CurrentWeapon->Stats.Range.MaxRangeCm : Config.DefaultReachCm;
		float BandMin = 0.f;
		float BandMaxUnused = 0.f;
		FiringPositionRules::WeaponBand(Reach, 0, Config, BandMin, BandMaxUnused);
		// Tier 1: inside his weapon's band. Tier 2 (nothing there, e.g. a shotgun on an open street): cover up to twice as far - he holds there in cover
		// and closes in when the attackers come (the engagement-band orders); an idle man in the open is worse.
		for (int32 Tier = 0; Tier < 2 && Seat.Eligible.IsEmpty(); ++Tier)
		{
		float BandMax = 0.f;
		FiringPositionRules::WeaponBand(Reach, Tier, Config, BandMaxUnused, BandMax);
		int32 Valid = 0;
		int32 NotCover = 0;
		int32 OffLevel = 0;
		int32 Forward = 0;
		int32 GarrisonFar = 0;
		int32 GarrisonNoLos = 0;
		for (const FFiringCandidate& Candidate : Service->GetCandidates(KillZone, AreaRadius, BandMin, BandMax))
		{
			const FFiringPosition& Pos = Candidate.Position;
			if (!Pos.bValid)
			{
				continue;
			}
			++Valid;
			if (bRoof)
			{
				if (Pos.Kind != EFiringPositionKind::RoofEdge
					|| !PmcDefenceRules::IsInsideDefendedArea(Pos.StandPoint, Objective.Location, Team.Defence.LevelZ, Objective.RadiusCm, Team.Defence.Spots, AreaConfig))
				{
					continue;
				}
			}
			else
			{
				// On the ground a man takes a cover slot (the arena counts a defender on position when he is in cover); window stands and roof edges are not that.
				if (Pos.Kind != EFiringPositionKind::Corner && Pos.Kind != EFiringPositionKind::LowCover)
				{
					++NotCover;
					continue;
				}
				if (!PmcDefenceRules::IsGoalOnPostLevel(true, static_cast<float>(Pos.StandPoint.Z), static_cast<float>(Objective.Location.Z), Objective.LevelToleranceCm))
				{
					++OffLevel;
					continue;
				}
				// A defender never stands forward of his kill zone: nearer the attackers than the zone ALONG the approach axis by more than the margin (a flank position beside the zone is fine).
				if (Team.Defence.bHasApproach && FiringPositionRules::IsForwardOfKillZone(Pos.StandPoint, KillZone, Team.Defence.ApproachPoint, Config.ForwardMarginCm))
				{
					++Forward;
					continue;
				}
				if (bGarrisonReach && !FiringPositionRules::WithinGarrisonReach(Pos.StandPoint, Objective.Location, Reach, Config.GarrisonMaxReachFactor))
				{
					++GarrisonFar;
					continue;
				}
				if (bGarrisonLos && !Service->IsShotLineClear(Pos.StandPoint + FVector(0.f, 0.f, Config.MuzzleHeight(Pos.Stance)), GarrisonAim, GarrisonParams))
				{
					++GarrisonNoLos;
					continue;
				}
			}
			Seat.Eligible.Add(Candidate);
		}
		// Reach before contact: drop the positions this man cannot take before the attackers' expected contact. His speed is the one the defence post walk really uses before
		// he is under fire (the sprint, OperativeMovementRules::ComputeMaxSpeed); the clock of the attackers is their nav path up to where it first comes within the squad's
		// median effective reach of the position (PathLengthUntilWithin) at AttackerSpeedCm. Nearest candidates are asked first, at most max_path_queries per man.
		FString ReachNote;
		if (bReachFilter && !Seat.Eligible.IsEmpty())
		{
			const FVector Feet = PmcSquadLocal::FeetOf(*Seat.Fighter);
			const float DefenderSpeedCm = OperativeMovementRules::ComputeMaxSpeed(Seat.Fighter->MovementConfig, EOperativeStance::Standing, true, Seat.Fighter->IsWounded(), false)
				* Seat.Fighter->GetColdSpeedMultiplier() * Seat.Fighter->GetWoundTierSpeedScale();
			Seat.Eligible.StableSort([&Feet](const FFiringCandidate& A, const FFiringCandidate& B)
				{ return FVector::DistSquared(A.Position.StandPoint, Feet) < FVector::DistSquared(B.Position.StandPoint, Feet); });
			TArray<FFiringCandidate> Kept;
			int32 TooFar = 0;
			int32 NoPath = 0;
			int32 Unchecked = 0;
			float MinSlack = 999.f;
			float MaxSlack = -999.f;
			for (const FFiringCandidate& Candidate : Seat.Eligible)
			{
				const FVector& Stand = Candidate.Position.StandPoint;
				const float AttackerToContactCm = FiringPositionRules::PathLengthUntilWithin(AttackerPath, Stand, ContactReachCm);
				float Slack = 0.f;
				// A path is never shorter than the straight line: a candidate the straight line already loses needs no query.
				if (!FiringPositionRules::ReachableBeforeContact(static_cast<float>(FVector::Dist(Feet, Stand)), DefenderSpeedCm, AttackerToContactCm, AttackerSpeedCm, Config.ReachMarginS, Slack))
				{
					++TooFar;
					continue;
				}
				const FIntVector Key(FMath::RoundToInt(Stand.X), FMath::RoundToInt(Stand.Y), FMath::RoundToInt(Stand.Z));
				float* Known = Seat.PathCm.Find(Key);
				if (!Known)
				{
					if (Seat.PathQueries >= Config.MaxPathQueries)
					{
						++Unchecked;
						continue;
					}
					++Seat.PathQueries;
					const UNavigationPath* Path = PathNav ? PathNav->FindPathToLocationSynchronously(World, Seat.Fighter->GetActorLocation(), Stand) : nullptr;
					const float LengthCm = Path && Path->IsValid() && !Path->IsPartial() ? FiringPositionRules::PolylineLength(Path->PathPoints) : -1.f;
					Known = &Seat.PathCm.Add(Key, LengthCm);
				}
				if (*Known < 0.f)
				{
					++NoPath;
					continue;
				}
				if (!FiringPositionRules::ReachableBeforeContact(*Known, DefenderSpeedCm, AttackerToContactCm, AttackerSpeedCm, Config.ReachMarginS, Slack))
				{
					++TooFar;
					continue;
				}
				MinSlack = FMath::Min(MinSlack, Slack);
				MaxSlack = FMath::Max(MaxSlack, Slack);
				Kept.Add(Candidate);
			}
			ReachNote = FString::Printf(TEXT(", reachable %d of %d (slack %.1f..%.1f s; %d too late, %d no path, %d unchecked; %d path queries; walks %.0f cm/s, attackers %.0f cm/s)"),
				Kept.Num(), Seat.Eligible.Num(), Kept.IsEmpty() ? 0.f : MinSlack, Kept.IsEmpty() ? 0.f : MaxSlack, TooFar, NoPath, Unchecked, Seat.PathQueries, DefenderSpeedCm, AttackerSpeedCm);
			Seat.Eligible = MoveTemp(Kept);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: %s band %.0f..%.0f cm (tier %d): %d valid positions, %d not cover, %d off level, %d forward of the kill zone, %d too far from the objective, %d no line to the objective, %d eligible%s"), TeamId,
			*Seat.Fighter->DisplayName.ToString(), BandMin, BandMax, Tier, Valid, NotCover, OffLevel, Forward, GarrisonFar, GarrisonNoLos, Seat.Eligible.Num(), *ReachNote);
		}
		Seats.Add(MoveTemp(Seat));
	}
	if (Seats.IsEmpty())
	{
		return;
	}
	// The hammers (the first two shooters): the best position of the first, then the best partner of the second that crosses it (a roof: no crossfire, the best positions).
	TArray<FFiringPosition> Chosen;
	Chosen.SetNum(Seats.Num());
	TArray<bool> Has;
	Has.Init(false, Seats.Num());
	float CrossfireDeg = -1.f;
	if (!bRoof && Seats.Num() >= 2)
	{
		float BestSum = 0.f;
		int32 BestA = INDEX_NONE;
		int32 BestB = INDEX_NONE;
		for (int32 A = 0; A < Seats[0].Eligible.Num(); ++A)
		{
			const FFiringCandidate& First = Seats[0].Eligible[A];
			for (int32 B = 0; B < Seats[1].Eligible.Num(); ++B)
			{
				const FFiringCandidate& Second = Seats[1].Eligible[B];
				if (!FiringPositionRules::CrossfireOk(First.Position.StandPoint, Second.Position.StandPoint, KillZone, Config.CrossfireMinDeg, Config.LineClearCm))
				{
					continue;
				}
				const float Sum = First.Score * FiringPositionRules::SpacingOf(First.Position, TConstArrayView<FFiringPosition>(), TConstArrayView<FString>(), Config)
					+ Second.Score * FiringPositionRules::SpacingOf(Second.Position, MakeArrayView(&First.Position, 1), TConstArrayView<FString>(), Config);
				if (Sum > BestSum)
				{
					BestSum = Sum;
					BestA = A;
					BestB = B;
				}
			}
		}
		if (BestA != INDEX_NONE)
		{
			Chosen[0] = Seats[0].Eligible[BestA].Position;
			Chosen[1] = Seats[1].Eligible[BestB].Position;
			Has[0] = Has[1] = true;
			CrossfireDeg = FiringPositionRules::CrossfireAngleDeg(Chosen[0].StandPoint, Chosen[1].StandPoint, KillZone);
		}
	}
	// Everybody else (and the hammers when no pair exists): his best position, spaced from the chosen ones, one cover object each.
	int32 Taken = 0;
	for (int32 Man = 0; Man < Seats.Num(); ++Man)
	{
		if (!Has[Man])
		{
			TArray<FFiringPosition> Others;
			for (int32 Other = 0; Other < Seats.Num(); ++Other)
			{
				if (Has[Other])
				{
					Others.Add(Chosen[Other]);
				}
			}
			float BestScore = 0.f;
			for (const FFiringCandidate& Candidate : Seats[Man].Eligible)
			{
				const float Score = Candidate.Score * FiringPositionRules::SpacingOf(Candidate.Position, Others, TConstArrayView<FString>(), Config);
				if (Score > BestScore)
				{
					BestScore = Score;
					Chosen[Man] = Candidate.Position;
					Has[Man] = true;
				}
			}
		}
		Taken += Has[Man] ? 1 : 0;
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: firing positions on %s (kill zone r %.0f cm): %d of %d shooters seated%s"), TeamId, *KillZone.ToCompactString(), AreaRadius, Taken,
		Seats.Num(), bRoof ? TEXT(" (roof)") : TEXT(""));
	for (int32 Man = 0; Man < Seats.Num(); ++Man)
	{
		AOperativeCharacter* Fighter = Seats[Man].Fighter;
		if (!Has[Man])
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: %s keeps his G008 post: no firing position in his reach (%d eligible)"), TeamId, *Fighter->DisplayName.ToString(),
				Seats[Man].Eligible.Num());
			continue;
		}
		const FFiringPosition& Pos = Chosen[Man];
		if (!HasFullNavPath(Fighter->GetActorLocation(), Pos.StandPoint))
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: %s keeps his G008 post: no full path to the firing position %s %s"), TeamId, *Fighter->DisplayName.ToString(),
				FiringPositionRules::KindName(Pos.Kind), *Pos.SourceId);
			continue;
		}
		FPmcDefencePost& Post = Posts[Seats[Man].PostIndex];
		Post.Location = Pos.StandPoint;
		Post.Yaw = Pos.FacingYaw;
		Post.SpotIndex = INDEX_NONE;
		Post.Sector = INDEX_NONE; // no wedge: the sector shift would move him back to a G008 spot
		OutPositions.Add(Post.Index, Pos);
		UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: %s firing position %s %s at %s %s covers %.2f crossfire %.0f deg"), TeamId, *Fighter->DisplayName.ToString(),
			FiringPositionRules::KindName(Pos.Kind), *Pos.SourceId, *Pos.StandPoint.ToCompactString(), FiringPositionRules::StanceName(Pos.Stance), Pos.CoveredShare, CrossfireDeg);
	}
}

// --- T-08 defence roles (the holder in the zone, the hammers' fall-back) ---------------------------------------------------------

const FPmcDefenceRolesConfig& UPmcSquadSubsystem::GetRolesConfig()
{
	if (!RolesConfigCache.IsSet())
	{
		RolesConfigCache = PmcDefenceRoles::LoadConfig();
	}
	return RolesConfigCache.GetValue();
}

void UPmcSquadSubsystem::BuildZonePosts(int32 TeamId, FTeam& Team)
{
	Team.Defence.bZonePostsBuilt = true;
	Team.Defence.ZonePosts.Reset();
	UWorld* World = GetWorld();
	UFiringPositionSubsystem* Service = World ? World->GetSubsystem<UFiringPositionSubsystem>() : nullptr;
	if (!World || !Service || !Team.Objective.IsSet())
	{
		return;
	}
	const FPmcDefenceRolesConfig& Roles = GetRolesConfig();
	const FPmcObjectiveView& Objective = Team.Objective.GetValue();
	// The threat the zone posts shield from: the kill zone on the approach (where the attackers come from), else the attackers' spawn.
	FVector Threat = FVector::ZeroVector;
	if (Team.Defence.bHasKillZone && Team.Defence.KillZoneFromObjectiveCm > 0.f)
	{
		Threat = Team.Defence.KillZone;
	}
	else if (Team.Defence.bHasApproach)
	{
		Threat = Team.Defence.ApproachPoint;
	}
	else
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: zone posts: no approach known, no covered zone posts"), TeamId);
		return;
	}
	const double StartSeconds = FPlatformTime::Seconds();
	const float Radius = Objective.RadiusCm;
	TArray<FVector> Origins;
	Origins.Add(Objective.Location);
	for (const float Fraction : { 0.5f, 0.95f })
	{
		for (int32 Step = 0; Step < 8; ++Step)
		{
			Origins.Add(Objective.Location + FRotator(0.f, Step * 45.f, 0.f).Vector() * (Radius * Fraction));
		}
	}
	const FCollisionQueryParams Params = Service->MakeShotLineParams();
	const float SampleHeightCm = Service->GetConfig().SampleHeightCm;
	const float StandingMuzzleCm = Service->GetConfig().MuzzleHeight(EOperativeStance::Standing);
	TSet<FString> Cells;
	TSet<FString> SlotIds;
	TArray<FFiringCandidate> Found;
	int32 Rays = 0;
	int32 Slots = 0;
	int32 TooFar = 0;
	int32 OffLevel = 0;
	int32 WeakCover = 0;
	float NearestSlotCm = -1.f;
	for (const FVector& Origin : Origins)
	{
		FVector OnNav;
		if (!ProjectToNavAtLevel(Origin, OnNav) || FVector::Dist2D(OnNav, Objective.Location) > Radius * 1.1f)
		{
			continue;
		}
		const FVector Chest = OnNav + FVector(0.f, 0.f, 100.f);
		for (int32 Ray = 0; Ray < 16; ++Ray)
		{
			++Rays;
			const FVector Direction = FRotator(0.f, Ray * 22.5f, 0.f).Vector();
			// One slot attempt per 50 cm wall cell (FindCoverSlotAt is ~25 traces).
			const TFunction<bool(const FHitResult&)> Accept = [&Cells](const FHitResult& Hit)
			{
				const FVector Point = Hit.ImpactPoint;
				bool bAlready = false;
				Cells.Add(FString::Printf(TEXT("%s|%d,%d,%d"), Hit.GetActor() ? *Hit.GetActor()->GetPathName() : TEXT(""), FMath::FloorToInt(static_cast<float>(Point.X) / 50.f),
					FMath::FloorToInt(static_cast<float>(Point.Y) / 50.f), FMath::FloorToInt(static_cast<float>(Point.Z) / 50.f)), &bAlready);
				return !bAlready;
			};
			FCoverSlot Slot;
			if (!CoverSlotAlongRay(World, Chest, Direction, Roles.ZoneRayReachCm, Params, Slot, Accept))
			{
				continue;
			}
			++Slots;
			const FVector Stand = Slot.WorldLocation;
			const float DistCm = static_cast<float>(FVector::Dist2D(Stand, Objective.Location));
			NearestSlotCm = NearestSlotCm < 0.f ? DistCm : FMath::Min(NearestSlotCm, DistCm);
			if (DistCm > Radius)
			{
				++TooFar;
				continue;
			}
			if (!PmcDefenceRules::IsGoalOnPostLevel(true, static_cast<float>(Stand.Z), static_cast<float>(Objective.Location.Z), Objective.LevelToleranceCm))
			{
				++OffLevel;
				continue;
			}
			const float Quality = PmcCommanderRules::CoverQualityAgainst(Slot.WorldLocation, Slot.WallNormal, Threat);
			bool bAlreadyHave = false;
			SlotIds.Add(PmcSquadLocal::StableCoverSlotId(Slot, false), &bAlreadyHave);
			if (Quality < Roles.ZoneMinQuality || bAlreadyHave)
			{
				WeakCover += bAlreadyHave ? 0 : 1;
				continue;
			}
			// Can he fire on the approach from it? A corner to lean round, or a clear standing line over a low wall.
			const bool bSeesApproach = Slot.HasExposedEdge()
				|| Service->IsShotLineClear(Stand + FVector(0.f, 0.f, StandingMuzzleCm), Threat + FVector(0.f, 0.f, SampleHeightCm), Params);
			FFiringCandidate Candidate;
			FFiringPosition& Pos = Candidate.Position;
			Pos.Kind = Slot.Height == ECoverHeight::HighCover ? EFiringPositionKind::Corner : EFiringPositionKind::LowCover;
			Pos.StandPoint = Stand;
			Pos.Stance = EOperativeStance::Crouching;
			Pos.FacingYaw = PmcDefenceRules::YawDegrees(Stand, Threat);
			Pos.CoveredShare = Quality;
			const FString WallId = Slot.WallActor.IsValid() ? Slot.WallActor->GetPathName() : FString();
			Pos.CoverKey = PmcDefenceRules::CoverObjectKey(WallId, Slot.WallPoint);
			Pos.SourceId = FString::Printf(TEXT("%s@%s"), Slot.WallActor.IsValid() ? *Slot.WallActor->GetName() : TEXT("wall"), *Slot.WallPoint.ToCompactString());
			Pos.Slot = Slot;
			Pos.bHasSlot = true;
			Pos.bValid = true;
			Candidate.Score = PmcDefenceRoles::ZonePostScore(Quality, bSeesApproach, DistCm, Radius, Roles);
			Found.Add(Candidate);
		}
	}
	Found.StableSort([](const FFiringCandidate& A, const FFiringCandidate& B)
		{ return A.Score != B.Score ? A.Score > B.Score : A.Position.SourceId.Compare(B.Position.SourceId, ESearchCase::CaseSensitive) < 0; });
	if (Found.Num() > Roles.MaxZonePosts)
	{
		Found.SetNum(Roles.MaxZonePosts);
	}
	Team.Defence.ZonePosts = MoveTemp(Found);
	const FString Best = Team.Defence.ZonePosts.IsEmpty() ? FString(TEXT("-"))
		: FString::Printf(TEXT("%s %s score %.2f"), FiringPositionRules::KindName(Team.Defence.ZonePosts[0].Position.Kind), *Team.Defence.ZonePosts[0].Position.SourceId, Team.Defence.ZonePosts[0].Score);
	UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: zone posts: %d covered slots inside the zone (r %.0f cm, %d origins, %d rays, %d wall slots: %d outside the zone, %d off level, %d weak cover vs the approach; nearest slot %.1f m from the point) in %.1f ms; best %s"), TeamId,
		Team.Defence.ZonePosts.Num(), Radius, Origins.Num(), Rays, Slots, TooFar, OffLevel, WeakCover, NearestSlotCm / 100.f, (FPlatformTime::Seconds() - StartSeconds) * 1000.0, *Best);
}

void UPmcSquadSubsystem::ApplyDefenceRoles(int32 TeamId, FTeam& Team, const TArray<AOperativeCharacter*>& Members, TArray<FPmcDefencePost>& Posts,
	TMap<int32, FFiringPosition>& InOutPositions, TMap<int32, FFallBackPlanEntry>& OutPlans, TSet<int32>& OutZoneMen)
{
	const FPmcDefenceRolesConfig& Roles = GetRolesConfig();
	UWorld* World = GetWorld();
	if (!Roles.bEnabled || !World || !Team.Objective.IsSet() || Team.Defence.bRoof)
	{
		return;
	}
	if (!Team.Defence.bZonePostsBuilt)
	{
		BuildZonePosts(TeamId, Team);
	}
	const TArray<FFiringCandidate>& ZonePosts = Team.Defence.ZonePosts;
	auto MemberOf = [&Members](int32 SquadIndex) -> AOperativeCharacter*
	{
		for (AOperativeCharacter* Member : Members)
		{
			if (Member && Member->SquadIndex == SquadIndex)
			{
				return Member;
			}
		}
		return nullptr;
	};
	// The zone posts already held: the men who fell back keep theirs.
	TArray<FVector> ClaimedStands;
	TArray<FString> ClaimedKeys;
	for (AOperativeCharacter* Member : Members)
	{
		const FFighterState* State = Member ? Team.Fighters.Find(Member) : nullptr;
		if (State && State->FallBackPhase != EPmcFallBackPhase::None)
		{
			ClaimedStands.Add(State->FallBackPost.Position.StandPoint);
			ClaimedKeys.Add(State->FallBackPost.Position.CoverKey);
		}
	}
	// The holder: the best covered post inside the zone he can walk to; none = he keeps his G008 post.
	const int32 HolderIndex = Team.Defence.HolderIndex;
	if (Roles.bHolderCover && HolderIndex != INDEX_NONE)
	{
		AOperativeCharacter* Holder = MemberOf(HolderIndex);
		FPmcDefencePost* HolderPost = Posts.FindByPredicate([HolderIndex](const FPmcDefencePost& Post) { return Post.Index == HolderIndex; });
		if (Holder && HolderPost)
		{
			TArray<int32> Excluded;
			bool bPlaced = false;
			for (int32 Try = 0; Try < 4 && !bPlaced; ++Try)
			{
				const int32 Pick = PmcDefenceRoles::PickZonePost(ZonePosts, Excluded, ClaimedStands, ClaimedKeys, Roles);
				if (Pick == INDEX_NONE)
				{
					break;
				}
				const FFiringPosition& Pos = ZonePosts[Pick].Position;
				// A full nav route that does not cross the kill zone (a holder who was a hammer walks in from his firing position).
				UNavigationSystemV1* HolderNav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
				const UNavigationPath* HolderPath = HolderNav ? HolderNav->FindPathToLocationSynchronously(World, Holder->GetActorLocation(), Pos.StandPoint) : nullptr;
				const bool bRouteOk = HolderPath && HolderPath->IsValid() && !HolderPath->IsPartial() && HolderPath->PathPoints.Num() >= 2
					&& !(Team.Defence.bHasKillZone && Team.Defence.KillZoneFromObjectiveCm > 0.f
						&& PmcDefenceRoles::PathCrossesKillZone(HolderPath->PathPoints, Team.Defence.KillZone, Roles.KillZoneClearCm));
				if (bRouteOk)
				{
					HolderPost->Location = Pos.StandPoint;
					HolderPost->Yaw = Pos.FacingYaw;
					HolderPost->SpotIndex = INDEX_NONE;
					HolderPost->Sector = INDEX_NONE; // no wedge: the sector shift would move him back to a G008 spot
					InOutPositions.Add(HolderIndex, Pos);
					OutZoneMen.Add(HolderIndex);
					ClaimedStands.Add(Pos.StandPoint);
					ClaimedKeys.Add(Pos.CoverKey);
					bPlaced = true;
					UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: %s is the holder: covered zone post %s %s at %s (%.1f m from the point, shields %.2f, score %.2f)"), TeamId,
						*Holder->DisplayName.ToString(), FiringPositionRules::KindName(Pos.Kind), *Pos.SourceId, *Pos.StandPoint.ToCompactString(),
						FVector::Dist2D(Pos.StandPoint, Team.Objective->Location) / 100.0, Pos.CoveredShare, ZonePosts[Pick].Score);
				}
				else
				{
					Excluded.Add(Pick);
				}
			}
			if (!bPlaced)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: %s is the holder but the zone has no covered post he can reach (%d posts): he keeps his G008 post"), TeamId,
					*Holder->DisplayName.ToString(), ZonePosts.Num());
			}
		}
	}
	// The hammers' fall-back plans: the best free zone post whose nav route from the firing position does not cross the kill zone.
	if (!Roles.bFallBack)
	{
		return;
	}
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	for (const TPair<int32, FFiringPosition>& Entry : InOutPositions)
	{
		AOperativeCharacter* Hammer = MemberOf(Entry.Key);
		if (!Hammer || Entry.Key == HolderIndex || OutZoneMen.Contains(Entry.Key))
		{
			continue;
		}
		const FFiringPosition& From = Entry.Value;
		TArray<int32> Excluded;
		int32 Crossing = 0;
		int32 NoPath = 0;
		bool bPlanned = false;
		for (int32 Query = 0; Query < Roles.MaxPathQueries && !bPlanned; ++Query)
		{
			const int32 Pick = PmcDefenceRoles::PickZonePost(ZonePosts, Excluded, ClaimedStands, ClaimedKeys, Roles);
			if (Pick == INDEX_NONE)
			{
				break;
			}
			Excluded.Add(Pick);
			const FFiringPosition& Zone = ZonePosts[Pick].Position;
			const UNavigationPath* Path = Nav ? Nav->FindPathToLocationSynchronously(World, From.StandPoint, Zone.StandPoint) : nullptr;
			if (!Path || !Path->IsValid() || Path->IsPartial() || Path->PathPoints.Num() < 2)
			{
				++NoPath;
				continue;
			}
			if (Team.Defence.bHasKillZone && Team.Defence.KillZoneFromObjectiveCm > 0.f && PmcDefenceRoles::PathCrossesKillZone(Path->PathPoints, Team.Defence.KillZone, Roles.KillZoneClearCm))
			{
				++Crossing;
				continue;
			}
			FFallBackPlanEntry Plan;
			Plan.Post = ZonePosts[Pick];
			Plan.PathCm = FiringPositionRules::PolylineLength(Path->PathPoints);
			OutPlans.Add(Entry.Key, Plan);
			ClaimedStands.Add(Zone.StandPoint);
			ClaimedKeys.Add(Zone.CoverKey);
			bPlanned = true;
			UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: %s fall-back plan: zone post %s %s at %s, route %.1f m from his firing position (%d routes crossed the kill zone, %d had no path)"), TeamId,
				*Hammer->DisplayName.ToString(), FiringPositionRules::KindName(Zone.Kind), *Zone.SourceId, *Zone.StandPoint.ToCompactString(), Plan.PathCm / 100.f, Crossing, NoPath);
		}
		if (!bPlanned)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: %s has no fall-back plan (%d zone posts, %d routes crossed the kill zone, %d had no path): he holds his firing position"), TeamId,
				*Hammer->DisplayName.ToString(), ZonePosts.Num(), Crossing, NoPath);
		}
	}
}

void UPmcSquadSubsystem::UpdateFallBack(int32 TeamId, FTeam& Team, const TArray<AOperativeCharacter*>& Mates, double Now)
{
	const FPmcDefenceRolesConfig& Roles = GetRolesConfig();
	if (!Roles.bEnabled || Team.Defence.bRoof || !Team.Objective.IsSet())
	{
		return;
	}
	UWorld* World = GetWorld();
	const UFiringPositionSubsystem* Service = World ? World->GetSubsystem<UFiringPositionSubsystem>() : nullptr;
	if (!Service)
	{
		return;
	}
	const FPmcObjectiveView& Objective = Team.Objective.GetValue();
	auto InZone = [&Objective](const AOperativeCharacter& Man)
	{
		const FVector Feet = PmcSquadLocal::FeetOf(Man);
		return PmcCommanderRules::IsSameLevel(static_cast<float>(Feet.Z), static_cast<float>(Objective.Location.Z), Objective.LevelToleranceCm)
			&& FVector::Dist2D(Feet, Objective.Location) <= Objective.RadiusCm;
	};

	// 1. Arrivals; a zone man (holder / fallen-back hammer) at his post but not in cover takes it - the commander's Ambush order does not run in contact on a post.
	for (AOperativeCharacter* Mate : Mates)
	{
		FFighterState* State = Team.Fighters.Find(Mate);
		if (!State)
		{
			continue;
		}
		const FVector Feet = PmcSquadLocal::FeetOf(*Mate);
		const bool bAtPost = State->bHasDefencePost && PmcTacticalRules::IsAtPost(Feet, State->DefencePost, PostArriveCm, Team.Weights.LevelToleranceCm);
		if (State->FallBackPhase == EPmcFallBackPhase::Moving)
		{
			const bool bTimedOut = Now - State->FallBackStartedAt > Roles.MoveTimeoutS;
			if (bAtPost || bTimedOut)
			{
				State->FallBackPhase = EPmcFallBackPhase::InZone;
				State->bFallingBack = false;
				UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: %s %s the zone post (%.1f s, %.1f m from the point)"), TeamId, *Mate->DisplayName.ToString(),
					bAtPost ? TEXT("is on") : TEXT("did not reach (timed out), counts as in"), Now - State->FallBackStartedAt, FVector::Dist2D(Feet, Objective.Location) / 100.0);
			}
		}
		if (State->bZonePost && State->bFiringCover && bAtPost && !State->bFallingBack && !Mate->bInCover && !Mate->HasOwnMoveOrder()
			&& Now - State->ZoneCoverTriedAt >= 1.5)
		{
			State->ZoneCoverTriedAt = Now;
			FSquadOrderContext Context;
			Context.Operative = Mate;
			Context.Cover = State->FiringSlot;
			Context.bSprint = false;
			SquadOrderCatalog::Execute(ESquadOrder::TakeCover, Context);
		}
	}
	if (!Roles.bFallBack)
	{
		return;
	}

	// 2. What the squad knows: the nearest attacker it saw lately, the attackers it saw go down, whether anybody stands in the zone.
	float Nearest = -1.f;
	for (const TPair<TWeakObjectPtr<AActor>, FKnownTarget>& Pair : Team.Known)
	{
		const AOperativeCharacter* Enemy = Cast<AOperativeCharacter>(Pair.Value.Actor.Get());
		if (!Enemy || !PmcSquadLocal::IsLiveOperative(Enemy))
		{
			continue;
		}
		const double Age = Pair.Value.bSeenNow ? 0.0 : Now - Pair.Value.LastSeenTime;
		if (Age > Roles.KnownMaxAgeS)
		{
			continue;
		}
		const float Dist = static_cast<float>(FVector::Dist2D(Pair.Value.LastKnown, Objective.Location));
		Nearest = Nearest < 0.f ? Dist : FMath::Min(Nearest, Dist);
	}
	int32 Down = 0;
	for (const TWeakObjectPtr<AActor>& Seen : Team.EnemiesSeen)
	{
		const AOperativeCharacter* Enemy = Cast<AOperativeCharacter>(Seen.Get());
		Down += (!Enemy || !PmcSquadLocal::IsLiveOperative(Enemy)) ? 1 : 0;
	}
	bool bZoneHeld = false;
	bool bAnyMoving = false;
	for (AOperativeCharacter* Mate : Mates)
	{
		const FFighterState* State = Team.Fighters.Find(Mate);
		bZoneHeld |= !Mate->IsKnockedDown() && InZone(*Mate);
		bAnyMoving |= State && State->FallBackPhase == EPmcFallBackPhase::Moving;
	}

	// 3. The hammers: the rules, then bounding (one mover at a time).
	const bool bFinish = Down >= Roles.FinishDownMin;
	TArray<FPmcMoverCandidate> Candidates;
	TMap<int32, FPmcFallBackDecision> Decisions;
	for (AOperativeCharacter* Mate : Mates)
	{
		FFighterState* State = Team.Fighters.Find(Mate);
		if (!State || !State->bFallBackPlan || State->FallBackPhase != EPmcFallBackPhase::None || Mate->IsKnockedDown())
		{
			continue;
		}
		const float ColdSpeed = Mate->GetColdSpeedMultiplier();
		FPmcFallBackSituation Situation;
		Situation.bZoneHeld = bZoneHeld;
		Situation.NearestAttackerFromObjectiveCm = Nearest;
		Situation.ObjectiveRadiusCm = Objective.RadiusCm;
		Situation.KillZoneFromObjectiveCm = Team.Defence.KillZoneFromObjectiveCm;
		Situation.AttackersDown = Down;
		Situation.bDefenceHasFired = Team.Defence.FirstShotAt >= 0.0;
		Situation.AttackerSpeedCm = Service->GetConfig().AttackerSpeedFactor * OperativeMovementRules::ComputeMaxSpeed(Mate->MovementConfig, EOperativeStance::Standing, false, false, false) * ColdSpeed;
		Situation.HammerSpeedCm = OperativeMovementRules::ComputeMaxSpeed(Mate->MovementConfig, EOperativeStance::Standing, true, Mate->IsWounded(), false) * ColdSpeed
			* Mate->GetWoundTierSpeedScale();
		Situation.HammerPathCm = State->FallBackPlanPathCm;
		FPmcFallBackDecision Decision = PmcDefenceRoles::DecideFallBack(Situation, Roles);
		const bool bFollows = Team.Defence.bFallBackLatched; // a fall-back was ordered: the other hammers follow in turn
		if ((Decision.Rules != 0 || bFollows) && bFinish)
		{
			if (!State->bFallBackHoldLogged)
			{
				State->bFallBackHoldLogged = true;
				UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: %s holds (>= %d down: %d of %d seen; rules %s%s)"), TeamId, *Mate->DisplayName.ToString(), Roles.FinishDownMin, Down,
					Team.EnemiesSeen.Num(), *PmcDefenceRoles::RuleList(Decision.Rules), bFollows ? TEXT(", the fall-back had been ordered") : TEXT(""));
			}
			continue;
		}
		if (Decision.Rules == 0 && !bFollows)
		{
			State->bFallBackHoldLogged = false;
			continue;
		}
		Decision.bFallBack = true;
		Decisions.Add(Mate->SquadIndex, Decision);
		FPmcMoverCandidate Candidate;
		Candidate.Index = Mate->SquadIndex;
		Candidate.PathTimeS = Situation.HammerSpeedCm > 0.f ? State->FallBackPlanPathCm / Situation.HammerSpeedCm : 999.f;
		Candidates.Add(Candidate);
	}
	const int32 Picked = PmcDefenceRoles::PickMover(Candidates, bAnyMoving, static_cast<float>(Now - Team.Defence.LastMoverStartAt), Roles);
	if (Picked == INDEX_NONE)
	{
		return;
	}
	for (AOperativeCharacter* Mate : Mates)
	{
		FFighterState* State = Team.Fighters.Find(Mate);
		if (!State || Mate->SquadIndex != Picked)
		{
			continue;
		}
		const FPmcFallBackDecision& Decision = Decisions.FindChecked(Picked);
		const FFiringPosition& Zone = State->FallBackPlanPost.Position;
		FVector OnNav;
		const FVector Post = ProjectToNavAtLevel(Zone.StandPoint, OnNav) ? OnNav : Zone.StandPoint;
		const uint8 Rules = Decision.Rules != 0 ? Decision.Rules : Team.Defence.LatchedRules;
		FString Why;
		if (Decision.Rules != 0)
		{
			Why = FString::Printf(TEXT("%s%s%s"), (Decision.Rules & static_cast<uint8>(EPmcFallBackRule::ZoneEmptying)) ? TEXT("zone_emptying ") : TEXT(""),
				(Decision.Rules & static_cast<uint8>(EPmcFallBackRule::Bypassed)) ? TEXT("bypassed ") : TEXT(""), (Decision.Rules & static_cast<uint8>(EPmcFallBackRule::NotInTime)) ? TEXT("not_in_time") : TEXT(""));
			Why.TrimEndInline();
		}
		else
		{
			Why = TEXT("follows the ordered fall-back, bounding");
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: %s falls back (rule %s: %s; nearest attacker %.1f m from the point, kill zone %.1f m out, zone %s, %d of %d seen down, slack %.1f s, route %.1f m to %s %s)"),
			TeamId, *Mate->DisplayName.ToString(), *PmcDefenceRoles::RuleList(Rules), *Why, Nearest < 0.f ? -1.f : Nearest / 100.f, Team.Defence.KillZoneFromObjectiveCm / 100.f,
			bZoneHeld ? TEXT("held") : TEXT("EMPTY"), Down, Team.EnemiesSeen.Num(), Decision.SlackS, State->FallBackPlanPathCm / 100.f, FiringPositionRules::KindName(Zone.Kind), *Zone.SourceId);
		Team.Defence.bFallBackLatched = true;
		Team.Defence.LatchedRules |= Decision.Rules;
		Team.Defence.LastMoverStartAt = Now;
		State->FallBackPhase = EPmcFallBackPhase::Moving;
		State->FallBackPost = State->FallBackPlanPost;
		State->FallBackStartedAt = Now;
		State->bFallingBack = true;
		State->bZonePost = true;
		State->DefencePost = Post;
		State->DefenceYaw = Zone.FacingYaw;
		State->DefenceSpotIndex = INDEX_NONE;
		State->DefenceSector = INDEX_NONE;
		State->bFiringPosition = true;
		State->FiringStance = Zone.Stance;
		State->bFiringCover = Zone.bHasSlot;
		State->FiringSlot = Zone.Slot;
		State->DefenceArrivedAt = -1.0;
		State->DefencePostWalks = 0;
		State->DefencePostWalkFromCm = -1.f;
		State->bDefencePostDropped = false;
		State->bDefenceSettled = false;
		break;
	}
}

void UPmcSquadSubsystem::DropDefencePost(int32 TeamId, FTeam& Team, AOperativeCharacter& Fighter, FFighterState& State)
{
	const FVector Feet = PmcSquadLocal::FeetOf(Fighter);
	const FVector Failed = State.DefencePost;
	FVector NewPost = Feet;
	int32 NewSpot = INDEX_NONE;
	// The first give-up tries the roof spots, nearest first. A second one (that spot failed too) holds where he stands.
	if (!State.bDefencePostDropped)
	{
		TArray<FVector> Claimed;
		TArray<FString> ClaimedKeys;
		CollectDefenceClaims(Team, Fighter, Claimed, ClaimedKeys);
		TArray<int32> Order;
		for (int32 Index = 0; Index < Team.Defence.Spots.Num(); ++Index)
		{
			Order.Add(Index);
		}
		const TArray<FPmcDefenceSpot>& Spots = Team.Defence.Spots;
		Order.Sort([&Spots, &Feet](int32 A, int32 B) { return FVector::DistSquared(Spots[A].Location, Feet) < FVector::DistSquared(Spots[B].Location, Feet); });
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		for (const int32 Index : Order)
		{
			FVector OnNav;
			if (!ProjectToNavAtLevel(Spots[Index].Location, OnNav) || FVector::Dist2D(OnNav, Failed) <= PostArriveCm)
			{
				continue;
			}
			if (Claimed.ContainsByPredicate([&OnNav](const FVector& Other) { return FVector::Dist2D(Other, OnNav) <= PostArriveCm; }))
			{
				continue;
			}
			const UNavigationPath* Path = Nav ? Nav->FindPathToLocationSynchronously(GetWorld(), Fighter.GetActorLocation(), OnNav) : nullptr;
			if (Path && Path->IsValid() && !Path->IsPartial())
			{
				NewPost = OnNav;
				NewSpot = Index;
				break;
			}
		}
	}
	if (NewSpot == INDEX_NONE)
	{
		FVector OnNav;
		if (ProjectToNav(Feet, OnNav))
		{
			NewPost = OnNav;
		}
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: %s gives up his %s post %s after %d short walks; sector shooter at %s (%s)"), TeamId,
		*Fighter.DisplayName.ToString(), PmcDefenceRules::RoleName(State.DefenceRole), *Failed.ToCompactString(), State.DefencePostWalks,
		*NewPost.ToCompactString(), NewSpot != INDEX_NONE ? TEXT("nearest reachable roof spot") : TEXT("where he stands"));
	State.DefenceRole = EPmcDefenceRole::SectorShooter;
	// No wedge: he does not shift along one (the shift needs a sector).
	State.DefenceSector = INDEX_NONE;
	State.DefenceSpotIndex = NewSpot;
	State.DefencePost = NewPost;
	if (Team.Defence.bHasApproach)
	{
		State.DefenceYaw = Team.Defence.ApproachYawDeg;
	}
	State.DefenceArrivedAt = -1.0;
	State.DefencePostWalks = 0;
	State.DefencePostWalkFromCm = -1.f;
	State.bDefencePostDropped = true;
	State.bDefenceSettled = false;
}

void UPmcSquadSubsystem::CollectDefenceClaims(const FTeam& Team, const AOperativeCharacter& Fighter, TArray<FVector>& OutPoints, TArray<FString>& OutKeys) const
{
	OutPoints.Reset();
	OutKeys.Reset();
	if (!Team.Objective.IsSet() || Team.Objective->Role != EPmcRole::Defender)
	{
		return;
	}
	for (const TPair<TWeakObjectPtr<AOperativeCharacter>, FFighterState>& Pair : Team.Fighters)
	{
		const AOperativeCharacter* Mate = Pair.Key.Get();
		if (!PmcSquadLocal::IsLiveOperative(Mate) || Mate == &Fighter)
		{
			continue;
		}
		const FFighterState& State = Pair.Value;
		if (Mate->bInCover)
		{
			const FCoverSlot& Slot = Mate->GetCoverSlot();
			const FString WallId = Slot.WallActor.IsValid() ? Slot.WallActor->GetPathName() : FString();
			OutPoints.Add(Slot.WorldLocation);
			OutKeys.Add(PmcDefenceRules::CoverObjectKey(WallId, Slot.WallPoint));
		}
		else if (State.bHasDefencePost)
		{
			OutPoints.Add(State.DefencePost);
			OutKeys.Add(PmcDefenceRules::CoverObjectKey(FString(), State.DefencePost));
		}
	}
}

const FPmcLayoutAccess* UPmcSquadSubsystem::DefenceLayout()
{
	if (bDefenceLayoutTried)
	{
		return DefenceLayoutCache.IsSet() ? &DefenceLayoutCache.GetValue() : nullptr;
	}
	const UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	bDefenceLayoutTried = true;
	// The map's own LevelJson, not -LevelJson=. The arena combat override has no levelgen_layout.
	const FString MapName = LevelJsonRules::NormalizeMapName(World->GetMapName());
	const FString LevelJsonFile = FPaths::ProjectContentDir() / TEXT("Data/LevelJson") / (MapName + TEXT(".json"));
	FString LevelJsonText;
	TSharedPtr<FJsonObject> LevelJson;
	FString Relative;
	if (!FFileHelper::LoadFileToString(LevelJsonText, *LevelJsonFile)
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(LevelJsonText), LevelJson)
		|| !LevelJson.IsValid()
		|| !LevelJson->TryGetStringField(TEXT("levelgen_layout"), Relative)
		|| Relative.IsEmpty())
	{
		return nullptr;
	}
	const FString LayoutPath = FPaths::IsRelative(Relative) ? FPaths::ProjectDir() / Relative : Relative;
	FString LayoutText;
	if (!FFileHelper::LoadFileToString(LayoutText, *LayoutPath))
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("[PmcDefence] levelgen layout missing: %s"), *LayoutPath);
		return nullptr;
	}
	FPmcLayoutAccess Parsed;
	TArray<FString> Errors;
	if (!PmcDefenceRules::ParseLayout(LayoutText, Parsed, Errors))
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("[PmcDefence] levelgen layout is not a JSON object: %s"), *LayoutPath);
		return nullptr;
	}
	for (const FString& Error : Errors)
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("[PmcDefence] layout %s"), *Error);
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[PmcDefence] layout %s: %d buildings, %d ladders"), *LayoutPath, Parsed.Buildings.Num(), Parsed.Ladders.Num());
	DefenceLayoutCache = MoveTemp(Parsed);
	return &DefenceLayoutCache.GetValue();
}

FPmcDefenceSite UPmcSquadSubsystem::ResolveDefenceSite(const FVector& Objective, float LevelToleranceCm)
{
	FPmcDefenceSite Site;
	Site.LevelZ = Objective.Z;
	TArray<FPmcRoofRef> Roofs;
	TArray<FPmcLadderTop> LadderTops;
	if (const UTacticalMapSubsystem* Map = GetTacticalMap())
	{
		const FTacticalMapFile& File = Map->GetFile();
		Roofs.Reserve(File.OverwatchRoofs.Num());
		for (const FTacticalOverwatchRoof& Roof : File.OverwatchRoofs)
		{
			FPmcRoofRef Ref;
			Ref.Building = Roof.Building;
			Ref.Z = Roof.Z;
			Ref.AccessKind = PmcDefenceRules::ParseAccessKind(Roof.AccessKind);
			Ref.AccessIds = Roof.Access;
			Ref.Spots.Reserve(Roof.Spots.Num());
			for (const FTacticalOverwatchSpot& Spot : Roof.Spots)
			{
				FPmcDefenceSpot DefenceSpot;
				DefenceSpot.Location = Spot.Location;
				DefenceSpot.CoveredM = Spot.CoveredM;
				Ref.Spots.Add(DefenceSpot);
			}
			Roofs.Add(MoveTemp(Ref));
		}
		for (const FTacticalLadderRoof& LadderRoof : File.LadderOnlyRoofs)
		{
			const int32 Count = FMath::Min(LadderRoof.Ladders.Num(), LadderRoof.LadderTops.Num());
			for (int32 Index = 0; Index < Count; ++Index)
			{
				FPmcLadderTop Top;
				Top.Building = LadderRoof.Building;
				Top.LadderId = LadderRoof.Ladders[Index];
				Top.Location = LadderRoof.LadderTops[Index];
				LadderTops.Add(Top);
			}
		}
	}
	const int32 RoofIndex = PmcDefenceRules::PickRoof(Roofs, Objective, LevelToleranceCm);
	if (Roofs.IsValidIndex(RoofIndex))
	{
		const FPmcRoofRef& Roof = Roofs[RoofIndex];
		Site.Access = PmcDefenceRules::ResolveTacticalAccess(Roof, LadderTops, DefenceLayout());
		Site.Spots = Roof.Spots;
		Site.LevelZ = Roof.Z;
		Site.bRoof = true;
		if (Roof.Spots.Num() > 0)
		{
			Site.Inward = PmcDefenceRules::SpotCentroid(Roof.Spots);
			Site.bHasInward = true;
		}
		return Site;
	}
	if (const FPmcLayoutAccess* Layout = DefenceLayout())
	{
		const int32 BuildingIndex = PmcDefenceRules::BuildingAt(*Layout, Objective);
		if (PmcDefenceRules::IsWalkableRoofSite(*Layout, BuildingIndex, Objective.Z, LevelToleranceCm))
		{
			const FPmcLayoutBuilding& Building = Layout->Buildings[BuildingIndex];
			Site.Access = PmcDefenceRules::ResolveLayoutAccess(*Layout, BuildingIndex, Objective);
			Site.LevelZ = PmcDefenceRules::RoofZ(*Layout, Building);
			Site.Inward = FVector((Building.Min.X + Building.Max.X) * 0.5f, (Building.Min.Y + Building.Max.Y) * 0.5f, Site.LevelZ);
			Site.bHasInward = true;
		}
	}
	return Site;
}

bool UPmcSquadSubsystem::CopyDefenceSnapshot(int32 TeamId, FPmcDefenceSnapshot& Out) const
{
	Out = FPmcDefenceSnapshot();
	const FTeam* Team = FindTeam(TeamId);
	if (!Team || !Team->Defence.bActive)
	{
		return false;
	}
	Out.bActive = true;
	Out.Access = Team->Defence.Access;
	for (const TPair<TWeakObjectPtr<AOperativeCharacter>, FFighterState>& Pair : Team->Fighters)
	{
		const AOperativeCharacter* Mate = Pair.Key.Get();
		if (!PmcSquadLocal::IsLiveOperative(Mate))
		{
			continue;
		}
		Out.Feet.Add(PmcSquadLocal::FeetOf(*Mate));
		if (Mate->bInCover)
		{
			const FCoverSlot& Slot = Mate->GetCoverSlot();
			const FString WallId = Slot.WallActor.IsValid() ? Slot.WallActor->GetPathName() : FString();
			Out.CoverKeys.Add(PmcDefenceRules::CoverObjectKey(WallId, Slot.WallPoint));
		}
	}
	return true;
}

void UPmcSquadSubsystem::RefreshDefence(int32 TeamId, FTeam& Team, bool bResolveAccess)
{
	if (!Team.Objective.IsSet() || Team.Objective->Role != EPmcRole::Defender)
	{
		ClearDefencePosts(Team);
		Team.Defence.bActive = false;
		return;
	}
	const FPmcObjectiveView& Objective = Team.Objective.GetValue();
	FPmcDefenceConfig Config;
	Config.GuardDistCm = Team.Weights.GuardDistCm;
	Config.GuardInsetCm = Team.Weights.GuardInsetCm;
	Config.MinSpacingCm = Team.Weights.MinSpacingCm;
	Config.SpacingPenalty = Team.Weights.SpacingPenalty;
	Config.AreaFactor = Team.Weights.AreaFactor;
	Config.ShiftAfterSeconds = Team.Weights.ShiftAfterSeconds;
	Config.SectorArcDeg = Team.Weights.SectorArcDeg;
	Config.LevelToleranceCm = Objective.LevelToleranceCm;

	if (bResolveAccess)
	{
		const FPmcDefenceSite Site = ResolveDefenceSite(Objective.Location, Config.LevelToleranceCm);
		Team.Defence.Access = Site.Access;
		Team.Defence.Spots = Site.Spots;
		Team.Defence.LevelZ = Site.LevelZ;
		Team.Defence.bRoof = Site.bRoof;
		Team.Defence.Inward = Site.Inward;
		Team.Defence.bHasInward = Site.bHasInward;
		UE_LOG(LogCodexTactics, Display, TEXT("%s"), *PmcDefenceRules::AccessLogLine(TeamId, Team.Defence.Access));
		Team.Defence.bLogged = true;
	}

	Team.Defence.ApproachYawDeg = Team.Defence.bHasApproach
		? PmcDefenceRules::YawDegrees(Objective.Location, Team.Defence.ApproachPoint)
		: 0.f;
	Team.Defence.bActive = true;

	struct FKeptPost
	{
		EPmcDefenceRole Role = EPmcDefenceRole::None;
		int32 Spot = INDEX_NONE;
		FVector Post = FVector::ZeroVector;
		double ArrivedAt = -1.0;
	};
	TMap<int32, FKeptPost> Kept;
	const TArray<AOperativeCharacter*> Members = GetTeamMembers(TeamId);
	TArray<FPmcDefenceMan> Men;
	// The same helper and source as Decide's change test, so a refresh settles the comparison.
	TArray<int32> Living = PmcSquadLocal::LivingSquadIndices(Members);
	Men.Reserve(Members.Num());
	for (AOperativeCharacter* Member : Members)
	{
		if (!PmcSquadLocal::IsLiveOperative(Member))
		{
			continue;
		}
		FPmcDefenceMan Man;
		Man.Index = Member->SquadIndex;
		Man.Feet = PmcSquadLocal::FeetOf(*Member);
		if (const FFighterState* State = Team.Fighters.Find(Member))
		{
			if (!bResolveAccess)
			{
				Man.PreviousRole = State->DefenceRole;
			}
			FKeptPost& Previous = Kept.Add(Member->SquadIndex);
			Previous.Role = State->DefenceRole;
			Previous.Spot = State->DefenceSpotIndex;
			Previous.Post = State->DefencePost;
			Previous.ArrivedAt = State->DefenceArrivedAt;
		}
		Men.Add(Man);
	}

	const FVector* Inward = Team.Defence.bHasInward ? &Team.Defence.Inward : nullptr;
	const TArray<FPmcDefenceSlot> Slots;
	TArray<FPmcDefencePost> Posts = PmcDefenceRules::AssignRoles(Men, Team.Defence.Access, Objective.Location,
		Team.Defence.ApproachYawDeg, Team.Defence.Spots, Slots, Inward, Config);
	// T-05 phase b: firing positions on the kill zone for the hammers (a new objective = a new round: the cached lines are dropped first).
	TMap<int32, FFiringPosition> FiringPositionMap;
	TMap<int32, FFallBackPlanEntry> FallBackPlans;
	TSet<int32> ZoneMen;
	if (UFiringPositionSubsystem* FiringService = GetWorld() ? GetWorld()->GetSubsystem<UFiringPositionSubsystem>() : nullptr)
	{
		if (bResolveAccess)
		{
			FiringService->InvalidateCache();
		}
		AssignFiringPositions(TeamId, Team, Members, Posts, FiringPositionMap);
		// T-08 (defence_roles.enabled): the holder on a covered post inside the zone, the hammers' fall-back plans.
		if (GetRolesConfig().bEnabled && !Team.Defence.bRoof)
		{
			ApplyDefenceRoles(TeamId, Team, Members, Posts, FiringPositionMap, FallBackPlans, ZoneMen);
		}
	}

	ClearDefencePosts(Team);
	int32 SectorCount = 0;
	// The occupier: the sector shooter nearest the point keeps the time win possible; the guard only when he is alone.
	int32 Occupier = INDEX_NONE;
	bool bOccupierIsShooter = false;
	double OccupierDistSq = TNumericLimits<double>::Max();
	for (const FPmcDefencePost& Post : Posts)
	{
		const bool bShooter = Post.Role == EPmcDefenceRole::SectorShooter;
		const double DistSq = FVector::DistSquared(Post.Location, Objective.Location);
		if ((bShooter && !bOccupierIsShooter) || (bShooter == bOccupierIsShooter && DistSq < OccupierDistSq))
		{
			Occupier = Post.Index;
			bOccupierIsShooter = bShooter;
			OccupierDistSq = DistSq;
		}
	}
	Team.Defence.OccupierIndex = Occupier;
	for (const FPmcDefencePost& Post : Posts)
	{
		if (Post.Role == EPmcDefenceRole::SectorShooter)
		{
			++SectorCount;
		}
		AOperativeCharacter* Fighter = nullptr;
		for (AOperativeCharacter* Member : Members)
		{
			if (Member && Member->SquadIndex == Post.Index)
			{
				Fighter = Member;
				break;
			}
		}
		if (!Fighter)
		{
			continue;
		}
		FFighterState& State = Team.Fighters.FindOrAdd(Fighter);
		State.DefenceRole = Post.Role;
		State.DefenceSector = Post.Sector;
		State.DefenceYaw = Post.Yaw;
		State.DefenceSpotIndex = Post.SpotIndex;
		State.bHasDefencePost = Post.bHasPost;
		if (const FFiringPosition* Firing = FiringPositionMap.Find(Post.Index))
		{
			State.bFiringPosition = true;
			State.FiringStance = Firing->Stance;
			State.bFiringCover = Firing->bHasSlot;
			State.FiringSlot = Firing->Slot;
		}
		State.bZonePost = ZoneMen.Contains(Post.Index);
		if (const FFallBackPlanEntry* Plan = FallBackPlans.Find(Post.Index))
		{
			State.bFallBackPlan = true;
			State.FallBackPlanPost = Plan->Post;
			State.FallBackPlanPathCm = Plan->PathCm;
		}
		FVector Location = Post.Location;
		const bool bFallen = State.FallBackPhase != EPmcFallBackPhase::None;
		if (bFallen)
		{
			// T-08: a hammer that fell back keeps his zone post over a refresh (a death elsewhere does not walk him out of the zone).
			const FFiringPosition& Zone = State.FallBackPost.Position;
			State.bZonePost = true;
			State.bFallingBack = State.FallBackPhase == EPmcFallBackPhase::Moving;
			State.DefenceYaw = Zone.FacingYaw;
			State.DefenceSpotIndex = INDEX_NONE;
			State.DefenceSector = INDEX_NONE;
			State.bHasDefencePost = true;
			State.bFiringPosition = true;
			State.FiringStance = Zone.Stance;
			State.bFiringCover = Zone.bHasSlot;
			State.FiringSlot = Zone.Slot;
			Location = Zone.StandPoint;
		}
		if (Post.bHasPost || bFallen)
		{
			FVector OnNav;
			if (ProjectToNavAtLevel(Location, OnNav))
			{
				Location = OnNav;
			}
		}
		State.DefencePost = Location;
		const FKeptPost* Previous = Kept.Find(Post.Index);
		const bool bSamePost = !bResolveAccess && Previous != nullptr && Previous->Role == Post.Role && Previous->Spot == Post.SpotIndex
			&& FVector::DistSquared2D(Previous->Post, Location) <= 25.f;
		State.DefenceArrivedAt = bSamePost ? Previous->ArrivedAt : -1.0;
	}
	Team.Defence.LivingIndices = MoveTemp(Living);
	Team.Defence.SectorCount = SectorCount;
}
