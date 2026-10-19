#include "Combat/TacticalSightSubsystem.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "Vehicles/VehicleSubsystem.h"
#include "Survival/TacticalHeatSubsystem.h"

#include "Characters/EnemyCharacter.h"
#include "Characters/MarksmanEnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/EnemyGhostActor.h"
#include "Combat/HealthComponent.h"
#include "Combat/PeekSightRules.h"
#include "Combat/SightRules.h"
#include "Components/SkinnedMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"

namespace TacticalSight
{
	static TAutoConsoleVariable<int32> CVarSight(TEXT("Codex.Sight"), 1,
		TEXT("Tactical line of sight (Sprint 08): 1 enemies out of sight are hidden / ghosts / enemies pursue last known spots; 0 everybody sees everything"));
	static TAutoConsoleVariable<int32> CVarSightExploration(TEXT("Codex.Sight.Exploration"), 1,
		TEXT("Symmetric sight while exploring (2026-10-19): 1 an enemy no operative sees from his stance's eye height is hidden / not targetable in exploration too; 0 everything is shown outside the fight (the old rule)"));

	FVector Feet(const AActor& Actor)
	{
		const ACharacter* Character = Cast<ACharacter>(&Actor);
		return Actor.GetActorLocation() - FVector(0.f, 0.f, Character ? Character->GetSimpleCollisionHalfHeight() : 0.f);
	}

	bool IsLiveOperative(const AOperativeCharacter* Operative)
	{
		return Operative && Operative->HealthComponent && Operative->HealthComponent->IsAlive();
	}

	bool IsLiveEnemy(const AEnemyCharacter* Enemy)
	{
		const UHealthComponent* Health = Enemy ? Enemy->GetHealthComponent() : nullptr;
		return Enemy && !Enemy->IsDying() && (!Health || Health->IsAlive());
	}
}

TStatId UTacticalSightSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTacticalSightSubsystem, STATGROUP_Tickables);
}

void UTacticalSightSubsystem::Deinitialize()
{
	RestoreAll();
	Super::Deinitialize();
}

bool UTacticalSightSubsystem::IsActive() const
{
	const UGameFlowSubsystem* Flow = GetWorld() ? GetWorld()->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	return Flow && TacticalSight::CVarSight.GetValueOnGameThread() != 0 && Flow->GetPhase() == ECodexGamePhase::WaveCombat
		&& Flow->GetCombatMode() != ECodexCombatMode::TurnBased;
}

bool UTacticalSightSubsystem::IsSquadSightActive() const
{
	const UGameFlowSubsystem* Flow = GetWorld() ? GetWorld()->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	return Flow && TacticalSight::CVarSight.GetValueOnGameThread() != 0
		&& SightRules::IsSquadSightPhase(Flow->GetPhase(), Flow->GetCombatMode(), TacticalSight::CVarSightExploration.GetValueOnGameThread() != 0);
}

void UTacticalSightSubsystem::Tick(float DeltaTime)
{
	if (!IsSquadSightActive())
	{
		if (bWasActive)
		{
			RestoreAll();
		}
		return;
	}
	bWasActive = true;
	Timer -= DeltaTime;
	if (Timer <= 0.f)
	{
		Timer = UpdateInterval;
		Update();
	}
	SummaryTimer += DeltaTime;
	if (SummaryTimer >= 10.f)
	{
		SummaryTimer = 0.f;
		UE_LOG(LogCodexTactics, Display, TEXT("[Sight] hidden %d, revealed %d, ghosts %d (heard %d), demasks %d, pack alerts %d, forgotten %d"),
			Stats.Hidden, Stats.Revealed, Stats.GhostsSpawned, Stats.HeardGhosts, Stats.Demasks, Stats.PackAlerts, Stats.Forgotten);
	}
}

void UTacticalSightSubsystem::Refresh()
{
	if (IsSquadSightActive())
	{
		bWasActive = true;
		Update();
	}
	else if (bWasActive)
	{
		RestoreAll();
	}
}

FVector UTacticalSightSubsystem::EyePoint(const AActor& Actor)
{
	if (const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(&Actor))
	{
		return TacticalSight::Feet(Actor) + FVector(0.f, 0.f, SightRules::EyeHeight(Operative->GetStance()));
	}
	if (const AMarksmanEnemyCharacter* Marksman = Cast<AMarksmanEnemyCharacter>(&Actor))
	{
		return TacticalSight::Feet(Actor) + FVector(0.f, 0.f, SightRules::EyeHeight(Marksman->GetStance()));
	}
	const ACharacter* Character = Cast<ACharacter>(&Actor);
	const float Height = Character ? Character->GetSimpleCollisionHalfHeight() * 2.f : 160.f;
	return TacticalSight::Feet(Actor) + FVector(0.f, 0.f, FMath::Min(160.f, Height * 0.9f));
}

FVector UTacticalSightSubsystem::ProfilePoint(const AActor& Actor)
{
	if (const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(&Actor))
	{
		// Peek sight (2026-10-19): a peeking operative shows his leaned-out head and shoulder - what he sees from there sees him.
		FVector Exposed;
		if (Operative->GetPeekExposedPoint(Exposed))
		{
			return Exposed;
		}
		return TacticalSight::Feet(Actor) + FVector(0.f, 0.f, SightRules::ProfileHeight(Operative->GetStance()));
	}
	if (const AMarksmanEnemyCharacter* Marksman = Cast<AMarksmanEnemyCharacter>(&Actor))
	{
		return TacticalSight::Feet(Actor) + FVector(0.f, 0.f, SightRules::ProfileHeight(Marksman->GetStance()));
	}
	const ACharacter* Character = Cast<ACharacter>(&Actor);
	const float Height = Character ? Character->GetSimpleCollisionHalfHeight() * 2.f : 150.f;
	return TacticalSight::Feet(Actor) + FVector(0.f, 0.f, FMath::Min(150.f, Height * 0.85f));
}

bool UTacticalSightSubsystem::HasClearSight(const FVector& From, const FVector& To) const
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TacticalSight), false);
	for (const TWeakObjectPtr<AActor>& Pawn : IgnoredPawns)
	{
		Params.AddIgnoredActor(Pawn.Get());
	}
	for (const TPair<TWeakObjectPtr<AEnemyCharacter>, FEnemyState>& Pair : EnemyStates)
	{
		Params.AddIgnoredActor(Pair.Value.Ghost.Get());
	}
	FHitResult Hit;
	return !GetWorld()->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Params) || Cast<APawn>(Hit.GetActor()) != nullptr;
}

bool UTacticalSightSubsystem::CanOperativeSee(const AOperativeCharacter& Operative, const FVector& TargetPoint) const
{
	// His own eyes (stance height over the capsule): what his position allows - hugging a wall, that wall hides what lies behind it.
	const FVector Eye = EyePoint(Operative);
	if (FVector::Dist(Eye, TargetPoint) <= MaxSightCm && HasClearSight(Eye, TargetPoint))
	{
		return true;
	}
	// Peek sight (game designer 2026-10-19): leaned out round a corner / over a low wall, the limited peek cone from that eye point.
	FVector PeekEye;
	FVector PeekDirection;
	return Operative.GetPeekSight(PeekEye, PeekDirection) && PeekSightRules::IsInCone(Operative.PeekSightConfig, PeekEye, PeekDirection, TargetPoint)
		&& HasClearSight(PeekEye, TargetPoint);
}

bool UTacticalSightSubsystem::IsDemasked(const AActor* Actor) const
{
	const double* Until = DemaskUntil.Find(Actor);
	return Until && *Until > GetWorld()->GetTimeSeconds();
}

void UTacticalSightSubsystem::NotifyFired(AActor* Shooter)
{
	if (!Shooter || !GetWorld())
	{
		return;
	}
	DemaskUntil.Add(Shooter, GetWorld()->GetTimeSeconds() + SightRules::DemaskSeconds);
	++Stats.Demasks;
	// Muzzle flash: a hidden enemy shows up at once (no wait for the next update).
	if (AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Shooter); Enemy && IsSquadSightActive())
	{
		if (FEnemyState* State = EnemyStates.Find(Enemy); State && State->bHidden)
		{
			SetEnemyHidden(*Enemy, false);
			State->bHidden = false;
			State->bEverSeen = true;
			DropGhost(*State);
			++Stats.Revealed;
		}
	}
}

bool UTacticalSightSubsystem::IsVisibleToSquad(const AActor* Enemy) const
{
	if (!bWasActive || !IsSquadSightActive())
	{
		return true;
	}
	const FEnemyState* State = EnemyStates.Find(Cast<AEnemyCharacter>(const_cast<AActor*>(Enemy)));
	return !State || !State->bHidden;
}

bool UTacticalSightSubsystem::GetBelief(const AEnemyCharacter* Enemy, const AActor* Operative, FVector& OutLocation, bool* bOutPerceived) const
{
	// APC P1: an operative inside a vehicle is nobody's target (the hull takes the attention in P4).
	if (!Operative || VehicleQueries::IsMounted(Operative))
	{
		return false;
	}
	// A horde (UHordeSubsystem) knows where the squad is: always «perceived». Exploring (2026-10-19: the squad's sight runs there too)
	// the enemies' side is the patrol perception: here they "know everything" as before.
	if (!bWasActive || !bBeliefWasActive || !IsActive() || (Enemy && Enemy->bKnowsSquadPosition))
	{
		OutLocation = Operative->GetActorLocation();
		if (bOutPerceived)
		{
			*bOutPerceived = true;
		}
		return true;
	}
	const TMap<TWeakObjectPtr<AOperativeCharacter>, FIntel>* Known = Intel.Find(const_cast<AEnemyCharacter*>(Enemy));
	const FIntel* Entry = Known ? Known->Find(Cast<AOperativeCharacter>(const_cast<AActor*>(Operative))) : nullptr;
	if (!Entry)
	{
		return false;
	}
	OutLocation = Entry->bPerceived ? Operative->GetActorLocation() : Entry->Location;
	if (bOutPerceived)
	{
		*bOutPerceived = Entry->bPerceived;
	}
	return true;
}

AEnemyGhostActor* UTacticalSightSubsystem::GetGhost(const AActor* Enemy) const
{
	const FEnemyState* State = EnemyStates.Find(Cast<AEnemyCharacter>(const_cast<AActor*>(Enemy)));
	return State ? State->Ghost.Get() : nullptr;
}

AEnemyGhostActor* UTacticalSightSubsystem::FindGhostNear(const FVector& Point, float RadiusCm) const
{
	AEnemyGhostActor* Best = nullptr;
	float BestDistance = RadiusCm;
	for (const TPair<TWeakObjectPtr<AEnemyCharacter>, FEnemyState>& Pair : EnemyStates)
	{
		AEnemyGhostActor* Ghost = Pair.Value.Ghost.Get();
		const float Distance = Ghost ? FVector::Dist2D(Ghost->GetLastKnownFeet(), Point) : TNumericLimits<float>::Max();
		if (Distance <= BestDistance)
		{
			BestDistance = Distance;
			Best = Ghost;
		}
	}
	return Best;
}

UMaterialInterface* UTacticalSightSubsystem::GetGhostMaterial()
{
	if (!GhostMaterial)
	{
		// The turn-based out-of-queue ghost shader (UTurnBasedCombatSubsystem::FreezeWorld).
		GhostMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/VFX/Materials/M_TacticalStasis.M_TacticalStasis"));
	}
	return GhostMaterial;
}

void UTacticalSightSubsystem::KeepPoseWhileHidden(USkinnedMeshComponent& Mesh, bool bHidden, FSavedAnimTickOptions& Saved)
{
	if (bHidden)
	{
		if (!Saved.Contains(&Mesh))
		{
			Saved.Add(&Mesh, Mesh.VisibilityBasedAnimTickOption);
		}
		Mesh.VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	}
	else if (const EVisibilityBasedAnimTickOption* Authored = Saved.Find(&Mesh))
	{
		Mesh.VisibilityBasedAnimTickOption = *Authored;
		Saved.Remove(&Mesh);
	}
}

void UTacticalSightSubsystem::SetEnemyHidden(AEnemyCharacter& Enemy, bool bHidden)
{
	TArray<AActor*> Hidden;
	Hidden.Add(&Enemy);
	Enemy.GetAttachedActors(Hidden, false, true);
	for (AActor* Actor : Hidden)
	{
		Actor->SetActorHiddenInGame(bHidden);
		TArray<USkinnedMeshComponent*> Meshes;
		Actor->GetComponents<USkinnedMeshComponent>(Meshes);
		for (USkinnedMeshComponent* Mesh : Meshes)
		{
			KeepPoseWhileHidden(*Mesh, bHidden, SavedAnimTickOptions);
		}
	}
	for (auto It = SavedAnimTickOptions.CreateIterator(); It; ++It)
	{
		if (!It->Key.IsValid())
		{
			It.RemoveCurrent(); // destroyed meshes
		}
	}
}

AEnemyGhostActor* UTacticalSightSubsystem::PlaceGhost(AEnemyCharacter& Enemy, FEnemyState& State)
{
	if (AEnemyGhostActor* Existing = State.Ghost.Get())
	{
		Existing->SnapTo(Enemy);
		return Existing;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AEnemyGhostActor* Ghost = GetWorld()->SpawnActor<AEnemyGhostActor>(Enemy.GetActorLocation(), Enemy.GetActorRotation(), Params);
	if (Ghost)
	{
		Ghost->InitFrom(Enemy, GetGhostMaterial());
		State.Ghost = Ghost;
		++Stats.GhostsSpawned;
	}
	return Ghost;
}

void UTacticalSightSubsystem::DropGhost(FEnemyState& State)
{
	if (AEnemyGhostActor* Ghost = State.Ghost.Get())
	{
		Ghost->Destroy();
	}
	State.Ghost.Reset();
}

void UTacticalSightSubsystem::RestoreAll()
{
	bWasActive = false;
	bBeliefWasActive = false;
	for (TPair<TWeakObjectPtr<AEnemyCharacter>, FEnemyState>& Pair : EnemyStates)
	{
		if (AEnemyCharacter* Enemy = Pair.Key.Get(); Enemy && Pair.Value.bHidden)
		{
			SetEnemyHidden(*Enemy, false);
		}
		DropGhost(Pair.Value);
	}
	EnemyStates.Reset();
	SavedAnimTickOptions.Reset();
	Intel.Reset();
	DemaskUntil.Reset();
}

void UTacticalSightSubsystem::SetCinematicReveal(bool bReveal)
{
	if (bCinematicReveal != bReveal)
	{
		bCinematicReveal = bReveal;
		Refresh();
	}
}

void UTacticalSightSubsystem::Update()
{
	UWorld* World = GetWorld();
	const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
	if (!Squad)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	// The fight rules (enemy knowledge) only in a fight; a new fight starts from fresh knowledge (seeded below).
	const bool bBelief = IsActive();
	if (bBelief != bBeliefWasActive)
	{
		Intel.Reset();
		bBeliefWasActive = bBelief;
	}
	TArray<AOperativeCharacter*> Operatives;
	for (AOperativeCharacter* Member : Squad->GetMembers())
	{
		if (TacticalSight::IsLiveOperative(Member))
		{
			Operatives.Add(Member);
		}
	}
	TArray<AEnemyCharacter*> Enemies;
	IgnoredPawns.Reset();
	for (AOperativeCharacter* Operative : Operatives)
	{
		IgnoredPawns.Add(Operative);
	}
	for (AEnemyCharacter* Enemy : UEnemyRegistrySubsystem::GetEnemyCharacters(World))
	{
		IgnoredPawns.Add(Enemy);
		if (TacticalSight::IsLiveEnemy(Enemy))
		{
			Enemies.Add(Enemy);
		}
	}

	// Gone / dying enemies: shown again (a blind-fire kill is seen), no silhouette.
	for (auto It = EnemyStates.CreateIterator(); It; ++It)
	{
		AEnemyCharacter* Enemy = It->Key.Get();
		if (!Enemy || !Enemies.Contains(Enemy))
		{
			if (Enemy && It->Value.bHidden)
			{
				SetEnemyHidden(*Enemy, false);
			}
			DropGhost(It->Value);
			Intel.Remove(It->Key);
			It.RemoveCurrent();
		}
	}

	// Every entry exists before the loop: the pack calls below write into other enemies' maps, and adding to the outer
	// maps there would move the entries the loop holds references to.
	for (AEnemyCharacter* Enemy : Enemies)
	{
		if (!EnemyStates.Contains(Enemy))
		{
			EnemyStates.Add(Enemy);
		}
		if (bBelief && !Intel.Contains(Enemy))
		{
			// The wave is sent at the squad: a new enemy (or every enemy when the fight starts) knows where everybody stood.
			TMap<TWeakObjectPtr<AOperativeCharacter>, FIntel>& Seeded = Intel.Add(Enemy);
			for (AOperativeCharacter* Operative : Operatives)
			{
				Seeded.Add(Operative, { Operative->GetActorLocation(), Now, -1.0, false });
			}
		}
	}

	for (AEnemyCharacter* Enemy : Enemies)
	{
		FEnemyState& State = EnemyStates.FindChecked(Enemy);

		// Squad -> enemy.
		const FVector Profile = ProfilePoint(*Enemy);
		// Sprint 15: a burning signal flare's magnesium light shows every enemy within its reveal radius.
		bool bSeen = bCinematicReveal || IsDemasked(Enemy) || UTacticalHeatSubsystem::IsRevealedByFlare(GetWorld(), Enemy->GetActorLocation());
		bool bHeard = false;
		for (AOperativeCharacter* Operative : Operatives)
		{
			const float Distance = FVector::Dist(Operative->GetActorLocation(), Enemy->GetActorLocation());
			const float Distance2D = static_cast<float>(FVector::Dist2D(Operative->GetActorLocation(), Enemy->GetActorLocation()));
			bHeard |= Distance2D <= SightRules::SquadHearingCm;
			// Bug hunt 2026-10-18: an enemy at arm's length is noticed without a line of sight (a frostbitten waiting 1.7 m off
			// right behind the wall's corner stayed hidden from the corner hold, so it was never fired at).
			bSeen |= SightRules::IsWithinTouchReveal(Distance2D);
			if (!bSeen && Distance <= MaxSightCm + 100.f && CanOperativeSee(*Operative, Profile))
			{
				bSeen = true;
			}
		}
		if (bSeen)
		{
			if (State.bHidden)
			{
				SetEnemyHidden(*Enemy, false);
				++Stats.Revealed;
			}
			State.bHidden = false;
			State.bEverSeen = true;
			State.LastKnownTime = Now;
			DropGhost(State);
		}
		else
		{
			if (!State.bHidden)
			{
				SetEnemyHidden(*Enemy, true);
				State.bHidden = true;
				++Stats.Hidden;
				// The last confirmed spot keeps a silhouette (never one for an enemy nobody has seen yet).
				if (State.bEverSeen)
				{
					PlaceGhost(*Enemy, State);
				}
			}
			if (bHeard)
			{
				// Snow crunch within 12 m: a faint silhouette at the sound's source.
				const bool bWasHeard = State.Ghost.IsValid() && State.Ghost->IsHeard();
				if (AEnemyGhostActor* Ghost = PlaceGhost(*Enemy, State))
				{
					Ghost->SetHeard(true);
					Stats.HeardGhosts += bWasHeard ? 0 : 1;
				}
				State.LastKnownTime = Now;
			}
			else if (AEnemyGhostActor* Ghost = State.Ghost.Get())
			{
				Ghost->SetHeard(false); // stays where it was last heard
				// Exploring (2026-10-19): the patrol walks on - the last-known silhouette fades like the minimap's last-known icon.
				if (SightRules::ShouldDropLastKnownGhost(bBelief, false, static_cast<float>(Now - State.LastKnownTime)))
				{
					DropGhost(State);
				}
			}
		}

		if (!bBelief)
		{
			continue; // exploring: the enemies perceive the squad through their patrol perception (PerceptionRules)
		}
		// Enemy -> squad (symmetric).
		TMap<TWeakObjectPtr<AOperativeCharacter>, FIntel>& Known = Intel.FindChecked(Enemy);
		const FVector Eye = EyePoint(*Enemy);
		for (AOperativeCharacter* Operative : Operatives)
		{
			const float Distance = FVector::Dist2D(Operative->GetActorLocation(), Enemy->GetActorLocation());
			// Sprint 12-E: high cover with the head down hides him from everything behind the wall (firing demasks).
			const bool bCoverHidden = Operative->IsHiddenInCoverFrom(Enemy->GetActorLocation());
			const bool bPerceives = IsDemasked(Operative) || Distance <= SightRules::EnemyHearingRadius(Operative->GetStance())
				|| (!bCoverHidden && Distance <= MaxSightCm && HasClearSight(Eye, ProfilePoint(*Operative)));
			if (bPerceives)
			{
				Known.Add(Operative, { Operative->GetActorLocation(), Now, -1.0, true });
				// Pack call: mates nearby learn where he is.
				for (AEnemyCharacter* Mate : Enemies)
				{
					if (Mate != Enemy && FVector::Dist2D(Mate->GetActorLocation(), Enemy->GetActorLocation()) <= SightRules::PackAlertCm)
					{
						FIntel& MateIntel = Intel.FindChecked(Mate).FindOrAdd(Operative);
						if (!MateIntel.bPerceived)
						{
							Stats.PackAlerts += MateIntel.Time < Now - 1.0 ? 1 : 0;
							MateIntel.Location = Operative->GetActorLocation();
							MateIntel.Time = Now;
							MateIntel.ArrivedTime = -1.0;
						}
					}
				}
			}
			else if (FIntel* Entry = Known.Find(Operative))
			{
				Entry->bPerceived = false;
				if (Entry->ArrivedTime < 0.0 && FVector::Dist2D(Enemy->GetActorLocation(), Entry->Location) <= SightRules::ForgetArrivalCm)
				{
					Entry->ArrivedTime = Now; // at the spot: it searches there a while
				}
				const float Searching = Entry->ArrivedTime >= 0.0 ? static_cast<float>(Now - Entry->ArrivedTime) : 0.f;
				if (SightRules::ShouldForget(static_cast<float>(Now - Entry->Time), Searching))
				{
					Known.Remove(Operative);
					++Stats.Forgotten;
				}
			}
		}
	}
}
