#pragma once

#include "CoreMinimal.h"
#include "AI/PmcCommanderRules.h"
#include "AI/PmcDefenceRules.h"
#include "AI/PmcEncounterRules.h"
#include "AI/PmcTacticalRules.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tactics/CoverTypes.h"
#include "PmcSquadSubsystem.generated.h"

class AActor;
class AOperativeCharacter;
class UTacticalMapSubsystem;

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
	/** RpgStrike orders taken (the RPG gunner's ordered rocket, 2026-11-09). */
	int32 RpgStrikes = 0;
	/** fix/pmc-arena-stalemate (2026-11-10): orders that ended short of their goal / were not taken (cooldown), detours taken after
	 * RepathAfterSeconds without progress, running orders replaced by a clearly better one, decisions made while the team pushed. */
	int32 OrdersFailed = 0;
	int32 Detours = 0;
	int32 Preempts = 0;
	int32 PushDecisions = 0;
	/** TACTICAL_MAP P2 (2026-11-17): walks onto a tactical post (the Ambush order), and fall-backs turned off a chokepoint (not straight away). */
	int32 PostMoves = 0;
	int32 TacticalFallBacks = 0;
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
	 * LevelToleranceCm = the point's level (fix/pmc-onpoint-3d-detour: on / near it only within this height, FPmcObjectiveView).
	 */
	void SetTeamObjective(int32 TeamId, EPmcRole Role, const FVector& Location, float RadiusCm, float LevelToleranceCm = 150.f);
	/**
	 * G008: where the attackers come from. The defender arc is centred on the yaw from the objective toward this point.
	 * Unset: yaw 0, and the sectors are still deterministic. Call it before SetTeamObjective, or after (the posts are split again).
	 */
	void SetDefenceApproach(int32 TeamId, const FVector& AttackerSpawn);
	/**
	 * G008: the access a defender objective at this point would use (the tactical file, else the map's own levelgen layout).
	 * Does not log and does not change a live plan. The roofs scenario skips a site whose access is unset.
	 */
	FPmcDefenceSite ResolveDefenceSite(const FVector& Objective, float LevelToleranceCm);
	/** G008: living defenders' feet and the cover they hold, for the arena's 1 Hz sample. False when this team is not defending. */
	bool CopyDefenceSnapshot(int32 TeamId, FPmcDefenceSnapshot& Out) const;
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
	/**
	 * A hunting squad out of sight of the player's men this long after contact hunts again (PmcEncounterRules::ShouldResumeHunt), and a belief
	 * older than HuntBeliefFreshSeconds is no search point (the radio intel instead), s.
	 */
	static constexpr float HuntResumeSeconds = 15.f;
	static constexpr float HuntBeliefFreshSeconds = 20.f;

	// --- Spawning ---
	/**
	 * Spawns Count PMC fighters of TeamId round Centre facing Facing (a line 150 cm apart): the operative Blueprint through
	 * ACodexTacticsGameMode::SpawnOperative with the PmcOperatorRules identity (name, look, weapon, stats; seeded), Aggressive posture.
	 */
	TArray<AOperativeCharacter*> SpawnSquad(int32 TeamId, const FVector& Centre, const FRotator& Facing, int32 Count, int32 Seed);
	AOperativeCharacter* SpawnFighter(int32 TeamId, const FVector& Location, const FRotator& Facing, int32 Seed, int32 Index);
	/** Destroys every registered fighter of a team (an arena round's end). */
	void DestroyTeam(int32 TeamId);

	// --- The tactical map (docs/design/TACTICAL_MAP.md P2, designer 2026-11-17; PmcTacticalRules) ---
	/** The world's tactical map when it is loaded with items (UTacticalMapSubsystem); nullptr = no tactical file: the old behaviour. */
	const UTacticalMapSubsystem* GetTacticalMap() const;
	/** The squad's route as a navmesh path From -> To (its points; just [From, To] without a full path). */
	TArray<FVector> RouteBetween(const FVector& From, const FVector& To) const;
	/**
	 * The route the team's enemy walks (the player's squad / the arena's attackers): the posts, the cover slots facing it (route_cover) read
	 * it. Empty clears. Used only with a tactical map.
	 */
	void SetTeamRoute(int32 TeamId, const TArray<FVector>& Route);
	const TArray<FVector>& GetTeamRoute(int32 TeamId) const;
	/**
	 * The post candidates along Route from the tactical map (PmcTacticalRules): hiding spots biting the route (BestAmbushSpots per leg, the
	 * EXIT first), overwatch roof spots reaching it, windows covering it, the ladder-only roofs PickLadderOnlyRoofs(Seed) gives an enemy (the
	 * file's 0.65; OutLadderRoofsPicked = their indices), drop edges landing on it; each with its PMC-side danger. Ambush / window / drop /
	 * ladder-roof / overwatch posts lie within Weights.TacticalReachCm of the route (ladder roofs and overwatch roof spots 1.5 x) and
	 * MinFromStartCm or more from its start. Empty without a tactical map.
	 */
	TArray<FPmcPostCandidate> GatherPostCandidates(const TArray<FVector>& Route, int32 Seed, const FPmcWeights& Weights, float MinFromStartCm = 0.f,
		TArray<int32>* OutLadderRoofsPicked = nullptr) const;
	/**
	 * Spawns Count fighters ON their tactical posts along Route (PmcTacticalRules::PlanPlacement over GatherPostCandidates: the marksman on
	 * an overwatch roof, a man on a picked ladder-only roof, ambushers at chokepoint exits / riflemen at windows); a fighter without a post
	 * spawns in a line at Fallback. Only candidates on the navmesh at their own level are planned (KeepCandidatesOnNav); a planned post that
	 * still fails that projection is dropped (the man spawns on the line). The team's weights must be set first (SetTeamWeights). Without a
	 * tactical map: SpawnSquad(Fallback). OutPosts[i] = the post of the returned fighter i (invalid: none) - always the same length as the
	 * result: a fighter that fails to spawn leaves out both his entry and his post.
	 */
	TArray<AOperativeCharacter*> SpawnSquadTactical(int32 TeamId, const TArray<FVector>& Route, const FVector& Fallback, const FRotator& Facing, int32 Count,
		int32 Seed, TArray<FPmcTacticalPost>* OutPosts = nullptr, TArray<int32>* OutLadderRoofsPicked = nullptr);
	/** Gives the team's living fighters posts along Route (they walk there with the Ambush order before contact); the number posted. */
	int32 AssignTacticalPosts(int32 TeamId, const TArray<FVector>& Route, int32 Seed, float MinFromStartCm = 0.f);
	/** A fighter's tactical post (nullptr: none). */
	const FPmcTacticalPost* GetFighterPost(const AOperativeCharacter* Fighter) const;
	/** A man counts on his post within this (planar, his post's level). */
	static constexpr float PostArriveCm = 200.f;

	static constexpr float SightInterval = 0.2f;

	/** A fighter took an order (team, fighter, the order, its score, why it was picked): the arena's track log. */
	DECLARE_MULTICAST_DELEGATE_FiveParams(FOnFighterOrder, int32, AOperativeCharacter*, EPmcOrder, float, EPmcPickReason);
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
		/** The threat actor this fighter was reacting to when deciding orders (a new threat resets completed cooldowns). */
		TWeakObjectPtr<AActor> LastThreat;
		/** Recently fired-at visible target, retained until the next sight update. */
		TWeakObjectPtr<AActor> RecentFireTarget;
		/** Area suppression started automatically after losing that target. */
		TWeakObjectPtr<AActor> LostFireTarget;
		FVector LostFirePoint = FVector::ZeroVector;
		double LostFireUntil = -1.0;
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
		/** The RPG gunner's best rocket target this decision (BuildFighterView; the RpgStrike order fires at it). */
		TWeakObjectPtr<AActor> LauncherTarget;
		/** fix/pmc-arena-stalemate: a walk order was given and has not ended yet (ending short of its goal = failed). */
		bool bWalkPending = false;
		/** An RpgStrike was given and has not ended yet (its end starts a cooldown: no rocket order every decision). */
		bool bStrikePending = false;
		/** Detours taken on the running walk. */
		int32 Detours = 0;
		/**
		 * fix/pmc-onpoint-3d-detour: re-route steps tried on the running walk (found or not; PmcCommanderRules::NextDetourStep) and the side
		 * the Side step took (+1 left, -1 right, 0 none).
		 */
		int32 DetourAttempts = 0;
		int32 DetourSide = 0;
		/** Per-order world-clock expiries (PickCommitted's cooldown). */
		TMap<EPmcOrder, double> FailedUntil;
		/** TACTICAL_MAP P2: his post (the placement's / AssignTacticalPosts'); the Ambush order waits on it. */
		FPmcTacticalPost Post;
		/** The Ambush order faced / crouched him on his post (false while he still walks there). */
		bool bPostSettled = false;
		/** Walks onto the post that ended short of it (refused, or no closer than PostWalkMinProgressCm; three drop the post). */
		int32 PostWalks = 0;
		/** His distance to the post when the running post walk was issued (-1: no post walk running). */
		float PostWalkFromCm = -1.f;
		/** G008: his defence role and post. Reassigned only when the living set changes. */
		EPmcDefenceRole DefenceRole = EPmcDefenceRole::None;
		int32 DefenceSector = INDEX_NONE;
		FVector DefencePost = FVector::ZeroVector;
		float DefenceYaw = 0.f;
		bool bHasDefencePost = false;
		int32 DefenceSpotIndex = INDEX_NONE;
		/** World time he arrived on the defence post (-1: not there). The sector shift counts from this. */
		double DefenceArrivedAt = -1.0;
		/** Defence post walks that ended short or were refused (defence_post_walks of them drop the post). */
		int32 DefencePostWalks = 0;
		/** His distance to the defence post when the running walk there was issued (-1: none running). */
		float DefencePostWalkFromCm = -1.f;
		/** He already gave up one unreachable defence post; a second give-up holds where he stands. */
		bool bDefencePostDropped = false;
		/** The Ambush order faced / crouched him on his defence post (false while he still walks there). */
		bool bDefenceSettled = false;
		/** World time of his last sector shift (the shift cooldown counts from this). */
		double LastSectorShiftAt = -1e9;
		/** World time an enemy last shot at him (NotifyShotAt; -1e9: never). */
		double LastShotAtTime = -1e9;
		/**
		 * G008b: world time a known enemy first entered this overwatch post's engagement range (-1: not yet).
		 * It is not cleared when that attacker steps back out. Stepping off the roof clears it.
		 */
		double PostRangeContactAt = -1.0;
		/**
		 * G008b review: the overwatch hold closed for this posting (hit, suppression, wound, too close).
		 * Stays set while he remains on the post. Cleared when he steps off.
		 */
		bool bOverwatchHoldClosed = false;
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
		/** The last time one of the team SAW an enemy (honest sight; heard shots do not count). */
		double LastSeenContactTime = -1e9;
		/** The team came as hunters; it hunts again when it lost the player's men (HuntResumeSeconds); since when (-1: not resumed). */
		bool bHuntTeam = false;
		double HuntResumedAt = -1.0;
		/** The fight's first contact (-1: none yet) and whether a shot was exchanged with an enemy (target attention, 2026-11-07). */
		double FirstContactTime = -1.0;
		bool bShotsExchanged = false;
		/** The last shot between this team and an enemy (either way; fix/pmc-arena-stalemate: the quiet clock of the push). */
		double LastShotTime = -1e9;
		FPmcTeamStats Stats;
		TOptional<FVector> AssaultPoint;
		/** The round objective (the arena, task #53; DistanceCm is filled per fighter) and when it was set (the quiet clock's start). */
		TOptional<FPmcObjectiveView> Objective;
		double ObjectiveSetTime = -1e9;
		/** The encounter posture (before contact). */
		TOptional<EPmcPosture> Posture;
		FVector PostureAnchor = FVector::ZeroVector;
		FVector PlayerStart = FVector::ZeroVector;
		int32 PostureSeed = 0;
		int32 PostureLeg = 0;
		float PostureTimer = 0.f;
		/** TACTICAL_MAP P2: the route the team's enemy walks (SetTeamRoute; empty: none). */
		TArray<FVector> Route;
		/**
		 * The analysis hiding spots near Route (within the tactical reach + the 2.5 m cover match), cached by SetRouteOf so FindCoverNear's
		 * route-cover term does not scan every hiding spot of the map per slot.
		 */
		TArray<FVector> RouteCoverSpots;
		/** G008: the defender plan for an objective round. Inactive for attackers and for teams with no objective. */
		struct FDefence
		{
			bool bActive = false;
			/** The access line was logged for this objective. A death does not log it again. */
			bool bLogged = false;
			FPmcDefenceAccess Access;
			FVector ApproachPoint = FVector::ZeroVector;
			bool bHasApproach = false;
			float ApproachYawDeg = 0.f;
			TArray<FPmcDefenceSpot> Spots;
			float LevelZ = 0.f;
			/** A roof was chosen, so the area is the spot span. Empty spots still use the ground radius. */
			bool bRoof = false;
			FVector Inward = FVector::ZeroVector;
			bool bHasInward = false;
			/** SquadIndex of each living defender the posts were built for, sorted. */
			TArray<int32> LivingIndices;
			/** How many wedges the living sector shooters were split into (0 when the only man is the guard). */
			int32 SectorCount = 0;
			/**
			 * SquadIndex of the occupier (review finding 3): the sector shooter whose post is nearest the point, else the guard.
			 * He alone walks onto the point on a quiet push or a late clock, so a time win stays possible.
			 */
			int32 OccupierIndex = INDEX_NONE;
		};
		FDefence Defence;
	};

	/** Sets Team.Route and refreshes Team.RouteCoverSpots from the tactical map (every route assignment goes through here). */
	void SetRouteOf(FTeam& Team, const TArray<FVector>& Route) const;

	/**
	 * Keeps the post candidates that stand on the navmesh AT THEIR OWN LEVEL (|dz| <= 150 cm, so a roof spot never snaps to the street under
	 * it) and moves each onto that navmesh point; a candidate off the navmesh would leave its man unable to move.
	 */
	void KeepCandidatesOnNav(TArray<FPmcPostCandidate>& Candidates) const;

	/** ProjectToNav with a small vertical extent: OutPoint on the navmesh within 150 cm of Point's own height (false: none there). */
	bool ProjectToNavAtLevel(const FVector& Point, FVector& OutPoint) const;

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
	 * PostLevelZ (G008 lead review 2, B): a posted defender's post height - slots off that level are skipped (no cover down the stair).
	 */
	bool FindCoverNear(const AOperativeCharacter& Fighter, const FVector& Threat, float RadiusCm, const FVector* Exclude, const FPmcWeights& Weights,
		FCoverSlot& OutSlot, float& OutQuality, const FPmcEngagementBand& Band = FPmcEngagementBand(), float* OutBandFactor = nullptr,
		TConstArrayView<FVector> ClaimedPosts = TConstArrayView<FVector>(), TConstArrayView<FString> ClaimedKeys = TConstArrayView<FString>(),
		const float* PostLevelZ = nullptr) const;
	/** G008: resolve the roof access (once per objective) and assign guard / sector posts. bResolveAccess false keeps the access. */
	void RefreshDefence(int32 TeamId, FTeam& Team, bool bResolveAccess);
	void ClearDefencePosts(FTeam& Team);
	/**
	 * G008 follow-up (review finding 1): the defence post could not be reached in defence_post_walks walks. He becomes a sector
	 * shooter with no wedge on the nearest roof spot with a full path (not a mate's post), else where he stands.
	 */
	void DropDefencePost(int32 TeamId, FTeam& Team, AOperativeCharacter& Fighter, FFighterState& State);
	/** Other living defenders' cover slots or defence posts, for the spacing penalty. Empty unless this team is defending. */
	void CollectDefenceClaims(const FTeam& Team, const AOperativeCharacter& Fighter, TArray<FVector>& OutPoints, TArray<FString>& OutKeys) const;
	/** The map's levelgen layout (not the -LevelJson= combat override). Cached. Null when the map has none. */
	const FPmcLayoutAccess* DefenceLayout();
	bool ProjectToNav(const FVector& Point, FVector& OutPoint) const;
	/** The placement roles of a squad about to spawn (its pool weapons by seed) / of living fighters (their weapons). */
	TArray<FPmcPlacementFighter> PlacementRoles(int32 Count, int32 Seed, const FPmcWeights& Weights) const;
	TArray<FPmcPlacementFighter> PlacementRoles(const TArray<AOperativeCharacter*>& Fighters, const FPmcWeights& Weights) const;
	/** A full (not partial) navmesh path From -> To. */
	bool HasFullNavPath(const FVector& From, const FVector& To) const;
	/**
	 * fix/pmc-arena-stalemate + fix/pmc-onpoint-3d-detour: the running walk made no progress for RepathAfterSeconds - steer it through the
	 * first waypoint of the detour Step (PmcCommanderRules::DetourCandidates: a side, the other side, a short step back) that has full paths
	 * from him and on to the goal (AOperativeCharacter::SteerOwnMoveVia; the walk re-routes to its goal from the waypoint). OutSide = the
	 * side taken.
	 */
	bool TryDetour(AOperativeCharacter& Fighter, const FFighterState& State, const FPmcWeights& Weights, EPmcDetourStep Step, int32& OutSide) const;
	/** The order failed (cooldown in PickCommitted); counted and logged. */
	void MarkFailed(int32 TeamId, FTeam& Team, AOperativeCharacter& Fighter, FFighterState& State, EPmcOrder Order, const TCHAR* Why);
	bool IsExposedTo(int32 TeamId, const AOperativeCharacter& Fighter) const;
	/** The enemies the team sees now: their positions and facings as seen (target attention, 2026-11-07). */
	void SeenWatchers(const FTeam& Team, TArray<FVector2D>& OutPositions, TArray<float>& OutYaws) const;
	void HandleWeaponFired(AOperativeCharacter* Shooter, AActor* Target, bool bHit);

	TMap<int32, FTeam> Teams;
	float SightTimer = 0.f;
	/** G008: one parse of the map's levelgen layout per world. */
	bool bDefenceLayoutTried = false;
	TOptional<FPmcLayoutAccess> DefenceLayoutCache;
	/** Fighters whose OnWeaponFiredNative this subsystem listens to. */
	TSet<TWeakObjectPtr<AOperativeCharacter>> Listened;
};
