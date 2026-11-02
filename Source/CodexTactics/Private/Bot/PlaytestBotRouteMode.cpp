// UPlaytestBotSubsystem's ROUTE mode (Training Ground "Combat lab" part C, game designer 2026-11-01, docs/design/COMBAT_LAB.md section 7;
// UE-only, no Godot reference): on an endless pressure level the bot leads the squad in formation along the level's planned route
// ("bot_route", FBotRouteTracker), fights what it meets, stops by time and writes the run report (FCombatLabRunCollector).
// Every order goes through the player's own click path (ACodexTacticsPlayerController::HandleWorldHit), so the squad moves exactly as
// it does for the player: a ground click = the formation move, a click on a wall face = the group cover / formation wall layout, a click
// on a tower roof = a walk up its ladder (Solo, the leader alone). In a fight the squad first takes COVER AT A WALL facing the threat (the same
// wall click; BotCoverRules - game designer decision 5, 2026-11-01 night).
// Generated maps (2026-11-02, docs/design/levelgen/LEVEL_GENERATOR.md section 8): a waypoint's "z" puts the click just above its own
// level (a floor under a roof, the roof above a floor) and is checked on arrival; a "drop" leg goes down through a roof / floor hole by
// the builder's drop nav link (the leader walks off the rim and falls) - recorded per drop (fell / scripted), and a drop leg that gets
// stuck is SCRIPTED: the bot puts the squad down at the waypoint itself and the record says so. Codex.Bot.Route starts the mode from the
// console (LevelGenRouteSmoke, the clip recorder).
#include "Bot/PlaytestBotSubsystem.h"

#include "AI/AmbushWatchRules.h"
#include "Bot/BotCoverRules.h"
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
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
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
	/** A waypoint with a "z": the click trace runs from this far above its level down to this far below it, cm (under the next level). */
	constexpr float LevelClickAboveCm = 150.f;
	constexpr float LevelClickBelowCm = 200.f;
	/** A scripted drop puts the members on a ring this wide round the waypoint, cm. */
	constexpr float DropRingCm = 90.f;

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
	if (RouteForce >= 0)
	{
		RouteSwitch = RouteForce;
	}
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
	if (RouteDurationOverride > 0.f)
	{
		RouteDurationSeconds = RouteDurationOverride;
	}
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
	RouteDrops.Reset();
	RouteDropActive = INDEX_NONE;
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
		In.LeaderFeetZ = BotRouteModeLocal::RouteFeetZ(*Leader);
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

	// A fight (the skirmish) holds the route a while (decision 5, 2026-11-01 night): the squad takes cover at the nearest wall facing the threat
	// and fights from there (no wall: it halts where it stands - the autonomy's ROE crouches it only when the halt lasts); then it walks on:
	// the fight is over, or the hold time is up (it walks on fighting, the Aggressive posture fires on the move).
	const bool bInFight = Flow->GetPhase() == ECodexGamePhase::WaveCombat;
	const FBotRouteConfig& Route = RouteTracker.GetConfig();
	if (bInFight && !bRouteWasInFight)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Bot] Route: fight at %.1f s on the way to #%d (%s), formation %s"), Now, RouteTracker.GetIndex(),
			RouteTracker.GetWaypoint() ? *RouteTracker.GetWaypoint()->Label : TEXT("?"), *LabCollector.GetFormation());
		if (RoutePhase == ERouteActionPhase::None)
		{
			if (Route.Cover.bEnabled && TryTakeFightCover())
			{
				bRouteFightHold = true;
				bRouteFightCover = true;
				RouteFightHoldStart = Now;
			}
			else if (Route.FightHoldSeconds > 0.f)
			{
				bRouteFightHold = true;
				bRouteFightCover = false;
				RouteFightHoldStart = Now;
				Leader->OrderMoveTo(Leader->GetActorLocation(), false); // halt where he stands
				RouteTracker.NotifyOrder(TEXT("halt: fight (no wall to take cover at)"));
			}
		}
	}
	const float HoldLimit = bRouteFightCover ? Route.Cover.HoldSeconds : Route.FightHoldSeconds;
	if (bRouteFightHold && (!bInFight || Now - RouteFightHoldStart >= HoldLimit))
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Bot] Route: walking on at %.1f s (%s; %s %.1f s)"), Now,
			bInFight ? TEXT("still fighting, firing on the move") : TEXT("the fight is over"), bRouteFightCover ? TEXT("at the wall") : TEXT("halted"),
			Now - RouteFightHoldStart);
		bRouteFightHold = false;
		bRouteFightCover = false;
		// Up from any crouch the fight left (the wall layout's / the ROE's) to walk on with the formation.
		Squad->SetSquadStance(EOperativeStance::Standing);
		bRouteReorder = true;
	}
	bRouteWasInFight = bInFight;

	if (RoutePhase != ERouteActionPhase::None)
	{
		TickRouteAction(DeltaTime);
	}
	TrackRouteDrop();
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
		if (ScriptedRouteDrop())
		{
			return;
		}
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

bool UPlaytestBotSubsystem::TryTakeFightCover()
{
	using namespace BotRouteModeLocal;
	UWorld* World = GetWorld();
	USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
	UCombatPressureSubsystem* Pressure = World->GetSubsystem<UCombatPressureSubsystem>();
	const AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	if (!Leader || !Pressure || Squad->IsSoloMode())
	{
		return false;
	}
	// The threat: the nearest living enemy that fights (it left its patrol / post), else the nearest living one.
	const AEnemyCharacter* Threat = nullptr;
	float Best = MAX_flt;
	for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
	{
		if (It->IsDying() || !IsLiving(*It))
		{
			continue;
		}
		const float Score = static_cast<float>(FVector::Dist2D(It->GetActorLocation(), Leader->GetActorLocation())) + (It->IsOnPatrol() ? 100000.f : 0.f);
		if (Score < Best)
		{
			Best = Score;
			Threat = *It;
		}
	}
	if (!Threat)
	{
		return false;
	}
	const FBotRouteConfig& Route = RouteTracker.GetConfig();
	const TOptional<FBotCoverChoice> Choice = BotCoverRules::PickWall(Pressure->GetObstacleBoxes(), To2D(Leader->GetActorLocation()),
		To2D(Threat->GetActorLocation()), Route.Cover);
	if (!Choice.IsSet())
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Bot] Route: no wall within %.0f m between the squad and %s - it halts where it stands"), Route.Cover.SearchCm / 100.f,
			*Threat->GetName());
		return false;
	}
	// The player's click on the near face of the wall (from the squad's side, at knee height) = the group cover order / the formation wall layout.
	const float Z = RouteFeetZ(*Leader) + 100.f;
	const FVector2D From = Choice->FacePoint + Choice->Normal * 120.0;
	const FVector2D Into = Choice->FacePoint - Choice->Normal * 60.0;
	RouteClickCooldown = ClickInterval;
	if (!RouteClick(FVector(From.X, From.Y, Z), FVector(Into.X, Into.Y, Z), TEXT("fight cover")))
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Bot] Route: the cover click on the wall at (%.0f, %.0f) missed - it halts where it stands"), Choice->FacePoint.X,
			Choice->FacePoint.Y);
		return false;
	}
	ACodexTacticsPlayerController* Controller = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController());
	if (Controller && Controller->HasCoverPreview())
	{
		Controller->ConfirmCoverPreview(false);
	}
	RouteTracker.NotifyOrder(FString::Printf(TEXT("fight: cover at the wall (%.0f, %.0f) against %s"), Choice->FacePoint.X, Choice->FacePoint.Y, *Threat->GetName()));
	UE_LOG(LogCodexTactics, Display, TEXT("[Bot] Route: COVER - the squad to the wall at (%.0f, %.0f), %.1f m away, facing %s (%.0f m off, facing cos %.2f)"),
		Choice->FacePoint.X, Choice->FacePoint.Y, Choice->DistanceCm / 100.f, *Threat->GetName(),
		FVector2D::Distance(To2D(Threat->GetActorLocation()), Choice->FacePoint) / 100.0, Choice->FacingCos);
	return true;
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
	// Visibility first (the cursor's channel). A slab the view cuts away round the squad inside a building (a generated map's floor under
	// the leader's roof hole, 2026-11-02) ignores Visibility but still blocks WorldStatic: the click lands on it then.
	if (!World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params) && !World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Bot] Route click (%s) from %s to %s hit nothing"), What, *Start.ToCompactString(), *End.ToCompactString());
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
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
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
	// A waypoint with its level ("z"): the click just above that level (never the roof over it, never the floor under it); else from
	// above the leader's own level as before.
	const FVector Target(Waypoint->At.X, Waypoint->At.Y, Waypoint->bHasZ ? Waypoint->Z : RouteFeetZ(*Leader));
	const float Above = Waypoint->bHasZ ? LevelClickAboveCm : 400.f;
	const float Below = Waypoint->bHasZ ? LevelClickBelowCm : 600.f;
	if (!RouteClick(Target + FVector(0.f, 0.f, Above), Target - FVector(0.f, 0.f, Below), TEXT("move")))
	{
		HandleRouteCommand(RouteTracker.NotifyOrderFailed(RouteInput(), TEXT("no_ground_hit")));
		return;
	}
	bRouteReorder = false;
	// A walk waypoint within the wall-click reach of a wall (a generated map's point 2.7 m off a building corner, 2026-11-02): the click
	// became a COVER preview that waits for a second click - the route means a walk: the preview is dropped and the leader ordered there
	// (the formation follows him as on any leader move).
	ACodexTacticsPlayerController* Controller = Cast<ACodexTacticsPlayerController>(GetWorld()->GetFirstPlayerController());
	if (Controller && Controller->HasCoverPreview())
	{
		Controller->HideCoverPreview();
		Leader->OrderMoveTo(Target, false);
		UE_LOG(LogCodexTactics, Display, TEXT("[Bot] Route: the click on #%d (%s) was a wall-cover preview - dropped, the leader walks there"), RouteTracker.GetIndex(),
			*Waypoint->Label);
	}
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
	Sample.bCoverOrdered = bRouteFightHold && bRouteFightCover;
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
	Meta.Drops = GetRouteDrops();
	Meta.RealSeconds = static_cast<float>(FPlatformTime::Seconds() - StartRealTime);
	Meta.bImmortal = CombatCheats::IsImmortal(World);
	Meta.bInfiniteAmmo = CombatCheats::IsInfiniteAmmo(World);
	if (const UCombatPressureSubsystem* Pressure = World->GetSubsystem<UCombatPressureSubsystem>())
	{
		Meta.PressureEvents = Pressure->GetEventsFired();
		Meta.PressureSpawned = Pressure->GetSpawnRecords().Num();
		Meta.PressureSprungByWatch = Pressure->GetSprungByWatchCount();
		Meta.PressureRelocations = Pressure->GetRelocations().Num();
		Meta.PressureSprungByHold = Pressure->GetSprungByHoldCount();
		Meta.PressureNoRoute = Pressure->GetNoRouteSearches();
		Meta.PressureFollowers = Pressure->GetFollowerConversions();
		for (const FPressureRelocationRecord& Run : Pressure->GetRelocations())
		{
			Meta.PressureRunsSpotted += Run.Outcome == EAmbushRunStep::Spotted ? 1 : 0;
		}
	}
	const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
	const AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	const FVector2D LeaderNow = Leader ? BotRouteModeLocal::To2D(Leader->GetActorLocation()) : FVector2D::ZeroVector;
	const FString Line = CombatLabRules::ToJsonLine(CombatLabRules::MakeRunRecord(Meta, RouteTracker, Final, LeaderNow));
	UE_LOG(LogCodexTactics, Display, TEXT("[Bot] Route report: %s, laps %d, route completed %d (progress %.0f %%, skipped %d, stuck %d), contacts %d (patrol %d, ambush %d, coordinated %d, unwatched strikes %d), formation changes %d, wall hugs %d, cover taken %d of %d (%.0f s), stance changes %d, climbs %d, knockdowns %d, rounds %d, damage %.0f / absorbed %.0f"),
		Result, RouteTracker.GetLaps().Num(), RouteTracker.IsRouteCompleted() ? 1 : 0, RouteTracker.GetProgress() * 100.f, RouteTracker.GetTotalSkipped(),
		RouteTracker.GetStucks().Num(), Final.GetContacts().Num(), Final.CountContacts(ECombatLabContactKind::Patrol), Final.CountContacts(ECombatLabContactKind::Ambush),
		Final.CountContacts(ECombatLabContactKind::Coordinated), Final.CountUnwatchedStrikes(), Final.GetFormationChanges(), Final.GetWallHugs(),
		Final.GetCoverTaken(), Final.GetCoverOrders(), Final.GetCoverSeconds(), Final.GetStanceChanges(), Final.GetClimbsUp(), Final.GetKnockdowns(), Final.GetRounds(), Final.GetDamageTaken(), Final.GetDamageAbsorbed());
	if (!Meta.Drops.IsEmpty())
	{
		int32 Fell = 0;
		int32 Scripted = 0;
		for (const FBotRouteDrop& Drop : Meta.Drops)
		{
			Fell += Drop.bFell ? 1 : 0;
			Scripted += Drop.bScripted ? 1 : 0;
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[Bot] Route report: drops %d (fell through the hole %d, scripted %d)"), Meta.Drops.Num(), Fell, Scripted);
	}
	if (!CombatLabReportFile.IsEmpty())
	{
		if (!FFileHelper::SaveStringToFile(Line + TEXT("\n"), *CombatLabReportFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[Bot] Route report: cannot write %s"), *CombatLabReportFile);
		}
	}
}

void UPlaytestBotSubsystem::TrackRouteDrop()
{
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	const AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	const float Now = static_cast<float>(GetWorld()->GetTimeSeconds() - RouteStartTime);
	const FBotRouteWaypoint* Waypoint = RouteTracker.IsFinished() ? nullptr : RouteTracker.GetWaypoint();
	if (RouteDrops.IsValidIndex(RouteDropActive) && (RouteDrops[RouteDropActive].Waypoint != RouteTracker.GetIndex() || RouteTracker.IsFinished()))
	{
		FBotRouteDrop& Done = RouteDrops[RouteDropActive];
		Done.bReached = RouteTracker.GetTotalReached() > RouteDropReachedBase;
		Done.Seconds = Now - RouteDropStart;
		UE_LOG(LogCodexTactics, Display, TEXT("[Bot] Route: DROP %s to #%d (%s) %s after %.1f s: %s"), *Done.Via, Done.Waypoint, *Done.Label,
			Done.bReached ? TEXT("reached") : TEXT("GIVEN UP"), Done.Seconds,
			Done.bScripted ? TEXT("SCRIPTED (the squad put down by the bot)")
				: (Done.bFell ? TEXT("the leader fell through the hole (drop nav link)") : TEXT("no fall - walked round by the stairs")));
		RouteDropActive = INDEX_NONE;
	}
	if (RouteDropActive == INDEX_NONE && Waypoint && !Waypoint->Drop.IsEmpty() && !RouteTracker.IsInAction())
	{
		FBotRouteDrop Drop;
		Drop.Waypoint = RouteTracker.GetIndex();
		Drop.Label = Waypoint->Label;
		Drop.Via = Waypoint->Drop;
		RouteDropActive = RouteDrops.Add(Drop);
		RouteDropReachedBase = RouteTracker.GetTotalReached();
		RouteDropStart = Now;
	}
	if (RouteDrops.IsValidIndex(RouteDropActive) && !RouteDrops[RouteDropActive].bFell && Leader && Leader->GetCharacterMovement()
		&& Leader->GetCharacterMovement()->IsFalling())
	{
		RouteDrops[RouteDropActive].bFell = true;
		UE_LOG(LogCodexTactics, Display, TEXT("[Bot] Route: the leader drops through %s at %.1f s (feet z %.0f)"), *RouteDrops[RouteDropActive].Via, Now,
			BotRouteModeLocal::RouteFeetZ(*Leader));
	}
}

bool UPlaytestBotSubsystem::ScriptedRouteDrop()
{
	using namespace BotRouteModeLocal;
	const FBotRouteWaypoint* Waypoint = RouteTracker.GetWaypoint();
	USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	if (!Waypoint || Waypoint->Drop.IsEmpty() || !Waypoint->bHasZ || !Squad || !RouteDrops.IsValidIndex(RouteDropActive) || RouteDrops[RouteDropActive].bFell)
	{
		return false;
	}
	// The drop nav link did not take the squad down (no link / the leg stuck): the living members are put down at the waypoint's level on a
	// small ring round it - a SCRIPTED drop, marked in the record (the report shows it apart from a real fall).
	int32 Placed = 0;
	const TArray<AOperativeCharacter*> Members = Squad->GetMembers();
	for (int32 Each = 0; Each < Members.Num(); ++Each)
	{
		AOperativeCharacter* Member = Members[Each];
		if (!Member || !IsLiving(Member))
		{
			continue;
		}
		const float Angle = 2.f * PI * static_cast<float>(Each) / static_cast<float>(FMath::Max(1, Members.Num()));
		const FVector2D Spot = Waypoint->At + (Each == 0 ? FVector2D::ZeroVector : FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * DropRingCm);
		Member->TeleportTo(FVector(Spot.X, Spot.Y, Waypoint->Z + Member->GetSimpleCollisionHalfHeight() + 5.f), Member->GetActorRotation(), false, false);
		++Placed;
	}
	RouteDrops[RouteDropActive].bScripted = true;
	RouteTracker.NotifyOrder(FString::Printf(TEXT("scripted drop through %s to #%d"), *Waypoint->Drop, RouteTracker.GetIndex()));
	UE_LOG(LogCodexTactics, Display, TEXT("[Bot] Route: SCRIPTED DROP through %s to #%d (%s): %d members put down at (%.0f, %.0f, %.0f) - the drop leg got stuck"),
		*Waypoint->Drop, RouteTracker.GetIndex(), *Waypoint->Label, Placed, Waypoint->At.X, Waypoint->At.Y, Waypoint->Z);
	RouteClickCooldown = 0.f;
	IssueRouteMove(TEXT("after the scripted drop"));
	return true;
}

TArray<FBotRouteDrop> UPlaytestBotSubsystem::GetRouteDrops() const
{
	TArray<FBotRouteDrop> Out = RouteDrops;
	if (Out.IsValidIndex(RouteDropActive))
	{
		Out[RouteDropActive].Seconds = static_cast<float>(GetWorld()->GetTimeSeconds() - RouteStartTime) - RouteDropStart;
	}
	return Out;
}

void UPlaytestBotSubsystem::StartRouteRun(float DurationSeconds, bool bQuitAtEnd, int32 InSeed)
{
	RouteForce = 1;
	RouteDurationOverride = DurationSeconds;
	// A console start has no -BotTimeout: the real-time cap only guards against a hung game (the route ends by game time).
	TimeoutSeconds = FMath::Max(TimeoutSeconds, FMath::Max(3600.f, (DurationSeconds > 0.f ? DurationSeconds : 600.f) * 6.f));
	StartBot(EBotProfile::Normal, bQuitAtEnd, false, InSeed);
}

#if CODEX_WITH_PLAYTEST_BOT
namespace PlaytestBotRouteCommand
{
	void Run(const TArray<FString>& Args, UWorld* World)
	{
		UPlaytestBotSubsystem* Bot = World ? World->GetSubsystem<UPlaytestBotSubsystem>() : nullptr;
		if (!Bot)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("[Bot] Codex.Bot.Route: no bot in this world"));
			return;
		}
		float Seconds = 0.f;
		int32 RunSeed = 1;
		bool bQuitAtEnd = false;
		for (const FString& Arg : Args)
		{
			if (Arg.StartsWith(TEXT("seconds=")))
			{
				Seconds = FCString::Atof(*Arg.Mid(8));
			}
			else if (Arg.StartsWith(TEXT("seed=")))
			{
				RunSeed = FCString::Atoi(*Arg.Mid(5));
			}
			bQuitAtEnd |= Arg == TEXT("quit");
		}
		Bot->StartRouteRun(Seconds, bQuitAtEnd, RunSeed);
		UE_LOG(LogCodexTactics, Display, TEXT("[Bot] Codex.Bot.Route: route mode for %.0f s (quit %d, seed %d)"), Seconds, bQuitAtEnd ? 1 : 0, RunSeed);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(TEXT("Codex.Bot.Route"),
		TEXT("Dev (2026-11-02): the playtest bot walks the level's bot_route (route mode, also without an endless pressure): ")
		TEXT("Codex.Bot.Route [seconds=<game s>] [seed=<n>] [quit] (quit: exit the game when the time is up)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}
#endif // CODEX_WITH_PLAYTEST_BOT
