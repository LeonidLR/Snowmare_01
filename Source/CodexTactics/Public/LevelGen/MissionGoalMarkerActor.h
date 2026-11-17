#pragma once

#include "CoreMinimal.h"
#include "Tutorial/TutorialMarkerActor.h"
#include "MissionGoalMarkerActor.generated.h"

/**
 * The world marker of one mission goal of a generated map (F-4 «а», designer 2026-11-17): the tutorial's orange hologram ring + beacon
 * (ATutorialMarkerActor: engine shapes wearing MI_HoloMarker_Orange, no asset of its own), spawned by UMissionGoalsSubsystem at the
 * goal and shown while the goal is unreached; hidden once reached. It never registers with the tutorial (the tutorial would hide it:
 * it shows only its own steps' markers). The minimap draws every shown ATutorialMarkerActor as an objective icon, so the goal is on
 * the minimap too and leaves it when reached. UE-only.
 */
UCLASS(NotBlueprintable)
class CODEXTACTICS_API AMissionGoalMarkerActor : public ATutorialMarkerActor
{
	GENERATED_BODY()

public:
	AMissionGoalMarkerActor();

	virtual void BeginPlay() override;

	/** The goal id this marker stands for ("GOAL_1"). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "CodexTactics|MissionGoals")
	FString GoalId;
};
