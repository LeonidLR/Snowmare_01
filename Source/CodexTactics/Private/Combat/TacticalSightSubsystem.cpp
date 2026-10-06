#include "Combat/TacticalSightSubsystem.h"

#include "Characters/EnemyCharacter.h"
#include "Characters/MarksmanEnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/EnemyGhostActor.h"
#include "Combat/HealthComponent.h"
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

void UTacticalSightSubsystem::Tick(float DeltaTime)
{
	if (!IsActive())
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
	if (IsActive())
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
	if (AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Shooter); Enemy && IsActive())
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
	if (!bWasActive || !IsActive())
	{
		return true;
	}
	const FEnemyState* State = EnemyStates.Find(Cast<AEnemyCharacter>(const_cast<AActor*>(Enemy)));
	return !State || !State->bHidden;
}

bool UTacticalSightSubsystem::GetBelief(const AEnemyCharacter* Enemy, const AActor* Operative, FVector& OutLocation, bool* bOutPerceived) const
{
	if (!Operative)
	{
		return false;
	}
	// A horde (UHordeSubsystem) knows where the squad is: always «perceived».
	if (!bWasActive || !IsActive() || (Enemy && Enemy->bKnowsSquadPosition))
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

void UTacticalSightSubsystem::Update()
{
	UWorld* World = GetWorld();
	const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
	if (!Squad)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
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
	for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
	{
		IgnoredPawns.Add(*It);
		if (TacticalSight::IsLiveEnemy(*It))
		{
			Enemies.Add(*It);
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
			// The wave is sent at the squad: a new enemy knows where everybody stood.
			TMap<TWeakObjectPtr<AOperativeCharacter>, FIntel>& Seeded = Intel.FindOrAdd(Enemy);
			for (AOperativeCharacter* Operative : Operatives)
			{
				Seeded.Add(Operative, { Operative->GetActorLocation(), Now, -1.0, false });
			}
		}
		Intel.FindOrAdd(Enemy);
	}

	for (AEnemyCharacter* Enemy : Enemies)
	{
		FEnemyState& State = EnemyStates.FindChecked(Enemy);
		TMap<TWeakObjectPtr<AOperativeCharacter>, FIntel>& Known = Intel.FindChecked(Enemy);

		// Squad -> enemy.
		const FVector Profile = ProfilePoint(*Enemy);
		bool bSeen = IsDemasked(Enemy);
		bool bHeard = false;
		for (AOperativeCharacter* Operative : Operatives)
		{
			const float Distance = FVector::Dist(Operative->GetActorLocation(), Enemy->GetActorLocation());
			bHeard |= FVector::Dist2D(Operative->GetActorLocation(), Enemy->GetActorLocation()) <= SightRules::SquadHearingCm;
			if (!bSeen && Distance <= MaxSightCm && HasClearSight(EyePoint(*Operative), Profile))
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
			}
			else if (AEnemyGhostActor* Ghost = State.Ghost.Get())
			{
				Ghost->SetHeard(false); // stays where it was last heard
			}
		}

		// Enemy -> squad (symmetric).
		const FVector Eye = EyePoint(*Enemy);
		for (AOperativeCharacter* Operative : Operatives)
		{
			const float Distance = FVector::Dist2D(Operative->GetActorLocation(), Enemy->GetActorLocation());
			const bool bPerceives = IsDemasked(Operative) || Distance <= SightRules::EnemyHearingRadius(Operative->GetStance())
				|| (Distance <= MaxSightCm && HasClearSight(Eye, ProfilePoint(*Operative)));
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
