#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tactics/DoorwayBreachRules.h"
#include "DoorwaySubsystem.generated.h"

/** Runtime representation of a level doorway or gate. */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FRuntimeDoorway
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doorway")
	FString Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doorway")
	bool bIsGate = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doorway")
	FString BuildingId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doorway")
	FString WallGroupId;

	/** World 3D center of doorway opening on threshold. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doorway")
	FVector Center = FVector::ZeroVector;

	/** Unit normal pointing outward to exterior approach (zero for gates before approach resolution). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doorway")
	FVector OutwardNormal = FVector::ForwardVector;

	/** Unit vector along the wall containing the opening. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doorway")
	FVector WallTangent = FVector::RightVector;

	/** Opening width in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doorway")
	float WidthCm = 120.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doorway")
	int32 Floor = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doorway")
	FString WalkLayer = TEXT("ground");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doorway")
	bool bOnMandatoryRoute = false;

	/** Layout role ("entry", "room", ...). Empty on a schema v1 doorway. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doorway")
	FString Role;
};

/**
 * World subsystem for runtime doorway discovery, spatial queries, and breach triad construction.
 * Step 2 of Tactical Doorway Breach (docs/design/DOORWAY_BREACH.md).
 * Zero ticker.
 */
UCLASS()
class CODEXTACTICS_API UDoorwaySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/** Loads doorways from a layout JSON file (relative to ProjectDir / ProjectContentDir or absolute). */
	bool LoadFromLayout(const FString& LayoutFilePath);

	/** Parses layout JSON string and registers all doorways and gates. */
	bool LoadFromLayoutJson(const FString& JsonString);

	/** Registers an individual doorway. */
	void RegisterDoorway(const FRuntimeDoorway& Doorway);

	/** Clears all registered doorways. */
	void ClearDoorways();

	int32 GetDoorwayCount() const { return Doorways.Num(); }
	const TArray<FRuntimeDoorway>& GetAllDoorways() const { return Doorways; }

	/** Finds a doorway by unique ID. */
	const FRuntimeDoorway* FindDoorwayById(const FString& Id) const;

	/** Finds the nearest doorway within RadiusCm of WorldPos. */
	bool FindDoorwayNear(const FVector& WorldPos, float RadiusCm, FRuntimeDoorway& OutDoorway) const;

	/**
	 * Checks if WorldHitPoint (e.g. cursor trace) falls directly on a doorway portal.
	 * Within opening width + SlackCm laterally, and depth slack across threshold.
	 */
	bool FindDoorwayAtCursor(const FVector& WorldHitPoint, float SlackCm, FRuntimeDoorway& OutDoorway) const;

	/**
	 * Computes the breach triad for a given doorway, resolving approach normal for gates and checking navmesh.
	 * @param Doorway The doorway or gate to breach.
	 * @param OutTriad Populated breach triad.
	 * @param OptionalApproachOrigin Operative or squad location to determine approach side for symmetric gates.
	 */
	bool GetBreachTriadForDoorway(
		const FRuntimeDoorway& Doorway,
		FDoorwayBreachTriad& OutTriad,
		const FVector* OptionalApproachOrigin = nullptr) const;

	/** Convenience method: finds nearest doorway and generates its breach triad. */
	bool GetBreachTriadNear(
		const FVector& WorldPos,
		float RadiusCm,
		FDoorwayBreachTriad& OutTriad,
		const FVector* OptionalApproachOrigin = nullptr) const;

private:
	void AutoDiscoverDoorways(UWorld& InWorld);

	UPROPERTY()
	TArray<FRuntimeDoorway> Doorways;
};
