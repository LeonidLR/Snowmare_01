#include "Misc/AutomationTest.h"
#include "Blueprint/WidgetTree.h"
#include "Core/PlayerCommands.h"
#include "UI/Frontend/CodexActivatableScreen.h"
#include "UI/Frontend/CodexControlsRules.h"
#include "UI/Frontend/CodexControlsScreen.h"
#include "UI/Frontend/CodexFrontendSettings.h"
#include "UI/Frontend/CodexPauseMenuScreen.h"
#include "UI/Frontend/CodexUISubsystem.h"
#include "UI/Frontend/CodexUITags.h"

#if WITH_DEV_AUTOMATION_TESTS

// F1 Controls reference (2026-10-11 user decision, no Godot reference): the screen is generated from the PlayerCommands
// registry, so the registry must be complete and consistent.

#define CONTROLS_TEST(TestClass, TestPath) 	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.UI.Frontend.Controls." TestPath, 		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace ControlsTest
{
	/** Every id declared in PlayerCommands.h (keep in step with the header: the count check below fails when it is not). */
	TArray<FName> AllDeclaredIds()
	{
		return {
		PlayerCommands::CameraPanKeys, PlayerCommands::CameraPanEdge, PlayerCommands::CameraPanDrag, PlayerCommands::CameraRotateKeys,
		PlayerCommands::CameraRotateMouse, PlayerCommands::CameraZoom, PlayerCommands::Move,
		PlayerCommands::Sprint, PlayerCommands::WatchSector, PlayerCommands::SelectMember, PlayerCommands::SelectBox,
		PlayerCommands::Interact, PlayerCommands::StanceStand, PlayerCommands::StanceCrouch, PlayerCommands::StanceProne,
		PlayerCommands::SquadStanceStand, PlayerCommands::SquadStanceCrouch, PlayerCommands::SquadStanceProne, PlayerCommands::SoloOn,
		PlayerCommands::Regroup, PlayerCommands::Guard, PlayerCommands::Autonomy, PlayerCommands::HoldLine,
		PlayerCommands::PosturePassive, PlayerCommands::PostureDefensive, PlayerCommands::PostureAggressive, PlayerCommands::CoverPreview,
		PlayerCommands::CoverEnter, PlayerCommands::CoverCorner, PlayerCommands::CoverShimmy, PlayerCommands::CoverFireMode,
		PlayerCommands::Relocate, PlayerCommands::LightBarrel, PlayerCommands::UseObject, PlayerCommands::UseHeatPack,
		PlayerCommands::UseItem, PlayerCommands::LootCrate, PlayerCommands::LootTake, PlayerCommands::DisarmMine,
		PlayerCommands::DisarmTrap, PlayerCommands::DisarmTripwire, PlayerCommands::SetTrap, PlayerCommands::DeployStart,
		PlayerCommands::DeployMine, PlayerCommands::DeployBarricade, PlayerCommands::DeployTurret, PlayerCommands::DeployTripwire,
		PlayerCommands::DeployOther, PlayerCommands::TargetedShot, PlayerCommands::PriorityTarget, PlayerCommands::GrenadeAim,
		PlayerCommands::GrenadeThrow, PlayerCommands::FlareAim, PlayerCommands::CycleWeapon, PlayerCommands::Reload,
		PlayerCommands::TacticalPause, PlayerCommands::PauseRelease, PlayerCommands::TurnBasedEnter, PlayerCommands::TurnBasedExit,
		PlayerCommands::EndTurn, PlayerCommands::NextUnit, PlayerCommands::TurnBasedAttack, PlayerCommands::TurnBasedOrder,
		PlayerCommands::QuickSave, PlayerCommands::QuickLoad, PlayerCommands::ControlsScreen, PlayerCommands::PauseMenu,
		PlayerCommands::VehicleBoard, PlayerCommands::VehicleDisembark, PlayerCommands::VehicleDrive, PlayerCommands::VehicleTurretFire,
		PlayerCommands::VehicleRepair,
		};
	}
}

CONTROLS_TEST(FControlsRegistryTest, "Registry")
bool FControlsRegistryTest::RunTest(const FString&)
{
	for (const FString& Problem : CodexControlsRules::ValidateRegistry())
	{
		AddError(Problem);
	}
	const TArray<FName> Declared = ControlsTest::AllDeclaredIds();
	TestEqual(TEXT("Every declared command id has a registry row (and the registry has no extras)"), PlayerCommands::GetRegistry().Num(), Declared.Num());
	TSet<FName> Unique;
	for (const FName Id : Declared)
	{
		const FPlayerCommandInfo* Info = PlayerCommands::Find(Id);
		TestNotNull(*FString::Printf(TEXT("%s in the registry"), *Id.ToString()), Info);
		TestFalse(*FString::Printf(TEXT("%s declared once"), *Id.ToString()), Unique.Contains(Id));
		Unique.Add(Id);
	}
	return true;
}

CONTROLS_TEST(FControlsSectionsTest, "Sections")
bool FControlsSectionsTest::RunTest(const FString&)
{
	const TArray<FCodexControlsSection> Sections = CodexControlsRules::BuildSections();
	const TArray<const TCHAR*>& Titles = PlayerCommands::GetSections();
	TestEqual(TEXT("Eight sections, none empty"), Sections.Num(), Titles.Num());
	TestEqual(TEXT("Section count"), Sections.Num(), 8);
	for (int32 Index = 0; Index < Sections.Num() && Index < Titles.Num(); ++Index)
	{
		TestEqual(TEXT("Section order"), Sections[Index].Title, FString(Titles[Index]));
	}
	TestEqual(TEXT("Every registry command appears on the screen"), CodexControlsRules::CountRows(Sections), PlayerCommands::GetRegistry().Num());
	TSet<FName> Seen;
	for (const FCodexControlsSection& Section : Sections)
	{
		for (const FCodexControlsRow& Row : Section.Rows)
		{
			TestFalse(*FString::Printf(TEXT("%s listed once"), *Row.CommandId.ToString()), Seen.Contains(Row.CommandId));
			Seen.Add(Row.CommandId);
			TestFalse(*FString::Printf(TEXT("%s has keys"), *Row.CommandId.ToString()), Row.Keys.IsEmpty());
			TestFalse(*FString::Printf(TEXT("%s has a description"), *Row.CommandId.ToString()), Row.Description.IsEmpty());
		}
	}
	return true;
}

CONTROLS_TEST(FControlsRebindTest, "KeyOverride")
bool FControlsRebindTest::RunTest(const FString&)
{
	TestEqual(TEXT("Default key"), PlayerCommands::GetKeyLabel(PlayerCommands::Reload), FString(TEXT("R")));
	PlayerCommands::SetKeyLabelOverride(PlayerCommands::Reload, TEXT("Backspace"));
	bool bShown = false;
	for (const FCodexControlsSection& Section : CodexControlsRules::BuildSections())
	{
		for (const FCodexControlsRow& Row : Section.Rows)
		{
			bShown |= Row.CommandId == PlayerCommands::Reload && Row.Keys == TEXT("Backspace");
		}
	}
	TestTrue(TEXT("A rebound key shows its current label"), bShown);
	TestEqual(TEXT("Hint card token follows it"), PlayerCommands::FormatKeyToken(TEXT("Weapon.Reload")), FString(TEXT("[Backspace]")));
	PlayerCommands::SetKeyLabelOverride(PlayerCommands::Reload, FString());
	TestEqual(TEXT("Restored"), PlayerCommands::GetKeyLabel(PlayerCommands::Reload), FString(TEXT("R")));
	return true;
}

CONTROLS_TEST(FControlsScreenSetupTest, "ScreenSetup")
bool FControlsScreenSetupTest::RunTest(const FString&)
{
	const FGameplayTag Tag = CodexUITags::Screen_Controls.GetTag();
	const TSoftClassPtr<UCodexActivatableScreen>* Configured = UCodexFrontendSettings::Get().ScreenClasses.Find(Tag);
	TestTrue(TEXT("Controls has a Widget Blueprint entry"), Configured && !Configured->IsNull());
	TestTrue(TEXT("Controls has the C++ fallback"), UCodexUISubsystem::GetNativeScreenClass(Tag) == UCodexControlsScreen::StaticClass());
	UWidgetTree* Tree = NewObject<UWidgetTree>(GetTransientPackage());
	Cast<ICodexDefaultTree>(UCodexControlsScreen::StaticClass()->GetDefaultObject())->BuildDefaultTree(*Tree);
	for (const TCHAR* Name : { TEXT("TitleText"), TEXT("ControlsList"), TEXT("BackButton") })
	{
		TestNotNull(*FString::Printf(TEXT("Controls tree has %s"), Name), Tree->FindWidget(FName(Name)));
	}
	TestTrue(TEXT("F1 works from the Controls screen"), UCodexControlsScreen::StaticClass()->GetDefaultObject<UCodexActivatableScreen>()->bHandlesControlsHotkey);
	TestTrue(TEXT("F1 works from the pause menu"), UCodexPauseMenuScreen::StaticClass()->GetDefaultObject<UCodexActivatableScreen>()->bHandlesControlsHotkey);
	UWidgetTree* PauseTree = NewObject<UWidgetTree>(GetTransientPackage());
	Cast<ICodexDefaultTree>(UCodexPauseMenuScreen::StaticClass()->GetDefaultObject())->BuildDefaultTree(*PauseTree);
	for (const TCHAR* Name : { TEXT("ControlsButton"), TEXT("TutorialButton"), TEXT("ResetTutorialButton") })
	{
		TestNotNull(*FString::Printf(TEXT("Pause tree has %s"), Name), PauseTree->FindWidget(FName(Name)));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
