#include "Modules/ModuleManager.h"
#include "BlueprintGraphToolset.h"
#include "CodexCookRules.h"
#include "Core/ActorLabelTag.h"
#include "GameDelegates.h"
#include "GameFramework/Actor.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/UObjectGlobals.h"

/**
 * Editor-only tools of the project: the Blueprint graph toolset for AI agents (served over the Unreal MCP plugin), and the
 * cook hook that keeps placed actors' labels in a packaged game (ActorLabelTag: the tutorial / level data select actors
 * by their Outliner label, which is editor-only data).
 */
class FCodexTacticsEditorModule : public IModuleInterface
{
	virtual void StartupModule() override
	{
		// Commandlets (e.g. -run=CodexTacticsEditor.CodexBuildNav) load this module before the toolset registry subsystem
		// exists: registering there logs an error and fails the commandlet. The toolset only serves the interactive editor.
		bToolsetRegistered = !IsRunningCommandlet();
		if (bToolsetRegistered)
		{
			UToolsetRegistry::RegisterToolsetClass(UBlueprintGraphToolset::StaticClass());
		}
		PreSaveHandle = FCoreUObjectDelegates::OnObjectPreSave.AddStatic(&FCodexTacticsEditorModule::StampLabelWhenCooking);
		ModifyCookHandle = FGameDelegates::Get().GetModifyCookDelegate().AddStatic(&CodexCookRules::AddNativeSoftReferences);
	}

	virtual void ShutdownModule() override
	{
		FGameDelegates::Get().GetModifyCookDelegate().Remove(ModifyCookHandle);
		FCoreUObjectDelegates::OnObjectPreSave.Remove(PreSaveHandle);
		if (bToolsetRegistered)
		{
			UToolsetRegistry::UnregisterToolsetClass(UBlueprintGraphToolset::StaticClass());
		}
	}

	/** While cooking, a placed actor gets its label as a "Label=<label>" tag (only the cooked copy is written; the map is not). */
	static void StampLabelWhenCooking(UObject* Object, FObjectPreSaveContext Context)
	{
		AActor* Actor = Context.IsCooking() ? Cast<AActor>(Object) : nullptr;
		if (!Actor || Actor->IsTemplate() || !Actor->GetLevel())
		{
			return;
		}
		const FName Tag = ActorLabelTag::MakeTag(Actor->GetActorLabel(false));
		if (!Tag.IsNone())
		{
			Actor->Tags.AddUnique(Tag);
		}
	}

	bool bToolsetRegistered = false;
	FDelegateHandle PreSaveHandle;
	FDelegateHandle ModifyCookHandle;
};

IMPLEMENT_MODULE(FCodexTacticsEditorModule, CodexTacticsEditor);
