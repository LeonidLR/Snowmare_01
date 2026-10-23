#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/DataTable.h"
#include "Ladders/LadderRules.h"
#include "Ladders/LadderTypes.h"
#include "LadderActor.generated.h"

class UBoxComponent;
class UInstancedStaticMeshComponent;
class UNavLinkCustomComponent;
class UTextRenderComponent;

/**
 * Procedural ladder (docs/design/LADDERS.md section 3). Origin = the bottom centre of the rung plane on the floor, +X (actor
 * forward) away from the wall / towards the climber, +Z up; the climber hangs facing -X. OnConstruction lays out the rails
 * (tiled segments), the rungs (pitch 36.4 cm: the clips' contract), the wall brackets and the top grab bars as one ISM each
 * (no collision, no nav effect), a click proxy (blocks Visibility only) and a smart nav link (UNavArea_Ladder, both ways)
 * between the bottom and the top link points. Fewer than 7 rungs or no top exit = a decoration ladder: no link, nothing climbs
 * it. A ladder WITHOUT a wall behind it (free-standing, user decision 2026-10-22) stands on its own rails to a platform: no
 * brackets, the same climb. The climb itself is ULadderClimbComponent on the climber; this actor is data + the lease.
 * UE-only system (no Godot reference).
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API ALadderActor : public AActor
{
	GENERATED_BODY()

public:
	ALadderActor();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// --- Parameters (LADDERS.md 3.2) ---

	/** A row of DT_LadderTypes (FLadderMeshData); empty = MeshData below (the graybox steel look). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Ladder")
	FDataTableRowHandle LadderType;

	/** The look used when LadderType names no row. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Ladder")
	FLadderMeshData MeshData;

	/** Wanted height, cm; quantised to the pitch (RungCount = round(Height / Pitch), the top floor = RungCount * Pitch). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Ladder", meta = (ExposeOnSpawn = "true", ClampMin = "10"))
	float Height = 255.f;

	/** Rung pitch, cm (the clip set's contract: the up loop climbs two rungs of 36.4 cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Ladder", meta = (ClampMin = "10"))
	float Pitch = 36.4f;

	/** Rail to rail, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Ladder", meta = (ClampMin = "30"))
	float Width = 60.f;

	/** The ladder leads onto a floor at its top (false = it ends against nothing: no nav link, decoration). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Ladder", meta = (ExposeOnSpawn = "true"))
	bool bTopExit = true;

	/** Rail segments above the top floor (hand holds past the edge). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Ladder", meta = (ClampMin = "0"))
	int32 RailOvershootSegments = 1;

	/** Out-bent grab bars above the top. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Ladder")
	bool bTopGrabBars = true;

	/** Wall brackets; -1 = auto (3 when a wall is found behind the rungs, else 0 = free-standing). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Ladder", meta = (ExposeOnSpawn = "true", ClampMin = "-1"))
	int32 WallBracketCount = -1;

	/** How far behind the rung plane a wall still counts (the brackets reach it), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Ladder", meta = (ClampMin = "1"))
	float WallSearchCm = 25.f;

	/** The climbing enemies (AEnemyCharacter::bCanClimbLadders, LADDERS.md 7.5) may use this ladder; off = the squad only. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Ladder")
	bool bAllowEnemies = true;

	/** The clip contract the anchor points come from (LADDERS.md 3.1 / 4). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Ladder")
	float HangStandoff = 48.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Ladder")
	float HangLateral = 7.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Ladder")
	float TopStandoff = 49.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Ladder")
	float TopExitStandoff = 40.f;

	/** Re-runs the validation traces (editor; the result shows as the editor-only text and in the log). */
	UFUNCTION(CallInEditor, Category = "CodexTactics|Ladder")
	void Validate();

	// --- Queries ---

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Ladder")
	int32 GetRungCount() const { return RungCount; }

	/** Climbable (>= 7 rungs, a top exit) - else decoration. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Ladder")
	bool IsClimbable() const;

	/** A wall stands behind the rungs (brackets drawn); false = free-standing. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Ladder")
	bool HasWall() const { return bHasWall; }

	/** The clip contract with this ladder's numbers. */
	LadderRules::FClipContract GetContract() const;

	/** World Z of the ladder's floor / of the top floor. */
	float GetFloorZ() const { return GetActorLocation().Z; }
	float GetTopWorldZ() const;

	/** Rung K centre, world (K = 0 the floor). */
	FVector GetRungWorldLocation(int32 K) const;

	/** The hang line at feet height FeetZ above the ladder's floor, world (the capsule bottom point). */
	FVector GetHangWorldLocation(float FeetZ) const;

	/** Link points (feet), world. */
	FVector GetBottomLinkWorld() const;
	FVector GetTopLinkWorld() const;
	FVector GetTopExitWorld() const;

	/** Away from the wall (towards the climber), horizontal. */
	FVector GetOutward() const { return GetActorForwardVector().GetSafeNormal2D(); }

	/** The climber's yaw on the ladder (always facing the wall). */
	float GetClimberYaw() const;

	/** The nav link (its id is matched against the path corridor). */
	UNavLinkCustomComponent* GetNavLink() const { return NavLink; }

	/** Rail-to-rail half width, cm. */
	float GetHalfWidth() const { return Width * 0.5f; }

	// --- Lease: one climber at a time (LADDERS.md 7.4) ---

	/** Takes the ladder for Climber; true when free or already his. */
	bool TryLease(AActor* Climber);
	void ReleaseLease(AActor* Climber);
	AActor* GetLeaseHolder() const { return LeaseHolder.Get(); }
	bool IsLeasedByOther(const AActor* Climber) const { return LeaseHolder.IsValid() && LeaseHolder.Get() != Climber; }

	/** The last validation result (empty = fine). */
	const FString& GetValidationText() const { return ValidationText; }

	/** Instances drawn (smokes / LadderGeneratorSmoke). */
	int32 GetRungInstanceCount() const;
	int32 GetRailInstanceCount() const;
	int32 GetBracketInstanceCount() const;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Ladder")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Ladder")
	TObjectPtr<UInstancedStaticMeshComponent> Rails;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Ladder")
	TObjectPtr<UInstancedStaticMeshComponent> Rungs;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Ladder")
	TObjectPtr<UInstancedStaticMeshComponent> Brackets;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Ladder")
	TObjectPtr<UInstancedStaticMeshComponent> GrabBars;

	/** Click target around the rungs: blocks Visibility only (the cursor trace), nothing else. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Ladder")
	TObjectPtr<UBoxComponent> ClickProxy;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Ladder")
	TObjectPtr<UNavLinkCustomComponent> NavLink;

#if WITH_EDITORONLY_DATA
	/** Editor-only validation text (hidden in game). */
	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Ladder")
	TObjectPtr<UTextRenderComponent> ValidationLabel;
#endif

private:
	/** Lays out every instance and the link from the parameters. */
	void Generate();
	const FLadderMeshData& ResolveMeshData() const;
	void ApplyLook(UInstancedStaticMeshComponent* Component, UStaticMesh* Mesh, const FLinearColor& Color, UMaterialInterface* Material);
	/** Is there a wall within WallSearchCm behind the rung plane (traces at a few heights)? */
	bool DetectWall() const;
	void UpdateNavLink();
	/** The smart link's "reached" callback: hands the climber over to his ULadderClimbComponent. */
	void HandleLinkReached(UNavLinkCustomComponent* ThisComp, UObject* PathComp, const FVector& DestPoint);

	int32 RungCount = 0;
	bool bHasWall = false;
	FString ValidationText;
	TWeakObjectPtr<AActor> LeaseHolder;
};
