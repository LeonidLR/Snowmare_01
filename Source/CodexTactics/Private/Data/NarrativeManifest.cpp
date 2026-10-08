#include "Data/NarrativeManifest.h"
#include "CodexTactics.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	FNarrativeManifest GCachedManifest;
	bool bGCacheLoaded = false;
}

bool FNarrativeManifest::Parse(const FString& Json, FNarrativeManifest& Out, FString& OutError)
{
	Out = FNarrativeManifest();
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
	{
		OutError = TEXT("narrative manifest: not valid JSON");
		return false;
	}
	const TSharedPtr<FJsonObject>* SequencesObj = nullptr;
	if (!Root->TryGetObjectField(TEXT("sequences"), SequencesObj))
	{
		OutError = TEXT("narrative manifest: no \"sequences\" object");
		return false;
	}
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Seq : (*SequencesObj)->Values)
	{
		const TSharedPtr<FJsonObject>* SeqObj = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* LinesArr = nullptr;
		if (!Seq.Value.IsValid() || !Seq.Value->TryGetObject(SeqObj) || !(*SeqObj)->TryGetArrayField(TEXT("lines"), LinesArr))
		{
			continue;
		}
		TArray<FNarrativeLine>& Lines = Out.Sequences.Add(Seq.Key);
		for (const TSharedPtr<FJsonValue>& LineValue : *LinesArr)
		{
			const TSharedPtr<FJsonObject>* LineObj = nullptr;
			FNarrativeLine Line;
			if (LineValue.IsValid() && LineValue->TryGetObject(LineObj))
			{
				(*LineObj)->TryGetStringField(TEXT("speaker_en"), Line.Speaker);
				(*LineObj)->TryGetStringField(TEXT("text_en"), Line.Text);
				double Delay = 0.0;
				if ((*LineObj)->TryGetNumberField(TEXT("delay"), Delay) && Delay > 0.0)
				{
					Line.DelayAfter = static_cast<float>(Delay);
				}
			}
			Lines.Add(MoveTemp(Line));
		}
	}
	const TSharedPtr<FJsonObject>* GlossaryObj = nullptr;
	if (Root->TryGetObjectField(TEXT("glossary"), GlossaryObj))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*GlossaryObj)->Values)
		{
			const TSharedPtr<FJsonObject>* EntryObj = nullptr;
			FString Name;
			if (Entry.Value.IsValid() && Entry.Value->TryGetObject(EntryObj) && (*EntryObj)->TryGetStringField(TEXT("name_en"), Name))
			{
				Out.GlossaryNames.Add(Entry.Key, Name);
			}
		}
	}
	return true;
}

const FNarrativeManifest& FNarrativeManifest::Get()
{
	if (!bGCacheLoaded)
	{
		bGCacheLoaded = true;
		const FString Path = FPaths::ProjectContentDir() / TEXT("Data/Narrative/narrative_manifest.json");
		FString Json;
		FString Error;
		if (!FFileHelper::LoadFileToString(Json, *Path))
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("Narrative manifest not found: %s (run Scripts/Narrative/sync_narrative.py)"), *Path);
		}
		else if (!Parse(Json, GCachedManifest, Error))
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("%s (%s)"), *Error, *Path);
		}
		else
		{
			UE_LOG(LogCodexTactics, Log, TEXT("Narrative manifest loaded: %d sequences, %d glossary names"), GCachedManifest.Sequences.Num(), GCachedManifest.GlossaryNames.Num());
		}
	}
	return GCachedManifest;
}

void FNarrativeManifest::Reset()
{
	GCachedManifest = FNarrativeManifest();
	bGCacheLoaded = false;
}

bool FNarrativeManifest::ContainsCyrillic(const FString& Text)
{
	for (const TCHAR Char : Text)
	{
		if (Char >= 0x0400 && Char <= 0x052F)
		{
			return true;
		}
	}
	return false;
}

FString FNarrativeManifest::EnglishOr(const FString& Text, const FString& Fallback)
{
	return (Text.IsEmpty() || ContainsCyrillic(Text)) ? Fallback : Text;
}

FString FNarrativeManifest::MakeMissingPlaceholder(const FString& SequenceId, int32 LineNumber)
{
	return FString::Printf(TEXT("[EN missing: %s#%d]"), *SequenceId, LineNumber);
}

FString FNarrativeManifest::GetGlossaryName(const FString& Key, const FString& Fallback) const
{
	const FString* Name = GlossaryNames.Find(Key);
	return Name ? *Name : Fallback;
}

TArray<FDialogueLine> FNarrativeManifest::BuildLines(const FString& SequenceId, const TArray<FDialogueLine>& AssetLines, TArray<FString>& OutWarnings) const
{
	TArray<FDialogueLine> Result;
	if (const TArray<FNarrativeLine>* Found = Sequences.Find(SequenceId))
	{
		for (int32 Index = 0; Index < Found->Num(); ++Index)
		{
			const FNarrativeLine& Source = (*Found)[Index];
			FDialogueLine Line;
			Line.DelayAfter = Source.DelayAfter;
			Line.SpeakerName = Source.Speaker.IsEmpty() ? TEXT("Unknown") : Source.Speaker;
			if (Source.Text.IsEmpty())
			{
				Line.Text = MakeMissingPlaceholder(SequenceId, Index + 1);
				OutWarnings.Add(FString::Printf(TEXT("%s line %d has no text_en"), *SequenceId, Index + 1));
			}
			else
			{
				Line.Text = Source.Text;
			}
			Result.Add(MoveTemp(Line));
		}
		if (Result.IsEmpty())
		{
			FDialogueLine Line;
			Line.Text = MakeMissingPlaceholder(SequenceId, 1);
			Result.Add(Line);
			OutWarnings.Add(FString::Printf(TEXT("%s has no lines in the manifest"), *SequenceId));
		}
		return Result;
	}

	OutWarnings.Add(FString::Printf(TEXT("sequence %s is missing from the narrative manifest"), *SequenceId));
	for (int32 Index = 0; Index < AssetLines.Num(); ++Index)
	{
		FDialogueLine Line = AssetLines[Index];
		if (ContainsCyrillic(Line.Text) || Line.Text.IsEmpty())
		{
			Line.Text = MakeMissingPlaceholder(SequenceId, Index + 1);
		}
		Line.SpeakerName = EnglishOr(Line.SpeakerName, TEXT("Unknown"));
		Result.Add(MoveTemp(Line));
	}
	if (Result.IsEmpty())
	{
		FDialogueLine Line;
		Line.Text = MakeMissingPlaceholder(SequenceId, 1);
		Result.Add(Line);
	}
	return Result;
}
