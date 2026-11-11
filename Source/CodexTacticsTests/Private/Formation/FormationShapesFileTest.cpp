// CodexTactics.Formation.Shapes.* - the saved formation shapes reader and lookup (game designer 2026-11-10, variant B; docs/design/FORMATION_CONTRACT.md 19,
// TS reference Tools/TrainingGround/src/movement/shapesSchema.ts / sim.ts / shapesCoverage.ts) and CodexTactics.Formation.Parity.SavedShapes - the
// C++ reader + slots against the Training Ground golden vectors (Tools/TrainingGround/golden/formation_SavedShapes.json, built by
// `node src/movement/cli/shapesGolden.ts` from Content/Data/FormationLab/fixtures/formation_shapes_*.json). UE-only (no Godot reference).

#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Formation/FormationShapesFile.h"
#include "Formation/FormationShapeRules.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace FormationShapesTestLocal
{
	FString ShapesFixturePath(const FString& Name)
	{
		return FPaths::ProjectContentDir() / TEXT("Data/FormationLab/fixtures") / Name;
	}

	bool LoadFixture(FAutomationTestBase& Test, const FString& Name, FFormationShapesFile& Out)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *ShapesFixturePath(Name)))
		{
			Test.AddError(FString::Printf(TEXT("fixture %s missing"), *Name));
			return false;
		}
		TArray<FString> Errors;
		if (!FormationShapes::Parse(Text, Out, Errors))
		{
			Test.AddError(FString::Printf(TEXT("fixture %s rejected: %s"), *Name, *FString::Join(Errors, TEXT("; "))));
			return false;
		}
		return true;
	}

	/** A one-formation document around Members (a JSON array text). */
	FString Doc(const TCHAR* Formation, const TCHAR* Count, const TCHAR* Members, const TCHAR* Custom = TEXT("{}"))
	{
		return FString::Printf(TEXT("{ \"schema\": \"codex.formation_shapes/1\", \"formations\": { \"%s\": { \"%s\": { \"members\": %s } } }, \"custom\": %s }"),
			Formation, Count, Members, Custom);
	}

	const TCHAR* const Diamond4 = TEXT("[ { \"role\": \"commander\", \"fwd\": 0, \"right\": 0, \"look\": 0, \"arc\": 90 },")
		TEXT(" { \"role\": \"right\", \"fwd\": -310, \"right\": 330, \"look\": 77, \"arc\": 60 },")
		TEXT(" { \"role\": \"left\", \"fwd\": -310, \"right\": -330, \"look\": -77 },")
		TEXT(" { \"role\": \"rear\", \"fwd\": -620, \"right\": 0, \"look\": 180, \"arc\": 120 } ]");
	const TCHAR* const Pair2 = TEXT("[ { \"role\": \"commander\", \"fwd\": 0, \"right\": 0, \"look\": 0 }, { \"role\": \"rear\", \"fwd\": -150, \"right\": 100, \"look\": 60 } ]");

	bool Rejects(FAutomationTestBase& Test, const FString& What, const FString& Text, const TCHAR* Expected)
	{
		FFormationShapesFile File;
		TArray<FString> Errors;
		const bool bOk = FormationShapes::Parse(Text, File, Errors);
		const bool bFound = Errors.ContainsByPredicate([Expected](const FString& Error) { return Error.Contains(Expected); });
		if (bOk || !bFound)
		{
			Test.AddError(FString::Printf(TEXT("%s: expected a rejection with \"%s\", got %s [%s]"), *What, Expected, bOk ? TEXT("ok") : TEXT("rejected"),
				*FString::Join(Errors, TEXT("; "))));
			return false;
		}
		Test.TestTrue(What + TEXT(": the whole file is dropped"), File.IsEmpty());
		return true;
	}

	double ShapesAngleDiff(double A, double B)
	{
		return FMath::Abs(FormationMath::NormYaw(A - B));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFormationShapesParseTest, "CodexTactics.Formation.Shapes.Parse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFormationShapesParseTest::RunTest(const FString&)
{
	using namespace FormationShapesTestLocal;
	FFormationShapesFile Diamond;
	if (LoadFixture(*this, TEXT("formation_shapes_diamond4.json"), Diamond))
	{
		const TMap<int32, FFormationShape>* ByCount = Diamond.Formations.Find(TEXT("diamond"));
		TestTrue(TEXT("diamond4 fixture: diamond/4 with 4 members"), ByCount && ByCount->Contains(4) && (*ByCount)[4].Members.Num() == 4);
		TestTrue(TEXT("diamond4 fixture: no custom defaults"), !Diamond.Custom.ContainsByPredicate([](const FFormationCustomShape& S) { return !S.IsEmpty(); }));
	}
	FFormationShapesFile Echelons;
	if (LoadFixture(*this, TEXT("formation_shapes_echelon_mirror.json"), Echelons))
	{
		TestTrue(TEXT("echelon fixture: both echelons"), Echelons.Formations.Contains(TEXT("echelon_left")) && Echelons.Formations.Contains(TEXT("echelon_right")));
	}
	// The empty document of main (the editor's initial file) is valid and empty.
	FFormationShapesFile Empty;
	TArray<FString> Errors;
	TestTrue(TEXT("the empty document parses"), FormationShapes::Parse(TEXT("{ \"schema\": \"codex.formation_shapes/1\", \"formations\": {}, \"custom\": {} }"), Empty, Errors));
	TestTrue(TEXT("... and is empty"), Empty.IsEmpty());

	// Members, arcs, custom defaults.
	FFormationShapesFile File;
	Errors.Reset();
	const FString Custom = TEXT("{ \"custom_2\": { \"name\": \"alpha\", \"members\": [ { \"squad\": 0, \"fwd\": 0, \"right\": 0, \"look\": 0 }, { \"squad\": 2, \"fwd\": -200, \"right\": 150, \"look\": 45, \"arc\": 90 } ] } }");
	TestTrue(TEXT("diamond + custom_2 parse"), FormationShapes::Parse(Doc(TEXT("diamond"), TEXT("4"), Diamond4, *Custom), File, Errors));
	const FFormationShape* Shape = File.Formations.Contains(TEXT("diamond")) ? File.Formations[TEXT("diamond")].Find(4) : nullptr;
	if (TestNotNull(TEXT("diamond/4 stored"), Shape))
	{
		TestEqual(TEXT("P fwd"), Shape->Members[1].Fwd, -310);
		TestEqual(TEXT("P right"), Shape->Members[1].Right, 330);
		TestEqual(TEXT("P look"), Shape->Members[1].Look, 77);
		TestTrue(TEXT("P arc 60"), Shape->Members[1].Arc == TOptional<int32>(60));
		TestFalse(TEXT("L without arc"), Shape->Members[2].Arc.IsSet());
	}
	TestTrue(TEXT("custom_1 empty"), File.Custom[0].IsEmpty());
	TestEqual(TEXT("custom_2 two members"), File.Custom[1].Members.Num(), 2);
	TestTrue(TEXT("custom_2 member 1 = squad 2 at (-200, 150) look 45"), File.Custom[1].Members.Num() == 2 && File.Custom[1].Members[1].Squad == 2
		&& File.Custom[1].Members[1].Fwd == -200.f && File.Custom[1].Members[1].Right == 150.f && File.Custom[1].Members[1].Look == 45.f);

	// Rule-driven / unknown formations: warned and kept out, the rest of the file stays.
	Errors.Reset();
	TArray<FString> Warnings;
	const FString Mixed = TEXT("{ \"schema\": \"codex.formation_shapes/1\", \"formations\": { \"wall\": { \"2\": { \"members\": ") + FString(Pair2)
		+ TEXT(" } }, \"zigzag\": { \"2\": { \"members\": ") + FString(Pair2) + TEXT(" } }, \"pair\": { \"2\": { \"members\": ") + FString(Pair2) + TEXT(" } } } }");
	TestTrue(TEXT("wall / unknown / pair: valid"), FormationShapes::Parse(Mixed, File, Errors, &Warnings));
	TestFalse(TEXT("wall stays rule-driven"), File.Formations.Contains(TEXT("wall")));
	TestFalse(TEXT("an unknown formation is dropped"), File.Formations.Contains(TEXT("zigzag")));
	TestTrue(TEXT("the pair is kept"), File.Formations.Contains(TEXT("pair")));
	TestEqual(TEXT("two warnings"), Warnings.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFormationShapesRejectTest, "CodexTactics.Formation.Shapes.Reject",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFormationShapesRejectTest::RunTest(const FString&)
{
	using namespace FormationShapesTestLocal;
	Rejects(*this, TEXT("not JSON"), TEXT("{ nope"), TEXT("expected a JSON object"));
	Rejects(*this, TEXT("schema"), TEXT("{ \"schema\": \"bad/1\", \"formations\": {} }"), TEXT("schema must be"));
	Rejects(*this, TEXT("count 5"), Doc(TEXT("diamond"), TEXT("5"), Diamond4), TEXT("must be 2, 3, or 4"));
	Rejects(*this, TEXT("length != count"), Doc(TEXT("diamond"), TEXT("3"), Diamond4), TEXT("does not match count 3"));
	Rejects(*this, TEXT("member 0 not the commander"), Doc(TEXT("line"), TEXT("2"),
		TEXT("[ { \"role\": \"rear\", \"fwd\": 0, \"right\": 0, \"look\": 0 }, { \"role\": \"commander\", \"fwd\": 0, \"right\": 0, \"look\": 0 } ]")), TEXT("member 0 must be the commander"));
	Rejects(*this, TEXT("commander off the origin"), Doc(TEXT("line"), TEXT("2"),
		TEXT("[ { \"role\": \"commander\", \"fwd\": 10, \"right\": 0, \"look\": 0 }, { \"role\": \"rear\", \"fwd\": -200, \"right\": 0, \"look\": 180 } ]")), TEXT("commander must be at fwd 0, right 0"));
	Rejects(*this, TEXT("non-integer cm"), Doc(TEXT("wedge"), TEXT("2"),
		TEXT("[ { \"role\": \"commander\", \"fwd\": 0, \"right\": 0, \"look\": 0 }, { \"role\": \"rear\", \"fwd\": -200.5, \"right\": 0, \"look\": 180 } ]")), TEXT("fwd and right must be integer cm"));
	Rejects(*this, TEXT("a string for cm"), Doc(TEXT("wedge"), TEXT("2"),
		TEXT("[ { \"role\": \"commander\", \"fwd\": 0, \"right\": 0, \"look\": 0 }, { \"role\": \"rear\", \"fwd\": \"-200\", \"right\": 0, \"look\": 180 } ]")), TEXT("fwd and right must be integer cm"));
	Rejects(*this, TEXT("duplicate role"), Doc(TEXT("line"), TEXT("2"),
		TEXT("[ { \"role\": \"commander\", \"fwd\": 0, \"right\": 0, \"look\": 0 }, { \"role\": \"commander\", \"fwd\": 0, \"right\": 0, \"look\": 0 } ]")), TEXT("duplicate role"));
	Rejects(*this, TEXT("roles != rolesFor(3)"), Doc(TEXT("wedge"), TEXT("3"),
		TEXT("[ { \"role\": \"commander\", \"fwd\": 0, \"right\": 0, \"look\": 0 }, { \"role\": \"left\", \"fwd\": -200, \"right\": -200, \"look\": 0 }, { \"role\": \"rear\", \"fwd\": -400, \"right\": 0, \"look\": 180 } ]")),
		TEXT("roles must match rolesFor(3)"));
	Rejects(*this, TEXT("a bad role name"), Doc(TEXT("wedge"), TEXT("2"),
		TEXT("[ { \"role\": \"commander\", \"fwd\": 0, \"right\": 0, \"look\": 0 }, { \"role\": \"point\", \"fwd\": -200, \"right\": 0, \"look\": 0 } ]")), TEXT("bad role"));
	Rejects(*this, TEXT("look -180"), Doc(TEXT("wedge"), TEXT("2"),
		TEXT("[ { \"role\": \"commander\", \"fwd\": 0, \"right\": 0, \"look\": 0 }, { \"role\": \"rear\", \"fwd\": -200, \"right\": 0, \"look\": -180 } ]")), TEXT("look must be integer degrees"));
	Rejects(*this, TEXT("arc 400"), Doc(TEXT("wedge"), TEXT("2"),
		TEXT("[ { \"role\": \"commander\", \"fwd\": 0, \"right\": 0, \"look\": 0 }, { \"role\": \"rear\", \"fwd\": -200, \"right\": 0, \"look\": 0, \"arc\": 400 } ]")), TEXT("arc must be integer degrees between 10 and 360"));
	Rejects(*this, TEXT("arc 45.5"), Doc(TEXT("wedge"), TEXT("2"),
		TEXT("[ { \"role\": \"commander\", \"fwd\": 0, \"right\": 0, \"look\": 0, \"arc\": 45.5 }, { \"role\": \"rear\", \"fwd\": -200, \"right\": 0, \"look\": 0 } ]")), TEXT("arc must be integer degrees between 10 and 360"));
	Rejects(*this, TEXT("arc null (TS: only an absent arc is the default)"), Doc(TEXT("wedge"), TEXT("2"),
		TEXT("[ { \"role\": \"commander\", \"fwd\": 0, \"right\": 0, \"look\": 0, \"arc\": null }, { \"role\": \"rear\", \"fwd\": -200, \"right\": 0, \"look\": 0 } ]")), TEXT("arc must be integer degrees between 10 and 360"));
	Rejects(*this, TEXT("custom arc null"), Doc(TEXT("wedge"), TEXT("2"), Pair2,
		TEXT("{ \"custom_1\": { \"members\": [ { \"squad\": 0, \"fwd\": 0, \"right\": 0, \"look\": 0, \"arc\": null } ] } }")), TEXT("arc must be integer degrees between 10 and 360"));
	Rejects(*this, TEXT("beyond 40 m"), Doc(TEXT("wedge"), TEXT("2"),
		TEXT("[ { \"role\": \"commander\", \"fwd\": 0, \"right\": 0, \"look\": 0 }, { \"role\": \"rear\", \"fwd\": -4500, \"right\": 0, \"look\": 180 } ]")), TEXT("exceeds CUSTOM_MAX_RADIUS"));
	Rejects(*this, TEXT("custom slot key"), Doc(TEXT("wedge"), TEXT("2"), Pair2, TEXT("{ \"custom_4\": { \"members\": [] } }")), TEXT("unexpected slot key"));
	Rejects(*this, TEXT("custom duplicate squad"), Doc(TEXT("wedge"), TEXT("2"), Pair2,
		TEXT("{ \"custom_1\": { \"members\": [ { \"squad\": 0, \"fwd\": 0, \"right\": 0, \"look\": 0 }, { \"squad\": 0, \"fwd\": 0, \"right\": 0, \"look\": 0 } ] } }")), TEXT("duplicate squad index"));
	Rejects(*this, TEXT("custom anchor off the origin"), Doc(TEXT("wedge"), TEXT("2"), Pair2,
		TEXT("{ \"custom_1\": { \"members\": [ { \"squad\": 0, \"fwd\": 5, \"right\": 0, \"look\": 0 } ] } }")), TEXT("squad 0 (anchor) must be at fwd 0, right 0"));
	Rejects(*this, TEXT("custom member null"), Doc(TEXT("wedge"), TEXT("2"), Pair2, TEXT("{ \"custom_1\": { \"members\": [ null ] } }")), TEXT("expected an object"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFormationShapesFindTest, "CodexTactics.Formation.Shapes.Find",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFormationShapesFindTest::RunTest(const FString&)
{
	using namespace FormationShapesTestLocal;
	FFormationShapesFile File;
	TArray<FString> Errors;
	const FString Text = TEXT("{ \"schema\": \"codex.formation_shapes/1\", \"formations\": { \"diamond\": { \"4\": { \"members\": ") + FString(Diamond4)
		+ TEXT(" } }, \"pair\": { \"2\": { \"members\": ") + FString(Pair2) + TEXT(" } } } }");
	if (!TestTrue(TEXT("parse"), FormationShapes::Parse(Text, File, Errors)))
	{
		return false;
	}
	TestTrue(TEXT("diamond/4 exact"), FormationShapes::Find(File, EFormationKind::Diamond, 4).IsSet());
	TestTrue(TEXT("a 5th member uses the 4-man shape"), FormationShapes::Find(File, EFormationKind::Diamond, 5).IsSet());
	TestFalse(TEXT("diamond/3 not saved -> the formula"), FormationShapes::Find(File, EFormationKind::Diamond, 3).IsSet());
	TestFalse(TEXT("wedge/4 is another formation"), FormationShapes::Find(File, EFormationKind::Wedge, 4).IsSet());
	TestFalse(TEXT("one member: nothing"), FormationShapes::Find(File, EFormationKind::Diamond, 1).IsSet());
	// The pair (review 008 finding 4): only the game's pair (wedge / diamond with 2) takes it.
	const TOptional<FFormationShape> Pair = FormationShapes::Find(File, EFormationKind::Wedge, 2);
	TestTrue(TEXT("wedge/2 = the pair"), Pair.IsSet() && Pair->Members.Num() == 2 && Pair->Members[1].Right == 100);
	TestTrue(TEXT("diamond/2 = the pair"), FormationShapes::Find(File, EFormationKind::Diamond, 2).IsSet());
	TestFalse(TEXT("column/2 never takes the pair"), FormationShapes::Find(File, EFormationKind::Column, 2).IsSet());
	TestFalse(TEXT("line/2 never takes the pair"), FormationShapes::Find(File, EFormationKind::Line, 2).IsSet());
	TestFalse(TEXT("echelon_left/2 never takes the pair"), FormationShapes::Find(File, EFormationKind::EchelonLeft, 2).IsSet());
	// Rule-driven formations never read the file, even when an entry is forced in.
	FFormationShapesFile Forced = File;
	for (const TCHAR* Name : { TEXT("wall"), TEXT("hedgehog"), TEXT("blizzard"), TEXT("custom") })
	{
		Forced.Formations.Add(Name, File.Formations[TEXT("diamond")]);
	}
	for (const EFormationKind Kind : { EFormationKind::Wall, EFormationKind::Hedgehog, EFormationKind::Blizzard, EFormationKind::Custom })
	{
		TestFalse(FString::Printf(TEXT("%s stays rule-driven"), FormationNames::ToString(Kind)), FormationShapes::Find(Forced, Kind, 4).IsSet());
	}
	// An explicit wedge/2 beats the pair.
	FFormationShapesFile Own = File;
	FFormationShape Wedge2 = File.Formations[TEXT("pair")][2];
	Wedge2.Members[1].Right = -100;
	Own.Formations.FindOrAdd(TEXT("wedge")).Add(2, Wedge2);
	TestTrue(TEXT("wedge/2 saved beats the pair"), FormationShapes::Find(Own, EFormationKind::Wedge, 2)->Members[1].Right == -100);

	// Mirroring for the echelons: only echelon_left saved -> echelon_right is its mirror.
	FFormationShapesFile Echelons;
	if (LoadFixture(*this, TEXT("formation_shapes_echelon_mirror.json"), Echelons))
	{
		const FFormationShape Right = Echelons.Formations[TEXT("echelon_right")][4];
		Echelons.Formations.Remove(TEXT("echelon_right"));
		bool bMirrored = false;
		const TOptional<FFormationShape> Found = FormationShapes::Find(Echelons, EFormationKind::EchelonRight, 4, &bMirrored);
		TestTrue(TEXT("echelon_right from echelon_left, mirrored"), Found.IsSet() && bMirrored);
		if (Found.IsSet() && TestEqual(TEXT("4 members"), Found->Members.Num(), Right.Members.Num()))
		{
			for (int32 Index = 0; Index < Right.Members.Num(); ++Index)
			{
				const FFormationShapeMember& A = Found->Members[Index];
				const FFormationShapeMember& B = Right.Members[Index];
				TestTrue(FString::Printf(TEXT("mirrored member %d == the fixture's echelon_right"), Index), A.Role == B.Role && A.Fwd == B.Fwd && A.Right == B.Right
					&& A.Look == B.Look && A.Arc == B.Arc);
			}
		}
		bool bExact = true;
		TestTrue(TEXT("echelon_left itself not mirrored"), FormationShapes::Find(Echelons, EFormationKind::EchelonLeft, 4, &bExact).IsSet() && !bExact);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFormationShapesSlotsTest, "CodexTactics.Formation.Shapes.Slots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFormationShapesSlotsTest::RunTest(const FString&)
{
	using namespace FormationShapesTestLocal;
	FFormationShapesFile File;
	TArray<FString> Errors;
	if (!TestTrue(TEXT("parse"), FormationShapes::Parse(Doc(TEXT("diamond"), TEXT("4"), Diamond4), File, Errors)))
	{
		return false;
	}
	TArray<float> Arcs;
	// Heading +Y (yaw 90): forward = (0, 1), right = (-1, 0). P 310 back / 330 right -> (100 - 330, 200 - 310), look 90 + 77.
	const TArray<FFormationSlot> Slots = FormationShapes::Slots(File.Formations[TEXT("diamond")][4], FVector2D(100.0, 200.0), 90.f, &Arcs);
	if (!TestEqual(TEXT("four slots"), Slots.Num(), 4) || !TestEqual(TEXT("four arcs"), Arcs.Num(), 4))
	{
		return false;
	}
	TestTrue(TEXT("K on the leader"), Slots[0].Role == EFormationRole::Commander && Slots[0].Position.Equals(FVector2D(100.0, 200.0), 0.01));
	TestTrue(TEXT("P rotated with the heading"), Slots[1].Role == EFormationRole::Right && Slots[1].Position.Equals(FVector2D(-230.0, -110.0), 0.01));
	TestEqual(TEXT("P look = heading + 77"), Slots[1].Yaw, 167.f, 0.01f);
	TestTrue(TEXT("Z 620 behind"), Slots[3].Position.Equals(FVector2D(100.0, -420.0), 0.01));
	TestEqual(TEXT("Z look = heading + 180 -> -90"), Slots[3].Yaw, -90.f, 0.01f);
	TestEqual(TEXT("P arc 60"), Arcs[1], 60.f);
	TestEqual(TEXT("L default arc 90"), Arcs[2], 90.f);
	TestEqual(TEXT("Z arc 120"), Arcs[3], 120.f);
	// Mirror: right / look negated, L <-> R; -180 never appears (180 stays 180).
	const FFormationShape Mirrored = FormationShapes::Mirror(File.Formations[TEXT("diamond")][4]);
	TestTrue(TEXT("mirror swaps P -> L"), Mirrored.Members[1].Role == EFormationRole::Left && Mirrored.Members[1].Right == -330 && Mirrored.Members[1].Look == -77);
	TestEqual(TEXT("mirror keeps 180"), Mirrored.Members[3].Look, 180);
	TestTrue(TEXT("mirror keeps the arc"), Mirrored.Members[1].Arc == TOptional<int32>(60));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFormationShapesRuntimeTest, "CodexTactics.Formation.Shapes.Runtime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFormationShapesRuntimeTest::RunTest(const FString&)
{
	using namespace FormationShapesTestLocal;
	// The invalid save below is meant to be rejected: its errors and the "rejected, the last shapes kept" line are expected warnings.
	AddExpectedMessage(TEXT("[Formation] saved shapes"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0, false);
	// Override wins; clearing it returns to the file.
	FFormationShapesFile File;
	TArray<FString> Errors;
	FormationShapes::Parse(Doc(TEXT("line"), TEXT("4"),
		TEXT("[ { \"role\": \"commander\", \"fwd\": 0, \"right\": 0, \"look\": 0 }, { \"role\": \"right\", \"fwd\": 0, \"right\": 300, \"look\": 30 }, { \"role\": \"left\", \"fwd\": 0, \"right\": -300, \"look\": -30 }, { \"role\": \"rear\", \"fwd\": 0, \"right\": 600, \"look\": 60 } ]")),
		File, Errors);
	const uint32 Revision = FormationShapes::GetRevision();
	FormationShapes::SetOverride(File);
	TestTrue(TEXT("override in force"), FormationShapes::Find(FormationShapes::Get(), EFormationKind::Line, 4).IsSet());
	TestTrue(TEXT("override bumps the revision"), FormationShapes::GetRevision() != Revision);
	FormationShapes::ClearOverride();

	// Live reload of a scratch file (the dev builds' timestamp / size check, LiveReloadSeconds apart).
	const FString Dir = FPaths::ProjectSavedDir() / TEXT("Automation/FormationShapes");
	const FString Path = Dir / TEXT("formation_shapes.json");
	IFileManager::Get().MakeDirectory(*Dir, true);
	IFileManager::Get().Delete(*Path, false, true, true);
	FormationShapes::SetPath(Path);
	TestTrue(TEXT("a missing file = no shapes"), FormationShapes::Get().IsEmpty());
	FFileHelper::SaveStringToFile(Doc(TEXT("diamond"), TEXT("4"), Diamond4), *Path);
	FPlatformProcess::Sleep(static_cast<float>(FormationShapes::LiveReloadSeconds) + 0.15f);
	TestTrue(TEXT("a saved file shows up without a restart"), FormationShapes::Find(FormationShapes::Get(), EFormationKind::Diamond, 4).IsSet());
	FFileHelper::SaveStringToFile(TEXT("{ \"schema\": \"codex.formation_shapes/1\", \"formations\": { \"diamond\": { \"4\": { \"members\": [] } } } }"), *Path);
	FPlatformProcess::Sleep(static_cast<float>(FormationShapes::LiveReloadSeconds) + 0.15f);
	TestTrue(TEXT("an invalid save keeps the last valid shapes"), FormationShapes::Find(FormationShapes::Get(), EFormationKind::Diamond, 4).IsSet());
	IFileManager::Get().Delete(*Path, false, true, true);
	FPlatformProcess::Sleep(static_cast<float>(FormationShapes::LiveReloadSeconds) + 0.15f);
	TestTrue(TEXT("the file removed -> the formula again"), FormationShapes::Get().IsEmpty());
	FormationShapes::SetPath(FString());
	TestEqual(TEXT("back on the default path"), FormationShapes::GetPath(), FormationShapes::GetDefaultPath());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFormationParitySavedShapesTest, "CodexTactics.Formation.Parity.SavedShapes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFormationParitySavedShapesTest::RunTest(const FString&)
{
	using namespace FormationShapesTestLocal;
	const FString Path = FPaths::ProjectDir() / TEXT("Tools/TrainingGround/golden/formation_SavedShapes.json");
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		AddWarning(FString::Printf(TEXT("Golden vectors %s not found (node src/movement/cli/shapesGolden.ts): parity not checked"), *Path));
		return true;
	}
	TSharedPtr<FJsonObject> Root;
	FString Schema;
	FString Rule;
	const TArray<TSharedPtr<FJsonValue>>* Cases = nullptr;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid() || !Root->TryGetStringField(TEXT("schema"), Schema)
		|| Schema != TEXT("codex.formation_golden/1") || !Root->TryGetStringField(TEXT("rule"), Rule) || Rule != TEXT("SavedShapes")
		|| !Root->TryGetArrayField(TEXT("cases"), Cases) || Cases->Num() == 0)
	{
		AddError(FString::Printf(TEXT("%s: schema codex.formation_golden/1, rule SavedShapes and cases expected"), *Path));
		return false;
	}
	double TolCm = 0.5;
	double TolDeg = 0.5;
	const TSharedPtr<FJsonObject>* Tolerance = nullptr;
	if (Root->TryGetObjectField(TEXT("tolerance"), Tolerance))
	{
		(*Tolerance)->TryGetNumberField(TEXT("cm"), TolCm);
		(*Tolerance)->TryGetNumberField(TEXT("deg"), TolDeg);
	}
	TMap<FString, FFormationShapesFile> Fixtures;
	int32 Failures = 0;
	int32 Checked = 0;
	int32 Found = 0;
	for (int32 CaseIndex = 0; CaseIndex < Cases->Num(); ++CaseIndex)
	{
		const TSharedPtr<FJsonObject> Case = (*Cases)[CaseIndex]->AsObject();
		const TSharedPtr<FJsonObject>* In = nullptr;
		const TSharedPtr<FJsonObject>* Out = nullptr;
		FString Op;
		FString Fixture;
		FString FormationName;
		double Count = 0.0;
		EFormationKind Kind = EFormationKind::Diamond;
		if (!Case.IsValid() || !Case->TryGetObjectField(TEXT("in"), In) || !Case->TryGetObjectField(TEXT("out"), Out) || !(*In)->TryGetStringField(TEXT("op"), Op)
			|| !(*In)->TryGetStringField(TEXT("fixture"), Fixture) || !(*In)->TryGetStringField(TEXT("formation"), FormationName)
			|| !(*In)->TryGetNumberField(TEXT("memberCount"), Count) || !FormationNames::Parse(FormationName, Kind))
		{
			AddError(FString::Printf(TEXT("case %d unreadable"), CaseIndex));
			++Failures;
			continue;
		}
		if (!Fixtures.Contains(Fixture))
		{
			FFormationShapesFile Loaded;
			if (!LoadFixture(*this, Fixture, Loaded))
			{
				return false;
			}
			Fixtures.Add(Fixture, Loaded);
		}
		const FFormationShapesFile& File = Fixtures[Fixture];
		++Checked;
		FString Mismatch;
		if (Op == TEXT("slots"))
		{
			const TArray<TSharedPtr<FJsonValue>>* Positions = nullptr;
			double X = 0.0;
			double Y = 0.0;
			double Yaw = 0.0;
			if (!(*In)->TryGetArrayField(TEXT("leaderPos"), Positions) || Positions->Num() != 2 || !(*In)->TryGetNumberField(TEXT("leaderYaw"), Yaw))
			{
				Mismatch = TEXT("unreadable leader pose");
			}
			X = Positions && Positions->Num() == 2 ? (*Positions)[0]->AsNumber() : 0.0;
			Y = Positions && Positions->Num() == 2 ? (*Positions)[1]->AsNumber() : 0.0;
			const TOptional<FFormationShape> Shape = FormationShapes::Find(File, Kind, static_cast<int32>(Count));
			const TArray<TSharedPtr<FJsonValue>>* Expected = nullptr;
			const bool bExpected = (*Out)->TryGetArrayField(TEXT("slots"), Expected);
			if (Mismatch.IsEmpty() && bExpected != Shape.IsSet())
			{
				Mismatch = FString::Printf(TEXT("saved shape %s, expected %s"), Shape.IsSet() ? TEXT("found") : TEXT("none"), bExpected ? TEXT("found") : TEXT("none"));
			}
			else if (Mismatch.IsEmpty() && Shape.IsSet())
			{
				++Found;
				TArray<float> Arcs;
				const TArray<FFormationSlot> Slots = FormationShapes::Slots(Shape.GetValue(), FVector2D(X, Y), static_cast<float>(Yaw), &Arcs);
				if (Slots.Num() != Expected->Num())
				{
					Mismatch = FString::Printf(TEXT("%d slots, expected %d"), Slots.Num(), Expected->Num());
				}
				for (int32 Index = 0; Mismatch.IsEmpty() && Index < Slots.Num(); ++Index)
				{
					const TSharedPtr<FJsonObject> Slot = (*Expected)[Index]->AsObject();
					const TArray<TSharedPtr<FJsonValue>>* Pos = nullptr;
					FString Role;
					double SlotYaw = 0.0;
					double Arc = 0.0;
					if (!Slot.IsValid() || !Slot->TryGetStringField(TEXT("role"), Role) || !Slot->TryGetArrayField(TEXT("pos"), Pos) || Pos->Num() != 2
						|| !Slot->TryGetNumberField(TEXT("yaw"), SlotYaw) || !Slot->TryGetNumberField(TEXT("arc"), Arc))
					{
						Mismatch = FString::Printf(TEXT("expected slot %d unreadable"), Index);
						break;
					}
					const FVector2D Want((*Pos)[0]->AsNumber(), (*Pos)[1]->AsNumber());
					if (Role != FormationNames::ToString(Slots[Index].Role) || FVector2D::Distance(Slots[Index].Position, Want) > TolCm
						|| ShapesAngleDiff(Slots[Index].Yaw, SlotYaw) > TolDeg || FMath::Abs(Arcs[Index] - Arc) > TolDeg)
					{
						Mismatch = FString::Printf(TEXT("slot %d {%s (%.2f, %.2f) %.2f arc %.0f}, expected {%s (%.2f, %.2f) %.2f arc %.0f}"), Index,
							FormationNames::ToString(Slots[Index].Role), Slots[Index].Position.X, Slots[Index].Position.Y, Slots[Index].Yaw, Arcs[Index], *Role, Want.X,
							Want.Y, SlotYaw, Arc);
					}
				}
			}
		}
		else if (Op == TEXT("mirror"))
		{
			const TMap<int32, FFormationShape>* ByCount = File.Formations.Find(FormationName);
			const FFormationShape* Source = ByCount ? ByCount->Find(static_cast<int32>(Count)) : nullptr;
			FString WantFormation;
			const TArray<TSharedPtr<FJsonValue>>* Members = nullptr;
			if (!Source || !(*Out)->TryGetStringField(TEXT("formation"), WantFormation) || !(*Out)->TryGetArrayField(TEXT("members"), Members))
			{
				Mismatch = TEXT("unreadable mirror case / source shape missing");
			}
			else
			{
				const FString GotFormation = Kind == EFormationKind::EchelonLeft ? TEXT("echelon_right") : Kind == EFormationKind::EchelonRight ? TEXT("echelon_left")
					: FormationName;
				const FFormationShape Mirrored = FormationShapes::Mirror(*Source);
				if (GotFormation != WantFormation || Mirrored.Members.Num() != Members->Num())
				{
					Mismatch = FString::Printf(TEXT("mirror %s / %d members, expected %s / %d"), *GotFormation, Mirrored.Members.Num(), *WantFormation, Members->Num());
				}
				for (int32 Index = 0; Mismatch.IsEmpty() && Index < Mirrored.Members.Num(); ++Index)
				{
					const TSharedPtr<FJsonObject> Member = (*Members)[Index]->AsObject();
					FString Role;
					double Fwd = 0.0;
					double Right = 0.0;
					double Look = 0.0;
					double Arc = 0.0;
					const bool bArc = Member.IsValid() && Member->TryGetNumberField(TEXT("arc"), Arc);
					const FFormationShapeMember& Got = Mirrored.Members[Index];
					if (!Member.IsValid() || !Member->TryGetStringField(TEXT("role"), Role) || !Member->TryGetNumberField(TEXT("fwd"), Fwd)
						|| !Member->TryGetNumberField(TEXT("right"), Right) || !Member->TryGetNumberField(TEXT("look"), Look) || Role != FormationShapes::RoleName(Got.Role)
						|| Got.Fwd != static_cast<int32>(Fwd) || Got.Right != static_cast<int32>(Right) || Got.Look != static_cast<int32>(Look)
						|| Got.Arc.IsSet() != bArc || (bArc && Got.Arc.GetValue() != static_cast<int32>(Arc)))
					{
						Mismatch = FString::Printf(TEXT("mirrored member %d {%s %d %d %d}, expected {%s %.0f %.0f %.0f}"), Index, FormationShapes::RoleName(Got.Role), Got.Fwd,
							Got.Right, Got.Look, *Role, Fwd, Right, Look);
					}
				}
			}
		}
		else
		{
			Mismatch = FString::Printf(TEXT("unknown op %s"), *Op);
		}
		if (!Mismatch.IsEmpty())
		{
			if (++Failures <= 12)
			{
				AddError(FString::Printf(TEXT("case %d (%s %s %s/%d): %s"), CaseIndex, *Op, *Fixture, *FormationName, static_cast<int32>(Count), *Mismatch));
			}
		}
	}
	AddInfo(FString::Printf(TEXT("SavedShapes parity: %d cases (%d with a saved shape), %d mismatches"), Checked, Found, Failures));
	TestTrue(TEXT("some cases take a saved shape"), Found > 0);
	return Failures == 0;
}

#endif // WITH_DEV_AUTOMATION_TESTS
