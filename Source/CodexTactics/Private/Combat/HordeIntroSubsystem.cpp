#include "Combat/HordeIntroSubsystem.h"

#include "Camera/TacticalCameraPawn.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/DeathCinematicSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/TacticalSightSubsystem.h"
#include "UI/GameMessageSubsystem.h"
#include "Engine/TargetPoint.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"

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
	Super::Deinitialize();
}

void UHordeIntroSubsystem::HandleGameFlowChanged(ECodexGamePhase Phase, ECodexCombatMode CombatMode)
{
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	// A new fight (or a load before the first wave): every type is new again.
	if (Phase == ECodexGamePhase::Exploration || (Phase == ECodexGamePhase::Preparation && Flow && Flow->GetWaveIndex() <= 1))
	{
		ShownTypes.Reset();
	}
	if (Phase == ECodexGamePhase::WaveCombat && Flow && Flow->IsFreePause())
	{
		if (!IsIntroPending())
		{
			BeginIntro();
		}
		return;
	}
	if (IsIntroPending())
	{
		// The pause ended (Space tap -> real time, hold -> turn-based), the wave was cleared or the mission failed.
		EndIntro(bActive, TEXT("the free pause ended"));
	}
}

void UHordeIntroSubsystem::BeginIntro()
{
	Config = HordeIntroRules::GetConfig();
	bPending = true;
	bActive = false;
	CurrentShot = INDEX_NONE;
	RealSinceSpawn = 0.f;
	Shots.Reset();
	++IntroCount;
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	UE_LOG(LogCodexTactics, Display, TEXT("[HordeIntro] wave %d spawned suddenly: free tactical pause, flyover in %.2f s%s"),
		Flow ? Flow->GetWaveIndex() : 0, Config.FlyoverDelaySeconds, Config.bEnabled ? TEXT("") : TEXT(" (flyover off)"));
	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(LOCTEXT("HQ", "HQ"), LOCTEXT("Contact",
			"⚠️ CONTACT! The horde is upon you! Free tactical pause: give your orders, place barricades - [SPACE] to engage."));
	}
}

void UHordeIntroSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!IsIntroPending())
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World || World->IsPaused())
	{
		return; // the pause menu stops the flyover clock
	}
	// The death cinematic owns the camera: the flyover yields (it hands the view back to the leader itself).
	if (const UDeathCinematicSubsystem* DeathCam = World->GetSubsystem<UDeathCinematicSubsystem>(); DeathCam && DeathCam->IsFocusActive())
	{
		EndIntro(false, TEXT("a death cinematic took the camera"));
		return;
	}
	RealSinceSpawn += FMath::Clamp(static_cast<float>(FApp::GetDeltaTime()), 0.f, HordeIntroMaxRealDelta);
	if (bPending && !bActive)
	{
		if (RealSinceSpawn < Config.FlyoverDelaySeconds)
		{
			return;
		}
		BeginFlyover();
		if (!bActive)
		{
			return;
		}
	}
	const FHordeIntroCursor Cursor = HordeIntroRules::Evaluate(Shots, Config, RealSinceSpawn);
	if (Cursor.bFinished)
	{
		++CompletedCount;
		EndIntro(false, TEXT("done"));
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
}

void UHordeIntroSubsystem::BeginFlyover()
{
	TArray<AEnemyCharacter*> Enemies;
	GatherEnemies(Enemies);
	TArray<EEnemyArchetype> Types;
	for (const AEnemyCharacter* Enemy : Enemies)
	{
		Types.Add(Enemy->GetArchetype());
	}
	Shots = HordeIntroRules::BuildShotList(HordeIntroRules::UniqueTypesInOrder(Types), ShownTypes, Config);
	ATacticalCameraPawn* Camera = GetCamera();
	if (Shots.IsEmpty() || !Camera)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[HordeIntro] no flyover (%d enemies, %d types known, camera %d)"), Enemies.Num(),
			ShownTypes.Num(), Camera ? 1 : 0);
		bPending = false;
		return;
	}
	FString Plan;
	for (const FHordeIntroShot& Shot : Shots)
	{
		Plan += FString::Printf(TEXT(" %s %.1fs"), *HordeIntroRules::ShotLabel(Shot), Shot.Seconds);
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[HordeIntro] flyover:%s"), *Plan);
	bActive = true;
	++StartedCount;
	// The squad may not see the horde yet (Sprint 08 line of sight hides it): the flyover shows it.
	if (UTacticalSightSubsystem* Sight = GetWorld()->GetSubsystem<UTacticalSightSubsystem>())
	{
		Sight->SetCinematicReveal(true);
	}
	CurrentShot = INDEX_NONE;
	ReturnDistance = Camera->GetTargetDistance();
	Camera->SetCinematicLock(true);
	// A camera zone's fixed camera steps aside for the flyover (the tactical pause uses the gameplay camera anyway).
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0); PC && PC->GetViewTarget() != Camera)
	{
		PC->SetViewTargetWithBlend(Camera, Config.ShotBlendSeconds);
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
			WideAnchor = GetWorld()->SpawnActor<ATargetPoint>(SquadCentre(), FRotator::ZeroRotator, Params);
		}
		UpdateWideAnchor();
		TArray<AEnemyCharacter*> Enemies;
		GatherEnemies(Enemies);
		TArray<FVector> Points;
		for (const AEnemyCharacter* Enemy : Enemies)
		{
			Points.Add(Enemy->GetActorLocation());
		}
		if (const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>())
		{
			for (const AOperativeCharacter* Member : Squad->GetMembers())
			{
				Points.Add(Member->GetActorLocation());
			}
		}
		Target = WideAnchor;
		if (Target)
		{
			Camera->SmoothFocusOnTarget(Target, Config.ShotBlendSeconds,
				HordeIntroRules::WideDistance(Target->GetActorLocation(), Points, Config));
		}
		break;
	}
	case EHordeIntroShotKind::Type:
	{
		TArray<AEnemyCharacter*> Enemies;
		GatherEnemies(Enemies);
		TArray<AEnemyCharacter*> OfType;
		TArray<FVector> Locations;
		for (AEnemyCharacter* Enemy : Enemies)
		{
			if (Enemy->GetArchetype() == Shot.Archetype)
			{
				OfType.Add(Enemy);
				Locations.Add(Enemy->GetActorLocation());
			}
		}
		const int32 Pick = HordeIntroRules::PickRepresentative(Locations, SquadCentre());
		if (OfType.IsValidIndex(Pick))
		{
			Target = OfType[Pick];
			Camera->SmoothFocusOnTarget(Target, Config.ShotBlendSeconds, Config.TypeShotDistance);
			ShownTypes.Add(Shot.Archetype);
		}
		break;
	}
	default:
		Target = GetLeaderActor();
		if (Target)
		{
			Camera->SmoothFocusOnTarget(Target, FMath::Max(0.1f, Shot.Seconds), ReturnDistance);
		}
		break;
	}
	ShotTarget = Target;
	const FString Label = FString::Printf(TEXT("%s@%s"), *HordeIntroRules::ShotLabel(Shot), Target ? *Target->GetName() : TEXT("-"));
	ShotLog.Add(Label);
	UE_LOG(LogCodexTactics, Display, TEXT("[HordeIntro] shot %d: %s (%.2f s after the spawn)"), Index, *Label, RealSinceSpawn);
}

void UHordeIntroSubsystem::UpdateWideAnchor()
{
	if (!WideAnchor)
	{
		return;
	}
	TArray<AEnemyCharacter*> Enemies;
	GatherEnemies(Enemies);
	const FVector Squad = SquadCentre();
	WideAnchor->SetActorLocation(Enemies.IsEmpty() ? Squad : HordeIntroRules::WideFocus(Squad, HordeCentre(Enemies), Config));
}

void UHordeIntroSubsystem::EndIntro(bool bSnapBack, const TCHAR* Why)
{
	const bool bWasActive = bActive;
	bPending = false;
	bActive = false;
	CurrentShot = INDEX_NONE;
	ShotTarget.Reset();
	if (UTacticalSightSubsystem* Sight = GetWorld() ? GetWorld()->GetSubsystem<UTacticalSightSubsystem>() : nullptr)
	{
		Sight->SetCinematicReveal(false); // back to the squad's own sight (seen ones keep a silhouette)
	}
	ATacticalCameraPawn* Camera = GetCamera();
	if (Camera && bWasActive)
	{
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
	UE_LOG(LogCodexTactics, Display, TEXT("[HordeIntro] intro over: %s (%.2f s after the spawn)"), Why, RealSinceSpawn);
}

bool UHordeIntroSubsystem::HandleSkipInput()
{
	if (!IsIntroPending())
	{
		return false;
	}
	++SkippedCount;
	EndIntro(bActive, TEXT("skipped by the player"));
	return true;
}

FText UHordeIntroSubsystem::GetCaption() const
{
	if (!bActive || !Shots.IsValidIndex(CurrentShot))
	{
		return FText::GetEmpty();
	}
	const FHordeIntroShot& Shot = Shots[CurrentShot];
	FText What;
	switch (Shot.Kind)
	{
	case EHordeIntroShotKind::Wide:
		What = LOCTEXT("WideCaption", "THE HORDE IS ADVANCING");
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
	for (TActorIterator<AEnemyCharacter> It(GetWorld()); It; ++It)
	{
		AEnemyCharacter* Enemy = *It;
		if (IsValid(Enemy) && !Enemy->IsDying() && (!Enemy->GetHealthComponent() || Enemy->GetHealthComponent()->IsAlive()))
		{
			OutEnemies.Add(Enemy);
		}
	}
}

FVector UHordeIntroSubsystem::HordeCentre(const TArray<AEnemyCharacter*>& Enemies) const
{
	FVector Sum = FVector::ZeroVector;
	for (const AEnemyCharacter* Enemy : Enemies)
	{
		Sum += Enemy->GetActorLocation();
	}
	return Enemies.IsEmpty() ? SquadCentre() : Sum / Enemies.Num();
}

#undef LOCTEXT_NAMESPACE
