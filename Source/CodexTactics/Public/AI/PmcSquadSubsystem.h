#pragma once

#include "CoreMinimal.h"
#include "AI/PmcCommanderRules.h"
#include "AI/PmcEncounterRules.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tactics/CoverTypes.h"
#include "PmcSquadSubsystem.generated.h"

class AActor;
class AOperativeCharacter;

/** Team ids of the operatives (AOperativeCharacter::TeamId): 0 = the player's squad, 1.. = hostile PMC squads (hostile to every other id). */
namespace PmcTeam
{
	inline constexpr int32 Player = 0;
	inline bool AreHostile(int32 A, int32 B) { return A != B; }
}

/** What a hostile squad did in a fight (the arena JSON, smokes). */
struct CODEXTACTICS_API FPmcTeamStats
{
	int32 Kills = 0;
	int32 FlanksAttempted = 0;
	int32 FlanksSucceeded = 0;
	int32 Suppressions = 0;
	float SuppressionSeconds = 0.f;
	int32 Ambushes = 0;
	int32 Retreats = 0;
	/** Retreats that were the "too close for his weapon" standoff (2026-11-04; counted in Retreats too). */
	int32 Standoffs = 0;
	int32 Heals = 0;
	int32 Relocations = 0;
	int32 Advances = 0;
	int32 Covers = 0;
	int32 FriendlyFireHits = 0;
	int32 StuckEvents = 0;
	int32 OrdersIssued = 0;
	/** Objective orders taken (task #53: walks onto / back to the round's point). */
	int32 ObjectiveMoves = 0;
};

/**
 * The hostile PMC squads (user request 2026-11-03: "a real fight between humanoid squads"; docs/systems/pmc-squad-ai.md; UE-only).
 * One body: a PMC fighter IS an AOperativeCharacter (the operative Blueprint) with TeamId != 0, so every operative skill serves him;
 * this subsystem is the HOSTILE side's squad: the registry of fighters per team (the player's USquadSubsystem never sees them), the
 * TEAM SIGHT (what each team's men see of the other teams' men, by the same eye / profile / FOV / height rules the squad sight uses:
 * UTacticalSightSubsystem::CanOperativeSee - no cheating), the target lists the fighters shoot from, and the UTILITY-AI COMMANDER
 * (PmcCommanderRules): every DecisionInterval it scores the catalog orders per fighter and runs the best one through the shared
 * SquadOrderCatalog. The player's team (0) can be commanded too (the arena's "player-ai" opponent).
 */
UCLASS()
class CODEXTACTICS_API UPmcSquadSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	static UPmcSquadSubsystem* Get(const UWorld* World);

	// --- Registry (AOperativeCharacter::BeginPlay / EndPlay for TeamId != 0) ---
	void RegisterOperative(AOperativeCharacter* Operative);
	void UnregisterOperative(AOperativeCharacter* Operative);
	/** The living members of a team (team 0: the player's squad). */
	TArray<AOperativeCharacter*> GetTeamMembers(int32 TeamId) const;
	/** Every registered fighter of a hostile team (alive or down). */
	TArray<AOperativeCharacter*> GetHostileOperatives() const;
	/**
	 * A living ENGAGED hostile fighter exists (the skirmish goes on while one does): his team has met an enemy, or the player's squad
	 * has seen him. A squad still waiting unseen in its posture (a level encounter, 2026-11-03) never holds a finished fight open.
	 */
	static bool HasLiveHostileOperatives(const UWorld* World);
	/** AOperativeCharacter::HandleDied of a hostile fighter: the kill statistics and the fight's end check. */
	void NotifyOperativeDied(AOperativeCharacter* Operative, const FString& AttackerSource);

	// --- Team sight / targets ---
	/** Team TeamId sees Target now (seen by one of its men, within touch reveal, or demasked); true while the sight rules are off. */
	bool IsVisibleToTeam(int32 TeamId, const AActor* Target) const;
	/** Where TeamId last knew Target to be; false when it never saw it. */
	bool GetTeamBelief(int32 TeamId, const AActor* Target, FVector& OutLocation, float* OutSecondsSince = nullptr) const;
	/** What Shooter may fire at now: the other teams' men his team sees + (hostile teams) the monsters hunting a mate within SelfDefenceCm. */
	TArray<AActor*> GetTargetsFor(const AOperativeCharacter& Shooter) const;
	/** Actor is a legitimate target of Shooter's team (another team's operative; a monster the team defends itself from; the player's team: any "Enemy"). */
	bool IsHostileTo(const AOperativeCharacter& Shooter, const AActor* Actor) const;
	/** Runs the sight update now (smokes; the tick runs one every SightInterval). */
	void RefreshSight();
	/**
	 * Bug #43 (UnderFireRules): Shooter fired a round at Victim - Victim's team notices the shooter (his muzzle flash at Flash) for
	 * SightRules::DemaskSeconds whatever the range (the 60 m gunfire hearing is for the other teams). Honest sight (2026-11-08): he is SEEN
	 * only while an eye of the team has a clear line to him or to that flash; else known (his spot), not a target.
	 */
	void NotifyShotAt(const AActor* Shooter, const AOperativeCharacter* Victim, const FVector* Flash = nullptr);

	// --- Commander ---
	void SetTeamCommanded(int32 TeamId, bool bCommanded, const FPmcWeights* Weights = nullptr);
	bool IsTeamCommanded(int32 TeamId) const;
	void SetTeamWeights(int32 TeamId, const FPmcWeights& Weights);
	const FPmcWeights* GetTeamWeights(int32 TeamId) const;
	/** Runs the decisions of a team now (smokes). */
	void RunDecisions(int32 TeamId);
	const FPmcTeamStats* GetTeamStats(int32 TeamId) const;
	/** Forgets a team's intel, order states and statistics (an arena round). */
	void ResetTeam(int32 TeamId);
	/** The order a fighter runs and where it sends him (smokes, the overhead debug). */
	EPmcOrder GetFighterOrder(const AOperativeCharacter* Fighter) const;
	FVector GetFighterGoal(const AOperativeCharacter* Fighter) const;
	/** The teams that exist (registered fighters or commanded). */
	TArray<int32> GetTeamIds() const;
	/**
	 * An assault objective (the arena's attacking side): with no threat known the commander treats Point as a remembered threat - the
	 * fighters advance on it by bounding overwatch instead of holding in ambush. nullptr clears it.
	 */
	void SetTeamAssault(int32 TeamId, const FVector* Point);
	/** The team has met the enemy in this fight. */
	bool HasTeamContact(int32 TeamId) const;
	/**
	 * The round objective (the arena, task #53): the team's role (attacker / defender), the point and its hold radius - the commander's
	 * Objective order and the objective considerations (PmcCommanderRules::ObjectivePositionFactor) read it. Role None clears it.
	 */
	void SetTeamObjective(int32 TeamId, EPmcRole Role, const FVector& Location, float RadiusCm);
	/** The objective's live state for the team's next decisions: the round's time fraction, the capture clock, an enemy on the point. */
	void UpdateTeamObjective(int32 TeamId, float TimeFrac, float HoldFrac, bool bEnemyOnPoint);

	// --- Encounter postures (level JSON "pmc_squads", CodexTactics.PmcSpawn; docs/systems/pmc-squad-ai.md 7) ---
	/**
	 * What the team does until its first contact: Defend = commanded, ambush round Anchor; Hunt = commanded with an assault point
	 * walked leg by leg (PmcEncounterRules::HuntSearchPoint: the player start, then radio intel near the squad); Patrol = NOT commanded,
	 * walks slowly between seeded points round Anchor. Contact (seen an enemy, or a man hurt) ends the posture: the commander runs the
	 * fight from then on. Weights = the commander's weights for the team.
	 */
	void SetTeamPosture(int32 TeamId, EPmcPosture Posture, const FVector& Anchor, const FVector& PlayerStart, int32 Seed, const FPmcWeights& Weights);
	/** The team's posture while it still runs (before contact); false once engaged or without one. */
	bool GetTeamPosture(int32 TeamId, EPmcPosture& OutPosture) const;
	/** The team is in the fight: met an enemy, or the player's squad has seen one of its men. */
	bool IsTeamEngaged(int32 TeamId) const;
	/** Seconds between a hunting squad's search legs and a patrol's roam re-plans. */
	static constexpr float HuntLegSeconds = 30.f;
	static constexpr float PatrolLegSeconds = 25.f;
	static constexpr float PatrolRadiusCm = 1500.f;
	static constexpr float HuntJitterCm = 1500.f;

	// --- Spawning ---
	/**
	 * Spawns Count PMC fighters of TeamId round Centre facing Facing (a line 150 cm apart): the operative Blueprint through
	 * ACodexTacticsGameMode::SpawnOperative with the PmcOperatorRules identity (name, look, weapon, stats; seeded), Aggressive posture.
	 */
	TArray<AOperativeCharacter*> SpawnSquad(int32 TeamId, const FVector& Centre, const FRotator& Facing, int32 Count, int32 Seed);
	AOperativeCharacter* SpawnFighter(int32 TeamId, const FVector& Location, const FRotator& Facing, int32 Seed, int32 Index);
	/** Destroys every registered fighter of a team (an arena round's end). */
	void DestroyTeam(int32 TeamId);

	static constexpr float SightInterval = 0.2f;

	/** A fighter took an order (team, fighter, the order, its score): the arena's track log. */
	DECLARE_MULTICAST_DELEGATE_FourParams(FOnFighterOrder, int32, AOperativeCharacter*, EPmcOrder, float);
	FOnFighterOrder OnFighterOrder;

private:
	struct FKnownTarget
	{
		TWeakObjectPtr<AActor> Actor;
		FVector LastKnown = FVector::ZeroVector;
		double LastSeenTime = -1.0;
		bool bSeenNow = false;
	};

	struct FFighterState
	{
		EPmcOrder Order = EPmcOrder::Hold;
		double OrderSince = 0.0;
		FVector Goal = FVector::ZeroVector;
		TWeakObjectPtr<AActor> Target;
		TWeakObjectPtr<AOperativeCharacter> Patient;
		FCoverSlot Slot;
		/** Where he stands and since when (exposure / stuck tracking). */
		FVector Spot = FVector::ZeroVector;
		double SpotSince = 0.0;
		double ExposedSince = -1.0;
		FVector ProgressPoint = FVector::ZeroVector;
		double ProgressTime = 0.0;
		bool bFlankCounted = false;
		bool bWasSuppressing = false;
		/** An ambusher holding his fire (the Passive posture, PmcCommanderRules::ShouldHoldFire; user 2026-11-07). */
		bool bHoldingFire = false;
	};

	struct FTeam
	{
		TArray<TWeakObjectPtr<AOperativeCharacter>> Members;
		TMap<TWeakObjectPtr<AActor>, FKnownTarget> Known;
		/** NotifyShotAt: a shooter who fired at one of the team, seen until this time whatever the range (bug #43). */
		TMap<TWeakObjectPtr<AActor>, double> ShotRevealUntil;
		/** Where that shooter's muzzle flash was (fix/pmc-fire-through-wall: seen only with a clear line to it, or to him). */
		TMap<TWeakObjectPtr<AActor>, FVector> ShotRevealFlash;
		TMap<TWeakObjectPtr<AOperativeCharacter>, FFighterState> Fighters;
		FPmcWeights Weights;
		bool bCommanded = false;
		float DecisionTimer = 0.f;
		bool bEverContact = false;
		double LastContactTime = -1e9;
		/** The fight's first contact (-1: none yet) and whether a shot was exchanged with an enemy (target attention, 2026-11-07). */
		double FirstContactTime = -1.0;
		bool bShotsExchanged = false;
		FPmcTeamStats Stats;
		TOptional<FVector> AssaultPoint;
		/** The round objective (the arena, task #53; DistanceCm is filled per fighter). */
		TOptional<FPmcObjectiveView> Objective;
		/** The encounter posture (before contact). */
		TOptional<EPmcPosture> Posture;
		FVector PostureAnchor = FVector::ZeroVector;
		FVector PlayerStart = FVector::ZeroVector;
		int32 PostureSeed = 0;
		int32 PostureLeg = 0;
		float PostureTimer = 0.f;
	};

	FTeam& TeamOf(int32 TeamId);
	const FTeam* FindTeam(int32 TeamId) const;
	TArray<AOperativeCharacter*> MembersOf(int32 TeamId) const;
	void UpdateSight();
	void UpdateTeamRunning(int32 TeamId, FTeam& Team, float DeltaTime);
	/** Runs the encounter posture of a team before its contact; ends it on contact (the commander takes over). */
	void UpdatePosture(int32 TeamId, FTeam& Team, float DeltaTime);
	void Decide(int32 TeamId, FTeam& Team);
	FPmcFighterView BuildFighterView(int32 TeamId, const FTeam& Team, AOperativeCharacter& Fighter, FFighterState& State, const TArray<AOperativeCharacter*>& Mates,
		FCoverSlot& OutCover, float& OutCoverQuality, AActor*& OutThreatActor) const;
	bool IsOrderRunning(const AOperativeCharacter& Fighter, const FFighterState& State) const;
	bool ExecuteOrder(int32 TeamId, FTeam& Team, AOperativeCharacter& Fighter, FFighterState& State, EPmcOrder Order, const FPmcFighterView& View,
		AActor* ThreatActor, const FCoverSlot& Cover, const TArray<AOperativeCharacter*>& Mates);
	/** The best cover slot against Threat within RadiusCm (rays round him + spots up to Weights.HighGroundMaxLevels floors up), high ground favoured. */
	/**
	 * The best cover slot round Fighter against Threat (quality x nearness x height x - 2026-11-04 - the slot's distance to the threat in his
	 * weapon's Band (PmcCommanderRules::BandFactor, weighted by band_position) x a corner bonus); OutBandFactor = the chosen slot's band factor.
	 */
	bool FindCoverNear(const AOperativeCharacter& Fighter, const FVector& Threat, float RadiusCm, const FVector* Exclude, const FPmcWeights& Weights,
		FCoverSlot& OutSlot, float& OutQuality, const FPmcEngagementBand& Band = FPmcEngagementBand(), float* OutBandFactor = nullptr) const;
	bool ProjectToNav(const FVector& Point, FVector& OutPoint) const;
	bool IsExposedTo(int32 TeamId, const AOperativeCharacter& Fighter) const;
	/** The enemies the team sees now: their positions and facings as seen (target attention, 2026-11-07). */
	void SeenWatchers(const FTeam& Team, TArray<FVector2D>& OutPositions, TArray<float>& OutYaws) const;
	void HandleWeaponFired(AOperativeCharacter* Shooter, AActor* Target, bool bHit);

	TMap<int32, FTeam> Teams;
	float SightTimer = 0.f;
	/** Fighters whose OnWeaponFiredNative this subsystem listens to. */
	TSet<TWeakObjectPtr<AOperativeCharacter>> Listened;
};
