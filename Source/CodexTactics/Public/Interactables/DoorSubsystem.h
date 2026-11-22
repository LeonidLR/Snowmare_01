#pragma once

#include "CoreMinimal.h"
#include "Interactables/DoorRules.h"
#include "Subsystems/WorldSubsystem.h"
#include "DoorSubsystem.generated.h"

class ADoorActor;
class AOperativeCharacter;
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

	/** A cursor hit that means a door: on one of its leaves, or on the floor next to an open leaf (doors.json click_snap_cm). */
	ADoorActor* FindClickedDoor(const FHitResult& Hit) const;

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

	/** The squad found a key (a building id, an opening id, a door_key tag, or "*"); console Codex.Doors.GiveKey. TODO: a loot item. */
	void AddKey(const FString& KeyId);
	bool HasKeyFor(const ADoorActor& Door) const;

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
	};

	struct FProxy
	{
		TWeakObjectPtr<AActor> Actor;
		bool bWasHidden = false;
		bool bHadCollision = true;
	};

	void TickAutoOpen(float DeltaTime);
	void TickOrders(float DeltaTime);
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
	TWeakObjectPtr<const ADoorActor> LastClickDoor;
	double LastClickSeconds = -1.0;
	FDelegateHandle ExplosionHandle;
	bool bAdopted = false;
	int32 AutoOpenCount = 0;
	int32 KickAttempts = 0;
	int32 KickInjuries = 0;
};
