#include "Tutorial/TutorialRules.h"
#include "Core/PlayerCommands.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

const FTutorialStep* FTutorialStepsData::FindStep(FName StepId) const
{
	return Steps.FindByPredicate([StepId](const FTutorialStep& Step) { return Step.Id == StepId; });
}

int32 FTutorialStepsData::FindStepIndex(FName StepId) const
{
	return Steps.IndexOfByPredicate([StepId](const FTutorialStep& Step) { return Step.Id == StepId; });
}

int32 FTutorialStepsData::FindBeatIndex(FName BeatId) const
{
	return BeatId.IsNone() ? INDEX_NONE : Beats.IndexOfByPredicate([BeatId](const FTutorialBeat& Beat) { return Beat.Id == BeatId; });
}

namespace TutorialRulesLocal
{
	FString JsonString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, const FString& Default = FString())
	{
		FString Value;
		return Object.IsValid() && Object->TryGetStringField(Field, Value) ? Value : Default;
	}

	double JsonNumber(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, double Default)
	{
		double Value = Default;
		return Object.IsValid() && Object->TryGetNumberField(Field, Value) ? Value : Default;
	}

	bool JsonBool(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, bool bDefault)
	{
		bool bValue = bDefault;
		return Object.IsValid() && Object->TryGetBoolField(Field, bValue) ? bValue : bDefault;
	}

	/** A string or an array of strings. */
	TArray<FString> JsonStrings(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		TArray<FString> Out;
		if (!Object.IsValid())
		{
			return Out;
		}
		FString Single;
		if (Object->TryGetStringField(Field, Single))
		{
			if (!Single.IsEmpty())
			{
				Out.Add(Single);
			}
			return Out;
		}
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Object->TryGetArrayField(Field, Values))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				FString Text;
				if (Value.IsValid() && Value->TryGetString(Text) && !Text.IsEmpty())
				{
					Out.Add(Text);
				}
			}
		}
		return Out;
	}

	void WarnUnknownKeys(const TSharedPtr<FJsonObject>& Object, std::initializer_list<const TCHAR*> Known, const FString& Where, TArray<FString>& OutWarnings)
	{
		if (!Object.IsValid())
		{
			return;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Object->Values)
		{
			if (Field.Key.StartsWith(TEXT("_")))
			{
				continue; // "_comment" and friends
			}
			bool bKnown = false;
			for (const TCHAR* Name : Known)
			{
				bKnown |= Field.Key.Equals(Name, ESearchCase::IgnoreCase);
			}
			if (!bKnown)
			{
				OutWarnings.Add(FString::Printf(TEXT("%s: unknown key \"%s\""), *Where, *Field.Key));
			}
		}
	}
}

bool TutorialRules::ParseKind(const FString& Text, ETutorialStepKind& OutKind)
{
	const FString Lower = Text.TrimStartAndEnd().ToLower();
	if (Lower == TEXT("hint_card") || Lower == TEXT("card") || Lower == TEXT("hint"))
	{
		OutKind = ETutorialStepKind::HintCard;
	}
	else if (Lower == TEXT("objective"))
	{
		OutKind = ETutorialStepKind::Objective;
	}
	else if (Lower == TEXT("radio"))
	{
		OutKind = ETutorialStepKind::Radio;
	}
	else if (Lower == TEXT("walk_talk") || Lower == TEXT("walk_and_talk"))
	{
		OutKind = ETutorialStepKind::WalkTalk;
	}
	else
	{
		return false;
	}
	return true;
}

bool TutorialRules::ParseTriggerType(const FString& Text, ETutorialTriggerType& OutType)
{
	static const TMap<FString, ETutorialTriggerType> Types = {
		{ TEXT("manual"), ETutorialTriggerType::Manual }, { TEXT("beat_start"), ETutorialTriggerType::BeatStart },
		{ TEXT("volume"), ETutorialTriggerType::Volume }, { TEXT("event"), ETutorialTriggerType::Event },
		{ TEXT("after_step"), ETutorialTriggerType::AfterStep }, { TEXT("first_use"), ETutorialTriggerType::FirstUse },
		{ TEXT("failure_retry"), ETutorialTriggerType::FailureRetry } };
	if (const ETutorialTriggerType* Found = Types.Find(Text.TrimStartAndEnd().ToLower()))
	{
		OutType = *Found;
		return true;
	}
	return false;
}

const TCHAR* TutorialRules::KindName(ETutorialStepKind Kind)
{
	switch (Kind)
	{
	case ETutorialStepKind::HintCard: return TEXT("hint_card");
	case ETutorialStepKind::Objective: return TEXT("objective");
	case ETutorialStepKind::Radio: return TEXT("radio");
	case ETutorialStepKind::WalkTalk: return TEXT("walk_talk");
	default: return TEXT("?");
	}
}

const TCHAR* TutorialRules::StateName(ETutorialStepState State)
{
	switch (State)
	{
	case ETutorialStepState::Idle: return TEXT("idle");
	case ETutorialStepState::Waiting: return TEXT("waiting");
	case ETutorialStepState::Queued: return TEXT("queued");
	case ETutorialStepState::Active: return TEXT("active");
	case ETutorialStepState::Completed: return TEXT("completed");
	case ETutorialStepState::Skipped: return TEXT("skipped");
	case ETutorialStepState::TimedOut: return TEXT("timed_out");
	default: return TEXT("?");
	}
}

bool TutorialRules::ParseFinishedState(const FString& Text, ETutorialStepState& OutState)
{
	for (const ETutorialStepState State : { ETutorialStepState::Completed, ETutorialStepState::Skipped, ETutorialStepState::TimedOut })
	{
		if (Text.Equals(StateName(State), ESearchCase::IgnoreCase))
		{
			OutState = State;
			return true;
		}
	}
	return false;
}

bool TutorialRules::ParseSteps(const FString& Json, FTutorialStepsData& OutData, TArray<FString>& OutWarnings, FString& OutError)
{
	using namespace TutorialRulesLocal;
	OutData = FTutorialStepsData();
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
	{
		OutError = TEXT("tutorial steps: not valid JSON");
		return false;
	}
	WarnUnknownKeys(Root, { TEXT("version"), TEXT("id"), TEXT("start_beat"), TEXT("beats"), TEXT("steps") }, TEXT("root"), OutWarnings);
	OutData.Version = static_cast<int32>(JsonNumber(Root, TEXT("version"), 1.0));
	OutData.Id = JsonString(Root, TEXT("id"));
	OutData.StartBeat = FName(*JsonString(Root, TEXT("start_beat")));

	const TArray<TSharedPtr<FJsonValue>>* Beats = nullptr;
	if (Root->TryGetArrayField(TEXT("beats"), Beats))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Beats)
		{
			const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
			const FString Id = JsonString(Object, TEXT("id"));
			if (Id.IsEmpty())
			{
				OutError = TEXT("tutorial steps: a beat without \"id\"");
				return false;
			}
			if (OutData.FindBeatIndex(FName(*Id)) != INDEX_NONE)
			{
				OutError = FString::Printf(TEXT("tutorial steps: duplicate beat id \"%s\""), *Id);
				return false;
			}
			WarnUnknownKeys(Object, { TEXT("id"), TEXT("lesson"), TEXT("checkpoint"), TEXT("title_id") }, TEXT("beat ") + Id, OutWarnings);
			FTutorialBeat& Beat = OutData.Beats.AddDefaulted_GetRef();
			Beat.Id = FName(*Id);
			Beat.bLesson = JsonBool(Object, TEXT("lesson"), false);
			Beat.bCheckpoint = JsonBool(Object, TEXT("checkpoint"), true);
			Beat.TitleId = FName(*JsonString(Object, TEXT("title_id")));
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* Steps = nullptr;
	if (!Root->TryGetArrayField(TEXT("steps"), Steps))
	{
		OutError = TEXT("tutorial steps: no \"steps\" array");
		return false;
	}
	for (const TSharedPtr<FJsonValue>& Value : *Steps)
	{
		const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
		const FString Id = JsonString(Object, TEXT("id"));
		if (Id.IsEmpty())
		{
			OutError = TEXT("tutorial steps: a step without \"id\"");
			return false;
		}
		if (OutData.FindStepIndex(FName(*Id)) != INDEX_NONE)
		{
			OutError = FString::Printf(TEXT("tutorial steps: duplicate step id \"%s\""), *Id);
			return false;
		}
		const FString Where = TEXT("step ") + Id;
		WarnUnknownKeys(Object, { TEXT("id"), TEXT("beat"), TEXT("kind"), TEXT("text_id"), TEXT("keys"), TEXT("trigger"), TEXT("complete"),
			TEXT("once"), TEXT("skippable"), TEXT("blocking"), TEXT("lines") }, Where, OutWarnings);
		FTutorialStep Step;
		Step.Id = FName(*Id);
		Step.Beat = FName(*JsonString(Object, TEXT("beat")));
		if (!ParseKind(JsonString(Object, TEXT("kind"), TEXT("hint_card")), Step.Kind))
		{
			OutError = FString::Printf(TEXT("%s: unknown kind \"%s\" (hint_card | objective | radio | walk_talk)"), *Where, *JsonString(Object, TEXT("kind")));
			return false;
		}
		Step.TextId = FName(*JsonString(Object, TEXT("text_id")));
		Step.Keys = JsonStrings(Object, TEXT("keys"));
		Step.bOnce = JsonBool(Object, TEXT("once"), true);
		Step.bSkippable = JsonBool(Object, TEXT("skippable"), true);
		if (JsonBool(Object, TEXT("blocking"), false))
		{
			OutWarnings.Add(Where + TEXT(": \"blocking\": true is not supported (the tutorial never blocks the game) - forced to false"));
		}
		Step.bBlocking = false;

		const TSharedPtr<FJsonObject>* TriggerObject = nullptr;
		if (Object->TryGetObjectField(TEXT("trigger"), TriggerObject))
		{
			const TSharedPtr<FJsonObject> Trigger = *TriggerObject;
			WarnUnknownKeys(Trigger, { TEXT("type"), TEXT("volume"), TEXT("event"), TEXT("step"), TEXT("command"), TEXT("count"), TEXT("delay_s") },
				Where + TEXT(" trigger"), OutWarnings);
			if (!ParseTriggerType(JsonString(Trigger, TEXT("type"), TEXT("manual")), Step.Trigger.Type))
			{
				OutError = FString::Printf(TEXT("%s: unknown trigger type \"%s\""), *Where, *JsonString(Trigger, TEXT("type")));
				return false;
			}
			Step.Trigger.Volume = FName(*JsonString(Trigger, TEXT("volume")));
			Step.Trigger.Event = JsonString(Trigger, TEXT("event"));
			Step.Trigger.Step = FName(*JsonString(Trigger, TEXT("step")));
			Step.Trigger.Command = JsonString(Trigger, TEXT("command"));
			Step.Trigger.Count = FMath::Max(1, static_cast<int32>(JsonNumber(Trigger, TEXT("count"), 1.0)));
			Step.Trigger.DelaySeconds = FMath::Max(0.f, static_cast<float>(JsonNumber(Trigger, TEXT("delay_s"), 0.0)));
		}
		else if (!Step.Beat.IsNone())
		{
			Step.Trigger.Type = ETutorialTriggerType::BeatStart; // no trigger: shown when its beat starts (cards queue in file order)
		}

		const TSharedPtr<FJsonObject>* CompleteObject = nullptr;
		if (Object->TryGetObjectField(TEXT("complete"), CompleteObject))
		{
			const TSharedPtr<FJsonObject> Complete = *CompleteObject;
			WarnUnknownKeys(Complete, { TEXT("command"), TEXT("commands"), TEXT("event"), TEXT("count"), TEXT("volume"), TEXT("timeout_s"),
				TEXT("distinct_operatives") },
				Where + TEXT(" complete"), OutWarnings);
			Step.Complete.Commands = JsonStrings(Complete, TEXT("commands"));
			Step.Complete.Commands.Append(JsonStrings(Complete, TEXT("command")));
			Step.Complete.Event = JsonString(Complete, TEXT("event"));
			Step.Complete.Count = FMath::Max(1, static_cast<int32>(JsonNumber(Complete, TEXT("count"), 1.0)));
			Step.Complete.Volume = FName(*JsonString(Complete, TEXT("volume")));
			Step.Complete.TimeoutSeconds = FMath::Max(0.f, static_cast<float>(JsonNumber(Complete, TEXT("timeout_s"), 0.0)));
			Step.Complete.bDistinctSubjects = JsonBool(Complete, TEXT("distinct_operatives"), false);
			if (Step.Complete.bDistinctSubjects && Step.Complete.Event.IsEmpty())
			{
				OutWarnings.Add(Where + TEXT(": \"distinct_operatives\" only counts events - no \"event\" given"));
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* Lines = nullptr;
		if (Object->TryGetArrayField(TEXT("lines"), Lines))
		{
			for (const TSharedPtr<FJsonValue>& LineValue : *Lines)
			{
				const TSharedPtr<FJsonObject> LineObject = LineValue.IsValid() ? LineValue->AsObject() : nullptr;
				FTutorialLine Line;
				Line.Speaker = JsonString(LineObject, TEXT("speaker"));
				Line.TextId = FName(*JsonString(LineObject, TEXT("text_id")));
				Line.DurationSeconds = static_cast<float>(JsonNumber(LineObject, TEXT("duration_s"), 0.0));
				if (Line.TextId.IsNone())
				{
					OutWarnings.Add(Where + TEXT(": a line without text_id is skipped"));
					continue;
				}
				Step.Lines.Add(MoveTemp(Line));
			}
		}
		if (Step.TextId.IsNone() && Step.Lines.IsEmpty())
		{
			OutWarnings.Add(Where + TEXT(": no text_id / lines - the card shows only its keys"));
		}
		if (Step.Kind == ETutorialStepKind::HintCard && !Step.Complete.HasCondition() && Step.Complete.TimeoutSeconds <= 0.f)
		{
			Step.Complete.TimeoutSeconds = 12.f;
			OutWarnings.Add(Where + TEXT(": a hint card with no completion gets timeout_s 12"));
		}
		if (!Step.Beat.IsNone() && OutData.FindBeatIndex(Step.Beat) == INDEX_NONE)
		{
			OutWarnings.Add(FString::Printf(TEXT("%s: beat \"%s\" is not declared in \"beats\" - added (not a lesson, checkpoint on)"), *Where, *Step.Beat.ToString()));
			FTutorialBeat& Beat = OutData.Beats.AddDefaulted_GetRef();
			Beat.Id = Step.Beat;
		}
		OutData.Steps.Add(MoveTemp(Step));
	}

	for (const FTutorialStep& Step : OutData.Steps)
	{
		if (Step.Trigger.Type == ETutorialTriggerType::AfterStep && OutData.FindStepIndex(Step.Trigger.Step) == INDEX_NONE)
		{
			OutError = FString::Printf(TEXT("step %s: after_step names an unknown step \"%s\""), *Step.Id.ToString(), *Step.Trigger.Step.ToString());
			return false;
		}
		if ((Step.Trigger.Type == ETutorialTriggerType::Event || Step.Trigger.Type == ETutorialTriggerType::FailureRetry) && Step.Trigger.Event.IsEmpty())
		{
			OutError = FString::Printf(TEXT("step %s: trigger \"%s\" needs \"event\""), *Step.Id.ToString(),
				Step.Trigger.Type == ETutorialTriggerType::Event ? TEXT("event") : TEXT("failure_retry"));
			return false;
		}
		if (Step.Trigger.Type == ETutorialTriggerType::FirstUse && Step.Trigger.Command.IsEmpty())
		{
			OutError = FString::Printf(TEXT("step %s: trigger \"first_use\" needs \"command\""), *Step.Id.ToString());
			return false;
		}
		if (Step.Trigger.Type == ETutorialTriggerType::BeatStart && Step.Beat.IsNone())
		{
			OutError = FString::Printf(TEXT("step %s: trigger \"beat_start\" needs \"beat\""), *Step.Id.ToString());
			return false;
		}
		for (const FString& Command : Step.Complete.Commands)
		{
			if (!Command.EndsWith(TEXT("*")) && !PlayerCommands::Find(FName(*Command)))
			{
				OutWarnings.Add(FString::Printf(TEXT("step %s: command \"%s\" is not in the registry (Core/PlayerCommands.h)"), *Step.Id.ToString(), *Command));
			}
		}
	}
	if (!OutData.StartBeat.IsNone() && OutData.FindBeatIndex(OutData.StartBeat) == INDEX_NONE)
	{
		OutWarnings.Add(FString::Printf(TEXT("start_beat \"%s\" is not a beat - ignored"), *OutData.StartBeat.ToString()));
		OutData.StartBeat = NAME_None;
	}
	return true;
}

FString TutorialRules::GetTutorialDirectory()
{
	return FPaths::ProjectContentDir() / TEXT("Data/Tutorial");
}

bool TutorialRules::LoadStepsFile(const FString& FileName, FTutorialStepsData& OutData, TArray<FString>& OutWarnings, FString& OutError)
{
	FString Path = FileName;
	if (FPaths::IsRelative(Path))
	{
		Path = GetTutorialDirectory() / FileName;
	}
	if (FPaths::GetExtension(Path).IsEmpty())
	{
		Path += TEXT(".json");
	}
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		OutError = FString::Printf(TEXT("tutorial steps file not found: %s"), *Path);
		return false;
	}
	return ParseSteps(Json, OutData, OutWarnings, OutError);
}

bool TutorialRules::IsFinished(ETutorialStepState State)
{
	return State == ETutorialStepState::Completed || State == ETutorialStepState::Skipped || State == ETutorialStepState::TimedOut;
}

bool TutorialRules::IsRunning(ETutorialStepState State)
{
	return State == ETutorialStepState::Waiting || State == ETutorialStepState::Queued || State == ETutorialStepState::Active;
}

bool TutorialRules::MatchesName(const FString& Pattern, FName Name)
{
	return PlayerCommands::Matches(Pattern, Name);
}

bool TutorialRules::CountsCommand(const FTutorialStep& Step, FName CommandId)
{
	for (const FString& Pattern : Step.Complete.Commands)
	{
		if (PlayerCommands::Matches(Pattern, CommandId))
		{
			return true;
		}
	}
	return false;
}

float TutorialRules::LineDurationSeconds(const FString& Text, float Override)
{
	if (Override > 0.f)
	{
		return Override;
	}
	return FMath::Clamp(1.2f + 0.055f * Text.Len(), 2.5f, 9.f);
}

FString TutorialRules::MakeMissingText(FName TextId)
{
	return FString::Printf(TEXT("[EN missing: %s]"), *TextId.ToString());
}

bool TutorialRules::ShouldDownInsteadOfKill(bool bTutorialEnabled, bool bLessonBeatActive)
{
	return bTutorialEnabled && bLessonBeatActive;
}

TSharedRef<FJsonObject> TutorialRules::SaveProgress(const FTutorialProgress& Progress, bool bEnabled)
{
	const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("beat"), Progress.CurrentBeat.IsNone() ? FString() : Progress.CurrentBeat.ToString());
	Object->SetBoolField(TEXT("enabled"), bEnabled);
	const TSharedRef<FJsonObject> Finished = MakeShared<FJsonObject>();
	TArray<FName> Ids;
	Progress.Finished.GetKeys(Ids);
	Ids.Sort(FNameLexicalLess());
	for (const FName Id : Ids)
	{
		Finished->SetStringField(Id.ToString(), StateName(Progress.Finished[Id]));
	}
	Object->SetObjectField(TEXT("finished"), Finished);
	TArray<TSharedPtr<FJsonValue>> Pending;
	for (const FName Id : Progress.Pending)
	{
		Pending.Add(MakeShared<FJsonValueString>(Id.ToString()));
	}
	Object->SetArrayField(TEXT("pending"), Pending);
	return Object;
}

bool TutorialRules::LoadProgress(const FJsonObject& Object, FTutorialProgress& OutProgress)
{
	OutProgress = FTutorialProgress();
	const TSharedPtr<FJsonObject>* Finished = nullptr;
	if (!Object.TryGetObjectField(TEXT("finished"), Finished))
	{
		return false;
	}
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Finished)->Values)
	{
		FString StateText;
		ETutorialStepState State = ETutorialStepState::Completed;
		if (Entry.Value.IsValid() && Entry.Value->TryGetString(StateText) && ParseFinishedState(StateText, State))
		{
			OutProgress.Finished.Add(FName(*Entry.Key), State);
		}
	}
	FString Beat;
	if (Object.TryGetStringField(TEXT("beat"), Beat) && !Beat.IsEmpty())
	{
		OutProgress.CurrentBeat = FName(*Beat);
	}
	const TArray<TSharedPtr<FJsonValue>>* Pending = nullptr;
	if (Object.TryGetArrayField(TEXT("pending"), Pending))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Pending)
		{
			FString Id;
			if (Value.IsValid() && Value->TryGetString(Id) && !Id.IsEmpty())
			{
				OutProgress.Pending.Add(FName(*Id));
			}
		}
	}
	return true;
}

FString TutorialRules::BuildTelemetryLine(const FTutorialTelemetryEntry& Entry)
{
	const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("t"), Entry.Timestamp);
	Object->SetStringField(TEXT("session"), Entry.Session);
	Object->SetStringField(TEXT("map"), Entry.Map);
	Object->SetStringField(TEXT("event"), Entry.Event);
	Object->SetStringField(TEXT("step"), Entry.Step.IsNone() ? FString() : Entry.Step.ToString());
	Object->SetStringField(TEXT("beat"), Entry.Beat.IsNone() ? FString() : Entry.Beat.ToString());
	Object->SetNumberField(TEXT("active_s"), FMath::RoundToDouble(Entry.ActiveSeconds * 100.0) / 100.0);
	Object->SetNumberField(TEXT("game_s"), FMath::RoundToDouble(Entry.GameSeconds * 100.0) / 100.0);
	FString Line;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Line);
	FJsonSerializer::Serialize(Object, Writer);
	return Line;
}

FString TutorialRules::FormatKeys(const TArray<FString>& Keys)
{
	TArray<FString> Caps;
	for (const FString& Key : Keys)
	{
		const FString Cap = PlayerCommands::FormatKeyToken(Key);
		if (!Cap.IsEmpty())
		{
			Caps.Add(Cap);
		}
	}
	return FString::Join(Caps, TEXT("  "));
}
