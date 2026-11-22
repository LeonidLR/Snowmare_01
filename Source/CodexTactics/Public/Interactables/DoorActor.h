#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interactables/DoorRules.h"
#include "NavAreas/NavArea.h"
#include "DoorActor.generated.h"

class UBoxComponent;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * Nav area over a CLOSED (unlocked) door (ADR-0003 "after R1": priced): passable with an entering cost = the open delay
 * (doors.json nav_entering_cost_cm, read when the class default object is built: a change needs a restart). A locked door uses
 * NavArea_Null.
 */
UCLASS()
class CODEXTACTICS_API UNavArea_DoorClosed : public UNavArea
{
	GENERATED_BODY()

public:
	UNavArea_DoorClosed();
};

/** What the door runtime needs to build one door (from the builder's leaf actor, UDoorSubsystem::AdoptLeaves). */
struct CODEXTACTICS_API FDoorSetup
{
	/** Bottom centre of the doorway in the wall plane; X = along the wall, Y = the wall normal, Z up. */
	FTransform Frame;
	float WidthCm = 120.f;
	float HeightCm = 210.f;
	float ThicknessCm = 5.f;
	EDoorLeafState State = EDoorLeafState::Closed;
	FString BuildingId;
	FString OpeningId;
	int32 Floor = 0;
	/** The leaf look: the builder leaf's mesh and its materials (cube 100 cm when null). */
	TObjectPtr<UStaticMesh> LeafMesh;
	TArray<TObjectPtr<UMaterialInterface>> LeafMaterials;
	/** The builder leaf was a hidden kit-dress proxy: the leaf shows the kit instances lifted out of the kit actor instead. */
	bool bVisualHidden = false;
	/** Tag `door_heavy` (or the width rule DoorRules::IsHeavy): a harder kick. */
	bool bHeavyTag = false;
	/** Tag `door_key=<id>`: the key that opens it besides the building / opening id keys. */
	FString KeyId;
	/** Its strength (designer 2026-11-22: one strength for every way in; DoorRules::ResolveStrength from its tag / layout role at the adoption). */
	EDoorStrength Strength = EDoorStrength::Wood;
	/** The layout opening's role ("entry", "unit_entry", "room", ...; empty when unknown). */
	FString Role;
};

/**
 * An openable door of a generated building (R1, designer 2026-11-21; opening-states.md 1 + 4.6, ADR-0003): one actor per door opening
 * whose builder leaf is `closed` or `locked`, made at runtime by UDoorSubsystem from the leaf's tags (so maps need no rebuild and the
 * switch Codex.Doors.Openable off leaves the game exactly as before). One or two leaves on hinges (DoorRules::CountLeaves); Closed /
 * Locked: the leaves block sight, bullets and bodies (WorldStatic, every channel); Open: no collision. The doorway's nav box prices a
 * closed door (UNavArea_DoorClosed) and blocks a locked one (NavArea_Null). Opening pushes the leaf away from the opener over
 * doors.json open_s; the cold shelter (T-021) is told about every open / shut. Interaction: a click on the door (or next to an open
 * leaf) sends the leader (UDoorSubsystem::RequestInteraction); anyone heading through a closed door opens it after auto_open_delay_s.
 * UE-only (no Godot reference).
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API ADoorActor : public AActor
{
	GENERATED_BODY()

public:
	ADoorActor();

	virtual void Tick(float DeltaSeconds) override;

	/** Builds the leaves, the nav box and the initial state (called once, right after spawning). */
	void Setup(const FDoorSetup& InSetup);

	/** An open order (click / script): a Closed door swings open away from Instigator. False when refused (locked, open already). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Doors")
	bool OpenDoor(AActor* InstigatorActor);

	/** A close order: an Open door swings shut when nobody stands in the doorway. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Doors")
	bool CloseDoor(AActor* InstigatorActor);

	/**
	 * One kick attempt: always loud (doors.json kick_noise_m through the lure noise); bLands = it gives (the caller rolled
	 * DoorRules::ResolveKick): a locked / closed door bursts open. True when it opened.
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Doors")
	bool KickDoor(AActor* InstigatorActor, bool bLands = true);

	/** The squad's key: a locked door becomes an unlocked (closed) one. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Doors")
	bool UnlockDoor(AActor* InstigatorActor);

	/** The lock is shot off (the caller rolled the weapon's chance): a locked / closed door bursts open. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Doors")
	bool ShootLockOpen(AActor* InstigatorActor);

	/**
	 * R2 (lock_shot_push): a shot broke the lock - a locked door becomes an unlocked one that is still shut (he pushes it open next,
	 * the push clip). Event DoorLockShot. True when it changed.
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Doors")
	bool BreakLock(AActor* InstigatorActor);

	/** It was locked and its lock was opened (the key or a shot) and it has not been opened since: the next open is the forced push (R2). */
	bool IsLockForced() const { return bLockForced; }

	/** The handle's X in the door frame (one leaf: its free edge; a pair: 0 - where the leaves meet). */
	float GetHandleLocalX() const;

	/** An explosion: the leaf is gone (hidden, no collision), the doorway open for good. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Doors")
	bool BlastDoor(AActor* InstigatorActor);

	/** Heavy (tag door_heavy or doors.json heavy_min_width_cm): a harder, riskier kick. */
	bool IsHeavy() const;
	/** The leaf was blown off. */
	bool IsBroken() const { return bBroken; }
	const FString& GetKeyId() const { return Setup_.KeyId; }
	/** Its strength class (every kick and lock shot is judged against it). */
	EDoorStrength GetStrength() const { return Setup_.Strength; }
	const FString& GetRole() const { return Setup_.Role; }
	/** Where the lock sits (the leaf's free edge, 100 cm up). */
	FVector GetLockPoint() const;

	/** Applies a state at once with no swing (save / smokes); true when it changed. */
	bool ForceState(EDoorLeafState NewState, AActor* InstigatorActor = nullptr);

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Doors")
	EDoorLeafState GetDoorState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Doors")
	bool IsOpen() const { return State == EDoorLeafState::Open; }

	/** Swinging now (opening or closing). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Doors")
	bool IsSwinging() const { return bSwinging; }

	/** 0 = shut, 1 = fully open. */
	float GetOpenAlpha() const { return OpenAlpha; }

	const FString& GetBuildingId() const { return Setup_.BuildingId; }
	const FString& GetOpeningId() const { return Setup_.OpeningId; }
	int32 GetFloor() const { return Setup_.Floor; }
	float GetWidthCm() const { return Setup_.WidthCm; }
	bool WasKicked() const { return bKicked; }
	bool WasLockShot() const { return bLockShot; }

	/** Doorway centre at mid height. */
	FVector GetDoorwayCenter() const;
	/** Unit wall normal (+Y of the door frame) and the wall direction (+X). */
	FVector GetNormal() const;
	FVector GetTangent() const;
	/** World point -> door frame (X along the wall, Y across, from the doorway centre). */
	FVector2D ToLocal(const FVector& World) const;
	/** Is World within the door's height band (feet between the doorway's floor - 60 cm and its top)? */
	bool IsAtDoorLevel(const FVector& FeetWorld) const;

	/** Where an operative stands to work the door from From's side (ApproachCm in front of the leaf, on the floor). */
	FVector GetApproachPoint(const FVector& From) const;

	/** A pawn's capsule stands in the doorway (|y| < 45 cm): a closing door waits / refuses. */
	bool IsDoorwayOccupied() const;
	/** Who stands in the doorway (names, for the log). */
	FString DescribeDoorwayOccupants() const;

	/** The middle of leaf LeafIndex's footprint (0 = A, 1 = B) at floor height, in its current pose. */
	FVector GetLeafFootprintMid(int32 LeafIndex) const;

	/** A click on the floor at Point is a click on this OPEN door's leaf (within doors.json click_snap_cm of a leaf's footprint). */
	bool IsNearOpenLeaf(const FVector& Point) const;

	/** The cursor ray Start -> End passes through one of this OPEN door's leaves (their collision is off while open). */
	bool IsRayOnOpenLeaf(const FVector& Start, const FVector& End) const;

	/** One of the door's own components (leaves) - the click / trace hit is this door. */
	bool OwnsComponent(const UPrimitiveComponent* Component) const;

	/** Blueprint hook (sound, VFX): the state changed (Open after the swing started, Closed once shut). */
	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Doors", meta = (DisplayName = "On Door State Changed"))
	void ReceiveStateChanged(EDoorLeafState OldState, EDoorLeafState NewState, bool bKick);

	/** Kit-dressed maps: the kit leaf instances this door lifted out of a kit ISM (restored when the door goes). */
	struct FLiftedInstance
	{
		TWeakObjectPtr<class UInstancedStaticMeshComponent> Component;
		int32 Index = INDEX_NONE;
		FTransform Original;
	};
	void AddLiftedInstance(class UInstancedStaticMeshComponent* Component, int32 Index, const FTransform& WorldTransform);
	/** Puts the lifted kit instances back (the switch turned off). */
	void RestoreLiftedInstances();

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Doors")
	TObjectPtr<USceneComponent> Frame;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Doors")
	TObjectPtr<USceneComponent> HingeA;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Doors")
	TObjectPtr<USceneComponent> HingeB;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Doors")
	TObjectPtr<UStaticMeshComponent> LeafA;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Doors")
	TObjectPtr<UStaticMeshComponent> LeafB;

	/** The doorway's nav modifier (dynamic obstacle; area by state). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Doors")
	TObjectPtr<UBoxComponent> NavBox;

	/** Kit leaves lifted out of the kit ISM, shown on the hinges instead of the grey leaf (dressed maps). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> KitLeaves;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "CodexTactics|Doors")
	EDoorLeafState State = EDoorLeafState::Closed;

private:
	void ApplyCollision(bool bBlocking);
	void ApplyNav();
	void ApplyLeafPose();
	void StartSwing(bool bOpen, float Sign);
	void NotifyChanged(EDoorLeafState OldState, AActor* InstigatorActor, bool bKick, FName EventOverride = NAME_None);

	FDoorSetup Setup_;
	int32 LeafCount = 1;
	float OpenAlpha = 0.f;
	float TargetAlpha = 0.f;
	float SwingSign_ = 1.f;
	bool bSwinging = false;
	bool bKicked = false;
	bool bLockShot = false;
	bool bBroken = false;
	bool bLockForced = false;
	TArray<FLiftedInstance> Lifted;
};
