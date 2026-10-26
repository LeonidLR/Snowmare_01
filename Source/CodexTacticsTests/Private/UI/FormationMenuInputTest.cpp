// CodexTactics.UI.FormationMenu.* - the formation radial menu on B (stage 7, user decision 2026-10-26: G is the grenade, the menu
// moved to B): the tap / hold tracker, the layout helpers, the English labels, the icon assets (UI/FormationMenuInputRules).
// UE-only (no Godot reference); design docs/design/FORMATION_SECTORS.md "Selection", contract docs/design/FORMATION_CONTRACT.md 9.

#include "Misc/AutomationTest.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "UI/FormationMenuInputRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define FORMATION_MENU_INPUT_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.UI.FormationMenu." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace FormationMenuInputTestUtil
{
	using EKey = EFormationMenuKeyAction;
	constexpr float Hold = FormationMenuInput::DefaultHoldSeconds;
	constexpr float Frame = 1.f / 60.f;

	/** Ticks Frames frames of 1/60 s; the actions that were not None, in order. */
	TArray<EKey> TickFrames(FFormationMenuKeyTracker& Key, int32 Frames, bool bAvailable)
	{
		TArray<EKey> Fired;
		for (int32 Index = 0; Index < Frames; ++Index)
		{
			const EKey Action = Key.Tick(Frame, Hold, bAvailable);
			if (Action != EKey::None)
			{
				Fired.Add(Action);
			}
		}
		return Fired;
	}
}

FORMATION_MENU_INPUT_TEST(FFormationMenuKeyTapTest, "Key.TapToggles")
bool FFormationMenuKeyTapTest::RunTest(const FString&)
{
	using namespace FormationMenuInputTestUtil;
	FFormationMenuKeyTracker Key;
	TestEqual(TEXT("a release with no press does nothing"), Key.Release(), EKey::None);
	// The smokes' tap: pressed, released on the next frame.
	Key.Press();
	TestTrue(TEXT("one frame: nothing yet"), TickFrames(Key, 1, true).IsEmpty());
	TestEqual(TEXT("released after one frame: the solo / squad toggle"), Key.Release(), EKey::Toggle);
	TestFalse(TEXT("released: not held"), Key.IsPressed());
	// Just under the threshold (14 frames = 0.233 s) is still a tap.
	Key.Press();
	TestTrue(TEXT("0.23 s: the menu does not open"), TickFrames(Key, 14, true).IsEmpty());
	TestEqual(TEXT("0.23 s: a tap"), Key.Release(), EKey::Toggle);
	TestEqual(TEXT("a second release does nothing"), Key.Release(), EKey::None);
	return true;
}

FORMATION_MENU_INPUT_TEST(FFormationMenuKeyHoldTest, "Key.HoldOpensOnceAndPicks")
bool FFormationMenuKeyHoldTest::RunTest(const FString&)
{
	using namespace FormationMenuInputTestUtil;
	FFormationMenuKeyTracker Key;
	Key.Press();
	TestTrue(TEXT("0.1 s: closed"), Key.Tick(0.1f, Hold, true) == EKey::None);
	TestTrue(TEXT("exactly the hold time: opens"), Key.Tick(0.15f, Hold, true) == EKey::Open && Key.IsMenuOpen());
	TestTrue(TEXT("opens once per press"), TickFrames(Key, 120, true).IsEmpty());
	TestEqual(TEXT("released while open: pick"), Key.Release(), EKey::Pick);
	TestFalse(TEXT("closed after the pick"), Key.IsMenuOpen());
	// A negative / zero delta never advances the hold.
	Key.Press();
	TestTrue(TEXT("negative delta: nothing"), Key.Tick(-5.f, Hold, true) == EKey::None && Key.GetHeldTime() == 0.f);
	TestEqual(TEXT("still a tap"), Key.Release(), EKey::Toggle);
	// The threshold is the caller's (the controller's FormationMenuHoldSeconds).
	Key.Press();
	TestTrue(TEXT("hold 0.5: not at 0.3"), Key.Tick(0.3f, 0.5f, true) == EKey::None);
	TestTrue(TEXT("hold 0.5: at 0.5"), Key.Tick(0.2f, 0.5f, true) == EKey::Open);
	TestEqual(TEXT("pick"), Key.Release(), EKey::Pick);
	return true;
}

FORMATION_MENU_INPUT_TEST(FFormationMenuKeyUnavailableTest, "Key.UnavailableHoldIsATap")
bool FFormationMenuKeyUnavailableTest::RunTest(const FString&)
{
	using namespace FormationMenuInputTestUtil;
	FFormationMenuKeyTracker Key;
	// Solo / one member / turn-based: a long hold never opens the menu, the release still toggles (B never does nothing).
	Key.Press();
	TestTrue(TEXT("1 s unavailable: no menu"), TickFrames(Key, 60, false).IsEmpty());
	TestEqual(TEXT("released: the toggle"), Key.Release(), EKey::Toggle);
	// Available while still held (e.g. the dialogue closed): it opens then.
	Key.Press();
	TestTrue(TEXT("0.5 s unavailable"), TickFrames(Key, 30, false).IsEmpty());
	TestTrue(TEXT("becomes available: opens at once"), Key.Tick(Frame, Hold, true) == EKey::Open);
	TestEqual(TEXT("pick"), Key.Release(), EKey::Pick);
	return true;
}

FORMATION_MENU_INPUT_TEST(FFormationMenuKeyCloseTest, "Key.CancelSpendsTheRelease")
bool FFormationMenuKeyCloseTest::RunTest(const FString&)
{
	using namespace FormationMenuInputTestUtil;
	FFormationMenuKeyTracker Key;
	// Close with no open menu changes nothing.
	Key.Press();
	Key.Close();
	TestEqual(TEXT("Close before the menu opened: still a tap"), Key.Release(), EKey::Toggle);
	// Esc / RMB while open: the later release neither picks nor toggles.
	Key.Press();
	TestTrue(TEXT("opened"), TickFrames(Key, 20, true) == TArray<EKey>({ EKey::Open }));
	Key.Close();
	TestFalse(TEXT("closed"), Key.IsMenuOpen());
	TestTrue(TEXT("never reopens in the same press"), TickFrames(Key, 60, true).IsEmpty());
	TestEqual(TEXT("release after the cancel: nothing"), Key.Release(), EKey::None);
	// The next press is a fresh one.
	Key.Press();
	TestEqual(TEXT("next tap toggles again"), Key.Release(), EKey::Toggle);
	return true;
}

FORMATION_MENU_INPUT_TEST(FFormationMenuLayoutTest, "Layout")
bool FFormationMenuLayoutTest::RunTest(const FString&)
{
	const FVector2D View(1920.0, 1080.0);
	const FVector2D Half(350.0, 260.0);
	TestEqual(TEXT("cursor well inside: unchanged"), FormationMenuInput::ClampCentre(FVector2D(900.0, 500.0), View, Half, 16.f), FVector2D(900.0, 500.0));
	TestEqual(TEXT("top-left corner: pulled in"), FormationMenuInput::ClampCentre(FVector2D(5.0, 5.0), View, Half, 16.f), FVector2D(366.0, 276.0));
	TestEqual(TEXT("bottom-right corner: pulled in"), FormationMenuInput::ClampCentre(FVector2D(1919.0, 1079.0), View, Half, 16.f), FVector2D(1554.0, 804.0));
	TestEqual(TEXT("view smaller than the menu: its centre"), FormationMenuInput::ClampCentre(FVector2D(10.0, 10.0), FVector2D(600.0, 400.0), Half, 16.f),
		FVector2D(300.0, 200.0));

	// Directions: 12 up, clockwise, y down; the item drawn there is the one ItemAt picks there.
	TestTrue(TEXT("line at 12"), FormationMenuInput::ItemDirection(EFormationMenuItem::Line).Equals(FVector2D(0.0, -1.0), 1e-6));
	TestTrue(TEXT("echelon right at 3"), FormationMenuInput::ItemDirection(EFormationMenuItem::EchelonRight).Equals(FVector2D(1.0, 0.0), 1e-6));
	TestTrue(TEXT("column at 6"), FormationMenuInput::ItemDirection(EFormationMenuItem::Column).Equals(FVector2D(0.0, 1.0), 1e-6));
	TestTrue(TEXT("echelon left at 9"), FormationMenuInput::ItemDirection(EFormationMenuItem::EchelonLeft).Equals(FVector2D(-1.0, 0.0), 1e-6));
	for (int32 Index = 0; Index < FormationMenuRules::ItemCount; ++Index)
	{
		const EFormationMenuItem Item = static_cast<EFormationMenuItem>(Index);
		const FVector2D At = FormationMenuInput::ItemDirection(Item) * 150.0;
		TestTrue(FString::Printf(TEXT("%s: drawn where ItemAt picks it"), FormationMenuRules::ToString(Item)),
			FormationMenuRules::ItemAt(static_cast<float>(At.X), static_cast<float>(At.Y), 40.f) == TOptional<EFormationMenuItem>(Item));
	}
	return true;
}

FORMATION_MENU_INPUT_TEST(FFormationMenuLabelsTest, "LabelsAndIcons")
bool FFormationMenuLabelsTest::RunTest(const FString&)
{
	using EI = EFormationMenuItem;
	auto Label = [](const TOptional<EI>& Item, int32 Count) { return FormationMenuInput::Label(Item, Count).ToString(); };
	TestEqual(TEXT("centre"), Label(TOptional<EI>(), 4), FString(TEXT("Auto")));
	TestEqual(TEXT("12"), Label(EI::Line, 4), FString(TEXT("Line")));
	TestEqual(TEXT("diamond with 4"), Label(EI::Diamond, 4), FString(TEXT("Diamond")));
	TestEqual(TEXT("diamond with 3 = the wedge"), Label(EI::Diamond, 3), FString(TEXT("Wedge")));
	TestEqual(TEXT("diamond with 2 = the pair"), Label(EI::Diamond, 2), FString(TEXT("Pair")));
	TestEqual(TEXT("3"), Label(EI::EchelonRight, 4), FString(TEXT("Echelon right")));
	TestEqual(TEXT("4:30"), Label(EI::Blizzard, 4), FString(TEXT("Blizzard diamond")));
	TestEqual(TEXT("6"), Label(EI::Column, 4), FString(TEXT("Column")));
	TestEqual(TEXT("7:30"), Label(EI::Hedgehog, 4), FString(TEXT("Hedgehog")));
	TestEqual(TEXT("9"), Label(EI::EchelonLeft, 4), FString(TEXT("Echelon left")));
	TestEqual(TEXT("10:30"), Label(EI::AllForward, 4), FString(TEXT("All forward")));

	// One icon per item + Auto: the SVG source and the imported texture (Scripts/Editor/import_formation_menu_icons.py).
	const FString Folder = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("UI/FormationMenu"));
	for (int32 Index = 0; Index <= FormationMenuRules::ItemCount; ++Index)
	{
		const TOptional<EI> Item = Index < FormationMenuRules::ItemCount ? TOptional<EI>(static_cast<EI>(Index)) : TOptional<EI>();
		const FString Id = Item.IsSet() ? FormationMenuRules::ToString(Item.GetValue()) : TEXT("auto");
		const FString Asset = FormationMenuInput::IconAssetName(Item);
		TestEqual(FString::Printf(TEXT("%s icon name"), *Id), Asset, FString::Printf(TEXT("T_FormationMenu_%s"), *Id));
		TestTrue(FString::Printf(TEXT("%s svg"), *Id), IFileManager::Get().FileExists(*(Folder / TEXT("src") / (Id + TEXT(".svg")))));
		TestTrue(FString::Printf(TEXT("%s texture imported"), *Id), IFileManager::Get().FileExists(*(Folder / (Asset + TEXT(".uasset")))));
	}
	return true;
}

#undef FORMATION_MENU_INPUT_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
