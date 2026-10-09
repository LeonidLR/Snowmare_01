#include "UI/Frontend/CodexFrontendPlayerController.h"
#include "CodexTactics.h"
#include "EngineUtils.h"
#include "GameFramework/HUD.h"
#include "UI/Frontend/CodexFrontendRules.h"
#include "UI/Frontend/CodexFrontendSettings.h"
#include "UI/Frontend/CodexFrontendSubsystem.h"
#include "UI/Frontend/CodexMenuCameraAnchor.h"
#include "UI/Frontend/CodexUISubsystem.h"
#include "UI/Frontend/CodexUITags.h"

ACodexFrontendPlayerController::ACodexFrontendPlayerController()
{
	bShowMouseCursor = true;
	bAutoManageActiveCameraTarget = false; // the menu anchors own the view
}

void ACodexFrontendPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController())
	{
		return;
	}
	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);

	if (UCodexUISubsystem* UI = UCodexUISubsystem::Get(this))
	{
		UI->GetOrCreateLayout(this);
	}
	UCodexFrontendSubsystem* Frontend = UCodexFrontendSubsystem::Get(this);
	if (Frontend && Frontend->ConsumeSkipTitle())
	{
		ShowMainMenu();
	}
	else
	{
		ShowTitle();
	}
}

ACodexMenuCameraAnchor* ACodexFrontendPlayerController::FindAnchor(FName AnchorId) const
{
	for (TActorIterator<ACodexMenuCameraAnchor> It(GetWorld()); It; ++It)
	{
		if (It->AnchorId == AnchorId)
		{
			return *It;
		}
	}
	return nullptr;
}

bool ACodexFrontendPlayerController::FocusCameraAnchor(FName AnchorId, bool bInstant)
{
	if (AnchorId.IsNone() || (AnchorId == CurrentAnchorId && !bInstant))
	{
		return AnchorId == CurrentAnchorId;
	}
	ACodexMenuCameraAnchor* Anchor = FindAnchor(AnchorId);
	if (!Anchor)
	{
		UE_LOG(LogCodexTactics, Verbose, TEXT("Frontend: no camera anchor '%s'"), *AnchorId.ToString());
		return false;
	}
	const UCodexFrontendSettings& Settings = UCodexFrontendSettings::Get();
	const float BlendTime = bInstant ? 0.f : CodexFrontendRules::ResolveBlendTime(Anchor->BlendTimeOverride, Settings.CameraBlendTime);
	SetViewTargetWithBlend(Anchor, BlendTime, Settings.CameraBlendFunction, Settings.CameraBlendExp, false);
	CurrentAnchorId = AnchorId;
	return true;
}

void ACodexFrontendPlayerController::ShowTitle()
{
	UCodexUISubsystem* UI = UCodexUISubsystem::Get(this);
	if (!UI)
	{
		return;
	}
	UI->ClearLayer(CodexUITags::Layer_Menu);
	UI->PushScreen(CodexUITags::Layer_Menu, CodexUITags::Screen_Title);
	FocusCameraAnchor(UCodexFrontendSettings::Get().TitleAnchorId, CurrentAnchorId.IsNone());
}

void ACodexFrontendPlayerController::ShowMainMenu()
{
	UCodexUISubsystem* UI = UCodexUISubsystem::Get(this);
	if (!UI)
	{
		return;
	}
	const bool bFirstView = CurrentAnchorId.IsNone();
	UI->ClearLayer(CodexUITags::Layer_Menu);
	UI->PushScreen(CodexUITags::Layer_Menu, CodexUITags::Screen_MainMenu);
	if (bFirstView)
	{
		FocusCameraAnchor(UCodexFrontendSettings::Get().TitleAnchorId, true);
	}
}

ACodexFrontendGameMode::ACodexFrontendGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = ACodexFrontendPlayerController::StaticClass();
	HUDClass = AHUD::StaticClass();
}
