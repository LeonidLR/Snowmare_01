// UPlaytestBotSubsystem's ROUTE mode (Training Ground "Combat lab" part C, game designer 2026-11-01, docs/design/COMBAT_LAB.md section 7;
// UE-only, no Godot reference): on an endless pressure level the bot leads the squad in formation along the level's planned route
// ("bot_route", FBotRouteTracker), fights what it meets, stops by time and writes the run report (FCombatLabRunCollector).
// Every order goes through the player's own click path (ACodexTacticsPlayerController::HandleWorldHit), so the squad moves exactly as
// it does for the player: a ground click = the formation move, a click on a wall face = the group cover / formation wall layout, a click
// on a tower roof = a walk up its ladder (Solo, the leader alone).
#include "Bot/PlaytestBotSubsystem.h"

#include "AI/AmbushWatchRules.h"
#include "Bot/BotRouteRules.h"
#include "Bot/CombatLabRunCollector.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/CombatCheats.h"
#include "Combat/CombatPressureSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Core/CodexTacticsGameMode.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Data/WaveConfigTypes.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/PlatformTime.h"
#include "Ladders/LadderActor.h"
#include "Ladders/LadderClimbComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

namespace BotRouteModeLocal
{
	/** World samples for the report this often, s. */
	constexpr float SampleInterval = 0.1f;
	/** The live snapshot (<report>.live.json) this often, s. */
	constexpr float LiveInterval = 5.f;
	/** Clicks at most this often (two clicks within the double-click time would be a run order), s. */
	constexpr float ClickInterval = 1.f;
	/** A group is "seen" (a contact) when an operative has a line of sight to a member within this, cm. */
	constexpr float ContactRangeCm = 3000.f;
	/** The ladder of a climb waypoint stands within this of it, cm. */
	constexpr float LadderSearchCm = 1500.f;
	/** The wall layout counts as settled after this long even when a follower is still shuffling, s. */
	constexpr float WallSettleLimit = 10.f;
	/** On the roof / back on the ground: feet within this of the level, cm. */
	constexpr float LevelToleranceCm = 30.f;

	FVector2D To2D(const FVector& Point)
	{
		return FVector2D(Point.X, Point.Y);
	}

	float RouteFeetZ(const ACharacter& Character)
	{
		return static_cast<float>(Character.GetActorLocation().Z - Character.GetSimpleCollisionHalfHeight());
	}

	bool IsLiving(const AActor* Actor)
	{
		const UHealthComponent* Health = Actor ? Actor->FindComponentByClass<UHealthComponent>() : nullptr;
		return Health && Health->IsAlive();
	}

	bool IsResting(const AOperativeCharacter& Leader)
	{
		const ULadderClimbComponent* Climb = Leader.LadderClimb.Get();
		return (!Climb || !Climb->IsOnLadder()) && !Leader.IsMoving();
	}
}

bool UPlaytestBotSubsystem::TrySetupRouteMode()
{
	using namespace BotRouteModeLocal;
	UWorld* World = GetWorld();
	const ACodexTacticsGameMode* GameMode = World ? World->GetAuthGameMode<ACodexTacticsGameMode>() : nullptr;
	const ULevelConfigAsset* Level = GameMode ? GameMode->GetActiveLevelConfig() : nullptr;
	int32 RouteSwitch = -1;
	FParse::Value(FCommandLine::Get(), TEXT("BotRoute="), RouteSwitch);
	const bool bHasRoute = Level && Level->Config.BotRoute.IsValid();
	const bool bEndless = Level && Level->Config.Pressure.bEnabled;
	if (RouteSwitch == 0 || !bHasRoute || (RouteSwitch < 0 && !bEndless))
	{
		if (RouteSwitch > 0)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[Bot] -BotRoute=1 but the level JSON has no valid \"bot_route\": the usual bot"));
		}
		return false;
	}
	USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	if (!Squad || !Leader)
	{
		return false;
	}
	bRouteMode = true;
	bStealthLevel = false;
	Stage = EBotStage::Route;
	FParse::Value(FCommandLine::Get(), TEXT("BotDuration="), RouteDurationSeconds);
	RouteDurationSeconds = FMath::Max(10.f, RouteDurationSeconds);
	FParse::Value(FCommandLine::Get(), TEXT("CombatLabReport="), CombatLabReportFile);
	RoutePressureSeed = Level->Config.Pressure.Seed;
	FParse::Value(FCommandLine::Get(), TEXT("PressureSeed="), RoutePressureSeed);
	if (Squad->IsSoloMode())
	{
		Squad->ExitSoloMode();
	}
	// The squad fights with its automatic fire (as the wave bot); the route is walked in formation.
	Squad->SetSquadPosture(ESquadFirePosture::Aggressive);
	RouteStartTime = World->GetTimeSeconds();
	RouteTracker.Reset(Level->Config.BotRoute, 0.f, To2D(Leader->GetActorLocation()));
	LabCollector.Begin(0.f);
	LabSampleTimer = 0.f;
	LabLiveTimer = 0.f;
	RouteClickCooldown = 0.f;
	RouteLoggedStucks = 0;
	RouteLoggedIndex = 0;
	const FBotRouteConfig& Route = Level->Config.BotRoute;
	UE_LOG(LogCodexTactics, Display, TEXT("[Bot] Route mode: route '%s' (%d waypoints, lap %.0f m, loop %d), %.0f s by time, pressure seed %d, immortal %d, infinite ammo %d, report %s"),
		*Route.Name, Route.Points.Num(), Route.LapLengthCm() / 100.f, Route.bLoop ? 1 : 0, RouteDurationSeconds, RoutePressureSeed,
		CombatCheats::IsImmortal(World) ? 1 : 0, CombatCheats::IsInfiniteAmmo(World) ? 1 : 0, CombatLabReportFile.IsEmpty() ? TEXT("(none)") : *CombatLabReportFile);
	WriteCombatLabLive();
	return true;
}

FBotRouteInput UPlaytestBotSubsystem::RouteInput() const
{
	FBotRouteInput In;
	In.Time = static_cast<float>(GetWorld()->GetTimeSeconds() - RouteStartTime);
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	const AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	if (Leader)
	{
		In.Leader = BotRouteModeLocal::To2D(Leader->GetActorLocation());
	}
	const ULadderClimbComponent* Climb = Leader ? Leader->LadderClimb.Get() : nullptr;
	In.bPaused = bRouteFightHold || RoutePhase != ERouteActionPhase::None || !Leader || Leader->IsKnockedDown() || (Climb && Climb->IsOnLadder());
	return In;
}

void UPlaytestBotSubsystem::TickRoute(float DeltaTime)
{
	using namespace BotRouteModeLocal;
	UWorld* World = GetWorld();
	UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
	USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
	if (!Flow || !Squad)
	{
		return;
	}
	if (Flow->GetPhase() == ECodexGamePhase::GameOver)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Bot] Route: >>> the squad is down <<<"));
		Finish(TEXT("DEFEAT"), 0);
		return;
	}
	if (Flow->GetPhase() == ECodexGamePhase::Cutscene)
	{
		Flow->FinishCutscene();
	}
	const float Now = static_cast<float>(World->GetTimeSeconds() - RouteStartTime);
	if (Now >= RouteDurationSeconds)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Bot] Route: time is up (%.0f s): laps %d, progress %.0f %%, contacts %d"), RouteDurationSeconds,
			RouteTracker.GetLaps().Num(), RouteTracker.GetProgress() * 100.f, LabCollector.GetContacts().Num());
		Finish(TEXT("COMPLETE"), 0);
		return;
	}
	RouteClickCooldown -= DeltaTime;
	LabSampleTimer += DeltaTime;
	if (LabSampleTimer >= SampleInterval)
	{
		LabSampleTimer = 0.f;
		LabCollector.AddSample(SampleCombatLab());
	}
	LabLiveTimer += DeltaTime;
	if (LabLiveTimer >= LiveInterval)
	{
		LabLiveTimer = 0.f;
		WriteCombatLabLive();
	}
	AOperativeCharacter* Leader = Squad->GetLeader();
	if (!Leader || !IsLiving(Leader))
	{
		return;
	}

	// A fight (the skirmish) holds the route a while: the squad halts crouched and fights; then it walks on, fighting.
	const bool bInFight = Flow->GetPhase() == ECodexGamePhase::WaveCombat;
	const FBotRouteConfig& Route = RouteTracker.GetConfig();
	if (bInFight && !bRouteWasInFight)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Bot] Route: fight at %.1f s on the way to #%d (%s), formation %s"), Now, RouteTracker.GetIndex(),
			RouteTracker.GetWaypoint() ? *RouteTracker.GetWaypoint()->Label : TEXT("?"), *LabCollector.GetFormation());
		if (Route.FightHoldSeconds > 0.f && RoutePhase == ERouteActionPhase::None)
		{
			bRouteFightHold = true;
			RouteFightHoldStart = Now;
			Leader->OrderMoveTo(Leader->GetActorLocation(), false); // halt where he stands
			Squad->SetSquadStance(EOperativeStance::Crouching);
			RouteTracker.NotifyOrder(TEXT("halt: fight (crouched)"));
		}
	}
	if (bRouteFightHold && (!bInFight || Now - RouteFightHoldStart >= Route.FightHoldSeconds))
	{
		bRouteFightHold = false;
		Squad->SetSquadStance(EOperativeStance::Standing);
		bRouteReorder = true;
		UE_LOG(LogCodexTactics, Display, TEXT("[Bot] Route: walking on at %.1f s (%s)"), Now, bInFight ? TEXT("still fighting") : TEXT("the fight is over"));
	}
	bRouteWasInFight = bInFight;

	if (RoutePhase != ERouteActionPhase::None)
	{
		TickRouteAction(DeltaTime);
	}
	const EBotRouteCommand Command = RouteTracker.Tick(RouteInput());
	if (Command != EBotRouteCommand::None)
	{
		HandleRouteCommand(Command);
	}
	else if (bRouteReorder && !bRouteFightHold && RoutePhase == ERouteActionPhase::None && !RouteTracker.IsInAction() && RouteClickCooldown <= 0.f)
	{
		bRouteReorder = false;
		IssueRouteMove(TEXT("resume"));
	}
}

void UPlaytestBotSubsystem::HandleRouteCommand(EBotRouteCommand Command)
{
	const float Now = static_cast<float>(GetWorld()->GetTimeSeconds() - RouteStartTime);
	// The log: every new stuck event and every waypoint left behind.
	const TArray<FBotRouteStuck>& Stucks = RouteTracker.GetStucks();
	for (; RouteLoggedStucks < Stucks.Num(); ++RouteLoggedStucks)
	{
		const FBotRouteStuck& Stuck = Stucks[RouteLoggedStucks];
		UE_LOG(LogCodexTactics, Display, TEXT("[Bot] Route: STUCK at %.1f s on #%d (%s) at (%.0f, %.0f): %s, last order '%s'%s"), Stuck.Time, Stuck.Waypoint,
			*Stuck.Label, Stuck.Position.X, Stuck.Position.Y, *Stuck.Reason, *Stuck.LastOrder, Stuck.bSkipped ? TEXT(" -> waypoint skipped") : TEXT(" -> order again"));
	}
	if (RouteTracker.GetIndex() != RouteLoggedIndex)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Bot] Route: #%d done at %.1f s (reached %d, skipped %d, laps %d), next #%d (%s)"), RouteLoggedIndex, Now,
			RouteTracker.GetTotalReached(), RouteTracker.GetTotalSkipped(), RouteTracker.GetLaps().Num(), RouteTracker.GetIndex(),
			RouteTracker.GetWaypoint() ? *RouteTracker.GetWaypoint()->Label : TEXT("?"));
		RouteLoggedIndex = RouteTracker.GetIndex();
	}
	switch (Command)
	{
	case EBotRouteCommand::Move:
		IssueRouteMove(TEXT("move"));
		return;
	case EBotRouteCommand::Retry:
		IssueRouteMove(TEXT("retry"));
		return;
	case EBotRouteCommand::StartAction:
		BeginRouteAction();
		return;
	case EBotRouteCommand::Finished:
		UE_LOG(LogCodexTactics, Display, TEXT("[Bot] Route: the last waypoint is done (no loop)"));
		Finish(TEXT("COMPLETE"), 0);
		return;
	default:
		return;
	}
}

bool UPlaytestBotSubsystem::RouteClick(const FVector& Start, const FVector& End, const TCHAR* What)
{
	UWorld* World = GetWorld();
	ACodexTacticsPlayerController* Controller = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController());
	if (!Controller)
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BotRouteClick), false);
	if (const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>())
	{
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Params.AddIgnoredActor(Member); // a click through a body is not meant to select him
		}
	}
	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		return false;
	}
	Controller->HandleWorldHit(Hit);
	UE_LOG(LogCodexTactics, Verbose, TEXT("[Bot] Route click (%s) on %s at %s"), What, *GetNameSafe(Hit.GetActor()), *Hit.ImpactPoint.ToCompactString());
	return true;
}

void UPlaytestBotSubsystem::IssueRouteMove(const TCHAR* Why)
{
	using namespace BotRouteModeLocal;
	const FBotRouteWaypoint* Waypoint = RouteTracker.GetWaypoint();
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	const AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	if (!Waypoint || !Leader)
	{
		return;
	}
	if (RouteClickCooldown > 0.f)
	{
		bRouteReorder = true; // issued once the click interval has passed
		return;
	}
	RouteClickCooldown = ClickInterval;
	const FVector Target(Waypoint->At.X, Waypoint->At.Y, RouteFeetZ(*Leader));
	if (!RouteClick(Target + FVector(0.f, 0.f, 400.f), Target - FVector(0.f, 0.f, 600.f), TEXT("move")))
	{
		HandleRouteCommand(RouteTracker.NotifyOrderFailed(RouteInput(), TEXT("no_ground_hit")));
		return;
	}
	bRouteReorder = false;
	RouteTracker.NotifyOrder(FString::Printf(TEXT("%s: move to #%d %s (%.0f, %.0f)"), Why, RouteTracker.GetIndex(), *Waypoint->Label, Waypoint->At.X, Waypoint->At.Y));
}

void UPlaytestBotSubsystem::BeginRouteAction()
{
	using namespace BotRouteModeLocal;
	UWorld* World = GetWorld();
	const FBotRouteWaypoint* Waypoint = RouteTracker.GetWaypoint();
	USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	if (!Waypoint || !Leader)
	{
		EndRouteAction(false, TEXT("no_leader"));
		return;
	}
	RoutePhaseTime = 0.f;
	RouteClickCooldown = ClickInterval;
	if (Waypoint->Action == EBotRouteAction::Wall)
	{
		// A click on the wall's face at knee height (the group cover order -> the formation wall layout), the ghost confirmed at once.
		const FVector From = Leader->GetActorLocation();
		const FVector2D Direction = (Waypoint->WallPoint - To2D(From)).GetSafeNormal();
		const float Z = RouteFeetZ(*Leader) + 100.f;
		const FVector Start(From.X, From.Y, Z);
		const FVector End(Waypoint->WallPoint.X + Direction.X * 150.f, Waypoint->WallPoint.Y + Direction.Y * 150.f, Z);
		RoutePhase = ERouteActionPhase::WallWait;
		if (!RouteClick(Start, End, TEXT("wall")))
		{
			EndRouteAction(false, TEXT("wall_click_missed"));
			return;
		}
		ACodexTacticsPlayerController* Controller = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController());
		if (Controller && Controller->HasCoverPreview())
		{
			Controller->ConfirmCoverPreview(false);
		}
		RouteTracker.NotifyOrder(FString::Printf(TEXT("wall: cover at (%.0f, %.0f)"), Waypoint->WallPoint.X, Waypoint->WallPoint.Y));
		UE_LOG(LogCodexTactics, Display, TEXT("[Bot] Route: #%d %s - the squad to the wall"), RouteTracker.GetIndex(), *Waypoint->Label);
		return;
	}
	// Climb: the nearest climbable ladder; the leader alone walks to its roof (the click on the roof = a move up its ladder).
	ALadderActor* Best = nullptr;
	float BestDistance = LadderSearchCm;
	for (TActorIterator<ALadderActor> It(World); It; ++It)
	{
		const float Distance = static_cast<float>(FVector2D::Distance(To2D(It->GetActorLocation()), Waypoint->At));
		if (It->IsClimbable() && Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = *It;
		}
	}
	RoutePhase = ERouteActionPhase::ClimbUp;
	if (!Best)
	{
		EndRouteAction(false, TEXT("no_ladder"));
		return;
	}
	RouteLadder = Best;
	if (!Squad->IsSoloMode())
	{
		Squad->EnterSoloMode();
	}
	const FVector Roof = Best->GetTopLinkWorld() - Best->GetOutward() * 100.f;
	if (!RouteClick(Roof + FVector(0.f, 0.f, 300.f), Roof - FVector(0.f, 0.f, 300.f), TEXT("roof")))
	{
		EndRouteAction(false, TEXT("roof_click_missed"));
		return;
	}
	RouteTracker.NotifyOrder(FString::Printf(TEXT("climb: up %s"), *Best->GetName()));
	UE_LOG(LogCodexTactics, Display, TEXT("[Bot] Route: #%d %s - the leader climbs %s"), RouteTracker.GetIndex(), *Waypoint->Label, *Best->GetName());
}

void UPlaytestBotSubsystem::TickRouteAction(float DeltaTime)
{
	using namespace BotRouteModeLocal;
	RoutePhaseTime += DeltaTime;
	USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	const FBotRouteWaypoint* Waypoint = RouteTracker.GetWaypoint();
	const float Timeout = RouteTracker.GetConfig().ActionTimeoutSeconds;
	if (!Leader || !Waypoint)
	{
		EndRouteAction(false, TEXT("no_leader"));
		return;
	}
	ALadderActor* Ladder = RouteLadder.Get();
	switch (RoutePhase)
	{
	case ERouteActionPhase::WallWait:
		if (Squad->IsSquadAtWall() && (Squad->AreFormationWallMembersSettled() || RoutePhaseTime >= WallSettleLimit))
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[Bot] Route: at the wall after %.1f s (settled %d)"), RoutePhaseTime, Squad->AreFormationWallMembersSettled() ? 1 : 0);
			RoutePhase = ERouteActionPhase::WallHold;
			RoutePhaseTime = 0.f;
		}
		else if (RoutePhaseTime >= Timeout)
		{
			EndRouteAction(false, TEXT("wall_not_reached"));
		}
		return;
	case ERouteActionPhase::WallHold:
		if (RoutePhaseTime >= Waypoint->HoldSeconds)
		{
			EndRouteAction(true, FString());
		}
		return;
	case ERouteActionPhase::ClimbUp:
		if (Ladder && FMath::Abs(RouteFeetZ(*Leader) - Ladder->GetTopWorldZ()) < LevelToleranceCm && IsResting(*Leader))
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[Bot] Route: on the roof after %.1f s"), RoutePhaseTime);
			RoutePhase = ERouteActionPhase::ClimbHold;
			RoutePhaseTime = 0.f;
		}
		else if (!Ladder || RoutePhaseTime >= Timeout)
		{
			EndRouteAction(false, TEXT("climb_up_timeout"));
		}
		return;
	case ERouteActionPhase::ClimbHold:
		if (RoutePhaseTime >= Waypoint->HoldSeconds && Ladder && RouteClickCooldown <= 0.f)
		{
			RouteClickCooldown = ClickInterval;
			const FVector Ground = Ladder->GetBottomLinkWorld() + Ladder->GetOutward() * 450.f;
			if (!RouteClick(Ground + FVector(0.f, 0.f, 300.f), Ground - FVector(0.f, 0.f, 300.f), TEXT("down")))
			{
				EndRouteAction(false, TEXT("ground_click_missed"));
				return;
			}
			RouteTracker.NotifyOrder(FString::Printf(TEXT("climb: down %s"), *Ladder->GetName()));
			RoutePhase = ERouteActionPhase::ClimbDown;
			RoutePhaseTime = 0.f;
		}
		return;
	case ERouteActionPhase::ClimbDown:
		if (Ladder && FMath::Abs(RouteFeetZ(*Leader) - Ladder->GetFloorZ()) < LevelToleranceCm && IsResting(*Leader))
		{
			EndRouteAction(true, FString());
		}
		else if (!Ladder || RoutePhaseTime >= Timeout)
		{
			EndRouteAction(false, TEXT("climb_down_timeout"));
		}
		return;
	default:
		return;
	}
}

void UPlaytestBotSubsystem::EndRouteAction(bool bOk, const FString& Why)
{
	if (USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>(); Squad && Squad->IsSoloMode())
	{
		Squad->ExitSoloMode();
	}
	const FBotRouteWaypoint* Waypoint = RouteTracker.GetWaypoint();
	UE_LOG(LogCodexTactics, Display, TEXT("[Bot] Route: #%d %s %s %s"), RouteTracker.GetIndex(), Waypoint ? *Waypoint->Label : TEXT("?"),
		Waypoint ? BotRouteRules::ActionName(Waypoint->Action) : TEXT("?"), bOk ? TEXT("done") : *FString::Printf(TEXT("FAILED (%s)"), *Why));
	RoutePhase = ERouteActionPhase::None;
	RoutePhaseTime = 0.f;
	RouteLadder.Reset();
	RouteClickCooldown = 0.f;
	HandleRouteCommand(RouteTracker.FinishAction(RouteInput(), bOk, Why));
}

FCombatLabSample UPlaytestBotSubsystem::SampleCombatLab() const
{
	using namespace BotRouteModeLocal;
	const UWorld* World = GetWorld();
	FCombatLabSample Sample;
	Sample.Time = static_cast<float>(World->GetTimeSeconds() - RouteStartTime);
	const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
	const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
	const UCombatPressureSubsystem* Pressure = World->GetSubsystem<UCombatPressureSubsystem>();
	Sample.bInFight = Flow && Flow->GetPhase() == ECodexGamePhase::WaveCombat;
	if (Squad)
	{
		Sample.bAtWall = Squad->IsSquadAtWall();
		Sample.Formation = CombatLabRules::FormationLabel(Squad->IsSoloMode(), Squad->IsFormationSectorsActive(), Squad->HasFormationWallOrder(),
			Squad->GetFormationKind(), Squad->GetFormationMemberCount());
		for (const AOperativeCharacter* Member : Squad->GetMembers())
		{
			if (!Member)
			{
				continue;
			}
			FCombatLabMemberSample Each;
			Each.Name = Member->DisplayName.IsEmpty() ? Member->GetName() : Member->DisplayName.ToString();
			const UHealthComponent* Health = Member->HealthComponent.Get();
			Each.bAlive = Health && Health->IsAlive();
			Each.Stance = Member->GetStance();
			Each.bKnockedDown = Member->IsKnockedDown();
			Each.Health = Health ? Health->GetCurrentHealth() : 0.f;
			Each.AbsorbedDamage = Health ? Health->GetCheatBlockedDamage() : 0.f;
			Each.RoundsSpent = Member->GetRoundsSpent();
			if (const ULadderClimbComponent* Climb = Member->LadderClimb.Get())
			{
				Each.ClimbsUp = Climb->GetClimbsUp();
				Each.ClimbsDown = Climb->GetClimbsDown();
			}
			Sample.Members.Add(MoveTemp(Each));
		}
	}
	if (Pressure)
	{
		const TArray<FAmbushWatcher> Watchers = Pressure->GatherWatchers();
		const TArray<FVector2D> SquadPositions = Pressure->GatherSquadPositions();
		const FAmbushWatchParams& Watch = Pressure->GetConfig().Watch;
		TMap<int32, FCombatLabGroupSample> Groups;
		for (const FPressureSpawnRecord& Record : Pressure->GetSpawnRecords())
		{
			const AEnemyCharacter* Enemy = Record.Enemy.Get();
			if (!Enemy || Enemy->IsDying() || !IsLiving(Enemy))
			{
				continue;
			}
			const int32 Id = Record.Plan > 0 ? 100000 + Record.Plan : Record.Group;
			FCombatLabGroupSample& Group = Groups.FindOrAdd(Id);
			const FVector2D Here = To2D(Enemy->GetActorLocation());
			if (Group.Alive == 0)
			{
				Group.Id = Id;
				Group.Kind = Record.Plan > 0 || Record.Kind == EPressureSpawnKind::Coordinated ? ECombatLabContactKind::Coordinated
					: (Record.Kind == EPressureSpawnKind::Patrol ? ECombatLabContactKind::Patrol : ECombatLabContactKind::Ambush);
				Group.Position = Here;
			}
			++Group.Alive;
			Group.bEngaged |= !Enemy->IsOnPatrol();
			EPressureAmbushState State = EPressureAmbushState::Hidden;
			float HoldSeconds = 0.f;
			Group.bSprung |= Pressure->GetAmbushState(Enemy, State, HoldSeconds) && State == EPressureAmbushState::Sprung;
			Group.bWatched |= AmbushWatchRules::IsWatched(Watchers, Here, Watch);
			if (!Group.bSeen && !LabCollector.HasContact(Id) && AmbushWatchRules::NearestDistance(SquadPositions, Here) <= ContactRangeCm)
			{
				Group.bSeen = Pressure->IsVisibleToSquad(Enemy->GetActorLocation() - FVector(0.f, 0.f, Enemy->GetSimpleCollisionHalfHeight()));
			}
		}
		Groups.GenerateValueArray(Sample.Groups);
	}
	return Sample;
}

void UPlaytestBotSubsystem::WriteCombatLabLive() const
{
	if (CombatLabReportFile.IsEmpty())
	{
		return;
	}
	const FBotRouteWaypoint* Waypoint = RouteTracker.GetWaypoint();
	TSharedRef<FJsonObject> Live = MakeShared<FJsonObject>();
	Live->SetNumberField(TEXT("run"), Seed);
	Live->SetNumberField(TEXT("bot_seed"), Seed);
	Live->SetNumberField(TEXT("pressure_seed"), RoutePressureSeed);
	Live->SetNumberField(TEXT("time_sec"), FMath::RoundToDouble(GetWorld()->GetTimeSeconds() - RouteStartTime));
	Live->SetNumberField(TEXT("planned_sec"), RouteDurationSeconds);
	Live->SetNumberField(TEXT("real_sec"), FMath::RoundToDouble(FPlatformTime::Seconds() - StartRealTime));
	Live->SetNumberField(TEXT("waypoint"), RouteTracker.GetIndex());
	Live->SetStringField(TEXT("label"), Waypoint ? Waypoint->Label : FString());
	Live->SetNumberField(TEXT("laps"), RouteTracker.GetLaps().Num());
	Live->SetNumberField(TEXT("progress"), FMath::RoundToDouble(RouteTracker.GetProgress() * 100.0) / 100.0);
	Live->SetNumberField(TEXT("stuck"), RouteTracker.GetStucks().Num());
	Live->SetNumberField(TEXT("contacts"), LabCollector.GetContacts().Num());
	Live->SetBoolField(TEXT("fight"), bRouteWasInFight);
	Live->SetStringField(TEXT("formation"), LabCollector.GetFormation());
	Live->SetStringField(TEXT("last_order"), RouteTracker.GetLastOrder());
	FFileHelper::SaveStringToFile(CombatLabRules::ToJsonLine(Live), *(FPaths::ChangeExtension(CombatLabReportFile, TEXT("")) + TEXT(".live.json")),
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void UPlaytestBotSubsystem::WriteCombatLabReport(const TCHAR* Result) const
{
	const UWorld* World = GetWorld();
	FCombatLabRunCollector Final = LabCollector;
	Final.End(static_cast<float>(World->GetTimeSeconds() - RouteStartTime));
	FCombatLabRunMeta Meta;
	Meta.Result = Result;
	Meta.Run = Seed;
	Meta.BotSeed = Seed;
	Meta.PressureSeed = RoutePressureSeed;
	const ACodexTacticsGameMode* GameMode = World->GetAuthGameMode<ACodexTacticsGameMode>();
	const ULevelConfigAsset* Level = GameMode ? GameMode->GetActiveLevelConfig() : nullptr;
	Meta.LevelId = Level ? Level->Config.LevelId : FString();
	Meta.Map = World->GetMapName();
	Meta.TimestampUtc = FDateTime::UtcNow().ToIso8601();
	Meta.PlannedSeconds = RouteDurationSeconds;
	Meta.RealSeconds = static_cast<float>(FPlatformTime::Seconds() - StartRealTime);
	Meta.bImmortal = CombatCheats::IsImmortal(World);
	Meta.bInfiniteAmmo = CombatCheats::IsInfiniteAmmo(World);
	if (const UCombatPressureSubsystem* Pressure = World->GetSubsystem<UCombatPressureSubsystem>())
	{
		Meta.PressureEvents = Pressure->GetEventsFired();
		Meta.PressureSpawned = Pressure->GetSpawnRecords().Num();
		Meta.PressureSprungByWatch = Pressure->GetSprungByWatchCount();
		Meta.PressureRelocations = Pressure->GetRelocations().Num();
	}
	const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
	const AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	const FVector2D LeaderNow = Leader ? BotRouteModeLocal::To2D(Leader->GetActorLocation()) : FVector2D::ZeroVector;
	const FString Line = CombatLabRules::ToJsonLine(CombatLabRules::MakeRunRecord(Meta, RouteTracker, Final, LeaderNow));
	UE_LOG(LogCodexTactics, Display, TEXT("[Bot] Route report: %s, laps %d, route completed %d (progress %.0f %%, skipped %d, stuck %d), contacts %d (patrol %d, ambush %d, coordinated %d, unwatched strikes %d), formation changes %d, wall hugs %d, stance changes %d, climbs %d, knockdowns %d, rounds %d, damage %.0f / absorbed %.0f"),
		Result, RouteTracker.GetLaps().Num(), RouteTracker.IsRouteCompleted() ? 1 : 0, RouteTracker.GetProgress() * 100.f, RouteTracker.GetTotalSkipped(),
		RouteTracker.GetStucks().Num(), Final.GetContacts().Num(), Final.CountContacts(ECombatLabContactKind::Patrol), Final.CountContacts(ECombatLabContactKind::Ambush),
		Final.CountContacts(ECombatLabContactKind::Coordinated), Final.CountUnwatchedStrikes(), Final.GetFormationChanges(), Final.GetWallHugs(),
		Final.GetStanceChanges(), Final.GetClimbsUp(), Final.GetKnockdowns(), Final.GetRounds(), Final.GetDamageTaken(), Final.GetDamageAbsorbed());
	if (!CombatLabReportFile.IsEmpty())
	{
		if (!FFileHelper::SaveStringToFile(Line + TEXT("\n"), *CombatLabReportFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[Bot] Route report: cannot write %s"), *CombatLabReportFile);
		}
	}
}
