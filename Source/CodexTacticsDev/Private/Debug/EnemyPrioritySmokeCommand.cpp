// Dev-only console command for a headless check of "operatives first" on L_MovementTest (its own empty arena, SmokeUtils::FindClearArena):
//   Scripts/smoke_parallel.ps1 -Jobs 1 -Commands EnemyPrioritySmoke
// The game designer's rule 2026-10-25 (packaged-build play-test: the Demo 01 lake zombie went for a barricade instead of the operative
// it had engaged). Stages:
//   1. the barricade's vault nav area covers its whole 3 m box (+ a capsule radius): the enemies' path round it clears the solid ends
//      (it was the engine's 2 x 2 m failsafe square - the last 50 cm of each end were open navmesh, a zombie going round stuck there);
//   2. a lone Frostbitten (the Military Zombie's archetype) with the squad's turret 3 m off and a barricade between it and the
//      commander goes for an OPERATIVE and walks round the barricade to him - barricade and turret untouched;
//   3. saturation: with Codex.Enemy.OperativeAttackersBeforeObjects - 1 other enemies on one operative it still picks an operative,
//      with the cap reached on every operative an extra one goes for the turret (and hits it);
//   4. a saturated extra one right in front of the barricade smashes it (the Godot rule, allowed now);
//   5. a ring of barricades round the commander: he is unreachable on foot (the path check says so, a mate outside is reachable)
//      and a lone enemy smashes its way in.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/BarricadeActor.h"
#include "Interactables/InteractableActor.h"
#include "Interactables/TurretActor.h"
#include "Interactables/VaultNavigation.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "NavModifierComponent.h"
#include "TimerManager.h"

namespace EnemyPrioritySmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		FVector O = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		float GroundZ = 0.f;
		TWeakObjectPtr<AOperativeCharacter> Leader;
		TWeakObjectPtr<ABarricadeActor> Barricade;
		TWeakObjectPtr<ATurretActor> Turret;
		TWeakObjectPtr<AEnemyCharacter> Zombie;
		TArray<TWeakObjectPtr<AEnemyCharacter>> Parked;
		TArray<TWeakObjectPtr<ABarricadeActor>> Ring;
		float BarricadeHealth = 0.f;
		float TurretHealth = 0.f;
		bool bNonOperativeTarget = false;
		FString NonOperativeName;
		/** Console values the smoke changes, restored at the end. */
		TMap<FString, FString> SavedCVars;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	void SetCVar(FState& State, const TCHAR* Name, const TCHAR* Value)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			if (!State.SavedCVars.Contains(Name))
			{
				State.SavedCVars.Add(Name, Variable->GetString());
			}
			Variable->Set(Value, ECVF_SetByConsole);
		}
	}

	bool Finish(FState& State, bool bComplete)
	{
		for (const TPair<FString, FString>& Saved : State.SavedCVars)
		{
			if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(*Saved.Key))
			{
				Variable->Set(*Saved.Value, ECVF_SetByConsole);
			}
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("EnemyPrioritySmoke"));
		return false;
	}

	FVector At(const FState& State, float Along, float Side, float Up = 0.f)
	{
		return State.O + State.F * Along + State.R * Side + FVector(0.f, 0.f, Up);
	}

	ABarricadeActor* SpawnBarricade(UWorld* World, const FState& State, const FVector& Where, float Yaw)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<ABarricadeActor>(FVector(Where.X, Where.Y, State.GroundZ + ABarricadeActor::HeightCm * 0.5f), FRotator(0.f, Yaw, 0.f), Params);
	}

	AEnemyCharacter* SpawnZombie(UWorld* World, const FState& State, float Along, float Side, bool bFrozen)
	{
		const FVector Spot = At(State, Along, Side);
		AEnemyCharacter* Enemy = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::Frostbitten, FVector(Spot.X, Spot.Y, State.GroundZ + 100.f),
			(-State.F).Rotation());
		if (Enemy && bFrozen)
		{
			Enemy->CustomTimeDilation = 0.f;
			Enemy->SetActorTickEnabled(false); // CustomTimeDilation 0 alone does not stop TickBehavior (TRAPS)
		}
		if (UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>())
		{
			Sight->Refresh(); // a new enemy in a fight is seeded with where the squad stands
		}
		return Enemy;
	}

	void Unfreeze(AEnemyCharacter* Enemy)
	{
		if (Enemy)
		{
			Enemy->CustomTimeDilation = 1.f;
			Enemy->SetActorTickEnabled(true);
		}
	}

	/** A parked stand-in "already on" Target: alive, ticking off, far under the map, its current target forced. */
	void ParkAttacker(UWorld* World, FState& State, AOperativeCharacter* Target, int32 Index)
	{
		AEnemyCharacter* Enemy = SpawnZombie(World, State, 1200.f, -600.f + Index * 60.f, true);
		if (Enemy)
		{
			SmokeUtils::ParkEnemyFarAway(Enemy);
			Enemy->SetCurrentTargetForTesting(Target);
			State.Parked.Add(Enemy);
		}
	}

	void ClearParked(FState& State)
	{
		for (const TWeakObjectPtr<AEnemyCharacter>& Enemy : State.Parked)
		{
			if (Enemy.IsValid())
			{
				Enemy->Destroy();
			}
		}
		State.Parked.Reset();
	}

	float HealthOf(const AActor* Actor)
	{
		const UHealthComponent* Health = Actor ? Actor->FindComponentByClass<UHealthComponent>() : nullptr;
		return Health ? Health->GetCurrentHealth() : -1.f;
	}

	FString Describe(const AActor* Actor)
	{
		return Actor ? FString::Printf(TEXT("%s (%s)"), *Actor->GetName(), Actor->IsA<AOperativeCharacter>() ? TEXT("operative") : TEXT("object")) : TEXT("none");
	}

	void Next(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 150.f)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke stopped in stage %d (timeout)"), State.Stage);
			return World ? Finish(State, false) : false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = State.Leader.Get();
		const int32 Cap = AEnemyCharacter::GetOperativeAttackersBeforeObjects();
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 3.f || !Squad || !Squad->GetLeader())
			{
				return true;
			}
			Leader = Squad->GetLeader();
			State.Leader = Leader;
			SetCVar(State, TEXT("Codex.Horde.Enabled"), TEXT("0")); // L_MovementTest's sudden horde would join the fight
			Squad->SetFollowersHolding(true);
			TArray<const AActor*> Ignore;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Ignore.Add(Member);
				Member->HealthComponent->SetMaxHealth(100000.f);
				Member->bTacticalCeaseFire = true; // nobody shoots the test enemies
			}
			if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0))
			{
				Ignore.Add(Pawn);
			}
			// Small enemies go for a running generator first (0.4 x distance, unchanged): out of the picture.
			for (TActorIterator<AInteractableActor> It(World); It; ++It)
			{
				if (It->ObjectType == EInteractableType::Generator)
				{
					It->bGeneratorBroken = true;
				}
			}
			// Enemies of the level: parked, so they neither count as attackers nor join in.
			for (AEnemyCharacter* Enemy : UEnemyRegistrySubsystem::GetEnemyCharacters(World))
			{
				Enemy->SetActorTickEnabled(false);
				Enemy->SetCurrentTargetForTesting(nullptr);
				SmokeUtils::ParkEnemyFarAway(Enemy);
			}
			const FVector Feet = Leader->GetActorLocation() - FVector(0.f, 0.f, Leader->GetSimpleCollisionHalfHeight());
			const SmokeUtils::FClearArena Arena = SmokeUtils::FindClearArena(World, Feet, Leader->GetActorForwardVector(),
				FBox2D(FVector2D(-900.f, -900.f), FVector2D(1400.f, 900.f)), Ignore);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: arena %s at %s facing %s (%d tried), cap %d, %d operatives"), Arena.bFound ? TEXT("found") : TEXT("NOT found"),
				*Arena.Origin.ToCompactString(), *Arena.Forward.ToCompactString(), Arena.Tried, Cap, Squad->GetMembers().Num());
			if (!Arena.bFound || Cap <= 0)
			{
				Check(State, false, TEXT("an empty, flat, navigable 23 x 18 m spot and the rule switched on (cap > 0)"));
				return Finish(State, false);
			}
			State.O = Arena.Origin;
			State.F = Arena.Forward;
			State.R = Arena.Right;
			FHitResult Ground;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(EnemyPriorityGround), false);
			State.GroundZ = World->LineTraceSingleByChannel(Ground, State.O + FVector(0.f, 0.f, 200.f), State.O - FVector(0.f, 0.f, 400.f), ECC_Visibility, Params)
				? Ground.ImpactPoint.Z : State.O.Z;
			// The commander stands right behind the barricade (the tutorial: an operative in cover at it), the mates 6 m back.
			const float LeaderZ = State.GroundZ + Leader->GetSimpleCollisionHalfHeight() + 2.f;
			Leader->TeleportTo(FVector(State.O.X, State.O.Y, LeaderZ), State.F.Rotation(), false, true);
			int32 MateIndex = 0;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member != Leader)
				{
					const FVector Spot = At(State, -700.f, MateIndex++ % 2 == 0 ? 600.f : -600.f);
					Member->TeleportTo(FVector(Spot.X, Spot.Y, State.GroundZ + Member->GetSimpleCollisionHalfHeight() + 2.f), State.F.Rotation(), false, true);
				}
			}
			State.Barricade = SpawnBarricade(World, State, At(State, 100.f, 0.f), State.F.Rotation().Yaw + 90.f);
			State.BarricadeHealth = HealthOf(State.Barricade.Get());
			Check(State, State.Barricade.IsValid() && State.BarricadeHealth > 0.f, TEXT("a barricade 1 m in front of the commander (its 3 m across his front)"));
			Next(State, 1);
			return true;
		}
		case 1:
		{
			if (State.StageTime < 1.5f)
			{
				return true; // the navmesh takes the vault area in
			}
			ABarricadeActor* Barricade = State.Barricade.Get();
			const UNavModifierComponent* Area = VaultNavigation::FindVaultNavModifier(Barricade);
			const FVector Extent = Area ? Area->FailsafeExtent : FVector::ZeroVector;
			Check(State, Area && Extent.X >= 150.f + 30.f && Extent.Y >= 30.f + 30.f,
				FString::Printf(TEXT("the vault area covers the whole box + a capsule radius (extent %s; the old failsafe was 100 x 100)"), *Extent.ToCompactString()));
			// The enemies' path from the far side to the commander's side clears the solid box (no path through its ends).
			const UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, At(State, 750.f, 0.f, 50.f), At(State, -60.f, 0.f, 50.f),
				nullptr, UNavFilter_NoVault::StaticClass());
			float Clearance = TNumericLimits<float>::Max();
			if (Path && Path->IsValid() && Barricade)
			{
				const FTransform Box = Barricade->GetActorTransform();
				for (int32 Index = 1; Index < Path->PathPoints.Num(); ++Index)
				{
					const FVector A = Path->PathPoints[Index - 1];
					const FVector B = Path->PathPoints[Index];
					const int32 Samples = FMath::Max(1, FMath::CeilToInt(FVector::Dist2D(A, B) / 10.f));
					for (int32 Sample = 0; Sample <= Samples; ++Sample)
					{
						const FVector Local = Box.InverseTransformPositionNoScale(FMath::Lerp(A, B, Sample / static_cast<float>(Samples)));
						const float OutX = FMath::Max(0.f, FMath::Abs(static_cast<float>(Local.X)) - 150.f);
						const float OutY = FMath::Max(0.f, FMath::Abs(static_cast<float>(Local.Y)) - 30.f);
						Clearance = FMath::Min(Clearance, FMath::Sqrt(OutX * OutX + OutY * OutY));
					}
				}
			}
			Check(State, Path && Path->IsValid() && !Path->IsPartial() && Clearance >= 30.f,
				FString::Printf(TEXT("an enemy path round the barricade clears its box by >= 30 cm (%s, %d points, clearance %.0f cm)"),
					Path && Path->IsValid() ? (Path->IsPartial() ? TEXT("partial") : TEXT("full")) : TEXT("no path"), Path ? Path->PathPoints.Num() : 0, Clearance));

			// The squad's turret 3.6 m from the zombie, switched off (no fire: the test is the zombie's choice).
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			const FVector TurretSpot = At(State, 550.f, 300.f);
			ATurretActor* Turret = World->SpawnActor<ATurretActor>(FVector(TurretSpot.X, TurretSpot.Y, State.GroundZ + 50.f), State.F.Rotation(), Params);
			if (Turret)
			{
				Turret->SetPowered(false);
				Turret->SetActorTickEnabled(false);
			}
			State.Turret = Turret;
			State.TurretHealth = HealthOf(Turret);
			AEnemyCharacter* Zombie = SpawnZombie(World, State, 750.f, 0.f, false);
			State.Zombie = Zombie;
			if (!Zombie || !Turret)
			{
				Check(State, false, TEXT("turret and zombie spawned"));
				return Finish(State, false);
			}
			const AActor* Picked = Zombie->FindTarget();
			Check(State, Picked && Picked->IsA<AOperativeCharacter>() && !Zombie->IsObjectDistractionAllowed(),
				FString::Printf(TEXT("a lone zombie with the turret 3.6 m off (Godot: turret 1.8 m weighted vs operative 7.5 m) picks %s, objects allowed %d"),
					*Describe(Picked), Zombie->IsObjectDistractionAllowed() ? 1 : 0));
			Check(State, Zombie->IsTargetReachableOnFoot(Leader),
				TEXT("the commander right behind the barricade is reachable on foot (no false 'unreachable' at a cover spot)"));
			Next(State, 2);
			return true;
		}
		case 2:
		{
			AEnemyCharacter* Zombie = State.Zombie.Get();
			if (!Zombie)
			{
				Check(State, false, TEXT("the zombie is alive"));
				return Finish(State, false);
			}
			const AActor* Target = Zombie->GetCurrentTarget();
			if (Target && !Target->IsA<AOperativeCharacter>() && !State.bNonOperativeTarget)
			{
				State.bNonOperativeTarget = true;
				State.NonOperativeName = Target->GetName();
			}
			bool bBit = false;
			for (const AOperativeCharacter* Member : Squad->GetMembers())
			{
				bBit |= Member->HealthComponent->GetCurrentHealth() < Member->HealthComponent->GetMaxHealth() - 0.5f;
			}
			const bool bAtTarget = Target && Target->IsA<AOperativeCharacter>() && FVector::Dist2D(Zombie->GetActorLocation(), Target->GetActorLocation()) <= 200.f;
			const bool bTouched = HealthOf(State.Barricade.Get()) < State.BarricadeHealth || HealthOf(State.Turret.Get()) < State.TurretHealth;
			if (!bBit && !bAtTarget && !bTouched && State.StageTime < 25.f)
			{
				return true;
			}
			const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: zombie at %s after %.1f s, target %s, phase %d, %s"), *Zombie->GetActorLocation().ToCompactString(),
				State.StageTime, *Describe(Target), Flow ? static_cast<int32>(Flow->GetPhase()) : -1, *Zombie->GetDebugState());
			Check(State, !State.bNonOperativeTarget, FString::Printf(TEXT("the zombie never took an object as its target (%s)"),
				State.bNonOperativeTarget ? *State.NonOperativeName : TEXT("operatives only")));
			Check(State, HealthOf(State.Barricade.Get()) >= State.BarricadeHealth, FString::Printf(TEXT("the barricade untouched (%.0f / %.0f)"),
				HealthOf(State.Barricade.Get()), State.BarricadeHealth));
			Check(State, HealthOf(State.Turret.Get()) >= State.TurretHealth, FString::Printf(TEXT("the turret untouched (%.0f / %.0f)"),
				HealthOf(State.Turret.Get()), State.TurretHealth));
			Check(State, bBit || bAtTarget, FString::Printf(TEXT("it walked round the barricade to the operative in %.1f s (bit %d, within 2 m %d)"),
				State.StageTime, bBit ? 1 : 0, bAtTarget ? 1 : 0));
			Zombie->Destroy();

			// 3. Saturation: Cap others on every operative except the commander, who has one fewer.
			const TArray<AOperativeCharacter*> Members = Squad->GetMembers();
			int32 Index = 0;
			for (AOperativeCharacter* Member : Members)
			{
				const int32 Count = Member == Leader ? Cap - 1 : Cap;
				for (int32 Each = 0; Each < Count; ++Each)
				{
					ParkAttacker(World, State, Member, Index++);
				}
			}
			AEnemyCharacter* Extra = SpawnZombie(World, State, 750.f, 0.f, true);
			State.Zombie = Extra;
			if (!Extra)
			{
				Check(State, false, TEXT("the extra zombie spawned"));
				return Finish(State, false);
			}
			int32 Known = 0;
			const UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>();
			for (const AOperativeCharacter* Member : Members)
			{
				FVector Belief;
				Known += !Sight || Sight->GetBelief(Extra, Member, Belief) ? 1 : 0;
			}
			const AActor* Below = Extra->FindTarget();
			Check(State, Known == Members.Num() && Below && Below->IsA<AOperativeCharacter>() && !Extra->IsObjectDistractionAllowed(),
				FString::Printf(TEXT("%d others on the commander (cap %d), %d on each mate: the extra one still picks %s (knows %d / %d operatives, objects allowed %d)"),
					Cap - 1, Cap, Cap, *Describe(Below), Known, Members.Num(), Extra->IsObjectDistractionAllowed() ? 1 : 0));
			ParkAttacker(World, State, Leader, Index++);
			const AActor* Saturated = Extra->FindTarget();
			Check(State, Saturated == State.Turret.Get() && Extra->IsObjectDistractionAllowed(),
				FString::Printf(TEXT("cap reached on every operative: the extra one may go for the turret (picks %s, objects allowed %d)"),
					*Describe(Saturated), Extra->IsObjectDistractionAllowed() ? 1 : 0));
			Unfreeze(Extra);
			Next(State, 3);
			return true;
		}
		case 3:
		{
			if (HealthOf(State.Turret.Get()) >= State.TurretHealth && State.StageTime < 15.f)
			{
				const AEnemyCharacter* Extra = State.Zombie.Get();
				if (Extra && State.Turret.IsValid() && FMath::Fmod(State.StageTime, 1.f) < StepSeconds * 0.5f)
				{
					UE_LOG(LogCodexTactics, Display, TEXT("Smoke: extra at %s, %.0f cm from the turret (radius %.0f), target %s, %s"),
						*Extra->GetActorLocation().ToCompactString(), FVector::Dist2D(Extra->GetActorLocation(), State.Turret->GetActorLocation()),
						State.Turret->GetSimpleCollisionRadius(), *Describe(Extra->GetCurrentTarget()), *Extra->GetDebugState());
				}
				return true;
			}
			Check(State, HealthOf(State.Turret.Get()) < State.TurretHealth, FString::Printf(TEXT("the distracted extra one hits the turret (%.0f -> %.0f, %.1f s)"),
				State.TurretHealth, HealthOf(State.Turret.Get()), State.StageTime));
			if (State.Zombie.IsValid())
			{
				State.Zombie->Destroy();
			}
			if (State.Turret.IsValid())
			{
				State.Turret->Destroy();
			}
			// 4. Still saturated: one right in front of the barricade (1.6 m off its middle, the commander behind it) smashes it.
			State.BarricadeHealth = HealthOf(State.Barricade.Get());
			State.Zombie = SpawnZombie(World, State, 260.f, 0.f, false);
			Next(State, 4);
			return true;
		}
		case 4:
		{
			if (HealthOf(State.Barricade.Get()) >= State.BarricadeHealth && State.StageTime < 10.f)
			{
				return true;
			}
			Check(State, HealthOf(State.Barricade.Get()) < State.BarricadeHealth,
				FString::Printf(TEXT("operatives saturated: an extra one in front of the barricade smashes it (%.0f -> %.0f, %.1f s)"),
					State.BarricadeHealth, HealthOf(State.Barricade.Get()), State.StageTime));
			if (State.Zombie.IsValid())
			{
				State.Zombie->Destroy();
			}
			ClearParked(State);
			if (State.Barricade.IsValid())
			{
				State.Barricade->Destroy();
			}
			// 5. A closed ring of barricades round the commander (corners overlap: no gap for an enemy).
			const float Yaw = State.F.Rotation().Yaw;
			State.Ring.Add(SpawnBarricade(World, State, At(State, 150.f, 0.f), Yaw + 90.f));
			State.Ring.Add(SpawnBarricade(World, State, At(State, -150.f, 0.f), Yaw + 90.f));
			State.Ring.Add(SpawnBarricade(World, State, At(State, 0.f, 150.f), Yaw));
			State.Ring.Add(SpawnBarricade(World, State, At(State, 0.f, -150.f), Yaw));
			// Beeline (no pack tactics): the lone enemy goes for the nearest operative, the commander in the ring.
			SetCVar(State, TEXT("Codex.Enemy.Tactics"), TEXT("0"));
			Next(State, 5);
			return true;
		}
		case 5:
		{
			if (State.StageTime < 2.f)
			{
				return true; // the navmesh takes the ring in
			}
			AEnemyCharacter* Zombie = SpawnZombie(World, State, 750.f, 0.f, true);
			State.Zombie = Zombie;
			AOperativeCharacter* Mate = nullptr;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Mate = Member != Leader ? Member : Mate;
			}
			if (!Zombie)
			{
				Check(State, false, TEXT("the ring zombie spawned"));
				return Finish(State, false);
			}
			Check(State, !Zombie->IsTargetReachableOnFoot(Leader), TEXT("the commander inside the closed ring is unreachable on foot"));
			Check(State, !Mate || Zombie->IsTargetReachableOnFoot(Mate), FString::Printf(TEXT("a mate outside it is reachable (%s)"), *GetNameSafe(Mate)));
			State.BarricadeHealth = 0.f;
			for (const TWeakObjectPtr<ABarricadeActor>& Part : State.Ring)
			{
				State.BarricadeHealth += HealthOf(Part.Get());
			}
			Unfreeze(Zombie);
			Next(State, 6);
			return true;
		}
		case 6:
		{
			float RingHealth = 0.f;
			for (const TWeakObjectPtr<ABarricadeActor>& Part : State.Ring)
			{
				RingHealth += FMath::Max(0.f, HealthOf(Part.Get()));
			}
			if (RingHealth >= State.BarricadeHealth && State.StageTime < 20.f)
			{
				return true;
			}
			const AEnemyCharacter* Zombie = State.Zombie.Get();
			Check(State, RingHealth < State.BarricadeHealth, FString::Printf(TEXT("a lone enemy smashes its way into the ring to the unreachable commander (%.0f -> %.0f, %.1f s, target %s)"),
				State.BarricadeHealth, RingHealth, State.StageTime, Zombie ? *Describe(Zombie->GetCurrentTarget()) : TEXT("-")));
			return Finish(State, true);
		}
		default:
			return Finish(State, false);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		{
			FTimerHandle PlaceHandle;
			TWeakObjectPtr<UWorld> PlaceWorld(World);
			World->GetTimerManager().SetTimer(PlaceHandle, FTimerDelegate::CreateLambda([PlaceWorld]()
			{
				SmokeUtils::PlaceSquadAtTestStart(PlaceWorld.Get());
			}), 0.5f, false);
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.EnemyPrioritySmoke"),
		TEXT("Dev check: operatives first (2026-10-25) - a lone zombie goes round a barricade to the operative, ignores the turret; saturated extras may hit them; an unreachable operative is smashed through to; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
