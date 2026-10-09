#include "Camera/CameraZoneVolume.h"
#include "Camera/CameraActor.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/DeathCinematicSubsystem.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "UI/GameMessageSubsystem.h"

#define LOCTEXT_NAMESPACE "CameraZone"

namespace
{
	/** Godot _is_body_inside: +1 m vertical tolerance around the box. */
	constexpr float VerticalTolerance = 100.f;
}

namespace CameraZoneRules
{
	float GetColdMultiplier(ECameraZoneEnvironment Environment)
	{
		switch (Environment)
		{
		case ECameraZoneEnvironment::Closed:
			return 0.f;
		case ECameraZoneEnvironment::Shelter:
			return 0.5f;
		case ECameraZoneEnvironment::Blizzard:
			return 2.5f;
		default:
			return 1.f;
		}
	}

	bool ShouldBeActive(bool bSwitchEnabled, bool bLeaderInside)
	{
		return bSwitchEnabled && bLeaderInside;
	}

	bool IsCameraAllowed(const FCameraZoneModes& Modes, ECodexCombatMode CombatMode)
	{
		switch (CombatMode)
		{
		case ECodexCombatMode::RealTime:
			return Modes.bRealTime;
		case ECodexCombatMode::TurnBased:
			return Modes.bTurnBased;
		case ECodexCombatMode::TacticalPause:
			return Modes.bTacticalPause;
		default:
			return true;
		}
	}
}

ACameraZoneVolume::ACameraZoneVolume()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;

	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	RootComponent = Box;
	Box->SetBoxExtent(FVector(400.f, 400.f, 200.f));
	Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Box->ShapeColor = FColor(80, 200, 255);

	ZoneName = LOCTEXT("DefaultZoneName", "Surveillance sector 01");
	ExitMessage = LOCTEXT("DefaultExit", "📡 Leaving the surveillance sector. Squad regrouped.");
}

bool ACameraZoneVolume::ContainsLocation(const FVector& Location) const
{
	const FVector Local = Box->GetComponentTransform().InverseTransformPosition(Location);
	const FVector Extent = Box->GetUnscaledBoxExtent();
	const FVector Scale = Box->GetComponentScale();
	const FVector LocalWorldScaled = Local * Scale;
	const FVector ScaledExtent = Extent * Scale;
	return FMath::Abs(LocalWorldScaled.X) <= ScaledExtent.X
		&& FMath::Abs(LocalWorldScaled.Y) <= ScaledExtent.Y
		&& FMath::Abs(LocalWorldScaled.Z) <= ScaledExtent.Z + VerticalTolerance;
}

void ACameraZoneVolume::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UWorld* World = GetWorld();
	const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
	const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;

	const bool bLeaderInside = Leader && ContainsLocation(Leader->GetActorLocation());
	const bool bShouldBeActive = CameraZoneRules::ShouldBeActive(bEnableCameraSwitch, bLeaderInside);

	if (IsZoneActive() && (!bShouldBeActive || ActiveExplorer.Get() != Leader))
	{
		Deactivate();
	}
	if (!IsZoneActive() && bShouldBeActive)
	{
		Activate(Leader);
	}
	// User request 2026-10-08: the fixed camera per combat mode (real time yes; turn-based / tactical pause off by default:
	// the normal camera takes over and the zone camera comes back with real time while the leader is still inside). The
	// death cinematic owns the camera while it runs and hands it back here afterwards.
	CameraZoneRules::FCameraZoneModes Modes;
	Modes.bRealTime = bActiveInRealTime;
	Modes.bTurnBased = bActiveInTurnBased;
	Modes.bTacticalPause = bActiveInTacticalPause;
	const UDeathCinematicSubsystem* DeathCam = World->GetSubsystem<UDeathCinematicSubsystem>();
	const bool bWantCamera = IsZoneActive() && TargetCamera
		&& CameraZoneRules::IsCameraAllowed(Modes, Flow ? Flow->GetCombatMode() : ECodexCombatMode::None)
		&& !(DeathCam && DeathCam->IsFocusActive());
	if (bWantCamera != bCameraShown)
	{
		ShowZoneCamera(bWantCamera);
	}
}

void ACameraZoneVolume::ShowZoneCamera(bool bShow)
{
	bCameraShown = bShow;
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!PC)
	{
		return;
	}
	if (bShow && TargetCamera)
	{
		PC->SetViewTargetWithBlend(TargetCamera, BlendTime);
	}
	else if (!bShow && PC->GetPawn() && PC->GetViewTarget() == TargetCamera)
	{
		PC->SetViewTargetWithBlend(PC->GetPawn(), BlendTime);
	}
}

void ACameraZoneVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IsZoneActive())
	{
		Deactivate();
	}
	Super::EndPlay(EndPlayReason);
}

void ACameraZoneVolume::Activate(AOperativeCharacter* Explorer)
{
	ActiveExplorer = Explorer;
	if (Explorer)
	{
		Explorer->bInCameraZone = true;
	}

	// The camera itself follows in Tick (ShowZoneCamera) for the current combat mode.
	if (USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>())
	{
		Squad->SetFollowersHolding(true);
	}

	const FText Text = EnterMessage.IsEmpty()
		? FText::Format(LOCTEXT("EnterDefault", "📹 {0} [{1}]. Squad holds the perimeter outside."), ZoneName, GetEnvironmentLabel())
		: FText::Format(LOCTEXT("EnterCustom", "{0} [{1}]"), EnterMessage, GetEnvironmentLabel());
	PostMessage(Text);
}

void ACameraZoneVolume::Deactivate()
{
	if (AOperativeCharacter* Explorer = ActiveExplorer.Get())
	{
		Explorer->bInCameraZone = false;
	}
	ActiveExplorer.Reset();
	if (bCameraShown)
	{
		ShowZoneCamera(false);
	}
	if (USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>())
	{
		Squad->SetFollowersHolding(false);
	}
	if (!ExitMessage.IsEmpty())
	{
		PostMessage(ExitMessage);
	}
}

FText ACameraZoneVolume::GetEnvironmentLabel() const
{
	switch (Environment)
	{
	case ECameraZoneEnvironment::Closed:
		return LOCTEXT("EnvClosed", "Closed (Bunker, 0.0x)");
	case ECameraZoneEnvironment::Shelter:
		return LOCTEXT("EnvShelter", "Shelter (0.5x)");
	case ECameraZoneEnvironment::Blizzard:
		return LOCTEXT("EnvBlizzard", "Blizzard (2.5x)");
	default:
		return LOCTEXT("EnvStandard", "Standard (1.0x)");
	}
}

void ACameraZoneVolume::PostMessage(const FText& Text) const
{
	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(LOCTEXT("Speaker", "SURVEILLANCE"), Text);
	}
}

#undef LOCTEXT_NAMESPACE
