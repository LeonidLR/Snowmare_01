#pragma once

#include "CoreMinimal.h"
#include "Combat/MortarRules.h"
#include "GameFramework/Actor.h"
#include "Math/RandomStream.h"
#include "MortarNestActor.generated.h"

class AOperativeCharacter;
class UStaticMeshComponent;

/** One round leaving the tube (AMortarNestActor::OnRoundFired). */
struct CODEXTACTICS_API FMortarShot
{
	FVector Target = FVector::ZeroVector;
	/** Where the round was aimed after the bracketing error. */
	FVector Aim = FVector::ZeroVector;
	float ErrorCm = 0.f;
	/** 0 = the first round at this target. */
	int32 RoundOnTarget = 0;
	TWeakObjectPtr<AOperativeCharacter> Spotter;
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnMortarRoundFired, class AMortarNestActor* /*Nest*/, const FMortarShot& /*Shot*/);

/**
 * A mortar nest (2026-11-09; docs/design/SQUAD_SIX_FIRE_TEAMS.md 7.3; docs/systems/weapons.md "Mortar nests"; new - no Godot reference): a fixed
 * emplacement of either side, placed from the level JSON "mortar_nests" (UMortarSubsystem). It fires only with a crew of three at its places
 * (AssignCrew: the man walks there and is held; he leaves the crew when he walks off or dies), only at a point that someone of its side SEES now
 * (the spotter rule, UMortarSubsystem::FindSpotter, re-checked before every round) inside its range band; its high-angle rounds fly through the
 * shared projectile layer and burst through Explosions::Detonate as plunging blasts (walls do not protect, a roof does); the first rounds land
 * off the aim point and each next one closer (MortarRules bracketing). Placeholder visuals (a base disc + a tube). Data: MortarRules::Get().
 */
UCLASS()
class CODEXTACTICS_API AMortarNestActor : public AActor
{
	GENERATED_BODY()

public:
	AMortarNestActor();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** 0 = the player's squad; 1.. = a PMC team. A nest left without its crew changes side when the other side mans it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Mortar")
	int32 TeamId = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Mortar")
	FString NestId;

	/** Rounds left (< 0 at BeginPlay = the data's ammo). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Mortar")
	int32 Ammo = -1;

	/**
	 * Takes Man into the crew (a free place; a nest with no crew of its side changes to his side). bAtOnce = he stands at the place now
	 * (spawned crews, smokes), else he walks there (OrderMoveTo) and is held once he arrives. Returns the place index, -1 when refused.
	 */
	int32 AssignCrew(AOperativeCharacter* Man, bool bAtOnce = false);
	/** Lets Man go (his traffic hold released). */
	void ReleaseCrew(AOperativeCharacter* Man);
	void ReleaseAllCrew();

	/** Men standing at their places, alive. */
	int32 GetCrewAtNest() const;
	/** Men assigned (walking there or at their places). */
	int32 GetCrewAssigned() const;
	bool IsCrewing(const AOperativeCharacter* Man) const;
	TArray<AOperativeCharacter*> GetCrew() const;
	FVector GetCrewPlace(int32 Index) const;

	/** The fire checks for Point now (crew, ammo, the band, the spotter); OutSpotter = who sees it. */
	EMortarRefusal CheckFireAt(const FVector& Point, AOperativeCharacter** OutSpotter = nullptr) const;

	/** Starts RoundsPerOrder rounds at Point when CheckFireAt passes; returns the refusal (None = firing). */
	EMortarRefusal OrderFire(const FVector& Point);
	void CeaseFire();

	bool IsFiring() const { return RoundsLeft > 0; }
	const FVector& GetFireTarget() const { return FireTarget; }
	EMortarRefusal GetLastRefusal() const { return LastRefusal; }
	int32 GetRoundsFired() const { return RoundsFired; }
	int32 GetRoundOnTarget() const { return RoundOnTarget; }

	/** The tube's mouth (the rounds leave from here). */
	FVector GetMuzzle() const;

	/** Every round that leaves the tube. */
	FOnMortarRoundFired OnRoundFired;

	/** Fixes the bracketing roll (smokes). */
	void SetSeed(int32 Seed) { Stream.Initialize(Seed); }

protected:
	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Mortar")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Mortar")
	TObjectPtr<UStaticMeshComponent> BaseMesh;

	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Mortar")
	TObjectPtr<UStaticMeshComponent> TubeMesh;

private:
	struct FCrewPlace
	{
		TWeakObjectPtr<AOperativeCharacter> Man;
		bool bAtPlace = false;
	};

	void UpdateCrew();
	void FireRound();

	TArray<FCrewPlace> Places;
	FVector FireTarget = FVector::ZeroVector;
	bool bHasTarget = false;
	int32 RoundsLeft = 0;
	int32 RoundOnTarget = 0;
	int32 RoundsFired = 0;
	float Cooldown = 0.f;
	EMortarRefusal LastRefusal = EMortarRefusal::None;
	FRandomStream Stream;
};
