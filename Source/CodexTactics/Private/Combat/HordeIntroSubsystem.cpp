#include "Combat/HordeIntroSubsystem.h"

#include "Camera/TacticalCameraPawn.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/DeathCinematicSubsystem.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Combat/WaveSubsystem.h"
#include "Engine/TargetPoint.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "UI/GameMessageSubsystem.h"
#include "UI/HordeAlertWidget.h"

#define LOCTEXT_NAMESPACE "HordeIntro"

namespace
{
	/** A frame longer than this (hitch, map load) counts as this much real time. */
	constexpr float HordeIntroMaxRealDelta = 0.1f;
}

TStatId UHordeIntroSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UHordeIntroSubsystem, STATGROUP_Tickables);
}

bool UHordeIntroSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UHordeIntroSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (UGameFlowSubsystem* Flow = InWorld.GetSubsystem<UGameFlowSubsystem>())
	{
		Flow->OnGameFlowChanged.AddDynamic(this, &UHordeIntroSubsystem::HandleGameFlowChanged);
	}
}

void UHordeIntroSubsystem::Deinitialize()
{
	if (ATacticalCameraPawn* Camera = GetCamera())
	{
		Camera->SetCinematicLock(false);
	}
	if (AlertWidget)
	{
		AlertWidget->RemoveFromParent();
		AlertWidget = nullptr;
	}
	Super::Deinitialize();
}

void UHordeIntroSubsystem::HandleGameFlowChanged(ECodexGamePhase Phase, ECodexCombatMode CombatMode)
{
	if (bOpeningPause)
	{
		return; // our own RealTime -> free pause
	}
	UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	// Game designer 2026-10-25: what was flown over stays shown for the whole level (Memory, saved) - no reset per fight any more.
	if (!Flow)
	{
		return;
	}
	if (Stage == EHordeIntroStage::Flyover)
	{
		if (!(Phase == ECodexGamePhase::WaveCombat && CombatMode == ECodexCombatMode::TacticalPause))
		{
			// The pause ended under the flyover (Space tap -> real time, hold -> turn-based, the wave cleared, game over).
			EndFlyover(true, true, TEXT("the free pause ended"));
		}
		return;
	}
	if (Stage == EHordeIntroStage::Waiting && !Flow->IsAwaitingHordeIntro())
	{
		if (Flow->IsFreePause())
		{
			// A Space tap before the trigger: the free pause at once, the alert explains it, no flyover.
			Stage = EHordeIntroStage::Idle;
			bPreTriggerSlowed = false;
			ShowAlert();
			OnHordeIntroStarted.Broadcast(WaveIndex, false);
			OnHordeIntroFinished.Broadcast(WaveIndex, true);
			UE_LOG(LogCodexTactics, Display, TEXT("[HordeIntro] free pause opened by the player before the trigger (%.2f s)"), GameSinceSpawn);
		}
		else
		{
			Stage = EHordeIntroStage::Idle;
			bPreTriggerSlowed = false;
			UE_LOG(LogCodexTactics, Display, TEXT("[HordeIntro] intro dropped (flow left the sudden wave's real time)"));
		}
		return;
	}
	if (Stage == EHordeIntroStage::Idle && Flow->IsAwaitingHordeIntro())
	{
		BeginWaiting();
	}
}

void UHordeIntroSubsystem::BeginWaiting()
{
	Config = HordeIntroRules::GetConfig();
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	Stage = EHordeIntroStage::Waiting;
	WaveIndex = Flow ? Flow->GetWaveIndex() : 0;
	GameSinceSpawn = 0.f;
	MassRunningSeconds = 0.f;
	bMassFormed = false;
	CurrentShot = INDEX_NONE;
	Shots.Reset();
	++IntroCount;
	bPreTriggerSlowed = Config.PreTriggerTimeDilation < 0.999f;
	if (bPreTriggerSlowed)
	{
		UGameplayStatics::SetGlobalTimeDilation(GetWorld(), Config.PreTriggerTimeDilation);
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[HordeIntro] wave %d spawned suddenly: the fight runs, waiting for the running mass (%.0f %% at %.0f cm/s, %.1f..%.1f s)"),
		WaveIndex, Config.MassMovingFraction * 100.f, Config.MassMinSpeed, Config.MinDelaySeconds, Config.MaxWaitSeconds);
}

void UHordeIntroSubsystem::DismissIntro()
{
	UGameFlowSubsystem* Flow = GetWorld() ? GetWorld()->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	if (Stage == EHordeIntroStage::Waiting)
	{
		Stage = EHordeIntroStage::Idle;
		if (Flow)
		{
			Flow->CancelHordeIntro();
			if (bPreTriggerSlowed)
			{
				Flow->ApplyTimeDilation();
			}
		}
		bPreTriggerSlowed = false;
		UE_LOG(LogCodexTactics, Display, TEXT("[HordeIntro] intro dismissed: the fight stays in real time"));
	}
	else if (Stage == EHordeIntroStage::Flyover)
	{
		EndFlyover(true, true, TEXT("dismissed"));
		if (Flow && Flow->IsFreePause())
		{
			Flow->ToggleTacticalPause();
		}
	}
	else if (Flow)
	{
		Flow->CancelHordeIntro();
	}
}

void UHordeIntroSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	if (!World || World->IsPaused())
	{
		return; // the pause menu stops the intro clock
	}
	const float RealDelta = FMath::Clamp(static_cast<float>(FApp::GetDeltaTime()), 0.f, HordeIntroMaxRealDelta);
	if (AlertSeconds >= 0.f)
	{
		AlertSeconds += RealDelta;
		if (AlertSeconds > AlertShowSeconds + Config.AlertFadeSeconds)
		{
			AlertSeconds = -1.f;
			if (AlertWidget)
			{
				AlertWidget->RemoveFromParent();
				AlertWidget = nullptr;
			}
		}
	}
	if (Stage == EHordeIntroStage::Waiting)
	{
		const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		if (!Flow || !Flow->IsAwaitingHordeIntro())
		{
			Stage = EHordeIntroStage::Idle; // cancelled (smokes / the bot)
			bPreTriggerSlowed = false;
			return;
		}
		const AWorldSettings* Settings = World->GetWorldSettings();
		TickWaiting(RealDelta, RealDelta * (Settings ? Settings->GetEffectiveTimeDilation() : 1.f));
		return;
	}
	if (Stage != EHordeIntroStage::Flyover)
	{
		return;
	}
	// The death cinematic owns the camera: the flyover yields (it hands the view back to the leader itself).
	if (const UDeathCinematicSubsystem* DeathCam = World->GetSubsystem<UDeathCinematicSubsystem>(); DeathCam && DeathCam->IsFocusActive())
	{
		EndFlyover(false, true, TEXT("a death cinematic took the camera"));
		return;
	}
	FlyoverSeconds += RealDelta;
	const FHordeIntroCursor Cursor = HordeIntroRules::Evaluate(Shots, FlyoverSeconds);
	if (Cursor.bFinished)
	{
		++CompletedCount;
		EndFlyover(false, false, TEXT("done"));
		return;
	}
	if (Cursor.ShotIndex != CurrentShot && Cursor.ShotIndex != INDEX_NONE)
	{
		StartShot(Cursor.ShotIndex);
	}
	if (Shots.IsValidIndex(CurrentShot) && Shots[CurrentShot].Kind == EHordeIntroShotKind::Wide)
	{
		UpdateWideAnchor();
	}
	ApplyFlyoverDilation();
}

void UHordeIntroSubsystem::TickWaiting(float RealDelta, float GameDelta)
{
	GameSinceSpawn += GameDelta;
	TArray<AEnemyCharacter*> Enemies;
	GatherEnemies(Enemies);
	TArray<FHordeMover> Movers;
	for (const AEnemyCharacter* Enemy : Enemies)
	{
		Movers.Add(MoverOf(*Enemy));
	}
	const float Fraction = HordeIntroRules::RunningFraction(Movers, SquadCentre(), Config.MassMinSpeed);
	// Tuning trace (Codex.HordeIntro.*): once per game second, how the mass forms.
	if (FMath::FloorToInt(GameSinceSpawn) != FMath::FloorToInt(GameSinceSpawn - GameDelta))
	{
		int32 Moving = 0;
		float SpeedSum = 0.f;
		for (const FHordeMover& Mover : Movers)
		{
			const float Speed = Mover.Velocity.Size2D();
			Moving += Speed > 50.f ? 1 : 0;
			SpeedSum += Speed;
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[HordeIntro] %.1f s: %d enemies, %d moving, mean %.0f cm/s, %.0f %% running at the squad"),
			GameSinceSpawn, Movers.Num(), Moving, Movers.IsEmpty() ? 0.f : SpeedSum / Movers.Num(), Fraction * 100.f);
	}
	MassRunningSeconds = Fraction >= Config.MassMovingFraction && !Movers.IsEmpty() ? MassRunningSeconds + GameDelta : 0.f;
	if (!bMassFormed && HordeIntroRules::IsMassFormed(MassRunningSeconds, Config))
	{
		bMassFormed = true;
		TriggerRunningFraction = Fraction;
		UE_LOG(LogCodexTactics, Display, TEXT("[HordeIntro] the running mass formed at %.2f game s (%.0f %% run at the squad)"), GameSinceSpawn, Fraction * 100.f);
	}
	// 2026-10-22: never fly to a wave that has not spawned yet (queued spawns, or nobody alive) - up to SpawnGraceSeconds longer.
	const UWaveSubsystem* Waves = GetWorld()->GetSubsystem<UWaveSubsystem>();
	const bool bAllSpawned = !Waves || Waves->GetRemainingSpawnCount() == 0;
	if (!HordeIntroRules::ShouldTriggerSpawned(GameSinceSpawn, bMassFormed, bAllSpawned, Movers.Num(), Config))
	{
		return;
	}
	TriggerGameSeconds = GameSinceSpawn;
	TriggerRunningFraction = bMassFormed ? TriggerRunningFraction : Fraction;
	bTriggerByMass = bMassFormed;
	UE_LOG(LogCodexTactics, Display, TEXT("[HordeIntro] trigger after %.2f game s: %.0f %% of %d enemies run at the squad (%s)"),
		GameSinceSpawn, Fraction * 100.f, Movers.Num(), bTriggerByMass ? TEXT("running mass") : TEXT("timeout"));
	OpenPause(Config.bEnabled);
}

void UHordeIntroSubsystem::OpenPause(bool bFlyover)
{
	UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	bOpeningPause = true;
	const EGameFlowResult Result = Flow ? Flow->EnterFreeTacticalPause() : EGameFlowResult::WrongPhase;
	bOpeningPause = false;
	bPreTriggerSlowed = false;
	if (Result != EGameFlowResult::Ok)
	{
		Stage = EHordeIntroStage::Idle;
		return;
	}
	ShowAlert();
	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(LOCTEXT("HQ", "HQ"), LOCTEXT("Contact",
			"⚠️ HORDE INCOMING! Free tactical pause: give your orders, place barricades - [SPACE] to engage."));
	}
	ATacticalCameraPawn* Camera = GetCamera();
	if (bFlyover && Camera)
	{
		TArray<AEnemyCharacter*> Enemies;
		GatherEnemies(Enemies);
		TArray<EEnemyArchetype> Types;
		for (const AEnemyCharacter* Enemy : Enemies)
		{
			Types.Add(Enemy->GetArchetype());
		}
		Shots = HordeIntroRules::BuildShotList(HordeIntroRules::UniqueTypesInOrder(Types), Memory, Config);
	}
	else
	{
		Shots.Reset();
	}
	if (Shots.IsEmpty())
	{
		Stage = EHordeIntroStage::Idle;
		OnHordeIntroStarted.Broadcast(WaveIndex, false);
		OnHordeIntroFinished.Broadcast(WaveIndex, false);
		UE_LOG(LogCodexTactics, Display, TEXT("[HordeIntro] free pause without a flyover%s"),
			bFlyover && Memory.bWideShotShown ? TEXT(" (every type of the wave was shown on this level before)") : TEXT(""));
		return;
	}
	// The level's memory (2026-10-25): this flyover's wide shot and types count as shown from now on, a skip included.
	HordeIntroRules::RememberShots(Memory, Shots);
	FString Plan;
	for (const FHordeIntroShot& Shot : Shots)
	{
		Plan += FString::Printf(TEXT(" %s %.1fs"), *HordeIntroRules::ShotLabel(Shot), Shot.Seconds);
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[HordeIntro] flyover at %.2f world speed:%s"), Config.FlyoverTimeDilation, *Plan);
	Stage = EHordeIntroStage::Flyover;
	++StartedCount;
	FlyoverSeconds = 0.f;
	CurrentShot = INDEX_NONE;
	ReturnDistance = Camera->GetTargetDistance();
	Camera->SetCinematicLock(true);
	// The squad may not see the horde yet (Sprint 08 line of sight hides it): the flyover shows it.
	if (UTacticalSightSubsystem* Sight = GetWorld()->GetSubsystem<UTacticalSightSubsystem>())
	{
		Sight->SetCinematicReveal(true);
	}
	// A camera zone's fixed camera steps aside for the flyover.
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0); PC && PC->GetViewTarget() != Camera)
	{
		PC->SetViewTargetWithBlend(Camera, Config.ShotBlendSeconds);
	}
	ApplyFlyoverDilation();
	OnHordeIntroStarted.Broadcast(WaveIndex, true);
	StartShot(0);
}

void UHordeIntroSubsystem::ApplyFlyoverDilation()
{
	UWorld* World = GetWorld();
	const AWorldSettings* Settings = World ? World->GetWorldSettings() : nullptr;
	if (Settings && !FMath::IsNearlyEqual(Settings->TimeDilation, Config.FlyoverTimeDilation, 0.0005f))
	{
		UGameplayStatics::SetGlobalTimeDilation(World, Config.FlyoverTimeDilation);
	}
}

void UHordeIntroSubsystem::StartShot(int32 Index)
{
	CurrentShot = Index;
	ATacticalCameraPawn* Camera = GetCamera();
	if (!Camera || !Shots.IsValidIndex(Index))
	{
		return;
	}
	const FHordeIntroShot& Shot = Shots[Index];
	const FVector Squad = SquadCentre();
	AActor* Target = nullptr;
	switch (Shot.Kind)
	{
	case EHordeIntroShotKind::Wide:
	{
		if (!WideAnchor)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Params.ObjectFlags |= RF_Transient;
			WideAnchor = GetWorld()->SpawnActor<ATargetPoint>(Squad, FRotator::ZeroRotator, Params);
		}
		// Game designer play-test 2026-10-22 ("sometimes the fly-in shows nothing"): the shot frames the main cluster of the WAVE's
		// enemies (the level's other enemies far away pulled the old mass centre into empty ground) at the distance that really
		// holds them in the camera's frustum (the old span-based distance was too far for a tight pack and too close for a spread
		// one), the yaw fixed for the shot.
		const HordeIntroRules::FWideFrame Frame = ComputeWideFrame(true);
		Target = WideAnchor;
		if (Target)
		{
			WideAnchor->SetActorLocation(Frame.Focus);
			Camera->SetCinematicYaw(WideYaw);
			Camera->SmoothFocusOnTarget(Target, Config.ShotBlendSeconds, Frame.Distance);
			UE_LOG(LogCodexTactics, Display, TEXT("[HordeIntro] wide frame: %d of %d wave enemies in the main cluster, focus %s (%s), %.0f m"),
				WideClusterCount, WideEnemyCount, *Frame.Focus.ToCompactString(), Frame.bBiased ? TEXT("towards the squad") : TEXT("on the cluster"),
				Frame.Distance / 100.f);
		}
		break;
	}
	case EHordeIntroShotKind::Type:
	{
		TArray<AEnemyCharacter*> Enemies;
		GatherEnemies(Enemies);
		TArray<AEnemyCharacter*> OfType;
		TArray<FHordeMover> Candidates;
		for (AEnemyCharacter* Enemy : Enemies)
		{
			if (Enemy->GetArchetype() == Shot.Archetype && !Enemy->IsHidden())
			{
				OfType.Add(Enemy);
				Candidates.Add(MoverOf(*Enemy));
			}
		}
		if (OfType.IsEmpty())
		{
			for (AEnemyCharacter* Enemy : Enemies)
			{
				if (Enemy->GetArchetype() == Shot.Archetype)
				{
					OfType.Add(Enemy);
					Candidates.Add(MoverOf(*Enemy));
				}
			}
		}
		const int32 Pick = HordeIntroRules::PickRunningRepresentative(Candidates, Squad, Config.MassMinSpeed);
		int32 Running = 0;
		int32 Moving = 0;
		for (const FHordeMover& Candidate : Candidates)
		{
			Running += HordeIntroRules::IsRunningAtSquad(Candidate, Squad, Config.MassMinSpeed) ? 1 : 0;
			Moving += Candidate.Velocity.Size2D() > 50.f ? 1 : 0;
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[HordeIntro] %s: %d of the type, %d moving, %d running at the squad"),
			*HordeIntroRules::ShotLabel(Shot), Candidates.Num(), Moving, Running);
		if (OfType.IsValidIndex(Pick))
		{
			Target = OfType[Pick];
			Camera->SetCinematicYaw(HordeIntroRules::ViewYawAt(Squad, Target->GetActorLocation(), Config.ViewSideDegrees));
			Camera->SmoothFocusOnTarget(Target, Config.ShotBlendSeconds, Config.TypeShotDistance);
		}
		break;
	}
	default:
		Target = GetLeaderActor();
		Camera->RestorePlayerYaw(false);
		if (Target)
		{
			Camera->SmoothFocusOnTarget(Target, FMath::Max(0.1f, Shot.Seconds), ReturnDistance);
		}
		break;
	}
	ShotTarget = Target;
	const FString Label = FString::Printf(TEXT("%s@%s"), *HordeIntroRules::ShotLabel(Shot), Target ? *Target->GetName() : TEXT("-"));
	ShotLog.Add(Label);
	UE_LOG(LogCodexTactics, Display, TEXT("[HordeIntro] shot %d: %s (%.2f real s into the flyover, target speed %.0f cm/s)"), Index, *Label,
		FlyoverSeconds, Target ? Target->GetVelocity().Size2D() : 0.f);
}

HordeIntroRules::FWideFrame UHordeIntroSubsystem::ComputeWideFrame(bool bNewYaw)
{
	TArray<AEnemyCharacter*> Enemies;
	GatherEnemies(Enemies);
	TArray<FVector> Feet;
	for (const AEnemyCharacter* Enemy : Enemies)
	{
		Feet.Add(Enemy->GetActorLocation() - FVector(0.f, 0.f, Enemy->GetSimpleCollisionHalfHeight()));
	}
	TArray<FVector> Cluster;
	for (const int32 Index : HordeIntroRules::MainCluster(Feet, Config.WideClusterRadiusCm))
	{
		Cluster.Add(Feet[Index]);
	}
	WideEnemyCount = Feet.Num();
	WideClusterCount = Cluster.Num();
	const FVector Squad = SquadCentre();
	if (bNewYaw)
	{
		WideYaw = HordeIntroRules::ViewYawAt(Squad, Cluster.IsEmpty() ? Squad : HordeIntroRules::CentreOf(Cluster, TArray<int32>()),
			Config.ViewSideDegrees);
	}
	const ATacticalCameraPawn* Camera = GetCamera();
	const float Pitch = Camera ? Camera->Config.Pitch : -35.26f;
	const float VerticalFov = Camera ? Camera->Config.VerticalFov : 30.f;
	return HordeIntroRules::FrameWide(Squad, Cluster, WideYaw, Pitch, VerticalFov, 16.f / 9.f, Config);
}

void UHordeIntroSubsystem::UpdateWideAnchor()
{
	if (!WideAnchor)
	{
		return;
	}
	// The pack keeps running: the frame follows its main cluster (focus and distance), the yaw of the shot stays.
	const HordeIntroRules::FWideFrame Frame = ComputeWideFrame(false);
	WideAnchor->SetActorLocation(Frame.Focus);
	if (ATacticalCameraPawn* Camera = GetCamera())
	{
		Camera->SetCinematicDistance(Frame.Distance);
	}
}

void UHordeIntroSubsystem::EndFlyover(bool bSnapBack, bool bSkipped, const TCHAR* Why)
{
	Stage = EHordeIntroStage::Idle;
	CurrentShot = INDEX_NONE;
	ShotTarget.Reset();
	UWorld* World = GetWorld();
	if (UTacticalSightSubsystem* Sight = World ? World->GetSubsystem<UTacticalSightSubsystem>() : nullptr)
	{
		Sight->SetCinematicReveal(false); // back to the squad's own sight (seen ones keep a silhouette)
	}
	if (ATacticalCameraPawn* Camera = GetCamera())
	{
		Camera->RestorePlayerYaw(bSnapBack);
		Camera->SetCinematicLock(false);
		if (bSnapBack)
		{
			Camera->SnapFocusOnTarget(GetLeaderActor(), ReturnDistance);
		}
		else if (Camera->GetFollowTarget() == WideAnchor || Cast<AEnemyCharacter>(Camera->GetFollowTarget()))
		{
			// Ended mid-shot without a snap (a death cinematic takes over): never leave the camera on an enemy.
			Camera->SetFollowTarget(GetLeaderActor());
		}
	}
	// The world settles into the flow's dilation: the free pause (0.02), or whatever ended it.
	if (UGameFlowSubsystem* Flow = World ? World->GetSubsystem<UGameFlowSubsystem>() : nullptr)
	{
		Flow->ApplyTimeDilation();
	}
	OnHordeIntroFinished.Broadcast(WaveIndex, bSkipped);
	UE_LOG(LogCodexTactics, Display, TEXT("[HordeIntro] flyover over: %s (%.2f real s)"), Why, FlyoverSeconds);
}

bool UHordeIntroSubsystem::HandleSkipInput(bool bFromEscape)
{
	if (Stage == EHordeIntroStage::Flyover)
	{
		++SkippedCount;
		EndFlyover(true, true, TEXT("skipped by the player"));
		return true;
	}
	if (Stage == EHordeIntroStage::Waiting && bFromEscape)
	{
		++SkippedCount;
		UE_LOG(LogCodexTactics, Display, TEXT("[HordeIntro] Esc before the trigger: the free pause at once"));
		OpenPause(false);
		return true;
	}
	return false;
}

void UHordeIntroSubsystem::ShowAlert()
{
	AlertSeconds = 0.f;
	AlertShowSeconds = Config.AlertSeconds;
	if (AlertWidget)
	{
		return;
	}
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		UClass* Class = AlertWidgetClass ? AlertWidgetClass.Get() : UHordeAlertWidget::StaticClass();
		AlertWidget = CreateWidget<UHordeAlertWidget>(PC, Class);
		if (AlertWidget)
		{
			AlertWidget->AddToViewport(25);
			AlertWidget->Refresh();
		}
	}
}

float UHordeIntroSubsystem::GetAlertAlpha() const
{
	return AlertSeconds < 0.f ? 0.f : HordeIntroRules::AlertAlpha(AlertSeconds, AlertShowSeconds, Config);
}

FText UHordeIntroSubsystem::GetAlertTitle() const
{
	return AlertSeconds < 0.f ? FText::GetEmpty() : LOCTEXT("AlertTitle", "HORDE INCOMING");
}

FText UHordeIntroSubsystem::GetAlertSubtitle() const
{
	return AlertSeconds < 0.f ? FText::GetEmpty() : LOCTEXT("AlertSubtitle", "TACTICAL PAUSE - GIVE YOUR ORDERS  |  [SPACE] TO ENGAGE");
}

FText UHordeIntroSubsystem::GetCaption() const
{
	if (!IsFlyoverActive() || !Shots.IsValidIndex(CurrentShot))
	{
		return FText::GetEmpty();
	}
	const FHordeIntroShot& Shot = Shots[CurrentShot];
	FText What;
	switch (Shot.Kind)
	{
	case EHordeIntroShotKind::Wide:
		What = LOCTEXT("WideCaption", "THE HORDE IS CHARGING");
		break;
	case EHordeIntroShotKind::Type:
		What = FText::Format(LOCTEXT("TypeCaption", "HOSTILE: {0}"),
			StaticEnum<EEnemyArchetype>()->GetDisplayNameTextByValue(static_cast<int64>(Shot.Archetype)));
		break;
	default:
		What = LOCTEXT("ReturnCaption", "BACK TO THE SQUAD");
		break;
	}
	return FText::Format(LOCTEXT("Caption", "{0}  |  [Esc / click] skip"), What);
}

ATacticalCameraPawn* UHordeIntroSubsystem::GetCamera() const
{
	return GetWorld() ? Cast<ATacticalCameraPawn>(UGameplayStatics::GetPlayerPawn(GetWorld(), 0)) : nullptr;
}

AActor* UHordeIntroSubsystem::GetLeaderActor() const
{
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	return Squad ? Squad->GetLeader() : nullptr;
}

FVector UHordeIntroSubsystem::SquadCentre() const
{
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	FVector Sum = FVector::ZeroVector;
	int32 Count = 0;
	for (const AOperativeCharacter* Member : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
	{
		Sum += Member->GetActorLocation();
		++Count;
	}
	if (Count > 0)
	{
		return Sum / Count;
	}
	const AActor* Leader = GetLeaderActor();
	return Leader ? Leader->GetActorLocation() : FVector::ZeroVector;
}

void UHordeIntroSubsystem::GatherEnemies(TArray<AEnemyCharacter*>& OutEnemies) const
{
	OutEnemies.Reset();
	// 2026-10-22: the fight's own enemies (the wave); the level's other enemies (patrols elsewhere on the lake, parked ones) are
	// never part of the horde intro. Every registered enemy only when the fight has none (a test spawn outside a wave).
	if (const UWaveSubsystem* Waves = GetWorld() ? GetWorld()->GetSubsystem<UWaveSubsystem>() : nullptr)
	{
		for (AEnemyCharacter* Enemy : Waves->GetAliveEnemies())
		{
			if (IsValid(Enemy) && !Enemy->IsDying() && (!Enemy->GetHealthComponent() || Enemy->GetHealthComponent()->IsAlive()))
			{
				OutEnemies.Add(Enemy);
			}
		}
		if (!OutEnemies.IsEmpty())
		{
			return;
		}
	}
	for (AEnemyCharacter* Registered : UEnemyRegistrySubsystem::GetEnemyCharacters(GetWorld()))
	{
		AEnemyCharacter* Enemy = Registered;
		if (IsValid(Enemy) && !Enemy->IsDying() && (!Enemy->GetHealthComponent() || Enemy->GetHealthComponent()->IsAlive()))
		{
			OutEnemies.Add(Enemy);
		}
	}
}

FHordeMover UHordeIntroSubsystem::MoverOf(const AEnemyCharacter& Enemy) const
{
	FHordeMover Mover;
	Mover.Location = Enemy.GetActorLocation();
	Mover.Velocity = Enemy.GetVelocity();
	return Mover;
}

FVector UHordeIntroSubsystem::MassCentre(const TArray<AEnemyCharacter*>& Enemies) const
{
	const FVector Squad = SquadCentre();
	FVector Sum = FVector::ZeroVector;
	int32 Count = 0;
	for (const AEnemyCharacter* Enemy : Enemies)
	{
		if (HordeIntroRules::IsRunningAtSquad(MoverOf(*Enemy), Squad, Config.MassMinSpeed))
		{
			Sum += Enemy->GetActorLocation();
			++Count;
		}
	}
	if (Count == 0)
	{
		for (const AEnemyCharacter* Enemy : Enemies)
		{
			Sum += Enemy->GetActorLocation();
			++Count;
		}
	}
	return Count > 0 ? Sum / Count : Squad;
}

#undef LOCTEXT_NAMESPACE
