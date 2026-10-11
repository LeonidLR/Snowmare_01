#include "UI/Frontend/CodexControlsRules.h"
#include "Core/PlayerCommands.h"

TArray<FCodexControlsSection> CodexControlsRules::BuildSections()
{
	TArray<FCodexControlsSection> Result;
	for (const TCHAR* SectionTitle : PlayerCommands::GetSections())
	{
		FCodexControlsSection Section;
		Section.Title = SectionTitle;
		for (const FPlayerCommandInfo& Info : PlayerCommands::GetRegistry())
		{
			if (Section.Title.Equals(Info.Section))
			{
				Section.Rows.Add({ Info.Id, PlayerCommands::GetKeyLabel(Info.Id), Info.Description });
			}
		}
		if (!Section.Rows.IsEmpty())
		{
			Result.Add(MoveTemp(Section));
		}
	}
	return Result;
}

int32 CodexControlsRules::CountRows(const TArray<FCodexControlsSection>& Sections)
{
	int32 Count = 0;
	for (const FCodexControlsSection& Section : Sections)
	{
		Count += Section.Rows.Num();
	}
	return Count;
}

TArray<FString> CodexControlsRules::ValidateRegistry()
{
	TArray<FString> Problems;
	TSet<FName> Seen;
	TArray<FString> Known;
	for (const TCHAR* Title : PlayerCommands::GetSections())
	{
		Known.Add(Title);
	}
	for (const FPlayerCommandInfo& Info : PlayerCommands::GetRegistry())
	{
		const FString Id = Info.Id.ToString();
		if (Seen.Contains(Info.Id))
		{
			Problems.Add(FString::Printf(TEXT("%s: duplicate id"), *Id));
		}
		Seen.Add(Info.Id);
		if (!Known.Contains(FString(Info.Section)))
		{
			Problems.Add(FString::Printf(TEXT("%s: unknown section \"%s\""), *Id, Info.Section));
		}
		if (FCString::Strlen(Info.KeyLabel) == 0)
		{
			Problems.Add(FString::Printf(TEXT("%s: empty key label"), *Id));
		}
		if (FCString::Strlen(Info.Description) == 0)
		{
			Problems.Add(FString::Printf(TEXT("%s: empty description"), *Id));
		}
	}
	return Problems;
}
