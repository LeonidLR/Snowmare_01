#include "Data/AITuning.h"

#include "CodexTactics.h"
#include "Dom/JsonObject.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

FString AITuning::GetDefaultPath()
{
	return FPaths::ProjectContentDir() / TEXT("Data/AI/ai_tuning.json");
}

int32 AITuning::ApplyJson(const FString& Json)
{
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
	{
		return 0;
	}
	const TSharedPtr<FJsonObject>* CVars = nullptr;
	if (!Root->TryGetObjectField(TEXT("cvars"), CVars) || !CVars)
	{
		return 0;
	}
	int32 Applied = 0;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*CVars)->Values)
	{
		// Only the game's own tunables: the file never reaches engine / rendering settings.
		IConsoleVariable* CVar = Pair.Key.StartsWith(TEXT("Codex.")) ? IConsoleManager::Get().FindConsoleVariable(*Pair.Key) : nullptr;
		FString Value;
		if (!CVar || !Pair.Value.IsValid() || !Pair.Value->TryGetString(Value))
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("AI tuning: %s skipped"), *Pair.Key);
			continue;
		}
		CVar->Set(*Value, ECVF_SetByGameSetting);
		++Applied;
	}
	return Applied;
}

int32 AITuning::ApplyFile(const FString& Path)
{
	FString Json;
	if (FParse::Param(FCommandLine::Get(), TEXT("NoAITuning")) || !FFileHelper::LoadFileToString(Json, *Path))
	{
		return 0;
	}
	const int32 Applied = ApplyJson(Json);
	UE_LOG(LogCodexTactics, Display, TEXT("AI tuning: %d console variables from %s"), Applied, *Path);
	return Applied;
}
