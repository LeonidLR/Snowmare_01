#include "Combat/FirefightSubsystem.h"

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/CombatPressureSubsystem.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/HordeSubsystem.h"
#include "Engine/World.h"
#include "GameFlow/LevelEncounterSubsystem.h"
#include "UI/GameMessageSubsystem.h"

namespace FirefightTuning
{
	/** Seconds between two passes of the slip-past rule over the level's enemy groups. */
	constexpr float GroupPassSeconds = 0.25f;
}

void UFirefightSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(ULevelEncounterSubsystem::StaticClass());
	Collection.InitializeDependency(UHordeSubsystem::StaticClass());
}

void UFirefightSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (ULevelEncounterSubsystem* Encounters = InWorld.GetSubsystem<ULevelEncounterSubsystem>())
	{
		ContactHandle = Encounters->OnHostileContact.AddUObject(this, &UFirefightSubsystem::HandleHostileContact);
	}
}

void UFirefightSubsystem::Deinitialize()
{
	if (ULevelEncounterSubsystem* Encounters = GetWorld() ? GetWorld()->GetSubsystem<ULevelEncounterSubsystem>() : nullptr)
	{
		Encounters->OnHostileContact.Remove(ContactHandle);
	}
	Super::Deinitialize();
}

bool UFirefightSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UFirefightSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UFirefightSubsystem, STATGROUP_Tickables);
}

const FFirefightConfig& UFirefightSubsystem::GetConfig() const
{
	static const FFirefightConfig Defaults;
	const UHordeSubsystem* Horde = GetWorld() ? GetWorld()->GetSubsystem<UHordeSubsystem>() : nullptr;
	return Horde ? Horde->GetConfig().Firefight : Defaults;
}

int32 UFirefightSubsystem::GetThreshold() const
{
	const UHordeSubsystem* Horde = GetWorld() ? GetWorld()->GetSubsystem<UHordeSubsystem>() : nullptr;
	return Horde ? FMath::Max(1, Horde->GetConfig().AfterFirefights) : FirefightRules::DefaultAfterFirefights;
}

void UFirefightSubsystem::ResetForTest()
{
	Encounter = FFirefightEncounter();
	Streak = FFirefightStreak();
	EncounterCount = 0;
	LastEnd = EFirefightEnd::None;
	Engaged.Reset();
	Groups.Reset();
	ContactGroups.Reset();
	SourceInfo.Reset();
	SlipPastCount = 0;
}

void UFirefightSubsystem::PostHQ(const FString& Line) const
{
	if (UGameMessageSubsystem* Messages = GetWorld() ? GetWorld()->GetSubsystem<UGameMessageSubsystem>() : nullptr)
	{
		Messages->PostMessage(FText::FromString(TEXT("HQ")), FText::FromString(Line));
	}
}

bool UFirefightSubsystem::IsTrackedSource(const AActor* Source) const
{
	if (const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Source))
	{
		const ULevelEncounterSubsystem* Encounters = GetWorld()->GetSubsystem<ULevelEncounterSubsystem>();
		return Encounters && Encounters->IsLevelEnemy(Enemy) && !Enemy->bKnowsSquadPosition;
	}
	const AOperativeCharacter* Fighter = Cast<AOperativeCharacter>(Source);
	return Fighter && !Fighter->IsPlayerTeam();
}

UFirefightSubsystem::FSourceInfo UFirefightSubsystem::DescribeSource(const AActor* Source)
{
	if (const FSourceInfo* Known = SourceInfo.Find(Source))
	{
		return *Known;
	}
	FSourceInfo Info;
	const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Source);
	const UCombatPressureSubsystem* Pressure = GetWorld()->GetSubsystem<UCombatPressureSubsystem>();
	const FPressureSpawnRecord* Record = Enemy && Pressure
		? Pressure->GetSpawnRecords().FindByPredicate([Enemy](const FPressureSpawnRecord& Each) { return Each.Enemy.Get() == Enemy; })
		: nullptr;
	if (Record)
	{
		Info.Group = Record->Group; // the pressure's patrol / ambush group (> 0)
		Info.bAmbusher = Record->Kind != EPressureSpawnKind::Patrol;
	}
	else
	{
		// A placed patrol: its escort leader's group; a PMC fighter / a lone enemy: its own.
		const AActor* Leader = Enemy && Enemy->GetEscortLeader() ? Enemy->GetEscortLeader() : Source;
		Info.Group = -static_cast<int32>(Leader->GetUniqueID()) - 1;
	}
	SourceInfo.Add(Source, Info);
	return Info;
}

void UFirefightSubsystem::HandleHostileContact(EAmbushTrigger Trigger, AActor* Source)
{
	UWorld* World = GetWorld();
	if (!World || !Source || !IsTrackedSource(Source))
	{
		return;
	}
	const FSourceInfo Info = DescribeSource(Source);
	const UHealthComponent* Health = Cast<AEnemyCharacter>(Source) ? Cast<AEnemyCharacter>(Source)->GetHealthComponent()
		: Source->FindComponentByClass<UHealthComponent>();
	const bool bAlive = !Health || Health->IsAlive();
	const bool bHostileOperative = Source->IsA<AOperativeCharacter>();
	TOptional<EFirefightEvent> Event;
	switch (Trigger)
	{
	case EAmbushTrigger::PatrolDetection:
		// The lethal hit breaks the patrol of the body too (HandleDamaged -> BreakPatrol): a dead enemy noticed nothing (a silent kill).
		if (bAlive)
		{
			Event = Info.bAmbusher ? EFirefightEvent::AmbushSprung : EFirefightEvent::EnemyAware;
		}
		break;
	case EAmbushTrigger::EnemyAttack:
		Event = Info.bAmbusher ? EFirefightEvent::AmbushSprung : EFirefightEvent::EnemyAttack;
		break;
	case EAmbushTrigger::SquadAutoFire:
		// A PMC fighter's opening shot names himself as the source (OperativeCharacterCombat): his attack.
		Event = bHostileOperative ? EFirefightEvent::EnemyAttack : EFirefightEvent::SquadShot;
		break;
	case EAmbushTrigger::AttackOrder:
	case EAmbushTrigger::EnemyDamaged:
	default:
		Event = EFirefightEvent::SquadShot;
		break;
	}
	ContactGroups.Add(Info.Group);
	if (!Event.IsSet())
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	if (!Encounter.bActive)
	{
		++EncounterCount;
		Engaged.Reset();
		UE_LOG(LogCodexTactics, Display, TEXT("[Firefight] encounter %d: contact (%s by %s, group %d%s)"), EncounterCount,
			*UEnum::GetValueAsString(Trigger), *Source->GetName(), Info.Group, Info.bAmbusher ? TEXT(", ambush") : TEXT(""));
	}
	if (!Engaged.Contains(Source))
	{
		Engaged.Add(Source);
	}
	if (FirefightRules::ApplyEvent(Encounter, Event.GetValue(), Now))
	{
		CountFirefight(Encounter.bAmbushSprung ? TEXT("an ambush sprung on the squad") : TEXT("contact + shots exchanged"));
	}
}

void UFirefightSubsystem::CountFirefight(const TCHAR* Why)
{
	UHordeSubsystem* Horde = GetWorld()->GetSubsystem<UHordeSubsystem>();
	const int32 Threshold = GetThreshold();
	const bool bHordeOn = Horde && Horde->GetConfig().bEnabled;
	const bool bDue = Streak.AddFirefight(Threshold);
	UE_LOG(LogCodexTactics, Display, TEXT("[Firefight] firefight %d on the level (encounter %d, %s): %d in a row of %d%s"), Streak.Total, EncounterCount,
		Why, bDue ? Threshold : Streak.Count, Threshold, bDue ? (bHordeOn ? TEXT(" -> the HORDE") : TEXT(" (the horde is off on this level)")) : TEXT(""));
	if (bDue)
	{
		if (bHordeOn)
		{
			Horde->RequestFirefightHorde();
		}
		return;
	}
	const int32 Remaining = Streak.Remaining(Threshold);
	if (bHordeOn && FirefightRules::ShouldHint(Remaining, GetConfig()))
	{
		PostHQ(Remaining == 1
			? FString(TEXT("Your shooting carries for kilometres. One more firefight and the horde will come - slip past the patrols."))
			: FString::Printf(TEXT("Your shooting carries for kilometres. %d more firefights and the horde will come - slip past the patrols."), Remaining));
	}
}

void UFirefightSubsystem::EndEncounter(EFirefightEnd Why, double Now)
{
	LastEnd = Why;
	UE_LOG(LogCodexTactics, Display, TEXT("[Firefight] encounter %d over after %.1f s (%s)%s"), EncounterCount, Now - Encounter.StartTime,
		Why == EFirefightEnd::AllDown ? TEXT("every engaged enemy down") : TEXT("contact lost"),
		Encounter.bCounted ? TEXT("") : TEXT(": no firefight (no shots exchanged with an aware enemy)"));
	Encounter = FFirefightEncounter();
	Engaged.Reset();
}

void UFirefightSubsystem::UpdateEncounter(double Now)
{
	if (!Encounter.bActive)
	{
		return;
	}
	const FFirefightConfig& Config = GetConfig();
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	const TArray<AOperativeCharacter*> Members = Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>();
	int32 Alive = 0;
	bool bHunting = false;
	for (const TWeakObjectPtr<AActor>& Weak : Engaged)
	{
		const AActor* Actor = Weak.Get();
		if (!Actor || Actor->IsActorBeingDestroyed())
		{
			continue;
		}
		const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Actor);
		const UHealthComponent* Health = Enemy ? Enemy->GetHealthComponent() : Actor->FindComponentByClass<UHealthComponent>();
		if ((Enemy && Enemy->IsDying()) || (Health && !Health->IsAlive()))
		{
			continue;
		}
		++Alive;
		// Still hunting the squad nearby (off its patrol): the contact holds even between two shots.
		if (bHunting || (Enemy && Enemy->IsOnPatrol()))
		{
			continue;
		}
		for (const AOperativeCharacter* Member : Members)
		{
			if (Member && Member->HealthComponent && Member->HealthComponent->IsAlive()
				&& FVector::Dist(Member->GetActorLocation(), Actor->GetActorLocation()) <= Config.ContactRangeCm)
			{
				bHunting = true;
				break;
			}
		}
	}
	if (bHunting)
	{
		FirefightRules::KeepContact(Encounter, Now);
	}
	const EFirefightEnd End = FirefightRules::CheckEnd(Encounter, Alive, Now, Config);
	if (End != EFirefightEnd::None)
	{
		EndEncounter(End, Now);
	}
}

void UFirefightSubsystem::UpdateGroups()
{
	UWorld* World = GetWorld();
	const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
	const ULevelEncounterSubsystem* Encounters = World->GetSubsystem<ULevelEncounterSubsystem>();
	if (!Squad || !Encounters)
	{
		return;
	}
	TArray<FVector, TInlineAllocator<8>> Squaddies;
	for (const AOperativeCharacter* Member : Squad->GetMembers())
	{
		if (Member && Member->HealthComponent && Member->HealthComponent->IsAlive())
		{
			Squaddies.Add(Member->GetActorLocation());
		}
	}
	struct FPass
	{
		float ClosestCm = TNumericLimits<float>::Max();
		float LargestRangeCm = 0.f;
		bool bEngaged = false;
	};
	TMap<int32, FPass> Pass;
	for (AEnemyCharacter* Enemy : UEnemyRegistrySubsystem::GetEnemyCharacters(World))
	{
		if (!Enemy || Enemy->IsDying() || Enemy->bKnowsSquadPosition || !Encounters->IsLevelEnemy(Enemy)
			|| !Enemy->GetHealthComponent() || !Enemy->GetHealthComponent()->IsAlive())
		{
			continue;
		}
		const FSourceInfo Info = DescribeSource(Enemy);
		FPass& Entry = Pass.FindOrAdd(Info.Group);
		Entry.bEngaged |= Enemy->HasBrokenPatrol();
		Entry.LargestRangeCm = FMath::Max(Entry.LargestRangeCm, Enemy->GetVisionConeRange());
		for (const FVector& Point : Squaddies)
		{
			Entry.ClosestCm = FMath::Min(Entry.ClosestCm, static_cast<float>(FVector::Dist2D(Point, Enemy->GetActorLocation())));
		}
	}
	const FFirefightConfig& Config = GetConfig();
	for (TPair<int32, FTrackedGroup>& Group : Groups)
	{
		Group.Value.bSeen = false;
	}
	for (const TPair<int32, FPass>& Entry : Pass)
	{
		FTrackedGroup& Group = Groups.FindOrAdd(Entry.Key);
		Group.bSeen = true;
		const float Reach = FirefightRules::GroupReach(Entry.Value.LargestRangeCm, Config);
		const ESlipVerdict Verdict = FirefightRules::UpdateGroup(Group.State, Entry.Value.ClosestCm, Reach,
			Entry.Value.bEngaged || ContactGroups.Contains(Entry.Key), false, Config);
		if (Verdict == ESlipVerdict::EnteredReach)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[Firefight] the squad is within the detection reach of group %d (%.0f m < %.0f m)"), Entry.Key,
				Entry.Value.ClosestCm / 100.f, Reach / 100.f);
		}
		else if (Verdict == ESlipVerdict::SlippedPast)
		{
			++SlipPastCount;
			const bool bReset = Config.bSlipPastResets && Streak.SlipPast();
			UE_LOG(LogCodexTactics, Display, TEXT("[Firefight] slipped past group %d undetected (now %.0f m > %.0f m x %.2f): %s"), Entry.Key,
				Entry.Value.ClosestCm / 100.f, Reach / 100.f, Config.SlipExitFactor,
				bReset ? TEXT("the streak resets to 0") : Config.bSlipPastResets ? TEXT("no streak to reset") : TEXT("streak kept (reset off)"));
			if (bReset)
			{
				PostHQ(TEXT("Good - they never saw you. Keep it quiet."));
			}
		}
	}
	// Groups gone since the last pass (dead: engaged by the squad's fire; recycled / despawned: gone while maybe in reach).
	for (auto It = Groups.CreateIterator(); It; ++It)
	{
		if (It->Value.bSeen)
		{
			continue;
		}
		const ESlipVerdict Verdict = FirefightRules::UpdateGroup(It->Value.State, TNumericLimits<float>::Max(), 0.f, ContactGroups.Contains(It->Key), true, Config);
		if (Verdict == ESlipVerdict::SlippedPast)
		{
			++SlipPastCount;
			const bool bReset = Config.bSlipPastResets && Streak.SlipPast();
			UE_LOG(LogCodexTactics, Display, TEXT("[Firefight] group %d went away undetected while the squad was in its reach: %s"), It->Key,
				bReset ? TEXT("the streak resets to 0") : TEXT("no reset"));
		}
		It.RemoveCurrent();
	}
	ContactGroups.Reset();
	// Forget the descriptions of destroyed actors.
	for (auto It = SourceInfo.CreateIterator(); It; ++It)
	{
		if (!It->Key.IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

void UFirefightSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World || !World->HasBegunPlay())
	{
		return;
	}
	UpdateEncounter(World->GetTimeSeconds());
	GroupTimer -= DeltaTime;
	if (GroupTimer <= 0.f)
	{
		GroupTimer = FirefightTuning::GroupPassSeconds;
		UpdateGroups();
	}
}
