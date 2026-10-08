#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameMessageSubsystem.generated.h"

/** One line of the in-game message feed (top-right panel: speaker header + text). */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FGameMessage
{
	GENERATED_BODY()

	/** Speaker header, e.g. «Commander», «HQ», «SQUAD», «SURVEILLANCE». */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Messages")
	FText Speaker;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Messages")
	FText Text;

	/** World real time when posted, s. */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Messages")
	double PostedAt = 0.0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGameMessagePosted, const FGameMessage&, Message);

/**
 * Message feed for squad chatter and HQ notices; the HUD message panel listens to OnMessagePosted.
 * Godot reference: main.gd `_on_quest_message(speaker, text)`.
 */
UCLASS()
class CODEXTACTICS_API UGameMessageSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Posts a message and logs it. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Messages")
	void PostMessage(const FText& Speaker, const FText& Text);

	/** Recent messages, oldest first (at most MaxHistory). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Messages")
	const TArray<FGameMessage>& GetHistory() const { return History; }

	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Messages")
	FOnGameMessagePosted OnMessagePosted;

	/** Number of messages kept in history. */
	static constexpr int32 MaxHistory = 50;

private:
	TArray<FGameMessage> History;
};
