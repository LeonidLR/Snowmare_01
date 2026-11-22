// Story-showcase labels (designer test 2026-11-22, item 6): a debug / test overlay in the designer's language. Private/Debug: the player-text
// guard (CodexTactics.Text.NoCyrillicInPlayerText) skips this folder - these are no HUD texts.
#include "Debug/StoryLabelRules.h"

namespace StoryLabelLocal
{
	struct FStory
	{
		const TCHAR* Id;
		int32 Number;
		const TCHAR* Name;
	};

	// docs/design/levelgen/gdd/building-stories.md, the designer's names of the showcase (STORY_SHOWCASE_2026-11-22.md).
	const FStory Stories[] = {
		{ TEXT("open"), 1, TEXT("Открытый дом") },
		{ TEXT("pass_through"), 2, TEXT("Сквозной проход") },
		{ TEXT("locked_door"), 3, TEXT("Запертая дверь") },
		{ TEXT("rubble_escape"), 4, TEXT("Завал на лестнице + пожарная лестница") },
		{ TEXT("split_wings"), 5, TEXT("Разделённые крылья") },
		{ TEXT("collapsed_floor"), 6, TEXT("Провал перекрытия") },
		{ TEXT("roof_outside"), 7, TEXT("Крыша только снаружи") },
		{ TEXT("barred_ground"), 8, TEXT("Решётки на первом этаже") },
		{ TEXT("broken_back_windows"), 9, TEXT("Разбитые окна сзади") },
		{ TEXT("snow_drift"), 10, TEXT("Сугроб у входа") },
		{ TEXT("iced_escape"), 11, TEXT("Обледенелая пожарная лестница") },
		{ TEXT("facade_collapse"), 12, TEXT("Обрушение фасада") },
		{ TEXT("burnt_flight"), 13, TEXT("Сгоревший марш") },
		{ TEXT("neighbour_hole"), 14, TEXT("Пролом к соседу") },
	};

	const FStory* Find(const FString& Id)
	{
		for (const FStory& Story : Stories)
		{
			if (Id.Equals(Story.Id, ESearchCase::IgnoreCase))
			{
				return &Story;
			}
		}
		return nullptr;
	}
}

int32 StoryLabelRules::StoryNumber(const FString& StoryId)
{
	const StoryLabelLocal::FStory* Story = StoryLabelLocal::Find(StoryId);
	return Story ? Story->Number : 0;
}

FString StoryLabelRules::StoryTitle(const FString& StoryId)
{
	const StoryLabelLocal::FStory* Story = StoryLabelLocal::Find(StoryId);
	return Story ? FString::Printf(TEXT("%d · %s"), Story->Number, Story->Name) : StoryId;
}

FString StoryLabelRules::HouseLabel(const TArray<FString>& StoryIds)
{
	TArray<FString> Titles;
	for (const FString& Id : StoryIds)
	{
		if (!Id.IsEmpty())
		{
			Titles.Add(StoryTitle(Id));
		}
	}
	return Titles.IsEmpty() ? StoryTitle(TEXT("open")) : FString::Join(Titles, TEXT(" + "));
}

FString StoryLabelRules::DoorLabel(const FString& State, const FString& Strength, int32 StoryNumberOfDoor, bool bShowOpen)
{
	FString Word;
	if (State == TEXT("locked"))
	{
		Word = TEXT("заперта");
	}
	else if (State == TEXT("closed"))
	{
		Word = TEXT("закрыта");
	}
	else if (State == TEXT("barricaded"))
	{
		Word = TEXT("заколочена");
	}
	else if (State == TEXT("sealed"))
	{
		Word = TEXT("завалена");
	}
	else if (State == TEXT("open") && bShowOpen)
	{
		Word = TEXT("открыта");
	}
	if (Word.IsEmpty())
	{
		return FString();
	}
	if (Strength == TEXT("wood"))
	{
		Word += TEXT(" · дерево");
	}
	else if (Strength == TEXT("steel"))
	{
		Word += TEXT(" · сталь");
	}
	else if (Strength == TEXT("reinforced"))
	{
		Word += TEXT(" · усиленная");
	}
	if (StoryNumberOfDoor > 0)
	{
		Word += FString::Printf(TEXT(" · история %d"), StoryNumberOfDoor);
	}
	return Word;
}

bool StoryLabelRules::ShouldShow(int32 Console, const FString& MapName)
{
	if (Console >= 1)
	{
		return true;
	}
	return Console < 0 && MapName.Contains(TEXT("StoryShowcase"));
}

bool StoryLabelRules::IsDoorLabelHidden(bool bSameBuilding, int32 LabelStorey, int32 SelectedStorey, bool bStoryDoor)
{
	if (bSameBuilding && SelectedStorey != INDEX_NONE)
	{
		return LabelStorey != SelectedStorey;
	}
	return LabelStorey > 0 && !bStoryDoor;
}

void StoryLabelRules::DoorStoriesFromBricks(const FString& StoryId, const TArray<FString>& Bricks, TMap<FString, int32>& InOutDoorStory)
{
	const int32 Number = StoryNumber(StoryId);
	for (const FString& Brick : Bricks)
	{
		// "door_state(<opening id>,<state>)"
		if (Brick.StartsWith(TEXT("door_state(")) && Brick.EndsWith(TEXT(")")))
		{
			FString Id;
			FString State;
			if (Brick.Mid(11, Brick.Len() - 12).Split(TEXT(","), &Id, &State))
			{
				InOutDoorStory.Add(Id.TrimStartAndEnd(), Number);
			}
		}
	}
}
