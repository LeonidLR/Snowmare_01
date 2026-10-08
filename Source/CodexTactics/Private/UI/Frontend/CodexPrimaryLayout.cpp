#include "UI/Frontend/CodexPrimaryLayout.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "UI/Frontend/CodexUITags.h"
#include "Widgets/CommonActivatableWidgetContainer.h"

namespace CodexLayoutNames
{
	struct FLayerName
	{
		const TCHAR* Name;
		FGameplayTag Tag;
	};

	TArray<FLayerName> All()
	{
		return {
			{ TEXT("GameLayer"), CodexUITags::Layer_Game },
			{ TEXT("GameMenuLayer"), CodexUITags::Layer_GameMenu },
			{ TEXT("MenuLayer"), CodexUITags::Layer_Menu },
			{ TEXT("ModalLayer"), CodexUITags::Layer_Modal },
		};
	}
}

void UCodexPrimaryLayout::RegisterLayer(FGameplayTag LayerTag, UCommonActivatableWidgetContainerBase* Container)
{
	if (LayerTag.IsValid() && Container)
	{
		Layers.Add(LayerTag, Container);
	}
}

UCommonActivatableWidgetContainerBase* UCodexPrimaryLayout::GetLayer(FGameplayTag LayerTag) const
{
	const TObjectPtr<UCommonActivatableWidgetContainerBase>* Found = Layers.Find(LayerTag);
	return Found ? Found->Get() : nullptr;
}

void UCodexPrimaryLayout::NativeOnInitialized()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildDefaultTree(*WidgetTree);
	}
	Super::NativeOnInitialized();
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	for (const CodexLayoutNames::FLayerName& Layer : CodexLayoutNames::All())
	{
		if (!Layers.Contains(Layer.Tag))
		{
			RegisterLayer(Layer.Tag, Cast<UCommonActivatableWidgetContainerBase>(GetWidgetFromName(Layer.Name)));
		}
	}
}

void UCodexPrimaryLayout::BuildDefaultTree(UWidgetTree& Tree) const
{
	UOverlay* Root = Tree.ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("LayerOverlay"));
	Tree.RootWidget = Root;
	// Never hit-testable itself: with no screen open the game HUD below (clicks, drag & drop) must get every event.
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	for (const CodexLayoutNames::FLayerName& Layer : CodexLayoutNames::All())
	{
		UCommonActivatableWidgetStack* Stack = Tree.ConstructWidget<UCommonActivatableWidgetStack>(UCommonActivatableWidgetStack::StaticClass(), Layer.Name);
		// No transition by default: screens switch at once (the artist can add a fade on the stack in WBP_PrimaryLayout).
		Stack->SetTransitionDuration(0.f);
		CodexDefaultTree::MarkVariable(Stack);
		UOverlaySlot* LayerSlot = Root->AddChildToOverlay(Stack);
		LayerSlot->SetHorizontalAlignment(HAlign_Fill);
		LayerSlot->SetVerticalAlignment(VAlign_Fill);
	}
}
