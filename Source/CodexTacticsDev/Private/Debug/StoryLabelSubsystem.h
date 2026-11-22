#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "StoryLabelSubsystem.generated.h"

class APlayerController;
class UCanvas;

/**
 * Story-showcase labels (designer test 2026-11-22, item 6; dev / test overlay, never shipped): above every house of a generated map its
 * story ("3 · Запертая дверь", from the map's levelgen layout: buildings[].stories), above every shut door its state ("заперта" / "закрыта" /
 * "заколочена" / "завалена", from the builder's leaf tags door_state=; with the openable doors on the live state and the strength, "открыта"
 * once opened) and the story that set it. Drawn on the game canvas (UDebugDrawService "Game": in PIE, in -game and in screenshots). Console
 * Codex.Debug.StoryLabels: -1 (default) = on the story-showcase maps only, 0 = off, 1 = on any generated map. A door label of a storey the
 * storey cutaway hides is hidden (StoryLabelRules::IsDoorLabelHidden: inside a house only his storey's doors; outside the ground storey's
 * doors and the doors a story set); the house label stays above the roof position.
 * Pure rules: StoryLabelRules (tests CodexTactics.Debug.StoryLabels.*). UE-only (no Godot reference).
 */
UCLASS()
class UStoryLabelSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** The labels are built and drawn now. */
	bool IsShown() const { return bBuilt; }
	/** House / door labels made (smokes). */
	int32 GetHouseLabelCount() const;
	int32 GetDoorLabelCount() const;
	/** The labels drawn on the last frame (on screen, not hidden by the cutaway). */
	int32 GetLastDrawnCount() const { return LastDrawn; }
	/** The texts of the labels (smokes / the log). */
	TArray<FString> GetLabelTexts(bool bHouses) const;

private:
	struct FLabel
	{
		FVector World = FVector::ZeroVector;
		FString Text;
		FString BuildingId;
		FString OpeningId;
		/** The door's leaf state from the builder tags (doors off), the strength and the story that set it. */
		FString BaseState;
		int32 StoryNumber = 0;
		int32 Storey = 0;
		bool bHouse = false;
		bool bHidden = false;
	};

	void Build();
	void Clear();
	void Refresh();
	void Draw(UCanvas* Canvas, APlayerController* Controller);

	TArray<FLabel> Labels;
	FDelegateHandle DrawHandle;
	bool bBuilt = false;
	float RefreshIn = 0.f;
	int32 LastDrawn = 0;
};
