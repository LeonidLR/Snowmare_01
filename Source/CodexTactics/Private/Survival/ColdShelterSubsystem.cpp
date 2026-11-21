#include "Survival/ColdShelterSubsystem.h"

#include "AI/PmcDefenceRules.h"
#include "Camera/CameraZoneVolume.h"
#include "CodexTactics.h"
#include "Data/LevelJsonRules.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

bool UColdShelterSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UColdShelterSubsystem::BuildStates() const
{
	States.Reset();
	if (!Layout.IsSet())
	{
		return;
	}
	States.Reserve(Layout->Buildings.Num());
	for (const FPmcLayoutBuilding& Building : Layout->Buildings)
	{
		FBuildingState& State = States.AddDefaulted_GetRef();
		State.Open = ColdShelterRules::InitialOpenFlags(Building);
		State.Leak = ColdShelterRules::ComputeLeak(Building, State.Open, Config);
	}
}

void UColdShelterSubsystem::EnsureLoaded() const
{
	if (bTried)
	{
		return;
	}
	bTried = true;
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	// The map's own LevelJson (same source as UPmcSquadSubsystem's defence layout): a -LevelJson= override may be a combat arena
	// without a layout, the map's file is the one the generator wrote with its levelgen_layout.
	const FString MapName = LevelJsonRules::NormalizeMapName(World->GetMapName());
	const FString LevelJsonFile = FPaths::ProjectContentDir() / TEXT("Data/LevelJson") / (MapName + TEXT(".json"));
	FString LevelJsonText;
	TSharedPtr<FJsonObject> LevelJson;
	FString Relative;
	if (!FFileHelper::LoadFileToString(LevelJsonText, *LevelJsonFile)
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(LevelJsonText), LevelJson)
		|| !LevelJson.IsValid()
		|| !LevelJson->TryGetStringField(TEXT("levelgen_layout"), Relative)
		|| Relative.IsEmpty())
	{
		return;
	}
	const FString LayoutPath = FPaths::IsRelative(Relative) ? FPaths::ProjectDir() / Relative : Relative;
	FString LayoutText;
	if (!FFileHelper::LoadFileToString(LayoutText, *LayoutPath))
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("[ColdShelter] levelgen layout missing: %s"), *LayoutPath);
		return;
	}
	FPmcLayoutAccess Parsed;
	TArray<FString> Errors;
	if (!PmcDefenceRules::ParseLayout(LayoutText, Parsed, Errors))
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("[ColdShelter] levelgen layout is not a JSON object: %s"), *LayoutPath);
		return;
	}

	// The numbers: Content/Data/Survival/cold_shelter.json (a missing / unreadable file keeps the code defaults).
	const FString ConfigPath = FPaths::ProjectContentDir() / TEXT("Data/Survival/cold_shelter.json");
	FString ConfigText;
	TSharedPtr<FJsonObject> ConfigJson;
	if (FFileHelper::LoadFileToString(ConfigText, *ConfigPath) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ConfigText), ConfigJson) && ConfigJson.IsValid())
	{
		Config = ColdShelterRules::ConfigFromJson(*ConfigJson, Config);
	}
	else
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("[ColdShelter] %s missing or unreadable, code defaults"), *ConfigPath);
	}

	Layout = MoveTemp(Parsed);
	BuildStates();
	int32 Leaking = 0;
	int32 Openings = 0;
	for (const FBuildingState& State : States)
	{
		Leaking += State.Leak > 0.f ? 1 : 0;
		Openings += State.Open.Num();
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[ColdShelter] %s: %d buildings, %d openings, %d buildings leak at the start, weights door %.1f window %.1f breach %.1f, draught %.1f..%.1f m, from %s"),
		*MapName, States.Num(), Openings, Leaking, Config.WeightDoor, Config.WeightWindow, Config.WeightBreach,
		Config.DraughtFullCm / 100.f, Config.DraughtFadeCm / 100.f, *LayoutPath);
}

void UColdShelterSubsystem::SetLayoutForTest(const FPmcLayoutAccess& InLayout)
{
	bTried = true;
	Layout = InLayout;
	BuildStates();
}

const FColdShelterConfig& UColdShelterSubsystem::GetConfig() const
{
	EnsureLoaded();
	return Config;
}

void UColdShelterSubsystem::SetConfig(const FColdShelterConfig& InConfig)
{
	EnsureLoaded();
	Config = InConfig;
	BuildStates();
}

bool UColdShelterSubsystem::HasLayout() const
{
	EnsureLoaded();
	return Layout.IsSet();
}

const FPmcLayoutAccess* UColdShelterSubsystem::GetLayout() const
{
	EnsureLoaded();
	return Layout.IsSet() ? &Layout.GetValue() : nullptr;
}

FColdShelterSample UColdShelterSubsystem::GetShelterAt(const FVector& Feet) const
{
	const FPmcLayoutAccess* Parsed = GetLayout();
	if (!Parsed || States.Num() != Parsed->Buildings.Num())
	{
		return FColdShelterSample();
	}
	return ColdShelterRules::Sample(*Parsed, Feet, Config,
		[this](int32 Index) { return TConstArrayView<bool>(States[Index].Open); },
		[this](int32 Index) { return States[Index].Leak; });
}

int32 UColdShelterSubsystem::FindBuilding(const FString& BuildingId) const
{
	const FPmcLayoutAccess* Parsed = GetLayout();
	return Parsed ? Parsed->Buildings.IndexOfByPredicate([&BuildingId](const FPmcLayoutBuilding& Building) { return Building.Id == BuildingId; }) : INDEX_NONE;
}

bool UColdShelterSubsystem::ApplyOpening(int32 BuildingIndex, int32 OpeningIndex, bool bOpen)
{
	FBuildingState& State = States[BuildingIndex];
	if (State.Open[OpeningIndex] == bOpen)
	{
		return true;
	}
	State.Open[OpeningIndex] = bOpen;
	const float OldLeak = State.Leak;
	State.Leak = ColdShelterRules::ComputeLeak(Layout->Buildings[BuildingIndex], State.Open, Config);
	UE_LOG(LogCodexTactics, Display, TEXT("[ColdShelter] %s %s %s: leak %.3f -> %.3f"), *Layout->Buildings[BuildingIndex].Id,
		*Layout->Buildings[BuildingIndex].Openings[OpeningIndex].Id, bOpen ? TEXT("opened") : TEXT("sealed"), OldLeak, State.Leak);
	OnLeakChanged.Broadcast(Layout->Buildings[BuildingIndex].Id, OldLeak, State.Leak);
	return true;
}

bool UColdShelterSubsystem::SetOpeningOpen(const FString& BuildingId, const FString& OpeningId, bool bOpen)
{
	const int32 BuildingIndex = FindBuilding(BuildingId);
	if (BuildingIndex == INDEX_NONE || States.Num() != Layout->Buildings.Num())
	{
		return false;
	}
	const int32 OpeningIndex = Layout->Buildings[BuildingIndex].Openings.IndexOfByPredicate([&OpeningId](const FPmcLayoutOpening& Opening) { return Opening.Id == OpeningId; });
	return OpeningIndex != INDEX_NONE && ApplyOpening(BuildingIndex, OpeningIndex, bOpen);
}

bool UColdShelterSubsystem::SetOpeningOpenAt(const FVector2D& WorldXY, int32 Storey, float RadiusCm, bool bOpen, FString* OutBuildingId, FString* OutOpeningId)
{
	const FPmcLayoutAccess* Parsed = GetLayout();
	if (!Parsed || States.Num() != Parsed->Buildings.Num())
	{
		return false;
	}
	int32 BestBuilding = INDEX_NONE;
	int32 BestOpening = INDEX_NONE;
	float BestDistance = RadiusCm;
	for (int32 BuildingIndex = 0; BuildingIndex < Parsed->Buildings.Num(); ++BuildingIndex)
	{
		const TArray<FPmcLayoutOpening>& Openings = Parsed->Buildings[BuildingIndex].Openings;
		for (int32 OpeningIndex = 0; OpeningIndex < Openings.Num(); ++OpeningIndex)
		{
			if (!Openings[OpeningIndex].bHasPos || Openings[OpeningIndex].Floor != Storey)
			{
				continue;
			}
			const float Distance = FVector2D::Distance(Openings[OpeningIndex].Pos, WorldXY);
			if (Distance <= BestDistance)
			{
				BestDistance = Distance;
				BestBuilding = BuildingIndex;
				BestOpening = OpeningIndex;
			}
		}
	}
	if (BestBuilding == INDEX_NONE)
	{
		return false;
	}
	if (OutBuildingId)
	{
		*OutBuildingId = Parsed->Buildings[BestBuilding].Id;
	}
	if (OutOpeningId)
	{
		*OutOpeningId = Parsed->Buildings[BestBuilding].Openings[BestOpening].Id;
	}
	return ApplyOpening(BestBuilding, BestOpening, bOpen);
}

float UColdShelterSubsystem::GetBuildingLeak(const FString& BuildingId) const
{
	const int32 BuildingIndex = FindBuilding(BuildingId);
	return BuildingIndex != INDEX_NONE && States.IsValidIndex(BuildingIndex) ? States[BuildingIndex].Leak : -1.f;
}

bool UColdShelterSubsystem::IsOpeningOpen(const FString& BuildingId, const FString& OpeningId) const
{
	const int32 BuildingIndex = FindBuilding(BuildingId);
	if (BuildingIndex == INDEX_NONE || !States.IsValidIndex(BuildingIndex))
	{
		return false;
	}
	const int32 OpeningIndex = Layout->Buildings[BuildingIndex].Openings.IndexOfByPredicate([&OpeningId](const FPmcLayoutOpening& Opening) { return Opening.Id == OpeningId; });
	return OpeningIndex != INDEX_NONE && States[BuildingIndex].Open[OpeningIndex];
}

bool UColdShelterSubsystem::IsShelteredLocation(const UWorld* World, const FVector& Location)
{
	if (ACameraZoneVolume::IsShelteredLocation(World, Location))
	{
		return true;
	}
	const UColdShelterSubsystem* Shelter = World ? World->GetSubsystem<UColdShelterSubsystem>() : nullptr;
	return Shelter && Shelter->IsInsideBuilding(Location);
}
