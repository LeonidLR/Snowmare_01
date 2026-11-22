#pragma once

#include "CoreMinimal.h"
#include "Interactables/DoorRules.h"
#include "Subsystems/WorldSubsystem.h"
#include "DoorSubsystem.generated.h"

class ACharacter;
class ADoorActor;
class ALootCrateActor;
class AOperativeCharacter;
class APawn;
struct FHitResult;

/**
 * Openable doors R1 (designer 2026-11-21; docs/systems/interactables-traps-loot.md "Openable doors"). While the switch is on
 * (DoorTuning::IsOpenableOn: Codex.Doors.Openable, doors.json "enabled"; off by default) every builder leaf tagged `door_leaf` +
 * `door_state=closed|locked` (build_levelgen_map.py opening_state_specs) becomes an ADoorActor: the leaf proxy is hidden with its
 * collision off, a kit-dressed map's kit leaf instances are lifted onto the door's hinges. Switching off releases them: proxies and kit
 * instances come back, the game is exactly as before R1 (decorative walk-through leaves that block sight).
 * Every tick: a pawn in a CLOSED door's zone whose navmesh path crosses the doorway (or who walks into the leaf) opens it after
 * auto_open_delay_s (operatives, the squad, enemies alike; a LOCKED door never auto-opens - its NavArea_Null sends paths elsewhere);
 * operative door orders (RequestInteraction: click / double click / Ctrl + click on a door, designer door spec 2026-11-22) walk the
 * operative up and open it by hand / shut it / unlock it with a key / kick it / shoot its lock; any explosion near a door breaks it
 * (Explosions::OnDetonated). UE-only.
 * R2 (designer 2026-11-22): the door clips (open / kick / push, L or R by the side of the handle, the kicking legs alternate; montage per
 * skeleton from doors.json "montages", the leaf moves at the clip's contact time), the forced push after the key / a shot lock; ENEMIES
 * breach a LOCKED door their path ends at (doors.json "breach": per archetype key / shoot / kick / blast in escalation order; a hostile
 * operative works it like an operative, a monster bashes it in place) and walk on; deterministic rolls (DoorRules::DeterministicRoll,
 * Codex.Doors.Seed); KEYS are items: the squad's found keys (loot containers' FLootContents::KeyIds, a searched body, mission goals'
 * grant_keys), keys carried by enemies (GiveKeyToPawn: he uses it; his body leaves them as searchable remains when he dies).
 */
UCLASS()
class CODEXTACTICS_API UDoorSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** The switch is on and the leaves are adopted. */
	bool IsActive() const { return bAdopted; }

	const TArray<TObjectPtr<ADoorActor>>& GetDoors() const { return Doors; }

	/** The door of a layout opening (null when none). */
	ADoorActor* FindDoor(const FString& BuildingId, const FString& OpeningId) const;

	/** The nearest door whose doorway centre is within RadiusCm (planar, same level) of Point. */
	ADoorActor* FindNearestDoor(const FVector& Point, float RadiusCm) const;

	/**
	 * A cursor hit that means a door (it wins over the cover hug / cover ghost): on one of its leaves; the cursor ray through an OPEN leaf
	 * (no collision while open) or the floor next to it (click_snap_cm); the floor in / right at a SHUT doorway.
	 */
	ADoorActor* FindClickedDoor(const FHitResult& Hit) const;

	/** Doors made from open kit leaves (open doorways of a kit-dressed map) at the adoption (smokes). */
	int32 GetOpenKitDoors() const { return OpenKitDoors; }

	/**
	 * An operative's door order (designer spec 2026-11-22, DoorRules::ChooseClickAction): single click - an unlocked door opens by hand
	 * (hand_open_s, montage hook), an open one shuts, a locked one opens with the squad's key (unlock_s) or says how else; double click -
	 * the KICK (kick_s per attempt, loud, kick_max_attempts tries on a locked door, each may hurt him); Ctrl + click on a locked door -
	 * he SHOOTS THE LOCK (a quick-fire burst at it from within lock_shot_range_cm, the weapon's lock_shot_chance). Explosions break any
	 * door within blast_reach_cm (HandleExplosion). He walks up first unless already in reach. False when the click is not a door order
	 * (the caller goes on with it as an ordinary click).
	 */
	bool RequestInteraction(ADoorActor* Door, AOperativeCharacter* Operative, bool bDoubleClick = false, bool bAttack = false);

	/** A click on Door now: true when it is the second click on the same door within the double-click window (the kick). */
	bool NoteClick(const ADoorActor* Door, double NowSeconds, double WindowSeconds);

	/**
	 * The squad found a key (a building id, an opening id, a door_key tag, or "*"): a loot container / a searched body (ELootItem::DoorKey),
	 * a mission goal's grant_keys, console Codex.Doors.GiveKey. Found keys stay for the level (also across the switch).
	 */
	void AddKey(const FString& KeyId);
	/** The squad holds a key that fits Door (start keys + found keys). */
	bool HasKeyFor(const ADoorActor& Door) const;
	/** The squad's keys now (start + found). */
	TArray<FString> GetSquadKeys() const;
	/** Adds a key to the squad of World (loot, goals); no-op without the subsystem. */
	static void GrantSquadKey(UWorld* World, const FString& KeyId);

	/** An enemy carries a key (level data / smokes): he uses it on a locked door his path ends at; his body leaves it as searchable remains. */
	void GiveKeyToPawn(APawn* Pawn, const FString& KeyId);
	/** Pawn (an enemy; a player operative = the squad's keys) holds a key that fits Door. */
	bool PawnHasKeyFor(const APawn* Pawn, const ADoorActor& Door) const;
	TArray<FString> GetPawnKeys(const APawn* Pawn) const;

	/**
	 * A hostile operative's move was just asked (AOperativeCharacter::RequestMove): when its path stops at a LOCKED door with the goal behind
	 * it, he breaches the door (breach.archetypes "pmc") instead of stopping there. No-op off / for the player's men.
	 */
	static void NotifyMoveRequested(AOperativeCharacter* Operative);

	/** Pawn is breaching a door now (walking up to it / working it): the PMC commander counts his walk as running (no re-decision). */
	bool IsBreaching(const AActor* Pawn) const;
	static bool IsPawnBreaching(const AActor* Pawn);

	/** Breaches started / doors an enemy got through since the adoption (smokes). */
	int32 GetBreachStarts() const { return BreachStarts; }
	int32 GetBreachOpens() const { return BreachOpens; }
	/** What a pawn's breaches did ("shoot:holds shoot:gives push ..."), empty when none (smokes: the same seed = the same trace). */
	FString GetBreachTrace(const AActor* Pawn) const;
	void ClearBreachTraces() { BreachTraces.Reset(); }
	/** Forgets a pawn's given-up doors (smokes re-run a breach). */
	void ClearGiveUps() { GiveUps.Reset(); }
	/** Remains made for the keys of dead carriers (smokes). */
	int32 GetKeyRemainsSpawned() const { return KeyRemainsSpawned; }
	ALootCrateActor* GetLastKeyRemains() const { return LastKeyRemains.Get(); }

	/** The door clips chosen since the adoption ("open_r", "kick_l", "push_r" ...; the last 256) and the montages played (smokes). */
	const TArray<FString>& GetClipLog() const { return ClipLog; }
	int32 GetMontagesPlayed() const { return MontagesPlayed; }

	/** One door clip's root-motion move, measured on the capsule (smokes, the log). */
	struct FClipMove
	{
		FString Key;
		/** A kick: degrees between his facing and the way into the door at the clip start / at the impact (every attempt faces the leaf). */
		float StartFacingErrorDeg = 0.f;
		float ImpactFacingErrorDeg = -1.f;
		/** His feet's move from the clip start to its end along / across his facing, cm. */
		float ForwardCm = 0.f;
		float SideCm = 0.f;
		/** The move from just before the end to 0.3 s after it (a pop back would show here), cm. */
		float AfterEndCm = 0.f;
		/** The same move along his clip facing (a snap back is negative; walking on after a breach is positive), cm; 0 when followed. */
		float AfterEndForwardCm = 0.f;
		/** montage_root_cm of the clip's kind (0 = unknown, or his clip plays in place). */
		float ExpectedCm = 0.f;
		/** Who played it (GetName) and his skeleton (the PMC body's own set vs the operatives' default set). */
		FString PawnName;
		FString Skeleton;
	};
	/**
	 * Pawn's Clip moves him (root motion: he walks to the clip's start pose first, the clip walks him to the leaf). Operatives only: the
	 * default set always; a skeleton's own set (the PMC body) when its clip asset is on that skeleton with root motion (DoorRules::ClipMovesPawn:
	 * the retargeted PMC copies of setup_door_animation.py; the old bone-name pack copies play in place).
	 */
	bool UsesRootMotionClips(const ACharacter& Pawn, EDoorClip Clip) const;
	/** The measured moves since the adoption (the last 64). */
	const TArray<FClipMove>& GetClipMoves() const { return ClipMoves; }
	/** Every kick attempt's facing off the leaf at its impact, degrees (smokes). */
	const TArray<float>& GetKickFacings() const { return KickFacings; }
	const FString& GetLastMontageName() const { return LastMontageName; }

	/** Drops Operative's pending door order (another order replaced it). */
	void CancelInteraction(const AOperativeCharacter* Operative);

	/** Operative has a door order pending (walking up or kicking). */
	bool HasPendingInteraction(const AOperativeCharacter* Operative) const;

	/** Adopts the leaves now (also done by Tick when the switch turns on). Returns the doors made. */
	int32 AdoptLeaves();

	/** Releases every door (proxies back; also done by Tick when the switch turns off). */
	void ReleaseDoors();

	/** Doors opened by somebody walking through them since the adoption (smokes). */
	int32 GetAutoOpenCount() const { return AutoOpenCount; }

	/** Kick attempts / injuries since the adoption (smokes). */
	int32 GetKickAttempts() const { return KickAttempts; }
	int32 GetKickInjuries() const { return KickInjuries; }

	/** Forced rolls (0..1) for the next kick / lock shot decisions (smokes; < 0 = random). */
	float KickRollForTesting = -1.f;
	float InjuryRollForTesting = -1.f;
	float LockShotRollForTesting = -1.f;

private:
	struct FPendingOrder
	{
		TWeakObjectPtr<AOperativeCharacter> Operative;
		TWeakObjectPtr<ADoorActor> Door;
		EDoorAction Action = EDoorAction::Open;
		FVector Approach = FVector::ZeroVector;
		float Age = 0.f;
		/** < 0: still walking up; >= 0: seconds of work (hand on the handle, the key, a kick, the burst) left. */
		float WorkLeft = -1.f;
		/** Kicks done in this order. */
		int32 Attempts = 0;
		/** R2: the open is the forced push (after the key / a shot lock). */
		bool bPush = false;
		/** R2: WorkLeft counts the end of a held kick's clip (the next attempt starts after it). */
		bool bRecover = false;
		/** R2: the first kick's leg (the legs alternate from it). */
		EDoorSide KickFirst = EDoorSide::Right;
		/** R2: the playing clip's length (0 = none), s. */
		float ClipSeconds = 0.f;
		/** R2: an enemy's breach (not the player's order): escalates through his archetype's actions, then walks on to ResumeGoal. */
		bool bBreach = false;
		FVector ResumeGoal = FVector::ZeroVector;
		TSet<EDoorAction> Tried;
		/** A breach: the side of the door he works it from (+1 / -1; 0 = his own side). */
		float BreachSide = 0.f;
		/** The yaw the door work holds him at (set when the work starts; the facing lock). */
		TOptional<float> FaceYaw;
		/** Seconds since the order's last walk-up started (a stopped walk short of the point still acts after a moment). */
		float WalkAge = 0.f;
		/** R2 root motion: the order walks to its clip's start pose (montage_approach_cm) before the clip. */
		bool bClipApproach = false;
		/** Lock bursts fired in this order (a breach fires up to breach.lock_shots). */
		int32 Shots = 0;
	};

	/** A breacher's walk on after the door gave (asked after the doorway's navmesh changed). */
	struct FResume
	{
		TWeakObjectPtr<AOperativeCharacter> Operative;
		FVector Goal = FVector::ZeroVector;
		float TimeLeft = 0.f;
		int32 TriesLeft = 0;
	};

	/** A monster's path seen stopped by a locked door (it may reach the path's end before the next look). */
	struct FBlockedNote
	{
		TWeakObjectPtr<ADoorActor> Door;
		double Time = 0.0;
	};

	/** A monster bashing a locked door in place (R2; it stands at its path's end by the door). */
	struct FBash
	{
		TWeakObjectPtr<ACharacter> Pawn;
		TWeakObjectPtr<ADoorActor> Door;
		EDoorAction Action = EDoorAction::Kick;
		float WorkLeft = 0.f;
		int32 Attempts = 0;
	};

	struct FProxy
	{
		TWeakObjectPtr<AActor> Actor;
		bool bWasHidden = false;
		bool bHadCollision = true;
	};

	void TickAutoOpen(float DeltaTime);
	void TickOrders(float DeltaTime);
	/** R2: enemies whose path ends at a locked door start a breach (every breach.interval_s). */
	void TickBreachDetection(float DeltaTime);
	void TickBashes(float DeltaTime);
	void TickResumes(float DeltaTime);
	/** A hostile operative's path stops at a locked door: he starts its breach (also from the move request hook). */
	void ConsiderBreach(AOperativeCharacter& Operative);
	/** R2: dead key carriers leave their keys as searchable remains. */
	void TickKeyCarriers();
	/** The locked door Pawn's path stops at, its goal behind it (null when none). */
	ADoorActor* FindBlockingDoor(const ACharacter& Pawn, FVector& OutGoal, float* OutWorkSide = nullptr) const;
	/** Starts an enemy operative's breach of Door (his archetype's first possible action). False when he can do nothing to it. */
	bool StartBreach(AOperativeCharacter& Operative, ADoorActor& Door, const FVector& Goal, float WorkSide = 0.f);
	/** The breach abilities of an operative at Door now. */
	FDoorBreachAbility AbilityOf(const AOperativeCharacter& Operative, const ADoorActor& Door) const;
	/** Sets the order's approach for its action and walks him there unless in reach. False when the walk was refused. */
	bool WalkUp(FPendingOrder& Order, AOperativeCharacter& Operative, ADoorActor& Door, bool bSprint);
	/** An order's action ended: true = remove it; a breach that failed escalates to its next action (false = it goes on). */
	bool Conclude(FPendingOrder& Order);
	/** Starts kick attempt Order.Attempts (the leg, the clip, the time to the impact). */
	void StartKickAttempt(FPendingOrder& Order, AOperativeCharacter& Operative, ADoorActor& Door);
	/** Plays a door clip on Pawn (doors.json montages for its skeleton); returns its length (0 = none played). Logs the montage name. */
	float PlayClip(ACharacter& Pawn, EDoorClip Clip, EDoorSide Side, const ADoorActor* Door = nullptr);
	/** Open kit doorways (kit_part=door_leaf_open instances) become open doors; returns how many. */
	int32 AdoptKitOpenLeaves();
	/** Holds the facing of everyone working a door (orders at work, clips playing); releases the rest. */
	void TickFacingLocks();
	/** The side of the handle for Pawn at Door. */
	EDoorSide HandSideFor(const AActor& Pawn, const ADoorActor& Door) const;
	/** A deterministic roll (or the forced test value, consumed). */
	float RollFor(float& Forced, const ADoorActor& Door, const AActor& Pawn, int32 Attempt, const TCHAR* What) const;
	void AddTrace(const AActor* Pawn, const FString& Step);
	/** The clip an Open / Kick order plays (open / push / kick). */
	EDoorClip ClipOf(const FPendingOrder& Order, const ADoorActor& Door) const;
	/** Turns him square to the leaf plane (the clip's root motion runs along his facing). */
	void FaceDoor(AOperativeCharacter& Operative, const ADoorActor& Door) const;
	void TickClipWatches();

	/** A playing clip whose capsule move is measured. */
	struct FClipWatch
	{
		TWeakObjectPtr<ACharacter> Pawn;
		FString Key;
		FVector Start = FVector::ZeroVector;
		FVector Forward = FVector::ForwardVector;
		double EndAt = 0.0;
		bool bPre = false;
		FVector Pre = FVector::ZeroVector;
		float ExpectedCm = 0.f;
		float StartFacingErrorDeg = 0.f;
		FString Skeleton;
		TWeakObjectPtr<const ADoorActor> Door;
		/** His feet last tick (a jump = a teleport: the move is not measured). */
		FVector Last = FVector::ZeroVector;
		bool bTeleported = false;
		/** Another clip of his started before the after-end sample (the next kick): no pop measured. */
		bool bFollowed = false;
	};
	TArray<FClipWatch> ClipWatches;
	TMap<TWeakObjectPtr<AOperativeCharacter>, float> FacingLocks;
	int32 OpenKitDoors = 0;
	float LastKickImpactFacingDeg = 0.f;
	TArray<float> KickFacings;
	TArray<FClipMove> ClipMoves;
	/** Runs the order's action; true when the order is done (acted or refused). */
	bool Act(FPendingOrder& Order);
	/** The work of Order is over (its timer ran out): true when the order is done. */
	bool FinishWork(FPendingOrder& Order);
	bool IsInReach(const FPendingOrder& Order, const AOperativeCharacter& Operative, const ADoorActor& Door) const;
	void HandleExplosion(UWorld* World, const struct FExplosionResult& Result);
	/** Kit-dressed maps: lifts the kit leaf instances of Door's doorway onto its hinges; returns how many. */
	int32 LiftKitLeaves(ADoorActor& Door) const;
	void PostLine(const FText& Text) const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ADoorActor>> Doors;

	TArray<float> Held;
	TArray<FProxy> Proxies;
	TArray<FPendingOrder> Orders;
	TSet<FString> Keys;
	/** Keys the squad found (never reset by the switch). */
	TSet<FString> FoundKeys;
	TMap<TWeakObjectPtr<APawn>, TArray<FString>> CarriedKeys;
	TArray<FBash> Bashes;
	TArray<FResume> Resumes;
	TMap<TWeakObjectPtr<const ACharacter>, FBlockedNote> BlockedNotes;
	/** StartBreach is running (its own walk-up asks a move: no second breach from the hook). */
	bool bStartingBreach = false;
	/** Pawn -> door -> world time until which he leaves that door alone (he ran out of actions). */
	TMap<TWeakObjectPtr<const AActor>, TMap<TWeakObjectPtr<const ADoorActor>, double>> GiveUps;
	TMap<TWeakObjectPtr<const AActor>, FString> BreachTraces;
	TArray<FString> ClipLog;
	FString LastMontageName;
	TWeakObjectPtr<ALootCrateActor> LastKeyRemains;
	float BreachCheckIn = 0.f;
	int32 BreachStarts = 0;
	int32 BreachOpens = 0;
	int32 MontagesPlayed = 0;
	int32 KeyRemainsSpawned = 0;
	TWeakObjectPtr<const ADoorActor> LastClickDoor;
	double LastClickSeconds = -1.0;
	FDelegateHandle ExplosionHandle;
	bool bAdopted = false;
	int32 AutoOpenCount = 0;
	int32 KickAttempts = 0;
	int32 KickInjuries = 0;
};
