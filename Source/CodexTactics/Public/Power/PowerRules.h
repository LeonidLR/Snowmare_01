#pragma once

#include "CoreMinimal.h"
#include "PowerRules.generated.h"

/**
 * Turret power, phase 1 (docs/design/TURRET_POWER.md, designer decisions 2026-11-17 section 15): a generator feeds 1-4 level turrets by
 * cable; a turret is live only while ITS generator runs; whoever starts a stopped generator owns its network (the squad = friendly,
 * a PMC team = hostile, nobody = neutral); fuel (litres, burnt per nominal hour) is the only currency; a start is loud.
 * Pure rules: no world, no actors. Numbers in Content/Data/Balance/turret_power.json (GetTuning). UE-only, no Godot reference.
 * Tests: CodexTactics.Power.Rules.*.
 */

/** The shown state of a generator, derived in this order: Broken > Running > Fuelled > Dry (section 3.1). */
UENUM(BlueprintType)
enum class EGeneratorState : uint8
{
	/** Intact, 0 L, off. */
	Dry,
	/** Intact, fuel in the tank, off. */
	Fuelled,
	/** Intact, fuel, on: burns fuel, feeds its turrets. */
	Running,
	/** 0 HP: stopped, half of the fuel lost, needs a repair. */
	Broken
};

/** What the operative at the generator's panel can do (section 7). */
UENUM(BlueprintType)
enum class EGeneratorAction : uint8
{
	None,
	/** + one jerrycan (10 L). */
	Refuel,
	/** Off -> running; the owner = the starter's team. */
	Start,
	/** Running -> fuelled (our own network). */
	Stop,
	/** Stop + restart of a network that is not ours: it becomes ours. */
	Seize,
	/** Broken -> dry / fuelled (owner cleared). */
	Repair
};

/** Why an action cannot be done now (None = it can). */
UENUM(BlueprintType)
enum class EGeneratorRefusal : uint8
{
	None,
	Broken,
	NotBroken,
	NoFuel,
	NoJerrycan,
	TankFull,
	AlreadyRunning,
	NotRunning,
	AlreadyOurs,
	/** Designer 2026-11-17 (question 6): no generator actions in turn-based combat. */
	TurnBased
};

/** Who a network fights for, seen from a viewer team (the squad = 0). */
UENUM(BlueprintType)
enum class EPowerAllegiance : uint8
{
	/** No owner (the facility's perimeter automatics): monsters only. */
	Neutral,
	/** The viewer's own team. */
	Friendly,
	/** Another team. */
	Hostile
};

/** A generator's state at level start (props object "start", props_balance.json presets.power_default.start_state). */
UENUM(BlueprintType)
enum class EGeneratorStart : uint8
{
	Dry,
	FuelledOff,
	RunningHostile,
	RunningNeutral,
	Broken
};

/** Turret power tuning (turret_power.json; distances in cm, times in seconds, fuel in litres). */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FPowerTuning
{
	GENERATED_BODY()

	/** World seconds of one nominal hour of burn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power|Time") float NominalHourRealSeconds = 50.f;
	/** A generator running at level start burns before the squad came near. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power|Time") bool bBurnBeforeContact = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power|Time") float ContactWakeCm = 8000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power|Generator") float TankL = 20.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power|Generator") float JerrycanL = 10.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power|Generator") float BurnBaseLPerHour = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power|Generator") float BurnPerTurretLPerHour = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power|Generator") float GeneratorHp = 200.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power|Generator") float FuelLostOnBreakFrac = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power|Generator") float HeatRadiusCm = 800.f;
	/** Seconds of fuel left at which a warning line is posted (descending). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power|Generator") TArray<float> LowFuelWarnSeconds = { 60.f, 15.f };
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power|Generator") float RefuelSeconds = 4.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power|Generator") float StartSeconds = 3.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power|Generator") float StopSeconds = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power|Generator") float SeizeSeconds = 4.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power|Generator") float SeizeDarkAfterSeconds = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power|Generator") float RepairSeconds = 5.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power|Generator") float EngineerTimeMult = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power|Generator") float StartBlizzardMult = 1.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power|Generator") float InterruptReachCm = 250.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power|Generator") float StartNoiseCm = 3000.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power|Generator") float WorkNoiseCm = 400.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power|Network") float CableMinCm = 800.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power|Network") float CableMaxCm = 4000.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power|Network") int32 MaxTurrets = 4;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power|Network") float PlugRangeCm = 4000.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power|Network") float SpinUpSeconds = 2.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power|Network") float WindDownSeconds = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power|Knowledge") float ObserveCm = 4000.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power|Knowledge") float ObserveSeconds = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power|Carry") int32 JerrycansPerOperative = 1;

	/** The PMC team owning a generator that runs hostile at level start. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power|Teams") int32 HostileOwnerTeam = 1;
};

/** What a generator is set to at level start (StartSetup). */
struct CODEXTACTICS_API FGeneratorSetup
{
	bool bBroken = false;
	float FuelL = 0.f;
	bool bSwitchOn = false;
	int32 OwnerTeam = -1;
	/** Already burning (false: a pre-powered generator waits for the squad's contact). */
	bool bContact = true;
};

namespace PowerRules
{
	/** No owner: a neutral network (and every stopped one). */
	inline constexpr int32 NoOwner = -1;
	/** The squad's team id (AOperativeCharacter::TeamId 0). */
	inline constexpr int32 PlayerTeam = 0;

	/** Content/Data/Balance/turret_power.json. */
	CODEXTACTICS_API FString GetTuningPath();

	/** The tuning in force (the file, read once; code defaults when it is missing or broken). */
	CODEXTACTICS_API const FPowerTuning& GetTuning();

	/** Overlays the keys present in Json on Out (others keep their value). False (Out untouched) when Json is not an object. */
	CODEXTACTICS_API bool ParseTuning(const FString& Json, FPowerTuning& Out);

	/** Broken > Running > Fuelled > Dry. */
	CODEXTACTICS_API EGeneratorState DeriveState(bool bBroken, float FuelL, bool bSwitchOn);

	/** Burn in litres per nominal hour: base + per live turret (LiveTurrets < 0 counts as 0). */
	CODEXTACTICS_API float BurnLPerHour(const FPowerTuning& Tuning, int32 LiveTurrets);

	/** Burn in litres per world second (BurnLPerHour / NominalHourRealSeconds). */
	CODEXTACTICS_API float BurnLPerSecond(const FPowerTuning& Tuning, int32 LiveTurrets);

	/** World seconds the tank lasts at this load (0 for an empty tank). */
	CODEXTACTICS_API float SecondsLeft(const FPowerTuning& Tuning, float FuelL, int32 LiveTurrets);

	/** Does a running generator burn now: never in turn-based; before the squad's contact only with bBurnBeforeContact. */
	CODEXTACTICS_API bool BurnsNow(const FPowerTuning& Tuning, bool bRunning, bool bContact, bool bTurnBased);

	/** Fuel after DeltaSeconds of burn (never below 0). */
	CODEXTACTICS_API float FuelAfterBurn(const FPowerTuning& Tuning, float FuelL, int32 LiveTurrets, float DeltaSeconds);

	/** Fuel after one jerrycan is poured in (capped by the tank). */
	CODEXTACTICS_API float FuelAfterRefuel(const FPowerTuning& Tuning, float FuelL);

	/** Fuel left after a breakdown (half is lost by default). */
	CODEXTACTICS_API float FuelAfterBreak(const FPowerTuning& Tuning, float FuelL);

	/**
	 * The low-fuel warning crossed when the time left went from SecondsBefore to SecondsAfter: its index in LowFuelWarnSeconds
	 * (the lowest crossed one when a single step crosses several), INDEX_NONE for none.
	 */
	CODEXTACTICS_API int32 CrossedWarning(const FPowerTuning& Tuning, float SecondsBefore, float SecondsAfter);

	/** The allegiance of a network owned by OwnerTeam, seen from ViewerTeam. */
	CODEXTACTICS_API EPowerAllegiance AllegianceOf(int32 OwnerTeam, int32 ViewerTeam = PlayerTeam);

	/**
	 * Rule 3 (section 0): a network fires at monsters always, and at a human of HumanTeam only when it has an owner and HumanTeam is not
	 * the owner (no owner = no humans).
	 */
	CODEXTACTICS_API bool MayTargetHuman(int32 OwnerTeam, int32 HumanTeam);

	/**
	 * The one action the panel offers ActorTeam's operative now: Broken -> Repair; Running -> Stop (ours) / Seize (not ours);
	 * Fuelled -> Start; Dry -> Refuel.
	 */
	CODEXTACTICS_API EGeneratorAction PrimaryAction(EGeneratorState State, int32 OwnerTeam, int32 ActorTeam);

	/** Why Action cannot be done (None = it can). */
	CODEXTACTICS_API EGeneratorRefusal CheckAction(const FPowerTuning& Tuning, EGeneratorAction Action, EGeneratorState State, float FuelL,
		int32 JerrycansCarried, int32 OwnerTeam, int32 ActorTeam, bool bTurnBased);

	/** Seconds of work for Action (the engineer x EngineerTimeMult; a start / seize in a blizzard x StartBlizzardMult). */
	CODEXTACTICS_API float ActionSeconds(const FPowerTuning& Tuning, EGeneratorAction Action, bool bEngineer, bool bBlizzard);

	/** How many more jerrycans an operative carrying Carried can take. */
	CODEXTACTICS_API int32 JerrycanRoom(const FPowerTuning& Tuning, int32 Carried);

	/** "dry" / "fuelled_off" / "running_hostile" / "running_neutral" / "broken" (case-insensitive); false for anything else. */
	CODEXTACTICS_API bool ParseStart(const FString& Text, EGeneratorStart& Out);

	/**
	 * What a generator starts as: running ones get FuelL (default: a full tank) and the hostile team / no owner and wait for contact
	 * (unless bBurnBeforeContact); a fuelled one FuelL (default one can); a broken one keeps FuelL (default 0); a dry one 0 L. Fuel is
	 * clamped to the tank.
	 */
	CODEXTACTICS_API FGeneratorSetup StartSetup(const FPowerTuning& Tuning, EGeneratorStart Start, TOptional<float> FuelL);

	/**
	 * The turrets a hand-placed generator at Generator links (legacy-free auto-link): indexes into Turrets of the nearest ones within
	 * PlugRangeCm (3D distance), at most MaxTurrets, nearest first (ties: the lower index).
	 */
	CODEXTACTICS_API TArray<int32> PickTurretsInReach(const FPowerTuning& Tuning, const FVector& Generator, const TArray<FVector>& Turrets);

	/** Observation time after DeltaSeconds: grows while bWatching, back to 0 otherwise. */
	CODEXTACTICS_API float ObserveStep(float Accumulated, float DeltaSeconds, bool bWatching);

	/** The observation identifies the turret's owner. */
	CODEXTACTICS_API bool IsObserved(const FPowerTuning& Tuning, float Accumulated);

	/** "HOSTILE" / "NEUTRAL" / "FRIENDLY". */
	CODEXTACTICS_API FString AllegianceName(EPowerAllegiance Allegiance);

	/** "DRY" / "FUELLED" / "RUNNING" / "BROKEN". */
	CODEXTACTICS_API FString StateName(EGeneratorState State);

	/** "m:ss" of a time left in seconds (rounded up, never negative). */
	CODEXTACTICS_API FString FormatMinutes(float Seconds);
}
