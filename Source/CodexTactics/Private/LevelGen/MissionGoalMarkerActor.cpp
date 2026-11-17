#include "LevelGen/MissionGoalMarkerActor.h"

#include "Engine/World.h"
#include "Tutorial/TutorialSubsystem.h"

AMissionGoalMarkerActor::AMissionGoalMarkerActor()
{
	bScreenEdgePointer = false; // the tutorial overlay never sees this marker (not registered)
	BeaconHeightCm = 500.f;     // taller than the tutorial's: seen over yard walls and crates
}

void AMissionGoalMarkerActor::BeginPlay()
{
	Super::BeginPlay(); // the marker's shape and hologram; it registers with the tutorial ...
	if (UTutorialSubsystem* Tutorial = GetWorld() ? GetWorld()->GetSubsystem<UTutorialSubsystem>() : nullptr)
	{
		Tutorial->UnregisterMarker(this); // ... which must not drive it: UMissionGoalsSubsystem shows / hides it
	}
	SetMarkerShown(bGoalShown); // the parent's BeginPlay hid it
}

void AMissionGoalMarkerActor::SetGoalShown(bool bShow)
{
	bGoalShown = bShow;
	if (HasActorBegunPlay())
	{
		SetMarkerShown(bShow);
	}
}
