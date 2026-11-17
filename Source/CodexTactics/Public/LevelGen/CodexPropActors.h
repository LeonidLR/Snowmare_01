#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "CodexPropActors.generated.h"

class UNavModifierComponent;
class UShapeComponent;
class UStaticMeshComponent;
struct FPropGreyboxSpec;
class UBoxComponent;
class USphereComponent;
class UNavArea;

/**
 * Armed trap component for mined crates (PROPS_LAYERS.md 3.2, task C6/C7).
 * Attached to ACodexPropCrate when a mine object targets the crate via 'attach'.
 * Does not tick (overlap/interaction event driven only).
 */
UCLASS(ClassGroup = (CodexTactics), meta = (BlueprintSpawnableComponent))
class CODEXTACTICS_API UCodexPropTrapComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCodexPropTrapComponent();

	/** Arms the trap on the crate with the given mine data ID */
	void Arm(const FString& InTrapMineId, bool bInInert = false);

	/** Triggers the trap (e.g. on crate open or destruction). Returns true if detonated, false if inert/disarmed */
	UFUNCTION(BlueprintCallable, Category = "Props|Trap")
	bool TriggerTrap();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Props|Trap")
	bool bArmed = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Props|Trap")
	bool bInert = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Props|Trap")
	FString TriggerType = TEXT("crate_open");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Props|Trap")
	FString TrapMineId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Props|Trap")
	float BlastRadiusCm = 250.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Props|Trap")
	float BlastDamage = 90.f;
};

/**
 * Base actor for levelgen props (PROPS_LAYERS.md 3.1, 3.2, task C6).
 * Carries tags: CodexProp, prop_layer=<layer id>, prop_id=<object id>.
 * Zero ticking (PrimaryActorTick.bCanEverTick = false).
 * Nav-affecting props register a UNavModifierComponent sized to their footprint.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API ACodexPropActor : public AActor
{
	GENERATED_BODY()

public:
	ACodexPropActor();

	/** Initializes the prop's metadata, tags, and nav modifier */
	virtual void InitProp(
		const FString& InPropId,
		const FString& InLayerId,
		const FString& InVariant,
		const FString& InWalkLayer,
		bool bInNavAffecting,
		const FVector& InExtent,
		TSubclassOf<UNavArea> InNavArea = nullptr);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Props")
	FString PropId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Props")
	FString LayerId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Props")
	FString Variant;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Props")
	FString WalkLayer;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Props")
	bool bNavAffecting = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Props")
	TObjectPtr<UNavModifierComponent> NavModifierComponent;

	/**
	 * Builds the greybox look (G003, PropsGreybox): an engine basic shape standing on the actor origin, coloured through the engine
	 * shape material, blocking or not as the spec says, never affecting navigation itself (the nav modifier does that). Plus the turret's
	 * muzzle bar / the tripwire's wire. Called once by UCodexLevelPropsSubsystem; a second call replaces the look.
	 */
	void ApplyGreybox(const FPropGreyboxSpec& Spec);

	/** The greybox body (null until ApplyGreybox). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Props")
	TObjectPtr<UStaticMeshComponent> GreyboxMesh;

	/** The turret's muzzle bar / the tripwire's wire (null when the spec has none). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Props")
	TObjectPtr<UStaticMeshComponent> GreyboxDetail;
};

/**
 * Crate prop (PROPS_LAYERS.md 3.2).
 * Supports HP, optional loot table, and optional trap component for mined crates.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API ACodexPropCrate : public ACodexPropActor
{
	GENERATED_BODY()

public:
	ACodexPropCrate();

	/** Ensures a UCodexPropTrapComponent exists on this crate */
	UCodexPropTrapComponent* EnsureTrapComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Props|Crate")
	float HP = 100.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Props|Crate")
	FString LootTable;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Props|Crate")
	TObjectPtr<UCodexPropTrapComponent> TrapComponent;

	/** True if derived as dangerous (mined crate: crate_colors.dangerous = red) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Props|Crate")
	bool bDangerous = false;
};

/**
 * Barrel prop (PROPS_LAYERS.md 3.2).
 * Supports static placement, HP, and explosive variant.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API ACodexPropBarrel : public ACodexPropActor
{
	GENERATED_BODY()

public:
	ACodexPropBarrel();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Props|Barrel")
	float HP = 60.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Props|Barrel")
	bool bExplosive = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Props|Barrel")
	float ExplosionRadiusCm = 350.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Props|Barrel")
	float ExplosionDamage = 120.f;
};

/**
 * Stationary turret prop (PROPS_LAYERS.md 3.2).
 * Spawns with orientation yaw and coverage arc.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API ACodexPropTurret : public ACodexPropActor
{
	GENERATED_BODY()

public:
	ACodexPropTurret();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Props|Turret")
	float HP = 200.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Props|Turret")
	float ArcDeg = 90.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Props|Turret")
	int32 CoveredM = 0;
};

/**
 * Mine prop (PROPS_LAYERS.md 3.2).
 * Overlap events only, zero tick. Never blocks navigation.
 * Trigger types: "tripwire" (doorways) or "proximity" (field).
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API ACodexPropMine : public ACodexPropActor
{
	GENERATED_BODY()

public:
	ACodexPropMine();

	/** Sets up trigger collision shapes based on trigger type and parameters */
	void SetupTrigger(const FString& InTriggerType, float InWidthCm = 180.f, float InTriggerRadiusCm = 120.f);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Props|Mine")
	FString TriggerType = TEXT("proximity");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Props|Mine")
	FString AttachedDoorId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Props|Mine")
	float WidthCm = 180.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Props|Mine")
	float BlastRadiusCm = 250.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Props|Mine")
	float BlastDamage = 90.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Props|Mine")
	bool bMinesInert = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Props|Mine")
	TObjectPtr<UShapeComponent> TriggerShape;
};

/**
 * Loose loot pile prop (PROPS_LAYERS.md 3.2).
 * Stores loot table name. Never blocks navigation.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API ACodexPropLootPile : public ACodexPropActor
{
	GENERATED_BODY()

public:
	ACodexPropLootPile();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Props|Loot")
	FString LootTable;
};
