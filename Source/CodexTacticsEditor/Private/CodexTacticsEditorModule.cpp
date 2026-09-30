#include "Modules/ModuleManager.h"
#include "BlueprintGraphToolset.h"
#include "ToolsetRegistry/UToolsetRegistry.h"

/** Editor-only tools of the project: the Blueprint graph toolset for AI agents (served over the Unreal MCP plugin). */
class FCodexTacticsEditorModule : public IModuleInterface
{
	virtual void StartupModule() override
	{
		UToolsetRegistry::RegisterToolsetClass(UBlueprintGraphToolset::StaticClass());
	}

	virtual void ShutdownModule() override
	{
		UToolsetRegistry::UnregisterToolsetClass(UBlueprintGraphToolset::StaticClass());
	}
};

IMPLEMENT_MODULE(FCodexTacticsEditorModule, CodexTacticsEditor);
