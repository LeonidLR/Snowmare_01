#include "UI/GameMessageSubsystem.h"
#include "CodexTactics.h"
#include "Engine/World.h"

void UGameMessageSubsystem::PostMessage(const FText& Speaker, const FText& Text)
{
	FGameMessage& Message = History.AddDefaulted_GetRef();
	Message.Speaker = Speaker;
	Message.Text = Text;
	Message.PostedAt = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;
	if (History.Num() > MaxHistory)
	{
		History.RemoveAt(0);
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[%s] %s"), *Speaker.ToString(), *Text.ToString());
	OnMessagePosted.Broadcast(History.Last());
}
