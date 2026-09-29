#include "Interactables/NarrativeElementActor.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
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
	DisplayName = LOCTEXT("Name", "Документ");
	InteractionDistance = 150.f;
	// Godot Area3D with a sphere: a small clickable volume, no mesh.
	Box->SetBoxExtent(FVector(40.f, 40.f, 40.f));
	Mesh->SetVisibility(false);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
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
	const FString Source = AuthorOrSource.IsEmpty() ? FString(TEXT("КПП «Северный Рубеж»")) : AuthorOrSource;
	return FActionMenuRequest::MakeMenu(FText::FromString(TEXT("📜 ") + Title),
		FText::FromString(FString::Printf(TEXT("«%s»\n\n— %s"), *ContentText, *Source)),
		LOCTEXT("Read", "Прочитать вслух"), LOCTEXT("Close", "Закрыть"), false);
}

void ANarrativeElementActor::PerformAction(AOperativeCharacter* User)
{
	// Godot narrative_element.gd interact: the text goes to the feed under «Title (icon)».
	bHasBeenRead = true;
	PostLine(FText::FromString(FString::Printf(TEXT("%s (%s)"), *Title, *GetTypeIcon())), FText::FromString(ContentText));
	UE_LOG(LogCodexTactics, Log, TEXT("%s read %s"), User ? *User->DisplayName.ToString() : TEXT("Squad"), *Title);
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
		// FText::ToUpper handles Cyrillic (FString::ToUpper does not).
		FString Text = FString::Printf(TEXT("%s\n%s"), *FText::FromString(Title).ToUpper().ToString(), *WrapWords(ContentText, 48));
		if (!AuthorOrSource.IsEmpty())
		{
			Text += TEXT("\n— ") + AuthorOrSource;
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
