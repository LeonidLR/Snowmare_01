// UTutorialSubsystem: dormant enemies (2026-10-23, game designer play-test of Demo 01 A4: "zombie #1 spots the squad while it runs to the
// cover"). An "enemies" binding with "dormant" (FTutorialDormant) keeps its enemies out of the game from the level start - parked far
// below the level, hidden, no collision, no tick (no AI, no senses), its movement off - until its "until" clauses hold; then they wake at
// the point of their own patrol route hidden from the whole squad (TutorialRules::ChooseHiddenRoutePoint) and patrol on from there, and
// "EnemyWoke.<binding>" then "EnemyWoke" are raised (subject = the enemy). Tutorial off / SKIP TUTORIAL: they wake where they were placed. Saved ("dormant_enemies").
// UE-only (the Godot archive had no tutorial). Doc: docs/systems/tutorial-engine.md section 22.

#include "Tutorial/TutorialSubsystem.h"
#include "AI/PatrolRouteActor.h"
#include "AIController.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Core/PlayerCommands.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Tutorial/TutorialRules.h"

namespace TutorialDormantLocal
{
	/** A sleeping enemy waits this far below where it was placed (out of every sight, hearing and blast range). */
	constexpr float ParkDepthCm = 50000.f;
	/** The patrol route is sampled every this many cm for the wake point. */
	constexpr float RouteSampleCm = 100.f;
	/** Sight from the operatives is tested at these heights over the route point (the zombie's chest, its knees). */
	constexpr float TargetHeightsCm[] = { 130.f, 50.f };
	/** Beyond this distance an operative does not see the point anyway. */
	constexpr float SightLimitCm = 6000.f;

	bool IsParked(const AEnemyCharacter& Enemy, const FVector4& Home)
	{
		return Enemy.GetActorLocation().Z < Home.Z - ParkDepthCm * 0.5f;
	}

	float EyeHeightCm(const AOperativeCharacter& Operative)
	{
		switch (Operative.GetStance())
		{
		case EOperativeStance::Prone: return 35.f;
		case EOperativeStance::Crouching: return 110.f;
		default: return 165.f;
		}
	}

	bool IsAliveEnemy(const AEnemyCharacter* Enemy)
	{
		const UHealthComponent* Health = Enemy ? Enemy->GetHealthComponent() : nullptr;
		return Enemy && !Enemy->IsDying() && (!Health || Health->IsAlive());
	}

	FVector GroundAt(const UWorld& World, const FVector& Point)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TutorialDormantGround), false);
		if (World.LineTraceSingleByObjectType(Hit, Point + FVector(0.f, 0.f, 300.f), Point - FVector(0.f, 0.f, 800.f), FCollisionObjectQueryParams(ECC_WorldStatic), Params))
		{
			return Hit.ImpactPoint;
		}
		return Point;
	}
}

bool UTutorialSubsystem::IsEnemyDormant(const AEnemyCharacter& Enemy) const
{
	for (const TPair<FName, FDormantBindingState>& Entry : DormantBindings)
	{
		if (!Entry.Value.bWoken && Entry.Value.Homes.Contains(Enemy.GetName()) && IsEnemyBound(Enemy, Entry.Key))
		{
			return true;
		}
	}
	return false;
}

bool UTutorialSubsystem::IsBindingAwake(FName BindingId) const
{
	const FDormantBindingState* State = DormantBindings.Find(BindingId);
	return !State || State->bWoken;
}

bool UTutorialSubsystem::GetWakeSpawn(FName BindingId, FVector& OutSpawn) const
{
	const FDormantBindingState* State = DormantBindings.Find(BindingId);
	if (!State || !State->bWoken || !State->bHasSpawn)
	{
		return false;
	}
	OutSpawn = State->Spawn;
	return true;
}

void UTutorialSubsystem::TickDormantEnemies()
{
	if (!Machine.IsLoaded())
	{
		return;
	}
	for (const FTutorialEnemyBinding& Binding : Machine.GetData().Enemies)
	{
		if (!Binding.Dormant.IsSet())
		{
			continue;
		}
		FDormantBindingState& State = DormantBindings.FindOrAdd(Binding.Id);
		if (State.bWoken)
		{
			continue;
		}
		if (!IsTutorialActive())
		{
			// Tutorial off / SKIP TUTORIAL: the enemy is simply there, as it was placed (no lesson waits for it).
			WakeDormantBinding(Binding.Id, /*bHiddenSpawn*/ false, TEXT("the tutorial is off"));
			continue;
		}
		for (AEnemyCharacter* Enemy : FindBoundEnemies(Binding.Id))
		{
			if (TutorialDormantLocal::IsAliveEnemy(Enemy))
			{
				PutEnemyToSleep(*Enemy, State); // every tick: a save's restore may have put it back into the level
			}
		}
		if (TutorialRules::IsConditionMet(Binding.Dormant.Until, [this](FName Stat) { return GetConditionStat(Stat); }))
		{
			WakeDormantBinding(Binding.Id, Binding.Dormant.SpawnsHidden(), TEXT("its condition holds"));
		}
	}
}

void UTutorialSubsystem::PutEnemyToSleep(AEnemyCharacter& Enemy, FDormantBindingState& State)
{
	using namespace TutorialDormantLocal;
	FVector4* Home = State.Homes.Find(Enemy.GetName());
	if (!Home)
	{
		const FVector At = Enemy.GetActorLocation();
		// A save restored it where it slept: its place is above (an older save without the homes).
		const FVector Placed = At.Z < -ParkDepthCm * 0.5f ? At + FVector(0.f, 0.f, ParkDepthCm) : At;
		Home = &State.Homes.Add(Enemy.GetName(), FVector4(Placed.X, Placed.Y, Placed.Z, Enemy.GetActorRotation().Yaw));
		UE_LOG(LogCodexTactics, Display, TEXT("Tutorial: %s is dormant (placed at %.0f, %.0f, %.0f) until its binding wakes"), *Enemy.GetName(), Placed.X, Placed.Y, Placed.Z);
	}
	if (IsParked(Enemy, *Home) && Enemy.IsHidden() && !Enemy.IsActorTickEnabled())
	{
		return;
	}
	if (AAIController* AI = Cast<AAIController>(Enemy.GetController()))
	{
		AI->StopMovement();
	}
	if (UCharacterMovementComponent* Movement = Enemy.GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}
	Enemy.SetActorHiddenInGame(true);
	Enemy.SetActorEnableCollision(false);
	Enemy.SetActorTickEnabled(false);
	Enemy.CustomTimeDilation = 0.f;
	Enemy.SetActorLocation(FVector(Home->X, Home->Y, Home->Z - ParkDepthCm), false, nullptr, ETeleportType::TeleportPhysics);
}

bool UTutorialSubsystem::FindHiddenRouteSpawn(const AEnemyCharacter& Enemy, FVector& OutSpawn, int32& OutWaypoint, bool& bOutHidden) const
{
	using namespace TutorialDormantLocal;
	const UWorld* World = GetWorld();
	const APatrolRouteActor* Route = Enemy.AssignedPatrolRoute.Get();
	const int32 Count = Route ? Route->GetNumberOfWaypoints() : 0;
	if (!World || Count < 2)
	{
		return false;
	}
	// The polyline the patrol walks (straight between its waypoints, closed while bIsLoop), sampled every metre.
	TArray<FVector> Points;
	TArray<float> Arcs;
	TArray<int32> NextWaypoint;
	float Arc = 0.f;
	const int32 Segments = Route->bIsLoop ? Count : Count - 1;
	for (int32 Segment = 0; Segment < Segments; ++Segment)
	{
		const FVector From = Route->GetWaypointWorldLocation(Segment);
		const FVector To = Route->GetWaypointWorldLocation((Segment + 1) % Count);
		const float Length = FVector::Dist2D(From, To);
		const int32 Steps = FMath::Max(1, FMath::CeilToInt(Length / RouteSampleCm));
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			const float T = static_cast<float>(Step) / Steps;
			Points.Add(GroundAt(*World, FMath::Lerp(From, To, T)));
			Arcs.Add(Arc + Length * T);
			NextWaypoint.Add((Segment + 1) % Count);
		}
		Arc += Length;
	}
	const float LoopLength = Route->bIsLoop ? Arc : 0.f;
	// The living squad: their eyes and where they stand along the route.
	TArray<FVector> Eyes;
	TArray<float> OperativeArcs;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TutorialDormantSight), false);
	Params.AddIgnoredActor(&Enemy);
	const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
	for (const AOperativeCharacter* Member : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
	{
		if (!Member)
		{
			continue;
		}
		Params.AddIgnoredActor(Member);
		const UHealthComponent* Health = Member->HealthComponent;
		if (Health && !Health->IsAlive())
		{
			continue;
		}
		const FVector Feet = Member->GetActorLocation() - FVector(0.f, 0.f, Member->GetSimpleCollisionHalfHeight());
		Eyes.Add(Feet + FVector(0.f, 0.f, EyeHeightCm(*Member)));
		int32 Nearest = 0;
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			if (FVector::DistSquared2D(Points[Index], Feet) < FVector::DistSquared2D(Points[Nearest], Feet))
			{
				Nearest = Index;
			}
		}
		OperativeArcs.Add(Arcs[Nearest]);
	}
	TArray<bool> Hidden;
	Hidden.Reserve(Points.Num());
	for (const FVector& Point : Points)
	{
		bool bSeen = false;
		for (const FVector& Eye : Eyes)
		{
			for (const float Height : TargetHeightsCm)
			{
				const FVector Target = Point + FVector(0.f, 0.f, Height);
				if (FVector::Dist(Eye, Target) <= SightLimitCm && !World->LineTraceTestByChannel(Eye, Target, ECC_Visibility, Params))
				{
					bSeen = true;
				}
			}
		}
		Hidden.Add(!bSeen);
	}
	const int32 Best = TutorialRules::ChooseHiddenRoutePoint(Arcs, Hidden, OperativeArcs, LoopLength);
	if (Best == INDEX_NONE)
	{
		return false;
	}
	OutSpawn = Points[Best];
	OutWaypoint = NextWaypoint[Best];
	bOutHidden = Hidden[Best];
	return true;
}

bool UTutorialSubsystem::WakeDormantBinding(FName BindingId, bool bHiddenSpawn, const TCHAR* Reason)
{
	FDormantBindingState* State = DormantBindings.Find(BindingId);
	if (!State || State->bWoken)
	{
		return false;
	}
	State->bWoken = true;
	for (AEnemyCharacter* Enemy : FindBoundEnemies(BindingId))
	{
		if (!TutorialDormantLocal::IsAliveEnemy(Enemy))
		{
			continue;
		}
		FVector Spawn = FVector::ZeroVector;
		int32 Waypoint = INDEX_NONE;
		bool bHidden = false;
		if (bHiddenSpawn && FindHiddenRouteSpawn(*Enemy, Spawn, Waypoint, bHidden))
		{
			State->bHasSpawn = true;
			State->Spawn = Spawn;
		}
		WakeEnemy(*Enemy, *State, bHiddenSpawn && State->bHasSpawn, Waypoint);
		UE_LOG(LogCodexTactics, Display, TEXT("Tutorial: %s wakes (%s) %s"), *Enemy->GetName(), Reason, bHiddenSpawn && State->bHasSpawn
			? *FString::Printf(TEXT("at (%.0f, %.0f, %.0f) on its route, %s, walking to waypoint %d"), Spawn.X, Spawn.Y, Spawn.Z,
				bHidden ? TEXT("hidden from the squad") : TEXT("NO point hidden from the squad - the farthest"), Waypoint)
			: TEXT("where it was placed"));
		GameplayEvents::Raise(this, FName(*(TEXT("EnemyWoke.") + BindingId.ToString())), Enemy); // the binding's own event (the A4 card)
		GameplayEvents::Raise(this, FName(TEXT("EnemyWoke")), Enemy);
	}
	WriteTelemetry(TEXT("enemy_woke"), BindingId, Machine.GetCurrentBeat(), 0.f);
	return true;
}

void UTutorialSubsystem::WakeEnemy(AEnemyCharacter& Enemy, const FDormantBindingState& State, bool bHiddenSpawn, int32 Waypoint)
{
	const FVector4* Home = State.Homes.Find(Enemy.GetName());
	const bool bSleeping = Home && TutorialDormantLocal::IsParked(Enemy, *Home);
	if (!bSleeping && !bHiddenSpawn)
	{
		return; // never put to sleep (woken before the first tick): nothing to undo
	}
	const float HalfHeight = Enemy.GetCapsuleComponent() ? Enemy.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
	const FVector Feet = bHiddenSpawn ? State.Spawn : (Home ? FVector(Home->X, Home->Y, Home->Z) - FVector(0.f, 0.f, HalfHeight) : Enemy.GetActorLocation());
	const FRotator Facing = bHiddenSpawn || !Home ? Enemy.GetActorRotation() : FRotator(0.f, Home->W, 0.f);
	Enemy.SetActorLocationAndRotation(Feet + FVector(0.f, 0.f, HalfHeight + 2.f), Facing, false, nullptr, ETeleportType::TeleportPhysics);
	Enemy.CustomTimeDilation = 1.f;
	Enemy.SetActorTickEnabled(true);
	Enemy.SetActorEnableCollision(true);
	Enemy.SetActorHiddenInGame(false);
	if (UCharacterMovementComponent* Movement = Enemy.GetCharacterMovement())
	{
		Movement->SetMovementMode(MOVE_Walking);
	}
	if (APatrolRouteActor* Route = Enemy.AssignedPatrolRoute.Get(); Route && bHiddenSpawn && Waypoint != INDEX_NONE)
	{
		// Patrol on from the wake point: to the waypoint ahead of it, the loop as before.
		FEnemyPatrolSnapshot Snapshot = Enemy.CapturePatrolSnapshot();
		Snapshot.bOnPatrol = true;
		Snapshot.WaypointIndex = Waypoint;
		Snapshot.bForward = true;
		Snapshot.Phase = 0;
		Snapshot.WaitLeft = 0.f;
		Snapshot.bSearching = false;
		Enemy.RestorePatrolSnapshot(Route, nullptr, Snapshot);
	}
}

void UTutorialSubsystem::SaveDormantEnemies(FJsonObject& Object) const
{
	const TSharedRef<FJsonObject> Bindings = MakeShared<FJsonObject>();
	for (const TPair<FName, FDormantBindingState>& Entry : DormantBindings)
	{
		const TSharedRef<FJsonObject> Saved = MakeShared<FJsonObject>();
		Saved->SetBoolField(TEXT("woken"), Entry.Value.bWoken);
		if (Entry.Value.bHasSpawn)
		{
			Saved->SetArrayField(TEXT("spawn"), { MakeShared<FJsonValueNumber>(Entry.Value.Spawn.X), MakeShared<FJsonValueNumber>(Entry.Value.Spawn.Y),
				MakeShared<FJsonValueNumber>(Entry.Value.Spawn.Z) });
		}
		const TSharedRef<FJsonObject> Homes = MakeShared<FJsonObject>();
		for (const TPair<FString, FVector4>& Home : Entry.Value.Homes)
		{
			Homes->SetArrayField(Home.Key, { MakeShared<FJsonValueNumber>(Home.Value.X), MakeShared<FJsonValueNumber>(Home.Value.Y),
				MakeShared<FJsonValueNumber>(Home.Value.Z), MakeShared<FJsonValueNumber>(Home.Value.W) });
		}
		Saved->SetObjectField(TEXT("homes"), Homes);
		Bindings->SetObjectField(Entry.Key.ToString(), Saved);
	}
	Object.SetObjectField(TEXT("dormant_enemies"), Bindings);
}

void UTutorialSubsystem::LoadDormantEnemies(const FJsonObject& Object)
{
	DormantBindings.Reset();
	const TSharedPtr<FJsonObject>* Bindings = nullptr;
	if (!Object.TryGetObjectField(TEXT("dormant_enemies"), Bindings))
	{
		return; // an older save: every dormant binding starts asleep again (its condition wakes it at once when it holds)
	}
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Bindings)->Values)
	{
		const TSharedPtr<FJsonObject> Saved = Entry.Value.IsValid() ? Entry.Value->AsObject() : nullptr;
		if (!Saved.IsValid())
		{
			continue;
		}
		FDormantBindingState& State = DormantBindings.Add(FName(*Entry.Key));
		Saved->TryGetBoolField(TEXT("woken"), State.bWoken);
		const TArray<TSharedPtr<FJsonValue>>* Spawn = nullptr;
		if (Saved->TryGetArrayField(TEXT("spawn"), Spawn) && Spawn->Num() == 3)
		{
			State.bHasSpawn = true;
			State.Spawn = FVector((*Spawn)[0]->AsNumber(), (*Spawn)[1]->AsNumber(), (*Spawn)[2]->AsNumber());
		}
		const TSharedPtr<FJsonObject>* Homes = nullptr;
		if (Saved->TryGetObjectField(TEXT("homes"), Homes))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Home : (*Homes)->Values)
			{
				const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
				if (Home.Value.IsValid() && Home.Value->TryGetArray(Values) && Values->Num() == 4)
				{
					State.Homes.Add(Home.Key, FVector4((*Values)[0]->AsNumber(), (*Values)[1]->AsNumber(), (*Values)[2]->AsNumber(), (*Values)[3]->AsNumber()));
				}
			}
		}
	}
}
