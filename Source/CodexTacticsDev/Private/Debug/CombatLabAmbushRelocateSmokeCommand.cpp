// Dev-only headless check of the game designer's decisions of 2026-11-01 night on the combat lab's ambushes (docs/design/COMBAT_LAB.md 2.1)
// on /Game/Maps/L_FormationLabCombat (level JSON L_FormationLabCombat.json):
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands CombatLabAmbushRelocateSmoke -ExtraArgs "-Multiprocess"
//  1. a coordinated plan watched ALL ROUND strikes anyway once it has held its hold limit (decision 2; the limit shortened to 5-6 s here);
//  2. a bypassed ambush running its concealed route is SPOTTED on the way by an operative on the ground who looks at it -> it strikes at once
//     and the fight starts (decision 1);
//  3. the same with the watcher on a TOWER ROOF (the elevated eye sees over the lab's walls) -> it strikes at once;
//  4. NO concealed route (every route counted as seen): no teleport - the ambush stays put after the first search, then becomes a patrol
//     following the squad on foot (decision 1);
//  5. a single ambush watched all along strikes anyway after its own 60-90 s hold limit (decision 2, the level's real numbers).
// (The concealed run itself - set off, run on foot unseen, arrive ahead - is CombatLabAmbushSmoke's relocation stage.)
// Godot reference: none (UE-only).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/AmbushWatchRules.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/FirePostureRules.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/AmbushPlanRules.h"
#include "Combat/AmbushRelocateRules.h"
#include "Combat/CombatPressureSubsystem.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Ladders/LadderActor.h"
#include "NavigationSystem.h"
#include "SmokeUtils.h"
#include "TimerManager.h"

namespace CombatLabAmbushRelocateSmoke
{
	constexpr float NavWarmupSeconds = 3.f;
	constexpr float Timeout = 900.f;
	/**
	 * The bypassed ambush: hidden NORTH of the 43 m wall W_E_13 (y 2500) while the squad stands south of it, heading west past it - the
	 * ambush is behind them; a concealed route runs along the wall's north side to the wall-end spots ahead (the decision 1 run).
	 */
	const FVector2D SquadLead(-2500.0, 1800.0);
	const FVector2D AmbushHide(-500.0, 2900.0);

	enum class EStage : uint8
	{
		PlanPlace,
		PlanHold,
		PlanSettle,
		WatchPlace,
		WatchWaitRun,
		WatchSpot,
		WatchSettle,
		NoRoutePlace,
		NoRouteStay,
		NoRoutePatrol,
		NoRouteSettle,
		HoldPlace,
		HoldWait,
		Done
	};

	struct FState
	{
		EStage Stage = EStage::PlanPlace;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Failures = 0;
		bool bPlaced = false;
		int32 PlanId = 0;
		int32 HoldBefore = 0;
		/** 0 = the ground watcher, 1 = the tower watcher. */
		int32 WatchCase = 0;
		TWeakObjectPtr<AEnemyCharacter> Ambusher;
		int32 RunRecord = INDEX_NONE;
		int32 RelocationsBefore = 0;
		TWeakObjectPtr<AOperativeCharacter> Watcher;
		bool bWatcherPlaced = false;
		FString WatcherWhere;
		float PlacedAt = 0.f;
		FVector StartFeet = FVector::ZeroVector;
		FVector LastFeet = FVector::ZeroVector;
		float MaxFrameStepCm = 0.f;
		int32 NoRouteBefore = 0;
		float HoldLimit = 0.f;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		State.Stage = EStage::Done;
		FPlatformMisc::RequestExit(false, TEXT("CombatLabAmbushRelocateSmoke"));
		return false;
	}

	void Enter(FState& State, EStage Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
		State.bPlaced = false;
		UE_LOG(LogCodexTactics, Display, TEXT("[CombatLabRelocate] stage %d at %.1f s"), static_cast<int32>(Stage), State.Time);
	}

	FVector FeetOf(const ACharacter& Character)
	{
		return Character.GetActorLocation() - FVector(0.f, 0.f, Character.GetSimpleCollisionHalfHeight());
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
		if (!Nav->ProjectPointToNavigation(Ground.ImpactPoint, OnNav, FVector(60.f, 60.f, 100.f)) || FMath::Abs(OnNav.Location.Z - Ground.ImpactPoint.Z) > 60.f)
		{
			return {};
		}
		return OnNav.Location;
	}

	/** Teleports the squad (Solo while placed): the leader at Lead facing Yaw, the others in pairs behind him; formation again unless bSolo. */
	void PlaceSquad(UWorld* World, USquadSubsystem& Squad, const FVector2D& Lead, float Yaw, bool bSolo = false)
	{
		AOperativeCharacter* Leader = Squad.GetLeader();
		Squad.EnterSoloMode();
		const FRotator Rotation(0.f, Yaw, 0.f);
		const FVector Forward = Rotation.Vector();
		const FVector Right = FRotator(0.f, Yaw + 90.f, 0.f).Vector();
		int32 Follower = 0;
		for (AOperativeCharacter* Each : Squad.GetMembers())
		{
			if (!Each)
			{
				continue;
			}
			Each->LeaveCover(TEXT("smoke reset"));
			Each->SetFormationWallPose(false);
			Each->ClearPendingCover();
			Each->StopOperative();
			Each->SetStance(EOperativeStance::Standing);
			FVector Offset = FVector::ZeroVector;
			if (Each != Leader)
			{
				Offset = (Follower % 2 == 0 ? Right : -Right) * 150.f - Forward * (150.f * static_cast<float>(1 + Follower / 2));
				++Follower;
			}
			const FVector Want(Lead.X + Offset.X, Lead.Y + Offset.Y, 0.f);
			const TOptional<FVector> Ground = GroundNav(World, Want);
			const FVector Spot = SmokeUtils::FreeSpot(World, Ground.Get(Want), Each);
			Each->TeleportTo(Spot + FVector(0.f, 0.f, 4.f), Rotation, false, true);
		}
		Squad.SetLeader(Leader);
		if (!bSolo)
		{
			Squad.ExitSoloMode(false, 0.f);
			Squad.ResetFormationState(Forward);
		}
	}

	EPressureAmbushState StateOf(UCombatPressureSubsystem& Pressure, const AEnemyCharacter* Enemy)
	{
		EPressureAmbushState Out = EPressureAmbushState::Sprung;
		float Held = 0.f;
		return Enemy && Pressure.GetAmbushState(Enemy, Out, Held) ? Out : EPressureAmbushState::Sprung;
	}

	/** A follower (not the leader) to post as the watcher. */
	AOperativeCharacter* PickWatcher(USquadSubsystem& Squad)
	{
		for (AOperativeCharacter* Each : Squad.GetMembers())
		{
			if (Each && Each != Squad.GetLeader())
			{
				return Each;
			}
		}
		return nullptr;
	}

	/** Puts Watcher at Feet (standing) facing Target. */
	void PostWatcher(AOperativeCharacter& Watcher, const FVector& Feet, const FVector& Target)
	{
		Watcher.StopOperative();
		const FVector Facing = (Target - Feet).GetSafeNormal2D();
		Watcher.TeleportTo(Feet + FVector(0.f, 0.f, Watcher.GetSimpleCollisionHalfHeight() + 4.f), Facing.Rotation(), false, true);
		Watcher.SetStance(EOperativeStance::Standing);
		Watcher.SetFacingPoint(Target);
	}

	/**
	 * A ground spot 9-15 m from the runner with a clear sight line from a standing eye to his profile (the watcher's post), or a tower roof
	 * with one (bTower). Unset when none now.
	 */
	TOptional<FVector> FindWatchPost(UWorld* World, const AEnemyCharacter& Runner, bool bTower, FString& OutWhere)
	{
		const UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>();
		if (!Sight)
		{
			return {};
		}
		const FVector Profile = UTacticalSightSubsystem::ProfilePoint(Runner);
		if (bTower)
		{
			for (TActorIterator<ALadderActor> It(World); It; ++It)
			{
				const FVector Roof = It->GetTopLinkWorld() - It->GetOutward() * 100.f;
				const FVector Eye = Roof + FVector(0.f, 0.f, 165.f);
				if (FVector::Dist(Eye, Profile) <= 4500.f && Sight->HasClearSight(Eye, Profile))
				{
					OutWhere = FString::Printf(TEXT("the roof of %s (%.0f cm up)"), *It->GetName(), It->GetTopWorldZ() - It->GetFloorZ());
					return Roof;
				}
			}
			return {};
		}
		const FVector Feet = FeetOf(Runner);
		for (const float Distance : { 1000.f, 1300.f, 900.f, 1500.f })
		{
			for (int32 Step = 0; Step < 12; ++Step)
			{
				const TOptional<FVector> Spot = GroundNav(World, Feet + FVector(Distance, 0.f, 0.f).RotateAngleAxis(Step * 30.f, FVector::UpVector));
				if (Spot.IsSet() && Sight->HasClearSight(Spot.GetValue() + FVector(0.f, 0.f, 160.f), Profile))
				{
					OutWhere = FString::Printf(TEXT("the ground %.0f m from it"), Distance / 100.f);
					return Spot;
				}
			}
		}
		return {};
	}

	bool Step(UWorld* World, FState& State, float Dt)
	{
		State.Time += Dt;
		State.StageTime += Dt;
		if (State.Time > Timeout)
		{
			Check(State, false, FString::Printf(TEXT("timeout in stage %d"), static_cast<int32>(State.Stage)));
			return Finish(State, false);
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		UCombatPressureSubsystem* Pressure = World->GetSubsystem<UCombatPressureSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Squad || !Flow || !Pressure || !Leader)
		{
			Check(State, false, TEXT("squad, game flow, pressure subsystems and a leader"));
			return Finish(State, false);
		}
		const FCombatPressureConfig& Config = Pressure->GetConfig();

		switch (State.Stage)
		{
		case EStage::PlanPlace:
		{
			if (!State.bPlaced)
			{
				State.bPlaced = true;
				Pressure->SetAutoSpawn(false);
				Pressure->DespawnAll();
				Squad->SetSquadPosture(ESquadFirePosture::Passive);
				PlaceSquad(World, *Squad, FVector2D(-1500.0, -2000.0), 90.f);
				return true;
			}
			if (State.StageTime < 1.2f)
			{
				return true;
			}
			// Decision 2: a squad that watches every side (the looks widened to all round) - the plan's limit shortened to 5-6 s for the smoke.
			Pressure->SetWatchHalfAngleOverride(180.f);
			Pressure->SetHoldLimitOverride(5.f, 6.f);
			State.HoldBefore = Pressure->GetSprungByHoldCount();
			const TArray<AEnemyCharacter*> Spawned = Pressure->TriggerCoordinated({ EEnemyArchetype::FrostHound, EEnemyArchetype::Frostbitten,
				EEnemyArchetype::Cutter, EEnemyArchetype::Marksman }, true);
			FPressurePlanInfo Info;
			if (Spawned.Num() < 4 || !Pressure->GetPlanInfo(Pressure->GetLastPlanId(), Info))
			{
				Check(State, false, FString::Printf(TEXT("a coordinated plan round the squad (%d spawned)"), Spawned.Num()));
				return Finish(State, false);
			}
			State.PlanId = Info.Id;
			Check(State, Info.HoldLimitSeconds >= 5.f && Info.HoldLimitSeconds <= 6.f, FString::Printf(TEXT("plan %d drew its hold limit %.2f s (5-6 s here; 60-90 s in the level)"),
				Info.Id, Info.HoldLimitSeconds));
			Enter(State, EStage::PlanHold);
			return true;
		}

		case EStage::PlanHold:
		{
			FPressurePlanInfo Info;
			Pressure->GetPlanInfo(State.PlanId, Info);
			if (Info.Phase != EAmbushPlanPhase::FlankBlock && Info.Phase != EAmbushPlanPhase::Assault && State.StageTime < 15.f)
			{
				return true;
			}
			Check(State, Info.FlankAtClock >= 0.f && Info.HoldSeconds >= Info.HoldLimitSeconds - 0.05f && Info.HoldSeconds <= Info.HoldLimitSeconds + 0.35f
				&& Pressure->GetSprungByHoldCount() == State.HoldBefore + 1,
				FString::Printf(TEXT("watched ALL ROUND, the plan strikes anyway after its hold limit: %s after %.2f s held (limit %.2f s), by the hold limit %d"),
					AmbushPlanRules::PhaseName(Info.Phase), Info.HoldSeconds, Info.HoldLimitSeconds, Pressure->GetSprungByHoldCount() - State.HoldBefore));
			Pressure->SetWatchHalfAngleOverride(-1.f);
			Pressure->SetHoldLimitOverride(-1.f, -1.f);
			Pressure->DespawnAll();
			Enter(State, EStage::PlanSettle);
			return true;
		}

		case EStage::PlanSettle:
		case EStage::WatchSettle:
		case EStage::NoRouteSettle:
		{
			if ((Flow->GetPhase() != ECodexGamePhase::Exploration || State.StageTime < 1.f) && State.StageTime < 25.f)
			{
				return true;
			}
			// 2026-11-10 (Solo without ties): the tower watcher is still on the roof when Solo ends and climbs down to regroup - the next
			// placement must not teleport a man in the middle of a ladder climb (his climb then ends at the tower, a member cut off).
			bool bClimbing = false;
			for (const AOperativeCharacter* Member : Squad->GetMembers())
			{
				bClimbing |= Member && Member->IsClimbingLadder();
			}
			if (bClimbing && State.StageTime < 25.f)
			{
				return true;
			}
			Check(State, Flow->GetPhase() == ECodexGamePhase::Exploration, TEXT("the fight over: exploring"));
			EStage Next = EStage::HoldPlace;
			if (State.Stage == EStage::PlanSettle)
			{
				Next = EStage::WatchPlace;
			}
			else if (State.Stage == EStage::WatchSettle)
			{
				Next = State.WatchCase == 0 ? EStage::WatchPlace : EStage::NoRoutePlace;
				++State.WatchCase;
			}
			Enter(State, Next);
			return true;
		}

		case EStage::WatchPlace:
		case EStage::NoRoutePlace:
		{
			// A single ambush ahead, then the squad turns away and leaves it 52 m behind (bypassed): after relocate.after_sec it searches a route.
			const bool bNoRoute = State.Stage == EStage::NoRoutePlace;
			if (!State.bPlaced)
			{
				State.bPlaced = true;
				Squad->SetSquadPosture(ESquadFirePosture::Passive);
				// The watcher scenarios keep the squad in Solo (its followers hold where they are: one can be posted as the watcher).
				PlaceSquad(World, *Squad, SquadLead, 180.f, !bNoRoute);
				return true;
			}
			if (State.StageTime < 1.2f)
			{
				return true;
			}
			Pressure->SetRouteBlockedOverride(bNoRoute);
			const TOptional<FVector> Hide = GroundNav(World, FVector(AmbushHide.X, AmbushHide.Y, 0.f));
			const TArray<AEnemyCharacter*> Spawned = Hide.IsSet() ? Pressure->SpawnAmbushGroupAt(Hide.GetValue(), { EEnemyArchetype::Frostbitten },
				Hide.GetValue() - FVector(0.f, 500.f, 0.f)) : TArray<AEnemyCharacter*>();
			State.Ambusher = Spawned.IsEmpty() ? nullptr : Spawned[0];
			if (!State.Ambusher.IsValid() || Pressure->IsVisibleToSquad(FeetOf(*State.Ambusher)))
			{
				Check(State, false, TEXT("a single ambush north of W_E_13, behind the squad, out of its sight"));
				return Finish(State, false);
			}
			State.RelocationsBefore = Pressure->GetRelocations().Num();
			State.NoRouteBefore = Pressure->GetNoRouteSearches();
			State.StartFeet = FeetOf(*State.Ambusher);
			State.LastFeet = State.StartFeet;
			State.MaxFrameStepCm = 0.f;
			State.bWatcherPlaced = false;
			Enter(State, bNoRoute ? EStage::NoRouteStay : EStage::WatchWaitRun);
			return true;
		}

		case EStage::WatchWaitRun:
		{
			AEnemyCharacter* Ambusher = State.Ambusher.Get();
			const bool bStarted = Pressure->GetRelocations().Num() > State.RelocationsBefore;
			if (!bStarted && Ambusher && State.StageTime < Config.Relocate.AfterSeconds + 12.f)
			{
				return true;
			}
			if (!bStarted || !Ambusher)
			{
				Check(State, false, FString::Printf(TEXT("case %d: the bypassed ambush sets off along a concealed route (started %d, no-route searches %d)"), State.WatchCase,
					bStarted ? 1 : 0, Pressure->GetNoRouteSearches() - State.NoRouteBefore));
				return Finish(State, false);
			}
			State.RunRecord = Pressure->GetRelocations().Num() - 1;
			State.Watcher = PickWatcher(*Squad);
			Enter(State, EStage::WatchSpot);
			return true;
		}

		case EStage::WatchSpot:
		{
			AEnemyCharacter* Ambusher = State.Ambusher.Get();
			AOperativeCharacter* Watcher = State.Watcher.Get();
			if (!Ambusher || !Watcher || !Pressure->GetRelocations().IsValidIndex(State.RunRecord))
			{
				Check(State, false, TEXT("the runner and a watcher"));
				return Finish(State, false);
			}
			const FPressureRelocationRecord& Run = Pressure->GetRelocations()[State.RunRecord];
			const bool bTower = State.WatchCase == 1;
			if (!State.bWatcherPlaced)
			{
				// Post the watcher where he sees the runner (the tower case: on the first roof with a sight line to him) while the run lasts.
				if (Run.Outcome != EAmbushRunStep::Running)
				{
					Check(State, false, FString::Printf(TEXT("%s watcher: the run ended (%s) before %s saw it"), bTower ? TEXT("tower") : TEXT("ground"),
						AmbushRelocateRules::RunStepName(Run.Outcome), bTower ? TEXT("any tower roof") : TEXT("a ground post")));
					Pressure->DespawnAll();
					Enter(State, EStage::WatchSettle);
					return true;
				}
				const TOptional<FVector> Post = FindWatchPost(World, *Ambusher, bTower, State.WatcherWhere);
				if (!Post.IsSet())
				{
					return true;
				}
				PostWatcher(*Watcher, Post.GetValue(), Ambusher->GetActorLocation());
				State.bWatcherPlaced = true;
				State.PlacedAt = State.StageTime;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: %s posted on %s, %.0f m from the runner"), *Watcher->DisplayName.ToString(), *State.WatcherWhere,
					FVector::Dist(Post.GetValue(), Ambusher->GetActorLocation()) / 100.f);
				return true;
			}
			if (StateOf(*Pressure, Ambusher) != EPressureAmbushState::Sprung && State.StageTime - State.PlacedAt < 3.f)
			{
				Watcher->SetFacingPoint(Ambusher->GetActorLocation());
				return true;
			}
			const float Reaction = State.StageTime - State.PlacedAt;
			Check(State, StateOf(*Pressure, Ambusher) == EPressureAmbushState::Sprung && Run.Outcome == EAmbushRunStep::Spotted && Reaction <= 1.f,
				FString::Printf(TEXT("%s: an operative on %s SPOTS the runner on its route -> it strikes at once (%s, %.2f s after he looked)"),
					bTower ? TEXT("tower") : TEXT("ground"), *State.WatcherWhere, AmbushRelocateRules::RunStepName(Run.Outcome), Reaction));
			Check(State, !Ambusher->IsOnPatrol() && Flow->GetPhase() == ECodexGamePhase::WaveCombat,
				FString::Printf(TEXT("... it engages, the fight starts (%s)"), *UEnum::GetValueAsString(Flow->GetPhase())));
			Pressure->DespawnAll();
			if (Squad->IsSoloMode())
			{
				Squad->ExitSoloMode(false, 0.f);
			}
			Enter(State, EStage::WatchSettle);
			return true;
		}

		case EStage::NoRouteStay:
		{
			AEnemyCharacter* Ambusher = State.Ambusher.Get();
			if (!Ambusher)
			{
				Check(State, false, TEXT("the ambusher is there"));
				return Finish(State, false);
			}
			const FVector Feet = FeetOf(*Ambusher);
			State.MaxFrameStepCm = FMath::Max(State.MaxFrameStepCm, static_cast<float>(FVector::Dist2D(Feet, State.LastFeet)));
			State.LastFeet = Feet;
			const int32 Searches = Pressure->GetNoRouteSearches() - State.NoRouteBefore;
			if (Searches < 1 && State.StageTime < Config.Relocate.AfterSeconds + 12.f)
			{
				return true;
			}
			if (Searches == 1 && State.StageTime < Config.Relocate.AfterSeconds + 2.f)
			{
				return true; // a moment after the first failed search
			}
			int32 Group = 0;
			int32 Plan = 0;
			EAmbushParty Party = EAmbushParty::Single;
			bool bSuppressor = false;
			const bool bFollower = Pressure->GetMemberInfo(Ambusher, Group, Plan, Party, bSuppressor) && Pressure->IsFollowerGroup(Group);
			Check(State, Searches >= 1 && Pressure->GetRelocations().Num() == State.RelocationsBefore && FVector::Dist2D(Feet, State.StartFeet) <= 60.f
				&& (Searches >= Config.Relocate.NoRouteTries || !bFollower) && StateOf(*Pressure, Ambusher) != EPressureAmbushState::Sprung,
				FString::Printf(TEXT("NO concealed route: NO teleport - it stays put (%d search(es), no run, moved %.0f cm)"), Searches,
					FVector::Dist2D(Feet, State.StartFeet)));
			Enter(State, EStage::NoRoutePatrol);
			return true;
		}

		case EStage::NoRoutePatrol:
		{
			AEnemyCharacter* Ambusher = State.Ambusher.Get();
			if (!Ambusher)
			{
				Check(State, false, TEXT("the ambusher is there"));
				return Finish(State, false);
			}
			const FVector Feet = FeetOf(*Ambusher);
			State.MaxFrameStepCm = FMath::Max(State.MaxFrameStepCm, static_cast<float>(FVector::Dist2D(Feet, State.LastFeet)));
			State.LastFeet = Feet;
			int32 Group = 0;
			int32 Plan = 0;
			EAmbushParty Party = EAmbushParty::Single;
			bool bSuppressor = false;
			const bool bFollower = Pressure->GetMemberInfo(Ambusher, Group, Plan, Party, bSuppressor) && Pressure->IsFollowerGroup(Group);
			const float Moved = static_cast<float>(FVector::Dist2D(Feet, State.StartFeet));
			if ((!bFollower || Moved < 400.f) && State.StageTime < Config.Relocate.AfterSeconds + 20.f)
			{
				return true;
			}
			const float BeforeNow = static_cast<float>(FVector::Dist2D(State.StartFeet, FeetOf(*Leader)));
			const float Now = static_cast<float>(FVector::Dist2D(Feet, FeetOf(*Leader)));
			Check(State, bFollower && Pressure->GetFollowerConversions() >= 1 && !Ambusher->bOverridePerception,
				FString::Printf(TEXT("after %d searches without a route it becomes a PATROL following the squad (its own senses back: %d)"),
					Config.Relocate.NoRouteTries, Ambusher->bOverridePerception ? 0 : 1));
			Check(State, Moved >= 400.f && Now < BeforeNow - 300.f && State.MaxFrameStepCm <= 30.f && Pressure->GetRelocations().Num() == State.RelocationsBefore,
				FString::Printf(TEXT("... it walks after the squad on foot: %.0f m closer (moved %.0f m), largest step %.1f cm a frame, no teleport"),
					(BeforeNow - Now) / 100.f, Moved / 100.f, State.MaxFrameStepCm));
			Pressure->SetRouteBlockedOverride(false);
			Pressure->DespawnAll();
			Enter(State, EStage::NoRouteSettle);
			return true;
		}

		case EStage::HoldPlace:
		{
			if (!State.bPlaced)
			{
				State.bPlaced = true;
				PlaceSquad(World, *Squad, FVector2D(3000.0, -600.0), 90.f);
				return true;
			}
			if (State.StageTime < 1.2f)
			{
				return true;
			}
			// A squad that watches every side (the looks widened to all round): the ambush 12 m ahead holds - then, after its own 60-90 s, strikes.
			Pressure->SetWatchHalfAngleOverride(180.f);
			TOptional<FVector> Spot;
			for (const float Turn : { 0.f, 20.f, -20.f, 40.f, -40.f })
			{
				Spot = GroundNav(World, FeetOf(*Leader) + FRotator(0.f, 90.f + Turn, 0.f).Vector() * 1200.f);
				if (Spot.IsSet())
				{
					break;
				}
			}
			const TArray<AEnemyCharacter*> Group = Spot.IsSet() ? Pressure->SpawnAmbushGroupAt(Spot.GetValue(), { EEnemyArchetype::Frostbitten }, FeetOf(*Leader))
				: TArray<AEnemyCharacter*>();
			State.Ambusher = Group.IsEmpty() ? nullptr : Group[0];
			if (!State.Ambusher.IsValid())
			{
				Check(State, false, TEXT("an ambusher 12 m in front of the squad"));
				return Finish(State, false);
			}
			State.HoldLimit = Pressure->GetHoldLimit(State.Ambusher.Get());
			State.HoldBefore = Pressure->GetSprungByHoldCount();
			Check(State, State.HoldLimit >= 60.f && State.HoldLimit <= 90.f, FString::Printf(TEXT("its hold limit %.1f s (the level's 60-90 s)"), State.HoldLimit));
			Enter(State, EStage::HoldWait);
			return true;
		}

		case EStage::HoldWait:
		{
			AEnemyCharacter* Ambusher = State.Ambusher.Get();
			EPressureAmbushState AmbushState = EPressureAmbushState::Sprung;
			float Held = 0.f;
			const bool bKnown = Ambusher && Pressure->GetAmbushState(Ambusher, AmbushState, Held);
			if (bKnown && AmbushState != EPressureAmbushState::Sprung && State.StageTime < 100.f)
			{
				if (State.StageTime >= 30.f && State.StageTime < 30.f + Dt)
				{
					Check(State, AmbushState == EPressureAmbushState::Holding && Flow->GetPhase() == ECodexGamePhase::Exploration,
						FString::Printf(TEXT("watched all round it HOLDS (30 s in, held %.1f s, no fight)"), Held));
				}
				return true;
			}
			Check(State, bKnown && AmbushState == EPressureAmbushState::Sprung && Pressure->GetSprungByHoldCount() == State.HoldBefore + 1
				&& State.StageTime >= State.HoldLimit - 0.3f && State.StageTime <= State.HoldLimit + 0.6f,
				FString::Printf(TEXT("a squad that watches every side still gets attacked: it strikes after %.1f s held (its limit %.1f s, by the hold limit %d)"), State.StageTime,
					State.HoldLimit, Pressure->GetSprungByHoldCount() - State.HoldBefore));
			Pressure->SetWatchHalfAngleOverride(-1.f);
			Pressure->DespawnAll();
			return Finish(State, true);
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
		TEXT("CodexTactics.CombatLabAmbushRelocateSmoke"),
		TEXT("Dev check (combat lab, game designer decisions 2026-11-01 night, L_FormationLabCombat): a plan watched all round strikes after its hold limit; ")
		TEXT("a relocation run spotted by a ground / a tower watcher strikes at once; no concealed route -> no teleport, it stays, then follows as a patrol; ")
		TEXT("a single ambush watched all along strikes after its 60-90 s. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
