// CodexTactics.LevelGen.Props.* - level authoring stage 1, task C1 (docs/design/levelgen/PROPS_LAYERS.md section 5): the
// codex.levelgen_props/1 file (parse / canonical write / unknown major refused), ApplyOverrides (move / delete / add / set, a deleted id
// becomes a keep-out after a re-roll) and Content/Data/Balance/props_balance.json. Pure, no world. Shared fixture (also read by the TS
// side): Content/Data/LevelGen/props/fixtures/fixture_small.props.json. UE-only (no Godot reference).

#include "LevelGen/LevelPropsFile.h"
#include "Misc/AutomationTest.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

#define LEVEL_PROPS_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.LevelGen.Props." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace LevelPropsTestLocal
{
	bool LoadContentFile(const TCHAR* RelativePath, FString& OutJson)
	{
		return FFileHelper::LoadFileToString(OutJson, *(FPaths::ProjectContentDir() / RelativePath));
	}

	bool LoadPropsFixture(FString& OutJson)
	{
		return LoadContentFile(TEXT("Data/LevelGen/props/fixtures/fixture_small.props.json"), OutJson);
	}

	const FLevelPropsResolvedObject* FindResolved(const FLevelPropsResolved& Resolved, const TCHAR* Id)
	{
		return Resolved.Objects.FindByPredicate([Id](const FLevelPropsResolvedObject& R) { return R.Object.Id == Id; });
	}

	FString Wrap(const FString& Layers, const FString& Overrides = TEXT("[]"))
	{
		return FString::Printf(TEXT("{\"schema\":\"codex.levelgen_props/1\",\"layers\":%s,\"overrides\":%s}"), *Layers, *Overrides);
	}
}

LEVEL_PROPS_TEST(FLevelPropsRoundTripTest, "RoundTrip")
bool FLevelPropsRoundTripTest::RunTest(const FString&)
{
	using namespace LevelPropsTestLocal;
	FString Json;
	if (!TestTrue(TEXT("fixture readable"), LoadPropsFixture(Json)))
	{
		return false;
	}
	FLevelPropsFile A;
	TArray<FString> Errors;
	if (!TestTrue(TEXT("fixture parses"), LevelPropsFile::Parse(Json, A, Errors)) || !TestTrue(TEXT("no errors"), Errors.IsEmpty()))
	{
		return false;
	}
	TestEqual(TEXT("map"), A.Map, FString(TEXT("L_Fixture_Small")));
	TestEqual(TEXT("layout"), A.Layout, FString(TEXT("fixture_small")));
	TestEqual(TEXT("revision"), A.Revision, 14);
	if (!TestEqual(TEXT("layers"), A.Layers.Num(), 3) || !TestEqual(TEXT("overrides"), A.Overrides.Num(), 4))
	{
		return false;
	}
	TestEqual(TEXT("seed"), static_cast<int32>(A.Layers[0].Seed), 7);
	TestTrue(TEXT("rules kept"), A.Layers[0].Rules.IsValid());
	TestTrue(TEXT("report kept"), A.Layers[0].Report.IsValid());
	TestFalse(TEXT("barrels have no report"), A.Layers[1].Report.IsValid());
	TestEqual(TEXT("crate pos"), A.Layers[0].Objects[0].Pos, FIntPoint(300, -500));
	TestTrue(TEXT("crate locked"), A.Layers[0].Objects[0].bLocked);
	TestEqual(TEXT("mine door"), A.Layers[2].Objects[0].Door, FString(TEXT("B_shed#0")));
	TestEqual(TEXT("mine attach"), A.Layers[2].Objects[1].Attach, FString(TEXT("crates_00000007_0")));
	TestTrue(TEXT("add override object"), A.Overrides[2].Op == ELevelPropsOp::Add && A.Overrides[2].Object.Id == TEXT("hand_barrels_0"));

	const FString W1 = LevelPropsFile::Write(A);
	FLevelPropsFile B;
	TestTrue(TEXT("written file parses"), LevelPropsFile::Parse(W1, B, Errors));
	TestTrue(TEXT("no errors on the written file"), Errors.IsEmpty());
	TestEqual(TEXT("write(parse(write(x))) == write(x)"), LevelPropsFile::Write(B), W1);
	TestEqual(TEXT("layers survive"), B.Layers.Num(), A.Layers.Num());
	TestEqual(TEXT("overrides survive"), B.Overrides.Num(), A.Overrides.Num());
	if (B.Layers.Num() == 3)
	{
		TestEqual(TEXT("objects survive"), B.Layers[2].Objects.Num(), 2);
		TestEqual(TEXT("trigger survives"), B.Layers[2].Objects[0].Trigger, FString(TEXT("tripwire")));
	}
	TestTrue(TEXT("unknown root key kept"), W1.Contains(TEXT("\"x_note\": \"unknown root key: must survive a round-trip\"")));
	TestTrue(TEXT("unknown object key kept"), W1.Contains(TEXT("\"x_tag\": \"keep me\"")));
	TestTrue(TEXT("integer cm"), W1.Contains(TEXT("\"pos\": [300, -500]")));
	TestTrue(TEXT("schema written"), W1.Contains(TEXT("\"schema\": \"codex.levelgen_props/1\"")));
	TestTrue(TEXT("opaque numbers stay as written (0.3, no 0.29999)"), W1.Contains(TEXT("\"share\": 0.3")));
	TestTrue(TEXT("stable key order: id before kind before seed"),
		W1.Find(TEXT("\"kind\"")) > W1.Find(TEXT("\"id\"")) && W1.Find(TEXT("\"seed\"")) > W1.Find(TEXT("\"kind\"")));
	TestTrue(TEXT("rules keys sorted: count < min_gap_any_cm < min_spacing_cm < variants"),
		W1.Find(TEXT("\"count\"")) < W1.Find(TEXT("\"min_gap_any_cm\"")) && W1.Find(TEXT("\"min_gap_any_cm\"")) < W1.Find(TEXT("\"min_spacing_cm\""))
		&& W1.Find(TEXT("\"min_spacing_cm\"")) < W1.Find(TEXT("\"variants\"")));
	TestTrue(TEXT("trailing newline"), W1.EndsWith(TEXT("}\n")));
	return true;
}

LEVEL_PROPS_TEST(FLevelPropsCanonicalTest, "Canonical")
bool FLevelPropsCanonicalTest::RunTest(const FString&)
{
	using namespace LevelPropsTestLocal;
	// Opaque keys sort ordinally (case-sensitive, like JS sort): "Zeta" < "alpha" < "beta" (a case-insensitive sort would put Zeta last).
	// Keys that differ only by case ("B" / "b") are not supported: UE's FJsonObject keeps one of them.
	const FString Json = Wrap(TEXT("[{\"id\":\"crates\",\"kind\":\"crates\",\"seed\":1,\"rules\":{\"beta\":1,\"alpha\":{\"z_x\":true,\"Y\":null},\"Zeta\":[1,2.5]},")
		TEXT("\"objects\":[{\"id\":\"crates_00000001_0\",\"variant\":\"crate_s\",\"pos\":[10.5,-2.5],\"yaw\":89.6,\"walk_layer\":null,\"arc\":null}]}]"));
	FLevelPropsFile File;
	TArray<FString> Errors;
	if (!TestTrue(TEXT("parses"), LevelPropsFile::Parse(Json, File, Errors)))
	{
		return false;
	}
	const FLevelPropsObject& O = File.Layers[0].Objects[0];
	TestEqual(TEXT("cm rounded half up, like JS Math.round (-2.5 -> -2)"), O.Pos, FIntPoint(11, -2));
	TestEqual(TEXT("yaw rounded"), O.Yaw, 90);
	TestEqual(TEXT("null walk_layer = absent = ground"), O.WalkLayer, FString(TEXT("ground")));
	TestFalse(TEXT("null arc = absent"), O.Arc.IsSet());
	const FString W = LevelPropsFile::Write(File);
	const int32 KeyBig = W.Find(TEXT("\"Zeta\": [1, 2.5]"), ESearchCase::CaseSensitive);
	const int32 KeyA = W.Find(TEXT("\"alpha\": {"), ESearchCase::CaseSensitive);
	const int32 KeyB = W.Find(TEXT("\"beta\": 1"), ESearchCase::CaseSensitive);
	if (!TestTrue(TEXT("ordinal order Zeta < alpha < beta"), KeyBig != INDEX_NONE && KeyBig < KeyA && KeyA < KeyB))
	{
		AddInfo(W);
	}
	TestTrue(TEXT("nested ordinal order, null kept"), W.Find(TEXT("\"Y\": null"), ESearchCase::CaseSensitive) < W.Find(TEXT("\"z_x\": true")));
	TestTrue(TEXT("written pos is integer"), W.Contains(TEXT("\"pos\": [11, -2]")));
	TestFalse(TEXT("no arc written when absent"), W.Contains(TEXT("\"arc\"")));
	TestTrue(TEXT("an empty overrides array is written"), W.Contains(TEXT("\"overrides\": []")));

	// FJsonObject looks fields up case-insensitively: "VARIANT" is read as the variant and never written twice.
	FLevelPropsFile Upper;
	TestTrue(TEXT("upper-case known key parses"), LevelPropsFile::Parse(Wrap(TEXT("[{\"id\":\"crates\",\"kind\":\"crates\",\"seed\":1,\"objects\":[")
		TEXT("{\"id\":\"crates_00000001_0\",\"VARIANT\":\"crate_l\",\"pos\":[0,0]}]}]")), Upper, Errors));
	const FString WU = LevelPropsFile::Write(Upper);
	TestTrue(TEXT("read as the variant"), WU.Contains(TEXT("\"variant\": \"crate_l\"")));
	TestFalse(TEXT("not kept as an unknown key"), WU.Contains(TEXT("VARIANT"), ESearchCase::CaseSensitive));
	return true;
}

LEVEL_PROPS_TEST(FLevelPropsUnknownMajorTest, "UnknownMajor")
bool FLevelPropsUnknownMajorTest::RunTest(const FString&)
{
	using namespace LevelPropsTestLocal;
	FLevelPropsFile File;
	TArray<FString> Errors;
	AddExpectedMessage(TEXT("Props file:"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0, false); // Parse logs each error
	TestFalse(TEXT("/2 is refused"), LevelPropsFile::Parse(TEXT("{\"schema\":\"codex.levelgen_props/2\",\"layers\":[{\"id\":\"crates\",\"kind\":\"crates\",\"seed\":1}]}"), File, Errors));
	TestTrue(TEXT("with one error naming the version"), Errors.Num() == 1 && Errors[0].Contains(TEXT("codex.levelgen_props/2")));
	TestTrue(TEXT("nothing read"), File.Layers.IsEmpty());

	Errors.Reset();
	TestFalse(TEXT("/10 is refused"), LevelPropsFile::Parse(TEXT("{\"schema\":\"codex.levelgen_props/10\"}"), File, Errors));
	Errors.Reset();
	TestFalse(TEXT("/1.5 is refused"), LevelPropsFile::Parse(TEXT("{\"schema\":\"codex.levelgen_props/1.5\"}"), File, Errors));
	Errors.Reset();
	TestFalse(TEXT("a foreign schema is refused"), LevelPropsFile::Parse(TEXT("{\"schema\":\"codex.levelgen_layout/1\"}"), File, Errors));
	Errors.Reset();
	TestFalse(TEXT("a missing schema is refused"), LevelPropsFile::Parse(TEXT("{\"layers\":[]}"), File, Errors));
	Errors.Reset();
	TestFalse(TEXT("invalid JSON is refused, no crash"), LevelPropsFile::Parse(TEXT("{ nope"), File, Errors));
	TestFalse(TEXT("with an error"), Errors.IsEmpty());
	Errors.Reset();
	TestFalse(TEXT("a root array is refused"), LevelPropsFile::Parse(TEXT("[]"), File, Errors));
	Errors.Reset();
	TestTrue(TEXT("a minimal /1 file is accepted"), LevelPropsFile::Parse(TEXT("{\"schema\":\"codex.levelgen_props/1\"}"), File, Errors));
	TestTrue(TEXT("null optional root fields are absent"), LevelPropsFile::Parse(
		TEXT("{\"schema\":\"codex.levelgen_props/1\",\"map\":null,\"revision\":null,\"layers\":null,\"overrides\":null}"), File, Errors));
	TestTrue(TEXT("no errors"), Errors.IsEmpty());
	return true;
}

LEVEL_PROPS_TEST(FLevelPropsValidationTest, "Validation")
bool FLevelPropsValidationTest::RunTest(const FString&)
{
	using namespace LevelPropsTestLocal;
	FLevelPropsFile File;
	TArray<FString> Errors;
	AddExpectedMessage(TEXT("Props file:"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0, false);

	TestFalse(TEXT("a layer without a seed"), LevelPropsFile::Parse(Wrap(TEXT("[{\"id\":\"crates\",\"kind\":\"crates\"}]")), File, Errors));
	TestTrue(TEXT("... is dropped"), File.Layers.IsEmpty());
	Errors.Reset();
	TestFalse(TEXT("a bad kind"), LevelPropsFile::Parse(Wrap(TEXT("[{\"id\":\"x\",\"kind\":\"lasers\",\"seed\":1}]")), File, Errors));
	Errors.Reset();
	TestFalse(TEXT("a fractional seed"), LevelPropsFile::Parse(Wrap(TEXT("[{\"id\":\"crates\",\"kind\":\"crates\",\"seed\":1.5}]")), File, Errors));
	Errors.Reset();
	TestFalse(TEXT("a negative seed"), LevelPropsFile::Parse(Wrap(TEXT("[{\"id\":\"crates\",\"kind\":\"crates\",\"seed\":-1}]")), File, Errors));
	Errors.Reset();
	TestFalse(TEXT("a seed as a string (no silent coercion)"), LevelPropsFile::Parse(Wrap(TEXT("[{\"id\":\"crates\",\"kind\":\"crates\",\"seed\":\"7\"}]")), File, Errors));
	Errors.Reset();
	TestTrue(TEXT("the largest uint32 seed"), LevelPropsFile::Parse(Wrap(TEXT("[{\"id\":\"crates\",\"kind\":\"crates\",\"seed\":4294967295}]")), File, Errors));
	TestEqual(TEXT("... is read exactly"), File.Layers.Num() == 1 ? File.Layers[0].Seed : 0u, 4294967295u);
	TestTrue(TEXT("... and written exactly"), LevelPropsFile::Write(File).Contains(TEXT("\"seed\": 4294967295")));

	Errors.Reset();
	TestFalse(TEXT("a bad object is an error"), LevelPropsFile::Parse(Wrap(TEXT("[{\"id\":\"crates\",\"kind\":\"crates\",\"seed\":1,\"objects\":[")
		TEXT("{\"id\":\"crates_00000001_0\",\"variant\":\"crate_s\",\"pos\":[0,0]},")
		TEXT("{\"id\":\"crates_00000001_1\",\"variant\":\"crate_s\",\"pos\":[0]},")
		TEXT("{\"id\":\"crates_00000001_2\",\"variant\":\"crate_s\",\"pos\":[0,0],\"locked\":\"yes\"},")
		TEXT("{\"id\":\"crates_00000001_0\",\"variant\":\"crate_l\",\"pos\":[5,5]}]}]")), File, Errors));
	if (TestEqual(TEXT("... but the layer stays"), File.Layers.Num(), 1))
	{
		TestEqual(TEXT("... with its good objects only"), File.Layers[0].Objects.Num(), 1);
	}
	TestTrue(TEXT("bad pos named"), Errors.ContainsByPredicate([](const FString& E) { return E.Contains(TEXT("crates_00000001_1")) && E.Contains(TEXT("pos")); }));
	TestTrue(TEXT("wrong type named"), Errors.ContainsByPredicate([](const FString& E) { return E.Contains(TEXT("'locked' must be")); }));
	TestTrue(TEXT("duplicate id named"), Errors.ContainsByPredicate([](const FString& E) { return E.Contains(TEXT("duplicate object id")); }));

	Errors.Reset();
	TestFalse(TEXT("an unknown op"), LevelPropsFile::Parse(Wrap(TEXT("[]"), TEXT("[{\"op\":\"spin\",\"id\":\"x\"}]")), File, Errors));
	Errors.Reset();
	TestFalse(TEXT("set without fields"), LevelPropsFile::Parse(Wrap(TEXT("[]"), TEXT("[{\"op\":\"set\",\"id\":\"x\"}]")), File, Errors));
	Errors.Reset();
	TestFalse(TEXT("add with a non-hand id"), LevelPropsFile::Parse(Wrap(TEXT("[]"),
		TEXT("[{\"op\":\"add\",\"object\":{\"id\":\"barrels_00000001_0\",\"variant\":\"barrel\",\"pos\":[0,0]}}]")), File, Errors));
	TestTrue(TEXT("bad overrides dropped"), File.Overrides.IsEmpty());
	return true;
}

LEVEL_PROPS_TEST(FLevelPropsOverridesTest, "Overrides")
bool FLevelPropsOverridesTest::RunTest(const FString&)
{
	using namespace LevelPropsTestLocal;
	FString Json;
	FLevelPropsFile File;
	TArray<FString> Errors;
	if (!TestTrue(TEXT("fixture parses"), LoadPropsFixture(Json) && LevelPropsFile::Parse(Json, File, Errors)))
	{
		return false;
	}
	TArray<FString> Warnings;
	const FLevelPropsResolved Resolved = LevelPropsFile::ApplyOverrides(File, &Warnings);
	TestTrue(TEXT("no warnings"), Warnings.IsEmpty());
	TestEqual(TEXT("6 stored - 1 deleted + 1 added"), Resolved.Objects.Num(), 6);

	// move
	const FLevelPropsResolvedObject* Moved = FindResolved(Resolved, TEXT("barrels_00000003_0"));
	if (TestNotNull(TEXT("moved object kept"), Moved))
	{
		TestEqual(TEXT("moved pos"), Moved->Object.Pos, FIntPoint(-1180, -340));
		TestEqual(TEXT("moved yaw"), Moved->Object.Yaw, 30);
		TestEqual(TEXT("no z_offset given"), Moved->ZOffsetCm, 0);
		TestTrue(TEXT("a moved object is pinned"), Moved->bPinned);
		TestEqual(TEXT("variant untouched"), Moved->Object.Variant, FString(TEXT("barrel_explosive")));
	}
	// delete -> hidden + keep-out
	TestNull(TEXT("deleted object hidden"), FindResolved(Resolved, TEXT("crates_00000007_1")));
	if (TestEqual(TEXT("one keep-out"), Resolved.KeepOuts.Num(), 1))
	{
		const FLevelPropsKeepOut& KeepOut = Resolved.KeepOuts[0];
		TestEqual(TEXT("keep-out id"), KeepOut.Id, FString(TEXT("crates_00000007_1")));
		TestEqual(TEXT("keep-out layer"), KeepOut.LayerId, FString(TEXT("crates")));
		TestEqual(TEXT("keep-out pos"), KeepOut.Pos, FIntPoint(500, 300));
		TestEqual(TEXT("keep-out radius = the layer's min_spacing_cm"), KeepOut.RadiusCm, 250);
	}
	// add
	const FLevelPropsResolvedObject* Added = FindResolved(Resolved, TEXT("hand_barrels_0"));
	if (TestNotNull(TEXT("hand object added"), Added))
	{
		TestTrue(TEXT("hand + pinned"), Added->bHand && Added->bPinned);
		TestEqual(TEXT("layer from the id"), Added->LayerId, FString(TEXT("barrels")));
		TestEqual(TEXT("origin"), Added->Object.Origin, FString(TEXT("hand")));
		TestEqual(TEXT("added pos"), Added->Object.Pos, FIntPoint(-1500, 1200));
	}
	// set
	const FLevelPropsResolvedObject* Set = FindResolved(Resolved, TEXT("crates_00000007_0"));
	if (TestNotNull(TEXT("set object kept"), Set))
	{
		TestEqual(TEXT("variant set"), Set->Object.Variant, FString(TEXT("crate_l")));
		TestEqual(TEXT("loot table set"), Set->Object.LootTable, FString(TEXT("ammo_common")));
		TestTrue(TEXT("a set object is pinned"), Set->bPinned);
		TestEqual(TEXT("pos untouched"), Set->Object.Pos, FIntPoint(300, -500));
	}
	const FLevelPropsResolvedObject* Untouched = FindResolved(Resolved, TEXT("barrels_00000003_1"));
	if (TestNotNull(TEXT("untouched object kept"), Untouched))
	{
		TestFalse(TEXT("an untouched object is not pinned"), Untouched->bPinned);
	}
	TestEqual(TEXT("ApplyOverrides leaves the file alone"), File.Layers[0].Objects[0].Variant, FString(TEXT("crate_s")));

	// A re-roll of the crates layer drops the old ids: the keep-out must stay, the id must not come back.
	FLevelPropsFile Rerolled = File;
	FLevelPropsObject Fresh;
	Fresh.Id = TEXT("crates_00000008_0");
	Fresh.Variant = TEXT("crate_s");
	Fresh.Pos = FIntPoint(-700, 600);
	Rerolled.Layers[0].Seed = 8;
	Rerolled.Layers[0].Objects = { Fresh };
	Warnings.Reset();
	const FLevelPropsResolved After = LevelPropsFile::ApplyOverrides(Rerolled, &Warnings);
	if (TestEqual(TEXT("keep-out survives the re-roll"), After.KeepOuts.Num(), 1))
	{
		TestEqual(TEXT("at the same spot"), After.KeepOuts[0].Pos, FIntPoint(500, 300));
		TestEqual(TEXT("with the layer's spacing"), After.KeepOuts[0].RadiusCm, 250);
	}
	TestNotNull(TEXT("the new roll is present"), FindResolved(After, TEXT("crates_00000008_0")));
	TestNull(TEXT("the deleted id stays gone"), FindResolved(After, TEXT("crates_00000007_1")));
	// the set of the re-rolled-away crates_00000007_0 is a warning, not a crash
	TestEqual(TEXT("one warning: the set of a vanished id"), Warnings.Num(), 1);
	TestTrue(TEXT("... naming it"), Warnings.Num() == 1 && Warnings[0].Contains(TEXT("crates_00000007_0")));
	return true;
}

LEVEL_PROPS_TEST(FLevelPropsOverrideEdgesTest, "OverrideEdges")
bool FLevelPropsOverrideEdgesTest::RunTest(const FString&)
{
	using namespace LevelPropsTestLocal;
	const FString Json = Wrap(
		TEXT("[{\"id\":\"crates\",\"kind\":\"crates\",\"seed\":1,\"objects\":[")
		TEXT("{\"id\":\"crates_00000001_0\",\"variant\":\"crate_s\",\"pos\":[0,0],\"loot_table\":\"ammo_common\",\"arc\":45,\"x_flag\":1}]},")
		TEXT("{\"id\":\"barrels\",\"kind\":\"barrels\",\"seed\":2,\"enabled\":false,\"rules\":{\"min_spacing_cm\":600},\"objects\":[")
		TEXT("{\"id\":\"barrels_00000002_0\",\"variant\":\"barrel\",\"pos\":[100,100]}]}]"),
		TEXT("[{\"op\":\"set\",\"id\":\"crates_00000001_0\",\"fields\":{\"loot_table\":null,\"arc\":null,\"x_flag\":null,\"explosive\":true,\"yaw\":\"east\",\"id\":\"other\"}},")
		TEXT("{\"op\":\"move\",\"id\":\"crates_00000001_0\",\"pos\":[50,60],\"z_offset\":300,\"walk_layer\":\"roof:B_shed\"},")
		TEXT("{\"op\":\"move\",\"id\":\"barrels_00000002_0\",\"pos\":[1,1]},")
		TEXT("{\"op\":\"delete\",\"id\":\"barrels_00000002_0\",\"pos\":[100,100]},")
		TEXT("{\"op\":\"add\",\"object\":{\"id\":\"hand_barrels_0\",\"variant\":\"barrel\",\"pos\":[0,500]}},")
		TEXT("{\"op\":\"add\",\"object\":{\"id\":\"hand_crates_0\",\"variant\":\"crate_l\",\"pos\":[900,900]}},")
		TEXT("{\"op\":\"delete\",\"id\":\"hand_crates_0\"},")
		TEXT("{\"op\":\"delete\",\"id\":\"crates_00000001_9\"}]"));
	FLevelPropsFile File;
	TArray<FString> Errors;
	if (!TestTrue(TEXT("parses"), LevelPropsFile::Parse(Json, File, Errors)))
	{
		return false;
	}
	TArray<FString> Warnings;
	const FLevelPropsResolved Resolved = LevelPropsFile::ApplyOverrides(File, &Warnings);
	TestEqual(TEXT("only the crate survives (disabled layer, hand add deleted)"), Resolved.Objects.Num(), 1);
	const FLevelPropsResolvedObject* Crate = FindResolved(Resolved, TEXT("crates_00000001_0"));
	if (TestNotNull(TEXT("crate"), Crate))
	{
		TestTrue(TEXT("set null clears loot_table"), Crate->Object.LootTable.IsEmpty());
		TestFalse(TEXT("set null clears arc"), Crate->Object.Arc.IsSet());
		TestFalse(TEXT("set null removes an extra field"), Crate->Object.Extra.Contains(TEXT("x_flag")));
		TestTrue(TEXT("set of an unknown kind field is kept"), Crate->Object.Extra.Contains(TEXT("explosive")));
		TestEqual(TEXT("wrong-typed yaw skipped"), Crate->Object.Yaw, 0);
		TestEqual(TEXT("id immutable"), Crate->Object.Id, FString(TEXT("crates_00000001_0")));
		TestEqual(TEXT("move pos"), Crate->Object.Pos, FIntPoint(50, 60));
		TestEqual(TEXT("move z_offset"), Crate->ZOffsetCm, 300);
		TestEqual(TEXT("move walk layer"), Crate->Object.WalkLayer, FString(TEXT("roof:B_shed")));
		TestTrue(TEXT("pinned"), Crate->bPinned);
	}
	// The disabled layer's move + delete are skipped quietly; a hand delete leaves no disk; a delete with neither pos nor object warns.
	TestEqual(TEXT("no keep-outs"), Resolved.KeepOuts.Num(), 0);
	TestEqual(TEXT("3 warnings: wrong-typed yaw, immutable id, delete without pos"), Warnings.Num(), 3);
	TestTrue(TEXT("yaw warning"), Warnings.ContainsByPredicate([](const FString& W) { return W.Contains(TEXT("'yaw'")); }));
	TestTrue(TEXT("id warning"), Warnings.ContainsByPredicate([](const FString& W) { return W.Contains(TEXT("immutable")); }));
	TestTrue(TEXT("no-pos delete warning"), Warnings.ContainsByPredicate([](const FString& W) { return W.Contains(TEXT("crates_00000001_9")); }));
	TestFalse(TEXT("the disabled layer is never warned about"), Warnings.ContainsByPredicate([](const FString& W) { return W.Contains(TEXT("barrels_00000002_0")); }));

	// Re-enabled: the same move / delete now apply, and the delete leaves a disk with that layer's spacing.
	File.Layers[1].bEnabled = true;
	const FLevelPropsResolved Enabled = LevelPropsFile::ApplyOverrides(File);
	TestNull(TEXT("re-enabled: the deleted barrel is hidden"), FindResolved(Enabled, TEXT("barrels_00000002_0")));
	TestNotNull(TEXT("re-enabled: the hand barrel is added"), FindResolved(Enabled, TEXT("hand_barrels_0")));
	const FLevelPropsKeepOut* Disk = Enabled.KeepOuts.FindByPredicate([](const FLevelPropsKeepOut& K) { return K.Id == TEXT("barrels_00000002_0"); });
	if (TestNotNull(TEXT("re-enabled: keep-out of the deleted barrel"), Disk))
	{
		TestEqual(TEXT("at the delete's pos"), Disk->Pos, FIntPoint(100, 100));
		TestEqual(TEXT("radius 600"), Disk->RadiusCm, 600);
	}
	return true;
}

LEVEL_PROPS_TEST(FLevelPropsFrozenTest, "Frozen")
bool FLevelPropsFrozenTest::RunTest(const FString&)
{
	using namespace LevelPropsTestLocal;
	FString Json;
	FLevelPropsFile File;
	TArray<FString> Errors;
	if (!TestTrue(TEXT("fixture parses"), LoadPropsFixture(Json) && LevelPropsFile::Parse(Json, File, Errors)))
	{
		return false;
	}
	TestFalse(TEXT("fixture is not frozen"), File.bFrozen);
	File.bFrozen = true;
	const FString Written = LevelPropsFile::Write(File);
	TestTrue(TEXT("written as true"), Written.Contains(TEXT("\"frozen\": true")));
	FLevelPropsFile Back;
	TestTrue(TEXT("parses again"), LevelPropsFile::Parse(Written, Back, Errors));
	TestTrue(TEXT("frozen survives the round-trip"), Back.bFrozen);
	TestEqual(TEXT("revision untouched by the library"), Back.Revision, File.Revision);
	TestEqual(TEXT("second write is identical"), LevelPropsFile::Write(Back), Written);
	return true;
}

LEVEL_PROPS_TEST(FLevelPropsBalanceFileTest, "BalanceFile")
bool FLevelPropsBalanceFileTest::RunTest(const FString&)
{
	using namespace LevelPropsTestLocal;
	FString Json;
	if (!TestTrue(TEXT("props_balance.json readable"), LoadContentFile(TEXT("Data/Balance/props_balance.json"), Json)))
	{
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	if (!TestTrue(TEXT("valid JSON"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) && Root.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("schema"), Root->GetStringField(TEXT("schema")), FString(TEXT("codex.props_balance/1")));
	const TSharedPtr<FJsonObject>* Kinds = nullptr;
	const TSharedPtr<FJsonObject>* Presets = nullptr;
	const TSharedPtr<FJsonObject>* LootTables = nullptr;
	const TSharedPtr<FJsonObject>* Budgets = nullptr;
	const TSharedPtr<FJsonObject>* ClassMap = nullptr;
	if (!TestTrue(TEXT("all blocks present"), Root->TryGetObjectField(TEXT("kinds"), Kinds) && Root->TryGetObjectField(TEXT("presets"), Presets)
		&& Root->TryGetObjectField(TEXT("loot_tables"), LootTables) && Root->TryGetObjectField(TEXT("budgets"), Budgets)
		&& Root->TryGetObjectField(TEXT("class_map"), ClassMap)))
	{
		return false;
	}

	// Every variant of the shared fixture (stored + hand-added) has a class map entry; the presets it names exist.
	FString FixtureJson;
	FLevelPropsFile Fixture;
	TArray<FString> Errors;
	if (TestTrue(TEXT("fixture parses"), LoadPropsFixture(FixtureJson) && LevelPropsFile::Parse(FixtureJson, Fixture, Errors)))
	{
		for (const FLevelPropsResolvedObject& R : LevelPropsFile::ApplyOverrides(Fixture).Objects)
		{
			TestTrue(FString::Printf(TEXT("class_map has '%s'"), *R.Object.Variant), (*ClassMap)->HasTypedField<EJson::String>(R.Object.Variant));
		}
		for (const FLevelPropsLayer& Layer : Fixture.Layers)
		{
			if (!Layer.Preset.IsEmpty())
			{
				TestTrue(FString::Printf(TEXT("preset '%s' exists"), *Layer.Preset), (*Presets)->HasTypedField<EJson::Object>(Layer.Preset));
			}
		}
	}
	TestTrue(TEXT("the standard exclude preset exists"), (*Presets)->HasTypedField<EJson::Object>(TEXT("standard")));

	// Designer 6.1 #5: explosives >= 400 cm apart, the blast smaller than that gap so only a cluster chains.
	const double MinGap = (*Presets)->GetObjectField(TEXT("barrels_default"))->GetObjectField(TEXT("explosive"))->GetNumberField(TEXT("min_gap_cm"));
	TestEqual(TEXT("explosive min gap 400"), MinGap, 400.0);
	TestTrue(TEXT("blast radius < explosive gap"),
		(*Kinds)->GetObjectField(TEXT("barrel_explosive"))->GetNumberField(TEXT("explosion_radius_cm")) < MinGap);
	// Designer 6.1 #4: a visible mine marker within 3 m.
	TestEqual(TEXT("mine marker 300 cm"), (*Kinds)->GetObjectField(TEXT("mine"))->GetNumberField(TEXT("marker_visible_cm")), 300.0);

	// Section 3.3 budgets: per-layer caps fit under the total.
	double LayerSum = 0.0;
	for (const TCHAR* Kind : { TEXT("crates"), TEXT("barrels"), TEXT("turrets"), TEXT("mines"), TEXT("loot") })
	{
		TestTrue(FString::Printf(TEXT("budget for %s"), Kind), (*Budgets)->HasTypedField<EJson::Number>(Kind));
		LayerSum += (*Budgets)->GetNumberField(Kind);
	}
	TestTrue(TEXT("layer caps <= total"), LayerSum <= (*Budgets)->GetNumberField(TEXT("total")));
	TestEqual(TEXT("total 400"), (*Budgets)->GetNumberField(TEXT("total")), 400.0);

	// Every loot table the loot preset names exists.
	const TArray<TSharedPtr<FJsonValue>>& Tables = (*Presets)->GetObjectField(TEXT("loot_default"))->GetArrayField(TEXT("tables"));
	TestTrue(TEXT("loot preset names tables"), Tables.Num() > 0);
	for (const TSharedPtr<FJsonValue>& Table : Tables)
	{
		TestTrue(FString::Printf(TEXT("loot table '%s' exists"), *Table->AsString()), (*LootTables)->HasTypedField<EJson::Object>(Table->AsString()));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
