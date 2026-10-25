#include "Formation/FormationTuning.h"

#include "CodexTactics.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace FormationTuning
{
	struct FFormationTuningKey
	{
		const TCHAR* Key;
		float FFormationConfig::* Member;
	};

	/** formation_config.json key -> FFormationConfig member (the struct comments name the same keys). */
	static const FFormationTuningKey FormationTuningKeys[] = {
		{ TEXT("slot.side_offset"), &FFormationConfig::SlotSideOffset },
		{ TEXT("slot.back_offset"), &FFormationConfig::SlotBackOffset },
		{ TEXT("diamond.rear_back"), &FFormationConfig::DiamondRearBack },
		{ TEXT("slot.min_spacing"), &FFormationConfig::SlotMinSpacing },
		{ TEXT("pair.side_offset"), &FFormationConfig::PairSideOffset },
		{ TEXT("pair.back_offset"), &FFormationConfig::PairBackOffset },
		{ TEXT("pair.scan_period"), &FFormationConfig::PairScanPeriod },
		{ TEXT("pair.scan_from"), &FFormationConfig::PairScanFrom },
		{ TEXT("pair.scan_to"), &FFormationConfig::PairScanTo },
		{ TEXT("column.base_back"), &FFormationConfig::ColumnBaseBack },
		{ TEXT("column.spacing"), &FFormationConfig::ColumnSpacing },
		{ TEXT("column.stagger"), &FFormationConfig::ColumnStagger },
		{ TEXT("line.spacing"), &FFormationConfig::LineSpacing },
		{ TEXT("line.fan_inner"), &FFormationConfig::LineFanInner },
		{ TEXT("line.fan_outer"), &FFormationConfig::LineFanOuter },
		{ TEXT("echelon.step"), &FFormationConfig::EchelonStep },
		{ TEXT("echelon.sector_lead"), &FFormationConfig::EchelonSectorLead },
		{ TEXT("echelon.sector_2"), &FFormationConfig::EchelonSector2 },
		{ TEXT("echelon.sector_3"), &FFormationConfig::EchelonSector3 },
		{ TEXT("echelon.sector_4"), &FFormationConfig::EchelonSector4 },
		{ TEXT("blizzard.side_offset"), &FFormationConfig::BlizzardSideOffset },
		{ TEXT("blizzard.back_offset"), &FFormationConfig::BlizzardBackOffset },
		{ TEXT("hedgehog.radius"), &FFormationConfig::HedgehogRadius },
		{ TEXT("wall.spacing"), &FFormationConfig::WallSpacing },
		{ TEXT("wall.face_offset"), &FFormationConfig::WallFaceOffset },
		{ TEXT("wall.flank_deg"), &FFormationConfig::WallFlankDeg },
		{ TEXT("wall.neighbour_deg"), &FFormationConfig::WallNeighbourDeg },
		{ TEXT("wall.end_deg"), &FFormationConfig::WallEndDeg },
		{ TEXT("pace.patrol"), &FFormationConfig::PacePatrol },
		{ TEXT("pace.walk_speed"), &FFormationConfig::PaceWalkSpeed },
		{ TEXT("pace.run_speed"), &FFormationConfig::PaceRunSpeed },
		{ TEXT("move.backward_mult"), &FFormationConfig::MoveBackwardMult },
		{ TEXT("move.catchup_mult"), &FFormationConfig::MoveCatchupMult },
		{ TEXT("facing.turn_speed"), &FFormationConfig::FacingTurnSpeed },
		{ TEXT("scan.period"), &FFormationConfig::ScanPeriod },
		{ TEXT("scan.amp_side"), &FFormationConfig::ScanAmpSide },
		{ TEXT("scan.center_side"), &FFormationConfig::ScanCenterSide },
		{ TEXT("scan.amp_lead"), &FFormationConfig::ScanAmpLead },
		{ TEXT("scan.dwell_exp"), &FFormationConfig::ScanDwellExp },
		{ TEXT("halt.side_deg"), &FFormationConfig::HaltSideDeg },
		{ TEXT("column.side_deg"), &FFormationConfig::ColumnSideDeg },
		{ TEXT("column.narrow_width"), &FFormationConfig::ColumnNarrowWidth },
		{ TEXT("column.narrow_fwd_deg"), &FFormationConfig::ColumnNarrowFwdDeg },
		{ TEXT("column.narrow_back_deg"), &FFormationConfig::ColumnNarrowBackDeg },
		{ TEXT("classify.probe_side"), &FFormationConfig::ClassifyProbeSide },
		{ TEXT("classify.probe_ahead"), &FFormationConfig::ClassifyProbeAhead },
		{ TEXT("classify.hold_distance"), &FFormationConfig::ClassifyHoldDistance },
		{ TEXT("classify.at_wall_dist"), &FFormationConfig::ClassifyAtWallDist },
		{ TEXT("classify.arrive_dist"), &FFormationConfig::ClassifyArriveDist },
		{ TEXT("classify.side_ignore_perp_deg"), &FFormationConfig::ClassifySideIgnorePerpDeg },
		{ TEXT("sight.fov"), &FFormationConfig::SightFov } };

	static const TCHAR* FormationConfigSchema = TEXT("codex.formation_config/1");

	static FFormationConfig FormationTuningCurrent;
	static bool bFormationTuningLoaded = false;

	static const FFormationTuningKey* FindFormationTuningKey(const FString& Key)
	{
		for (const FFormationTuningKey& Entry : FormationTuningKeys)
		{
			if (Key.Equals(Entry.Key, ESearchCase::CaseSensitive))
			{
				return &Entry;
			}
		}
		return nullptr;
	}
}

FString FormationTuning::GetDefaultPath()
{
	return FPaths::ProjectContentDir() / TEXT("Data/FormationLab/formation_config.json");
}

TArray<FString> FormationTuning::KnownKeys()
{
	TArray<FString> Keys;
	for (const FFormationTuningKey& Entry : FormationTuningKeys)
	{
		Keys.Add(Entry.Key);
	}
	return Keys;
}

bool FormationTuning::GetValue(const FFormationConfig& Config, const FString& Key, float& OutValue)
{
	const FFormationTuningKey* Entry = FindFormationTuningKey(Key);
	if (!Entry)
	{
		return false;
	}
	OutValue = Config.*Entry->Member;
	return true;
}

bool FormationTuning::ParseConfigFile(const FString& JsonText, TMap<FString, float>& OutValues, TMap<FString, FVector2D>* OutRanges, FString* OutError)
{
	const auto Fail = [OutError](const FString& Why)
	{
		if (OutError)
		{
			*OutError = Why;
		}
		return false;
	};
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonText), Root) || !Root.IsValid())
	{
		return Fail(TEXT("not a JSON object"));
	}
	FString Schema;
	if (!Root->TryGetStringField(TEXT("schema"), Schema) || Schema != FormationConfigSchema)
	{
		return Fail(FString::Printf(TEXT("schema must be \"%s\""), FormationConfigSchema));
	}
	const TSharedPtr<FJsonObject>* Params = nullptr;
	if (!Root->TryGetObjectField(TEXT("params"), Params) || !Params)
	{
		return Fail(TEXT("params must be an object"));
	}
	OutValues.Reset();
	if (OutRanges)
	{
		OutRanges->Reset();
	}
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Params)->Values)
	{
		const TSharedPtr<FJsonObject>* Param = nullptr;
		double Value = 0.0;
		if (!Pair.Value.IsValid() || !Pair.Value->TryGetObject(Param) || !Param || !(*Param)->TryGetNumberField(TEXT("value"), Value))
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("formation_config.json: %s has no numeric value, skipped"), *Pair.Key);
			continue;
		}
		OutValues.Add(Pair.Key, static_cast<float>(Value));
		double Min = 0.0;
		double Max = 0.0;
		if (OutRanges && (*Param)->TryGetNumberField(TEXT("min"), Min) && (*Param)->TryGetNumberField(TEXT("max"), Max))
		{
			OutRanges->Add(Pair.Key, FVector2D(Min, Max));
		}
	}
	return true;
}

TMap<FString, float> FormationTuning::ReadFlatValues(const FJsonObject& Object)
{
	TMap<FString, float> Values;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object.Values)
	{
		double Value = 0.0;
		if (Pair.Value.IsValid() && Pair.Value->TryGetNumber(Value))
		{
			Values.Add(Pair.Key, static_cast<float>(Value));
		}
	}
	return Values;
}

FFormationConfig FormationTuning::FromValues(const TMap<FString, float>& Values, const FFormationConfig& Base, const TMap<FString, FVector2D>* Ranges,
	TArray<FString>* OutUnknown)
{
	FFormationConfig Config = Base;
	for (const TPair<FString, float>& Pair : Values)
	{
		const FFormationTuningKey* Entry = FindFormationTuningKey(Pair.Key);
		if (!Entry)
		{
			if (OutUnknown)
			{
				OutUnknown->Add(Pair.Key);
			}
			continue;
		}
		float Value = Pair.Value;
		if (const FVector2D* Range = Ranges ? Ranges->Find(Pair.Key) : nullptr)
		{
			const float Clamped = FMath::Clamp(Value, static_cast<float>(Range->X), static_cast<float>(Range->Y));
			if (Clamped != Value)
			{
				UE_LOG(LogCodexTactics, Warning, TEXT("formation_config.json: %s %g clamped into %g..%g"), *Pair.Key, Value, Range->X, Range->Y);
			}
			Value = Clamped;
		}
		Config.*Entry->Member = Value;
	}
	return Config;
}

TMap<FString, float> FormationTuning::ToValues(const FFormationConfig& Config)
{
	TMap<FString, float> Values;
	for (const FFormationTuningKey& Entry : FormationTuningKeys)
	{
		Values.Add(Entry.Key, Config.*Entry.Member);
	}
	return Values;
}

const FFormationConfig& FormationTuning::Get()
{
	if (!bFormationTuningLoaded)
	{
		bFormationTuningLoaded = true;
		if (!ApplyFile(GetDefaultPath()))
		{
			UE_LOG(LogCodexTactics, Log, TEXT("Formation tuning: %s missing / unreadable, defaults kept"), *GetDefaultPath());
		}
	}
	return FormationTuningCurrent;
}

void FormationTuning::Set(const FFormationConfig& Config)
{
	bFormationTuningLoaded = true;
	FormationTuningCurrent = Config;
}

bool FormationTuning::ApplyFile(const FString& Path)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		return false;
	}
	TMap<FString, float> Values;
	TMap<FString, FVector2D> Ranges;
	FString Error;
	if (!ParseConfigFile(Text, Values, &Ranges, &Error))
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("Formation tuning: %s unreadable (%s), values kept"), *Path, *Error);
		return false;
	}
	TArray<FString> Unknown;
	FormationTuningCurrent = FromValues(Values, FFormationConfig(), &Ranges, &Unknown);
	bFormationTuningLoaded = true;
	for (const FString& Key : Unknown)
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("Formation tuning: unknown key %s in %s, skipped"), *Key, *Path);
	}
	UE_LOG(LogCodexTactics, Display, TEXT("Formation tuning from %s: %d values (%d unknown)"), *Path, Values.Num() - Unknown.Num(), Unknown.Num());
	return true;
}
