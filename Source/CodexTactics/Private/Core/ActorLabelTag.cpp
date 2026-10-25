#include "Core/ActorLabelTag.h"

#include "GameFramework/Actor.h"

FName ActorLabelTag::MakeTag(const FString& Label)
{
	return Label.IsEmpty() ? NAME_None : FName(*(FString(Prefix) + Label));
}

FString ActorLabelTag::FindInTags(const TArray<FName>& Tags)
{
	for (const FName& Tag : Tags)
	{
		const FString Text = Tag.ToString();
		if (Text.StartsWith(Prefix, ESearchCase::CaseSensitive))
		{
			return Text.RightChop(FCString::Strlen(Prefix));
		}
	}
	return FString();
}

FString ActorLabelTag::GetLabel(const AActor& Actor)
{
#if WITH_EDITOR
	return Actor.GetActorLabel(false);
#else
	return FindInTags(Actor.Tags);
#endif
}
