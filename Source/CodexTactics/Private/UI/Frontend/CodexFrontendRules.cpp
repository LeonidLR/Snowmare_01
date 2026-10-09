#include "UI/Frontend/CodexFrontendRules.h"

#define LOCTEXT_NAMESPACE "CodexFrontendRules"

FName CodexFrontendRules::GetEntryId(ECodexMainMenuEntry Entry)
{
	switch (Entry)
	{
	case ECodexMainMenuEntry::Continue: return TEXT("Continue");
	case ECodexMainMenuEntry::NewGame: return TEXT("NewGame");
	case ECodexMainMenuEntry::LoadGame: return TEXT("LoadGame");
	case ECodexMainMenuEntry::Options: return TEXT("Options");
	case ECodexMainMenuEntry::Credits: return TEXT("Credits");
	case ECodexMainMenuEntry::Quit: return TEXT("Quit");
	}
	return NAME_None;
}

bool CodexFrontendRules::ParseEntryId(FName Id, ECodexMainMenuEntry& OutEntry)
{
	for (const ECodexMainMenuEntry Entry : { ECodexMainMenuEntry::Continue, ECodexMainMenuEntry::NewGame, ECodexMainMenuEntry::LoadGame,
		ECodexMainMenuEntry::Options, ECodexMainMenuEntry::Credits, ECodexMainMenuEntry::Quit })
	{
		if (GetEntryId(Entry) == Id)
		{
			OutEntry = Entry;
			return true;
		}
	}
	return false;
}

bool CodexFrontendRules::IsEntryEnabled(ECodexMainMenuEntry Entry, bool bHasSaves)
{
	return Entry != ECodexMainMenuEntry::Continue || bHasSaves;
}

FString CodexFrontendRules::FormatPlayTime(float Seconds)
{
	if (Seconds < 0.f)
	{
		return TEXT("--:--");
	}
	const int32 Total = FMath::FloorToInt32(Seconds);
	const int32 Hours = Total / 3600;
	const int32 Minutes = (Total / 60) % 60;
	const int32 Secs = Total % 60;
	return Hours > 0 ? FString::Printf(TEXT("%d:%02d:%02d"), Hours, Minutes, Secs) : FString::Printf(TEXT("%d:%02d"), Minutes, Secs);
}

float CodexFrontendRules::ResolveBlendTime(float AnchorOverride, float DefaultSeconds)
{
	return FMath::Max(0.f, AnchorOverride >= 0.f ? AnchorOverride : DefaultSeconds);
}

FText CodexFrontendRules::GetSaveButtonLabel(bool bSlotExists)
{
	return bSlotExists ? LOCTEXT("Overwrite", "OVERWRITE") : LOCTEXT("Save", "SAVE");
}

#undef LOCTEXT_NAMESPACE
