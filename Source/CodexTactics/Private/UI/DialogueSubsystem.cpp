#include "UI/DialogueSubsystem.h"
#include "Subsystems/CodexEventBus.h"
#include "CodexTactics.h"
#include "Data/DialogueSequenceAsset.h"
#include "Data/NarrativeManifest.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "UI/GameMessageSubsystem.h"
#include "UObject/Package.h"

const UDialogueSequenceAsset* UDialogueSubsystem::ResolveSequence(const UDialogueSequenceAsset* Sequence)
{
	// Runtime-built sequences (recruitment etc.) live in the transient package and carry their own English text.
	if (!Sequence || Sequence->GetPackage() == GetTransientPackage())
	{
		return Sequence;
	}
	const FString SequenceId = Sequence->GetName();
	TArray<FString> Warnings;
	UDialogueSequenceAsset* Resolved = NewObject<UDialogueSequenceAsset>(this);
	Resolved->Lines = FNarrativeManifest::Get().BuildLines(SequenceId, Sequence->Lines, Warnings);
	Resolved->Title = FNarrativeManifest::EnglishOr(Sequence->Title, SequenceId);
	Resolved->CustomFinishButtonText = FNarrativeManifest::EnglishOr(Sequence->CustomFinishButtonText, FString());
	Resolved->bIsRecruitmentDialogue = Sequence->bIsRecruitmentDialogue;
	for (const FString& Warning : Warnings)
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("Narrative: %s"), *Warning);
	}
	return Resolved;
}

void UDialogueSubsystem::StartDialogue(const UDialogueSequenceAsset* Sequence, FSimpleDelegate OnFinished)
{
	Sequence = ResolveSequence(Sequence);
	if (!Sequence || Sequence->Lines.IsEmpty())
	{
		OnFinished.ExecuteIfBound();
		return;
	}
	Current = Sequence;
	LineIndex = 0;
	OnCurrentFinished = OnFinished;
	UE_LOG(LogCodexTactics, Log, TEXT("Dialogue started: %s (%d lines)"), *Sequence->Title, Sequence->Lines.Num());
	OnDialogueChanged.Broadcast(true);
}

void UDialogueSubsystem::AdvanceLine()
{
	if (!Current)
	{
		return;
	}
	++LineIndex;
	if (LineIndex >= Current->Lines.Num())
	{
		Close();
		return;
	}
	OnDialogueChanged.Broadcast(true);
}

void UDialogueSubsystem::SkipDialogue()
{
	if (Current)
	{
		Close();
	}
}

void UDialogueSubsystem::Close()
{
	Current = nullptr;
	LineIndex = 0;
	OnDialogueChanged.Broadcast(false);
	if (UCodexEventBus* Bus = UCodexEventBus::Get(this))
	{
		Bus->OnDialogueFinished.Broadcast(); // Godot dialogue_box.gd
	}
	FSimpleDelegate Finished = OnCurrentFinished;
	OnCurrentFinished.Unbind();
	Finished.ExecuteIfBound();
}

void UDialogueSubsystem::PlayInFeed(const UDialogueSequenceAsset* Sequence, FSimpleDelegate OnFinished)
{
	++FeedPlayId;
	Sequence = ResolveSequence(Sequence);
	FeedSequence = Sequence;
	OnFeedFinished = OnFinished;
	if (!Sequence || Sequence->Lines.IsEmpty())
	{
		OnFeedFinished.ExecuteIfBound();
		return;
	}
	PostNextFeedLine(FeedPlayId, 0);
}

void UDialogueSubsystem::PostNextFeedLine(int32 PlayId, int32 Index)
{
	if (PlayId != FeedPlayId || !FeedSequence)
	{
		return;
	}
	if (!FeedSequence->Lines.IsValidIndex(Index))
	{
		FSimpleDelegate Finished = OnFeedFinished;
		OnFeedFinished.Unbind();
		Finished.ExecuteIfBound();
		return;
	}
	const FDialogueLine& Line = FeedSequence->Lines[Index];
	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(FText::FromString(Line.SpeakerName.IsEmpty() ? TEXT("Commander") : Line.SpeakerName), FText::FromString(Line.Text));
	}
	FTimerHandle Handle;
	GetWorld()->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateUObject(this, &UDialogueSubsystem::PostNextFeedLine, PlayId, Index + 1),
		Line.DelayAfter > 0.f ? Line.DelayAfter : 4.f, false);
}
