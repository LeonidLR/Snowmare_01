#include "Combat/DeathCinematicSubsystem.h"

#include "Animation/AnimMontage.h"
#include "Camera/TacticalCameraPawn.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/MissionRules.h"
#include "Core/MissionSubsystem.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "UI/DeathCinematicOverlayWidget.h"

namespace
{
	/** A frame longer than this (hitch, debugger) counts as this much real time. */
	constexpr float DeathCamMaxRealDelta = 0.1f;
}

TStatId UDeathCinematicSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDeathCinematicSubsystem, STATGROUP_Tickables);
}

bool UDeathCinematicSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UDeathCinematicSubsystem::Deinitialize()
{
	SetInputBlocked(false);
	Super::Deinitialize();
}

void UDeathCinematicSubsystem::NotifyOperativeDied(AOperativeCharacter* Victim, bool bDefeat)
{
	if (!Victim || Phase == EDeathCinematicPhase::Done || Phase == EDeathCinematicPhase::DefeatFade || Phase == EDeathCinematicPhase::DefeatText)
	{
		return; // the defeat is already on screen
	}
	FEntry Entry;
	Entry.Victim = Victim;
	Entry.bDefeat = bDefeat;
	// A death during a running focus queues up without a second slow motion (user rule: no stacking).
	Entry.bSlowMo = !IsActive();
	bDefeatQueued |= bDefeat;
	if (bDefeat)
	{
		DefeatVictim = Victim;
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[DeathCam] %s died%s%s"), *Victim->DisplayName.ToString(), bDefeat ? TEXT(" (defeat)") : TEXT(""),
		IsActive() ? TEXT(", queued") : TEXT(""));
	if (IsActive())
	{
		Queue.Add(Entry);
		return;
	}
	Config = DeathCinematicRules::GetConfig();
	StartFocus(Entry);
}

void UDeathCinematicSubsystem::StartFocus(const FEntry& Entry)
{
	Current = Entry;
	Phase = EDeathCinematicPhase::Focus;
	Times = FDeathFocusTimes();
	Times.bSlowMo = Entry.bSlowMo && Config.SlowMoRealSeconds > 0.f;
	Times.ClipSeconds = Config.FallbackClipSeconds;
	bClipMeasured = false;
	SetInputBlocked(true);
	AOperativeCharacter* Victim = Current.Victim.Get();
	if (ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
	{
		if (Victim)
		{
			if (!bHasReturnDistance)
			{
				ReturnDistance = Camera->GetTargetDistance();
				bHasReturnDistance = true;
			}
			Camera->SmoothFocusOnTarget(Victim, Config.FocusBlendSeconds, Config.FocusDistance);
		}
		// A camera zone's fixed camera steps aside (ACameraZoneVolume checks IsFocusActive and restores it afterwards).
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0); PC && PC->GetViewTarget() != Camera)
		{
			PC->SetViewTargetWithBlend(Camera, Config.FocusBlendSeconds);
		}
	}
	if (Times.bSlowMo)
	{
		ApplyDilation(DeathCinematicRules::FocusDilation(Config, Times, BaseFlowDilation()));
	}
}

float UDeathCinematicSubsystem::MeasureClipSeconds(const AOperativeCharacter* Victim) const
{
	const USkeletalMeshComponent* Mesh = Victim ? Victim->GetMesh() : nullptr;
	const UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (!Anim)
	{
		return Config.FallbackClipSeconds;
	}
	// The held death clip: the newest montage still playing (death / knockdown fall on the full-body slot).
	float Longest = 0.f;
	for (const FAnimMontageInstance* Instance : Anim->MontageInstances)
	{
		if (Instance && Instance->Montage && Instance->IsActive())
		{
			const float Rate = FMath::Max(FMath::Abs(Instance->GetPlayRate()), 0.05f);
			Longest = FMath::Max(Longest, (Instance->Montage->GetPlayLength() - Instance->GetPosition()) / Rate);
		}
	}
	return Longest > 0.f ? Longest : Config.FallbackClipSeconds;
}

void UDeathCinematicSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!IsActive())
	{
		return;
	}
	UWorld* World = GetWorld();
	const AWorldSettings* Settings = World ? World->GetWorldSettings() : nullptr;
	if (!Settings || World->IsPaused())
	{
		return;
	}
	const float RealDelta = FMath::Clamp(static_cast<float>(FApp::GetDeltaTime()), 0.f, DeathCamMaxRealDelta);
	const float Dilation = Settings->GetEffectiveTimeDilation();
	// A dialogue / narrative pause froze the time over us: the cinematic waits with it.
	if (bYieldedDilation && Dilation < 0.01f && Phase == EDeathCinematicPhase::Focus)
	{
		return;
	}
	if (Phase == EDeathCinematicPhase::Focus)
	{
		AOperativeCharacter* Victim = Current.Victim.Get();
		if (!bClipMeasured)
		{
			Times.ClipSeconds = MeasureClipSeconds(Victim);
			bClipMeasured = true;
		}
		Times.RealSeconds += RealDelta;
		Times.GameSeconds += RealDelta * Dilation;
		if (Times.bSlowMo)
		{
			ApplyDilation(DeathCinematicRules::FocusDilation(Config, Times, BaseFlowDilation()));
		}
		// The camera stays on him even when the leader / the turn changes meanwhile.
		ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(UGameplayStatics::GetPlayerPawn(this, 0));
		if (Camera && Victim && Camera->GetFollowTarget() != Victim)
		{
			Camera->SetFollowTarget(Victim);
		}
		if (!Victim || DeathCinematicRules::IsFocusFinished(Config, Times))
		{
			FinishFocus();
		}
		return;
	}
	PhaseRealSeconds += RealDelta;
	ApplyDilation(FMath::Min(BaseFlowDilation(), Config.SlowMoScale)); // the fight stays slowed behind the black
	if (Overlay)
	{
		Overlay->Refresh();
	}
	if (Phase == EDeathCinematicPhase::DefeatFade && PhaseRealSeconds >= Config.DefeatFadeSeconds)
	{
		Phase = EDeathCinematicPhase::DefeatText;
		PhaseRealSeconds = 0.f;
		UE_LOG(LogCodexTactics, Display, TEXT("[DeathCam] %s"), *MissionRules::GetSquadFallenText().ToString());
	}
	else if (Phase == EDeathCinematicPhase::DefeatText && PhaseRealSeconds >= Config.DefeatTextSeconds)
	{
		EndAll();
	}
}

void UDeathCinematicSubsystem::FinishFocus()
{
	++CompletedFocuses;
	UE_LOG(LogCodexTactics, Display, TEXT("[DeathCam] focus on %s done (real %.2f s, game %.2f s, clip %.2f s)"),
		Current.Victim.IsValid() ? *Current.Victim->DisplayName.ToString() : TEXT("?"), Times.RealSeconds, Times.GameSeconds, Times.ClipSeconds);
	if (Current.bDefeat)
	{
		StartDefeat();
		return;
	}
	if (!Queue.IsEmpty())
	{
		const FEntry Next = Queue[0];
		Queue.RemoveAt(0);
		StartFocus(Next);
		return;
	}
	if (bDefeatQueued)
	{
		StartDefeat(); // the commander's entry vanished (actor gone): the defeat still follows
		return;
	}
	// Back to the fight: time resumes, the camera returns to the leader (the turn-based active operative).
	RestoreDilation();
	Phase = EDeathCinematicPhase::Idle;
	Current = FEntry();
	if (ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
	{
		const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
		if (AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr)
		{
			Camera->SmoothFocusOnTarget(Leader, Config.ReturnBlendSeconds, bHasReturnDistance ? ReturnDistance : -1.f);
		}
	}
	bHasReturnDistance = false;
	SetInputBlocked(false);
}

void UDeathCinematicSubsystem::StartDefeat()
{
	Queue.Reset();
	Phase = EDeathCinematicPhase::DefeatFade;
	PhaseRealSeconds = 0.f;
	UE_LOG(LogCodexTactics, Display, TEXT("[DeathCam] defeat: fade to black"));
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0); PC && !Overlay)
	{
		Overlay = CreateWidget<UDeathCinematicOverlayWidget>(PC, UDeathCinematicOverlayWidget::StaticClass());
		if (Overlay)
		{
			Overlay->AddToViewport(30);
		}
	}
}

void UDeathCinematicSubsystem::EndAll()
{
	if (Overlay)
	{
		Overlay->RemoveFromParent(); // the mission-failed screen takes over
		Overlay = nullptr;
	}
	RestoreDilation();
	Phase = EDeathCinematicPhase::Done;
	bDefeatQueued = false;
	SetInputBlocked(false);
	if (UMissionSubsystem* Mission = GetWorld()->GetSubsystem<UMissionSubsystem>())
	{
		Mission->TriggerMissionFailed(DefeatVictim.Get());
	}
	else if (UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>())
	{
		Flow->TriggerGameOver();
	}
}

float UDeathCinematicSubsystem::BaseFlowDilation() const
{
	const UGameFlowSubsystem* Flow = GetWorld() ? GetWorld()->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	if (!Flow)
	{
		return 1.f;
	}
	// Same table as FGameFlowStateMachine::GetTimeDilation.
	switch (Flow->GetPhase())
	{
	case ECodexGamePhase::WaveCleared:
	case ECodexGamePhase::GameOver:
		return 0.f;
	case ECodexGamePhase::WaveCombat:
		return Flow->GetCombatMode() == ECodexCombatMode::TacticalPause ? Flow->GetConfig().TacticalPauseTimeDilation : 1.f;
	default:
		return 1.f;
	}
}

void UDeathCinematicSubsystem::ApplyDilation(float Wanted)
{
	UWorld* World = GetWorld();
	AWorldSettings* Settings = World ? World->GetWorldSettings() : nullptr;
	if (!Settings || bYieldedDilation)
	{
		return;
	}
	const float Now = Settings->TimeDilation;
	const float Base = BaseFlowDilation();
	// Someone else set the time (a dialogue's narrative pause, a cutscene): leave it to them, no stacking.
	const bool bOurs = AppliedDilation >= 0.f && FMath::IsNearlyEqual(Now, AppliedDilation, 0.001f);
	if (!bOurs && !FMath::IsNearlyEqual(Now, Base, 0.001f))
	{
		bYieldedDilation = true;
		UE_LOG(LogCodexTactics, Display, TEXT("[DeathCam] time dilation %.3f set by someone else: slow motion yields"), Now);
		return;
	}
	if (!FMath::IsNearlyEqual(Now, Wanted, 0.0005f))
	{
		UGameplayStatics::SetGlobalTimeDilation(World, Wanted);
	}
	AppliedDilation = Settings->TimeDilation;
	LowestAppliedDilation = FMath::Min(LowestAppliedDilation, AppliedDilation);
}

void UDeathCinematicSubsystem::RestoreDilation()
{
	UWorld* World = GetWorld();
	const AWorldSettings* Settings = World ? World->GetWorldSettings() : nullptr;
	if (Settings && !bYieldedDilation && AppliedDilation >= 0.f && FMath::IsNearlyEqual(Settings->TimeDilation, AppliedDilation, 0.001f))
	{
		if (UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>())
		{
			Flow->ApplyTimeDilation();
		}
		else
		{
			UGameplayStatics::SetGlobalTimeDilation(World, 1.f);
		}
	}
	AppliedDilation = -1.f;
	bYieldedDilation = false;
}

void UDeathCinematicSubsystem::SetInputBlocked(bool bBlocked)
{
	if (bInputBlocked == bBlocked)
	{
		return;
	}
	bInputBlocked = bBlocked;
	UWorld* World = GetWorld();
	APlayerController* PC = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	if (!PC)
	{
		return;
	}
	if (bBlocked)
	{
		PC->DisableInput(PC);
	}
	else
	{
		PC->EnableInput(PC);
	}
}

float UDeathCinematicSubsystem::GetFadeAlpha() const
{
	return DeathCinematicRules::FadeAlpha(Config, Phase, PhaseRealSeconds);
}

FText UDeathCinematicSubsystem::GetDefeatText() const
{
	return DeathCinematicRules::ShowsDefeatText(Phase) ? MissionRules::GetSquadFallenText() : FText::GetEmpty();
}
