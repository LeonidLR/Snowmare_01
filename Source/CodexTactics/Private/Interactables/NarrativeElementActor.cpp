#include "Interactables/NarrativeElementActor.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/NarrativeManifest.h"
#include "Engine/World.h"
#include "UI/OverheadLabel.h"

#define LOCTEXT_NAMESPACE "NarrativeElementActor"

namespace
{
	/** Word wrap for the in-world text (Godot Label3D width 650 px at 0.006 m / px). */
	FString WrapWords(const FString& Text, int32 Width)
	{
		TArray<FString> Paragraphs;
		Text.ParseIntoArrayLines(Paragraphs, false);
		FString Out;
		for (const FString& Paragraph : Paragraphs)
		{
			TArray<FString> Words;
			Paragraph.ParseIntoArrayWS(Words);
			FString Line;
			for (const FString& Word : Words)
			{
				if (!Line.IsEmpty() && Line.Len() + 1 + Word.Len() > Width)
				{
					Out += Line + TEXT("\n");
					Line.Reset();
				}
				Line += Line.IsEmpty() ? Word : TEXT(" ") + Word;
			}
			Out += Line + TEXT("\n");
		}
		return Out.TrimEnd();
	}
}

ANarrativeElementActor::ANarrativeElementActor()
{
	DisplayName = LOCTEXT("Name", "Document");
	InteractionDistance = 150.f;
	// Godot Area3D with a sphere: a small clickable volume, no mesh.
	Box->SetBoxExtent(FVector(40.f, 40.f, 40.f));
	Mesh->SetVisibility(false);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ANarrativeElementActor::GetEnglishTexts(FString& OutTitle, FString& OutContent, FString& OutSource) const
{
	// Level-authored texts that are not English are looked up in the narrative manifest by actor name:
	// sequence "<ActorName>", line 1 = speaker_en (title) + text_en (content), line 2 (optional) = speaker_en (author / source).
	const FNarrativeManifest& Manifest = FNarrativeManifest::Get();
	const TArray<FNarrativeLine>* Lines = Manifest.Sequences.Find(GetName());
	const FNarrativeLine* First = Lines && Lines->Num() > 0 ? &(*Lines)[0] : nullptr;
	const FNarrativeLine* Second = Lines && Lines->Num() > 1 ? &(*Lines)[1] : nullptr;
	OutTitle = FNarrativeManifest::EnglishOr(Title, First ? FNarrativeManifest::EnglishOr(First->Speaker, TEXT("Document")) : FString(TEXT("Document")));
	if (FNarrativeManifest::ContainsCyrillic(ContentText) || ContentText.IsEmpty())
	{
		OutContent = First && !First->Text.IsEmpty() ? First->Text : FNarrativeManifest::MakeMissingPlaceholder(GetName(), 1);
	}
	else
	{
		OutContent = ContentText;
	}
	OutSource = FNarrativeManifest::EnglishOr(AuthorOrSource, Second ? FNarrativeManifest::EnglishOr(Second->Speaker, FString()) : FString());
}

FString ANarrativeElementActor::GetTypeIcon() const
{
	switch (NarrativeType)
	{
	case ENarrativeType::Note: return TEXT("📜");
	case ENarrativeType::Tablet: return TEXT("📱");
	case ENarrativeType::Book: return TEXT("📖");
	case ENarrativeType::Newspaper: return TEXT("📰");
	case ENarrativeType::Signpost: return TEXT("🪧");
	case ENarrativeType::Poster: return TEXT("📢");
	case ENarrativeType::Graffiti: return TEXT("🎨");
	default: return TEXT("📄");
	}
}

bool ANarrativeElementActor::IsReadableNow() const
{
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	const AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	return Leader && FVector::Dist(Leader->GetActorLocation(), GetActorLocation()) <= ReadableDistance;
}

FActionMenuRequest ANarrativeElementActor::BuildActionMenu(const AOperativeCharacter* Leader) const
{
	FString EnTitle, EnContent, EnSource;
	GetEnglishTexts(EnTitle, EnContent, EnSource);
	const FString Source = EnSource.IsEmpty() ? FString(TEXT("Northern Line Checkpoint")) : EnSource;
	return FActionMenuRequest::MakeMenu(FText::FromString(TEXT("📜 ") + EnTitle),
		FText::FromString(FString::Printf(TEXT("\"%s\"\n\n- %s"), *EnContent, *Source)),
		LOCTEXT("Read", "Read Aloud"), LOCTEXT("Close", "Close"), false);
}

void ANarrativeElementActor::PerformAction(AOperativeCharacter* User)
{
	// Godot narrative_element.gd interact: the text goes to the feed under "Title (icon)".
	bHasBeenRead = true;
	FString EnTitle, EnContent, EnSource;
	GetEnglishTexts(EnTitle, EnContent, EnSource);
	PostLine(FText::FromString(FString::Printf(TEXT("%s (%s)"), *EnTitle, *GetTypeIcon())), FText::FromString(EnContent));
	UE_LOG(LogCodexTactics, Log, TEXT("%s read %s"), User ? *User->DisplayName.ToString() : TEXT("Squad"), *EnTitle);
}

bool ANarrativeElementActor::GetOverheadLabel(FOverheadLabel& OutLabel) const
{
	// The Godot marker is the type emoji (dim from afar, bright yellow when readable); the HUD font has no emoji, so
	// the marker is a small square. Within the readable distance the full text shows under it.
	const bool bReadable = IsReadableNow();
	OutLabel.bHasMarker = true;
	OutLabel.MarkerColor = bReadable ? FLinearColor(1.f, 1.f, 0.4f) : FLinearColor(0.9f, 0.85f, 0.5f, 0.75f);
	OutLabel.Color = FLinearColor(1.f, 0.95f, 0.8f);
	OutLabel.HeightCm = TextOffset + 35.f;
	if (bReadable && bInWorldText)
	{
		FString EnTitle, EnContent, EnSource;
		GetEnglishTexts(EnTitle, EnContent, EnSource);
		FString Text = FString::Printf(TEXT("%s\n%s"), *EnTitle.ToUpper(), *WrapWords(EnContent, 48));
		if (!EnSource.IsEmpty())
		{
			Text += TEXT("\n- ") + EnSource;
		}
		OutLabel.Text = Text;
	}
	else
	{
		OutLabel.Text = TEXT(" ");
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
