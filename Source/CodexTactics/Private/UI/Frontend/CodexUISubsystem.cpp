#include "UI/Frontend/CodexUISubsystem.h"
#include "CodexTactics.h"
#include "Engine/AssetManager.h"
#include "Engine/GameInstance.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "UI/Frontend/CodexConfirmDialog.h"
#include "UI/Frontend/CodexFrontendSettings.h"
#include "UI/Frontend/CodexInfoScreens.h"
#include "UI/Frontend/CodexMainMenuScreen.h"
#include "UI/Frontend/CodexPauseMenuScreen.h"
#include "UI/Frontend/CodexPrimaryLayout.h"
#include "UI/Frontend/CodexSaveSlotsScreen.h"
#include "UI/Frontend/CodexTitleScreen.h"
#include "UI/Frontend/CodexUITags.h"
#include "Widgets/CommonActivatableWidgetContainer.h"

namespace CodexUILayers
{
	/** Layer tags from the topmost (Modal) down. */
	TArray<FGameplayTag> TopDown()
	{
		return { CodexUITags::Layer_Modal.GetTag(), CodexUITags::Layer_Menu.GetTag(), CodexUITags::Layer_GameMenu.GetTag(), CodexUITags::Layer_Game.GetTag() };
	}
}

UCodexUISubsystem* UCodexUISubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UCodexUISubsystem>() : nullptr;
}

UCodexPrimaryLayout* UCodexUISubsystem::GetLayout() const
{
	UCodexPrimaryLayout* Current = Layout.Get();
	// A layout of a previous world (level travel) is gone with its world.
	return Current && Current->GetWorld() == GetGameInstance()->GetWorld() && Current->IsInViewport() ? Current : nullptr;
}

UCodexPrimaryLayout* UCodexUISubsystem::GetOrCreateLayout(APlayerController* PlayerController)
{
	if (UCodexPrimaryLayout* Existing = GetLayout())
	{
		return Existing;
	}
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return nullptr;
	}
	const UCodexFrontendSettings& Settings = UCodexFrontendSettings::Get();
	TSubclassOf<UCodexPrimaryLayout> LayoutClass = Settings.PrimaryLayoutClass.IsNull() ? nullptr : Settings.PrimaryLayoutClass.LoadSynchronous();
	if (!LayoutClass)
	{
		LayoutClass = UCodexPrimaryLayout::StaticClass();
	}
	UCodexPrimaryLayout* Created = CreateWidget<UCodexPrimaryLayout>(PlayerController, LayoutClass);
	if (!Created)
	{
		return nullptr;
	}
	Created->AddToViewport(Settings.LayoutZOrder);
	Layout = Created;
	PreloadScreenClasses();
	UE_LOG(LogCodexTactics, Log, TEXT("Frontend: primary layout %s created"), *LayoutClass->GetName());
	return Created;
}

void UCodexUISubsystem::PreloadScreenClasses()
{
	if (PreloadHandle.IsValid())
	{
		return;
	}
	TArray<FSoftObjectPath> Paths;
	for (const TPair<FGameplayTag, TSoftClassPtr<UCodexActivatableScreen>>& Entry : UCodexFrontendSettings::Get().ScreenClasses)
	{
		if (!Entry.Value.IsNull() && !Entry.Value.Get())
		{
			Paths.Add(Entry.Value.ToSoftObjectPath());
		}
	}
	if (!Paths.IsEmpty())
	{
		PreloadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths, FStreamableDelegate());
	}
}

UCommonActivatableWidgetContainerBase* UCodexUISubsystem::GetLayerContainer(FGameplayTag LayerTag) const
{
	const UCodexPrimaryLayout* Current = GetLayout();
	return Current ? Current->GetLayer(LayerTag) : nullptr;
}

TSubclassOf<UCodexActivatableScreen> UCodexUISubsystem::GetNativeScreenClass(FGameplayTag ScreenTag)
{
	if (ScreenTag == CodexUITags::Screen_Title) return UCodexTitleScreen::StaticClass();
	if (ScreenTag == CodexUITags::Screen_MainMenu) return UCodexMainMenuScreen::StaticClass();
	if (ScreenTag == CodexUITags::Screen_SaveSlots) return UCodexSaveSlotsScreen::StaticClass();
	if (ScreenTag == CodexUITags::Screen_Options) return UCodexOptionsScreen::StaticClass();
	if (ScreenTag == CodexUITags::Screen_Credits) return UCodexCreditsScreen::StaticClass();
	if (ScreenTag == CodexUITags::Screen_Pause) return UCodexPauseMenuScreen::StaticClass();
	if (ScreenTag == CodexUITags::Screen_Confirm) return UCodexConfirmDialog::StaticClass();
	return nullptr;
}

void UCodexUISubsystem::PushScreen(FGameplayTag LayerTag, FGameplayTag ScreenTag, TFunction<void(UCodexActivatableScreen&)> InitFunc,
	TFunction<void(UCodexActivatableScreen*)> OnPushed)
{
	const TSoftClassPtr<UCodexActivatableScreen>* Configured = UCodexFrontendSettings::Get().ScreenClasses.Find(ScreenTag);
	const FSoftObjectPath Path = Configured ? Configured->ToSoftObjectPath() : FSoftObjectPath();
	if (Path.IsNull())
	{
		PushLoadedClass(LayerTag, ScreenTag, nullptr, MoveTemp(InitFunc), MoveTemp(OnPushed));
		return;
	}
	if (UClass* Loaded = Cast<UClass>(Path.ResolveObject()))
	{
		PushLoadedClass(LayerTag, ScreenTag, Loaded, MoveTemp(InitFunc), MoveTemp(OnPushed));
		return;
	}
	// Soft class not in memory: load it in the background, push when it arrives (the C++ class if the asset is missing).
	TWeakObjectPtr<UCodexUISubsystem> WeakThis(this);
	UAssetManager::GetStreamableManager().RequestAsyncLoad(Path,
		FStreamableDelegate::CreateLambda([WeakThis, LayerTag, ScreenTag, Path, InitFunc = MoveTemp(InitFunc), OnPushed = MoveTemp(OnPushed)]() mutable
		{
			if (UCodexUISubsystem* Self = WeakThis.Get())
			{
				Self->PushLoadedClass(LayerTag, ScreenTag, Cast<UClass>(Path.ResolveObject()), MoveTemp(InitFunc), MoveTemp(OnPushed));
			}
		}));
}

void UCodexUISubsystem::PushLoadedClass(FGameplayTag LayerTag, FGameplayTag ScreenTag, UClass* LoadedClass,
	TFunction<void(UCodexActivatableScreen&)> InitFunc, TFunction<void(UCodexActivatableScreen*)> OnPushed)
{
	TSubclassOf<UCodexActivatableScreen> ScreenClass = LoadedClass && LoadedClass->IsChildOf(UCodexActivatableScreen::StaticClass())
		? TSubclassOf<UCodexActivatableScreen>(LoadedClass) : GetNativeScreenClass(ScreenTag);
	if (!LoadedClass && ScreenClass)
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("Frontend: no Widget Blueprint for %s, using the C++ placeholder %s"), *ScreenTag.ToString(), *ScreenClass->GetName());
	}
	UCommonActivatableWidgetContainerBase* Container = GetLayerContainer(LayerTag);
	if (!Container)
	{
		if (APlayerController* PC = GetGameInstance()->GetFirstLocalPlayerController(GetGameInstance()->GetWorld()))
		{
			GetOrCreateLayout(PC);
			Container = GetLayerContainer(LayerTag);
		}
	}
	if (!Container || !ScreenClass)
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("Frontend: cannot push %s on %s (layer or class missing)"), *ScreenTag.ToString(), *LayerTag.ToString());
		if (OnPushed)
		{
			OnPushed(nullptr);
		}
		return;
	}
	UCodexActivatableScreen* Screen = Container->AddWidget<UCodexActivatableScreen>(ScreenClass, [&](UCodexActivatableScreen& Instance)
	{
		Instance.SetScreenTag(ScreenTag);
		if (InitFunc)
		{
			InitFunc(Instance);
		}
	});
	UE_LOG(LogCodexTactics, Log, TEXT("Frontend: pushed %s (%s) on %s"), *ScreenTag.ToString(), *ScreenClass->GetName(), *LayerTag.ToString());
	if (OnPushed)
	{
		OnPushed(Screen);
	}
}

UCodexActivatableScreen* UCodexUISubsystem::GetTopScreen(FGameplayTag LayerTag) const
{
	const UCommonActivatableWidgetContainerBase* Container = GetLayerContainer(LayerTag);
	if (!Container || Container->GetWidgetList().IsEmpty())
	{
		return nullptr;
	}
	return Cast<UCodexActivatableScreen>(Container->GetWidgetList().Last());
}

int32 UCodexUISubsystem::GetScreenCount(FGameplayTag LayerTag) const
{
	const UCommonActivatableWidgetContainerBase* Container = GetLayerContainer(LayerTag);
	return Container ? Container->GetNumWidgets() : 0;
}

UCodexActivatableScreen* UCodexUISubsystem::FindScreen(FGameplayTag ScreenTag) const
{
	for (const FGameplayTag& Layer : CodexUILayers::TopDown())
	{
		if (const UCommonActivatableWidgetContainerBase* Container = GetLayerContainer(Layer))
		{
			for (UCommonActivatableWidget* Widget : Container->GetWidgetList())
			{
				UCodexActivatableScreen* Screen = Cast<UCodexActivatableScreen>(Widget);
				if (Screen && Screen->GetScreenTag() == ScreenTag)
				{
					return Screen;
				}
			}
		}
	}
	return nullptr;
}

void UCodexUISubsystem::RemoveScreen(UCodexActivatableScreen& Screen)
{
	for (const FGameplayTag& Layer : CodexUILayers::TopDown())
	{
		if (UCommonActivatableWidgetContainerBase* Container = GetLayerContainer(Layer); Container && Container->GetWidgetList().Contains(&Screen))
		{
			Container->RemoveWidget(Screen);
			return;
		}
	}
}

void UCodexUISubsystem::ClearLayer(FGameplayTag LayerTag)
{
	if (UCommonActivatableWidgetContainerBase* Container = GetLayerContainer(LayerTag))
	{
		Container->ClearWidgets();
	}
}

bool UCodexUISubsystem::HandleBackOnTopScreen()
{
	for (const FGameplayTag& Layer : CodexUILayers::TopDown())
	{
		if (UCodexActivatableScreen* Top = GetTopScreen(Layer))
		{
			Top->RequestBack();
			return true;
		}
	}
	return false;
}

bool UCodexUISubsystem::IsAnyMenuOpen() const
{
	return GetScreenCount(CodexUITags::Layer_GameMenu) > 0 || GetScreenCount(CodexUITags::Layer_Menu) > 0 || GetScreenCount(CodexUITags::Layer_Modal) > 0;
}

void UCodexUISubsystem::ShowConfirm(ECodexConfirmType Type, const FText& Title, const FText& Message, TFunction<void(ECodexConfirmResult)> OnResult)
{
	TSharedPtr<TFunction<void(ECodexConfirmResult)>> Shared = MakeShared<TFunction<void(ECodexConfirmResult)>>(MoveTemp(OnResult));
	PushScreen(CodexUITags::Layer_Modal, CodexUITags::Screen_Confirm, [Type, Title, Message, Shared](UCodexActivatableScreen& Screen)
	{
		if (UCodexConfirmDialog* Dialog = Cast<UCodexConfirmDialog>(&Screen))
		{
			Dialog->Setup(Type, Title, Message, [Shared](ECodexConfirmResult Result)
			{
				if (*Shared)
				{
					(*Shared)(Result);
				}
			});
		}
	});
}

void UCodexUISubsystem::BP_ShowConfirm(ECodexConfirmType Type, FText Title, FText Message, FCodexConfirmResultDelegate OnResult)
{
	ShowConfirm(Type, Title, Message, [OnResult](ECodexConfirmResult Result) { OnResult.ExecuteIfBound(Result); });
}
