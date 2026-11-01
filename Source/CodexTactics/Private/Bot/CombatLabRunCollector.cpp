#include "Bot/CombatLabRunCollector.h"

#include "Bot/BotRouteRules.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace CombatLabLocal
{
	/** Rounded for the record (cm -> m with two decimals, seconds with two). */
	double Round2(double Value)
	{
		return FMath::RoundToDouble(Value * 100.0) / 100.0;
	}

	/** [x, y] rounded to whole cm. */
	TArray<TSharedPtr<FJsonValue>> XY(const FVector2D& Point)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		Out.Add(MakeShared<FJsonValueNumber>(FMath::RoundToDouble(Point.X)));
		Out.Add(MakeShared<FJsonValueNumber>(FMath::RoundToDouble(Point.Y)));
		return Out;
	}

	TSharedRef<FJsonObject> CountsObject(const TMap<FString, int32>& Counts)
	{
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		TArray<FString> Keys;
		Counts.GetKeys(Keys);
		Keys.Sort();
		for (const FString& Key : Keys)
		{
			Out->SetNumberField(Key, Counts[Key]);
		}
		return Out;
	}

	TSharedRef<FJsonObject> SecondsObject(const TMap<FString, float>& Seconds)
	{
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		TArray<FString> Keys;
		Seconds.GetKeys(Keys);
		Keys.Sort();
		for (const FString& Key : Keys)
		{
			Out->SetNumberField(Key, Round2(Seconds[Key]));
		}
		return Out;
	}
}

const TCHAR* CombatLabRules::KindName(ECombatLabContactKind Kind)
{
	switch (Kind)
	{
	case ECombatLabContactKind::Ambush: return TEXT("ambush");
	case ECombatLabContactKind::Coordinated: return TEXT("coordinated");
	default: return TEXT("patrol");
	}
}

const TCHAR* CombatLabRules::TriggerName(ECombatLabContactTrigger Trigger)
{
	switch (Trigger)
	{
	case ECombatLabContactTrigger::Engaged: return TEXT("engaged");
	case ECombatLabContactTrigger::Sprung: return TEXT("sprung");
	default: return TEXT("seen");
	}
}

const TCHAR* CombatLabRules::StanceName(EOperativeStance Stance)
{
	switch (Stance)
	{
	case EOperativeStance::Crouching: return TEXT("crouching");
	case EOperativeStance::Prone: return TEXT("prone");
	default: return TEXT("standing");
	}
}

FString CombatLabRules::FormationLabel(bool bSolo, bool bFormationActive, bool bWallOrder, EFormationKind Kind, int32 Members)
{
	if (bSolo || !bFormationActive)
	{
		return FString();
	}
	if (bWallOrder || Kind == EFormationKind::Wall)
	{
		return TEXT("wall_layout");
	}
	if (Kind == EFormationKind::Diamond && Members == 3)
	{
		return TEXT("wedge");
	}
	if (Kind == EFormationKind::Diamond && Members == 2)
	{
		return TEXT("pair");
	}
	return FormationNames::ToString(Kind);
}

void FCombatLabRunCollector::Begin(float Time)
{
	*this = FCombatLabRunCollector();
	StartTime = Time;
	EndTime = Time;
	LastTime = Time;
}

void FCombatLabRunCollector::End(float Time)
{
	EndTime = FMath::Max(EndTime, Time);
}

void FCombatLabRunCollector::AddSample(const FCombatLabSample& Sample)
{
	const float Delta = bHasSample ? FMath::Max(0.f, Sample.Time - LastTime) : 0.f;
	LastTime = Sample.Time;
	EndTime = FMath::Max(EndTime, Sample.Time);

	// Fights: rising edges and their time.
	if (Sample.bInFight && !bWasInFight)
	{
		++Fights;
	}
	if (Sample.bInFight && bWasInFight)
	{
		FightSeconds += Delta;
	}
	bWasInFight = Sample.bInFight;

	// Wall hugs: pressed to a wall after being off it for WallRearmSeconds.
	if (Sample.bAtWall && !bWasAtWall && Sample.Time - OffWallSince >= WallRearmSeconds)
	{
		++WallHugs;
	}
	if (!Sample.bAtWall && bWasAtWall)
	{
		OffWallSince = Sample.Time;
	}
	bWasAtWall = Sample.bAtWall;

	SampleFormation(Sample, Delta);
	SampleContacts(Sample);
	SampleMembers(Sample);
	bHasSample = true;
}

void FCombatLabRunCollector::SampleFormation(const FCombatLabSample& Sample, float Delta)
{
	if (!CurrentFormation.IsEmpty())
	{
		FormationSeconds.FindOrAdd(CurrentFormation) += Delta;
	}
	if (Sample.Formation.IsEmpty() || Sample.Formation == CurrentFormation)
	{
		PendingFormation.Reset(); // Solo / formation off keeps the last real formation; back to it is no change
		return;
	}
	if (Sample.Formation != PendingFormation)
	{
		PendingFormation = Sample.Formation;
		PendingSince = Sample.Time;
	}
	if (Sample.Time - PendingSince + KINDA_SMALL_NUMBER < FormationSettleSeconds && !CurrentFormation.IsEmpty())
	{
		return;
	}
	if (!CurrentFormation.IsEmpty())
	{
		++FormationChanges;
	}
	CurrentFormation = PendingFormation;
	FormationsEntered.FindOrAdd(CurrentFormation)++;
	PendingFormation.Reset();
}

void FCombatLabRunCollector::SampleContacts(const FCombatLabSample& Sample)
{
	TSet<int32> Present;
	for (const FCombatLabGroupSample& Group : Sample.Groups)
	{
		if (Group.Alive <= 0)
		{
			continue;
		}
		Present.Add(Group.Id);
		if (const int32* Known = ContactIndex.Find(Group.Id))
		{
			FCombatLabContact& Contact = Contacts[*Known];
			if (!Contact.bClosed && Contact.ReactionSeconds < 0.f && Group.bWatched)
			{
				Contact.ReactionSeconds = Sample.Time - Contact.Time;
			}
			continue;
		}
		if (!Group.bSprung && !Group.bEngaged && !Group.bSeen)
		{
			continue;
		}
		FCombatLabContact Contact;
		Contact.Id = Group.Id;
		Contact.Kind = Group.Kind;
		Contact.Trigger = Group.bSprung ? ECombatLabContactTrigger::Sprung : (Group.bEngaged ? ECombatLabContactTrigger::Engaged : ECombatLabContactTrigger::Seen);
		Contact.Time = Sample.Time;
		Contact.Position = Group.Position;
		Contact.Formation = Sample.Formation.IsEmpty() ? CurrentFormation : Sample.Formation;
		Contact.bWatchedAtStart = Group.bWatched;
		Contact.ReactionSeconds = Group.bWatched ? 0.f : -1.f;
		ContactIndex.Add(Group.Id, Contacts.Add(MoveTemp(Contact)));
	}
	for (FCombatLabContact& Contact : Contacts)
	{
		if (!Contact.bClosed && !Present.Contains(Contact.Id))
		{
			Contact.bClosed = true; // every member dead or removed
		}
	}
}

void FCombatLabRunCollector::SampleMembers(const FCombatLabSample& Sample)
{
	for (int32 Each = 0; Each < Sample.Members.Num(); ++Each)
	{
		const FCombatLabMemberSample& Now = Sample.Members[Each];
		if (!Members.IsValidIndex(Each))
		{
			FCombatLabMemberStats Stats;
			Stats.Name = Now.Name;
			Members.Add(Stats);
			LastMembers.Add(Now); // the baseline: no change counted for the first sample
			continue;
		}
		FCombatLabMemberStats& Stats = Members[Each];
		const FCombatLabMemberSample& Before = LastMembers[Each];
		if (Now.Stance != Before.Stance && Now.bAlive)
		{
			++Stats.StanceChanges;
			(Now.Stance == EOperativeStance::Prone ? Stats.ToProne : (Now.Stance == EOperativeStance::Crouching ? Stats.ToCrouching : Stats.ToStanding))++;
		}
		if (Now.Health < Before.Health)
		{
			Stats.DamageTaken += Before.Health - Now.Health;
		}
		Stats.DamageAbsorbed += FMath::Max(0.f, Now.AbsorbedDamage - Before.AbsorbedDamage);
		Stats.Rounds += FMath::Max(0, Now.RoundsSpent - Before.RoundsSpent);
		Stats.ClimbsUp += FMath::Max(0, Now.ClimbsUp - Before.ClimbsUp);
		Stats.ClimbsDown += FMath::Max(0, Now.ClimbsDown - Before.ClimbsDown);
		if (Now.bKnockedDown && !Before.bKnockedDown)
		{
			++Stats.Knockdowns;
		}
		LastMembers[Each] = Now;
	}
}

int32 FCombatLabRunCollector::CountContacts(ECombatLabContactKind Kind) const
{
	return Contacts.FilterByPredicate([Kind](const FCombatLabContact& Each) { return Each.Kind == Kind; }).Num();
}

int32 FCombatLabRunCollector::CountUnwatchedStrikes() const
{
	return Contacts.FilterByPredicate([](const FCombatLabContact& Each) { return Each.IsUnwatchedStrike(); }).Num();
}

int32 FCombatLabRunCollector::CountNeverLooked() const
{
	return Contacts.FilterByPredicate([](const FCombatLabContact& Each) { return Each.ReactionSeconds < 0.f; }).Num();
}

float FCombatLabRunCollector::GetMeanReaction() const
{
	float Sum = 0.f;
	int32 Count = 0;
	for (const FCombatLabContact& Each : Contacts)
	{
		if (Each.ReactionSeconds >= 0.f)
		{
			Sum += Each.ReactionSeconds;
			++Count;
		}
	}
	return Count > 0 ? Sum / static_cast<float>(Count) : 0.f;
}

float FCombatLabRunCollector::GetMaxReaction() const
{
	float Max = 0.f;
	for (const FCombatLabContact& Each : Contacts)
	{
		Max = FMath::Max(Max, Each.ReactionSeconds);
	}
	return Max;
}

int32 FCombatLabRunCollector::GetStanceChanges() const
{
	int32 Sum = 0;
	for (const FCombatLabMemberStats& Each : Members)
	{
		Sum += Each.StanceChanges;
	}
	return Sum;
}

float FCombatLabRunCollector::GetStanceChangesPerMinute() const
{
	const float Minutes = GetDurationSeconds() / 60.f;
	return Minutes > KINDA_SMALL_NUMBER ? static_cast<float>(GetStanceChanges()) / Minutes : 0.f;
}

float FCombatLabRunCollector::GetDamageTaken() const
{
	float Sum = 0.f;
	for (const FCombatLabMemberStats& Each : Members)
	{
		Sum += Each.DamageTaken;
	}
	return Sum;
}

float FCombatLabRunCollector::GetDamageAbsorbed() const
{
	float Sum = 0.f;
	for (const FCombatLabMemberStats& Each : Members)
	{
		Sum += Each.DamageAbsorbed;
	}
	return Sum;
}

int32 FCombatLabRunCollector::GetRounds() const
{
	int32 Sum = 0;
	for (const FCombatLabMemberStats& Each : Members)
	{
		Sum += Each.Rounds;
	}
	return Sum;
}

int32 FCombatLabRunCollector::GetClimbsUp() const
{
	int32 Sum = 0;
	for (const FCombatLabMemberStats& Each : Members)
	{
		Sum += Each.ClimbsUp;
	}
	return Sum;
}

int32 FCombatLabRunCollector::GetClimbsDown() const
{
	int32 Sum = 0;
	for (const FCombatLabMemberStats& Each : Members)
	{
		Sum += Each.ClimbsDown;
	}
	return Sum;
}

int32 FCombatLabRunCollector::GetKnockdowns() const
{
	int32 Sum = 0;
	for (const FCombatLabMemberStats& Each : Members)
	{
		Sum += Each.Knockdowns;
	}
	return Sum;
}

TSharedRef<FJsonObject> CombatLabRules::MakeRunRecord(const FCombatLabRunMeta& Meta, const FBotRouteTracker& Route, const FCombatLabRunCollector& Collector,
	const FVector2D& LeaderNow)
{
	using namespace CombatLabLocal;
	TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
	Record->SetStringField(TEXT("schema"), TEXT("codex.combatlab_run/1"));
	Record->SetStringField(TEXT("kind"), TEXT("combat_lab"));
	Record->SetStringField(TEXT("result"), Meta.Result);
	Record->SetNumberField(TEXT("run"), Meta.Run);
	Record->SetNumberField(TEXT("bot_seed"), Meta.BotSeed);
	Record->SetNumberField(TEXT("pressure_seed"), Meta.PressureSeed);
	Record->SetStringField(TEXT("level_id"), Meta.LevelId);
	Record->SetStringField(TEXT("map"), Meta.Map);
	Record->SetStringField(TEXT("timestamp_utc"), Meta.TimestampUtc);
	Record->SetNumberField(TEXT("duration_sec"), Round2(Collector.GetDurationSeconds()));
	Record->SetNumberField(TEXT("planned_sec"), Round2(Meta.PlannedSeconds));
	Record->SetNumberField(TEXT("real_sec"), Round2(Meta.RealSeconds));
	{
		TSharedRef<FJsonObject> Cheats = MakeShared<FJsonObject>();
		Cheats->SetBoolField(TEXT("immortal"), Meta.bImmortal);
		Cheats->SetBoolField(TEXT("infinite_ammo"), Meta.bInfiniteAmmo);
		Record->SetObjectField(TEXT("cheats"), Cheats);
	}

	// The route: completed or not, how far, where / why the bot got stuck.
	{
		const FBotRouteConfig& Config = Route.GetConfig();
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("name"), Config.Name);
		Out->SetBoolField(TEXT("loop"), Config.bLoop);
		Out->SetNumberField(TEXT("waypoints"), Config.Points.Num());
		Out->SetNumberField(TEXT("lap_length_m"), Round2(Config.LapLengthCm() / 100.f));
		Out->SetBoolField(TEXT("completed"), Route.IsRouteCompleted());
		Out->SetNumberField(TEXT("progress"), Round2(Route.GetProgress()));
		Out->SetNumberField(TEXT("laps"), Route.GetLaps().Num());
		Out->SetNumberField(TEXT("reached"), Route.GetTotalReached());
		Out->SetNumberField(TEXT("skipped"), Route.GetTotalSkipped());
		Out->SetNumberField(TEXT("distance_m"), Round2(Route.GetDistanceCm() / 100.f));
		const int32 Furthest = Route.GetFurthestFirstLap();
		Out->SetNumberField(TEXT("furthest_index"), Furthest);
		Out->SetStringField(TEXT("furthest_label"), Config.Points.IsValidIndex(Furthest) ? Config.Points[Furthest].Label : FString());
		Out->SetNumberField(TEXT("current_index"), Route.GetIndex());
		Out->SetStringField(TEXT("current_label"), Route.GetWaypoint() ? Route.GetWaypoint()->Label : FString());
		Out->SetStringField(TEXT("last_order"), Route.GetLastOrder());
		Out->SetArrayField(TEXT("end_position"), XY(LeaderNow));
		TArray<TSharedPtr<FJsonValue>> Laps;
		for (const FBotRouteLap& Lap : Route.GetLaps())
		{
			TSharedRef<FJsonObject> Each = MakeShared<FJsonObject>();
			Each->SetNumberField(TEXT("seconds"), Round2(Lap.Seconds));
			Each->SetNumberField(TEXT("reached"), Lap.Reached);
			Each->SetNumberField(TEXT("skipped"), Lap.Skipped);
			Laps.Add(MakeShared<FJsonValueObject>(Each));
		}
		Out->SetArrayField(TEXT("lap_list"), Laps);
		TArray<TSharedPtr<FJsonValue>> Stucks;
		for (const FBotRouteStuck& Stuck : Route.GetStucks())
		{
			TSharedRef<FJsonObject> Each = MakeShared<FJsonObject>();
			Each->SetNumberField(TEXT("time"), Round2(Stuck.Time));
			Each->SetArrayField(TEXT("position"), XY(Stuck.Position));
			Each->SetNumberField(TEXT("waypoint"), Stuck.Waypoint);
			Each->SetStringField(TEXT("label"), Stuck.Label);
			Each->SetStringField(TEXT("last_order"), Stuck.LastOrder);
			Each->SetStringField(TEXT("reason"), Stuck.Reason);
			Each->SetBoolField(TEXT("skipped"), Stuck.bSkipped);
			Stucks.Add(MakeShared<FJsonValueObject>(Each));
		}
		Out->SetArrayField(TEXT("stuck"), Stucks);
		Record->SetObjectField(TEXT("route"), Out);
	}

	// Contacts.
	{
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetNumberField(TEXT("total"), Collector.GetContacts().Num());
		Out->SetNumberField(TEXT("patrol"), Collector.CountContacts(ECombatLabContactKind::Patrol));
		Out->SetNumberField(TEXT("ambush"), Collector.CountContacts(ECombatLabContactKind::Ambush));
		Out->SetNumberField(TEXT("coordinated"), Collector.CountContacts(ECombatLabContactKind::Coordinated));
		Out->SetNumberField(TEXT("unwatched_strikes"), Collector.CountUnwatchedStrikes());
		Out->SetNumberField(TEXT("never_looked"), Collector.CountNeverLooked());
		Out->SetNumberField(TEXT("reaction_mean_sec"), Round2(Collector.GetMeanReaction()));
		Out->SetNumberField(TEXT("reaction_max_sec"), Round2(Collector.GetMaxReaction()));
		TArray<TSharedPtr<FJsonValue>> List;
		for (const FCombatLabContact& Contact : Collector.GetContacts())
		{
			TSharedRef<FJsonObject> Each = MakeShared<FJsonObject>();
			Each->SetNumberField(TEXT("id"), Contact.Id);
			Each->SetStringField(TEXT("kind"), KindName(Contact.Kind));
			Each->SetStringField(TEXT("trigger"), TriggerName(Contact.Trigger));
			Each->SetNumberField(TEXT("time"), Round2(Contact.Time));
			Each->SetArrayField(TEXT("position"), XY(Contact.Position));
			Each->SetStringField(TEXT("formation"), Contact.Formation);
			Each->SetBoolField(TEXT("watched_at_start"), Contact.bWatchedAtStart);
			Each->SetBoolField(TEXT("unwatched_strike"), Contact.IsUnwatchedStrike());
			Each->SetNumberField(TEXT("reaction_sec"), Round2(Contact.ReactionSeconds));
			List.Add(MakeShared<FJsonValueObject>(Each));
		}
		Out->SetArrayField(TEXT("list"), List);
		Record->SetObjectField(TEXT("contacts"), Out);
	}

	// Formations, wall hugs, stances.
	{
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetNumberField(TEXT("changes"), Collector.GetFormationChanges());
		Out->SetObjectField(TEXT("entered"), CountsObject(Collector.GetFormationsEntered()));
		Out->SetObjectField(TEXT("seconds"), SecondsObject(Collector.GetFormationSeconds()));
		Record->SetObjectField(TEXT("formations"), Out);
	}
	Record->SetNumberField(TEXT("wall_hugs"), Collector.GetWallHugs());
	{
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetNumberField(TEXT("changes"), Collector.GetStanceChanges());
		Out->SetNumberField(TEXT("per_min"), Round2(Collector.GetStanceChangesPerMinute()));
		TArray<TSharedPtr<FJsonValue>> List;
		for (const FCombatLabMemberStats& Member : Collector.GetMembers())
		{
			TSharedRef<FJsonObject> Each = MakeShared<FJsonObject>();
			Each->SetStringField(TEXT("name"), Member.Name);
			Each->SetNumberField(TEXT("changes"), Member.StanceChanges);
			Each->SetNumberField(TEXT("standing"), Member.ToStanding);
			Each->SetNumberField(TEXT("crouching"), Member.ToCrouching);
			Each->SetNumberField(TEXT("prone"), Member.ToProne);
			Each->SetNumberField(TEXT("damage_taken"), Round2(Member.DamageTaken));
			Each->SetNumberField(TEXT("damage_absorbed"), Round2(Member.DamageAbsorbed));
			Each->SetNumberField(TEXT("rounds"), Member.Rounds);
			Each->SetNumberField(TEXT("climbs_up"), Member.ClimbsUp);
			Each->SetNumberField(TEXT("knockdowns"), Member.Knockdowns);
			List.Add(MakeShared<FJsonValueObject>(Each));
		}
		Out->SetArrayField(TEXT("members"), List);
		Record->SetObjectField(TEXT("stances"), Out);
	}
	{
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetNumberField(TEXT("taken"), Round2(Collector.GetDamageTaken()));
		Out->SetNumberField(TEXT("absorbed"), Round2(Collector.GetDamageAbsorbed()));
		Record->SetObjectField(TEXT("damage"), Out);
	}
	{
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetNumberField(TEXT("rounds"), Collector.GetRounds());
		Out->SetBoolField(TEXT("infinite"), Meta.bInfiniteAmmo);
		Record->SetObjectField(TEXT("ammo"), Out);
	}
	{
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetNumberField(TEXT("climbs_up"), Collector.GetClimbsUp());
		Out->SetNumberField(TEXT("climbs_down"), Collector.GetClimbsDown());
		Record->SetObjectField(TEXT("ladder"), Out);
	}
	Record->SetNumberField(TEXT("knockdowns"), Collector.GetKnockdowns());
	{
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetNumberField(TEXT("count"), Collector.GetFights());
		Out->SetNumberField(TEXT("seconds"), Round2(Collector.GetFightSeconds()));
		Record->SetObjectField(TEXT("fights"), Out);
	}
	{
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetNumberField(TEXT("events"), Meta.PressureEvents);
		Out->SetNumberField(TEXT("spawned"), Meta.PressureSpawned);
		Out->SetNumberField(TEXT("sprung_by_watch"), Meta.PressureSprungByWatch);
		Out->SetNumberField(TEXT("relocations"), Meta.PressureRelocations);
		Record->SetObjectField(TEXT("pressure"), Out);
	}
	return Record;
}

FString CombatLabRules::ToJsonLine(const TSharedRef<FJsonObject>& Record)
{
	FString Out;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
	FJsonSerializer::Serialize(Record, Writer);
	return Out;
}
