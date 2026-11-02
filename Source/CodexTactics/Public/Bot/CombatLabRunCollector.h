#pragma once

#include "CoreMinimal.h"
#include "Bot/BotRouteRules.h"
#include "Characters/OperativeMovementRules.h"
#include "Formation/FormationTypes.h"

class FJsonObject;
class FBotRouteTracker;

/**
 * The combat lab's bot run report (game designer 2026-11-01, Training Ground "Combat lab" part C, docs/design/COMBAT_LAB.md section 7;
 * UE-only, no Godot reference): the metric bookkeeping of one bot run on an endless pressure level - contacts (patrol / ambush /
 * coordinated, the time until an operative looks at the enemy, ambushes that struck from an unwatched side, the formation at each
 * contact), formation changes by type, wall hugs, stance changes, damage (absorbed by the immortality cheat), rounds (spent or, with
 * infinite ammo, would-be spent), ladder climbs and knockdowns. Pure: UPlaytestBotSubsystem (PlaytestBotRouteMode.cpp) samples the world
 * every 0.1 s into FCombatLabSample; tested in CodexTactics.Bot.CombatLab.*. The Training Ground parses the record
 * (Tools/TrainingGround/src/combatLab/report.ts, schema codex.combatlab_run/1).
 */

/** What kind of pressure event a contact was (UCombatPressureSubsystem's spawn kinds; a coordinated plan is one contact). */
enum class ECombatLabContactKind : uint8
{
	Patrol,
	Ambush,
	Coordinated
};

/** What opened a contact (the first that held). */
enum class ECombatLabContactTrigger : uint8
{
	/** An operative had a line of sight to a member within the contact range. */
	Seen,
	/** A member left its patrol / guard post (it perceived the squad, was shot at, ...). */
	Engaged,
	/** An ambush struck (UCombatPressureSubsystem: its side unwatched, or spotted / shot at first). */
	Sprung
};

/** One pressure group (a coordinated plan counts as one) as it stands at a sample. */
struct CODEXTACTICS_API FCombatLabGroupSample
{
	/** Group id (a plan: 100000 + its id). */
	int32 Id = 0;
	ECombatLabContactKind Kind = ECombatLabContactKind::Patrol;
	/** Living members. */
	int32 Alive = 0;
	/** The first living member's position. */
	FVector2D Position = FVector2D::ZeroVector;
	bool bSeen = false;
	bool bEngaged = false;
	bool bSprung = false;
	/** A look of the squad covers a living member (AmbushWatchRules: body, aim or formation sector look, +- the watch half angle). */
	bool bWatched = false;
};

/** One operative at a sample. */
struct CODEXTACTICS_API FCombatLabMemberSample
{
	FString Name;
	bool bAlive = true;
	EOperativeStance Stance = EOperativeStance::Standing;
	bool bKnockedDown = false;
	float Health = 0.f;
	/** UHealthComponent::GetCheatBlockedDamage (cumulative). */
	float AbsorbedDamage = 0.f;
	/** AOperativeCharacter::GetRoundsSpent (cumulative; counts the infinite-ammo rounds too). */
	int32 RoundsSpent = 0;
	int32 ClimbsUp = 0;
	int32 ClimbsDown = 0;
};

/** The world at one sample. */
struct CODEXTACTICS_API FCombatLabSample
{
	float Time = 0.f;
	/** CombatLabRules::FormationLabel ("" = no formation this sample: Solo, the formation off). */
	FString Formation;
	/** USquadSubsystem::IsSquadAtWall. */
	bool bAtWall = false;
	/** A fight runs (the skirmish). */
	bool bInFight = false;
	/** The bot has ordered the squad to cover at a wall for the fight (decision 5, 2026-11-01 night; BotCoverRules) and holds it. */
	bool bCoverOrdered = false;
	TArray<FCombatLabMemberSample> Members;
	TArray<FCombatLabGroupSample> Groups;
};

/** One contact of the run. */
struct CODEXTACTICS_API FCombatLabContact
{
	int32 Id = 0;
	ECombatLabContactKind Kind = ECombatLabContactKind::Patrol;
	ECombatLabContactTrigger Trigger = ECombatLabContactTrigger::Seen;
	float Time = 0.f;
	FVector2D Position = FVector2D::ZeroVector;
	/** The formation the squad was in (the last real one when "" at the contact). */
	FString Formation;
	bool bWatchedAtStart = false;
	/** Seconds from the contact to the first sample an operative's look covered it; < 0 = never. */
	float ReactionSeconds = -1.f;
	bool bClosed = false;

	/** An ambush (single or coordinated) that struck while nobody looked its way. */
	bool IsUnwatchedStrike() const { return Kind != ECombatLabContactKind::Patrol && Trigger == ECombatLabContactTrigger::Sprung && !bWatchedAtStart; }
};

/** One operative's totals. */
struct CODEXTACTICS_API FCombatLabMemberStats
{
	FString Name;
	int32 StanceChanges = 0;
	/** Changes INTO each stance. */
	int32 ToStanding = 0;
	int32 ToCrouching = 0;
	int32 ToProne = 0;
	float DamageTaken = 0.f;
	float DamageAbsorbed = 0.f;
	int32 Rounds = 0;
	int32 ClimbsUp = 0;
	int32 ClimbsDown = 0;
	int32 Knockdowns = 0;
};

/** Run identity for the record. */
struct CODEXTACTICS_API FCombatLabRunMeta
{
	FString Result;
	int32 Run = 1;
	int32 BotSeed = 1;
	int32 PressureSeed = 0;
	FString LevelId;
	FString Map;
	FString TimestampUtc;
	float PlannedSeconds = 0.f;
	float RealSeconds = 0.f;
	bool bImmortal = false;
	bool bInfiniteAmmo = false;
	/** UCombatPressureSubsystem totals: timer events fired, enemies spawned, groups sprung by the watch rule, relocations. */
	int32 PressureEvents = 0;
	int32 PressureSpawned = 0;
	int32 PressureSprungByWatch = 0;
	int32 PressureRelocations = 0;
	/** Decisions 1-2 (2026-11-01 night): ambushes that struck after their hold limit; relocation runs spotted on the way; searches with no concealed route; ambushes turned into following patrols. */
	int32 PressureSprungByHold = 0;
	int32 PressureRunsSpotted = 0;
	int32 PressureNoRoute = 0;
	int32 PressureFollowers = 0;
	/** The route's drops through roof / floor holes (generated maps; empty elsewhere -> no "drops" in the record). */
	TArray<FBotRouteDrop> Drops;
};

class CODEXTACTICS_API FCombatLabRunCollector
{
public:
	/** A formation must hold this long before it counts as a change, s (the classifier flickers through a frame or two). */
	static constexpr float FormationSettleSeconds = 0.5f;
	/** The squad must have been off the wall this long before pressing to a wall again counts as a new hug, s. */
	static constexpr float WallRearmSeconds = 1.f;

	void Begin(float Time);
	void AddSample(const FCombatLabSample& Sample);
	void End(float Time);

	/** A contact with this group id is known (the bot skips its line-of-sight traces then). */
	bool HasContact(int32 Id) const { return ContactIndex.Contains(Id); }

	float GetDurationSeconds() const { return FMath::Max(0.f, EndTime - StartTime); }
	const TArray<FCombatLabContact>& GetContacts() const { return Contacts; }
	int32 CountContacts(ECombatLabContactKind Kind) const;
	int32 CountUnwatchedStrikes() const;
	/** Contacts nobody ever looked at before they were gone. */
	int32 CountNeverLooked() const;
	/** Mean / max reaction over the contacts somebody looked at (0 when none). */
	float GetMeanReaction() const;
	float GetMaxReaction() const;

	int32 GetFormationChanges() const { return FormationChanges; }
	/** Formations entered (a change into it; the first formation of the run counts once too). */
	const TMap<FString, int32>& GetFormationsEntered() const { return FormationsEntered; }
	const TMap<FString, float>& GetFormationSeconds() const { return FormationSeconds; }
	int32 GetWallHugs() const { return WallHugs; }
	/**
	 * Cover at a wall in a fight (decision 5): orders given, covers taken (the squad at the wall while the order holds; one per order) and the
	 * seconds spent there.
	 */
	int32 GetCoverOrders() const { return CoverOrders; }
	int32 GetCoverTaken() const { return CoverTaken; }
	float GetCoverSeconds() const { return CoverSeconds; }
	int32 GetStanceChanges() const;
	/** Stance changes per minute of the run. */
	float GetStanceChangesPerMinute() const;
	const TArray<FCombatLabMemberStats>& GetMembers() const { return Members; }
	float GetDamageTaken() const;
	float GetDamageAbsorbed() const;
	int32 GetRounds() const;
	int32 GetClimbsUp() const;
	int32 GetClimbsDown() const;
	int32 GetKnockdowns() const;
	int32 GetFights() const { return Fights; }
	float GetFightSeconds() const { return FightSeconds; }
	/** The formation in force now (the last real one). */
	const FString& GetFormation() const { return CurrentFormation; }

private:
	void SampleFormation(const FCombatLabSample& Sample, float Delta);
	void SampleContacts(const FCombatLabSample& Sample);
	void SampleMembers(const FCombatLabSample& Sample);

	float StartTime = 0.f;
	float EndTime = 0.f;
	float LastTime = 0.f;
	bool bHasSample = false;
	TArray<FCombatLabContact> Contacts;
	TMap<int32, int32> ContactIndex;
	FString CurrentFormation;
	FString PendingFormation;
	float PendingSince = 0.f;
	int32 FormationChanges = 0;
	TMap<FString, int32> FormationsEntered;
	TMap<FString, float> FormationSeconds;
	bool bWasAtWall = false;
	float OffWallSince = -1000.f;
	int32 WallHugs = 0;
	bool bWasCoverOrdered = false;
	bool bCoverTakenThisOrder = false;
	int32 CoverOrders = 0;
	int32 CoverTaken = 0;
	float CoverSeconds = 0.f;
	bool bWasInFight = false;
	int32 Fights = 0;
	float FightSeconds = 0.f;
	TArray<FCombatLabMemberStats> Members;
	TArray<FCombatLabMemberSample> LastMembers;
};

namespace CombatLabRules
{
	/** "patrol" | "ambush" | "coordinated". */
	CODEXTACTICS_API const TCHAR* KindName(ECombatLabContactKind Kind);
	/** "seen" | "engaged" | "sprung". */
	CODEXTACTICS_API const TCHAR* TriggerName(ECombatLabContactTrigger Trigger);
	/** "standing" | "crouching" | "prone". */
	CODEXTACTICS_API const TCHAR* StanceName(EOperativeStance Stance);

	/**
	 * The report's formation label: "" in Solo or with the formation off; "wall_layout" for the wall layout / a wall order; the diamond with
	 * three members "wedge", with two "pair"; otherwise the contract name (FormationNames::ToString: "column", "echelon_left", "custom", ...).
	 */
	CODEXTACTICS_API FString FormationLabel(bool bSolo, bool bFormationActive, bool bWallOrder, EFormationKind Kind, int32 Members);

	/** The run record (schema codex.combatlab_run/1, one line of the batch's runs.jsonl). */
	CODEXTACTICS_API TSharedRef<FJsonObject> MakeRunRecord(const FCombatLabRunMeta& Meta, const FBotRouteTracker& Route, const FCombatLabRunCollector& Collector,
		const FVector2D& LeaderNow);

	/** The record as one compact JSON line (no trailing newline). */
	CODEXTACTICS_API FString ToJsonLine(const TSharedRef<FJsonObject>& Record);
}
