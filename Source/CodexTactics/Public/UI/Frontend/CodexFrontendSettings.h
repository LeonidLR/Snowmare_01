#pragma once

#include "CoreMinimal.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "CodexFrontendSettings.generated.h"

class UCodexActivatableScreen;
class UCodexPrimaryLayout;
class UWorld;

/**
 * Project Settings > Game > Codex Frontend: which Widget Blueprint is used for every frontend / pause screen, the
 * frontend and new-game levels and the menu camera blend. The artist swaps a WBP here without code.
 * Missing classes fall back to the C++ base class (it builds a plain default layout).
 * No Godot reference (2026-10-08 user decision: professional frontend).
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Codex Frontend"))
class CODEXTACTICS_API UCodexFrontendSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UCodexFrontendSettings();

	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	static const UCodexFrontendSettings& Get() { return *GetDefault<UCodexFrontendSettings>(); }

	/** Root widget with the four layer stacks (Game, GameMenu, Menu, Modal). */
	UPROPERTY(Config, EditAnywhere, Category = "Widgets")
	TSoftClassPtr<UCodexPrimaryLayout> PrimaryLayoutClass;

	/** Screen tag (Codex.UI.Screen.*) -> Widget Blueprint. */
	UPROPERTY(Config, EditAnywhere, Category = "Widgets", meta = (Categories = "Codex.UI.Screen"))
	TMap<FGameplayTag, TSoftClassPtr<UCodexActivatableScreen>> ScreenClasses;

	/** Viewport Z-order of the primary layout (above the in-game HUD widgets, which use 0-41). */
	UPROPERTY(Config, EditAnywhere, Category = "Widgets")
	int32 LayoutZOrder = 100;

	/** The frontend map (title / main menu); QUIT TO MAIN MENU opens it. */
	UPROPERTY(Config, EditAnywhere, Category = "Levels")
	TSoftObjectPtr<UWorld> FrontendLevel;

	/** NEW GAME opens this level at its start (the level's own encounter flow runs); also the fallback level for saves. */
	UPROPERTY(Config, EditAnywhere, Category = "Levels")
	TSoftObjectPtr<UWorld> NewGameLevel;

	/** Default camera blend between menu camera anchors (seconds; an anchor can override it). */
	UPROPERTY(Config, EditAnywhere, Category = "Camera", meta = (ClampMin = "0"))
	float CameraBlendTime = 1.2f;

	UPROPERTY(Config, EditAnywhere, Category = "Camera")
	TEnumAsByte<EViewTargetBlendFunction> CameraBlendFunction = VTBlend_EaseInOut;

	/** Exponent for the ease blend functions. */
	UPROPERTY(Config, EditAnywhere, Category = "Camera", meta = (ClampMin = "0.1"))
	float CameraBlendExp = 2.f;

	/** Anchor id the title screen looks through. */
	UPROPERTY(Config, EditAnywhere, Category = "Camera")
	FName TitleAnchorId = TEXT("Title");
};
