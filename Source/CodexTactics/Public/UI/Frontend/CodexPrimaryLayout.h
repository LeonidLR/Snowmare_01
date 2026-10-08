#pragma once

#include "CoreMinimal.h"
#include "CommonUserWidget.h"
#include "GameplayTagContainer.h"
#include "UI/Frontend/CodexDefaultTree.h"
#include "CodexPrimaryLayout.generated.h"

class UCommonActivatableWidgetContainerBase;

/**
 * Root widget of the frontend UI: one activatable widget stack per layer (Codex.UI.Layer.Game / GameMenu / Menu / Modal,
 * bottom to top). UCodexUISubsystem pushes screens onto a layer by tag.
 * Widget names (WBP_PrimaryLayout): GameLayer, GameMenuLayer, MenuLayer, ModalLayer — any UCommonActivatableWidgetContainerBase
 * (stack / queue); they register themselves by name. A Blueprint may instead call RegisterLayer in its Construct.
 * No Godot reference (2026-10-08 user decision: professional frontend).
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API UCodexPrimaryLayout : public UCommonUserWidget, public ICodexDefaultTree
{
	GENERATED_BODY()

public:
	/** Registers a layer stack under a Codex.UI.Layer.* tag (replaces an earlier one). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|UI", meta = (Categories = "Codex.UI.Layer"))
	void RegisterLayer(FGameplayTag LayerTag, UCommonActivatableWidgetContainerBase* Container);

	UFUNCTION(BlueprintPure, Category = "CodexTactics|UI", meta = (Categories = "Codex.UI.Layer"))
	UCommonActivatableWidgetContainerBase* GetLayer(FGameplayTag LayerTag) const;

	virtual void BuildDefaultTree(UWidgetTree& Tree) const override;

protected:
	virtual void NativeOnInitialized() override;

private:
	UPROPERTY(Transient)
	TMap<FGameplayTag, TObjectPtr<UCommonActivatableWidgetContainerBase>> Layers;
};
