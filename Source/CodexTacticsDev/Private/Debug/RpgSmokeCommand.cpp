// Dev-only console command for a headless check of the RPG-7 on the shared projectile + explosion layer (2026-11-09; docs/systems/weapons.md
// "The RPG-7") on L_MovementTest:
//   Scripts/smoke_parallel.ps1 -Commands RpgSmoke -Jobs 2 -NoRetry -ExtraArgs "-Multiprocess"
// Builds a range 30 m above the leader (a floor slab, target dummies: boxes with a deep health pool), gives the leader the RPG-7 the way
// CodexTactics.GiveWeapon does, and checks:
//   1. a rocket at the dummy 30 m away strikes it armed - the direct share + the blast through Explosions::Detonate; the back-blast hurts the
//      mate 2.5 m behind him; the tube reloads one rocket (reserve - 1);
//   2. a rocket at the dummy 80 m away strikes it (the flight time = 80 m at the launch speed);
//   3. a wall 1 m right behind him: the shot is refused with a warning (no rocket, the tube still loaded);
//   4. a closed room (a roof + a wall 5 m behind): refused; 5. prone: refused; 6. a target closer than the arming distance: refused.
// Logs PASS/FAIL, then exits.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/LauncherRules.h"
#include "Combat/ProjectileSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Data/WeaponDataAsset.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

namespace RpgSmoke
{
	constexpr float WarmupSeconds = 3.f;
	constexpr float StepSeconds = 0.1f;
	constexpr float StageTimeout = 15.f;
	constexpr float RangeHeight = 3000.f;

	struct FState
	{
		int32 Stage = 0;
		float StageTime = 0.f;
		bool bAllOk = true;
		FVector O = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		TWeakObjectPtr<AOperativeCharacter> Hero;
		TWeakObjectPtr<AOperativeCharacter> Mate;
		TWeakObjectPtr<AActor> Near;
		TWeakObjectPtr<AActor> Far;
		TWeakObjectPtr<AActor> Close;
		TArray<TWeakObjectPtr<AActor>> Props;
		TArray<FProjectileImpact> Impacts;
		FDelegateHandle ImpactHandle;
		float TargetHealthBefore = 0.f;
		float MateHealthBefore = 0.f;
		int32 ReserveBefore = 0;
		int32 ShotsBefore = 0;
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
		State.Stage = -1;
		FPlatformMisc::RequestExit(false, TEXT("RpgSmoke"));
	}

	FVector At(const FState& State, float Along, float Side, float Up = 0.f)
	{
		return State.O + State.F * Along + State.R * Side + FVector(0.f, 0.f, Up);
	}

	AStaticMeshActor* SpawnBlock(UWorld* World, FState& State, const FVector& Centre, const FVector& SizeCm)
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
		State.Props.Add(Block);
		return Block;
	}

	/** A target dummy: a 1 x 1 x 1.8 m box with a deep health pool (no enemy: the squad's autonomy leaves it alone). */
	AActor* SpawnDummy(UWorld* World, FState& State, const FVector& Feet)
	{
		AStaticMeshActor* Dummy = SpawnBlock(World, State, Feet + FVector(0.f, 0.f, 90.f), FVector(100.f, 100.f, 180.f));
		if (!Dummy)
		{
			return nullptr;
		}
		UHealthComponent* Health = NewObject<UHealthComponent>(Dummy, TEXT("DummyHealth"));
		Dummy->AddInstanceComponent(Health);
		Health->RegisterComponent();
		Health->SetMaxHealth(100000.f, true);
		return Dummy;
	}

	float Health(const AActor* Actor)
	{
		const UHealthComponent* Component = Actor ? Actor->FindComponentByClass<UHealthComponent>() : nullptr;
		return Component ? Component->GetCurrentHealth() : 0.f;
	}

	void Enter(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
		State.Impacts.Reset();
	}

	void PlaceHero(FState& State)
	{
		if (AOperativeCharacter* Hero = State.Hero.Get())
		{
			Hero->TeleportTo(At(State, 0.f, 0.f, 100.f), State.F.Rotation(), false, true);
			Hero->StopOperative();
		}
	}

	/** One shot that must be refused for Expected; the tube stays loaded, no rocket leaves. */
	void ExpectRefusal(FState& State, AActor* Target, ELauncherRefusal Expected, const TCHAR* What)
	{
		AOperativeCharacter* Hero = State.Hero.Get();
		if (!Hero || !Target)
		{
			Check(State, false, FString::Printf(TEXT("%s: the hero and the target"), What));
			return;
		}
		Hero->CurrentClip = FMath::Max(Hero->CurrentClip, 1);
		const int32 ClipBefore = Hero->CurrentClip;
		const int32 ShotsBefore = Hero->GetLauncherShots();
		const bool bShot = Hero->ShootAtTarget(Target);
		Check(State, !bShot && Hero->GetLauncherShots() == ShotsBefore && Hero->CurrentClip == ClipBefore && Hero->GetLastLauncherRefusal() == Expected,
			FString::Printf(TEXT("%s: refused (shot %d, refusal %d, back-blast %d, clip %d -> %d) \"%s\""), What, bShot ? 1 : 0,
				static_cast<int32>(Hero->GetLastLauncherRefusal()), static_cast<int32>(Hero->GetLastLauncherBackblast()), ClipBefore, Hero->CurrentClip,
				*LauncherRules::RefusalText(Hero->GetLastLauncherRefusal(), Hero->GetLastLauncherBackblast())));
	}

	void DestroyProps(FState& State, int32 Keep)
	{
		while (State.Props.Num() > Keep)
		{
			if (AActor* Prop = State.Props.Pop().Get())
			{
				Prop->Destroy();
			}
		}
	}

	/** Returns false when finished. */
	bool Step(UWorld* World, FState& State)
	{
		State.StageTime += StepSeconds;
		AOperativeCharacter* Hero = State.Hero.Get();
		AOperativeCharacter* Mate = State.Mate.Get();
		if (!Hero || State.StageTime > StageTimeout)
		{
			Check(State, false, FString::Printf(TEXT("stage %d timed out (impacts %d)"), State.Stage, State.Impacts.Num()));
			Finish(World, State);
			return false;
		}
		switch (State.Stage)
		{
		case 0: // settle on the range
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			Check(State, Hero->IsLauncherEquipped() && Hero->CurrentClip == 1, FString::Printf(TEXT("0: the RPG-7 in hands (launcher %d, clip %d / %d)"),
				Hero->IsLauncherEquipped() ? 1 : 0, Hero->CurrentClip, Hero->ReserveAmmo));
			if (Mate)
			{
				Mate->TeleportTo(Hero->GetActorLocation() - State.F * 250.f, State.F.Rotation(), false, true);
				Mate->StopOperative();
				State.MateHealthBefore = Health(Mate);
			}
			State.TargetHealthBefore = Health(State.Near.Get());
			State.ReserveBefore = Hero->ReserveAmmo;
			State.ShotsBefore = Hero->GetLauncherShots();
			Hero->SetActorRotation(State.F.Rotation());
			Hero->bForceHitForTesting = true;
			{
				const bool bShot = Hero->ShootAtTarget(State.Near.Get());
				Check(State, bShot && Hero->GetLauncherShots() == State.ShotsBefore + 1, FString::Printf(TEXT("1: the rocket left the tube (refusal %d)"),
					static_cast<int32>(Hero->GetLastLauncherRefusal())));
				const float MateLost = State.MateHealthBefore - Health(Mate);
				Check(State, Mate && Hero->GetLastBackblastVictims() >= 1 && MateLost > 0.f, FString::Printf(
					TEXT("1: the back-blast hurt the mate 2.5 m behind (-%.1f HP, %d bodies, %.1f damage)"), MateLost, Hero->GetLastBackblastVictims(),
					Hero->GetLastBackblastDamage()));
			}
			Hero->bForceHitForTesting = false;
			if (Mate)
			{
				Mate->TeleportTo(At(State, -300.f, 900.f, 100.f), State.F.Rotation(), false, true); // out of the cone for the rest
			}
			Enter(State, 1);
			return true;

		case 1: // the rocket at 30 m in flight
			if (State.Impacts.Num() > 0)
			{
				const FProjectileImpact Impact = State.Impacts[0];
				AActor* Target = State.Near.Get();
				const float Lost = State.TargetHealthBefore - Health(Target);
				const bool bInBlast = Impact.Blast.Victims.ContainsByPredicate([Target](const FExplosionVictim& Victim) { return Victim.Actor.Get() == Target; });
				Check(State, Impact.End == EProjectileEnd::Impact && Target && Impact.HitActor.Get() == Target,
					FString::Printf(TEXT("1: it struck the 30 m dummy armed (end %d, hit %s)"), static_cast<int32>(Impact.End),
						Impact.HitActor.IsValid() ? *Impact.HitActor->GetName() : TEXT("nothing")));
				Check(State, Impact.bExploded && bInBlast && Impact.DirectDamage > 0.f && Lost > Impact.DirectDamage,
					FString::Printf(TEXT("1: the direct share %.0f + the shared blast (-%.1f HP, %d blast victims)"), Impact.DirectDamage, Lost,
						Impact.Blast.Victims.Num()));
				Enter(State, 2);
			}
			return true;

		case 2: // one rocket reloads
			if (Hero->bIsReloading || Hero->CurrentClip < 1)
			{
				return true;
			}
			Check(State, Hero->CurrentClip == 1 && Hero->ReserveAmmo == State.ReserveBefore - 1,
				FString::Printf(TEXT("2: one rocket reloaded after %.1f s (clip %d, reserve %d -> %d)"), State.StageTime, Hero->CurrentClip, State.ReserveBefore,
					Hero->ReserveAmmo));
			State.TargetHealthBefore = Health(State.Far.Get());
			Hero->bForceHitForTesting = true;
			Check(State, Hero->ShootAtTarget(State.Far.Get()), TEXT("2: the rocket at 80 m left the tube"));
			Hero->bForceHitForTesting = false;
			Enter(State, 3);
			return true;

		case 3: // the rocket at 80 m in flight
			if (State.Impacts.Num() > 0)
			{
				const FProjectileImpact Impact = State.Impacts[0];
				AActor* Target = State.Far.Get();
				const float Speed = Hero->CurrentWeapon ? Hero->CurrentWeapon->Stats.Projectile.SpeedCmS : 1.f;
				Check(State, Impact.End == EProjectileEnd::Impact && Target && Impact.HitActor.Get() == Target && Health(Target) < State.TargetHealthBefore,
					FString::Printf(TEXT("2: it struck the 80 m dummy armed (end %d, hit %s, -%.1f HP)"), static_cast<int32>(Impact.End),
						Impact.HitActor.IsValid() ? *Impact.HitActor->GetName() : TEXT("nothing"), State.TargetHealthBefore - Health(Target)));
				Check(State, FMath::IsNearlyEqual(Impact.FlightSeconds, Impact.FlightDistanceCm / Speed, 0.05f) && Impact.FlightDistanceCm > 7500.f,
					FString::Printf(TEXT("2: flight %.2f s over %.0f cm at %.0f m/s"), Impact.FlightSeconds, Impact.FlightDistanceCm, Speed / 100.f));
				Enter(State, 4);
			}
			return true;

		case 4: // the back-blast rules, the stance, the minimum range
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			const int32 KeepProps = State.Props.Num();
			// 3. A wall 1 m right behind him.
			SpawnBlock(World, State, Hero->GetActorLocation() - State.F * 100.f, FVector(30.f, 400.f, 300.f));
			ExpectRefusal(State, State.Near.Get(), ELauncherRefusal::Backblast, TEXT("3: a wall 1 m behind"));
			Check(State, Hero->GetLastLauncherBackblast() == ELauncherBackblast::WallBehind, TEXT("3: ... as a wall behind"));
			DestroyProps(State, KeepProps);
			// 4. A closed room: a roof over him and a wall 5 m behind.
			SpawnBlock(World, State, Hero->GetActorLocation() - State.F * 500.f, FVector(30.f, 600.f, 400.f));
			SpawnBlock(World, State, Hero->GetActorLocation() + FVector(0.f, 0.f, 250.f) - State.F * 250.f, FVector(600.f, 600.f, 20.f));
			ExpectRefusal(State, State.Near.Get(), ELauncherRefusal::Backblast, TEXT("4: a closed room"));
			Check(State, Hero->GetLastLauncherBackblast() == ELauncherBackblast::ClosedRoom, TEXT("4: ... as a closed room"));
			DestroyProps(State, KeepProps);
			// 6. Closer than the arming distance.
			ExpectRefusal(State, State.Close.Get(), ELauncherRefusal::TooClose, TEXT("6: a target 5 m away"));
			// 5. Prone.
			Hero->SetStance(EOperativeStance::Prone);
			Enter(State, 5);
			return true;
		}

		case 5:
			if (Hero->GetStance() != EOperativeStance::Prone && State.StageTime < 4.f)
			{
				return true;
			}
			ExpectRefusal(State, State.Near.Get(), ELauncherRefusal::Prone, TEXT("5: prone"));
			Finish(World, State);
			return false;

		default:
			return false;
		}
	}

	void Start(UWorld* World)
	{
		TSharedRef<FState> State = MakeShared<FState>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Hero = Squad ? Squad->GetLeader() : nullptr;
		UProjectileSubsystem* Projectiles = World->GetSubsystem<UProjectileSubsystem>();
		if (!Hero || !Projectiles)
		{
			Check(*State, false, TEXT("a leader and the projectile subsystem"));
			Finish(World, *State);
			return;
		}
		State->Hero = Hero;
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			if (Member && Member != Hero && !State->Mate.IsValid())
			{
				State->Mate = Member;
			}
		}
		State->F = FVector(Hero->GetActorForwardVector().X, Hero->GetActorForwardVector().Y, 0.f).GetSafeNormal();
		State->R = FVector::CrossProduct(FVector::UpVector, State->F);
		State->O = Hero->GetActorLocation() + FVector(0.f, 0.f, RangeHeight);
		SpawnBlock(World, *State, At(*State, 3500.f, 0.f, -10.f), FVector(12000.f, 4000.f, 20.f));
		State->Near = SpawnDummy(World, *State, At(*State, 3000.f, 0.f));
		State->Far = SpawnDummy(World, *State, At(*State, 8000.f, 600.f));
		State->Close = SpawnDummy(World, *State, At(*State, 400.f, -300.f));
		PlaceHero(*State);
		// The RPG-7 the way CodexTactics.GiveWeapon gives it.
		UWeaponDataAsset* Rpg = LoadObject<UWeaponDataAsset>(nullptr, TEXT("/Game/Data/Weapons/DA_Weapon_rpg7.DA_Weapon_rpg7"));
		Check(*State, Rpg && Rpg->Stats.Handling == EWeaponHandling::Launcher, TEXT("0: DA_Weapon_rpg7 tuned from weapons_tuning.json (handling launcher)"));
		if (Rpg)
		{
			Hero->AddArsenalWeapon(Rpg);
			Hero->SwitchToWeaponById(TEXT("rpg7"));
		}
		Hero->SetStance(EOperativeStance::Standing);
		TWeakPtr<FState> WeakState = State;
		State->ImpactHandle = Projectiles->OnProjectileFinished.AddLambda([WeakState](const FProjectileImpact& Impact)
		{
			if (const TSharedPtr<FState> Pinned = WeakState.Pin())
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke impact: end %d at %s, hit %s, %.2f s, %.0f cm, exploded %d, %d victims"), static_cast<int32>(Impact.End),
					*Impact.Location.ToCompactString(), Impact.HitActor.IsValid() ? *Impact.HitActor->GetName() : TEXT("-"), Impact.FlightSeconds,
					Impact.FlightDistanceCm, Impact.bExploded ? 1 : 0, Impact.Blast.Victims.Num());
				Pinned->Impacts.Add(Impact);
			}
		});
		Enter(*State, 0);
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
		TEXT("CodexTactics.RpgSmoke"),
		TEXT("Dev check: the RPG-7 (rockets at 30 / 80 m through the shared blast, the back-blast on a mate, a wall behind / a closed room / prone / too close refused); logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
