// Dev-only headless check of the PMC RPG gunner (the game designer's answers on the RPG-7, 2026-11-09, items 6 / 7 / 8; feat/rpg-ai-pmc;
// docs/systems/pmc-squad-ai.md "The RPG gunner") on L_MovementTest in a runtime arena:
//   Scripts/smoke_parallel.ps1 -Commands RpgPmcSmoke -ExtraArgs "-Multiprocess"
// Our squad (cease fire, tough) stands together behind a 1.1 m wall; a commanded PMC team of two (team 1) 26 m away - a rifleman and the RPG
// gunner (m16 in hands + rpg7, as pmc_operators.json "rpg_gunner" gives a squad):
//  0. the gunner's data: the rpg_gunner block parsed, at most one gunner per squad over 200 seeds, ~the share;
//  1. the shared launcher rule (the gunner's own fire loop): he draws the RPG, the PMC skeleton plays the launcher set (the weapon layer on
//     the launcher handling with the Pmc retarget folder, the Shot clip), the rocket flies at our squad, bursts by the wall and holds our men
//     round it suppressed / pinned;
//  2. the commander's RpgStrike order (Codex.Combat.LauncherAI 0: only an ordered rocket may go): the team's commander orders the strike,
//     the gunner fires it.
// Logs PASS/FAIL, then exits.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PmcCommanderRules.h"
#include "Animation/Skeleton.h"
#include "AI/PmcOperatorRules.h"
#include "AI/PmcSquadSubsystem.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeAnimInstance.h"
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
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

namespace RpgPmcSmoke
{
	constexpr float StepSeconds = 0.05f;
	constexpr float WallAlong = 300.f;
	constexpr float FightersAlong = 2900.f;

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
		TArray<TWeakObjectPtr<AOperativeCharacter>> Ours;
		TWeakObjectPtr<AOperativeCharacter> Gunner;
		TWeakObjectPtr<AOperativeCharacter> Rifleman;
		FDelegateHandle ImpactHandle;
		int32 ShotsBefore = 0;
		int32 StrikesBefore = 0;
		int32 StrikeOrdersBefore = 0;
		bool bBurst = false;
		FVector BurstAt = FVector::ZeroVector;
		float BurstTime = -1.f;
		int32 BurstHeld = 0;
		bool bDrawn = false;
		bool bLayer = false;
		bool bShotClip = false;
		FString LayerFolder;
		FString ShotClip;
		FString Skeleton;
		bool bOursHeld = false;
		bool bOursPinned = false;
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
		SetCVar(TEXT("Codex.Combat.LauncherAI"), 1);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("RpgPmcSmoke"));
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
		Operative->StopOperative();
		const FVector Free = SmokeUtils::FreeSpot(World, Spot, Operative);
		Operative->TeleportTo(FVector(Free.X, Free.Y, Free.Z + 4.f), Facing.Rotation(), false, true);
	}

	/** Watches the gunner every tick: the draw, the weapon layer on the launcher set and its shot clip; our men's tiers after a burst. */
	void Watch(FState& State)
	{
		AOperativeCharacter* Gunner = State.Gunner.Get();
		if (!Gunner)
		{
			return;
		}
		State.bDrawn |= Gunner->IsLauncherEquipped();
		const UOperativeAnimInstance* Anim = Gunner->GetMesh() ? Cast<UOperativeAnimInstance>(Gunner->GetMesh()->GetAnimInstance()) : nullptr;
		if (Anim && Gunner->IsLauncherEquipped() && Anim->GetWeaponLayerHandling() == EWeaponHandling::Launcher && Anim->GetShotgunLayerMask() != EWeaponLayerMask::Off)
		{
			State.bLayer = true;
			State.LayerFolder = Anim->GetShotgunRetargetFolder();
		}
		if (Anim && Anim->GetLauncherAction() == ELauncherAction::Shot && Anim->GetLauncherActionClip())
		{
			State.bShotClip = true;
			State.ShotClip = Anim->GetLauncherActionClip()->GetPathName();
			const USkeleton* ClipSkeleton = Anim->GetLauncherActionClip()->GetSkeleton();
			State.Skeleton = ClipSkeleton ? ClipSkeleton->GetName() : TEXT("?");
		}
		if (State.bBurst && State.StageTime > State.BurstTime + 0.1f && State.StageTime < State.BurstTime + 1.f)
		{
			for (const TWeakObjectPtr<AOperativeCharacter>& Each : State.Ours)
			{
				const ESuppressionTier Tier = USuppressionSubsystem::GetTierOf(Each.Get());
				State.bOursHeld |= Tier != ESuppressionTier::None;
				State.bOursPinned |= Tier == ESuppressionTier::Pinned;
			}
		}
	}

	void ResetStage(UWorld* World, FState& State)
	{
		AOperativeCharacter* Gunner = State.Gunner.Get();
		if (Gunner)
		{
			Gunner->SwitchToWeaponById(TEXT("m16"));
			Gunner->ResetLauncherChoice();
			Gunner->AddAmmo(TEXT("rpg7"), 3);
			Gunner->bForceHitForTesting = true;
			State.ShotsBefore = Gunner->GetLauncherShots();
			State.StrikesBefore = Gunner->GetLauncherStrikeShots();
		}
		if (const UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World); Pmc && Pmc->GetTeamStats(1))
		{
			State.StrikeOrdersBefore = Pmc->GetTeamStats(1)->RpgStrikes;
		}
		Place(World, State.Gunner.Get(), At(State, FightersAlong, 150.f), -State.F);
		Place(World, State.Rifleman.Get(), At(State, FightersAlong, -250.f), -State.F);
		State.bBurst = false;
		State.BurstTime = -1.f;
		State.bDrawn = State.bLayer = State.bShotClip = false;
		State.bOursHeld = State.bOursPinned = false;
		if (USuppressionSubsystem* Suppression = USuppressionSubsystem::Get(World))
		{
			Suppression->ClearAll(TEXT("RpgPmcSmoke: next stage"));
		}
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
		if (State.Stage == 0)
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			// 0. The data: one gunner per squad at most, about the share.
			const FPmcOperatorConfig& Config = PmcOperatorRules::Get();
			int32 Squads = 0;
			int32 WithGunner = 0;
			int32 MaxInSquad = 0;
			for (int32 Seed = 0; Seed < 200; ++Seed)
			{
				int32 Count = 0;
				for (int32 Index = 0; Index < 6; ++Index)
				{
					Count += PmcOperatorRules::IsRpgGunner(Config, Seed, Index) ? 1 : 0;
				}
				++Squads;
				WithGunner += Count > 0 ? 1 : 0;
				MaxInSquad = FMath::Max(MaxInSquad, Count);
			}
			Check(State, Config.RpgGunner.WeaponId == TEXT("rpg7") && MaxInSquad <= Config.RpgGunner.MaxPerSquad
				&& FMath::Abs(static_cast<float>(WithGunner) / Squads - Config.RpgGunner.Share) < 0.15f,
				FString::Printf(TEXT("0: pmc_operators.json rpg_gunner %s, share %.2f: %d of %d squads have one, at most %d per squad"), *Config.RpgGunner.WeaponId,
					Config.RpgGunner.Share, WithGunner, Squads, MaxInSquad));

			TArray<AOperativeCharacter*> Roster = Squad->GetMembers();
			Roster.Sort([](const AOperativeCharacter& A, const AOperativeCharacter& B) { return A.SquadIndex < B.SquadIndex; });
			if (Roster.IsEmpty())
			{
				Check(State, false, TEXT("a squad"));
				return Finish(World, State, false);
			}
			TArray<const AActor*> Ignore;
			for (const AOperativeCharacter* Member : Roster)
			{
				Ignore.Add(Member);
			}
			const FVector Feet = Roster[0]->GetActorLocation() - FVector(0.f, 0.f, Roster[0]->GetSimpleCollisionHalfHeight());
			const SmokeUtils::FClearArena Arena = SmokeUtils::FindClearArena(World, Feet, Roster[0]->GetActorForwardVector(),
				FBox2D(FVector2D(-600.f, -900.f), FVector2D(3400.f, 900.f)), Ignore);
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
			Squad->SetSquadPosture(ESquadFirePosture::Passive);
			Squad->SetFollowersHolding(true);
			// Our three men side by side 1.2 m apart, 1 m behind a 1.1 m wall (in cover; their heads show), cease fire.
			UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
			const FVector Centre = At(State, WallAlong, 0.f);
			const FTransform Transform(State.F.Rotation(), FVector(Centre.X, Centre.Y, State.O.Z + 55.f), FVector(0.4f, 6.f, 1.1f));
			if (AStaticMeshActor* Wall = Cube ? World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn) : nullptr)
			{
				Wall->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
				Wall->GetStaticMeshComponent()->SetStaticMesh(Cube);
				Wall->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
				Wall->FinishSpawning(Transform);
			}
			for (int32 Index = 0; Index < Roster.Num(); ++Index)
			{
				AOperativeCharacter* Member = Roster[Index];
				Member->bTacticalCeaseFire = true;
				Member->ColdLevel = 0.f;
				MakeTough(Member);
				Place(World, Member, At(State, WallAlong - 100.f, (Index - 1) * 120.f), State.F);
				Member->SetStance(EOperativeStance::Standing);
				State.Ours.Add(Member);
			}
			SetCVar(TEXT("Codex.Combat.UnderFireManoeuvre"), 0); // our men stay behind the wall
			SetCVar(TEXT("Codex.Knockdown.Chance"), 0);
			if (UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>())
			{
				Sight->SetRevealHostileOperatives(true);
			}
			const TArray<AOperativeCharacter*> Spawned = Pmc->SpawnSquad(1, At(State, FightersAlong, 0.f), (-State.F).Rotation(), 2, 83);
			if (Spawned.Num() == 2)
			{
				// The squad's gunner as pmc_operators.json makes one (the seed decides who; the smoke makes sure one is there).
				AOperativeCharacter* Gunner = Spawned[0]->HasLauncherRockets() ? Spawned[0] : Spawned[1];
				if (!Gunner->HasLauncherRockets())
				{
					if (UWeaponDataAsset* Launcher = LoadObject<UWeaponDataAsset>(nullptr, *PmcOperatorRules::WeaponAssetPath(TEXT("rpg7"))))
					{
						Gunner->AddArsenalWeapon(Launcher);
					}
				}
				AOperativeCharacter* Rifleman = Gunner == Spawned[0] ? Spawned[1] : Spawned[0];
				Rifleman->AvailableWeapons.RemoveAll([](const UWeaponDataAsset* Weapon) { return Weapon && Weapon->WeaponId == TEXT("rpg7"); });
				for (AOperativeCharacter* Fighter : Spawned)
				{
					Fighter->SwitchToWeaponById(TEXT("m16"));
					MakeTough(Fighter);
				}
				Rifleman->bTacticalCeaseFire = true; // only the gunner's rockets in this smoke
				State.Gunner = Gunner;
				State.Rifleman = Rifleman;
			}
			AOperativeCharacter* Gunner = State.Gunner.Get();
			const USkeletalMesh* Body = Gunner && Gunner->GetMesh() ? Gunner->GetMesh()->GetSkeletalMeshAsset() : nullptr;
			Check(State, Gunner && Gunner->HasLauncherRockets() && Body && Body->GetName() == TEXT("SKM_PmcOperator"),
				FString::Printf(TEXT("0: a PMC team of two 26 m away - the gunner %s (%s, body %s) carries the RPG-7"), Gunner ? *Gunner->DisplayName.ToString() : TEXT("-"),
					Gunner && Gunner->CurrentWeapon ? *Gunner->CurrentWeapon->WeaponId : TEXT("-"), Body ? *Body->GetName() : TEXT("-")));
			if (!Gunner)
			{
				return Finish(World, State, false);
			}
			Pmc->SetTeamCommanded(1, true);
			if (UProjectileSubsystem* Projectiles = World->GetSubsystem<UProjectileSubsystem>())
			{
				State.ImpactHandle = Projectiles->OnProjectileFinished.AddLambda([&State](const FProjectileImpact& Impact)
				{
					if (Impact.bExploded && !State.bBurst)
					{
						State.bBurst = true;
						State.BurstAt = Impact.Location;
						State.BurstTime = State.StageTime;
						State.BurstHeld = Impact.SuppressedUnits;
					}
				});
			}
			State.Stage = 1;
			State.StageTime = 0.f;
			return true;
		}
		AOperativeCharacter* Gunner = State.Gunner.Get();
		if (!Gunner)
		{
			Check(State, false, TEXT("the gunner alive"));
			return Finish(World, State, false);
		}
		for (const TWeakObjectPtr<AOperativeCharacter>& Each : State.Ours)
		{
			if (AOperativeCharacter* Member = Each.Get())
			{
				Member->bTacticalCeaseFire = true;
			}
		}
		Watch(State);
		const float T = State.StageTime;
		const FPmcTeamStats* Stats = Pmc->GetTeamStats(1);
		switch (State.Stage)
		{
		case 1: // the shared launcher rule
		case 2: // the commander's RpgStrike order alone
		{
			if (!State.bEntered)
			{
				State.bEntered = true;
				SetCVar(TEXT("Codex.Combat.LauncherAI"), State.Stage == 1 ? 1 : 0);
				ResetStage(World, State);
				return true;
			}
			if ((!State.bBurst || T < State.BurstTime + 1.2f) && T < 16.f)
			{
				return true;
			}
			const float WallDistance = State.bBurst ? FMath::Abs(FVector::DotProduct(State.BurstAt - At(State, WallAlong, 0.f), State.F)) : -1.f;
			if (State.Stage == 1)
			{
				Check(State, State.bDrawn && Gunner->GetLauncherShots() > State.ShotsBefore,
					FString::Printf(TEXT("1: the gunner draws the RPG and fires (%d rockets; %s)"), Gunner->GetLauncherShots() - State.ShotsBefore,
						*Gunner->GetLastLauncherDecision()));
				Check(State, State.bLayer && State.LayerFolder.Contains(TEXT("Launcher/Pmc")),
					FString::Printf(TEXT("1: the launcher set on the PMC skeleton (layer %d, clips from %s)"), State.bLayer ? 1 : 0, *State.LayerFolder));
				Check(State, State.bShotClip && State.ShotClip.Contains(TEXT("/Launcher/Pmc/")) && State.Skeleton == TEXT("SK_PmcOperator_Skeleton"),
					FString::Printf(TEXT("1: the retargeted Shot clip plays on him (%s, skeleton %s)"), *State.ShotClip, *State.Skeleton));
			}
			else
			{
				Check(State, Stats && Stats->RpgStrikes > State.StrikeOrdersBefore && Gunner->GetLauncherStrikeShots() > State.StrikesBefore,
					FString::Printf(TEXT("2: Codex.Combat.LauncherAI 0 - the commander's RpgStrike order (%d orders) fires the rocket (%d strike shots; %s)"),
						Stats ? Stats->RpgStrikes - State.StrikeOrdersBefore : -1, Gunner->GetLauncherStrikeShots() - State.StrikesBefore, *Gunner->GetLastLauncherDecision()));
			}
			Check(State, State.bBurst && WallDistance >= 0.f && WallDistance < 300.f,
				FString::Printf(TEXT("%d: the rocket bursts at our wall (%.1f m off it)"), State.Stage, WallDistance / 100.f));
			Check(State, State.bOursHeld && State.BurstHeld >= 2,
				FString::Printf(TEXT("%d: our men behind it are suppressed (%d held, pinned seen %d)"), State.Stage, State.BurstHeld, State.bOursPinned ? 1 : 0));
			if (State.Stage == 2)
			{
				return Finish(World, State, true);
			}
			++State.Stage;
			State.StageTime = 0.f;
			State.bEntered = false;
			return true;
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
			if (!Current || State->Time > 120.f)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke stopped in stage %d"), State->Stage);
				return Current ? Finish(Current, *State, false) : false;
			}
			return Step(Current, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.RpgPmcSmoke"),
		TEXT("Dev check (the game designer 2026-11-09, feat/rpg-ai-pmc): a PMC RPG gunner draws the RPG-7, the launcher set plays on the PMC skeleton, the rocket bursts at our squad's wall and suppresses them; the commander's RpgStrike order fires one too; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
