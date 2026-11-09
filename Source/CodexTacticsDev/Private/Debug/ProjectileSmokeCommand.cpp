// Dev-only console command for a headless check of the shared projectile + explosion layer (2026-11-09, docs/design/WEAPONS_FRAMEWORK.md 16)
// on L_MovementTest:
//   Scripts/smoke_parallel.ps1 -Commands ProjectileSmoke -Jobs 2 -NoRetry -ExtraArgs "-Multiprocess"
// Builds a test range 30 m above the leader (a floor slab, frozen frost hounds with a deep health pool) and checks:
//   1. a flat round (a rocket: 40 m/s, no gravity) fired at a hound 25 m away strikes it, armed: the direct hit is its whole damage (the
//      blast leaves the actor struck out; the designer 2026-11-09);
//   2. a high-angle round (25 m/s, gravity 1: the plunging arc) fired at a hound 40 m away comes down on it (within 1.5 m) and hurts it;
//      the pool hands the parked round of shot 1 back out;
//   3. the cover rule: a projectile blast 3.5 m from a hound behind a wall does x0.25 of the same blast in the open; a plunging blast ignores
//      the wall; a roof slab over the hound stops the plunging blast; the grenade's blast (the cover rule off: parity) ignores the wall.
// Logs PASS/FAIL, then exits.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/Explosion.h"
#include "Combat/HealthComponent.h"
#include "Combat/ProjectileActor.h"
#include "Combat/ProjectileSubsystem.h"
#include "Combat/WaveSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

namespace ProjectileSmoke
{
	constexpr float WarmupSeconds = 3.f;
	constexpr float StepSeconds = 0.1f;
	constexpr float PhaseTimeout = 12.f;
	/** The test range's height above the leader (clear of the map's geometry). */
	constexpr float RangeHeight = 3000.f;
	/** A hound's capsule centre above the floor. */
	constexpr float HoundUp = 60.f;

	struct FState
	{
		int32 Phase = 0;
		float PhaseTime = 0.f;
		bool bAllOk = true;
		FVector O = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		TWeakObjectPtr<AEnemyCharacter> FlatTarget;
		TWeakObjectPtr<AEnemyCharacter> ArcTarget;
		TWeakObjectPtr<AEnemyCharacter> Covered;
		TWeakObjectPtr<AEnemyCharacter> Open;
		TWeakObjectPtr<AStaticMeshActor> Roof;
		TArray<FProjectileImpact> Impacts;
		FDelegateHandle ImpactHandle;
		float FlatHealthBefore = 0.f;
		float ArcHealthBefore = 0.f;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok") : TEXT("FAILED"), *What);
		State.bAllOk &= bOk;
	}

	void Finish(UWorld* World, FState& State)
	{
		if (UProjectileSubsystem* Projectiles = World ? World->GetSubsystem<UProjectileSubsystem>() : nullptr)
		{
			Projectiles->OnProjectileFinished.Remove(State.ImpactHandle);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.bAllOk ? TEXT("PASS") : TEXT("FAIL"));
		State.Phase = -1;
		FPlatformMisc::RequestExit(false, TEXT("ProjectileSmoke"));
	}

	FVector At(const FState& State, float Along, float Side, float Up = 0.f)
	{
		return State.O + State.F * Along + State.R * Side + FVector(0.f, 0.f, Up);
	}

	AStaticMeshActor* SpawnBlock(UWorld* World, const FState& State, const FVector& Centre, const FVector& SizeCm)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (!Cube)
		{
			return nullptr;
		}
		const FTransform Transform(FRotator(0.f, State.F.Rotation().Yaw, 0.f), Centre, SizeCm / 100.f);
		AStaticMeshActor* Block = World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Block)
		{
			return nullptr;
		}
		Block->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Block->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Block->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
		Block->FinishSpawning(Transform);
		return Block;
	}

	AEnemyCharacter* SpawnHound(UWorld* World, const FState& State, const FVector& Where)
	{
		AEnemyCharacter* Hound = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound, Where, (-State.F).Rotation());
		if (Hound)
		{
			Hound->CustomTimeDilation = 0.f;
			Hound->SetActorTickEnabled(false);
			Hound->GetHealthComponent()->SetMaxHealth(100000.f, true);
			Hound->SetActorLocation(Where, false, nullptr, ETeleportType::TeleportPhysics);
		}
		return Hound;
	}

	float Health(const AEnemyCharacter* Hound)
	{
		return Hound && Hound->GetHealthComponent() ? Hound->GetHealthComponent()->GetCurrentHealth() : 0.f;
	}

	/** The test warhead: 5 m blast, full damage over the whole radius (the same at every distance), class Vehicle. */
	FProjectileLaunch TestLaunch(const FVector& From, EProjectileTrajectory Trajectory, float SpeedCmS)
	{
		FProjectileLaunch Launch;
		Launch.From = From;
		Launch.Projectile.SpeedCmS = SpeedCmS;
		Launch.Projectile.Trajectory = Trajectory;
		Launch.Projectile.GravityScale = Trajectory == EProjectileTrajectory::Flat ? 0.f : 1.f;
		Launch.Projectile.ArmingDistanceCm = 200.f;
		Launch.Projectile.MaxFlightSeconds = 10.f;
		Launch.Explosion.RadiusCm = 500.f;
		Launch.Explosion.FullDamageRadiusCm = 500.f;
		Launch.Explosion.EdgeDamageMultiplier = 1.f;
		Launch.DirectDamage = 30.f;
		Launch.BlastDamage = 50.f;
		Launch.SourceName = TEXT("ProjectileSmoke");
		return Launch;
	}

	/**
	 * One blast with the given spec; returns the damage the model dealt the hound (its victim entry, before the hound's own armour - a hurt
	 * hound's own modifiers change its HP loss from hit to hit), 0 when it was not reached. Logs the HP it lost too.
	 */
	float BlastLoss(UWorld* World, AEnemyCharacter* Hound, const FExplosionSpec& Spec)
	{
		const float Before = Health(Hound);
		const FExplosionResult Result = Explosions::Detonate(World, Spec);
		const FExplosionVictim* Victim = Result.Victims.FindByPredicate([Hound](const FExplosionVictim& Entry) { return Entry.Actor.Get() == Hound; });
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke blast %s plunging=%d: damage %.2f cover x%.2f, HP lost %.2f"), *Spec.Kind.ToString(), Spec.bPlunging ? 1 : 0,
			Victim ? Victim->Damage : 0.f, Victim ? Victim->Cover : 1.f, Before - Health(Hound));
		return Victim ? Victim->Damage : 0.f;
	}

	void Enter(FState& State, int32 Phase)
	{
		State.Phase = Phase;
		State.PhaseTime = 0.f;
		State.Impacts.Reset();
	}

	void CoverChecks(UWorld* World, FState& State)
	{
		AEnemyCharacter* Covered = State.Covered.Get();
		AEnemyCharacter* Open = State.Open.Get();
		if (!Covered || !Open)
		{
			Check(State, false, TEXT("3: the cover hounds"));
			return;
		}
		const FVector CoveredBlast = Covered->GetActorLocation() - State.F * 350.f;
		const FVector OpenBlast = Open->GetActorLocation() - State.F * 350.f;
		const FProjectileLaunch Rocket = TestLaunch(FVector::ZeroVector, EProjectileTrajectory::Flat, 4000.f);
		const FProjectileLaunch Mortar = TestLaunch(FVector::ZeroVector, EProjectileTrajectory::HighAngle, 2500.f);
		const float OpenLoss = BlastLoss(World, Open, AProjectileActor::BlastSpecFor(Rocket, OpenBlast, nullptr));
		const float WallLoss = BlastLoss(World, Covered, AProjectileActor::BlastSpecFor(Rocket, CoveredBlast, nullptr));
		Check(State, OpenLoss > 0.f, FString::Printf(TEXT("3: a projectile blast in the open hurts (%.2f)"), OpenLoss));
		Check(State, OpenLoss > 0.f && FMath::IsNearlyEqual(WallLoss / OpenLoss, 0.25f, 0.02f),
			FString::Printf(TEXT("3: behind a wall x0.25 (%.2f, x%.3f)"), WallLoss, OpenLoss > 0.f ? WallLoss / OpenLoss : 0.f));
		const float PlungeLoss = BlastLoss(World, Covered, AProjectileActor::BlastSpecFor(Mortar, CoveredBlast, nullptr));
		Check(State, FMath::IsNearlyEqual(PlungeLoss, OpenLoss, 0.05f),
			FString::Printf(TEXT("3: a plunging blast ignores the wall (%.2f vs the open %.2f)"), PlungeLoss, OpenLoss));
		const FExplosionSpec GrenadeOpen = Explosions::GrenadeSpec(OpenBlast, 85.f, 400.f, TEXT("ProjectileSmoke grenade"), nullptr);
		const FExplosionSpec GrenadeWall = Explosions::GrenadeSpec(CoveredBlast, 85.f, 400.f, TEXT("ProjectileSmoke grenade"), nullptr);
		const float GrenadeOpenLoss = BlastLoss(World, Open, GrenadeOpen);
		const float GrenadeWallLoss = BlastLoss(World, Covered, GrenadeWall);
		Check(State, GrenadeOpenLoss > 0.f && FMath::IsNearlyEqual(GrenadeWallLoss, GrenadeOpenLoss, 0.05f),
			FString::Printf(TEXT("3: the grenade's blast ignores the wall (cover rule off: parity) %.2f vs %.2f"), GrenadeWallLoss, GrenadeOpenLoss));
		State.Roof = SpawnBlock(World, State, Covered->GetActorLocation() + FVector(0.f, 0.f, 250.f), FVector(400.f, 400.f, 20.f));
		const float RoofLoss = BlastLoss(World, Covered, AProjectileActor::BlastSpecFor(Mortar, CoveredBlast, nullptr));
		Check(State, State.Roof.IsValid() && RoofLoss <= 0.01f, FString::Printf(TEXT("3: a roof stops the plunging blast (%.2f)"), RoofLoss));
		const float RoofRocketLoss = BlastLoss(World, Covered, AProjectileActor::BlastSpecFor(Rocket, CoveredBlast, nullptr));
		Check(State, FMath::IsNearlyEqual(RoofRocketLoss, WallLoss, 0.05f),
			FString::Printf(TEXT("3: a roof does not matter to a flat blast - the wall still does (%.2f)"), RoofRocketLoss));
	}

	/** Returns false when finished. */
	bool Step(UWorld* World, FState& State)
	{
		State.PhaseTime += StepSeconds;
		UProjectileSubsystem* Projectiles = World->GetSubsystem<UProjectileSubsystem>();
		if (!Projectiles || State.PhaseTime > PhaseTimeout)
		{
			Check(State, false, FString::Printf(TEXT("phase %d timed out (impacts %d)"), State.Phase, State.Impacts.Num()));
			Finish(World, State);
			return false;
		}
		switch (State.Phase)
		{
		case 1: // the flat round in flight
			if (State.Impacts.Num() > 0)
			{
				const FProjectileImpact Impact = State.Impacts[0];
				AEnemyCharacter* Target = State.FlatTarget.Get();
				const float Lost = State.FlatHealthBefore - Health(Target);
				Check(State, Impact.End == EProjectileEnd::Impact, FString::Printf(TEXT("1: the flat round struck armed (end %d)"), static_cast<int32>(Impact.End)));
				Check(State, Target && Impact.HitActor.Get() == Target, FString::Printf(TEXT("1: it struck the hound (%s)"),
					Impact.HitActor.IsValid() ? *Impact.HitActor->GetName() : TEXT("nothing")));
				const bool bInBlast = Impact.Blast.Victims.ContainsByPredicate([Target](const FExplosionVictim& Victim) { return Target && Victim.Actor.Get() == Target; });
				// The designer 2026-11-09: the actor struck takes the direct hit as its whole share - the blast leaves it out.
				Check(State, Impact.bExploded && Impact.DirectDamage == 30.f && !bInBlast && Lost > 0.f,
					FString::Printf(TEXT("1: the direct hit is its whole damage, the blast leaves it out (-%.1f HP, %d blast victims, the hound among them %d)"), Lost,
						Impact.Blast.Victims.Num(), bInBlast ? 1 : 0));
				Check(State, FMath::IsNearlyEqual(Impact.FlightSeconds, 2500.f / 4000.f, 0.1f),
					FString::Printf(TEXT("1: flight %.2f s / %.0f cm (25 m at 40 m/s)"), Impact.FlightSeconds, Impact.FlightDistanceCm));
				Check(State, Projectiles->GetActiveCount() == 0 && Projectiles->GetPooledCount() == 1, FString::Printf(
					TEXT("1: the round is parked in the pool (active %d, pooled %d)"), Projectiles->GetActiveCount(), Projectiles->GetPooledCount()));
				// 2. The high-angle round at the hound 40 m away.
				AEnemyCharacter* Arc = State.ArcTarget.Get();
				State.ArcHealthBefore = Health(Arc);
				Enter(State, 2);
				AProjectileActor* Round = nullptr;
				const bool bFired = Arc && Projectiles->FireAt(TestLaunch(At(State, 0.f, 0.f, 120.f), EProjectileTrajectory::HighAngle, 2500.f),
					Arc->GetActorLocation(), &Round);
				Check(State, bFired, TEXT("2: a high-angle launch solves for 40 m at 25 m/s"));
				Check(State, Round && Projectiles->GetPooledCount() == 0, TEXT("2: the pool handed the parked round back out"));
				if (Round)
				{
					const FVector V = Round->GetLaunch().Velocity;
					const float Elevation = FMath::RadiansToDegrees(FMath::Atan2(V.Z, FVector(V.X, V.Y, 0.f).Size()));
					Check(State, Elevation > 45.f, FString::Printf(TEXT("2: the plunging arc (%.1f deg)"), Elevation));
				}
				if (!bFired)
				{
					Finish(World, State);
					return false;
				}
			}
			return true;

		case 2: // the high-angle round in flight
			if (State.Impacts.Num() > 0)
			{
				const FProjectileImpact Impact = State.Impacts[0];
				AEnemyCharacter* Target = State.ArcTarget.Get();
				const float Miss = Target ? static_cast<float>(FVector::Dist2D(Impact.Location, Target->GetActorLocation())) : 1e6f;
				const float Lost = State.ArcHealthBefore - Health(Target);
				Check(State, Impact.End == EProjectileEnd::Impact, FString::Printf(TEXT("2: the high-angle round struck armed (end %d)"), static_cast<int32>(Impact.End)));
				Check(State, Miss <= 150.f, FString::Printf(TEXT("2: it came down on the hound (%.1f cm off, hit %s)"), Miss,
					Impact.HitActor.IsValid() ? *Impact.HitActor->GetName() : TEXT("nothing")));
				Check(State, Lost > 0.f, FString::Printf(TEXT("2: the hound was hurt (-%.1f HP)"), Lost));
				Check(State, Impact.FlightSeconds > 3.f, FString::Printf(TEXT("2: a long plunging flight (%.2f s, %.0f cm)"), Impact.FlightSeconds,
					Impact.FlightDistanceCm));
				CoverChecks(World, State);
				Finish(World, State);
				return false;
			}
			return true;

		default:
			return false;
		}
	}

	void Start(UWorld* World)
	{
		TSharedRef<FState> State = MakeShared<FState>();
		AOperativeCharacter* Leader = World->GetSubsystem<USquadSubsystem>() ? World->GetSubsystem<USquadSubsystem>()->GetLeader() : nullptr;
		UProjectileSubsystem* Projectiles = World->GetSubsystem<UProjectileSubsystem>();
		if (!Leader || !Projectiles)
		{
			Check(*State, false, TEXT("a leader and the projectile subsystem"));
			Finish(World, *State);
			return;
		}
		State->F = FVector(Leader->GetActorForwardVector().X, Leader->GetActorForwardVector().Y, 0.f).GetSafeNormal();
		State->R = FVector::CrossProduct(FVector::UpVector, State->F);
		State->O = Leader->GetActorLocation() + FVector(0.f, 0.f, RangeHeight);
		// The range: a 100 x 40 m floor slab (its top at O), a wall 1.75 m in front of the covered hound.
		SpawnBlock(World, *State, At(*State, 1500.f, 0.f, -10.f), FVector(10000.f, 4000.f, 20.f));
		State->FlatTarget = SpawnHound(World, *State, At(*State, 2500.f, 0.f, HoundUp));
		State->ArcTarget = SpawnHound(World, *State, At(*State, 4000.f, 800.f, HoundUp));
		State->Covered = SpawnHound(World, *State, At(*State, -2000.f, -600.f, HoundUp));
		State->Open = SpawnHound(World, *State, At(*State, -2000.f, 600.f, HoundUp));
		SpawnBlock(World, *State, At(*State, -2175.f, -600.f, 150.f), FVector(30.f, 400.f, 300.f));
		Check(*State, State->FlatTarget.IsValid() && State->ArcTarget.IsValid() && State->Covered.IsValid() && State->Open.IsValid(), TEXT("0: four test hounds"));
		TWeakPtr<FState> WeakState = State;
		State->ImpactHandle = Projectiles->OnProjectileFinished.AddLambda([WeakState](const FProjectileImpact& Impact)
		{
			if (const TSharedPtr<FState> Pinned = WeakState.Pin())
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke impact: end %d at %s, hit %s, %.2f s, exploded %d, %d victims"), static_cast<int32>(Impact.End),
					*Impact.Location.ToCompactString(), Impact.HitActor.IsValid() ? *Impact.HitActor->GetName() : TEXT("-"), Impact.FlightSeconds,
					Impact.bExploded ? 1 : 0, Impact.Blast.Victims.Num());
				Pinned->Impacts.Add(Impact);
			}
		});
		// 1. The flat round at the hound 25 m away.
		State->FlatHealthBefore = Health(State->FlatTarget.Get());
		Enter(*State, 1);
		const bool bFired = State->FlatTarget.IsValid()
			&& Projectiles->FireAt(TestLaunch(At(*State, 0.f, 0.f, HoundUp), EProjectileTrajectory::Flat, 4000.f), State->FlatTarget->GetActorLocation());
		Check(*State, bFired, TEXT("1: the flat round fired"));
		if (!bFired)
		{
			Finish(World, *State);
			return;
		}
		TSharedRef<FTimerHandle> Handle = MakeShared<FTimerHandle>();
		TWeakObjectPtr<UWorld> WeakWorld(World);
		World->GetTimerManager().SetTimer(*Handle, FTimerDelegate::CreateLambda([WeakWorld, State, Handle]()
		{
			UWorld* W = WeakWorld.Get();
			if (W && !Step(W, *State))
			{
				W->GetTimerManager().ClearTimer(*Handle);
			}
		}), StepSeconds, true);
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld]()
		{
			if (UWorld* W = WeakWorld.Get())
			{
				Start(W);
			}
		}), WarmupSeconds, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.ProjectileSmoke"),
		TEXT("Dev check: the shared projectile + explosion layer (flat + high-angle rounds strike their hounds, the blast's wall / roof cover rule, the grenade's parity); logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
