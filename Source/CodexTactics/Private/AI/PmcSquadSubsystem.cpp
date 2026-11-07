// The hostile PMC squads: registry, team sight, targets and the utility-AI commander (user request 2026-11-03, docs/systems/pmc-squad-ai.md).
// UE-only, no Godot reference.

#include "AI/PmcSquadSubsystem.h"

#include "AI/PmcOperatorRules.h"
#include "AI/SquadOrderCatalog.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/PeekSightRules.h"
#include "Combat/SightRules.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Combat/WaveSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/CodexTacticsGameMode.h"
#include "Data/WeaponDataAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Materials/MaterialInterface.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Tactics/CoverTraceRules.h"

namespace PmcSquadLocal
{
	constexpr float CoverSearchRadiusCm = 1000.f;
	constexpr float ArriveCm = 250.f;
	constexpr float StuckSeconds = 4.f;
	constexpr float StuckProgressCm = 100.f;
	constexpr float AidReachCm = 230.f;
	constexpr float MonsterSightCm = 2500.f;
	/** A demasked shooter (his own fire) is noticed by the teams with a man this close (planar). */
	constexpr float GunfireHearingCm = 6000.f;

	inline bool IsLiveOperative(const AOperativeCharacter* Operative)
	{
		return IsValid(Operative) && !Operative->IsHidden() && !Operative->IsKilledInAction() && Operative->HealthComponent && Operative->HealthComponent->IsAlive();
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
	if (!Team.Posture.IsSet())
	{
		return;
	}
	UWorld* World = GetWorld();
	const TArray<AOperativeCharacter*> Mates = GetTeamMembers(TeamId);
	if (!World || Mates.IsEmpty())
	{
		return;
	}
	const EPmcPosture Posture = Team.Posture.GetValue();
	// Contact = seen an enemy (sight, touch, a shot heard within GunfireHearingCm - a hit's shooter is demasked) or seen by the squad.
	if (IsTeamEngaged(TeamId))
	{
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
			if (GetTeamBelief(TeamId, Player, Location, &Since) && Since < Freshest)
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
	for (TPair<int32, FTeam>& Pair : Teams)
	{
		UpdatePosture(Pair.Key, Pair.Value, DeltaTime);
		if (!Pair.Value.bCommanded)
		{
			continue;
		}
		UpdateTeamRunning(Pair.Key, Pair.Value, DeltaTime);
		Pair.Value.DecisionTimer -= DeltaTime;
		if (Pair.Value.DecisionTimer <= 0.f)
		{
			Pair.Value.DecisionTimer = Pair.Value.Weights.DecisionInterval;
			Decide(Pair.Key, Pair.Value);
		}
	}
}

void UPmcSquadSubsystem::RefreshSight()
{
	UpdateSight();
}

void UPmcSquadSubsystem::NotifyShotAt(const AActor* Shooter, const AOperativeCharacter* Victim)
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
			It.RemoveCurrent();
		}
	}
	Team->ShotRevealUntil.Add(const_cast<AActor*>(Shooter), Now + SightRules::DemaskSeconds);
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
			bool bSeen = !bRulesActive;
			// A shooter demasks himself - to the teams within earshot of the shot (2026-11-03: was the whole map, so one shot at a
			// monster told every PMC squad of the level where the player's men stood).
			if (!bSeen && Sight && Sight->IsDemasked(Target))
			{
				for (const AOperativeCharacter* Observer : Observers)
				{
					bSeen |= FVector::Dist2D(Observer->GetActorLocation(), Target->GetActorLocation()) <= PmcSquadLocal::GunfireHearingCm;
				}
			}
			// Bug #43: a shooter who fired at one of us is seen by his flash whatever the range (NotifyShotAt).
			if (const double* Until = !bSeen ? Team.ShotRevealUntil.Find(Target) : nullptr)
			{
				bSeen = *Until > Now;
			}
			for (AOperativeCharacter* Observer : Observers)
			{
				if (bSeen)
				{
					break;
				}
				// A monster coming at a mate is noticed all round (it is on him), a man only inside the eye rules.
				bSeen = SightRules::IsWithinTouchReveal(FVector::Dist2D(Observer->GetActorLocation(), Target->GetActorLocation()))
					|| (bAllRound ? Sight->HasClearSight(UTacticalSightSubsystem::EyePoint(*Observer), Profile) : Sight->CanOperativeSee(*Observer, Profile));
			}
			if (bSeen)
			{
				FKnownTarget& Known = Team.Known.FindOrAdd(Target);
				Known.Actor = Target;
				Known.LastKnown = Target->GetActorLocation();
				Known.LastSeenTime = Now;
				Known.bSeenNow = true;
				Team.LastContactTime = Now;
				Team.bEverContact = true;
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
	Team.Fighters.Reset();
	Team.Stats = FPmcTeamStats();
	Team.bEverContact = false;
	Team.LastContactTime = -1e9;
	Team.DecisionTimer = 0.f;
	Team.AssaultPoint.Reset();
	Team.Posture.Reset();
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
		return Fighter.HasOwnMoveOrder() || Fighter.HasPendingCover();
	case EPmcOrder::Suppress:
		return Fighter.HasSuppressOrder();
	case EPmcOrder::Heal:
		return State.Patient.IsValid() && PmcSquadLocal::IsLiveOperative(State.Patient.Get()) && PmcSquadLocal::HealthFractionOf(*State.Patient) < 1.f
			&& Fighter.MedkitsCount > 0;
	case EPmcOrder::Reload:
		return Fighter.bIsReloading;
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
		if (State.Order == EPmcOrder::Flank && !State.bFlankCounted && FVector::Dist2D(Location, State.Goal) <= PmcSquadLocal::ArriveCm)
		{
			State.bFlankCounted = true;
			++Team.Stats.FlanksSucceeded;
			UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: %s reached his flank point"), TeamId, *Fighter->DisplayName.ToString());
		}
		const bool bWalking = State.Order == EPmcOrder::Advance || State.Order == EPmcOrder::Flank || State.Order == EPmcOrder::FallBack
			|| State.Order == EPmcOrder::Relocate || State.Order == EPmcOrder::TakeCover;
		if (bWalking && Fighter->HasOwnMoveOrder())
		{
			if (FVector::Dist2D(Location, State.ProgressPoint) >= PmcSquadLocal::StuckProgressCm)
			{
				State.ProgressPoint = Location;
				State.ProgressTime = Now;
			}
			else if (Now - State.ProgressTime > PmcSquadLocal::StuckSeconds)
			{
				++Team.Stats.StuckEvents;
				UE_LOG(LogCodexTactics, Warning, TEXT("[PmcAI] team %d: %s stuck on %s - order dropped"), TeamId, *Fighter->DisplayName.ToString(),
					PmcCommanderRules::OrderName(State.Order));
				Fighter->StopOperative();
				State.Order = EPmcOrder::Hold;
				State.ProgressPoint = Location;
				State.ProgressTime = Now;
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
	const int32 ClipSize = Fighter.CurrentWeapon ? FMath::Max(1, Fighter.CurrentWeapon->MaxClipSize) : 30;
	const int32 Reserve = Fighter.CurrentWeapon ? FMath::Max(1, Fighter.CurrentWeapon->DefaultReserveAmmo) : 60;
	View.ClipFrac = Fighter.UsesAmmo() ? FMath::Clamp(static_cast<float>(Fighter.CurrentClip) / ClipSize, 0.f, 1.f) : 1.f;
	View.AmmoFrac = Fighter.UsesAmmo() ? FMath::Clamp(static_cast<float>(Fighter.CurrentClip + Fighter.ReserveAmmo) / (ClipSize + Reserve), 0.f, 1.f) : 1.f;
	View.bHasReserve = Fighter.ReserveAmmo > 0;
	View.bInCover = Fighter.bInCover;
	View.bExposed = State.ExposedSince >= 0.0;
	View.SecondsExposedHere = View.bExposed ? static_cast<float>(Now - State.ExposedSince) : 0.f;
	View.Stance = Fighter.GetStance();
	View.bMoving = Fighter.GetVelocity().SizeSquared2D() > 100.f || Fighter.HasOwnMoveOrder();
	View.bHasMedkit = Fighter.MedkitsCount > 0;
	View.bHasSidearm = PmcSquadLocal::HasWeaponId(Fighter, TEXT("pistol")) && Fighter.CurrentWeapon && Fighter.CurrentWeapon->WeaponId != TEXT("pistol");
	View.bKnockedDown = Fighter.IsKnockedDown();
	View.WeaponRangeCm = Fighter.CurrentWeapon ? Fighter.CurrentWeapon->AttackRangeCm : 1400.f;
	// 2026-11-04: the distance his weapon wants to fight from (riflemen keep their distance, the shotgun closes in).
	View.Band = PmcCommanderRules::ResolveBand(Fighter.CurrentWeapon ? Fighter.CurrentWeapon->WeaponId : FString(), View.WeaponRangeCm, Team.Weights);

	// The nearest known threat.
	OutThreatActor = nullptr;
	float BestDistance = TNumericLimits<float>::Max();
	for (const TPair<TWeakObjectPtr<AActor>, FKnownTarget>& Pair : Team.Known)
	{
		AActor* Actor = Pair.Value.Actor.Get();
		if (!Actor)
		{
			continue;
		}
		const float Distance = FVector::Dist2D(View.Location, Pair.Value.LastKnown);
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			OutThreatActor = Actor;
			View.bHasThreat = true;
			View.Threat.Location = Pair.Value.LastKnown;
			View.Threat.DistanceCm = Distance;
			View.Threat.SecondsSince = static_cast<float>(Now - Pair.Value.LastSeenTime);
			View.Threat.bSeenNow = Pair.Value.bSeenNow;
		}
	}
	// The assault objective stands in for a threat while none is known (the arena's attacking side walks up to the enemy).
	if (!View.bHasThreat && Team.AssaultPoint.IsSet())
	{
		View.bHasThreat = true;
		View.Threat.Location = Team.AssaultPoint.GetValue();
		View.Threat.DistanceCm = FVector::Dist2D(View.Location, View.Threat.Location);
		View.Threat.SecondsSince = 999.f;
		View.Threat.bSeenNow = false;
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
		View.bCoverAvailable = FindCoverNear(Fighter, View.Threat.Location, PmcSquadLocal::CoverSearchRadiusCm, Exclude, Team.Weights, OutCover, OutCoverQuality,
			View.Band, &View.CoverBandFactor) && OutCoverQuality > View.CoverQuality + 0.2f;
	}
	// The nearest wounded mate.
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
	// Nothing moves in a tactical pause / the turn-based grid fight: the commander waits like the player's autonomy does.
	if (const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		Flow && (Flow->GetCombatMode() == ECodexCombatMode::TacticalPause || Flow->GetCombatMode() == ECodexCombatMode::TurnBased))
	{
		return;
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
	for (AOperativeCharacter* Mate : Mates)
	{
		if (const FFighterState* State = Team.Fighters.Find(Mate); State && IsOrderRunning(*Mate, *State))
		{
			Squad.MatesSuppressing += State->Order == EPmcOrder::Suppress ? 1 : 0;
			Squad.MatesFlanking += State->Order == EPmcOrder::Flank ? 1 : 0;
			Squad.MatesAdvancing += State->Order == EPmcOrder::Advance ? 1 : 0;
			Squad.MatesHealing += State->Order == EPmcOrder::Heal ? 1 : 0;
		}
	}
	// The ambush springs: the first contact of a fight while men hold in ambush.
	if (Team.bEverContact)
	{
		for (AOperativeCharacter* Mate : Mates)
		{
			if (FFighterState* State = Team.Fighters.Find(Mate); State && State->Order == EPmcOrder::Ambush)
			{
				++Team.Stats.Ambushes;
				State->Order = EPmcOrder::Hold;
			}
		}
	}

	for (AOperativeCharacter* Fighter : Mates)
	{
		FFighterState& State = Team.Fighters.FindOrAdd(Fighter);
		FCoverSlot Cover;
		float CoverQuality = 0.f;
		AActor* ThreatActor = nullptr;
		FPmcFighterView View = BuildFighterView(TeamId, Team, *Fighter, State, Mates, Cover, CoverQuality, ThreatActor);
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
		}
		const TArray<FPmcOrderScore> Scored = PmcCommanderRules::ScoreAll(View, Own, Team.Weights);
		// A committed walk, or any order younger than CommitSeconds, is only broken by a clearly better one.
		const bool bCommitted = bRunning && (PmcCommanderRules::IsMoveOrder(State.Order) || Now - State.OrderSince < Team.Weights.CommitSeconds);
		const EPmcOrder Pick = PmcCommanderRules::PickWithMargin(Scored, State.Order, bRunning, bCommitted ? Team.Weights.MoveSwitchMargin : Team.Weights.SwitchMargin);
		if (Pick == State.Order && bRunning)
		{
			continue; // the running order keeps going
		}
		if (Pick == State.Order && (Pick == EPmcOrder::Hold || Pick == EPmcOrder::Ambush || Pick == EPmcOrder::Crouch))
		{
			continue; // nothing new to do
		}
		if (ExecuteOrder(TeamId, Team, *Fighter, State, Pick, View, ThreatActor, Cover, Mates))
		{
			++Team.Stats.OrdersIssued;
			OnFighterOrder.Broadcast(TeamId, Fighter, Pick, Scored[0].Score);
			UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: %s -> %s (score %.2f; threat %s at %.0f cm, hp %.0f%%, cover %.2f)"), TeamId,
				*Fighter->DisplayName.ToString(), PmcCommanderRules::OrderName(Pick), Scored[0].Score, View.bHasThreat ? (View.Threat.bSeenNow ? TEXT("seen") : TEXT("remembered")) : TEXT("none"),
				View.Threat.DistanceCm, View.HealthFrac * 100.f, View.CoverQuality);
			// Counting the orders the next decision reads as "running" relies on these mate counters being fresh.
			Squad.MatesSuppressing += Pick == EPmcOrder::Suppress ? 1 : 0;
			Squad.MatesFlanking += Pick == EPmcOrder::Flank ? 1 : 0;
			Squad.MatesAdvancing += Pick == EPmcOrder::Advance ? 1 : 0;
			Squad.MatesHealing += Pick == EPmcOrder::Heal ? 1 : 0;
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
	FVector Goal = FVector::ZeroVector;
	auto Walk = [&](const FVector& Point, bool bSprint)
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
		if (View.bHasThreat && FMath::Abs(Point.Z - View.Threat.Location.Z) > 250.f
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
		// 2026-11-04 ("fires from corners / over the cover, not blind"): holding in cover against a known threat, it becomes his priority -
		// the cover pose faces it and the corner / low-wall fire stance leans out for it (GatherCoverThreat), so he looks and fires from cover.
		if (Fighter.bInCover && ThreatActor && Fighter.GetManualPriorityTarget() != ThreatActor)
		{
			Context.Target = ThreatActor;
			SquadOrderCatalog::Execute(ESquadOrder::PriorityTarget, Context);
		}
		break;
	case EPmcOrder::TakeCover:
	case EPmcOrder::Relocate:
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
		FVector AdvanceGoal = PmcCommanderRules::AdvancePoint(View.Location, View.Threat.Location, View.WeaponRangeCm, Team.Weights, View.Band);
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
		bTaken = View.bHasThreat && Walk(PmcCommanderRules::FlankPoint(View.Threat.Location, Centre, View.Location, Team.Weights,
			PmcCommanderRules::FlankDistanceFor(View.Band, Team.Weights)), !View.Threat.bSeenNow);
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
			if (FindCoverNear(Fighter, View.Threat.Location, PmcSquadLocal::CoverSearchRadiusCm, nullptr, Team.Weights, BandCover, BandQuality, View.Band, &BandSlotFactor)
				&& BandSlotFactor >= 0.95f)
			{
				Context.Cover = BandCover;
				Context.bSprint = true;
				bTaken = SquadOrderCatalog::Execute(ESquadOrder::TakeCover, Context);
				Goal = BandCover.WorldLocation;
				State.Slot = BandCover;
			}
			if (!bTaken)
			{
				bTaken = Walk(PmcCommanderRules::StandoffPoint(View.Location, View.Threat.Location, View.Band), true);
			}
			Team.Stats.Standoffs += bTaken ? 1 : 0;
		}
		else
		{
			bTaken = View.bHasThreat && Walk(PmcCommanderRules::FallBackPoint(View.Location, View.Threat.Location), true);
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
		if (FVector::Dist2D(View.Location, Patient->GetActorLocation()) <= PmcSquadLocal::AidReachCm)
		{
			Context.Patient = Patient;
			bTaken = SquadOrderCatalog::Execute(ESquadOrder::Heal, Context);
			Team.Stats.Heals += bTaken ? 1 : 0;
		}
		else
		{
			bTaken = Walk(Patient->GetActorLocation(), true);
		}
		break;
	}
	case EPmcOrder::Ambush:
		Context.Stance = EOperativeStance::Crouching;
		bTaken = SquadOrderCatalog::Execute(ESquadOrder::Stance, Context);
		if (!Fighter.bInCover)
		{
			FCoverSlot Near;
			float Quality = 0.f;
			if (FindCoverNear(Fighter, View.Location + Fighter.GetActorForwardVector() * 1000.f, 600.f, nullptr, Team.Weights, Near, Quality))
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
	case EPmcOrder::Sidearm:
		Context.WeaponId = TEXT("pistol");
		bTaken = SquadOrderCatalog::Execute(ESquadOrder::SwitchWeapon, Context);
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
		State.Order = Order;
		State.OrderSince = Now;
		State.Goal = Goal;
		State.Target = ThreatActor;
		State.ProgressPoint = Fighter.GetActorLocation();
		State.ProgressTime = Now;
	}
	return bTaken;
}

bool UPmcSquadSubsystem::FindCoverNear(const AOperativeCharacter& Fighter, const FVector& Threat, float RadiusCm, const FVector* Exclude, const FPmcWeights& Weights,
	FCoverSlot& OutSlot, float& OutQuality, const FPmcEngagementBand& Band, float* OutBandFactor) const
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
	// #32 (2026-11-07): a corner is worth its bonus only when his peek round it reaches the threat (his sight range, the cover-and-peek rule).
	const float PeekRangeCm = Fighter.GetPeekSightRangeCm();
	struct FCandidate
	{
		FCoverSlot Slot;
		float Quality = 0.f;
		float Score = 0.f;
		bool bElevated = false;
		float BandFactor = 1.f;
	};
	TArray<FCandidate, TInlineAllocator<24>> Candidates;
	auto TryRay = [&](const FVector& Origin, const FVector& Direction, float Reach, bool bElevated)
	{
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, Origin, Origin + Direction * Reach, ECC_Visibility, Params) || !Hit.GetActor() || Cast<APawn>(Hit.GetActor()))
		{
			return;
		}
		FCoverSlot Slot;
		if (!CoverTraceRules::FindCoverSlotAt(World, Hit.ImpactPoint, Direction, Slot) || !Slot.IsValid())
		{
			return;
		}
		if (Exclude && FVector::Dist2D(Slot.WorldLocation, *Exclude) < 300.f)
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
		const float Score = Quality * (1.f - 0.5f * FMath::Clamp(Distance / RadiusCm, 0.f, 1.f)) * (Slot.Height == ECoverHeight::HighCover ? 1.f : 0.9f)
			* PmcCommanderRules::HighGroundFactor(Level, Weights) * FMath::Lerp(1.f, SlotBand, FMath::Clamp(Weights.BandPosition, 0.f, 1.f))
			* (bCorner ? 1.f + FMath::Max(Weights.CornerBonus, 0.f) : 1.f);
		Candidates.Add({ Slot, Quality, Score, bElevated, SlotBand });
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
	Candidates.StableSort([](const FCandidate& Left, const FCandidate& Right) { return Left.Score > Right.Score; });
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

void UPmcSquadSubsystem::HandleWeaponFired(AOperativeCharacter* Shooter, AActor* Target, bool bHit)
{
	const AOperativeCharacter* Victim = Cast<AOperativeCharacter>(Target);
	if (!bHit || !Shooter || !Victim || Victim == Shooter || Victim->TeamId != Shooter->TeamId)
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
	// Hostile fighters fire at anything they see.
	FSquadOrderContext Posture;
	Posture.Operative = Fighter;
	Posture.Posture = ESquadFirePosture::Aggressive;
	SquadOrderCatalog::Execute(ESquadOrder::FirePosture, Posture);
	RegisterOperative(Fighter); // BeginPlay did it already; harmless twice
	UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: spawned %s (look %s, %s) at %s"), TeamId, *Fighter->DisplayName.ToString(), *Look.Id, *WeaponId,
		*Location.ToCompactString());
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
