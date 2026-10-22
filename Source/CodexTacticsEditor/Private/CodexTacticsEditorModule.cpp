#include "Modules/ModuleManager.h"
#include "BlueprintGraphToolset.h"
#include "ToolsetRegistry/UToolsetRegistry.h"

/** Editor-only tools of the project: the Blueprint graph toolset for AI agents (served over the Unreal MCP plugin). */
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
	}

	virtual void ShutdownModule() override
	{
		if (bToolsetRegistered)
		{
			UToolsetRegistry::UnregisterToolsetClass(UBlueprintGraphToolset::StaticClass());
		}
	}

	bool bToolsetRegistered = false;
};

IMPLEMENT_MODULE(FCodexTacticsEditorModule, CodexTacticsEditor);
