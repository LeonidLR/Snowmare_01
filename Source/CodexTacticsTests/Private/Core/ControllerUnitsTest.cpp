#include "Misc/AutomationTest.h"
#include "Core/CodexTacticsPlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

// Cleanup 2026-10-15 (docs/port/REFACTOR_CONTROLLER_2026-10.md): ACodexTacticsPlayerController's members now live in one
// translation unit per input area (Private/Core/CodexTacticsPlayerController*.cpp). These nets call world-free members of
// several units on one controller and check the units' source hygiene (every member defined once, the shared LOCTEXT
// namespace closed per file for unity builds, the shared order-lock CVar registered once), so a definition lost or
// duplicated by a future move shows up as a test, not only as a smoke. UE-only, no Godot reference.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FControllerUnitsWorldFreeTest, "CodexTactics.Core.ControllerUnits.WorldFreeMembers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FControllerUnitsWorldFreeTest::RunTest(const FString&)
{
	ACodexTacticsPlayerController* Controller = NewObject<ACodexTacticsPlayerController>();
	if (!TestNotNull(TEXT("controller"), Controller))
	{
		return false;
	}

	// CodexTacticsPlayerController.cpp: constructor (top-down cursor) and the shared modifier queries (no player input).
	TestTrue(TEXT("cursor always visible"), Controller->bShowMouseCursor);
	TestFalse(TEXT("no Ctrl without player input"), Controller->IsAttackModifierDown());
	TestFalse(TEXT("no Alt without player input"), Controller->IsSquadWideModifierDown());

	// CodexTacticsPlayerControllerSelection.cpp: only 1-4 select (keys by name: the tests module does not link InputCore); no
	// box while LMB is up.
	TestFalse(TEXT("key 5 selects nobody"), Controller->HandleSquadNumberKey(FKey(FName(TEXT("Five")))));
	TestFalse(TEXT("key A selects nobody"), Controller->HandleSquadNumberKey(FKey(FName(TEXT("A")))));
	FVector2D Min(1.f, 2.f);
	FVector2D Max(3.f, 4.f);
	TestFalse(TEXT("no selection box"), Controller->GetSelectionBox(Min, Max));
	TestEqual(TEXT("box min untouched"), Min, FVector2D(1.f, 2.f));
	TestEqual(TEXT("box max untouched"), Max, FVector2D(3.f, 4.f));

	// CodexTacticsPlayerControllerOrders.cpp: no game flow -> never locked; the order-lock CVar (defined there, extern in the
	// internal header for the cover unit) is registered with its RTS default 1.
	TestFalse(TEXT("orders not locked without a game flow"), Controller->IsRealTimeOrderLocked());
	const IConsoleVariable* RealTimeOrders = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.RealTimeOrders"));
	if (TestNotNull(TEXT("Codex.RealTimeOrders registered"), RealTimeOrders))
	{
		TestEqual(TEXT("Codex.RealTimeOrders default = 1 (orders run at once in real time)"), RealTimeOrders->GetInt(), 1);
	}

	// CodexTacticsPlayerControllerCover.cpp: no ghost spawned yet.
	TestFalse(TEXT("no cover preview"), Controller->HasCoverPreview());
	TestFalse(TEXT("no corner peek preview"), Controller->IsCornerPeekPreviewShown());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FControllerUnitsSourceLayoutTest, "CodexTactics.Core.ControllerUnits.SourceLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FControllerUnitsSourceLayoutTest::RunTest(const FString&)
{
	static const TCHAR* Units[] = {
		TEXT("CodexTacticsPlayerController.cpp"), TEXT("CodexTacticsPlayerControllerSelection.cpp"),
		TEXT("CodexTacticsPlayerControllerOrders.cpp"), TEXT("CodexTacticsPlayerControllerStance.cpp"),
		TEXT("CodexTacticsPlayerControllerItems.cpp"), TEXT("CodexTacticsPlayerControllerTime.cpp"),
		TEXT("CodexTacticsPlayerControllerMenus.cpp"), TEXT("CodexTacticsPlayerControllerCamera.cpp"),
		TEXT("CodexTacticsPlayerControllerCover.cpp"), TEXT("CodexTacticsPlayerControllerMap.cpp") };
	const FString CoreDir = FPaths::ConvertRelativePathToFull(FPaths::GameSourceDir() / TEXT("CodexTactics/Private/Core"));
	TArray<FString> Found;
	IFileManager::Get().FindFiles(Found, *(CoreDir / TEXT("CodexTacticsPlayerController*.cpp")), true, false);
	if (!TestTrue(FString::Printf(TEXT("controller units found in %s"), *CoreDir), Found.Num() >= static_cast<int32>(UE_ARRAY_COUNT(Units))))
	{
		return false;
	}
	for (const TCHAR* Unit : Units)
	{
		TestTrue(FString::Printf(TEXT("%s exists (layout: docs/port/REFACTOR_CONTROLLER_2026-10.md)"), Unit), Found.Contains(FString(Unit)));
	}

	const FString ClassScope = TEXT("ACodexTacticsPlayerController::");
	TMap<FString, FString> DefinedIn; // member -> unit
	int32 Members = 0;
	int32 RealTimeOrdersCVars = 0;
	for (const FString& Unit : Found)
	{
		FString Text;
		if (!TestTrue(FString::Printf(TEXT("%s readable"), *Unit), FFileHelper::LoadFileToString(Text, *(CoreDir / Unit))))
		{
			continue;
		}
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, false);

		// The class header first (IWYU, one class over several units).
		const FString* FirstInclude = Lines.FindByPredicate([](const FString& Line) { return Line.StartsWith(TEXT("#include")); });
		TestTrue(FString::Printf(TEXT("%s includes its class header first"), *Unit),
			FirstInclude && *FirstInclude == TEXT("#include \"Core/CodexTacticsPlayerController.h\""));

		// Unity builds merge the units: one LOCTEXT namespace (keys unchanged), closed at the end of each file.
		if (Text.Contains(TEXT("LOCTEXT(")))
		{
			TestTrue(FString::Printf(TEXT("%s uses the controller's LOCTEXT namespace"), *Unit),
				Text.Contains(TEXT("#define LOCTEXT_NAMESPACE \"CodexTacticsPlayerController\"")));
			const FString* Last = nullptr;
			for (const FString& Line : Lines)
			{
				if (!Line.TrimStartAndEnd().IsEmpty())
				{
					Last = &Line;
				}
			}
			TestTrue(FString::Printf(TEXT("%s ends with #undef LOCTEXT_NAMESPACE"), *Unit), Last && *Last == TEXT("#undef LOCTEXT_NAMESPACE"));
		}

		// Shared helpers live in CodexTacticsPlayerControllerInternal.h only (no second copy in a unit).
		TestFalse(FString::Printf(TEXT("%s does not define ControllerCommands (internal header)"), *Unit),
			Text.Contains(TEXT("namespace ControllerCommands")));
		if (Text.Contains(TEXT("TEXT(\"Codex.RealTimeOrders\")")))
		{
			++RealTimeOrdersCVars;
			TestEqual(TEXT("Codex.RealTimeOrders defined in the orders unit"), Unit, FString(TEXT("CodexTacticsPlayerControllerOrders.cpp")));
		}

		// Member definitions start at column 0 ("Type ACodexTacticsPlayerController::Name(").
		for (const FString& Line : Lines)
		{
			if (Line.IsEmpty() || FChar::IsWhitespace(Line[0]) || Line[0] == TEXT('#') || Line[0] == TEXT('/') || Line[0] == TEXT('}'))
			{
				continue;
			}
			const int32 Scope = Line.Find(ClassScope);
			const int32 Paren = Scope == INDEX_NONE ? INDEX_NONE : Line.Find(TEXT("("), ESearchCase::CaseSensitive, ESearchDir::FromStart, Scope);
			if (Paren == INDEX_NONE)
			{
				continue;
			}
			const FString Member = Line.Mid(Scope + ClassScope.Len(), Paren - Scope - ClassScope.Len());
			++Members;
			if (const FString* Previous = DefinedIn.Find(Member))
			{
				AddError(FString::Printf(TEXT("%s defined twice: %s and %s"), *Member, **Previous, *Unit));
				continue;
			}
			DefinedIn.Add(Member, Unit);
		}
	}
	TestEqual(TEXT("Codex.RealTimeOrders registered by exactly one unit"), RealTimeOrdersCVars, 1);
	TestTrue(FString::Printf(TEXT("member definitions found (%d)"), Members), Members >= 87);
	TestEqual(TEXT("HandleWorldHit in the orders unit"), DefinedIn.FindRef(TEXT("HandleWorldHit")), FString(TEXT("CodexTacticsPlayerControllerOrders.cpp")));
	TestEqual(TEXT("PlayerTick in the main unit"), DefinedIn.FindRef(TEXT("PlayerTick")), FString(TEXT("CodexTacticsPlayerController.cpp")));
	return true;
}

#endif
