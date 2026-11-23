// UE-only (no Godot reference). Window order rules: Interactables/WindowOrderRules.h.
#include "Interactables/WindowOrderRules.h"
#include "CodexTactics.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace WindowOrderRulesLocal
{
	FWindowOrderTuning Current;
	bool bLoaded = false;

	void ReadFlag(const TSharedPtr<FJsonObject>& Json, const TCHAR* Key, bool& Out)
	{
		double Value = 0.0;
		if (Json->TryGetNumberField(Key, Value))
		{
			Out = Value != 0.0;
		}
	}
}

FString WindowOrderRules::FilePath()
{
	return FPaths::ProjectContentDir() / TEXT("Data/Interactables/windows.json");
}

FWindowOrderTuning WindowOrderRules::Parse(const FString& JsonText)
{
	using namespace WindowOrderRulesLocal;
	FWindowOrderTuning Tuning;
	TSharedPtr<FJsonObject> Json;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonText), Json) || !Json.IsValid())
	{
		return Tuning;
	}
	ReadFlag(Json, TEXT("solo_group_combat_windows"), Tuning.bSoloGroupCombatWindows);
	ReadFlag(Json, TEXT("solo_group_whole_squad_only"), Tuning.bSoloGroupWholeSquadOnly);
	ReadFlag(Json, TEXT("solo_run_in_fight_windows"), Tuning.bSoloRunInFightWindows);
	ReadFlag(Json, TEXT("formation_run_in_fight_windows"), Tuning.bFormationRunInFightWindows);
	ReadFlag(Json, TEXT("formation_follow_broken_only"), Tuning.bFormationFollowBrokenOnly);
	return Tuning;
}

void WindowOrderRules::Reload()
{
	using namespace WindowOrderRulesLocal;
	FString Text;
	Current = FFileHelper::LoadFileToString(Text, *FilePath()) ? Parse(Text) : FWindowOrderTuning();
	bLoaded = true;
}

const FWindowOrderTuning& WindowOrderRules::Get()
{
	using namespace WindowOrderRulesLocal;
	if (!bLoaded)
	{
		Reload();
	}
	return Current;
}

bool WindowOrderRules::MoveOpensWindows(bool bSolo, int32 SelectedCount, int32 SquadCount, bool bInFight, bool bSprint, const FWindowOrderTuning& Tuning)
{
	if (!bInFight)
	{
		return false; // rule 3: out of a fight only the window click opens a window
	}
	if (!bSolo)
	{
		return bSprint && Tuning.bFormationRunInFightWindows; // rule 2: the formation never takes a whole window on its own
	}
	const bool bGroup = SelectedCount >= 2 && (!Tuning.bSoloGroupWholeSquadOnly || SelectedCount >= SquadCount);
	return (bGroup && Tuning.bSoloGroupCombatWindows) || (bSprint && Tuning.bSoloRunInFightWindows);
}

bool WindowOrderRules::MayTakeWholeWindow(bool bWindowOrder, bool bFormationFollower, const FWindowOrderTuning& Tuning)
{
	return bWindowOrder && !(bFormationFollower && Tuning.bFormationFollowBrokenOnly);
}
