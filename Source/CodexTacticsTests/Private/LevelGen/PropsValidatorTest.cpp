// CodexTactics.LevelGen.Props.Validator.* - level authoring stage 1, task C5 (docs/design/levelgen/PROPS_LAYERS.md 2.4, 2.5, 5 C5):
// Static props validator + nav-free checks (doorway clear corridor, ladder & stair disks).
// Bad fixture objects are dropped with id logs; locked and hand objects are flagged as LOCKED/HAND INVALID.
// Pure, no world. UE-only (no Godot reference).

#include "LevelGen/PropsValidator.h"
#include "Misc/AutomationTest.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "LevelGen/LevelPropsFile.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

#define PROPS_VALIDATOR_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.LevelGen.Props.Validator." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace PropsValidatorTestLocal
{
	const TCHAR* const GValidatorFixtures = TEXT("Data/LevelGen/props/fixtures");
	const TCHAR* const GValidatorSmallLayout = TEXT("fixture_small.layout.json");
	const TCHAR* const GValidatorSmallCrates = TEXT("golden_small_crates.json");
	const TCHAR* const GValidatorDepotLayout = TEXT("../../accepted/gen_ref_01_02_depot_1.json");

	FString ValidatorFixturePath(const FString& Relative)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / GValidatorFixtures / Relative);
	}

	bool ValidatorLoadText(FAutomationTestBase& Test, const FString& Relative, FString& Out)
	{
		return Test.TestTrue(FString::Printf(TEXT("%s readable"), *Relative), FFileHelper::LoadFileToString(Out, *ValidatorFixturePath(Relative)));
	}

	TSharedPtr<FJsonObject> ValidatorLoadBalance(FAutomationTestBase& Test)
	{
		FString Json;
		TSharedPtr<FJsonObject> Root;
		if (ValidatorLoadText(Test, TEXT("../../../Balance/props_balance.json"), Json))
		{
			Test.TestTrue(TEXT("props_balance.json parses"),
				FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) && Root.IsValid());
		}
		return Root;
	}

	bool ValidatorLoadLayout(FAutomationTestBase& Test, const TSharedPtr<FJsonObject>& Balance, const FString& Relative, FPropsLayoutView& Out)
	{
		FString Json;
		if (!ValidatorLoadText(Test, Relative, Json) || !Balance.IsValid())
		{
			return false;
		}
		TArray<FString> Errors;
		FPropsLayoutConfig Config;
		Test.TestTrue(TEXT("layout_view config reads"), FPropsLayoutView::ReadConfig(*Balance, Config, Errors));
		const bool bOk = FPropsLayoutView::Build(Json, Out, Errors, Config);
		for (const FString& E : Errors)
		{
			Test.AddError(FString::Printf(TEXT("%s: %s"), *Relative, *E));
		}
		return Test.TestTrue(FString::Printf(TEXT("%s builds"), *Relative), bOk);
	}

	bool ValidatorLoadProps(FAutomationTestBase& Test, const FString& Relative, FLevelPropsFile& Out)
	{
		FString Json;
		if (!ValidatorLoadText(Test, Relative, Json))
		{
			return false;
		}
		TArray<FString> Errors;
		const bool bOk = LevelPropsFile::Parse(Json, Out, Errors);
		for (const FString& E : Errors)
		{
			Test.AddError(FString::Printf(TEXT("%s: %s"), *Relative, *E));
		}
		return Test.TestTrue(FString::Printf(TEXT("%s parses"), *Relative), bOk);
	}
}

// 1. Valid props from golden fixtures pass validation without any drops
PROPS_VALIDATOR_TEST(FPropsValidatorValidPropsPassTest, "ValidPropsPass")
bool FPropsValidatorValidPropsPassTest::RunTest(const FString& Parameters)
{
	using namespace PropsValidatorTestLocal;

	const TSharedPtr<FJsonObject> Balance = ValidatorLoadBalance(*this);
	FPropsLayoutView View;
	if (!ValidatorLoadLayout(*this, Balance, GValidatorSmallLayout, View))
	{
		return false;
	}

	FLevelPropsFile File;
	if (!ValidatorLoadProps(*this, GValidatorSmallCrates, File))
	{
		return false;
	}

	FPropsValidationResult Result;
	const bool bValid = FPropsValidator::Validate(View, File, Result, Balance.Get());

	TestTrue(TEXT("Valid props pass validation"), bValid);
	TestEqual(TEXT("Dropped objects is 0"), Result.DroppedObjects.Num(), 0);
	TestEqual(TEXT("Valid objects count equals total count"), Result.ValidObjects.Num(), Result.TotalObjects);
	TestFalse(TEXT("No validation issues"), Result.HasErrors());
	TestFalse(TEXT("No locked or hand invalid"), Result.HasLockedOrHandInvalid());

	return true;
}

// 2. Objects on unknown walk layers or out of bounds are dropped
PROPS_VALIDATOR_TEST(FPropsValidatorWalkLayerAndBoundsTest, "WalkLayerAndBounds")
bool FPropsValidatorWalkLayerAndBoundsTest::RunTest(const FString& Parameters)
{
	using namespace PropsValidatorTestLocal;

	const TSharedPtr<FJsonObject> Balance = ValidatorLoadBalance(*this);
	FPropsLayoutView View;
	if (!ValidatorLoadLayout(*this, Balance, GValidatorSmallLayout, View))
	{
		return false;
	}

	TArray<FLevelPropsResolvedObject> Objects;

	// Object on non-existent walk layer
	FLevelPropsResolvedObject BadLayerObj;
	BadLayerObj.LayerId = TEXT("crates");
	BadLayerObj.Object.Id = TEXT("crates_bad_layer_0");
	BadLayerObj.Object.Variant = TEXT("crate_s");
	BadLayerObj.Object.Pos = FIntPoint(0, 0);
	BadLayerObj.Object.WalkLayer = TEXT("roof:nonexistent_building");
	Objects.Add(BadLayerObj);

	// Object out of bounds
	FLevelPropsResolvedObject OutOfBoundsObj;
	OutOfBoundsObj.LayerId = TEXT("crates");
	OutOfBoundsObj.Object.Id = TEXT("crates_oob_0");
	OutOfBoundsObj.Object.Variant = TEXT("crate_s");
	OutOfBoundsObj.Object.Pos = FIntPoint(50000, 50000);
	OutOfBoundsObj.Object.WalkLayer = TEXT("ground");
	Objects.Add(OutOfBoundsObj);

	// Good object
	FLevelPropsResolvedObject GoodObj;
	GoodObj.LayerId = TEXT("crates");
	GoodObj.Object.Id = TEXT("crates_good_0");
	GoodObj.Object.Variant = TEXT("crate_s");
	GoodObj.Object.Pos = FIntPoint(0, -500);
	GoodObj.Object.WalkLayer = TEXT("ground");
	Objects.Add(GoodObj);

	FPropsValidationResult Result;
	const bool bValid = FPropsValidator::ValidateObjects(View, Objects, {}, Result, Balance.Get());

	TestFalse(TEXT("Validation fails due to bad objects"), bValid);
	TestEqual(TEXT("Total objects evaluated is 3"), Result.TotalObjects, 3);
	TestEqual(TEXT("Dropped objects count is 2"), Result.DroppedObjects.Num(), 2);
	TestEqual(TEXT("Valid objects count is 1"), Result.ValidObjects.Num(), 1);
	TestEqual(TEXT("Surviving object is good obj"), Result.ValidObjects[0].Object.Id, TEXT("crates_good_0"));

	return true;
}

// 3. Objects blocked by layout geometry (inside walls or non-enterable buildings) are dropped
PROPS_VALIDATOR_TEST(FPropsValidatorBlockedByGeometryTest, "BlockedByGeometry")
bool FPropsValidatorBlockedByGeometryTest::RunTest(const FString& Parameters)
{
	using namespace PropsValidatorTestLocal;

	const TSharedPtr<FJsonObject> Balance = ValidatorLoadBalance(*this);
	FPropsLayoutView View;
	if (!ValidatorLoadLayout(*this, Balance, GValidatorSmallLayout, View))
	{
		return false;
	}

	TArray<FLevelPropsResolvedObject> Objects;

	// In fixture_small, F_yard_S_0 fence runs from [-200, -1000] to [-200, 1000]
	FLevelPropsResolvedObject InWallObj;
	InWallObj.LayerId = TEXT("barrels");
	InWallObj.Object.Id = TEXT("barrels_in_wall_0");
	InWallObj.Object.Variant = TEXT("barrel");
	InWallObj.Object.Pos = FIntPoint(-200, 0); // Directly inside fence
	InWallObj.Object.WalkLayer = TEXT("ground");
	Objects.Add(InWallObj);

	FPropsValidationResult Result;
	const bool bValid = FPropsValidator::ValidateObjects(View, Objects, {}, Result, Balance.Get());

	TestFalse(TEXT("Validation fails"), bValid);
	TestEqual(TEXT("Dropped count is 1"), Result.DroppedObjects.Num(), 1);
	TestEqual(TEXT("Valid count is 0"), Result.ValidObjects.Num(), 0);
	TestTrue(TEXT("Reason mentions blocked"), Result.Issues[0].Reason.Contains(TEXT("blocked")));

	return true;
}

// 4. Doorway clear corridor: 90 cm corridor 150 cm deep on both sides drops props, except the doorway's own mine
PROPS_VALIDATOR_TEST(FPropsValidatorDoorwayCorridorTest, "DoorwayCorridor")
bool FPropsValidatorDoorwayCorridorTest::RunTest(const FString& Parameters)
{
	using namespace PropsValidatorTestLocal;

	const TSharedPtr<FJsonObject> Balance = ValidatorLoadBalance(*this);
	FPropsLayoutView View;
	if (!ValidatorLoadLayout(*this, Balance, GValidatorSmallLayout, View))
	{
		return false;
	}

	// In fixture_small, F_yard_N#g0 is a gate at [800, 0], along Y (unit axis (0, 1)).
	// Normal across opening is along X.
	// Door corridor: lateral along Y <= 45 cm (+ half size), depth across opening along X <= 150 cm.
	TArray<FLevelPropsResolvedObject> Objects;

	// Obstacle crate placed inside doorway corridor at [800, 20] (within lateral 45 cm, depth 0 cm)
	FLevelPropsResolvedObject CrateInDoor;
	CrateInDoor.LayerId = TEXT("crates");
	CrateInDoor.Object.Id = TEXT("crates_blocking_door_0");
	CrateInDoor.Object.Variant = TEXT("crate_s");
	CrateInDoor.Object.Pos = FIntPoint(800, 20);
	CrateInDoor.Object.WalkLayer = TEXT("ground");
	Objects.Add(CrateInDoor);

	// Doorway's OWN mine at [800, 0] with Door = "F_yard_N#g0"
	FLevelPropsResolvedObject OwnMine;
	OwnMine.LayerId = TEXT("mines");
	OwnMine.Object.Id = TEXT("mines_own_door_0");
	OwnMine.Object.Variant = TEXT("mine_tripwire");
	OwnMine.Object.Pos = FIntPoint(800, 0);
	OwnMine.Object.WalkLayer = TEXT("ground");
	OwnMine.Object.Door = TEXT("F_yard_N#g0");
	Objects.Add(OwnMine);

	// Rogue mine without Door field in the door corridor at [850, 0] (lateral 0, depth 50 cm)
	FLevelPropsResolvedObject RogueMine;
	RogueMine.LayerId = TEXT("mines");
	RogueMine.Object.Id = TEXT("mines_rogue_0");
	RogueMine.Object.Variant = TEXT("mine");
	RogueMine.Object.Pos = FIntPoint(850, 0);
	RogueMine.Object.WalkLayer = TEXT("ground");
	Objects.Add(RogueMine);

	FPropsValidationResult Result;
	const bool bValid = FPropsValidator::ValidateObjects(View, Objects, {}, Result, Balance.Get());

	TestFalse(TEXT("Validation fails due to blocking props"), bValid);
	TestEqual(TEXT("2 props dropped (crate and rogue mine)"), Result.DroppedObjects.Num(), 2);
	TestEqual(TEXT("1 prop valid (own mine)"), Result.ValidObjects.Num(), 1);
	TestEqual(TEXT("Surviving prop is own mine"), Result.ValidObjects[0].Object.Id, TEXT("mines_own_door_0"));

	return true;
}

// 5. Ladder foot / top (100 cm disk) drops props
PROPS_VALIDATOR_TEST(FPropsValidatorLadderDisksTest, "LadderDisks")
bool FPropsValidatorLadderDisksTest::RunTest(const FString& Parameters)
{
	using namespace PropsValidatorTestLocal;

	const TSharedPtr<FJsonObject> Balance = ValidatorLoadBalance(*this);
	FPropsLayoutView View;
	if (!ValidatorLoadLayout(*this, Balance, GValidatorSmallLayout, View))
	{
		return false;
	}

	// In fixture_small, ladder LAD_B_shed:
	// foot at [990, -350] on ground; top at [1200, -350] on roof:B_shed
	TArray<FLevelPropsResolvedObject> Objects;

	// Barrel placed 50 cm from ladder foot on ground
	FLevelPropsResolvedObject FootBlocker;
	FootBlocker.LayerId = TEXT("barrels");
	FootBlocker.Object.Id = TEXT("barrels_ladder_foot_0");
	FootBlocker.Object.Variant = TEXT("barrel");
	FootBlocker.Object.Pos = FIntPoint(990 + 50, -350);
	FootBlocker.Object.WalkLayer = TEXT("ground");
	Objects.Add(FootBlocker);

	// Crate placed 40 cm from ladder top on roof:B_shed
	FLevelPropsResolvedObject TopBlocker;
	TopBlocker.LayerId = TEXT("crates");
	TopBlocker.Object.Id = TEXT("crates_ladder_top_0");
	TopBlocker.Object.Variant = TEXT("crate_s");
	TopBlocker.Object.Pos = FIntPoint(1200, -350 + 40);
	TopBlocker.Object.WalkLayer = TEXT("roof:B_shed");
	Objects.Add(TopBlocker);

	// Barrel safe distance from ladder (300 cm away)
	FLevelPropsResolvedObject SafeObj;
	SafeObj.LayerId = TEXT("barrels");
	SafeObj.Object.Id = TEXT("barrels_safe_0");
	SafeObj.Object.Variant = TEXT("barrel");
	SafeObj.Object.Pos = FIntPoint(500, -350);
	SafeObj.Object.WalkLayer = TEXT("ground");
	Objects.Add(SafeObj);

	FPropsValidationResult Result;
	const bool bValid = FPropsValidator::ValidateObjects(View, Objects, {}, Result, Balance.Get());

	TestFalse(TEXT("Validation fails due to ladder disk blockages"), bValid);
	TestEqual(TEXT("2 props dropped"), Result.DroppedObjects.Num(), 2);
	TestEqual(TEXT("1 prop valid"), Result.ValidObjects.Num(), 1);
	TestEqual(TEXT("Safe prop survived"), Result.ValidObjects[0].Object.Id, TEXT("barrels_safe_0"));

	return true;
}

// 6. Dangling attach targets are dropped
PROPS_VALIDATOR_TEST(FPropsValidatorDanglingAttachTest, "DanglingAttach")
bool FPropsValidatorDanglingAttachTest::RunTest(const FString& Parameters)
{
	using namespace PropsValidatorTestLocal;

	const TSharedPtr<FJsonObject> Balance = ValidatorLoadBalance(*this);
	FPropsLayoutView View;
	if (!ValidatorLoadLayout(*this, Balance, GValidatorSmallLayout, View))
	{
		return false;
	}

	TArray<FLevelPropsResolvedObject> Objects;

	// Valid crate
	FLevelPropsResolvedObject ValidCrate;
	ValidCrate.LayerId = TEXT("crates");
	ValidCrate.Object.Id = TEXT("crates_target_0");
	ValidCrate.Object.Variant = TEXT("crate_s");
	ValidCrate.Object.Pos = FIntPoint(100, -500);
	ValidCrate.Object.WalkLayer = TEXT("ground");
	Objects.Add(ValidCrate);

	// Mine attached to ValidCrate
	FLevelPropsResolvedObject AttachedMine;
	AttachedMine.LayerId = TEXT("mines");
	AttachedMine.Object.Id = TEXT("mines_attached_0");
	AttachedMine.Object.Variant = TEXT("mine");
	AttachedMine.Object.Pos = FIntPoint(100, -500);
	AttachedMine.Object.WalkLayer = TEXT("ground");
	AttachedMine.Object.Attach = TEXT("crates_target_0");
	Objects.Add(AttachedMine);

	// Mine attached to non-existent crate
	FLevelPropsResolvedObject DanglingMine;
	DanglingMine.LayerId = TEXT("mines");
	DanglingMine.Object.Id = TEXT("mines_dangling_0");
	DanglingMine.Object.Variant = TEXT("mine");
	DanglingMine.Object.Pos = FIntPoint(200, -500);
	DanglingMine.Object.WalkLayer = TEXT("ground");
	DanglingMine.Object.Attach = TEXT("crates_nonexistent_99");
	Objects.Add(DanglingMine);

	// Crate placed inside a wall
	FLevelPropsResolvedObject BadCrate;
	BadCrate.LayerId = TEXT("crates");
	BadCrate.Object.Id = TEXT("crates_in_wall_0");
	BadCrate.Object.Variant = TEXT("crate_s");
	BadCrate.Object.Pos = FIntPoint(-200, 0); // Inside fence
	BadCrate.Object.WalkLayer = TEXT("ground");
	Objects.Add(BadCrate);

	// Mine attached to BadCrate (which will be dropped by spatial check)
	FLevelPropsResolvedObject CascadedDanglingMine;
	CascadedDanglingMine.LayerId = TEXT("mines");
	CascadedDanglingMine.Object.Id = TEXT("mines_cascaded_0");
	CascadedDanglingMine.Object.Variant = TEXT("mine");
	CascadedDanglingMine.Object.Pos = FIntPoint(-200, 0);
	CascadedDanglingMine.Object.WalkLayer = TEXT("ground");
	CascadedDanglingMine.Object.Attach = TEXT("crates_in_wall_0");
	Objects.Add(CascadedDanglingMine);

	FPropsValidationResult Result;
	const bool bValid = FPropsValidator::ValidateObjects(View, Objects, {}, Result, Balance.Get());

	TestFalse(TEXT("Validation fails"), bValid);
	TestEqual(TEXT("3 objects dropped (BadCrate, DanglingMine, CascadedDanglingMine)"), Result.DroppedObjects.Num(), 3);
	TestEqual(TEXT("2 objects valid (ValidCrate, AttachedMine)"), Result.ValidObjects.Num(), 2);

	return true;
}

// 7. Locked and hand objects that fail are flagged with LOCKED/HAND INVALID
PROPS_VALIDATOR_TEST(FPropsValidatorLockedAndHandInvalidTest, "LockedAndHandInvalid")
bool FPropsValidatorLockedAndHandInvalidTest::RunTest(const FString& Parameters)
{
	using namespace PropsValidatorTestLocal;

	const TSharedPtr<FJsonObject> Balance = ValidatorLoadBalance(*this);
	FPropsLayoutView View;
	if (!ValidatorLoadLayout(*this, Balance, GValidatorSmallLayout, View))
	{
		return false;
	}

	TArray<FLevelPropsResolvedObject> Objects;

	// Locked crate placed inside a wall
	FLevelPropsResolvedObject LockedCrate;
	LockedCrate.LayerId = TEXT("crates");
	LockedCrate.Object.Id = TEXT("crates_locked_bad_0");
	LockedCrate.Object.Variant = TEXT("crate_s");
	LockedCrate.Object.Pos = FIntPoint(-200, 0); // Inside fence
	LockedCrate.Object.WalkLayer = TEXT("ground");
	LockedCrate.Object.bLocked = true;
	Objects.Add(LockedCrate);

	// Hand-added barrel in doorway corridor
	FLevelPropsResolvedObject HandBarrel;
	HandBarrel.LayerId = TEXT("barrels");
	HandBarrel.Object.Id = TEXT("hand_barrels_0");
	HandBarrel.Object.Variant = TEXT("barrel");
	HandBarrel.Object.Pos = FIntPoint(800, 10); // Inside doorway corridor
	HandBarrel.Object.WalkLayer = TEXT("ground");
	HandBarrel.Object.Origin = TEXT("hand");
	HandBarrel.bHand = true;
	Objects.Add(HandBarrel);

	// Generated crate in doorway corridor (not locked, not hand)
	FLevelPropsResolvedObject GenCrate;
	GenCrate.LayerId = TEXT("crates");
	GenCrate.Object.Id = TEXT("crates_gen_bad_0");
	GenCrate.Object.Variant = TEXT("crate_s");
	GenCrate.Object.Pos = FIntPoint(800, -10); // Inside doorway corridor
	GenCrate.Object.WalkLayer = TEXT("ground");
	Objects.Add(GenCrate);

	FPropsValidationResult Result;
	const bool bValid = FPropsValidator::ValidateObjects(View, Objects, {}, Result, Balance.Get());

	TestFalse(TEXT("Validation fails"), bValid);
	TestEqual(TEXT("All 3 objects dropped"), Result.DroppedObjects.Num(), 3);
	TestTrue(TEXT("HasLockedOrHandInvalid is true"), Result.HasLockedOrHandInvalid());

	// Find the issues
	const FPropsValidationIssue* LockedIssue = Result.Issues.FindByPredicate(
		[](const FPropsValidationIssue& I) { return I.ObjectId == TEXT("crates_locked_bad_0"); });
	TestNotNull(TEXT("Locked issue found"), LockedIssue);
	if (LockedIssue)
	{
		TestTrue(TEXT("Locked issue has bLocked true"), LockedIssue->bLocked);
	}

	const FPropsValidationIssue* HandIssue = Result.Issues.FindByPredicate(
		[](const FPropsValidationIssue& I) { return I.ObjectId == TEXT("hand_barrels_0"); });
	TestNotNull(TEXT("Hand issue found"), HandIssue);
	if (HandIssue)
	{
		TestTrue(TEXT("Hand issue has bHand true"), HandIssue->bHand);
	}

	const FPropsValidationIssue* GenIssue = Result.Issues.FindByPredicate(
		[](const FPropsValidationIssue& I) { return I.ObjectId == TEXT("crates_gen_bad_0"); });
	TestNotNull(TEXT("Gen issue found"), GenIssue);
	if (GenIssue)
	{
		TestFalse(TEXT("Gen issue has bLocked false"), GenIssue->bLocked);
		TestFalse(TEXT("Gen issue has bHand false"), GenIssue->bHand);
	}

	return true;
}

// 8. Objects inside deleted keep-out disks are dropped
PROPS_VALIDATOR_TEST(FPropsValidatorKeepOutTest, "DeletedKeepOut")
bool FPropsValidatorKeepOutTest::RunTest(const FString& Parameters)
{
	using namespace PropsValidatorTestLocal;

	const TSharedPtr<FJsonObject> Balance = ValidatorLoadBalance(*this);
	FPropsLayoutView View;
	if (!ValidatorLoadLayout(*this, Balance, GValidatorSmallLayout, View))
	{
		return false;
	}

	TArray<FLevelPropsResolvedObject> Objects;

	FLevelPropsResolvedObject Obj;
	Obj.LayerId = TEXT("crates");
	Obj.Object.Id = TEXT("crates_test_0");
	Obj.Object.Variant = TEXT("crate_s");
	Obj.Object.Pos = FIntPoint(100, -500);
	Obj.Object.WalkLayer = TEXT("ground");
	Objects.Add(Obj);

	// Keepout disk centered at [100, -500] with radius 150 cm
	TArray<FLevelPropsKeepOut> KeepOuts;
	FLevelPropsKeepOut KO;
	KO.Id = TEXT("deleted_crate_0");
	KO.LayerId = TEXT("crates");
	KO.Pos = FIntPoint(100, -500);
	KO.RadiusCm = 150;
	KeepOuts.Add(KO);

	FPropsValidationResult Result;
	const bool bValid = FPropsValidator::ValidateObjects(View, Objects, KeepOuts, Result, Balance.Get());

	TestFalse(TEXT("Validation fails due to keep-out"), bValid);
	TestEqual(TEXT("1 object dropped"), Result.DroppedObjects.Num(), 1);
	TestTrue(TEXT("Reason mentions keep-out"), Result.Issues[0].Reason.Contains(TEXT("keep-out")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
