#include "Audio/CodexAudioSubsystem.h"
#include "Audio/CodexAudioRules.h"
#include "CodexTactics.h"
#include "Subsystems/CodexEventBus.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Combat/HordeSubsystem.h"
#include "UI/Frontend/CodexFrontendSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

UCodexAudioSubsystem* UCodexAudioSubsystem::Get(const UObject* WorldContext)
{
	if (!WorldContext)
	{
		return nullptr;
	}
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (World && World->GetGameInstance())
	{
		return World->GetGameInstance()->GetSubsystem<UCodexAudioSubsystem>();
	}
	return nullptr;
}

void UCodexAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UCodexEventBus* EventBus = Collection.InitializeDependency<UCodexEventBus>();
	if (EventBus)
	{
		EventBus->OnDialogueLineDisplayed.AddDynamic(this, &UCodexAudioSubsystem::HandleDialogueLineDisplayed);
		EventBus->OnWeatherChanged.AddDynamic(this, &UCodexAudioSubsystem::HandleWeatherChanged);
		EventBus->OnSoldierDowned.AddDynamic(this, &UCodexAudioSubsystem::HandleSoldierDowned);
		EventBus->OnMineSpotted.AddDynamic(this, &UCodexAudioSubsystem::HandleMineSpotted);
		EventBus->OnRageStarted.AddDynamic(this, &UCodexAudioSubsystem::HandleRageStarted);
		EventBus->OnGeneratorStateChanged.AddDynamic(this, &UCodexAudioSubsystem::HandleGeneratorStateChanged);
		EventBus->OnSoldierPanicked.AddDynamic(this, &UCodexAudioSubsystem::HandleSoldierPanicked);
		EventBus->OnGameplayEvent.AddDynamic(this, &UCodexAudioSubsystem::HandleGameplayEvent);
	}

	PostWorldInitHandle = FWorldDelegates::OnPostWorldInitialization.AddUObject(this, &UCodexAudioSubsystem::OnPostWorldInitialization);
	WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(this, &UCodexAudioSubsystem::OnWorldCleanup);

	if (GetGameInstance())
	{
		UWorld* World = GetGameInstance()->GetWorld();
		if (World)
		{
			BindToWorldSubsystems(World);
			RefreshMusicState();
		}
	}
}

void UCodexAudioSubsystem::Deinitialize()
{
	if (ActiveMusicComponent)
	{
		ActiveMusicComponent->Stop();
		ActiveMusicComponent->DestroyComponent();
		ActiveMusicComponent = nullptr;
	}

	if (PostWorldInitHandle.IsValid())
	{
		FWorldDelegates::OnPostWorldInitialization.Remove(PostWorldInitHandle);
		PostWorldInitHandle.Reset();
	}

	if (WorldCleanupHandle.IsValid())
	{
		FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
		WorldCleanupHandle.Reset();
	}

	Super::Deinitialize();
}

void UCodexAudioSubsystem::OnPostWorldInitialization(UWorld* World, const UWorld::InitializationValues IVS)
{
	if (!World || World->GetGameInstance() != GetGameInstance())
	{
		return;
	}

	bIsFrontendWorld = UCodexFrontendSubsystem::IsFrontendWorld(World);
	bHordeActive = false;
	bSquadSeesEnemy = false;
	bCombatPressureActive = false;
	AliveEnemies = 0;
	CurrentPhase = ECodexGamePhase::Exploration;
	CurrentWeather = EWeatherState::Clear;

	BindToWorldSubsystems(World);
	RefreshMusicState();
}

void UCodexAudioSubsystem::OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	if (World && World->GetGameInstance() == GetGameInstance())
	{
		if (ActiveMusicComponent)
		{
			ActiveMusicComponent->Stop();
			ActiveMusicComponent->DestroyComponent();
			ActiveMusicComponent = nullptr;
		}
		CurrentMusicState = ECodexMusicState::None;
		ActiveMusicCueId = NAME_None;
		LastTriggeredVoiceLineId = NAME_None;
		VoiceLineLastPlayedTime.Reset();
	}
}

void UCodexAudioSubsystem::BindToWorldSubsystems(UWorld* World)
{
	if (!World)
	{
		return;
	}

	if (UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>())
	{
		Flow->OnGameFlowChanged.RemoveDynamic(this, &UCodexAudioSubsystem::HandleGameFlowChanged);
		Flow->OnGameFlowChanged.AddDynamic(this, &UCodexAudioSubsystem::HandleGameFlowChanged);
		CurrentPhase = Flow->GetPhase();
	}

	if (UHordeSubsystem* Horde = World->GetSubsystem<UHordeSubsystem>())
	{
		Horde->OnHordeReleased.RemoveDynamic(this, &UCodexAudioSubsystem::HandleHordeReleased);
		Horde->OnHordeReleased.AddDynamic(this, &UCodexAudioSubsystem::HandleHordeReleased);
		bHordeActive = (Horde->GetHordesReleased() > 0);
	}
}

void UCodexAudioSubsystem::RefreshMusicState()
{
	const ECodexMusicState DesiredState = FCodexAudioRules::EvaluateMusicState(
		bIsFrontendWorld,
		CurrentPhase,
		AliveEnemies,
		bSquadSeesEnemy,
		bCombatPressureActive,
		bHordeActive,
		CurrentWeather
	);

	RequestMusicState(DesiredState);
}

bool UCodexAudioSubsystem::RequestMusicState(ECodexMusicState NewState, int32 VariantIndex)
{
	if (NewState == ECodexMusicState::None)
	{
		return false;
	}

	const ECodexMusicState OldState = CurrentMusicState;
	CurrentMusicState = NewState;

	const FName CueId = FCodexAudioRules::SelectMusicCueForState(NewState, VariantIndex);
	if (ActiveMusicCueId == CueId && ActiveMusicComponent && ActiveMusicComponent->IsPlaying())
	{
		// Early-out: cue is already actively playing, avoid restarting playback loop
		return true;
	}

	ActiveMusicCueId = CueId;

	// Cue change: stop/fade out previous music loop
	if (ActiveMusicComponent)
	{
		if (ActiveMusicComponent->IsPlaying())
		{
			ActiveMusicComponent->FadeOut(1.5f, 0.0f);
		}
		else
		{
			ActiveMusicComponent->Stop();
			ActiveMusicComponent->DestroyComponent();
		}
		ActiveMusicComponent = nullptr;
	}

	FCodexMusicCueDef CueDef;
	if (FCodexAudioRules::FindMusicCueDef(CueId, CueDef))
	{
		PlaySoundAssetQuiet(CueDef.AssetPath, true, 1.0f);
	}

	if (OldState != NewState)
	{
		OnMusicStateChanged.Broadcast(OldState, NewState);
	}

	return true;
}

bool UCodexAudioSubsystem::TriggerVoiceBark(FName VoiceLineId, const FVector& WorldLocation)
{
	FCodexVoiceLineDef LineDef;
	if (!FCodexAudioRules::FindVoiceLineDef(VoiceLineId, LineDef))
	{
		return false;
	}

	const float Now = GetCurrentGameTime();
	if (!FCodexAudioRules::CanPlayVoiceLine(VoiceLineId, Now, VoiceLineLastPlayedTime))
	{
		return false;
	}

	VoiceLineLastPlayedTime.FindOrAdd(VoiceLineId) = Now;
	LastTriggeredVoiceLineId = VoiceLineId;

	PlaySoundAssetQuiet(LineDef.AssetPath, false, 1.0f);

	OnVoiceBarkTriggered.Broadcast(VoiceLineId, LineDef.Speaker);
	return true;
}

bool UCodexAudioSubsystem::TriggerAudioEvent(FName EventId, FName Speaker)
{
	const FName VoiceLineId = FCodexAudioRules::MapEventToVoiceLine(EventId, Speaker);
	if (!VoiceLineId.IsNone())
	{
		return TriggerVoiceBark(VoiceLineId);
	}
	return false;
}

void UCodexAudioSubsystem::SetTacticalThreatState(bool bInSquadSeesEnemy, bool bInCombatPressureActive, int32 InAliveEnemyCount)
{
	bSquadSeesEnemy = bInSquadSeesEnemy;
	bCombatPressureActive = bInCombatPressureActive;
	AliveEnemies = InAliveEnemyCount;
	RefreshMusicState();
}

bool UCodexAudioSubsystem::PlaySoundAssetQuiet(const FString& AssetPath, bool bIsMusic, float Volume)
{
	if (AssetPath.IsEmpty())
	{
		return false;
	}

	// 1. Silent non-existence check: zero warnings or log spam if asset package is missing on disk
	if (!FCodexAudioRules::DoesSoundAssetExist(AssetPath))
	{
		UE_LOG(LogCodexTactics, Verbose, TEXT("[Audio] Sound asset '%s' not found on disk - silent no-op"), *AssetPath);
		return false;
	}

	// 2. Asset exists: attempt safe load and playback
	USoundBase* Sound = Cast<USoundBase>(StaticLoadObject(USoundBase::StaticClass(), nullptr, *AssetPath));
	if (!Sound)
	{
		UE_LOG(LogCodexTactics, Verbose, TEXT("[Audio] Sound asset '%s' failed to load - silent no-op"), *AssetPath);
		return false;
	}

	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (World)
	{
		if (bIsMusic)
		{
			ActiveMusicComponent = UGameplayStatics::SpawnSound2D(World, Sound, Volume, 1.0f, 0.0f, nullptr, false, true);
			return ActiveMusicComponent != nullptr;
		}
		else
		{
			UGameplayStatics::PlaySound2D(World, Sound, Volume);
			return true;
		}
	}

	return false;
}

float UCodexAudioSubsystem::GetCurrentGameTime() const
{
	if (GetGameInstance())
	{
		if (const UWorld* World = GetGameInstance()->GetWorld())
		{
			return World->GetTimeSeconds();
		}
	}
	return 0.f;
}

void UCodexAudioSubsystem::HandleGameFlowChanged(ECodexGamePhase Phase, ECodexCombatMode CombatMode)
{
	CurrentPhase = Phase;
	if (Phase == ECodexGamePhase::WaveCleared || Phase == ECodexGamePhase::PostCombat)
	{
		bHordeActive = false;
	}
	RefreshMusicState();
}

void UCodexAudioSubsystem::HandleWeatherChanged(EWeatherState From, EWeatherState To)
{
	CurrentWeather = To;
	RefreshMusicState();
}

void UCodexAudioSubsystem::HandleHordeReleased(int32 HordeIndex, int32 EnemyCount, FVector Location)
{
	bHordeActive = true;
	TriggerAudioEvent(TEXT("cmd_wave_advancing"));
	RefreshMusicState();
}

void UCodexAudioSubsystem::HandleSoldierDowned(AOperativeCharacter* Soldier)
{
	TriggerAudioEvent(TEXT("med_critically_down"));
}

void UCodexAudioSubsystem::HandleMineSpotted(AActor* Mine, AOperativeCharacter* Spotter)
{
	TriggerAudioEvent(TEXT("med_tripwire"));
}

void UCodexAudioSubsystem::HandleRageStarted(AOperativeCharacter* Soldier)
{
	TriggerAudioEvent(TEXT("eng_rage_shout"));
}

void UCodexAudioSubsystem::HandleGeneratorStateChanged(bool bPowered)
{
	if (bPowered)
	{
		TriggerAudioEvent(TEXT("cmd_generator_starts"));
	}
}

void UCodexAudioSubsystem::HandleSoldierPanicked(AOperativeCharacter* Soldier, const FString& Reason)
{
	TriggerAudioEvent(TEXT("cmd_panic_flee"));
}

void UCodexAudioSubsystem::HandleGameplayEvent(FName EventName, AActor* Subject)
{
	TriggerAudioEvent(EventName);
}

void UCodexAudioSubsystem::HandleDialogueLineDisplayed(const FString& Speaker, const FString& Text)
{
	// Optional VO hook
}
