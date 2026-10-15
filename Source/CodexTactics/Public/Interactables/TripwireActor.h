#pragma once

#include "CoreMinimal.h"
#include "Interactables/InteractableActor.h"
#include "Interactables/TripwireRules.h"
#include "NavAreas/NavArea.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "TripwireActor.generated.h"

class UBoxComponent;
class UMaterialInstanceDynamic;
class UNavModifierComponent;
class UStaticMeshComponent;

/**
 * Navmesh area over a KNOWN trap: an armed tripwire the squad knows of (its own / a spotted PMC wire) and, since rev 5e, a
 * spotted / squad mine. Costly for the squad's default query filter, cost 1 for the enemies (UNavFilter_NoVault overrides
 * it: they do not know about it). Rev 5e (game designer 2026-10-14): the operatives use the costly filter only in the squad
 * AUTO mode (TripwireRules::ChooseOperativeMoveFilter); in player control their moves take the *TrapLiteral filters below.
 */
UCLASS()
class CODEXTACTICS_API UNavArea_Tripwire : public UNavArea
{
	GENERATED_BODY()

public:
	UNavArea_Tripwire();
};

/** Rev 5e: player control, prone - known traps cost nothing (the order is literal: he crawls / walks where he was sent). */
UCLASS()
class CODEXTACTICS_API UNavFilter_TrapLiteral : public UNavigationQueryFilter
{
	GENERATED_BODY()

public:
	UNavFilter_TrapLiteral();
};

/** Rev 5e: player control, standing / crouched - crawl-only areas closed (UNavFilter_NoCrawl) and known traps cost nothing. */
UCLASS()
class CODEXTACTICS_API UNavFilter_NoCrawlTrapLiteral : public UNavigationQueryFilter
{
	GENERATED_BODY()

public:
	UNavFilter_NoCrawlTrapLiteral();
};

/** Who rigged a wire: the squad (friendly fire on everyone) or the PMC (a level trap that only operatives set off). */
UENUM(BlueprintType)
enum class ETripwireFaction : uint8
{
	/** The squad's own wire (drawer, two anchor clicks): trips on enemies and operatives, only the Medic-Sapper disarms it. */
	Squad,
	/** Hostile PMC wire placed in the level: hidden until spotted, trips only on operatives, anyone may disarm it (role chance). */
	PMC
};

/**
 * Tripwire mine "Tripwire" MUV-3 with two F-1 grenades (Sprint 09, TANDEM «SPRINT 09 DIRECTIVE»; UE-only). A wire
 * 1-5 m long at 30 cm between two anchors (a ground peg, or a bracket on an object). Anyone — enemy or operative — who
 * crosses it with a body higher than the wire (not prone) pulls the pin: "CLICK!", 0.25 s, the paired F-1 blast
 * (140, 4.5 m, full armour penetration, stagger). A medic-sapper disarms it in 3 s and gets the grenades back (one is
 * lost on a fumble). Rules: TripwireRules; placed by URelocationSubsystem (two clicks). A preview copy (bPreview) is
 * the placement hologram: cyan valid, red invalid.
 * Demo 01 G3 (user decisions 2026-10-11): a wire placed in the level with Faction PMC + Armed At Level Start (the
 * APMCTripwireActor defaults) lays itself between its two viewport anchors (AnchorA / AnchorB, actor space, projected on the
 * ground) at BeginPlay, armed. It stays hidden until an operative comes within the mine spotting range (4.5 m, Medic-Sapper
 * +1.5 m), trips only on operatives (prone crawls under), and ANY operative may disarm it crouched with the mine formula
 * (role base + luck, cold -15 / -30 / -50, failed attempts; the mine failure rule: warning / slip / detonation) in 3 / 4 / 5 s
 * (sapper / engineer / others); turn-based: a click on the wire next to it costs 2 AP for one attempt. Success gives the
 * grenades to the disarmer. Saved (faction, spotted, anchors, arming).
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API ATripwireActor : public AInteractableActor
{
	GENERATED_BODY()

public:
	ATripwireActor();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// --- Level placement (PMC wires; Demo 01 G3) ---

	/** Squad = the drawer wire; PMC = hostile level trap (hidden until spotted, operatives only, anyone disarms). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Tripwire")
	ETripwireFaction Faction = ETripwireFaction::Squad;

	/** Placed in the level: lays the wire between AnchorA and AnchorB at BeginPlay, armed at once (no arming delay). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Tripwire")
	bool bArmedAtLevelStart = false;

	/** First anchor in actor space (drag the diamond handle in the viewport); projected on the ground at BeginPlay, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Tripwire", meta = (MakeEditWidget = true))
	FVector AnchorA = FVector(-100.f, 0.f, 0.f);

	/** Second anchor in actor space (viewport handle). The span should be 1-5 m (outside it a warning is logged). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Tripwire", meta = (MakeEditWidget = true))
	FVector AnchorB = FVector(100.f, 0.f, 0.f);

	/** The anchor is a bracket on an object (door frame, post) instead of a peg in the snow (look only). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Tripwire")
	bool bAnchorABracket = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Tripwire")
	bool bAnchorBBracket = false;

	/** A PMC wire is invisible (and not clickable) until an operative spots it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Tripwire")
	bool bHiddenUntilSpotted = true;

	/** Charge: blast damage at the centre (enemies; the squad takes SquadDamageShare of it), radius cm (TripwireRules defaults). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Tripwire|Charge", meta = (ClampMin = "0"))
	float BlastDamage = TripwireRules::BlastDamage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Tripwire|Charge", meta = (ClampMin = "0"))
	float BlastRadiusCm = TripwireRules::BlastRadiusCm;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Tripwire|Charge", meta = (ClampMin = "0", ClampMax = "1"))
	float SquadDamageShare = TripwireRules::SquadDamageShare;

	/** Grenades a successful PMC wire disarm gives the disarmer. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Tripwire|Charge", meta = (ClampMin = "0"))
	int32 GrenadesRecovered = TripwireRules::GrenadeCost;

	/** Spotting range (mine rule 4.5 m) and the Medic-Sapper's bonus (+1.5 m), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Tripwire|PMC", meta = (ClampMin = "0"))
	float SpotRangeCm = TripwireRules::SpotRangeCm;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Tripwire|PMC", meta = (ClampMin = "0"))
	float SapperSpotBonusCm = TripwireRules::SapperSpotBonusCm;

	/** PMC wire disarm time by role, s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Tripwire|PMC", meta = (ClampMin = "0.1"))
	float DisarmSecondsSapper = TripwireRules::HostileDisarmSecondsSapper;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Tripwire|PMC", meta = (ClampMin = "0.1"))
	float DisarmSecondsEngineer = TripwireRules::HostileDisarmSecondsEngineer;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Tripwire|PMC", meta = (ClampMin = "0.1"))
	float DisarmSecondsOther = TripwireRules::HostileDisarmSecondsOther;

	/** Turn-based: AP of one disarm attempt. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Tripwire|PMC", meta = (ClampMin = "0"))
	int32 TurnBasedDisarmAP = TripwireRules::TurnBasedDisarmAP;

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Tripwire")
	bool IsHostile() const { return Faction == ETripwireFaction::PMC; }

	/** A PMC wire already seen by the squad (always true for a squad wire). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Tripwire")
	bool IsSpotted() const { return !IsHostile() || !bHiddenUntilSpotted || bSpotted; }

	/** Hidden from the squad now (not drawn, not clickable, no path cost). */
	bool IsConcealed() const { return !IsSpotted(); }

	/** Shows a hidden PMC wire; with a Spotter the squad stops and he warns (the mine rule). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Tripwire")
	void Reveal(AOperativeCharacter* Spotter = nullptr);

	/** Rev 5h: a hostile wire laid and armed, not spotted yet. */
	virtual bool IsHiddenTrap() const override;
	virtual void RevealByPeek(AOperativeCharacter* Spotter) override;

	/**
	 * Rev 5e: the armed, spotted, not-preview wire whose band (TripwireRules::ClickSnapCm) contains Point (ground level, within
	 * 1 m in height): a click there means the wire (discoverability - the wire is a thin bar). nullptr when none.
	 */
	static ATripwireActor* FindClickableWireNear(const UWorld* World, const FVector& Point);

	/** Who may disarm it (squad wire: the Medic-Sapper; PMC wire: anyone). */
	bool CanBeDisarmedBy(const AOperativeCharacter* Operative) const;

	/** Disarm time for Operative, s. */
	float GetDisarmSecondsFor(const AOperativeCharacter* Operative) const;

	/** A timed disarm is running. */
	bool IsDisarming() const { return bDisarming; }

	/**
	 * Turn-based disarm of a PMC wire (the caller paid TurnBasedDisarmAP): one attempt resolved at once. False when it cannot
	 * be tried (not hostile, hidden, tripped, busy).
	 */
	bool TryTurnBasedDisarm(AOperativeCharacter* User);

	/** Save-game load, before Setup: faction and spotted state. */
	void RestoreSavedState(bool bInHostile, bool bInSpotted);

	virtual FDefusalChance GetDefusalChance(const AOperativeCharacter* Operative) const override;
	virtual void DetonateTrap(bool bByShot = false, const FText& InstigatorName = FText::GetEmpty()) override;
	virtual ETrapFlavor GetTrapFlavor() const override { return IsHostile() ? ETrapFlavor::Tripwire : ETrapFlavor::Object; }

	/**
	 * Lays the wire between the ground points A and B (anchors on objects get a bracket, on the ground a peg). An armed
	 * wire (not bPreview) starts its arming delay; RiggedBy is ignored by the trigger until he leaves the wire.
	 */
	void Setup(const FVector& GroundA, const FVector& GroundB, bool bAOnObject, bool bBOnObject, bool bInPreview,
		AActor* RiggedBy = nullptr);

	/** Preview colour: cyan valid, red invalid. */
	void SetPreviewValid(bool bValid);

	/** Pulls the pin (as if someone crossed it): click, then the blast after the fuse delay. */
	void Trip(AActor* Tripper);

	bool IsArmed() const { return !bPreview && ArmingLeft <= 0.f && !bTripped; }
	bool IsTripped() const { return bTripped; }
	/** The cyan placement preview (not a real wire). */
	bool IsPreview() const { return bPreview; }
	FVector GetAnchorA() const { return WireA; }
	FVector GetAnchorB() const { return WireB; }
	/** The anchor is a bracket on an object (not a peg in the snow). */
	bool IsAnchorAOnObject() const { return bAnchorAOnObject; }
	bool IsAnchorBOnObject() const { return bAnchorBOnObject; }
	/** Seconds until it arms (0 = armed). */
	float GetArmingLeft() const { return ArmingLeft; }
	/** Save-game load (after Setup): the remaining arming time. */
	void RestoreArmingLeft(float Seconds) { ArmingLeft = FMath::Max(0.f, Seconds); }

	virtual FActionMenuRequest BuildActionMenu(const AOperativeCharacter* Leader) const override;
	virtual void ExecuteAction(AOperativeCharacter* User) override;
	virtual bool CanReceiveTrap() const override { return false; }
	virtual bool GetOverheadLabel(FOverheadLabel& OutLabel) const override;

	/** Blueprint hook for the pin click / the blast effects (no sound / VFX code in C++ yet). */
	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Tripwire", meta = (DisplayName = "On Pin Pulled"))
	void ReceivePinPulled();

private:
	void Detonate();
	void FinishDisarm();
	/** One PMC wire disarm attempt by User (mine formula); success removes the wire and hands over the grenades. */
	EDefusalResult ResolveHostileDisarm(AOperativeCharacter* User);
	/** Lays the placed wire from AnchorA / AnchorB (ground-projected) and arms it. */
	void ArmFromPlacement();
	/** Editor / pre-Setup look: wire and pegs between the local anchors. */
	void LayoutLocal(const FVector& LocalA, const FVector& LocalB);
	/** Concealment: wire / pegs drawn, cursor-clickable and the squad's path cost only while spotted. */
	void ApplyConcealment();
	void EnsureNavModifier();
	void ScanForSpotters();
	/** Body height above the feet that meets the wire (operatives / marksman by stance, others by size). */
	static float BodyHeight(const AActor& Actor);

	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Tripwire")
	TObjectPtr<UStaticMeshComponent> PegA;

	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Tripwire")
	TObjectPtr<UStaticMeshComponent> PegB;

	/** Wide box the nav modifier reads (no blocking). */
	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Tripwire")
	TObjectPtr<UBoxComponent> NavBox;

	/** Reveal: shown; with a Spotter the squad's spot response (bByPeek: found by a corner peek, the spotter keeps his pose). */
	void RevealBy(AOperativeCharacter* Spotter, bool bByPeek);

	UPROPERTY(Transient)
	TObjectPtr<UNavModifierComponent> NavModifier;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> WireMaterial;

	FVector WireA = FVector::ZeroVector;
	FVector WireB = FVector::ZeroVector;
	bool bPreview = false;
	bool bTripped = false;
	bool bAnchorAOnObject = false;
	bool bAnchorBOnObject = false;
	/** Setup ran (the wire is laid in the world). */
	bool bSetUp = false;
	bool bSpotted = false;
	bool bDisarming = false;
	float ArmingLeft = 0.f;
	/** Ignored by the trigger until they leave the wire (the rigger). */
	TArray<TWeakObjectPtr<AActor>> Ignored;
	TWeakObjectPtr<AOperativeCharacter> Disarmer;
	FTimerHandle FuseTimer;
	FTimerHandle DisarmTimer;
};

/**
 * Hostile PMC tripwire for level designers (Demo 01 G3): an ATripwireActor with Faction PMC, armed at level start and hidden
 * until spotted. Place it from Place Actors ("PMC Tripwire"), drag the AnchorA / AnchorB diamonds in the viewport (1-5 m);
 * charge / spotting / disarm values are in Details -> CodexTactics | Tripwire.
 */
UCLASS(Blueprintable, meta = (DisplayName = "PMC Tripwire"))
class CODEXTACTICS_API APMCTripwireActor : public ATripwireActor
{
	GENERATED_BODY()

public:
	APMCTripwireActor();
};
