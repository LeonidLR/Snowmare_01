#include "Tutorial/TutorialRules.h"
#include "Characters/FirePostureRules.h"
#include "Interactables/TrapSpotRules.h"
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

const FTutorialEnemyBinding* FTutorialStepsData::FindEnemy(FName BindingId) const
{
	return BindingId.IsNone() ? nullptr : Enemies.FindByPredicate([BindingId](const FTutorialEnemyBinding& Binding) { return Binding.Id == BindingId; });
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

	/** The non-empty strings of an array field (absent / not an array = none). */
	TArray<FString> JsonStringArray(const FJsonObject& Object, const TCHAR* Field)
	{
		TArray<FString> Out;
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Object.TryGetArrayField(Field, Values))
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
		return JsonStringArray(*Object, Field);
	}

	/** The names of an array field, each once (save "subjects" / "contributors" / "passed"). */
	void JsonUniqueNames(const FJsonObject& Object, const TCHAR* Field, TArray<FName>& Out)
	{
		for (const FString& Text : JsonStringArray(Object, Field))
		{
			Out.AddUnique(FName(*Text));
		}
	}

	/** Names as a JSON string array (save). */
	TArray<TSharedPtr<FJsonValue>> JsonNameValues(const TArray<FName>& Names)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FName Name : Names)
		{
			Values.Add(MakeShared<FJsonValueString>(Name.ToString()));
		}
		return Values;
	}

	/** The plain condition stats (UTutorialSubsystem::GetConditionStat); the prefixed ones are listed by DescribeKnownStats. */
	const TCHAR* const PlainConditionStats[] = { TEXT("squad_cold_max"), TEXT("squad_cold_min"), TEXT("leader_cold"), TEXT("squad_heat_packs"),
		TEXT("squad_min_heat_packs"), TEXT("heat_packs_given"), TEXT("squad_solo"), TEXT("squad_injury_max"), TEXT("squad_prone"),
		TEXT("enemies_unseen"), TEXT("squad_passive"), TEXT("in_fight"), TEXT("quest_progress") };

	/** "squad_cold_max | ... | traps_spotted.<tag> | squad_not_low_in.<volume>" for the parse error of an unknown stat. */
	FString DescribeKnownStats()
	{
		TArray<FString> Names;
		for (const TCHAR* Name : PlainConditionStats)
		{
			Names.Add(Name);
		}
		Names.Add(TEXT("traps_spotted.<tag>"));
		Names.Add(TEXT("squad_not_low_in.<volume>"));
		Names.Add(TEXT("squad_not_across.<volume>"));
		return FString::Join(Names, TEXT(" | "));
	}

	/**
	 * 2026-10-21: "enemy" / "within_m" / "off_route_m" of a trigger / complete / cancel_on object (FTutorialEnemyFilter). "enemy" naming no
	 * binding of the file: OutError and false (checked after every binding is parsed). The distances without "enemy", or a filter without
	 * the "event" it filters: warnings.
	 */
	bool ParseEnemyFilter(const TSharedPtr<FJsonObject>& Object, const FTutorialStepsData& Data, bool bHasEvent, const FString& Where,
		FTutorialEnemyFilter& OutFilter, TArray<FString>& OutWarnings, FString& OutError)
	{
		OutFilter = FTutorialEnemyFilter();
		if (!Object.IsValid())
		{
			return true;
		}
		const FString Enemy = JsonString(Object, TEXT("enemy"));
		OutFilter.Enemy = Enemy.IsEmpty() ? NAME_None : FName(*Enemy);
		OutFilter.WithinMetres = FMath::Max(0.f, static_cast<float>(JsonNumber(Object, TEXT("within_m"), 0.0)));
		OutFilter.OffRouteMetres = FMath::Max(0.f, static_cast<float>(JsonNumber(Object, TEXT("off_route_m"), 0.0)));
		if (!OutFilter.Enemy.IsNone() && !Data.FindEnemy(OutFilter.Enemy))
		{
			OutError = FString::Printf(TEXT("%s: \"enemy\" names an unknown binding \"%s\" (declare it in the root \"enemies\")"), *Where, *Enemy);
			return false;
		}
		if (OutFilter.Enemy.IsNone() && (OutFilter.WithinMetres > 0.f || OutFilter.OffRouteMetres > 0.f))
		{
			OutWarnings.Add(Where + TEXT(": \"within_m\" / \"off_route_m\" only filter an \"enemy\" event - no \"enemy\" given (ignored)"));
			OutFilter.WithinMetres = OutFilter.OffRouteMetres = 0.f;
		}
		if (OutFilter.IsSet() && !bHasEvent)
		{
			OutWarnings.Add(Where + TEXT(": \"enemy\" only filters events - no \"event\" given"));
		}
		return true;
	}

	/**
	 * Every id names a step of Data (and, unless bSelfAllowed, not Step itself); else OutError =
	 * "step <id>: <What> names an unknown step "<other>"" and false.
	 */
	bool CheckStepReferences(const FTutorialStepsData& Data, const FTutorialStep& Step, const TArray<FName>& Ids, const TCHAR* What, bool bSelfAllowed,
		FString& OutError)
	{
		for (const FName Other : Ids)
		{
			if (Data.FindStepIndex(Other) == INDEX_NONE || (!bSelfAllowed && Other == Step.Id))
			{
				OutError = FString::Printf(TEXT("step %s: %s names an unknown step \"%s\""), *Step.Id.ToString(), What, *Other.ToString());
				return false;
			}
		}
		return true;
	}

	/** The uses and the hold time the history holds for the commands a step's completion counts (precredit). */
	FTutorialCommandUse SumHistoryForStep(const FTutorialStep& Step, const TMap<FName, FTutorialCommandUse>& History)
	{
		FTutorialCommandUse Sum;
		for (const TPair<FName, FTutorialCommandUse>& Entry : History)
		{
			if (TutorialRules::CountsCommand(Step, Entry.Key))
			{
				Sum.Count += Entry.Value.Count;
				Sum.HeldSeconds += Entry.Value.HeldSeconds;
			}
		}
		return Sum;
	}

	/** True when some registered player command matches Pattern (exact id, "Prefix.*", "*"). */
	bool MatchesAnyRegisteredCommand(const FString& Pattern)
	{
		for (const FPlayerCommandInfo& Info : PlayerCommands::GetRegistry())
		{
			if (PlayerCommands::Matches(Pattern, Info.Id))
			{
				return true;
			}
		}
		return false;
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

	/** An object or an array of objects ("condition" / "actions"). */
	TArray<TSharedPtr<FJsonObject>> JsonObjects(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		TArray<TSharedPtr<FJsonObject>> Out;
		if (!Object.IsValid())
		{
			return Out;
		}
		const TSharedPtr<FJsonObject>* Single = nullptr;
		if (Object->TryGetObjectField(Field, Single))
		{
			Out.Add(*Single);
			return Out;
		}
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Object->TryGetArrayField(Field, Values))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				if (const TSharedPtr<FJsonObject> Item = Value.IsValid() ? Value->AsObject() : nullptr)
				{
					Out.Add(Item);
				}
			}
		}
		return Out;
	}

	/** Parses "condition" clauses; false + OutError on a bad clause. */
	bool ParseConditions(const TSharedPtr<FJsonObject>& Object, const FString& Where, TArray<FTutorialCondition>& OutConditions,
		TArray<FString>& OutWarnings, FString& OutError, const TCHAR* Key = TEXT("condition"))
	{
		for (const TSharedPtr<FJsonObject>& Clause : JsonObjects(Object, Key))
		{
			WarnUnknownKeys(Clause, { TEXT("stat"), TEXT("at_least"), TEXT("below") }, Where + TEXT(" ") + Key, OutWarnings);
			FTutorialCondition Condition;
			Condition.Stat = FName(*JsonString(Clause, TEXT("stat")));
			if (!TutorialRules::IsKnownStat(Condition.Stat))
			{
				OutError = FString::Printf(TEXT("%s: unknown condition stat \"%s\" (%s)"), *Where, *Condition.Stat.ToString(), *DescribeKnownStats());
				return false;
			}
			double Value = 0.0;
			if (Clause->TryGetNumberField(TEXT("at_least"), Value))
			{
				Condition.bAtLeast = true;
			}
			else if (Clause->TryGetNumberField(TEXT("below"), Value))
			{
				Condition.bAtLeast = false;
			}
			else
			{
				OutError = Where + TEXT(": a condition needs \"at_least\" or \"below\"");
				return false;
			}
			Condition.Value = static_cast<float>(Value);
			OutConditions.Add(Condition);
		}
		return true;
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
	else if (Lower == TEXT("feed"))
	{
		OutKind = ETutorialStepKind::Feed;
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
		{ TEXT("failure_retry"), ETutorialTriggerType::FailureRetry }, { TEXT("condition"), ETutorialTriggerType::Condition } };
	if (const ETutorialTriggerType* Found = Types.Find(Text.TrimStartAndEnd().ToLower()))
	{
		OutType = *Found;
		return true;
	}
	return false;
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
	WarnUnknownKeys(Root, { TEXT("version"), TEXT("id"), TEXT("start_beat"), TEXT("beats"), TEXT("steps"), TEXT("enemies") }, TEXT("root"), OutWarnings);
	OutData.Version = static_cast<int32>(JsonNumber(Root, TEXT("version"), 1.0));
	OutData.Id = JsonString(Root, TEXT("id"));
	OutData.StartBeat = FName(*JsonString(Root, TEXT("start_beat")));

	// 2026-10-21: "enemies" - the level enemies lessons are bound to (selectors by route / actor; the map is not changed for them).
	for (const TSharedPtr<FJsonObject>& Object : JsonObjects(Root, TEXT("enemies")))
	{
		const FString Id = JsonString(Object, TEXT("id"));
		if (Id.IsEmpty())
		{
			OutError = TEXT("tutorial steps: an \"enemies\" binding without \"id\"");
			return false;
		}
		if (OutData.FindEnemy(FName(*Id)))
		{
			OutError = FString::Printf(TEXT("tutorial steps: duplicate enemy binding id \"%s\""), *Id);
			return false;
		}
		WarnUnknownKeys(Object, { TEXT("id"), TEXT("route"), TEXT("actor") }, TEXT("enemy ") + Id, OutWarnings);
		FTutorialEnemyBinding& Binding = OutData.Enemies.AddDefaulted_GetRef();
		Binding.Id = FName(*Id);
		Binding.Routes = JsonStrings(Object, TEXT("route"));
		Binding.Actors = JsonStrings(Object, TEXT("actor"));
		if (Binding.Routes.IsEmpty() && Binding.Actors.IsEmpty())
		{
			OutError = FString::Printf(TEXT("tutorial steps: enemy binding \"%s\" needs \"route\" and / or \"actor\" (label, object name or tag)"), *Id);
			return false;
		}
	}

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
			WarnUnknownKeys(Object, { TEXT("id"), TEXT("lesson"), TEXT("checkpoint"), TEXT("title_id"), TEXT("lock"), TEXT("unlock") },
				TEXT("beat ") + Id, OutWarnings);
			FTutorialBeat& Beat = OutData.Beats.AddDefaulted_GetRef();
			Beat.Id = FName(*Id);
			Beat.bLesson = JsonBool(Object, TEXT("lesson"), false);
			Beat.bCheckpoint = JsonBool(Object, TEXT("checkpoint"), true);
			Beat.TitleId = FName(*JsonString(Object, TEXT("title_id")));
			Beat.Lock = JsonStrings(Object, TEXT("lock"));
			Beat.Unlock = JsonStrings(Object, TEXT("unlock"));
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
			TEXT("once"), TEXT("skippable"), TEXT("blocking"), TEXT("lines"), TEXT("condition"), TEXT("lock"), TEXT("unlock"), TEXT("action"),
			TEXT("actions"), TEXT("focus"), TEXT("precredit"), TEXT("cancel_on"), TEXT("confine"), TEXT("guide"), TEXT("beyond") }, Where, OutWarnings);
		FTutorialStep Step;
		Step.Id = FName(*Id);
		Step.Beat = FName(*JsonString(Object, TEXT("beat")));
		if (!ParseKind(JsonString(Object, TEXT("kind"), TEXT("hint_card")), Step.Kind))
		{
			OutError = FString::Printf(TEXT("%s: unknown kind \"%s\" (hint_card | objective | radio | walk_talk | feed)"), *Where, *JsonString(Object, TEXT("kind")));
			return false;
		}
		Step.TextId = FName(*JsonString(Object, TEXT("text_id")));
		Step.Keys = JsonStrings(Object, TEXT("keys"));
		Step.bOnce = JsonBool(Object, TEXT("once"), true);
		Step.bPrecredit = JsonBool(Object, TEXT("precredit"), true);
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
			WarnUnknownKeys(Trigger, { TEXT("type"), TEXT("volume"), TEXT("event"), TEXT("step"), TEXT("command"), TEXT("count"), TEXT("delay_s"),
				TEXT("requires"), TEXT("enemy"), TEXT("within_m"), TEXT("off_route_m") }, Where + TEXT(" trigger"), OutWarnings);
			if (!ParseTriggerType(JsonString(Trigger, TEXT("type"), TEXT("manual")), Step.Trigger.Type))
			{
				OutError = FString::Printf(TEXT("%s: unknown trigger type \"%s\""), *Where, *JsonString(Trigger, TEXT("type")));
				return false;
			}
			Step.Trigger.Volume = FName(*JsonString(Trigger, TEXT("volume")));
			Step.Trigger.Event = JsonString(Trigger, TEXT("event"));
			// "step": one id, or an array of ids that must ALL have finished (FTutorialTrigger::AfterSteps).
			for (const FString& AfterStep : JsonStrings(Trigger, TEXT("step")))
			{
				Step.Trigger.AfterSteps.AddUnique(FName(*AfterStep));
			}
			Step.Trigger.Step = Step.Trigger.AfterSteps.IsEmpty() ? NAME_None : Step.Trigger.AfterSteps[0];
			Step.Trigger.Command = JsonString(Trigger, TEXT("command"));
			Step.Trigger.Count = FMath::Max(1, static_cast<int32>(JsonNumber(Trigger, TEXT("count"), 1.0)));
			Step.Trigger.DelaySeconds = FMath::Max(0.f, static_cast<float>(JsonNumber(Trigger, TEXT("delay_s"), 0.0)));
			for (const FString& Required : JsonStrings(Trigger, TEXT("requires")))
			{
				Step.Trigger.Requires.Add(FName(*Required));
			}
			if (!ParseEnemyFilter(Trigger, OutData, !Step.Trigger.Event.IsEmpty(), Where + TEXT(" trigger"), Step.Trigger.EnemyFilter, OutWarnings, OutError))
			{
				return false;
			}
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
				TEXT("distinct_operatives"), TEXT("hold_s"), TEXT("in_volume"), TEXT("distinct_from"), TEXT("condition"), TEXT("subject"), TEXT("role_spots"),
				TEXT("enemy"), TEXT("within_m"), TEXT("off_route_m") }, Where + TEXT(" complete"), OutWarnings);
			Step.Complete.Commands = JsonStrings(Complete, TEXT("commands"));
			Step.Complete.Commands.Append(JsonStrings(Complete, TEXT("command")));
			Step.Complete.Event = JsonString(Complete, TEXT("event"));
			Step.Complete.Count = FMath::Max(1, static_cast<int32>(JsonNumber(Complete, TEXT("count"), 1.0)));
			Step.Complete.Volume = FName(*JsonString(Complete, TEXT("volume")));
			Step.Complete.TimeoutSeconds = FMath::Max(0.f, static_cast<float>(JsonNumber(Complete, TEXT("timeout_s"), 0.0)));
			Step.Complete.bDistinctSubjects = JsonBool(Complete, TEXT("distinct_operatives"), false);
			Step.Complete.HoldSeconds = FMath::Max(0.f, static_cast<float>(JsonNumber(Complete, TEXT("hold_s"), 0.0)));
			Step.Complete.InVolume = FName(*JsonString(Complete, TEXT("in_volume")));
			Step.Complete.bRoleSpots = JsonBool(Complete, TEXT("role_spots"), false);
			if (!ParseEnemyFilter(Complete, OutData, !Step.Complete.Event.IsEmpty(), Where + TEXT(" complete"), Step.Complete.EnemyFilter, OutWarnings, OutError))
			{
				return false;
			}
			if (Step.Complete.bRoleSpots && Step.Complete.Event.IsEmpty())
			{
				OutWarnings.Add(Where + TEXT(": \"role_spots\" only filters events - no \"event\" given"));
			}
			const FString Subject = JsonString(Complete, TEXT("subject"));
			if (!Subject.IsEmpty() && Subject != TEXT("focus"))
			{
				OutError = FString::Printf(TEXT("%s complete: unknown subject \"%s\" (focus)"), *Where, *Subject);
				return false;
			}
			Step.Complete.bFocusSubject = Subject == TEXT("focus");
			if (Step.Complete.bFocusSubject && Step.Complete.Event.IsEmpty())
			{
				OutWarnings.Add(Where + TEXT(": \"subject\" only filters events - no \"event\" given"));
			}
			for (const FString& Other : JsonStrings(Complete, TEXT("distinct_from")))
			{
				Step.Complete.DistinctFrom.AddUnique(FName(*Other));
			}
			if (!Step.Complete.InVolume.IsNone() && Step.Complete.Event.IsEmpty())
			{
				OutWarnings.Add(Where + TEXT(": \"in_volume\" only filters events - no \"event\" given"));
			}
			if (!Step.Complete.DistinctFrom.IsEmpty() && Step.Complete.Event.IsEmpty() && Step.Complete.Volume.IsNone())
			{
				OutWarnings.Add(Where + TEXT(": \"distinct_from\" only filters events / volume entries - neither given"));
			}
			if (Step.Complete.bDistinctSubjects && Step.Complete.Event.IsEmpty())
			{
				OutWarnings.Add(Where + TEXT(": \"distinct_operatives\" only counts events - no \"event\" given"));
			}
			if (Step.Complete.HoldSeconds > 0.f && Step.Complete.Commands.IsEmpty())
			{
				OutWarnings.Add(Where + TEXT(": \"hold_s\" only counts command hold time - no \"commands\" given (it would never complete)"));
			}
			// Rev 5d: "complete": { "condition": ... } - world-state completion (polled while the step runs).
			if (!ParseConditions(Complete, Where + TEXT(" complete"), Step.Complete.Conditions, OutWarnings, OutError))
			{
				return false;
			}
		}

		if (!ParseConditions(Object, Where, Step.Conditions, OutWarnings, OutError))
		{
			return false;
		}
		// 2026-10-20: "guide" (the current navigation target while the step is shown; "none" = never) and "beyond" (the squad is past
		// the step's place: it is passed and the route moves on).
		const FString GuideText = JsonString(Object, TEXT("guide")).TrimStartAndEnd();
		Step.bNoGuide = GuideText.Equals(TEXT("none"), ESearchCase::IgnoreCase);
		Step.Guide = Step.bNoGuide || GuideText.IsEmpty() ? NAME_None : FName(*GuideText);
		if (!ParseConditions(Object, Where, Step.Beyond, OutWarnings, OutError, TEXT("beyond")))
		{
			return false;
		}
		Step.Lock = JsonStrings(Object, TEXT("lock"));
		Step.Unlock = JsonStrings(Object, TEXT("unlock"));
		TArray<TSharedPtr<FJsonObject>> ActionObjects = JsonObjects(Object, TEXT("actions"));
		ActionObjects.Append(JsonObjects(Object, TEXT("action")));
		for (const TSharedPtr<FJsonObject>& ActionObject : ActionObjects)
		{
			WarnUnknownKeys(ActionObject, { TEXT("type"), TEXT("amount"), TEXT("min_level"), TEXT("posture") }, Where + TEXT(" action"), OutWarnings);
			FTutorialAction Action;
			Action.Type = FName(*JsonString(ActionObject, TEXT("type")));
			if (!IsKnownAction(Action.Type))
			{
				OutError = FString::Printf(TEXT("%s: unknown action type \"%s\" (cold_bump, set_posture, solo_focus, tactical_pause)"), *Where, *Action.Type.ToString());
				return false;
			}
			Action.Posture = JsonString(ActionObject, TEXT("posture"));
			ESquadFirePosture ParsedPosture = ESquadFirePosture::Aggressive;
			if (Action.Type == FName(TEXT("set_posture")) && !FirePostureRules::ParseName(Action.Posture, ParsedPosture))
			{
				OutError = FString::Printf(TEXT("%s: set_posture needs \"posture\": passive | defensive | aggressive (got \"%s\")"), *Where, *Action.Posture);
				return false;
			}
			Action.Amount = static_cast<float>(JsonNumber(ActionObject, TEXT("amount"), 0.0));
			Action.MinLevel = FMath::Clamp(static_cast<float>(JsonNumber(ActionObject, TEXT("min_level"), 0.0)), 0.f, 100.f);
			Step.Actions.Add(Action);
		}
		// Rev 5e: "cancel_on": { "step": id | [ids], "event": pattern, "volume": id | [ids] } (FTutorialCancelOn).
		const TSharedPtr<FJsonObject>* CancelObject = nullptr;
		if (Object->TryGetObjectField(TEXT("cancel_on"), CancelObject))
		{
			const TSharedPtr<FJsonObject> Cancel = *CancelObject;
			WarnUnknownKeys(Cancel, { TEXT("step"), TEXT("event"), TEXT("volume"), TEXT("enemy"), TEXT("within_m"), TEXT("off_route_m") }, Where + TEXT(" cancel_on"),
				OutWarnings);
			for (const FString& Other : JsonStrings(Cancel, TEXT("step")))
			{
				Step.CancelOn.Steps.AddUnique(FName(*Other));
			}
			Step.CancelOn.Event = JsonString(Cancel, TEXT("event"));
			if (!ParseEnemyFilter(Cancel, OutData, !Step.CancelOn.Event.IsEmpty(), Where + TEXT(" cancel_on"), Step.CancelOn.EnemyFilter, OutWarnings, OutError))
			{
				return false;
			}
			for (const FString& Volume : JsonStrings(Cancel, TEXT("volume")))
			{
				Step.CancelOn.Volumes.AddUnique(FName(*Volume));
			}
			if (!Step.CancelOn.IsSet())
			{
				OutWarnings.Add(Where + TEXT(": \"cancel_on\" names no step / event / volume"));
			}
		}
		// Rev 5i: "confine": { "volume", "allow", "until", "stop", "span", "stance", "exit": { "stance", "through" } } (FTutorialConfine).
		const TSharedPtr<FJsonObject>* ConfineObject = nullptr;
		if (Object->TryGetObjectField(TEXT("confine"), ConfineObject))
		{
			const TSharedPtr<FJsonObject> Confine = *ConfineObject;
			WarnUnknownKeys(Confine, { TEXT("volume"), TEXT("allow"), TEXT("until"), TEXT("stop"), TEXT("span"), TEXT("stance"), TEXT("exit"), TEXT("free") },
				Where + TEXT(" confine"), OutWarnings);
			// 2026-10-20: "free": "focus" - the step's focus operative is not held (the A4 ambusher).
			const FString FreeText = JsonString(Confine, TEXT("free"));
			if (!FreeText.IsEmpty() && FreeText != TEXT("focus"))
			{
				OutError = FString::Printf(TEXT("%s: unknown confine \"free\" \"%s\" (focus)"), *Where, *FreeText);
				return false;
			}
			Step.Confine.bFreeFocus = FreeText == TEXT("focus");
			Step.Confine.Volume = FName(*JsonString(Confine, TEXT("volume")));
			if (Step.Confine.Volume.IsNone())
			{
				OutError = Where + TEXT(": \"confine\" needs \"volume\" (the holding area)");
				return false;
			}
			for (const FString& Allow : JsonStrings(Confine, TEXT("allow")))
			{
				Step.Confine.Allow.AddUnique(FName(*Allow));
			}
			Step.Confine.Until = FName(*JsonString(Confine, TEXT("until")));
			Step.Confine.bStop = JsonBool(Confine, TEXT("stop"), false);
			Step.Confine.bSpan = JsonBool(Confine, TEXT("span"), false);
			const FString StanceText = JsonString(Confine, TEXT("stance"));
			Step.Confine.Stance = NormalizeStance(StanceText);
			if (!StanceText.IsEmpty() && Step.Confine.Stance.IsNone())
			{
				OutError = FString::Printf(TEXT("%s: unknown confine stance \"%s\" (crouch | prone)"), *Where, *StanceText);
				return false;
			}
			const TSharedPtr<FJsonObject>* ExitObject = nullptr;
			if (Confine->TryGetObjectField(TEXT("exit"), ExitObject))
			{
				// 2026-10-20: "one_at_a_time" (the fence queue) is gone - an old file gets the unknown-key warning.
				WarnUnknownKeys(*ExitObject, { TEXT("stance"), TEXT("through") }, Where + TEXT(" confine exit"), OutWarnings);
				const FString ExitStanceText = JsonString(*ExitObject, TEXT("stance"));
				Step.Confine.ExitStance = NormalizeStance(ExitStanceText);
				if (!ExitStanceText.IsEmpty() && Step.Confine.ExitStance.IsNone())
				{
					OutError = FString::Printf(TEXT("%s: unknown confine exit stance \"%s\" (crouch | prone)"), *Where, *ExitStanceText);
					return false;
				}
				Step.Confine.ThroughEvent = JsonString(*ExitObject, TEXT("through"));
			}
		}
		Step.Focus = FName(*JsonString(Object, TEXT("focus")));
		if (!Step.Focus.IsNone() && !IsKnownFocus(Step.Focus))
		{
			OutError = FString::Printf(TEXT("%s: unknown focus \"%s\" (coldest_operative, wounded_operative, role:<Commander | Engineer | MedicSapper>)"), *Where,
				*Step.Focus.ToString());
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* Lines = nullptr;
		if (Object->TryGetArrayField(TEXT("lines"), Lines))
		{
			for (const TSharedPtr<FJsonValue>& LineValue : *Lines)
			{
				const TSharedPtr<FJsonObject> LineObject = LineValue.IsValid() ? LineValue->AsObject() : nullptr;
				WarnUnknownKeys(LineObject, { TEXT("speaker"), TEXT("text_id"), TEXT("duration_s") }, Where + TEXT(" line"), OutWarnings);
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
		if (Step.Trigger.Type == ETutorialTriggerType::AfterStep)
		{
			if (Step.Trigger.AfterSteps.IsEmpty())
			{
				OutError = FString::Printf(TEXT("step %s: after_step needs \"step\""), *Step.Id.ToString());
				return false;
			}
			if (!CheckStepReferences(OutData, Step, Step.Trigger.AfterSteps, TEXT("after_step"), /*bSelfAllowed*/ true, OutError))
			{
				return false;
			}
		}
		if ((Step.Trigger.Type == ETutorialTriggerType::Event || Step.Trigger.Type == ETutorialTriggerType::FailureRetry) && Step.Trigger.Event.IsEmpty())
		{
			OutError = FString::Printf(TEXT("step %s: trigger \"%s\" needs \"event\""), *Step.Id.ToString(),
				Step.Trigger.Type == ETutorialTriggerType::Event ? TEXT("event") : TEXT("failure_retry"));
			return false;
		}
		if (Step.Trigger.Type == ETutorialTriggerType::Condition && Step.Conditions.IsEmpty())
		{
			OutError = FString::Printf(TEXT("step %s: trigger \"condition\" needs a \"condition\""), *Step.Id.ToString());
			return false;
		}
		const TArray<FName> ConfineUntil = Step.Confine.Until.IsNone() ? TArray<FName>() : TArray<FName>{ Step.Confine.Until };
		if (!CheckStepReferences(OutData, Step, Step.Complete.DistinctFrom, TEXT("complete \"distinct_from\""), /*bSelfAllowed*/ false, OutError)
			|| !CheckStepReferences(OutData, Step, Step.CancelOn.Steps, TEXT("\"cancel_on\""), /*bSelfAllowed*/ false, OutError)
			|| !CheckStepReferences(OutData, Step, ConfineUntil, TEXT("\"confine\" \"until\""), /*bSelfAllowed*/ true, OutError)
			|| !CheckStepReferences(OutData, Step, Step.Trigger.Requires, TEXT("trigger \"requires\""), /*bSelfAllowed*/ true, OutError))
		{
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
	if (!ParseSteps(Json, OutData, OutWarnings, OutError))
	{
		return false;
	}
	ValidateSteps(OutData, OutWarnings);
	return true;
}

void TutorialRules::ValidateSteps(const FTutorialStepsData& Data, TArray<FString>& OutWarnings)
{
	using namespace TutorialRulesLocal;
	auto CheckPatterns = [&OutWarnings](const FString& Where, const TCHAR* What, const TArray<FString>& Patterns)
	{
		for (const FString& Pattern : Patterns)
		{
			if (!MatchesAnyRegisteredCommand(Pattern))
			{
				OutWarnings.Add(FString::Printf(TEXT("%s: %s \"%s\" matches no command in the registry (Core/PlayerCommands.h)"), *Where, What, *Pattern));
			}
		}
	};
	for (const FTutorialBeat& Beat : Data.Beats)
	{
		const FString Where = TEXT("beat ") + Beat.Id.ToString();
		CheckPatterns(Where, TEXT("lock"), Beat.Lock);
		CheckPatterns(Where, TEXT("unlock"), Beat.Unlock);
	}
	for (const FTutorialStep& Step : Data.Steps)
	{
		const FString Where = TEXT("step ") + Step.Id.ToString();
		CheckPatterns(Where, TEXT("command"), Step.Complete.Commands);
		CheckPatterns(Where, TEXT("lock"), Step.Lock);
		CheckPatterns(Where, TEXT("unlock"), Step.Unlock);
		if (!Step.Trigger.Command.IsEmpty())
		{
			CheckPatterns(Where, TEXT("first_use command"), { Step.Trigger.Command });
		}
		for (const FString& Key : Step.Keys)
		{
			// A key cap is a command id ("Squad.Solo" -> "[B]") or a literal label ("H", "Alt+V"): a dotted token is meant as an id.
			if (Key.Contains(TEXT(".")) && !Key.Contains(TEXT("+")) && !Key.Contains(TEXT(" ")) && !PlayerCommands::Find(FName(*Key)))
			{
				OutWarnings.Add(FString::Printf(TEXT("%s: key \"%s\" looks like a command id but is not in the registry"), *Where, *Key));
			}
		}
		if (Step.Complete.bFocusSubject && Step.Focus.IsNone())
		{
			OutWarnings.Add(Where + TEXT(": \"subject\": \"focus\" without a step \"focus\" - any operative counts"));
		}
		// 2026-10-20: "beyond" passes the step AND moves the route past its place - a step without any place only gets passed.
		if (!Step.Beyond.IsEmpty() && Step.Trigger.Volume.IsNone() && Step.Complete.Volume.IsNone() && Step.Complete.InVolume.IsNone()
			&& (Step.Guide.IsNone() || Data.FindStepIndex(Step.Guide) != INDEX_NONE))
		{
			OutWarnings.Add(Where + TEXT(": \"beyond\" on a step without a place (no volume trigger / completion / guide volume) - the route does not move"));
		}
		if (Step.Guide.ToString().StartsWith(TEXT("@")) && !IsKnownGuideSpot(Step.Guide))
		{
			OutWarnings.Add(FString::Printf(TEXT("%s: unknown guide spot \"%s\" (@patrol_route, @patrol_route.<enemy>, @lure_spot.<enemy>, @enemy.<enemy>)"),
				*Where, *Step.Guide.ToString()));
		}
		// 2026-10-21: an enemy-bound spot names a binding of the file.
		FString SpotKind;
		FName SpotEnemy;
		if (ParseGuideSpot(Step.Guide, SpotKind, SpotEnemy) && !SpotEnemy.IsNone() && !Data.FindEnemy(SpotEnemy))
		{
			OutWarnings.Add(FString::Printf(TEXT("%s: guide spot \"%s\" names an unknown enemy binding (root \"enemies\")"), *Where, *Step.Guide.ToString()));
		}
		if (Step.Confine.bFreeFocus && Step.Focus.IsNone())
		{
			OutWarnings.Add(Where + TEXT(": confine \"free\": \"focus\" without a step \"focus\" - nobody is free"));
		}
		if (!Step.Guide.IsNone() && Step.Guide == Step.Id)
		{
			OutWarnings.Add(Where + TEXT(": \"guide\" names the step itself - its own markers are the default anyway"));
		}
		// after_step chains must not loop back to the step: such a step could never fire.
		if (Step.Trigger.Type == ETutorialTriggerType::AfterStep)
		{
			TArray<FName> Open = Step.Trigger.AfterSteps;
			TSet<FName> Seen;
			while (Open.Num() > 0)
			{
				const FName Current = Open.Pop();
				if (Current == Step.Id)
				{
					OutWarnings.Add(Where + TEXT(": its after_step chain leads back to itself - it never fires"));
					break;
				}
				bool bAlreadySeen = false;
				Seen.Add(Current, &bAlreadySeen);
				const FTutorialStep* Before = bAlreadySeen ? nullptr : Data.FindStep(Current);
				if (Before && Before->Trigger.Type == ETutorialTriggerType::AfterStep)
				{
					Open.Append(Before->Trigger.AfterSteps);
				}
			}
		}
	}
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

bool TutorialRules::IsProgressMet(int32 Progress, int32 Needed, float HeldSeconds, float HoldSeconds)
{
	return Progress >= FMath::Max(1, Needed) && HeldSeconds + KINDA_SMALL_NUMBER >= FMath::Max(0.f, HoldSeconds);
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
	using namespace TutorialRulesLocal;
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
	Object->SetArrayField(TEXT("pending"), JsonNameValues(Progress.Pending));
	TArray<TSharedPtr<FJsonValue>> Locks;
	for (const FString& Lock : Progress.Locks)
	{
		Locks.Add(MakeShared<FJsonValueString>(Lock));
	}
	Object->SetArrayField(TEXT("locks"), Locks);
	const TSharedRef<FJsonObject> History = MakeShared<FJsonObject>();
	TArray<FName> Commands;
	Progress.History.GetKeys(Commands);
	Commands.Sort(FNameLexicalLess());
	for (const FName Command : Commands)
	{
		const TSharedRef<FJsonObject> Use = MakeShared<FJsonObject>();
		Use->SetNumberField(TEXT("count"), Progress.History[Command].Count);
		Use->SetNumberField(TEXT("held_s"), Progress.History[Command].HeldSeconds);
		History->SetObjectField(Command.ToString(), Use);
	}
	Object->SetObjectField(TEXT("history"), History);
	// Rev 5f: how far the running steps got and who counted for which step.
	const TSharedRef<FJsonObject> Runtime = MakeShared<FJsonObject>();
	TArray<FName> StepIds;
	Progress.StepProgress.GetKeys(StepIds);
	StepIds.Sort(FNameLexicalLess());
	for (const FName StepId : StepIds)
	{
		const FTutorialStepProgress& Step = Progress.StepProgress[StepId];
		const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetNumberField(TEXT("progress"), Step.Progress);
		Entry->SetNumberField(TEXT("held_s"), Step.HeldSeconds);
		Entry->SetArrayField(TEXT("subjects"), JsonNameValues(Step.Subjects));
		Entry->SetArrayField(TEXT("contributors"), JsonNameValues(Step.Contributors));
		Runtime->SetObjectField(StepId.ToString(), Entry);
	}
	Object->SetObjectField(TEXT("runtime"), Runtime);
	// Rev 5i: the confinements started so far (a released one must stay released after a load).
	TArray<TSharedPtr<FJsonValue>> Confines;
	for (const FTutorialConfineProgress& Confine : Progress.Confines)
	{
		const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("step"), Confine.Owner.ToString());
		Entry->SetBoolField(TEXT("released"), Confine.bReleased);
		Entry->SetArrayField(TEXT("passed"), JsonNameValues(Confine.Passed));
		Confines.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Object->SetArrayField(TEXT("confines"), Confines);
	// 2026-10-20: the furthest place reached on the route (the current navigation target is derived from it).
	Object->SetStringField(TEXT("route"), Progress.RouteVolume.IsNone() ? FString() : Progress.RouteVolume.ToString());
	return Object;
}

bool TutorialRules::LoadProgress(const FJsonObject& Object, FTutorialProgress& OutProgress)
{
	using namespace TutorialRulesLocal;
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
	for (const FString& Id : JsonStringArray(Object, TEXT("pending")))
	{
		OutProgress.Pending.Add(FName(*Id));
	}
	// An older save without "locks" rebuilds them from the beats / finished steps (FTutorialStateMachine::ImportProgress).
	const TArray<TSharedPtr<FJsonValue>>* LockValues = nullptr;
	OutProgress.bHasLocks = Object.TryGetArrayField(TEXT("locks"), LockValues);
	OutProgress.Locks = JsonStringArray(Object, TEXT("locks"));
	const TSharedPtr<FJsonObject>* History = nullptr;
	if (Object.TryGetObjectField(TEXT("history"), History))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*History)->Values)
		{
			const TSharedPtr<FJsonObject> Use = Entry.Value.IsValid() ? Entry.Value->AsObject() : nullptr;
			if (Use.IsValid() && !Entry.Key.IsEmpty())
			{
				FTutorialCommandUse& Stored = OutProgress.History.Add(FName(*Entry.Key));
				Stored.Count = FMath::Max(0, static_cast<int32>(JsonNumber(Use, TEXT("count"), 0.0)));
				Stored.HeldSeconds = FMath::Max(0.f, static_cast<float>(JsonNumber(Use, TEXT("held_s"), 0.0)));
			}
		}
	}
	const TSharedPtr<FJsonObject>* Runtime = nullptr;
	if (Object.TryGetObjectField(TEXT("runtime"), Runtime))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Runtime)->Values)
		{
			const TSharedPtr<FJsonObject> Step = Entry.Value.IsValid() ? Entry.Value->AsObject() : nullptr;
			if (!Step.IsValid() || Entry.Key.IsEmpty())
			{
				continue;
			}
			FTutorialStepProgress& Stored = OutProgress.StepProgress.Add(FName(*Entry.Key));
			Stored.Progress = FMath::Max(0, static_cast<int32>(JsonNumber(Step, TEXT("progress"), 0.0)));
			Stored.HeldSeconds = FMath::Max(0.f, static_cast<float>(JsonNumber(Step, TEXT("held_s"), 0.0)));
			JsonUniqueNames(*Step, TEXT("subjects"), Stored.Subjects);
			JsonUniqueNames(*Step, TEXT("contributors"), Stored.Contributors);
		}
	}
	const TArray<TSharedPtr<FJsonValue>>* Confines = nullptr;
	if (Object.TryGetArrayField(TEXT("confines"), Confines))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Confines)
		{
			const TSharedPtr<FJsonObject> Entry = Value.IsValid() ? Value->AsObject() : nullptr;
			FString Owner;
			if (!Entry.IsValid() || !Entry->TryGetStringField(TEXT("step"), Owner) || Owner.IsEmpty())
			{
				continue;
			}
			FTutorialConfineProgress& Stored = OutProgress.Confines.AddDefaulted_GetRef();
			Stored.Owner = FName(*Owner);
			Stored.bReleased = JsonBool(Entry, TEXT("released"), false);
			JsonUniqueNames(*Entry, TEXT("passed"), Stored.Passed);
		}
	}
	FString Route;
	if (Object.TryGetStringField(TEXT("route"), Route) && !Route.IsEmpty())
	{
		OutProgress.RouteVolume = FName(*Route); // an older save without it: the route starts at the saved beat's start
	}
	return true;
}

FName TutorialRules::ParseNotLowInStat(FName Stat)
{
	static const FString Prefix(TEXT("squad_not_low_in."));
	const FString Name = Stat.ToString();
	return Name.StartsWith(Prefix) && Name.Len() > Prefix.Len() ? FName(*Name.Mid(Prefix.Len())) : NAME_None;
}

FName TutorialRules::ParseNotAcrossStat(FName Stat)
{
	static const FString Prefix(TEXT("squad_not_across."));
	const FString Name = Stat.ToString();
	return Name.StartsWith(Prefix) && Name.Len() > Prefix.Len() ? FName(*Name.Mid(Prefix.Len())) : NAME_None;
}

bool TutorialRules::ComputeEdgePointer(const FVector2D& ViewSize, const FVector2D& Projected, bool bProjected, float Margin, FVector2D& OutTip,
	FVector2D& OutDirection)
{
	if (ViewSize.X <= 2.f * Margin || ViewSize.Y <= 2.f * Margin)
	{
		return false;
	}
	const bool bOnScreen = bProjected && Projected.X >= Margin && Projected.Y >= Margin && Projected.X <= ViewSize.X - Margin && Projected.Y <= ViewSize.Y - Margin;
	if (bOnScreen)
	{
		return false;
	}
	const FVector2D Centre = ViewSize * 0.5f;
	FVector2D Direction = (Projected - Centre).GetSafeNormal();
	if (!bProjected)
	{
		Direction = -Direction; // behind the camera: the projection is mirrored
	}
	if (Direction.IsNearlyZero())
	{
		Direction = FVector2D(0.f, 1.f);
	}
	// The ray from the centre, clamped to the inset screen rectangle.
	const FVector2D Half = Centre - FVector2D(Margin, Margin);
	const float TX = FMath::Abs(Direction.X) > KINDA_SMALL_NUMBER ? Half.X / FMath::Abs(Direction.X) : BIG_NUMBER;
	const float TY = FMath::Abs(Direction.Y) > KINDA_SMALL_NUMBER ? Half.Y / FMath::Abs(Direction.Y) : BIG_NUMBER;
	OutDirection = Direction;
	OutTip = Centre + Direction * FMath::Min(TX, TY);
	return true;
}

bool TutorialRules::ProjectToView(const FVector& CameraLocation, const FRotator& CameraRotation, float HorizontalFovDegrees, const FVector2D& ViewSize,
	const FVector& World, FVector2D& OutScreen)
{
	const FVector2D Centre = ViewSize * 0.5f;
	const FVector Local = CameraRotation.UnrotateVector(World - CameraLocation);
	constexpr float NearPlaneCm = 1.f;
	if (Local.X > NearPlaneCm)
	{
		const float HalfFov = FMath::DegreesToRadians(FMath::Clamp(HorizontalFovDegrees, 5.f, 170.f) * 0.5f);
		const float Focal = Centre.X / FMath::Tan(HalfFov);
		OutScreen = Centre + FVector2D(Local.Y / Local.X, -Local.Z / Local.X) * Focal;
		return true;
	}
	FVector2D Side(Local.Y, -Local.Z);
	Side = Side.IsNearlyZero() ? FVector2D(0.f, 1.f) : Side.GetSafeNormal();
	OutScreen = Centre + Side * (ViewSize.Size() * 4.f);
	return false;
}

FString TutorialRules::FormatGuideDistance(float DistanceCm)
{
	const int32 Metres = FMath::Max(0, FMath::RoundToInt(DistanceCm / 100.f));
	return FString::Printf(TEXT("%d m"), Metres);
}

FName TutorialRules::NormalizeStance(const FString& Text)
{
	const FString Lower = Text.TrimStartAndEnd().ToLower();
	if (Lower == TEXT("crouch") || Lower == TEXT("crouched") || Lower == TEXT("crouching"))
	{
		return FName(TEXT("crouch"));
	}
	if (Lower == TEXT("prone"))
	{
		return FName(TEXT("prone"));
	}
	if (Lower == TEXT("stand") || Lower == TEXT("standing"))
	{
		return FName(TEXT("stand"));
	}
	return NAME_None;
}

bool TutorialRules::StanceSatisfies(FName Have, FName Need)
{
	if (Need.IsNone() || Need == FName(TEXT("stand")))
	{
		return true;
	}
	if (Need == FName(TEXT("crouch")))
	{
		return Have == FName(TEXT("crouch")) || Have == FName(TEXT("prone")); // lower than crouched is low enough
	}
	return Have == Need;
}

ETutorialConfineVerdict TutorialRules::JudgeConfinedMove(const FTutorialConfineView& Confine, FName OperativeId, FName OperativeStance, bool bDestinationInside)
{
	if (Confine.Spec.bFreeFocus && !OperativeId.IsNone() && OperativeId == Confine.FreeOperative)
	{
		return ETutorialConfineVerdict::Allowed; // 2026-10-20: the one who does the lesson is never held
	}
	if (!StanceSatisfies(OperativeStance, Confine.Spec.Stance))
	{
		return ETutorialConfineVerdict::Stance;
	}
	if (bDestinationInside)
	{
		return ETutorialConfineVerdict::Allowed;
	}
	if (Confine.Spec.bFreeFocus)
	{
		// 2026-10-20: one operative does the lesson; the rest hold the area whatever their stance.
		return !OperativeId.IsNone() && OperativeId == Confine.FreeOperative ? ETutorialConfineVerdict::Allowed : ETutorialConfineVerdict::HeldBack;
	}
	if (!Confine.Spec.HasExit())
	{
		return ETutorialConfineVerdict::Outside;
	}
	if (!OperativeId.IsNone() && Confine.Passed.Contains(OperativeId))
	{
		return ETutorialConfineVerdict::Allowed; // through already: free to move on
	}
	if (!StanceSatisfies(OperativeStance, Confine.Spec.ExitStance))
	{
		return ETutorialConfineVerdict::ExitStance;
	}
	return ETutorialConfineVerdict::Allowed;
}

const TCHAR* TutorialRules::ConfineVerdictName(ETutorialConfineVerdict Verdict)
{
	switch (Verdict)
	{
	case ETutorialConfineVerdict::Allowed: return TEXT("allowed");
	case ETutorialConfineVerdict::Outside: return TEXT("outside");
	case ETutorialConfineVerdict::Stance: return TEXT("stance");
	case ETutorialConfineVerdict::ExitStance: return TEXT("exit_stance");
	case ETutorialConfineVerdict::HeldBack: return TEXT("held_back");
	default: return TEXT("?");
	}
}

FString TutorialRules::GetCommandFamily(FName CommandId)
{
	FString Family = CommandId.ToString();
	int32 Dot = INDEX_NONE;
	if (Family.FindChar(TEXT('.'), Dot))
	{
		Family.LeftInline(Dot);
	}
	return Family;
}

bool TutorialRules::CanPrecredit(const FTutorialStep& Step)
{
	// Remedial / repeatable cards ("once": false, failure_retry) remind: their history never counts.
	return Step.bPrecredit && Step.bOnce && Step.Kind == ETutorialStepKind::HintCard && Step.Complete.Commands.Num() > 0 && Step.Complete.Event.IsEmpty()
		&& Step.Complete.Volume.IsNone();
}

int32 TutorialRules::CountHistoryUses(const FTutorialStep& Step, const TMap<FName, FTutorialCommandUse>& History)
{
	return TutorialRulesLocal::SumHistoryForStep(Step, History).Count;
}

bool TutorialRules::IsPrecreditSatisfied(const FTutorialStep& Step, const TMap<FName, FTutorialCommandUse>& History)
{
	if (!CanPrecredit(Step))
	{
		return false;
	}
	const FTutorialCommandUse Done = TutorialRulesLocal::SumHistoryForStep(Step, History);
	return IsProgressMet(Done.Count, Step.Complete.Count, Done.HeldSeconds, Step.Complete.HoldSeconds);
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

bool TutorialRules::IsKnownStat(FName Stat)
{
	// Rev 5h: "traps_spotted.<SpotEventTag>" counts the spotted traps of a tagged group (TrapSpotRules::ParseSpottedStatTag).
	if (!TrapSpotRules::ParseSpottedStatTag(Stat).IsNone() || !ParseNotLowInStat(Stat).IsNone() || !ParseNotAcrossStat(Stat).IsNone())
	{
		return true;
	}
	for (const TCHAR* Known : TutorialRulesLocal::PlainConditionStats)
	{
		if (Stat == FName(Known))
		{
			return true;
		}
	}
	return false;
}

bool TutorialRules::IsKnownAction(FName Type)
{
	return Type == FName(TEXT("cold_bump")) || Type == FName(TEXT("set_posture")) || Type == FName(TEXT("solo_focus"))
		|| Type == FName(TEXT("tactical_pause"));
}

bool TutorialRules::IsKnownFocus(FName Focus)
{
	return Focus == FName(TEXT("coldest_operative")) || Focus == FName(TEXT("wounded_operative")) || !ParseRoleFocus(Focus).IsNone();
}

FName TutorialRules::ParseRoleFocus(FName Focus)
{
	static const FString Prefix(TEXT("role:"));
	const FString Name = Focus.ToString();
	if (!Name.StartsWith(Prefix))
	{
		return NAME_None;
	}
	const FString Role = Name.Mid(Prefix.Len());
	for (const TCHAR* Known : { TEXT("Commander"), TEXT("Engineer"), TEXT("MedicSapper"), TEXT("Recruit") })
	{
		if (Role == Known)
		{
			return FName(Known);
		}
	}
	return NAME_None;
}

bool TutorialRules::ParseGuideSpot(FName Guide, FString& OutKind, FName& OutEnemy)
{
	OutKind.Reset();
	OutEnemy = NAME_None;
	const FString Text = Guide.IsNone() ? FString() : Guide.ToString();
	if (!Text.StartsWith(TEXT("@")))
	{
		return false;
	}
	FString Kind = Text.Mid(1);
	FString Enemy;
	if (Kind.Split(TEXT("."), &Kind, &Enemy))
	{
		OutEnemy = Enemy.IsEmpty() ? NAME_None : FName(*Enemy);
	}
	OutKind = Kind.ToLower();
	return true;
}

bool TutorialRules::IsKnownGuideSpot(FName Guide)
{
	FString Kind;
	FName Enemy;
	if (!ParseGuideSpot(Guide, Kind, Enemy))
	{
		return false;
	}
	if (Kind == TEXT("patrol_route"))
	{
		return true; // the level's routes, or the bound enemy's
	}
	return (Kind == TEXT("lure_spot") || Kind == TEXT("enemy")) && !Enemy.IsNone();
}

bool TutorialRules::MatchesActorSelector(const FString& Selector, FName ObjectName, const FString& Label, const TArray<FName>& Tags)
{
	const FString Wanted = Selector.TrimStartAndEnd();
	if (Wanted.IsEmpty())
	{
		return false;
	}
	if ((!ObjectName.IsNone() && ObjectName.ToString().Equals(Wanted, ESearchCase::IgnoreCase)) || (!Label.IsEmpty() && Label.Equals(Wanted, ESearchCase::IgnoreCase)))
	{
		return true;
	}
	return Tags.ContainsByPredicate([&Wanted](FName Tag) { return !Tag.IsNone() && Tag.ToString().Equals(Wanted, ESearchCase::IgnoreCase); });
}

FVector TutorialRules::NearestPointOnRoute(const TArray<FVector>& Points, bool bLoop, const FVector& From)
{
	FVector Best = Points.IsEmpty() ? From : Points[0];
	double BestDistance = TNumericLimits<double>::Max();
	const int32 Segments = Points.Num() < 2 ? Points.Num() : (bLoop ? Points.Num() : Points.Num() - 1);
	for (int32 Index = 0; Index < Segments; ++Index)
	{
		const FVector A = Points[Index];
		const FVector B = Points.Num() < 2 ? A : Points[(Index + 1) % Points.Num()];
		// 2D: the segment flattened at A's height (a waypoint dragged below the ground must not pull the spot under it).
		const FVector Candidate = FMath::ClosestPointOnSegment(FVector(From.X, From.Y, A.Z), A, FVector(B.X, B.Y, A.Z));
		const double Distance = FVector::DistSquared2D(Candidate, From);
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = Candidate;
		}
	}
	return Best;
}

float TutorialRules::DistanceToRoute2D(const TArray<FVector>& Points, bool bLoop, const FVector& Location)
{
	if (Points.IsEmpty())
	{
		return MAX_flt;
	}
	return static_cast<float>(FVector::Dist2D(NearestPointOnRoute(Points, bLoop, Location), Location));
}

FVector TutorialRules::LureSpotTowards(const FVector& RoutePoint, const FVector& From, float DistanceCm)
{
	const FVector Flat(From.X - RoutePoint.X, From.Y - RoutePoint.Y, 0.f);
	const double Length = Flat.Size();
	if (Length <= DistanceCm || Length < KINDA_SMALL_NUMBER)
	{
		return From;
	}
	return RoutePoint + Flat / Length * DistanceCm;
}

bool TutorialRules::PassesEnemyFilter(const FTutorialEnemyFilter& Filter, bool bSubjectMatches, float NearestOperativeCm, float OffRouteCm)
{
	if (!Filter.IsSet())
	{
		return true;
	}
	if (!bSubjectMatches)
	{
		return false; // another enemy (or an operative): not this lesson's
	}
	if (Filter.WithinMetres > 0.f && (NearestOperativeCm < 0.f || NearestOperativeCm > Filter.WithinMetres * 100.f))
	{
		return false; // nobody close: not a close-range kill
	}
	if (Filter.OffRouteMetres > 0.f && OffRouteCm >= 0.f && OffRouteCm < Filter.OffRouteMetres * 100.f)
	{
		return false; // still on its beat: not lured into the open
	}
	return true;
}

bool TutorialRules::IsConditionMet(const TArray<FTutorialCondition>& Conditions, TFunctionRef<float(FName)> Stat)
{
	for (const FTutorialCondition& Condition : Conditions)
	{
		const float Value = Stat(Condition.Stat);
		if (Condition.bAtLeast ? !(Value >= Condition.Value) : !(Value < Condition.Value))
		{
			return false;
		}
	}
	return true;
}

bool TutorialRules::PatternCovers(const FString& Outer, const FString& Inner)
{
	const FString OuterTrimmed = Outer.TrimStartAndEnd();
	const FString InnerTrimmed = Inner.TrimStartAndEnd();
	if (OuterTrimmed.IsEmpty() || InnerTrimmed.IsEmpty())
	{
		return false;
	}
	if (OuterTrimmed == TEXT("*"))
	{
		return true;
	}
	if (OuterTrimmed.EndsWith(TEXT("*")))
	{
		const FString Prefix = OuterTrimmed.LeftChop(1);
		const FString InnerBody = InnerTrimmed.EndsWith(TEXT("*")) ? InnerTrimmed.LeftChop(1) : InnerTrimmed;
		return InnerBody.StartsWith(Prefix, ESearchCase::IgnoreCase);
	}
	return !InnerTrimmed.EndsWith(TEXT("*")) && OuterTrimmed.Equals(InnerTrimmed, ESearchCase::IgnoreCase);
}

bool TutorialRules::ApplyLockChanges(TArray<FString>& InOutLocks, const TArray<FString>& Lock, const TArray<FString>& Unlock)
{
	// Entries: lock patterns ("Stance.*") and exceptions ("!Trap.DisarmMine": a command freed out of a wider lock).
	auto IsException = [](const FString& Entry) { return Entry.StartsWith(TEXT("!")); };
	const TArray<FString> Before = InOutLocks;
	for (const FString& Pattern : Unlock)
	{
		if (Pattern.IsEmpty())
		{
			continue;
		}
		if (Pattern == TEXT("*"))
		{
			InOutLocks.Reset();
			continue;
		}
		// Every lock inside the unlocked pattern goes ("Stance.*" frees "Stance.Squad.*" too).
		InOutLocks.RemoveAll([&Pattern, &IsException](const FString& Existing) { return !IsException(Existing) && PatternCovers(Pattern, Existing); });
		// A wider lock still covers it ("Trap.*" when "Trap.DisarmMine" is taught): keep that lock, free this part.
		const bool bStillCovered = InOutLocks.ContainsByPredicate([&Pattern, &IsException](const FString& Existing)
		{
			return !IsException(Existing) && PatternCovers(Existing, Pattern);
		});
		const FString Exception = TEXT("!") + Pattern;
		if (bStillCovered && !InOutLocks.ContainsByPredicate([&Exception](const FString& Existing) { return Existing.Equals(Exception, ESearchCase::IgnoreCase); }))
		{
			InOutLocks.Add(Exception);
		}
	}
	for (const FString& Pattern : Lock)
	{
		if (Pattern.IsEmpty() || IsException(Pattern))
		{
			continue;
		}
		// A new lock takes back the exceptions it covers.
		InOutLocks.RemoveAll([&Pattern, &IsException](const FString& Existing) { return IsException(Existing) && PatternCovers(Pattern, Existing.Mid(1)); });
		if (!InOutLocks.ContainsByPredicate([&Pattern](const FString& Existing) { return Existing.Equals(Pattern, ESearchCase::IgnoreCase); }))
		{
			InOutLocks.Add(Pattern);
		}
	}
	// Exceptions no lock covers any more are dropped.
	InOutLocks.RemoveAll([&InOutLocks, &IsException](const FString& Existing)
	{
		return IsException(Existing) && !InOutLocks.ContainsByPredicate([&Existing, &IsException](const FString& Other)
		{
			return !IsException(Other) && PatternCovers(Other, Existing.Mid(1));
		});
	});
	return InOutLocks != Before;
}

bool TutorialRules::IsCommandLocked(const TArray<FString>& Locks, FName CommandId)
{
	if (CommandId.IsNone())
	{
		return false;
	}
	bool bLocked = false;
	for (const FString& Pattern : Locks)
	{
		if (Pattern.StartsWith(TEXT("!")))
		{
			if (PlayerCommands::Matches(Pattern.Mid(1), CommandId))
			{
				return false; // taught: freed out of a wider lock
			}
		}
		else if (!bLocked && PlayerCommands::Matches(Pattern, CommandId))
		{
			bLocked = true;
		}
	}
	return bLocked;
}

FName TutorialRules::MakeLockedEventName(FName CommandId)
{
	return FName(*(GameplayEvents::TutorialLocked.ToString() + TEXT(".") + CommandId.ToString()));
}

FName TutorialRules::ResolveLockedCommand(const TArray<FString>& Locks, FName CommandId)
{
	if (!IsCommandLocked(Locks, CommandId))
	{
		return CommandId;
	}
	if (CommandId == PlayerCommands::Sprint && !IsCommandLocked(Locks, PlayerCommands::Move))
	{
		return PlayerCommands::Move;
	}
	// Wall-edge cover switch (2026-10-20): locked, the double-click is the ordinary cover order (taught before it).
	if (CommandId == PlayerCommands::CoverSwitch && !IsCommandLocked(Locks, PlayerCommands::CoverEnter))
	{
		return PlayerCommands::CoverEnter;
	}
	return NAME_None;
}
