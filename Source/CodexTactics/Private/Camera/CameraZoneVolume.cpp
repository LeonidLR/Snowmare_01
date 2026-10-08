#include "Camera/CameraZoneVolume.h"
#include "Camera/CameraActor.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
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

	bool ShouldBeActive(bool bSwitchEnabled, bool bAllowInCombat, ECodexCombatMode CombatMode, bool bWaveActive, bool bLeaderInside)
	{
		if (!bSwitchEnabled || !bLeaderInside)
		{
			return false;
		}
		if (CombatMode == ECodexCombatMode::TacticalPause)
		{
			return false;
		}
		return bAllowInCombat || !bWaveActive;
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
	const bool bShouldBeActive = CameraZoneRules::ShouldBeActive(bEnableCameraSwitch, bAllowInCombat,
		Flow ? Flow->GetCombatMode() : ECodexCombatMode::None, Flow && Flow->IsWaveActive(), bLeaderInside);

	if (IsZoneActive() && (!bShouldBeActive || ActiveExplorer.Get() != Leader))
	{
		Deactivate();
	}
	if (!IsZoneActive() && bShouldBeActive)
	{
		Activate(Leader);
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

	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (PC && TargetCamera)
	{
		PC->SetViewTargetWithBlend(TargetCamera, BlendTime);
	}
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

	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (PC && PC->GetPawn() && PC->GetViewTarget() == TargetCamera)
	{
		PC->SetViewTargetWithBlend(PC->GetPawn(), BlendTime);
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
