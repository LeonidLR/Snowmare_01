// CodexTacticsDev: developer-only console commands (smokes, probes, shot commands) moved out of the runtime module
// (audit 2026-10-11 #3). They register through static FAutoConsoleCommand objects when this module loads.

#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, CodexTacticsDev);
