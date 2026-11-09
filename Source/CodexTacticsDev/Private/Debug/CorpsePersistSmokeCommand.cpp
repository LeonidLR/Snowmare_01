// Dev-only headless check of "corpses stay" (game designer 2026-11-08; UCorpseSubsystem, AEnemyCharacter::FreezeCorpse) on L_Gen_Depot_1:
//   Scripts/smoke_parallel.ps1 -Commands CorpsePersistSmoke   (map /Game/Maps/Generated/L_Gen_Depot_1, Scripts/test_map.json)
// 11 enemies of every kind (hounds, frostbitten, Military Zombies = the variant Blueprint, a cutter, a brute, a marksman, a spitter, one
// frostbitten killed 4 m up in the air) and a hostile PMC fighter die 8-25 m from the squad. 30 s later every body is still there, on the
// ground, FROZEN: no AI controller, no actor / movement / mesh tick (anims paused on the death pose), no collision, out of the enemy
// registry, still visible. 2026-11-09 (game designer: "every enemy body stays, ALL enemies, one cap"): the Spitter (no Blueprint yet, the
// C++ placeholder body, no death pose) stays too, LAID on the ground (its cylinder on its side, resting on the floor), and the hostile PMC
// body is under the same cap and frozen (it used to stay unmanaged and ticking).
// Then the cap: Codex.Corpse.Cap 4 -> 8 bodies fade out (scaled down over ~1 s) and the 4 NEAREST the squad stay, whatever they are
// (OldestPool 10 = all).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/CorpseSubsystem.h"
#include "Characters/EnemyAnimInstance.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/CombatPressureRules.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "AIController.h"
#include "AI/PmcSquadSubsystem.h"
#include "Characters/OperativeAnimInstance.h"

namespace CorpsePersistSmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FBody
	{
		TWeakObjectPtr<AEnemyCharacter> Enemy;
		FString Label;
		bool bAirborne = false;
		float DistanceCm = 0.f;
		/** No Blueprint / death pose (the Spitter's placeholder body): kept too since 2026-11-09, laid on the ground. */
		bool bPlaceholder = false;
		/** A hostile PMC fighter (an operative body) instead of a monster. */
		TWeakObjectPtr<AOperativeCharacter> Fighter;

		AActor* Actor() const { return Enemy.IsValid() ? static_cast<AActor*>(Enemy.Get()) : static_cast<AActor*>(Fighter.Get()); }
	};

	struct FState
	{
		int32 Stage = 0;
		float StageTime = 0.f;
		float Total = 0.f;
		int32 Failures = 0;
		TArray<FBody> Bodies;
		bool bShrinkSeen = false;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	void SetCVar(const TCHAR* Name, float Value)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Variable->Set(Value, ECVF_SetByConsole);
		}
	}

	bool Finish(const FState& State, bool bComplete)
	{
		SetCVar(TEXT("Codex.Corpse.Cap"), -1.f);
		SetCVar(TEXT("Codex.Pressure.Enabled"), -1.f);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("CorpsePersistSmoke"));
		return false;
	}

	void Next(FState& State)
	{
		++State.Stage;
		State.StageTime = 0.f;
	}

	void Kill(AEnemyCharacter* Enemy)
	{
		if (Enemy && Enemy->GetHealthComponent())
		{
			FDamageSpec Spec;
			Spec.Amount = 1000000.f;
			Spec.ArmorPenetration = 1.f;
			Enemy->GetHealthComponent()->TakeDamage(Spec);
		}
	}

	/** Height of Point over the ground right under it (the body ignored); 1e6 without ground. */
	float PointOverGround(UWorld* World, const AActor& Body, const FVector& Root)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CorpseSmokeGround), false, &Body);
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, Root + FVector(0.f, 0.f, 150.f), Root - FVector(0.f, 0.f, 1000.f), ECC_Visibility, Params))
		{
			return 1.e6f;
		}
		return static_cast<float>(Root.Z - Hit.ImpactPoint.Z);
	}

	/** Height of the body's mesh root over the ground right under it (the body, pawns ignored); 1e6 without ground. */
	float RootOverGround(UWorld* World, const ACharacter& Body)
	{
		return PointOverGround(World, Body, Body.GetMesh()->GetComponentLocation());
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.StageTime += StepSeconds;
		State.Total += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Total > 180.f)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke stopped in stage %d"), State.Stage);
			return Finish(State, false);
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
		UCorpseSubsystem* Corpses = World->GetSubsystem<UCorpseSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Squad || !Waves || !Corpses || !Leader)
		{
			return State.Total < 10.f || Finish(State, false);
		}
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->bTacticalCeaseFire = true;
			Member->HealthComponent->SetMaxHealth(100000.f, false);
			Member->HealthComponent->Heal(100000.f);
		}
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 3.f)
			{
				return true;
			}
			SetCVar(TEXT("Codex.Pressure.Enabled"), 0.f);
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->Destroy();
			}
			struct FKind { EEnemyArchetype Type; FName Variant; const TCHAR* Label; };
			const FKind Kinds[] = {
				{ EEnemyArchetype::FrostHound, NAME_None, TEXT("hound") }, { EEnemyArchetype::FrostHound, NAME_None, TEXT("hound") },
				{ EEnemyArchetype::Frostbitten, NAME_None, TEXT("frostbitten") }, { EEnemyArchetype::Frostbitten, NAME_None, TEXT("frostbitten") },
				{ EEnemyArchetype::Frostbitten, CombatPressureRules::MilitaryZombieVariant(), TEXT("Military Zombie") },
				{ EEnemyArchetype::Frostbitten, CombatPressureRules::MilitaryZombieVariant(), TEXT("Military Zombie") },
				{ EEnemyArchetype::Cutter, NAME_None, TEXT("cutter") }, { EEnemyArchetype::Brute, NAME_None, TEXT("brute") },
				{ EEnemyArchetype::Marksman, NAME_None, TEXT("marksman") }, { EEnemyArchetype::Spitter, NAME_None, TEXT("spitter") } };
			const FVector From = Leader->GetActorLocation();
			int32 Index = 0;
			for (const FKind& Kind : Kinds)
			{
				const float Distance = 800.f + 170.f * Index;
				const FVector Direction = Leader->GetActorForwardVector().GetSafeNormal2D().RotateAngleAxis(Index * 36.f, FVector::UpVector);
				const FVector Spot = SmokeUtils::FreeSpot(World, SmokeUtils::ClearPoint(World, From, From + Direction * Distance), Leader);
				if (AEnemyCharacter* Enemy = Waves->SpawnEnemyVariant(Kind.Type, Spot, FRotator::ZeroRotator, Kind.Variant))
				{
					const UEnemyAnimInstance* Anim = Enemy->GetMesh() ? Cast<UEnemyAnimInstance>(Enemy->GetMesh()->GetAnimInstance()) : nullptr;
					State.Bodies.Add({ Enemy, Kind.Label, false, 0.f, !Anim || !Anim->HasDeathAnimation() });
				}
				++Index;
			}
			// One killed in the air (a drop link's flight, a jump): it falls to the ground first.
			const FVector AirSpot = SmokeUtils::FreeSpot(World, SmokeUtils::ClearPoint(World, From, From - Leader->GetActorForwardVector() * 900.f), Leader);
			if (AEnemyCharacter* Flyer = Waves->SpawnEnemy(EEnemyArchetype::Frostbitten, AirSpot + FVector(0.f, 0.f, 400.f)))
			{
				State.Bodies.Add({ Flyer, TEXT("frostbitten killed in the air"), true });
			}
			// 2026-11-09: a hostile PMC fighter too - his body goes under the same cap.
			if (UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World))
			{
				const FVector Direction = Leader->GetActorForwardVector().GetSafeNormal2D().RotateAngleAxis(-50.f, FVector::UpVector);
				const FVector Spot = SmokeUtils::FreeSpot(World, SmokeUtils::ClearPoint(World, From, From + Direction * 1400.f), Leader);
				const TArray<AOperativeCharacter*> Spawned = Pmc->SpawnSquad(1, Spot, (From - Spot).Rotation(), 1, 7);
				if (!Spawned.IsEmpty() && Spawned[0])
				{
					Pmc->SetTeamCommanded(1, false);
					FBody Fighter;
					Fighter.Fighter = Spawned[0];
					Fighter.Label = TEXT("hostile PMC fighter");
					State.Bodies.Add(Fighter);
				}
			}
			Check(State, State.Bodies.Num() == 12, FString::Printf(TEXT("%d enemies of every kind (a PMC fighter too) spawned 8-25 m from the squad"), State.Bodies.Num()));
			Next(State);
			return true;
		}
		case 1:
		{
			// The ground ones at once; the flyer once it is falling.
			if (State.StageTime < 0.2f)
			{
				return true;
			}
			for (FBody& Body : State.Bodies)
			{
				if (AOperativeCharacter* Fighter = Body.Fighter.Get(); Fighter && Fighter->HealthComponent && Fighter->HealthComponent->IsAlive())
				{
					FDamageSpec Spec;
					Spec.Amount = 1000000.f;
					Spec.ArmorPenetration = 1.f;
					Fighter->HealthComponent->TakeDamage(Spec);
				}
				AEnemyCharacter* Enemy = Body.Enemy.Get();
				if (Enemy && !Enemy->IsDying() && (!Body.bAirborne || Enemy->GetCharacterMovement()->IsFalling()))
				{
					Kill(Enemy);
					if (Body.bAirborne)
					{
						Check(State, RootOverGround(World, *Enemy) > 150.f, FString::Printf(TEXT("the flyer dies %.0f cm up in the air"), RootOverGround(World, *Enemy)));
					}
				}
			}
			bool bAllDead = true;
			for (const FBody& Body : State.Bodies)
			{
				bAllDead &= Body.Fighter.IsValid() ? Body.Fighter->IsKilledInAction() : (!Body.Enemy.IsValid() || Body.Enemy->IsDying());
			}
			if (!bAllDead && State.StageTime < 3.f)
			{
				return true;
			}
			Check(State, bAllDead, TEXT("all 12 killed"));
			Next(State);
			return true;
		}
		case 2:
		{
			if (State.StageTime < 30.f)
			{
				return true;
			}
			// 30 s later: every body still there, frozen.
			const UEnemyRegistrySubsystem::FEnemyList Registered = UEnemyRegistrySubsystem::GetEnemyCharacters(World);
			int32 Kept = 0;
			int32 Posed = 0;
			for (FBody& Body : State.Bodies)
			{
				if (AOperativeCharacter* Fighter = Body.Fighter.Get())
				{
					// The hostile PMC body: under the cap, frozen like the monsters (2026-11-09).
					++Posed;
					const bool bThere = !Fighter->IsActorBeingDestroyed();
					Kept += bThere && Corpses->IsKept(Fighter) ? 1 : 0;
					USkeletalMeshComponent* Mesh = Fighter->GetMesh();
					const UCharacterMovementComponent* Move = Fighter->GetCharacterMovement();
					const bool bFrozen = Fighter->IsCorpseFrozen() && !Fighter->GetController() && !Fighter->IsActorTickEnabled() && Mesh && Mesh->bPauseAnims
						&& !Mesh->IsComponentTickEnabled() && Move && !Move->IsComponentTickEnabled();
					// His capsule keeps a query collision that ignores every channel (its half height stays readable), the mesh none.
					const UCapsuleComponent* Capsule = Fighter->GetCapsuleComponent();
					bool bIgnoresAll = true;
					for (const ECollisionChannel Channel : { ECC_Pawn, ECC_Visibility, ECC_Camera, ECC_WorldStatic, ECC_WorldDynamic, ECC_PhysicsBody })
					{
						bIgnoresAll &= Capsule->GetCollisionResponseToChannel(Channel) == ECR_Ignore;
					}
					const bool bNoCollision = (Capsule->GetCollisionEnabled() == ECollisionEnabled::NoCollision || bIgnoresAll)
						&& Mesh && Mesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision;
					const bool bVisible = Mesh && Mesh->IsVisible() && !Fighter->IsHidden();
					const float Ground = PointOverGround(World, *Fighter, Fighter->GetActorLocation() - FVector(0.f, 0.f, Fighter->GetSimpleCollisionHalfHeight()));
					const UOperativeAnimInstance* Anim = Mesh ? Cast<UOperativeAnimInstance>(Mesh->GetAnimInstance()) : nullptr;
					Check(State, bThere && Corpses->IsKept(Fighter) && bFrozen && bNoCollision && bVisible && FMath::Abs(Ground) < 60.f && Anim && Anim->HasDeathPlayed(),
						FString::Printf(TEXT("%s (%s): kept under the cap %d, frozen %d (controller %s, ticks actor %d / mesh %d), collision off %d, visible %d, "
							"feet over the ground %.0f cm, death pose played %d"), *Body.Label, *GetNameSafe(Fighter->GetClass()), Corpses->IsKept(Fighter) ? 1 : 0,
							Fighter->IsCorpseFrozen() ? 1 : 0, *GetNameSafe(Fighter->GetController()), Fighter->IsActorTickEnabled() ? 1 : 0,
							Mesh && Mesh->IsComponentTickEnabled() ? 1 : 0, bNoCollision ? 1 : 0, bVisible ? 1 : 0, Ground, Anim && Anim->HasDeathPlayed() ? 1 : 0));
					Body.DistanceCm = FVector::Dist(Fighter->GetActorLocation(), Leader->GetActorLocation());
					continue;
				}
				AEnemyCharacter* Enemy = Body.Enemy.Get();
				const bool bThere = Enemy && !Enemy->IsActorBeingDestroyed();
				++Posed;
				Kept += bThere ? 1 : 0;
				if (!bThere)
				{
					Check(State, false, FString::Printf(TEXT("%s: the body is gone"), *Body.Label));
					continue;
				}
				if (Body.bPlaceholder)
				{
					// 2026-11-09: no Blueprint, no death pose - it stays, laid on its side on the floor, frozen.
					const UPrimitiveComponent* Visual = Enemy->GetCorpseVisual();
					const FBox Box = Visual ? Visual->Bounds.GetBox() : FBox(ForceInit);
					const FVector Size = Box.IsValid ? Box.GetSize() : FVector::ZeroVector;
					const bool bLying = Enemy->IsLaidDownCorpse() && Size.Z > 1.f && Size.Z < FMath::Max(Size.X, Size.Y) * 0.8f;
					const float Bottom = Box.IsValid ? PointOverGround(World, *Enemy, FVector(Box.GetCenter().X, Box.GetCenter().Y, Box.Min.Z)) : 1.e6f;
					const bool bFrozen = Enemy->IsCorpseFrozen() && !Enemy->GetController() && !Enemy->IsActorTickEnabled();
					const bool bVisible = Visual && Visual->IsVisible() && !Enemy->IsHidden();
					Check(State, bLying && bFrozen && bVisible && FMath::Abs(Bottom) < 30.f && !Registered.Contains(Enemy),
						FString::Printf(TEXT("%s (placeholder body, no death pose): stays 30 s, laid down %d (bounds %.0f x %.0f x %.0f cm), frozen %d, visible %d, "
							"bottom over the ground %.0f cm, out of the registry %d"), *Body.Label, Enemy->IsLaidDownCorpse() ? 1 : 0, Size.X, Size.Y, Size.Z,
							bFrozen ? 1 : 0, bVisible ? 1 : 0, Bottom, Registered.Contains(Enemy) ? 0 : 1));
					Body.DistanceCm = FVector::Dist(Enemy->GetActorLocation(), Leader->GetActorLocation());
					continue;
				}
				USkeletalMeshComponent* Mesh = Enemy->GetMesh();
				const UCharacterMovementComponent* Move = Enemy->GetCharacterMovement();
				const bool bFrozen = Enemy->IsCorpseFrozen() && !Enemy->GetController() && !Enemy->IsActorTickEnabled()
					&& Mesh && Mesh->bPauseAnims && !Mesh->IsComponentTickEnabled() && Move && !Move->IsComponentTickEnabled() && Move->MovementMode == MOVE_None;
				const bool bNoCollision = Enemy->GetCapsuleComponent()->GetCollisionEnabled() == ECollisionEnabled::NoCollision
					&& Mesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision;
				const bool bVisible = Mesh->IsVisible() && !Enemy->IsHidden();
				const float Ground = RootOverGround(World, *Enemy);
				const UEnemyAnimInstance* Anim = Cast<UEnemyAnimInstance>(Mesh->GetAnimInstance());
				const float PoseWeight = Anim ? Anim->GetSlotMontageGlobalWeight(Anim->OneShotSlot) : 0.f;
				Check(State, bFrozen && bNoCollision && bVisible && !Registered.Contains(Enemy) && FMath::Abs(Ground) < 60.f,
					FString::Printf(TEXT("%s (%s): stays 30 s, frozen %d (controller %s, ticks actor %d / mesh %d / move %d), collision off %d, visible %d, "
						"out of the registry %d, on the ground (root %.0f cm), death pose weight %.2f"), *Body.Label, *GetNameSafe(Enemy->GetClass()),
						Enemy->IsCorpseFrozen() ? 1 : 0, *GetNameSafe(Enemy->GetController()), Enemy->IsActorTickEnabled() ? 1 : 0,
						Mesh->IsComponentTickEnabled() ? 1 : 0, Move && Move->IsComponentTickEnabled() ? 1 : 0, bNoCollision ? 1 : 0, bVisible ? 1 : 0,
						Registered.Contains(Enemy) ? 0 : 1, Ground, PoseWeight));
				Body.DistanceCm = FVector::Dist(Enemy->GetActorLocation(), Leader->GetActorLocation());
			}
			int32 Brains = 0;
			for (TActorIterator<AAIController> It(World); It; ++It)
			{
				const AEnemyCharacter* Pawn = Cast<AEnemyCharacter>(It->GetPawn());
				Brains += Pawn && Pawn->IsDying() ? 1 : 0;
			}
			Check(State, Posed == 12 && Kept == Posed && Corpses->GetCorpseCount() == Posed && Corpses->GetFrozenCount() == Posed && Brains == 0,
				FString::Printf(TEXT("%d bodies kept of %d (monsters, the placeholder Spitter, the PMC fighter - one cap), %d frozen, %d AI controllers of the dead left"),
					Kept, Posed, Corpses->GetFrozenCount(), Brains));
			// The cap: 4 bodies on this level now.
			SetCVar(TEXT("Codex.Corpse.Cap"), 4.f);
			Next(State);
			return true;
		}
		case 3:
		{
			for (const FBody& Body : State.Bodies)
			{
				AActor* Actor = Body.Actor();
				const ICorpseBody* Corpse = Cast<ICorpseBody>(Actor);
				const UPrimitiveComponent* Visual = Corpse ? Corpse->GetCorpseVisual() : nullptr;
				if (Visual && Corpses->IsFading(Actor) && Visual->GetComponentScale().X < 0.9f && Visual->GetComponentScale().X > 0.05f)
				{
					State.bShrinkSeen = true;
				}
			}
			if ((Corpses->GetCorpseCount() > 4 || Corpses->GetFadingCount() > 0) && State.StageTime < 6.f)
			{
				return true;
			}
			Check(State, Corpses->GetCorpseCount() == 4 && Corpses->GetFadedTotal() == State.Bodies.Num() - 4,
				FString::Printf(TEXT("cap 4: %d bodies left, %d faded out in %.1f s"), Corpses->GetCorpseCount(), Corpses->GetFadedTotal(), State.StageTime));
			Check(State, State.bShrinkSeen, TEXT("... they shrank away (no pop)"));
			TArray<FBody> ByDistance = State.Bodies;
			ByDistance.Sort([](const FBody& A, const FBody& B) { return A.DistanceCm < B.DistanceCm; });
			int32 NearestKept = 0;
			for (int32 Index = 0; Index < FMath::Min(4, ByDistance.Num()); ++Index)
			{
				const AActor* Actor = ByDistance[Index].Actor();
				NearestKept += Actor && !Actor->IsActorBeingDestroyed() ? 1 : 0;
			}
			Check(State, NearestKept == 4, FString::Printf(TEXT("... the farthest went, the 4 nearest the squad stay (%d of 4)"), NearestKept));
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
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.CorpsePersistSmoke"),
		TEXT("Dev check: enemy corpses stay frozen on the ground, the per-level cap fades the farthest; PASS / FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
