#pragma once

#include "CoreMinimal.h"
#include "AI/PmcCommanderRules.h"
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
	int32 Heals = 0;
	int32 Relocations = 0;
	int32 Advances = 0;
	int32 Covers = 0;
	int32 FriendlyFireHits = 0;
	int32 StuckEvents = 0;
	int32 OrdersIssued = 0;
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
	/** A living hostile fighter exists (the skirmish goes on while one does). */
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
	};

	struct FTeam
	{
		TArray<TWeakObjectPtr<AOperativeCharacter>> Members;
		TMap<TWeakObjectPtr<AActor>, FKnownTarget> Known;
		TMap<TWeakObjectPtr<AOperativeCharacter>, FFighterState> Fighters;
		FPmcWeights Weights;
		bool bCommanded = false;
		float DecisionTimer = 0.f;
		bool bEverContact = false;
		double LastContactTime = -1e9;
		FPmcTeamStats Stats;
		TOptional<FVector> AssaultPoint;
	};

	FTeam& TeamOf(int32 TeamId);
	const FTeam* FindTeam(int32 TeamId) const;
	TArray<AOperativeCharacter*> MembersOf(int32 TeamId) const;
	void UpdateSight();
	void UpdateTeamRunning(int32 TeamId, FTeam& Team, float DeltaTime);
	void Decide(int32 TeamId, FTeam& Team);
	FPmcFighterView BuildFighterView(int32 TeamId, const FTeam& Team, AOperativeCharacter& Fighter, FFighterState& State, const TArray<AOperativeCharacter*>& Mates,
		FCoverSlot& OutCover, float& OutCoverQuality, AActor*& OutThreatActor) const;
	bool IsOrderRunning(const AOperativeCharacter& Fighter, const FFighterState& State) const;
	bool ExecuteOrder(int32 TeamId, FTeam& Team, AOperativeCharacter& Fighter, FFighterState& State, EPmcOrder Order, const FPmcFighterView& View,
		AActor* ThreatActor, const FCoverSlot& Cover, const TArray<AOperativeCharacter*>& Mates);
	/** The best cover slot against Threat within RadiusCm (rays round him + spots up to Weights.HighGroundMaxLevels floors up), high ground favoured. */
	bool FindCoverNear(const AOperativeCharacter& Fighter, const FVector& Threat, float RadiusCm, const FVector* Exclude, const FPmcWeights& Weights,
		FCoverSlot& OutSlot, float& OutQuality) const;
	bool ProjectToNav(const FVector& Point, FVector& OutPoint) const;
	bool IsExposedTo(int32 TeamId, const AOperativeCharacter& Fighter) const;
	void HandleWeaponFired(AOperativeCharacter* Shooter, AActor* Target, bool bHit);

	TMap<int32, FTeam> Teams;
	float SightTimer = 0.f;
	/** Fighters whose OnWeaponFiredNative this subsystem listens to. */
	TSet<TWeakObjectPtr<AOperativeCharacter>> Listened;
};
