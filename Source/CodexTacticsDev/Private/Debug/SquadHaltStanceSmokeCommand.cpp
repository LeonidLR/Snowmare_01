// Dev-only headless check of the game designer's decision 6 (2026-11-01 night, the stance jitter: "the Engineer and the Medic crouch / stand
// ~6 times in 2 minutes on short stops") on /Game/Maps/L_FormationLabCombat:
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands SquadHaltStanceSmoke -ExtraArgs "-Multiprocess"
// The game's crouch on a halt is the squad autonomy's ROE stance (Commander Mode, Ctrl + T: open_ground_stance = crouch for an operative standing
// still with enemies about; the bot's old crouched fight halt is gone, decision 5). In a real-time fight (a skirmish against a frozen decoy far
// away), Commander Mode on, every operative walks 8 m by his own order:
//  1. a 3 s halt: NOBODY crouches (the hold crouch waits squad_roe.json "hold_crouch_after_sec", 4 s);
//  2. a 10 s halt: everyone crouches as before - only after >= 4 s standing still;
//  3. a move stands them up at once: the leader's own order (<= 0.5 s) and - Commander Mode off, the formation walks on - the followers'
//     formation follow (<= 1.5 s); they follow upright;
//  4. the deliberate case stays: Solo ("teammates hold crouched") crouches the followers at once.
// Godot reference: none (UE-only; the autonomy's stance is Sprint 07-C).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/FirePostureRules.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/CombatPressureSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Data/SquadROE.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFlow/LevelEncounterSubsystem.h"
#include "NavigationSystem.h"
#include "SmokeUtils.h"
#include "TimerManager.h"

namespace SquadHaltStanceSmoke
{
	constexpr float NavWarmupSeconds = 3.f;
	constexpr float Timeout = 300.f;
	/** The lab's spawn lane: walking north from the spawn (open ground west of the lanes). */
	const FVector2D LaneStart(-5000.0, -4600.0);
	constexpr float LegCm = 800.f;

	enum class EStage : uint8
	{
		Setup,
		Fight,
		ShortWalk,
		ShortHalt,
		LongWalk,
		LongHalt,
		MoveOn,
		Solo,
		Done
	};

	struct FMember
	{
		TWeakObjectPtr<AOperativeCharacter> Operative;
		/** His own spot's offset from the lane (the leader 0). */
		FVector2D Offset = FVector2D::ZeroVector;
		/** Stage time he stopped moving (-1 = moving), when he crouched (-1 = not yet). */
		float StoppedAt = -1.f;
		float CrouchedAt = -1.f;
		bool bCrouchedInShortHalt = false;
	};

	struct FState
	{
		EStage Stage = EStage::Setup;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Failures = 0;
		TWeakObjectPtr<AEnemyCharacter> Decoy;
		TArray<FMember> Members;
		int32 Leg = 0;
		bool bChecked = false;
		float LeaderUpAt = -1.f;
		float FollowersUpAt = -1.f;
		bool bAutonomyBefore = false;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(UWorld* World, FState& State, bool bComplete)
	{
		if (USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr)
		{
			Squad->SetAutonomousSquadCombat(State.bAutonomyBefore);
		}
		if (AEnemyCharacter* Decoy = State.Decoy.Get())
		{
			Decoy->Destroy();
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		State.Stage = EStage::Done;
		FPlatformMisc::RequestExit(false, TEXT("SquadHaltStanceSmoke"));
		return false;
	}

	void Enter(FState& State, EStage Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
		State.bChecked = false;
		for (FMember& Each : State.Members)
		{
			Each.StoppedAt = -1.f;
			Each.CrouchedAt = -1.f;
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[HaltStance] stage %d at %.1f s"), static_cast<int32>(Stage), State.Time);
	}

	TOptional<FVector> GroundNav(UWorld* World, const FVector& Point)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		FHitResult Ground;
		if (!Nav || !World->LineTraceSingleByObjectType(Ground, Point + FVector(0.f, 0.f, 1500.f), Point - FVector(0.f, 0.f, 1500.f),
			FCollisionObjectQueryParams(ECC_WorldStatic)))
		{
			return {};
		}
		FNavLocation OnNav;
		if (!Nav->ProjectPointToNavigation(Ground.ImpactPoint, OnNav, FVector(60.f, 60.f, 100.f)))
		{
			return {};
		}
		return OnNav.Location;
	}

	/** Every operative walks the next 8 m leg north by his own order (Commander Mode: each holds his own order point). */
	bool OrderLeg(UWorld* World, FState& State)
	{
		++State.Leg;
		bool bAll = true;
		for (const FMember& Each : State.Members)
		{
			AOperativeCharacter* Operative = Each.Operative.Get();
			const TOptional<FVector> Goal = GroundNav(World, FVector(LaneStart.X + Each.Offset.X, LaneStart.Y + Each.Offset.Y + LegCm * State.Leg, 0.f));
			bAll &= Operative && Goal.IsSet() && Operative->OrderMoveTo(Goal.GetValue(), false) != EOperativeOrderResult::Refused;
		}
		return bAll;
	}

	/** Tracks each operative's stop / crouch times this stage; true when everybody has stood still >= 0.3 s. */
	bool TrackMembers(FState& State)
	{
		bool bAllStill = true;
		for (FMember& Each : State.Members)
		{
			const AOperativeCharacter* Operative = Each.Operative.Get();
			if (!Operative)
			{
				continue;
			}
			const bool bMoving = Operative->IsMoving() || Operative->GetVelocity().Size2D() > 30.f;
			Each.StoppedAt = bMoving ? -1.f : (Each.StoppedAt < 0.f ? State.StageTime : Each.StoppedAt);
			if (Operative->GetStance() == EOperativeStance::Crouching && Each.CrouchedAt < 0.f)
			{
				Each.CrouchedAt = State.StageTime;
			}
			bAllStill &= Each.StoppedAt >= 0.f && State.StageTime - Each.StoppedAt >= 0.3f;
		}
		return bAllStill;
	}

	FString NameOf(const FMember& Each)
	{
		return Each.Operative.IsValid() ? Each.Operative->DisplayName.ToString() : FString(TEXT("?"));
	}

	bool Step(UWorld* World, FState& State, float Dt)
	{
		State.Time += Dt;
		State.StageTime += Dt;
		if (State.Time > Timeout)
		{
			Check(State, false, FString::Printf(TEXT("timeout in stage %d"), static_cast<int32>(State.Stage)));
			return Finish(World, State, false);
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Squad || !Flow || !Leader)
		{
			Check(State, false, TEXT("squad, game flow and a leader"));
			return Finish(World, State, false);
		}
		const float CrouchAfter = SquadROE::Get().HoldCrouchAfterSeconds;

		switch (State.Stage)
		{
		case EStage::Setup:
		{
			if (UCombatPressureSubsystem* Pressure = World->GetSubsystem<UCombatPressureSubsystem>())
			{
				Pressure->SetAutoSpawn(false);
				Pressure->DespawnAll();
			}
			Squad->SetSquadPosture(ESquadFirePosture::Passive);
			if (Squad->IsSoloMode())
			{
				Squad->ExitSoloMode(false, 0.f);
			}
			// The squad at the spawn lane facing north, standing, 3 m apart abreast.
			const FRotator North(0.f, 90.f, 0.f);
			int32 Index = 0;
			for (AOperativeCharacter* Each : Squad->GetMembers())
			{
				if (!Each)
				{
					continue;
				}
				FMember& Member = State.Members.AddDefaulted_GetRef();
				Member.Operative = Each;
				Member.Offset = FVector2D(Each == Leader ? 0.0 : (Index % 2 == 1 ? 300.0 : -300.0) * ((Index + 1) / 2), Each == Leader ? 0.0 : -150.0);
				Each->StopOperative();
				Each->SetStance(EOperativeStance::Standing);
				const FVector Want(LaneStart.X + Member.Offset.X, LaneStart.Y + Member.Offset.Y, 0.f);
				const FVector Spot = SmokeUtils::FreeSpot(World, GroundNav(World, Want).Get(Want), Each);
				Each->TeleportTo(Spot + FVector(0.f, 0.f, 96.f), North, false, true);
				Index += Each == Leader ? 0 : 1;
			}
			Squad->ResetFormationState(North.Vector());
			// A frozen decoy far away (east of the lab): the real-time skirmish runs; Commander Mode on (the autonomy, its ROE stance).
			UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
			const TOptional<FVector> DecoyFeet = GroundNav(World, FVector(4800.0, 5000.0, 0.0));
			AEnemyCharacter* Decoy = Waves && DecoyFeet.IsSet() ? Waves->SpawnLevelEnemy(EEnemyArchetype::Frostbitten, DecoyFeet.GetValue() + FVector(0.f, 0.f, 95.f)) : nullptr;
			if (!Decoy || State.Members.Num() < 3)
			{
				Check(State, false, FString::Printf(TEXT("a decoy enemy and three operatives (%d)"), State.Members.Num()));
				return Finish(World, State, false);
			}
			if (ULevelEncounterSubsystem* Encounter = World->GetSubsystem<ULevelEncounterSubsystem>())
			{
				Encounter->MarkLevelEnemy(Decoy);
			}
			Decoy->CustomTimeDilation = 0.f;
			Decoy->SetActorTickEnabled(false);
			if (UHealthComponent* Health = Decoy->GetHealthComponent())
			{
				Health->SetMaxHealth(1000000.f, true);
			}
			State.Decoy = Decoy;
			State.bAutonomyBefore = Squad->IsAutonomousSquadCombat();
			Squad->SetAutonomousSquadCombat(true);
			ULevelEncounterSubsystem::NotifyHostileContactIn(World, EAmbushTrigger::SquadAutoFire, Decoy);
			Enter(State, EStage::Fight);
			return true;
		}

		case EStage::Fight:
		{
			if (!Flow->IsSquadAutonomyActive() && State.StageTime < 10.f)
			{
				return true;
			}
			if (!State.bChecked)
			{
				State.bChecked = true;
				Check(State, Flow->GetPhase() == ECodexGamePhase::WaveCombat && Flow->IsSquadAutonomyActive(),
					FString::Printf(TEXT("a real-time skirmish runs, Commander Mode on (open-ground stance crouch, hold_crouch_after_sec %.1f)"), CrouchAfter));
				Check(State, CrouchAfter >= 3.5f, FString::Printf(TEXT("the hold crouch waits longer than a 3 s halt (%.1f s)"), CrouchAfter));
			}
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			Check(State, OrderLeg(World, State), TEXT("everybody walks 8 m north (own orders)"));
			Enter(State, EStage::ShortWalk);
			return true;
		}

		case EStage::ShortWalk:
		case EStage::LongWalk:
		{
			const bool bAllStill = TrackMembers(State);
			if (State.StageTime < 0.5f || (!bAllStill && State.StageTime < 20.f))
			{
				return true;
			}
			for (const FMember& Each : State.Members)
			{
				Check(State, Each.Operative.IsValid() && Each.Operative->GetStance() == EOperativeStance::Standing,
					FString::Printf(TEXT("%s walked his 8 m upright and stopped"), *NameOf(Each)));
			}
			Enter(State, State.Stage == EStage::ShortWalk ? EStage::ShortHalt : EStage::LongHalt);
			return true;
		}

		case EStage::ShortHalt:
		{
			TrackMembers(State);
			for (FMember& Each : State.Members)
			{
				Each.bCrouchedInShortHalt |= Each.Operative.IsValid() && Each.Operative->GetStance() != EOperativeStance::Standing;
			}
			if (State.StageTime < 3.f)
			{
				return true;
			}
			FString Who;
			bool bNone = true;
			for (const FMember& Each : State.Members)
			{
				Who += FString::Printf(TEXT("%s%s %s"), Who.IsEmpty() ? TEXT("") : TEXT(", "), *NameOf(Each), Each.bCrouchedInShortHalt ? TEXT("CROUCHED") : TEXT("standing"));
				bNone &= !Each.bCrouchedInShortHalt;
			}
			Check(State, bNone, FString::Printf(TEXT("a 3 s halt in the fight: nobody crouches (%s)"), *Who));
			Check(State, OrderLeg(World, State), TEXT("everybody walks on 8 m"));
			Enter(State, EStage::LongWalk);
			return true;
		}

		case EStage::LongHalt:
		{
			TrackMembers(State);
			if (State.StageTime < 10.f)
			{
				return true;
			}
			for (const FMember& Each : State.Members)
			{
				const AOperativeCharacter* Operative = Each.Operative.Get();
				const float StillFor = Each.CrouchedAt >= 0.f ? Each.CrouchedAt : -1.f;
				Check(State, Operative && Operative->GetStance() == EOperativeStance::Crouching && StillFor >= CrouchAfter - 0.7f && StillFor <= CrouchAfter + 1.f,
					FString::Printf(TEXT("a long halt: %s crouches as before, %.1f s after the halt began (hold_crouch_after_sec %.1f)"), *NameOf(Each), StillFor,
						CrouchAfter));
			}
			// The fight's autonomy off (the formation walks on), the leader's own order: everybody up at once, the squad follows upright.
			Squad->SetAutonomousSquadCombat(false);
			const TOptional<FVector> Goal = GroundNav(World, FVector(LaneStart.X, LaneStart.Y + LegCm * (State.Leg + 1), 0.f));
			Check(State, Goal.IsSet() && Leader->OrderMoveTo(Goal.GetValue(), false) != EOperativeOrderResult::Refused, TEXT("the leader walks on (Commander Mode off)"));
			State.LeaderUpAt = -1.f;
			State.FollowersUpAt = -1.f;
			Enter(State, EStage::MoveOn);
			return true;
		}

		case EStage::MoveOn:
		{
			bool bFollowersUp = true;
			for (const FMember& Each : State.Members)
			{
				const bool bUp = Each.Operative.IsValid() && Each.Operative->GetStance() == EOperativeStance::Standing;
				if (Each.Operative.Get() == Leader)
				{
					State.LeaderUpAt = bUp && State.LeaderUpAt < 0.f ? State.StageTime : State.LeaderUpAt;
				}
				else
				{
					bFollowersUp &= bUp;
				}
			}
			State.FollowersUpAt = bFollowersUp && State.FollowersUpAt < 0.f ? State.StageTime : State.FollowersUpAt;
			if (State.StageTime < 4.f)
			{
				return true;
			}
			Check(State, State.LeaderUpAt >= 0.f && State.LeaderUpAt <= 0.5f,
				FString::Printf(TEXT("the leader's own move order stands him up at once (%.2f s)"), State.LeaderUpAt));
			Check(State, State.FollowersUpAt >= 0.f && State.FollowersUpAt <= 1.5f,
				FString::Printf(TEXT("the formation's follow stands the followers up at once (%.2f s)"), State.FollowersUpAt));
			bool bFollowing = true;
			for (const FMember& Each : State.Members)
			{
				bFollowing &= Each.Operative.IsValid() && Each.Operative->GetStance() == EOperativeStance::Standing
					&& FVector::Dist2D(Each.Operative->GetActorLocation(), Leader->GetActorLocation()) <= 1200.f;
			}
			Check(State, bFollowing, TEXT("... and they follow the leader upright"));
			Squad->EnterSoloMode();
			Enter(State, EStage::Solo);
			return true;
		}

		case EStage::Solo:
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			bool bAllCrouched = true;
			for (const FMember& Each : State.Members)
			{
				bAllCrouched &= Each.Operative.Get() == Leader || (Each.Operative.IsValid() && Each.Operative->GetStance() == EOperativeStance::Crouching);
			}
			Check(State, bAllCrouched, TEXT("the deliberate case stays: Solo crouches the followers at once (\"teammates hold crouched\")"));
			Squad->ExitSoloMode(false, 0.f);
			return Finish(World, State, true);
		}

		default:
			return false;
		}
	}

	void Start(UWorld* World)
	{
		TSharedRef<FState> State = MakeShared<FState>();
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			UWorld* W = WeakWorld.Get();
			return W && State->Stage != EStage::Done && Step(W, *State, W->IsPaused() ? 0.f : W->GetDeltaSeconds());
		}), 0.f);
	}

	void Run(const TArray<FString>&, UWorld* World)
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
		}), NavWarmupSeconds, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.SquadHaltStanceSmoke"),
		TEXT("Dev check (decision 6, 2026-11-01 night, L_FormationLabCombat): in a fight (Commander Mode) a 3 s halt crouches nobody, a long halt crouches ")
		TEXT("them after hold_crouch_after_sec, the next move stands them up at once, Solo still crouches the followers at once. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
