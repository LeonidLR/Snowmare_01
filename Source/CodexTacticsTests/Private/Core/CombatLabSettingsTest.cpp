#include "Misc/AutomationTest.h"
#include "Combat/CombatPressureRules.h"
#include "Data/LevelJsonRules.h"
#include "Data/WaveConfigTypes.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#if WITH_DEV_AUTOMATION_TESTS

// The Training Ground department "Combat lab" part A (game designer 2026-11-01, docs/design/COMBAT_LAB.md section 7) edits the level JSON
// "pressure" / "cheats" blocks with its own validation (Tools/TrainingGround/src/combatLab/pressureSettings.ts). These tests keep the page
// and LevelJsonRules::ParsePressureSettings consistent: the page's field table (exported by `npm run golden` to
// golden/combatlab_pressure_fields.json) has the C++ defaults, every min / max the page accepts parses unclamped, and where the page says
// the C++ clamps it does; every named preset (Content/Data/CombatLab/presets.json) parses with the level and is taken as written.

namespace CombatLabSettingsTest
{
	using FGetter = TFunction<double(const FCombatPressureConfig&)>;

	/** Every field of the page's table -> its FCombatPressureConfig value. */
	const TMap<FString, FGetter>& Getters()
	{
		static const TMap<FString, FGetter> Map = {
			{ TEXT("enabled"), [](const FCombatPressureConfig& C) { return C.bEnabled ? 1.0 : 0.0; } },
			{ TEXT("first_event_sec"), [](const FCombatPressureConfig& C) { return double(C.FirstEventSeconds); } },
			{ TEXT("interval_sec"), [](const FCombatPressureConfig& C) { return double(C.IntervalSeconds); } },
			{ TEXT("seed"), [](const FCombatPressureConfig& C) { return double(C.Seed); } },
			{ TEXT("group_min"), [](const FCombatPressureConfig& C) { return double(C.GroupMin); } },
			{ TEXT("group_max"), [](const FCombatPressureConfig& C) { return double(C.GroupMax); } },
			{ TEXT("max_alive"), [](const FCombatPressureConfig& C) { return double(C.MaxAlive); } },
			{ TEXT("patrol_share"), [](const FCombatPressureConfig& C) { return double(C.PatrolShare); } },
			{ TEXT("wall_clearance_cm"), [](const FCombatPressureConfig& C) { return double(C.WallClearanceCm); } },
			{ TEXT("min_squad_distance_cm"), [](const FCombatPressureConfig& C) { return double(C.MinSquadDistanceCm); } },
			{ TEXT("max_squad_distance_cm"), [](const FCombatPressureConfig& C) { return double(C.MaxSquadDistanceCm); } },
			{ TEXT("patrol.points"), [](const FCombatPressureConfig& C) { return double(C.PatrolPoints); } },
			{ TEXT("patrol.radius_cm"), [](const FCombatPressureConfig& C) { return double(C.PatrolRadiusCm); } },
			{ TEXT("patrol.wait_sec"), [](const FCombatPressureConfig& C) { return double(C.PatrolWaitSeconds); } },
			{ TEXT("ambush.min_squad_distance_cm"), [](const FCombatPressureConfig& C) { return double(C.AmbushMinDistanceCm); } },
			{ TEXT("ambush.max_squad_distance_cm"), [](const FCombatPressureConfig& C) { return double(C.AmbushMaxDistanceCm); } },
			{ TEXT("ambush.watch_half_angle_deg"), [](const FCombatPressureConfig& C) { return double(C.Watch.WatchHalfAngleDeg); } },
			{ TEXT("ambush.watch_range_cm"), [](const FCombatPressureConfig& C) { return double(C.Watch.WatchRangeCm); } },
			{ TEXT("ambush.strike_range_cm"), [](const FCombatPressureConfig& C) { return double(C.Watch.StrikeRangeCm); } },
			{ TEXT("ambush.spots.wall_standoff_cm"), [](const FCombatPressureConfig& C) { return double(C.Spots.WallStandoffCm); } },
			{ TEXT("ambush.spots.end_inset_cm"), [](const FCombatPressureConfig& C) { return double(C.Spots.EndInsetCm); } },
			{ TEXT("ambush.spots.min_wall_length_cm"), [](const FCombatPressureConfig& C) { return double(C.Spots.MinWallLengthCm); } },
			{ TEXT("ambush.spots.joined_gap_cm"), [](const FCombatPressureConfig& C) { return double(C.Spots.JoinedGapCm); } },
			{ TEXT("ambush.spots.doorway_min_cm"), [](const FCombatPressureConfig& C) { return double(C.Spots.DoorwayMinCm); } },
			{ TEXT("ambush.spots.doorway_max_cm"), [](const FCombatPressureConfig& C) { return double(C.Spots.DoorwayMaxCm); } },
			{ TEXT("ambush.spots.authored_bonus"), [](const FCombatPressureConfig& C) { return double(C.Spots.AuthoredBonus); } },
			{ TEXT("ambush.spots.doorway_bonus"), [](const FCombatPressureConfig& C) { return double(C.Spots.DoorwayBonus); } },
			{ TEXT("ambush.spots.merge_cm"), [](const FCombatPressureConfig& C) { return double(C.Spots.MergeCm); } },
			{ TEXT("ambush.coordinated.share"), [](const FCombatPressureConfig& C) { return double(C.Coordinated.Share); } },
			{ TEXT("ambush.coordinated.bodies_min"), [](const FCombatPressureConfig& C) { return double(C.Coordinated.BodiesMin); } },
			{ TEXT("ambush.coordinated.bodies_max"), [](const FCombatPressureConfig& C) { return double(C.Coordinated.BodiesMax); } },
			{ TEXT("ambush.coordinated.max_alive"), [](const FCombatPressureConfig& C) { return double(C.Coordinated.MaxAlive); } },
			{ TEXT("ambush.coordinated.min_squad_distance_cm"), [](const FCombatPressureConfig& C) { return double(C.Coordinated.MinSquadDistanceCm); } },
			{ TEXT("ambush.coordinated.max_squad_distance_cm"), [](const FCombatPressureConfig& C) { return double(C.Coordinated.MaxSquadDistanceCm); } },
			{ TEXT("ambush.coordinated.anchor_ahead_cm"), [](const FCombatPressureConfig& C) { return double(C.Coordinated.AnchorAheadCm); } },
			{ TEXT("ambush.coordinated.flank_offset_cm"), [](const FCombatPressureConfig& C) { return double(C.Coordinated.FlankOffsetCm); } },
			{ TEXT("ambush.coordinated.front_offset_cm"), [](const FCombatPressureConfig& C) { return double(C.Coordinated.FrontOffsetCm); } },
			{ TEXT("ambush.coordinated.rear_offset_cm"), [](const FCombatPressureConfig& C) { return double(C.Coordinated.RearOffsetCm); } },
			{ TEXT("ambush.coordinated.min_separation_cm"), [](const FCombatPressureConfig& C) { return double(C.Coordinated.MinSeparationCm); } },
			{ TEXT("ambush.coordinated.trigger_range_cm"), [](const FCombatPressureConfig& C) { return double(C.Coordinated.TriggerRangeCm); } },
			{ TEXT("ambush.coordinated.assault_min_sec"), [](const FCombatPressureConfig& C) { return double(C.Coordinated.AssaultMinSeconds); } },
			{ TEXT("ambush.coordinated.assault_max_sec"), [](const FCombatPressureConfig& C) { return double(C.Coordinated.AssaultMaxSeconds); } },
			{ TEXT("ambush.coordinated.block_distance_cm"), [](const FCombatPressureConfig& C) { return double(C.Coordinated.BlockDistanceCm); } },
			{ TEXT("ambush.coordinated.min_ranged"), [](const FCombatPressureConfig& C) { return double(C.Coordinated.MinRanged); } },
			{ TEXT("ambush.relocate.enabled"), [](const FCombatPressureConfig& C) { return C.Relocate.bEnabled ? 1.0 : 0.0; } },
			{ TEXT("ambush.relocate.after_sec"), [](const FCombatPressureConfig& C) { return double(C.Relocate.AfterSeconds); } },
			{ TEXT("ambush.relocate.behind_angle_deg"), [](const FCombatPressureConfig& C) { return double(C.Relocate.BehindAngleDeg); } },
			{ TEXT("ambush.relocate.far_cm"), [](const FCombatPressureConfig& C) { return double(C.Relocate.FarCm); } },
			{ TEXT("ambush.relocate.min_ahead_cos"), [](const FCombatPressureConfig& C) { return double(C.Relocate.MinAheadCos); } },
			{ TEXT("recycle.distance_cm"), [](const FCombatPressureConfig& C) { return double(C.RecycleDistanceCm); } },
			{ TEXT("recycle.after_sec"), [](const FCombatPressureConfig& C) { return double(C.RecycleAfterSeconds); } },
		};
		return Map;
	}

	TSharedPtr<FJsonObject> LoadJson(const FString& Path)
	{
		FString Text;
		TSharedPtr<FJsonObject> Root;
		if (FFileHelper::LoadFileToString(Text, *Path))
		{
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root);
		}
		return Root;
	}

	/** Sets Path (a.b.c) in Object to Value, creating the objects on the way. */
	void SetPath(const TSharedRef<FJsonObject>& Object, const FString& Path, const TSharedRef<FJsonValue>& Value)
	{
		TArray<FString> Keys;
		Path.ParseIntoArray(Keys, TEXT("."));
		TSharedRef<FJsonObject> Node = Object;
		for (int32 Each = 0; Each + 1 < Keys.Num(); ++Each)
		{
			const TSharedPtr<FJsonObject>* Child = nullptr;
			if (!Node->TryGetObjectField(Keys[Each], Child) || !Child || !Child->IsValid())
			{
				TSharedRef<FJsonObject> Created = MakeShared<FJsonObject>();
				Node->SetObjectField(Keys[Each], Created);
				Node = Created;
			}
			else
			{
				Node = Child->ToSharedRef();
			}
		}
		Node->SetField(Keys.Last(), Value);
	}

	/** Parses {Path: Value} (plus the other fields' defaults) -> the config value of Path. */
	bool ParseOne(const FString& Path, double Value, double& OutParsed)
	{
		const TSharedRef<FJsonObject> Pressure = MakeShared<FJsonObject>();
		SetPath(Pressure, Path, MakeShared<FJsonValueNumber>(Value));
		FCombatPressureConfig Config;
		if (!LevelJsonRules::ParsePressureSettings(*Pressure, Config))
		{
			return false;
		}
		OutParsed = Getters()[Path](Config);
		return true;
	}

	/** The value at a dot path of Object, if it is a number / bool. */
	bool TryGetPath(const FJsonObject& Object, const FString& Path, double& OutValue)
	{
		TArray<FString> Keys;
		Path.ParseIntoArray(Keys, TEXT("."));
		const FJsonObject* Node = &Object;
		for (int32 Each = 0; Each + 1 < Keys.Num(); ++Each)
		{
			const TSharedPtr<FJsonObject>* Child = nullptr;
			if (!Node->TryGetObjectField(Keys[Each], Child) || !Child || !Child->IsValid())
			{
				return false;
			}
			Node = Child->Get();
		}
		// By the value's own type (a number would also read as a bool: non-zero = true).
		const TSharedPtr<FJsonValue> Leaf = Node->TryGetField(Keys.Last());
		if (!Leaf.IsValid())
		{
			return false;
		}
		if (Leaf->Type == EJson::Boolean)
		{
			OutValue = Leaf->AsBool() ? 1.0 : 0.0;
			return true;
		}
		return Leaf->Type == EJson::Number && Leaf->TryGetNumber(OutValue);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatLabPressureFieldsGoldenTest, "CodexTactics.LevelJson.PressureFieldsGolden",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCombatLabPressureFieldsGoldenTest::RunTest(const FString&)
{
	using namespace CombatLabSettingsTest;
	const FString Path = FPaths::ProjectDir() / TEXT("Tools/TrainingGround/golden/combatlab_pressure_fields.json");
	const TSharedPtr<FJsonObject> Golden = LoadJson(Path);
	if (!TestTrue(FString::Printf(TEXT("%s loads (npm run golden)"), *Path), Golden.IsValid()) || Golden->GetStringField(TEXT("schema")) != TEXT("codex.combatlab_fields/1"))
	{
		return false;
	}
	FCombatPressureConfig Defaults;
	TestTrue(TEXT("an empty block parses"), LevelJsonRules::ParsePressureSettings(FJsonObject(), Defaults));
	TMap<FString, double> DefaultOf;
	const TArray<TSharedPtr<FJsonValue>>& Fields = Golden->GetArrayField(TEXT("fields"));
	TestEqual(TEXT("every C++ field is on the page"), Fields.Num(), Getters().Num());
	for (const TSharedPtr<FJsonValue>& Value : Fields)
	{
		const TSharedPtr<FJsonObject> Field = Value->AsObject();
		const FString FieldPath = Field->GetStringField(TEXT("path"));
		const FString Kind = Field->GetStringField(TEXT("kind"));
		if (!TestTrue(FString::Printf(TEXT("%s is a C++ field"), *FieldPath), Getters().Contains(FieldPath)))
		{
			continue;
		}
		const double Def = Kind == TEXT("bool") ? (Field->GetBoolField(TEXT("def")) ? 1.0 : 0.0) : Field->GetNumberField(TEXT("def"));
		DefaultOf.Add(FieldPath, Def);
		TestEqual(FString::Printf(TEXT("%s: the page's default is the C++ default"), *FieldPath), Getters()[FieldPath](Defaults), Def, 1e-4);
	}
	for (const TSharedPtr<FJsonValue>& Value : Fields)
	{
		const TSharedPtr<FJsonObject> Field = Value->AsObject();
		const FString FieldPath = Field->GetStringField(TEXT("path"));
		const FString Kind = Field->GetStringField(TEXT("kind"));
		if (Kind == TEXT("bool") || !Getters().Contains(FieldPath))
		{
			continue;
		}
		const FString Clamp = Field->GetStringField(TEXT("cpp_clamp"));
		double Min = 0.0;
		FString MinFrom;
		if (Field->TryGetStringField(TEXT("min_from"), MinFrom))
		{
			Min = DefaultOf.FindRef(MinFrom) + Field->GetNumberField(TEXT("min_offset"));
		}
		else
		{
			Min = Field->GetNumberField(TEXT("min"));
		}
		const double Max = Field->GetNumberField(TEXT("max"));
		double Parsed = 0.0;
		TestTrue(FString::Printf(TEXT("%s: the page's min %g parses unclamped"), *FieldPath, Min), ParseOne(FieldPath, Min, Parsed) && FMath::IsNearlyEqual(Parsed, Min, 1e-4));
		TestTrue(FString::Printf(TEXT("%s: the page's max %g parses unclamped"), *FieldPath, Max), ParseOne(FieldPath, Max, Parsed) && FMath::IsNearlyEqual(Parsed, Max, 1e-4));
		const double Step = Kind == TEXT("integer") ? 1.0 : FMath::Max(0.5, FMath::Abs(Min) * 0.1);
		if (Clamp == TEXT("min") || Clamp == TEXT("both"))
		{
			TestTrue(FString::Printf(TEXT("%s: below the page's min the C++ clamps to it (%g -> %g)"), *FieldPath, Min - Step, Parsed),
				ParseOne(FieldPath, Min - Step, Parsed) && FMath::IsNearlyEqual(Parsed, Min, 1e-4));
		}
		if (Clamp == TEXT("max") || Clamp == TEXT("both"))
		{
			TestTrue(FString::Printf(TEXT("%s: above the page's max the C++ clamps to it"), *FieldPath),
				ParseOne(FieldPath, Max + Step, Parsed) && FMath::IsNearlyEqual(Parsed, Max, 1e-4));
		}
	}
	// Types: the page's allowed / forbidden lists are the parser's.
	const TSharedPtr<FJsonObject> Types = Golden->GetObjectField(TEXT("types"));
	for (const TSharedPtr<FJsonValue>& Name : Types->GetArrayField(TEXT("allowed")))
	{
		const TSharedRef<FJsonObject> Pressure = MakeShared<FJsonObject>();
		SetPath(Pressure, FString::Printf(TEXT("types.%s"), *Name->AsString()), MakeShared<FJsonValueNumber>(1.0));
		FCombatPressureConfig Config;
		TestTrue(FString::Printf(TEXT("type %s parses"), *Name->AsString()), LevelJsonRules::ParsePressureSettings(*Pressure, Config) && Config.TypeWeights.Num() == 1);
	}
	for (const TSharedPtr<FJsonValue>& Name : Types->GetArrayField(TEXT("forbidden")))
	{
		const TSharedRef<FJsonObject> Pressure = MakeShared<FJsonObject>();
		SetPath(Pressure, FString::Printf(TEXT("types.%s"), *Name->AsString()), MakeShared<FJsonValueNumber>(1.0));
		FCombatPressureConfig Config;
		FString Error;
		TestFalse(FString::Printf(TEXT("type %s refused"), *Name->AsString()), LevelJsonRules::ParsePressureSettings(*Pressure, Config, &Error));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatLabPresetsTest, "CodexTactics.LevelJson.CombatLabPresets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCombatLabPresetsTest::RunTest(const FString&)
{
	using namespace CombatLabSettingsTest;
	const TSharedPtr<FJsonObject> File = LoadJson(FPaths::ProjectContentDir() / TEXT("Data/CombatLab/presets.json"));
	const TSharedPtr<FJsonObject> Level = LoadJson(LevelJsonRules::GetLevelsDirectory() / TEXT("L_FormationLabCombat.json"));
	if (!TestTrue(TEXT("presets.json and L_FormationLabCombat.json load"), File.IsValid() && Level.IsValid()))
	{
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>& Presets = File->GetArrayField(TEXT("presets"));
	TestTrue(TEXT("the designer's four presets at least"), Presets.Num() >= 4);
	for (const TSharedPtr<FJsonValue>& Value : Presets)
	{
		const TSharedPtr<FJsonObject> Preset = Value->AsObject();
		const FString Id = Preset->GetStringField(TEXT("id"));
		const TSharedPtr<FJsonObject> Pressure = Preset->GetObjectField(TEXT("pressure"));
		const TSharedPtr<FJsonObject> Cheats = Preset->GetObjectField(TEXT("cheats"));
		FCombatPressureConfig Config;
		FString Error;
		if (!TestTrue(FString::Printf(TEXT("%s: the pressure parses (%s)"), *Id, *Error), LevelJsonRules::ParsePressureSettings(*Pressure, Config, &Error)))
		{
			continue;
		}
		for (const TPair<FString, FGetter>& Each : Getters())
		{
			double Written = 0.0;
			if (TryGetPath(*Pressure, Each.Key, Written))
			{
				TestEqual(FString::Printf(TEXT("%s: %s taken as written"), *Id, *Each.Key), Each.Value(Config), Written, 1e-4);
			}
		}
		// The level with this preset applied parses (CodexTactics.LevelJson.AllFilesParse holds for every preset).
		const TSharedRef<FJsonObject> Applied = MakeShared<FJsonObject>(*Level);
		Applied->SetObjectField(TEXT("pressure"), Pressure);
		Applied->SetObjectField(TEXT("cheats"), Cheats);
		FString Text;
		FJsonSerializer::Serialize(Applied, TJsonWriterFactory<>::Create(&Text));
		FLevelCombatConfig Parsed;
		TestTrue(FString::Printf(TEXT("%s: the level with it parses (%s)"), *Id, *Error), LevelJsonRules::ParseLevel(Text, TEXT("L_FormationLabCombat"), Parsed, Error));
		TestTrue(FString::Printf(TEXT("%s: ... with waves and its route"), *Id), Parsed.Waves.Num() > 0 && Parsed.BotRoute.IsValid());
		TestEqual(FString::Printf(TEXT("%s: immortality as written"), *Id), Parsed.bCheatImmortal, Cheats->GetBoolField(TEXT("immortal")));
		TestEqual(FString::Printf(TEXT("%s: infinite ammo as written"), *Id), Parsed.bCheatInfiniteAmmo, Cheats->GetBoolField(TEXT("infinite_ammo")));
	}
	return true;
}

#endif
