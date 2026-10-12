#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Subsystems/WorldSubsystem.h"
#include "Vehicles/VehicleCombatRules.h"
#include "Vehicles/VehicleHullRules.h"
#include "Vehicles/VehicleRules.h"
#include "VehicleSubsystem.generated.h"

class AOperativeCharacter;
class AVehiclePawn;
class ANavMeshBoundsVolume;

/**
 * The squad's vehicles (APC P1, docs/vehicles/APC_02_DESIGN.md §1): boarding (Y / Alt + Y: walk to the nearest free door,
 * ~2 s, then mounted), getting out (exit on the side away from known threats, valid navmesh, straight into cover at the
 * hull), driving orders from the controller, the mounted registry every system asks (VehicleQueries::IsMounted), the
 * opt-in vehicle navmesh bounds and the save / load of vehicles and crews.
 *
 * Mounted operatives stay squad members, co-located with the vehicle (attached, hidden, no collision, movement off); a
 * short list of systems checks IsMounted (enemy targeting / perception, damage, auto fire, formation, autonomy, move
 * orders, mines / tripwires, turn-based entry). UE-only.
 * P4 (2026-10-12): a hull at 0 HP ejects its crew (fade in real time, the safe side, into hull cover); Y near a disabled
 * vehicle sends the Engineer to repair it (walk to the hull, ~5 s, VehicleRepaired); blasts / mines reach the hull
 * (ApplyBlastToVehicles); enemies ask FindEnemyTargetVehicle; the save keeps hull health / disabled.
 */
UCLASS()
class CODEXTACTICS_API UVehicleSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	void RegisterVehicle(AVehiclePawn* Vehicle);
	void UnregisterVehicle(AVehiclePawn* Vehicle);
	TArray<AVehiclePawn*> GetVehicles() const;

	// --- Mounted registry ------------------------------------------------------------------------------------------

	bool IsMounted(const AActor* Operative) const;
	AVehiclePawn* GetVehicleOf(const AActor* Operative) const;
	bool HasMountedOperatives() const { return !Mounted.IsEmpty(); }
	/** Walking to a door / climbing in (smokes). */
	bool IsBoarding(const AOperativeCharacter* Operative) const;
	/** Climbing out (smokes). */
	bool IsDisembarking(const AOperativeCharacter* Operative) const;

	// --- Orders --------------------------------------------------------------------------------------------------

	/**
	 * The Y key (bSquadWide: Alt + Y): the selected operatives (or the whole squad) board the nearest vehicle within range
	 * (Preferred when it is in range), or all get out when they are all aboard. Posts the feed lines, reports
	 * Vehicle.Board / Vehicle.Disembark. Returns what it did.
	 */
	EVehicleKeyAction HandleBoardKey(bool bSquadWide, AVehiclePawn* Preferred = nullptr);

	/** Orders Operatives into Vehicle (nullptr: the nearest in range); returns how many were sent. */
	int32 OrderBoard(const TArray<AOperativeCharacter*>& Operatives, AVehiclePawn* Vehicle = nullptr);

	/** A new order for Operative (a move / cover click): his walk to the door is dropped. */
	void CancelBoarding(const AOperativeCharacter* Operative);

	/** Orders the mounted ones among Operatives out; returns how many will get out. */
	int32 OrderDisembark(const TArray<AOperativeCharacter*>& Operatives);

	/**
	 * A ground click while the leader is aboard (the controller): drive his vehicle there (bRush: double click). Refused in
	 * the tactical pause / turn-based (P1) with a feed line. True when the click was a vehicle order (handled).
	 */
	bool HandleDriveClick(AOperativeCharacter* Leader, const FVector& Goal, bool bRush);

	/** The nearest vehicle to Location within MaxDistanceCm (any when < 0). */
	AVehiclePawn* FindNearestVehicle(const FVector& Location, float MaxDistanceCm = -1.f) const;

	/** Mounts at once (load / smokes): no walk, no door time. */
	bool MountImmediately(AOperativeCharacter* Operative, AVehiclePawn* Vehicle);

	/** Dismounts at once at Location (load / smokes); no cover order. */
	void DismountImmediately(AOperativeCharacter* Operative, const FVector& Location);

	// --- Turret (P2) -----------------------------------------------------------------------------------------------

	/**
	 * Ctrl + click on Target with the leader aboard: the turret's priority target (any posture), reported as
	 * Vehicle.TurretFire. True when it was a turret order (handled; also when refused: no gunner / not an enemy).
	 */
	bool HandleTurretTargetClick(AOperativeCharacter* Leader, AActor* Target);

	// --- Hull (P4) ---------------------------------------------------------------------------------------------------

	/**
	 * The hull of Vehicle reached 0 HP: everyone aboard gets out at once (forced: also in the tactical pause; the fade in
	 * real time) on the side away from known threats into hull cover; boarders still walking up are turned away.
	 * Returns how many are getting out.
	 */
	int32 EjectCrew(AVehiclePawn* Vehicle);

	/**
	 * Y near a disabled Vehicle: the Engineer among Operatives (anyone when HullConfig.OtherRepairSeconds > 0) walks to the
	 * hull and repairs it (RepairSeconds at the hull; a new order / a knockdown interrupts). True when a repair was ordered.
	 */
	bool OrderRepair(const TArray<AOperativeCharacter*>& Operatives, AVehiclePawn* Vehicle);

	bool IsRepairing(const AOperativeCharacter* Operative) const;

	/** 0..1 while someone works at Vehicle's hull (0 when nobody does / still walking). */
	float GetRepairProgress(const AVehiclePawn* Vehicle) const;

	/**
	 * A blast at Centre reaches every vehicle whose hull side is within Radius: DamageAtHullDistance(hull distance) as
	 * Explosion damage; the vehicle that set the charge off itself (TriggeredBy: a mine / tripwire under its wheels) takes
	 * the flat mine damage instead (VehicleHullRules::GetBlastHullDamage).
	 */
	static void ApplyBlastToVehicles(UWorld* World, const FVector& Centre, float Radius, TFunctionRef<float(float)> DamageAtHullDistance,
		const AVehiclePawn* TriggeredBy, const FString& Source);

	/** The manned vehicle an enemy at From goes for (VehicleHullRules::IsEnemyTarget; the nearest hull), nullptr when none. */
	AVehiclePawn* FindEnemyTargetVehicle(const FVector& From, bool bHordeKnowsSquad, float* OutHullDistanceCm = nullptr) const;

	// --- Boarding fade (user 2026-10-12: no boarding animations) ---------------------------------------------------

	/** Screen darkness of the boarding fade, 0..1 (real time). */
	float GetFadeAlpha() const;
	bool IsFadeActive() const { return Fade.IsActive(); }
	/** Fades started so far (one per group of boarders / getting out; smokes). */
	int32 GetFadeCount() const { return FadeCount; }

	// --- Navigation ----------------------------------------------------------------------------------------------

	/**
	 * Lets Volume build the vehicle navmesh too (the vehicle agent bit) and re-registers its bounds. Vehicle navmesh is
	 * opt-in: at BeginPlay every NavMeshBoundsVolume without the "VehicleNav" tag that also serves the infantry loses the
	 * Vehicle agent (no second navmesh cost on maps without vehicles); a vehicle-only volume (Default unticked) keeps it.
	 */
	void EnableVehicleNavigation(ANavMeshBoundsVolume* Volume);

	/** SupportedAgents index of the vehicle agent, INDEX_NONE without one. */
	int32 GetVehicleAgentIndex() const;

	// --- Save / load -----------------------------------------------------------------------------------------------

	/** world_state.vehicles: transform, crew + seats per vehicle. */
	TArray<TSharedPtr<FJsonValue>> CaptureSave() const;

	/** Before a load is applied: everyone out at once (they get their saved places), drives / boarding orders dropped. */
	void PrepareForLoad();

	/** Applies world_state.vehicles (vehicles by name, else spawned from their class). */
	void RestoreSave(const TArray<TSharedPtr<FJsonValue>>& Entries);

private:
	struct FPendingBoard
	{
		TWeakObjectPtr<AOperativeCharacter> Operative;
		TWeakObjectPtr<AVehiclePawn> Vehicle;
		int32 Entry = 0;
		/** < 0: walking to the door; >= 0: climbing in. */
		float ClimbLeft = -1.f;
		float Age = 0.f;
		/** Seconds standing still short of the door (a crowded door, a path dropped by a tile rebuild). */
		float IdleSeconds = 0.f;
		int32 Retries = 0;
		/** At the door, waiting for the group fade (mates still walking up). */
		bool bReady = false;
		float ReadySeconds = 0.f;
		/** Handed to the fade: mounted when the screen is black. */
		bool bFadeQueued = false;
	};

	struct FPendingRepair
	{
		TWeakObjectPtr<AOperativeCharacter> Operative;
		TWeakObjectPtr<AVehiclePawn> Vehicle;
		/** < 0: walking to the hull; >= 0: working. */
		float WorkLeft = -1.f;
		float WorkTotal = 1.f;
		float Age = 0.f;
		float IdleSeconds = 0.f;
		int32 Retries = 0;
	};

	struct FPendingExit
	{
		TWeakObjectPtr<AOperativeCharacter> Operative;
		TWeakObjectPtr<AVehiclePawn> Vehicle;
		FVector Point = FVector::ZeroVector;
		float ClimbLeft = 0.f;
		/** Handed to the fade: moved out when the screen is black. */
		bool bFadeQueued = false;
	};

	void Mount(AOperativeCharacter* Operative, AVehiclePawn* Vehicle, bool bRaiseEvent);
	void Unmount(AOperativeCharacter* Operative, const FVector& Location, bool bTakeCover);
	void UpdateBoarding(float DeltaTime);
	void UpdateExits(float DeltaTime);
	void UpdateRepairs(float DeltaTime);
	void HandleDisabled(AVehiclePawn* Vehicle);
	/** Exit points round Vehicle on the infantry navmesh (projected) and whether each is free of bodies / other exits. */
	void GatherExitPoints(const AVehiclePawn& Vehicle, TArray<FVector>& OutCandidates, TArray<bool>& OutFree) const;
	/** Sends Worker towards the side of Vehicle's hull nearest him (projected on the infantry navmesh). */
	bool WalkToHull(AOperativeCharacter* Worker, const AVehiclePawn& Vehicle) const;
	void UpdateDoors();
	void StripVehicleAgentFromBounds();
	TArray<FVector> GatherKnownThreats(const FVector& Around) const;
	bool IsExitFree(const AVehiclePawn& Vehicle, const FVector& Point) const;
	void PostLine(const FText& Speaker, const FText& Text) const;
	FText RefusalText(EVehicleRefusal Refusal, const AOperativeCharacter* Operative) const;
	void HandleArrived(AVehiclePawn* Vehicle);
	void HandleBlocked(AVehiclePawn* Vehicle);
	/** Board / exit with a fade now? (real time, the vehicle's fade on; never in the tactical pause / turn-based.) */
	bool ShouldFade(const AVehiclePawn* Vehicle) const;
	void RequestFade(const AVehiclePawn* Vehicle);
	void UpdateFade();
	/** On black: mounts / moves out everyone handed to the fade, snaps the camera. */
	void ExecuteFadeQueue();
	void ShowFadeOverlay();
	void RemoveFadeOverlay();

	TArray<TWeakObjectPtr<AVehiclePawn>> Vehicles;
	TMap<TWeakObjectPtr<const AActor>, TWeakObjectPtr<AVehiclePawn>> Mounted;
	TArray<FPendingBoard> Boarding;
	TArray<FPendingExit> Exits;
	TArray<FPendingRepair> Repairs;
	FVehicleFadeTimeline Fade;
	FVehicleFadeConfig FadeConfig;
	int32 FadeCount = 0;
	/** Real seconds the current fade has run (watchdog: never stuck black). */
	float FadeAge = 0.f;
	TSharedPtr<class SWidget> FadeOverlay;
};

/** Fast queries for the systems that treat a mounted operative differently (no include of the subsystem's users). */
namespace VehicleQueries
{
	/** Operative sits in a vehicle. */
	CODEXTACTICS_API bool IsMounted(const AActor* Operative);

	/** Its vehicle, nullptr when on foot. */
	CODEXTACTICS_API AVehiclePawn* GetVehicle(const AActor* Operative);

	/** Walking to a door / waiting there for the boarding fade. */
	CODEXTACTICS_API bool IsBoarding(const AActor* Operative);
}
