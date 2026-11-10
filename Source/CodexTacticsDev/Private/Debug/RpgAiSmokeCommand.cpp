// Dev-only headless check of who fires the RPG-7 and when (the game designer's answers on the RPG-7, 2026-11-09, items 7 / 8; feat/rpg-ai-pmc;
// docs/systems/weapons.md "The RPG-7: who fires it and when") on L_MovementTest in a runtime arena:
//   Scripts/smoke_parallel.ps1 -Commands RpgAiSmoke -ExtraArgs "-Multiprocess"
// The Commander (m16 / rpg7 / pistol / grenade, the M16 in hands, AGGRESSIVE, no autonomy) against PMC fighters (team 1, uncommanded, tough):
//  1. EVIDENCE of the old behaviour (Codex.Combat.LauncherAI 0): a cluster of three 22 m away - he fires the M16 at it, never the RPG;
//  2. the rule on: the same cluster -> he draws the RPG on his own (the decision log names the cluster), fires a rocket; the fighters within
//     6 m of the burst are PINNED and fire no round for 1.5 s (they were firing at him before), those within 9 m suppressed;
//  3. a single fighter in the open 18 m away -> he keeps the rifle (no draw in 4 s, M16 rounds fired);
//  4. a single fighter standing behind a 1.1 m wall 22 m away (his head shows, the rifle's line to his body is blocked) -> the RPG, the
//     rocket bursts at the wall, he is held suppressed;
//  5. the player picked the M16 (X): the cluster gets no rocket (the player's choice wins);
//  6. the player picked the RPG: a fighter 6 m away (inside the minimum range) -> he falls back to the rifle; the fighter at 25 m -> back to
//     the RPG and a rocket;
//  7. a mate standing 3 m from the cluster -> no rocket (a mate near the impact).
// Logs PASS/FAIL, then exits.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PmcSquadSubsystem.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/ProjectileSubsystem.h"
#include "Combat/SuppressionSubsystem.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Data/WeaponDataAsset.h"
#include "Debug/SmokeUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

namespace RpgAiSmoke
{
	constexpr float StepSeconds = 0.05f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		bool bEntered = false;
		FVector O = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		TWeakObjectPtr<AOperativeCharacter> Hero;
		TWeakObjectPtr<AOperativeCharacter> Mate;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Others;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Fighters;
		TWeakObjectPtr<AStaticMeshActor> Wall;
		FDelegateHandle ImpactHandle;
		TArray<FDelegateHandle> FighterHandles;
		/** Stage counters. */
		int32 ShotsBefore = 0;
		int32 DrawsBefore = 0;
		int32 RiflesBefore = 0;
		int32 FallbacksBefore = 0;
		int32 RifleRounds = 0;
		/** The last rocket burst this stage. */
		bool bBurst = false;
		FVector BurstAt = FVector::ZeroVector;
		float BurstTime = -1.f;
		int32 BurstHeld = 0;
		/** Fighters' rounds and the last one's time. */
		int32 FighterRounds = 0;
		float LastFighterRound = -100.f;
		int32 FighterRoundsAfterBurst = 0;
		bool bPinnedSeen = false;
		bool bSuppressedSeen = false;
		bool bSawFallback = false;
		/** The decision line of the stage's draw (taken the tick the draw counter moved). */
		int32 DrawsSeen = 0;
		FString DrawLine;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	void SetCVar(const TCHAR* Name, int32 Value)
	{
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Var->Set(Value, ECVF_SetByConsole);
		}
	}

	bool Finish(UWorld* World, FState& State, bool bComplete)
	{
		if (UProjectileSubsystem* Projectiles = World ? World->GetSubsystem<UProjectileSubsystem>() : nullptr)
		{
			Projectiles->OnProjectileFinished.Remove(State.ImpactHandle);
		}
		for (int32 Index = 0; Index < State.Fighters.Num(); ++Index)
		{
			if (AOperativeCharacter* Fighter = State.Fighters[Index].Get(); Fighter && State.FighterHandles.IsValidIndex(Index))
			{
				Fighter->OnWeaponFiredNative.Remove(State.FighterHandles[Index]);
			}
		}
		SetCVar(TEXT("Codex.Combat.LauncherAI"), 1);
		SetCVar(TEXT("Codex.Combat.UnderFireManoeuvre"), 1);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("RpgAiSmoke"));
		return false;
	}

	FVector At(const FState& State, float Forward, float Right)
	{
		return State.O + State.F * Forward + State.R * Right;
	}

	void MakeTough(AOperativeCharacter* Operative)
	{
		if (Operative && Operative->HealthComponent)
		{
			Operative->HealthComponent->SetMaxHealth(100000.f, false);
			Operative->HealthComponent->Heal(100000.f);
		}
		// The live pose (as when rendered): the shot waits for the tube's real barrel to point at the target.
		if (USkeletalMeshComponent* Body = Operative ? Operative->GetMesh() : nullptr)
		{
			Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		}
	}

	void Place(UWorld* World, AOperativeCharacter* Operative, const FVector& Spot, const FVector& Facing)
	{
		if (!Operative)
		{
			return;
		}
		if (Operative->bInCover)
		{
			Operative->LeaveCover(TEXT("RpgAiSmoke: next stage"));
		}
		Operative->StopOperative();
		Operative->SetStance(EOperativeStance::Standing);
		const FVector Free = SmokeUtils::FreeSpot(World, Spot, Operative);
		Operative->TeleportTo(FVector(Free.X, Free.Y, Free.Z + 4.f), Facing.Rotation(), false, true);
		MakeTough(Operative);
	}

	/** The fighters: the first Count at the spots, the rest parked far away; cease fire on all but bFire. */
	void PlaceFighters(UWorld* World, FState& State, const TArray<FVector>& Spots, bool bFire)
	{
		for (int32 Index = 0; Index < State.Fighters.Num(); ++Index)
		{
			AOperativeCharacter* Fighter = State.Fighters[Index].Get();
			if (!Fighter)
			{
				continue;
			}
			Fighter->bTacticalCeaseFire = !bFire;
			Place(World, Fighter, Spots.IsValidIndex(Index) ? Spots[Index] : At(State, -6000.f, 6000.f + Index * 300.f), -State.F);
		}
	}

	TArray<FVector> Cluster(const FState& State, float Along)
	{
		return { At(State, Along, -150.f), At(State, Along, 150.f), At(State, Along + 200.f, 0.f) };
	}

	/** The Commander back to his rifle with the rule's state reset (not a player pick), at the start, facing the range. */
	void ResetHero(UWorld* World, FState& State)
	{
		AOperativeCharacter* Hero = State.Hero.Get();
		if (!Hero)
		{
			return;
		}
		Hero->SwitchToWeaponById(TEXT("m16"));
		Hero->ResetLauncherChoice();
		Hero->bForceHitForTesting = true; // the AI's decision is under test, not the hit roll (a near miss suppresses too: the unit tests)
		Place(World, Hero, At(State, 0.f, 0.f), State.F);
		Hero->AddAmmo(TEXT("rpg7"), 4);
		Hero->bTacticalCeaseFire = false;
		State.ShotsBefore = Hero->GetLauncherShots();
		State.DrawsBefore = Hero->GetLauncherAutoDraws();
		State.FallbacksBefore = Hero->GetLauncherFallbacks();
		State.RifleRounds = 0;
		State.bBurst = false;
		State.BurstTime = -1.f;
		State.FighterRounds = 0;
		State.FighterRoundsAfterBurst = 0;
		State.bPinnedSeen = false;
		State.bSuppressedSeen = false;
		State.bSawFallback = false;
		State.DrawsSeen = State.DrawsBefore;
		State.DrawLine.Reset();
		if (USuppressionSubsystem* Suppression = USuppressionSubsystem::Get(World))
		{
			Suppression->ClearAll(TEXT("RpgAiSmoke: next stage"));
		}
	}

	void Next(FState& State)
	{
		++State.Stage;
		State.StageTime = 0.f;
		State.bEntered = false;
	}

	bool Step(UWorld* World, FState& State)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
		if (!Squad || !Flow || !Pmc)
		{
			Check(State, false, TEXT("squad, game flow and the PMC subsystem present"));
			return Finish(World, State, false);
		}
		AOperativeCharacter* Hero = State.Hero.Get();
		if (State.Stage == 0)
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			TArray<AOperativeCharacter*> Roster = Squad->GetMembers();
			Roster.Sort([](const AOperativeCharacter& A, const AOperativeCharacter& B) { return A.SquadIndex < B.SquadIndex; });
			for (AOperativeCharacter* Member : Roster)
			{
				if (Member->SquadRole == EOperativeRole::Commander)
				{
					State.Hero = Member;
				}
			}
			Hero = State.Hero.Get();
			Check(State, Hero != nullptr, TEXT("the Commander is in the squad"));
			if (!Hero)
			{
				return Finish(World, State, false);
			}
			TArray<FString> Arsenal;
			for (const UWeaponDataAsset* Weapon : Hero->AvailableWeapons)
			{
				Arsenal.Add(Weapon ? Weapon->WeaponId : TEXT("?"));
			}
			Check(State, Hero->CurrentWeapon && Hero->CurrentWeapon->WeaponId == TEXT("m16") && Arsenal.Contains(TEXT("rpg7")),
				FString::Printf(TEXT("0: the Commander's loadout %s, the M16 in hands (rpg7 %d / %d)"), *FString::Join(Arsenal, TEXT(" / ")),
					Hero->GetAmmoState(TEXT("rpg7")).Clip, Hero->GetAmmoState(TEXT("rpg7")).Reserve));
			TArray<const AActor*> Ignore;
			for (const AOperativeCharacter* Member : Roster)
			{
				Ignore.Add(Member);
			}
			const FVector Feet = Hero->GetActorLocation() - FVector(0.f, 0.f, Hero->GetSimpleCollisionHalfHeight());
			const SmokeUtils::FClearArena Arena = SmokeUtils::FindClearArena(World, Feet, Hero->GetActorForwardVector(),
				FBox2D(FVector2D(-600.f, -900.f), FVector2D(3200.f, 900.f)), Ignore);
			Check(State, Arena.bFound, FString::Printf(TEXT("0: an empty arena found (tried %d)"), Arena.Tried));
			if (!Arena.bFound)
			{
				return Finish(World, State, false);
			}
			State.O = Arena.Origin;
			State.F = Arena.Forward;
			State.R = Arena.Right;
			SmokeUtils::StartWaveFight(Flow);
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				SmokeUtils::ParkEnemyFarAway(*It);
				It->SetActorTickEnabled(false);
			}
			Squad->SetSquadPosture(ESquadFirePosture::Aggressive);
			Squad->SetFollowersHolding(true);
			Squad->SetLeader(Hero);
			for (AOperativeCharacter* Member : Roster)
			{
				Member->ColdLevel = 0.f;
				MakeTough(Member);
				if (Member != Hero)
				{
					Member->bTacticalCeaseFire = true;
					State.Others.Add(Member);
					State.Mate = State.Mate.IsValid() ? State.Mate : TWeakObjectPtr<AOperativeCharacter>(Member);
					Place(World, Member, At(State, -500.f, 400.f + State.Others.Num() * 200.f), State.F);
				}
			}
			if (UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>())
			{
				Sight->SetRevealHostileOperatives(true);
			}
			const TArray<AOperativeCharacter*> Spawned = Pmc->SpawnSquad(1, At(State, 2200.f, 0.f), (-State.F).Rotation(), 3, 71);
			Pmc->SetTeamCommanded(1, false); // the smoke places them; they fire by their own rules
			for (AOperativeCharacter* Fighter : Spawned)
			{
				// Targets only: no launcher of their own (an RPG gunner drawn by the squad seed would fire back with rockets).
				Fighter->AvailableWeapons.RemoveAll([](const UWeaponDataAsset* Weapon) { return Weapon && Weapon->WeaponId == TEXT("rpg7"); });
				Fighter->SwitchToWeaponById(TEXT("m16"));
				MakeTough(Fighter);
				State.Fighters.Add(Fighter);
				State.FighterHandles.Add(Fighter->OnWeaponFiredNative.AddLambda([&State](AOperativeCharacter*, AActor*, bool)
				{
					++State.FighterRounds;
					State.LastFighterRound = State.StageTime;
					State.FighterRoundsAfterBurst += State.BurstTime >= 0.f && State.StageTime > State.BurstTime + 0.15f && State.StageTime < State.BurstTime + 1.65f ? 1 : 0;
				}));
			}
			Check(State, State.Fighters.Num() == 3, FString::Printf(TEXT("0: three PMC fighters (team 1) spawned (%d)"), State.Fighters.Num()));
			if (State.Fighters.Num() != 3)
			{
				return Finish(World, State, false);
			}
			State.FighterHandles.Add(Hero->OnWeaponFiredNative.AddLambda([&State](AOperativeCharacter* Shooter, AActor*, bool)
			{
				State.RifleRounds += Shooter && !Shooter->IsLauncherEquipped() ? 1 : 0;
			}));
			if (UProjectileSubsystem* Projectiles = World->GetSubsystem<UProjectileSubsystem>())
			{
				State.ImpactHandle = Projectiles->OnProjectileFinished.AddLambda([&State](const FProjectileImpact& Impact)
				{
					if (Impact.bExploded)
					{
						State.bBurst = true;
						State.BurstAt = Impact.Location;
						State.BurstTime = State.StageTime;
						State.BurstHeld = Impact.SuppressedUnits;
					}
				});
			}
			SetCVar(TEXT("Codex.Knockdown.Chance"), 0);
			SetCVar(TEXT("Codex.Combat.UnderFireManoeuvre"), 0); // the fighters stay where the stage puts them (the cluster)
			Next(State);
			return true;
		}
		if (!Hero)
		{
			Check(State, false, TEXT("the Commander alive"));
			return Finish(World, State, false);
		}
		for (const TWeakObjectPtr<AOperativeCharacter>& Other : State.Others)
		{
			if (AOperativeCharacter* Mate = Other.Get(); Mate && Mate != State.Mate.Get())
			{
				Mate->bTacticalCeaseFire = true;
			}
		}
		const float T = State.StageTime;
		if (Hero->GetLauncherAutoDraws() > State.DrawsSeen)
		{
			State.DrawsSeen = Hero->GetLauncherAutoDraws();
			State.DrawLine = Hero->GetLastLauncherDecision();
		}
		switch (State.Stage)
		{
		case 1: // the evidence: the rule off
		case 2: // the rule on
		{
			if (!State.bEntered)
			{
				State.bEntered = true;
				SetCVar(TEXT("Codex.Combat.LauncherAI"), State.Stage == 1 ? 0 : 1);
				ResetHero(World, State);
				PlaceFighters(World, State, Cluster(State, 2200.f), true);
				return true;
			}
			if (State.Stage == 1)
			{
				if (T < 8.f)
				{
					return true;
				}
				Check(State, Hero->GetLauncherShots() == State.ShotsBefore && Hero->GetLauncherAutoDraws() == State.DrawsBefore && !Hero->IsLauncherEquipped(),
					FString::Printf(TEXT("1 (EVIDENCE, Codex.Combat.LauncherAI 0 = before this change): a cluster of three 22 m away - %d rockets in 8 s, %d M16 rounds; the RPG never leaves his back"),
						Hero->GetLauncherShots() - State.ShotsBefore, State.RifleRounds));
				Check(State, State.FighterRounds > 0, FString::Printf(TEXT("1: the fighters fire at him meanwhile (%d rounds)"), State.FighterRounds));
				Next(State);
				return true;
			}
			// Stage 2.
			if (State.bBurst && State.BurstTime >= 0.f)
			{
				for (const TWeakObjectPtr<AOperativeCharacter>& Each : State.Fighters)
				{
					const AOperativeCharacter* Fighter = Each.Get();
					const ESuppressionTier Tier = USuppressionSubsystem::GetTierOf(Fighter);
					if (Fighter && T > State.BurstTime + 0.1f && T < State.BurstTime + 1.f)
					{
						const float Distance = FVector::Dist(Fighter->GetActorLocation(), State.BurstAt);
						State.bPinnedSeen |= Distance <= 600.f && Tier == ESuppressionTier::Pinned;
						State.bSuppressedSeen |= Tier != ESuppressionTier::None;
					}
				}
			}
			if ((!State.bBurst || T < State.BurstTime + 1.8f) && T < 12.f)
			{
				return true;
			}
			Check(State, Hero->GetLauncherAutoDraws() > State.DrawsBefore && Hero->GetLastLauncherDecision().Len() > 0,
				FString::Printf(TEXT("2: the rule draws the RPG on its own (%d draws; the decision: %s)"), Hero->GetLauncherAutoDraws() - State.DrawsBefore,
					*State.DrawLine));
			Check(State, Hero->GetLauncherShots() > State.ShotsBefore && State.bBurst,
				FString::Printf(TEXT("2: a rocket at the cluster (%d shots, burst %s, %d enemies held)"), Hero->GetLauncherShots() - State.ShotsBefore,
					State.bBurst ? *FString::Printf(TEXT("%.1f s into the stage"), State.BurstTime) : TEXT("none"), State.BurstHeld));
			Check(State, State.bPinnedSeen && State.bSuppressedSeen && State.BurstHeld >= 2,
				FString::Printf(TEXT("2: the fighters round the burst are PINNED (pinned %d, suppressed %d, held %d)"), State.bPinnedSeen ? 1 : 0, State.bSuppressedSeen ? 1 : 0,
					State.BurstHeld));
			Check(State, State.FighterRounds > 0 && State.FighterRoundsAfterBurst == 0,
				FString::Printf(TEXT("2: they had been firing (%d rounds) and fire none in the 1.5 s after the burst (%d)"), State.FighterRounds, State.FighterRoundsAfterBurst));
			Next(State);
			return true;
		}
		case 3: // a single exposed man: the rifle
		{
			if (!State.bEntered)
			{
				State.bEntered = true;
				ResetHero(World, State);
				PlaceFighters(World, State, { At(State, 1800.f, 0.f) }, false);
				return true;
			}
			if (T < 4.f)
			{
				return true;
			}
			Check(State, Hero->GetLauncherAutoDraws() == State.DrawsBefore && !Hero->IsLauncherEquipped() && State.RifleRounds > 0,
				FString::Printf(TEXT("3: a single fighter in the open 18 m away - he keeps the M16 (%d draws, %d M16 rounds; %s)"), Hero->GetLauncherAutoDraws() - State.DrawsBefore,
					State.RifleRounds, *Hero->GetLastLauncherDecision()));
			Next(State);
			return true;
		}
		case 4: // a man behind a wall: the RPG
		{
			if (!State.bEntered)
			{
				State.bEntered = true;
				ResetHero(World, State);
				UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
				const FVector Centre = At(State, 2100.f, 0.f);
				const FTransform Transform(State.F.Rotation(), FVector(Centre.X, Centre.Y, State.O.Z + 55.f), FVector(0.4f, 4.f, 1.1f));
				if (AStaticMeshActor* Wall = Cube ? World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform, nullptr, nullptr,
					ESpawnActorCollisionHandlingMethod::AlwaysSpawn) : nullptr)
				{
					Wall->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
					Wall->GetStaticMeshComponent()->SetStaticMesh(Cube);
					Wall->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
					Wall->FinishSpawning(Transform);
					State.Wall = Wall;
				}
				PlaceFighters(World, State, { At(State, 2200.f, 0.f) }, false);
				return true;
			}
			if (State.bBurst && T > State.BurstTime + 0.1f && T < State.BurstTime + 1.f)
			{
				State.bSuppressedSeen |= State.Fighters.Num() > 0 && USuppressionSubsystem::GetTierOf(State.Fighters[0].Get()) != ESuppressionTier::None;
			}
			if ((!State.bBurst || T < State.BurstTime + 1.2f) && T < 10.f)
			{
				return true;
			}
			const AOperativeCharacter* Fighter = State.Fighters.Num() > 0 ? State.Fighters[0].Get() : nullptr;
			Check(State, Hero->GetLauncherAutoDraws() > State.DrawsBefore && Hero->GetLauncherShots() > State.ShotsBefore && State.bBurst
					&& State.DrawLine.Contains(TEXT("in cover")),
				FString::Printf(TEXT("4: a fighter behind a 1.1 m wall 22 m away - the RPG (%d draws, %d rockets; the decision: %s)"), Hero->GetLauncherAutoDraws() - State.DrawsBefore,
					Hero->GetLauncherShots() - State.ShotsBefore, *State.DrawLine));
			Check(State, State.bBurst && Fighter && FVector::Dist(State.BurstAt, Fighter->GetActorLocation()) < 400.f && State.bSuppressedSeen,
				FString::Printf(TEXT("4: the rocket bursts by him (%.1f m off) and holds him suppressed (%d)"), Fighter && State.bBurst ? FVector::Dist(State.BurstAt,
					Fighter->GetActorLocation()) / 100.f : -1.f, State.bSuppressedSeen ? 1 : 0));
			if (AStaticMeshActor* Wall = State.Wall.Get())
			{
				Wall->Destroy();
			}
			Next(State);
			return true;
		}
		case 5: // the player picked the M16
		{
			if (!State.bEntered)
			{
				State.bEntered = true;
				ResetHero(World, State);
				Hero->NotePlayerWeaponChoice();
				PlaceFighters(World, State, Cluster(State, 2200.f), false);
				return true;
			}
			if (T < 4.f)
			{
				return true;
			}
			Check(State, Hero->GetLauncherAutoDraws() == State.DrawsBefore && !Hero->IsLauncherEquipped(),
				FString::Printf(TEXT("5: the player picked the M16 (X) - the cluster gets no rocket (%d draws; %s)"), Hero->GetLauncherAutoDraws() - State.DrawsBefore,
					*Hero->GetLastLauncherDecision()));
			Next(State);
			return true;
		}
		case 6: // the player picked the RPG: a target too close -> the rifle; far again -> the RPG
		{
			if (!State.bEntered)
			{
				State.bEntered = true;
				ResetHero(World, State);
				Hero->SwitchToWeaponById(TEXT("rpg7"));
				Hero->CurrentClip = 1; // a loaded tube (the last stage's rocket left it empty)
				Hero->NotePlayerWeaponChoice();
				PlaceFighters(World, State, { At(State, 600.f, 0.f) }, false);
				return true;
			}
			State.bSawFallback |= Hero->GetLauncherFallbacks() > State.FallbacksBefore && Hero->CurrentWeapon && Hero->CurrentWeapon->WeaponId == TEXT("m16");
			if (State.bSawFallback && State.Fighters.Num() > 0 && State.Fighters[0].IsValid()
				&& FVector::Dist(State.Fighters[0]->GetActorLocation(), Hero->GetActorLocation()) < 1500.f)
			{
				Place(World, State.Fighters[0].Get(), At(State, 2500.f, 0.f), -State.F);
			}
			if ((!State.bSawFallback || Hero->GetLauncherShots() == State.ShotsBefore) && T < 12.f)
			{
				return true;
			}
			Check(State, State.bSawFallback, FString::Printf(TEXT("6: the player's RPG, a fighter 6 m away (inside the 10 m minimum range) - he falls back to the M16 (%d)"),
				Hero->GetLauncherFallbacks() - State.FallbacksBefore));
			Check(State, Hero->GetLauncherShots() > State.ShotsBefore,
				FString::Printf(TEXT("6: the fighter at 25 m - back to the player's RPG and a rocket (%d; %s)"), Hero->GetLauncherShots() - State.ShotsBefore,
					*Hero->GetLastLauncherDecision()));
			Next(State);
			return true;
		}
		case 7: // a mate near the cluster
		{
			if (!State.bEntered)
			{
				State.bEntered = true;
				ResetHero(World, State);
				PlaceFighters(World, State, Cluster(State, 2200.f), false);
				if (AOperativeCharacter* Mate = State.Mate.Get())
				{
					Place(World, Mate, At(State, 2200.f, 400.f), State.F);
					Mate->bTacticalCeaseFire = true;
				}
				return true;
			}
			if (T < 5.f)
			{
				return true;
			}
			FLauncherTargetView View;
			const FLauncherChoice Choice = State.Fighters.Num() > 0 ? Hero->EvaluateLauncherTarget(State.Fighters[0].Get(), -1.f, &View) : FLauncherChoice();
			Check(State, Hero->GetLauncherShots() == State.ShotsBefore && Choice.Veto == ELauncherVeto::FriendlyNear,
				FString::Printf(TEXT("7: a mate 3 m from the cluster - no rocket (%d shots; the rule: %s, veto %s, mates near %d)"), Hero->GetLauncherShots() - State.ShotsBefore,
					LauncherTacticsRules::UseName(Choice.Reason), LauncherTacticsRules::VetoName(Choice.Veto), View.FriendliesNear));
			return Finish(World, State, true);
		}
		default:
			return Finish(World, State, false);
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
			State->Time += StepSeconds;
			State->StageTime += StepSeconds;
			UWorld* Current = WeakWorld.Get();
			if (!Current || State->Time > 150.f)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke stopped in stage %d"), State->Stage);
				return Current ? Finish(Current, *State, false) : false;
			}
			return Step(Current, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.RpgAiSmoke"),
		TEXT("Dev check (the game designer 2026-11-09, feat/rpg-ai-pmc): who fires the RPG-7 - the Commander draws it on his own for a cluster / a man behind a wall, the burst pins the fighters round it (no fire), a lone exposed man keeps the rifle, the player's pick wins, a too-close target falls back to the rifle, a mate near the impact vetoes; stage 1 logs the old behaviour (Codex.Combat.LauncherAI 0); logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
