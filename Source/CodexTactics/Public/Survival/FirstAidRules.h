#pragma once

#include "CoreMinimal.h"
#include "Characters/CriticalWoundRules.h"
#include "FirstAidRules.generated.h"

/** The two first-aid items (the game designer 2026-11-16): the medkit and the medical bag (the designer's new item). */
UENUM(BlueprintType)
enum class EFirstAidItem : uint8
{
	/** The medkit (AOperativeCharacter::MedkitsCount): quick, the light / medium wound on himself, the heavy one on a mate. */
	Medkit,
	/** The medical bag (AOperativeCharacter::MedicalBagsCount): more HP and more tiers healed, but the treatment takes longer. */
	MedicalBag
};

/**
 * FIRST AID BY WOUND TIER (the game designer 2026-11-16, feat/first-aid; UE-only - Godot had an instant medkit only, player.gd heal_with_item).
 * The medkit / medical bag is no longer instant: a TREATMENT plays a clip of the healing pack (/Game/HealingAnimations, IK-retargeted copies in
 * /Game/Characters/Animations/FirstAid/{Male,Female} by Scripts/Editor/setup_first_aid_animation.py) on the ABP's FullBody slot and the heal lands at
 * its "apply" moment. The designer's tiers (first_aid.json uses his words) map onto CriticalWoundRules::EWoundTier:
 *   LIGHT  = EWoundTier::Light  (wounded, small penalties)      -> he treats HIMSELF STANDING (a bandage on the arm / an injection, random clip)
 *   MEDIUM = EWoundTier::Heavy  (holds his side, limps slowly)  -> he treats HIMSELF KNEELING / SITTING
 *   HEAVY  = EWoundTier::Critical (lying, crawling)              -> he CANNOT treat himself: a mate drags the medkit / the bag onto him (his model or
 *            portrait), walks up, kneels beside him and treats him (the helper's squat / sitting clip, the patient's lying clip)
 *   NONE   = not wounded but hurt (HP under max)                -> like LIGHT (a standing bandage)
 * The tier improves by the item's "tier_steps" (the health is raised at least to the improved tier's floor) and its "heal" HP is added on top.
 * Interruptions: damage (>= "cancel_min_damage" in one tick), a move / stance order, either body moved away, the helper dead / knocked down / out of
 * reach, the patient dead. The item is consumed only at the apply moment ("consume_on_cancel": consumed when cancelled before it too).
 * Data: Content/Data/Survival/first_aid.json (GetConfig). Runtime: UFirstAidSubsystem. Tests CodexTactics.Survival.FirstAid.*; smoke
 * CodexTactics.FirstAidSmoke. Report: docs/port/FIRST_AID_REPORT.md.
 */
namespace FirstAidRules
{
	/** The designer's wound tiers (see above). */
	enum class ETier : uint8
	{
		None,
		Light,
		Medium,
		Heavy
	};

	constexpr int32 TierCount = 4;

	/** How a treatment is played. */
	enum class EMode : uint8
	{
		/** No treatment (refused). */
		None,
		/** On himself, standing (light / none). */
		SelfStanding,
		/** On himself, kneeling / sitting (medium). */
		SelfKneeling,
		/** On himself, lying (he is prone: none / light / medium - a lying clip instead of standing up for it). */
		SelfLying,
		/** A mate treats him (the helper's clip + the patient's lying clip). */
		Aid
	};

	/** Why a treatment was refused (RefusalText). */
	enum class ERefusal : uint8
	{
		None,
		/** "enabled": false - the medkit is the old instant heal. */
		Disabled,
		/** No such item in his kit. */
		NoItem,
		/** Health at max: nothing to treat. */
		FullHealth,
		/** Dead (patient or helper). */
		Dead,
		/** The HEAVY wound (lying): he cannot treat himself - a mate must do it. */
		CannotTreatSelf,
		/** The item's data has no time for this tier / way (self / aid) = not allowed. */
		TierNotAllowed,
		/** The helper is knocked down or heavily wounded himself. */
		HelperUnfit,
		/** The same operative as helper and patient through the aid path. */
		SamePerson,
		/** Busy: ladder, vault / mantle, knockdown, in cover, already treating / being treated. */
		Busy
	};

	/** Why a running treatment stopped early. */
	enum class EInterrupt : uint8
	{
		None,
		/** Helper or patient took damage (>= cancel_min_damage in one tick). */
		Damaged,
		/** A new move / stance order for the helper or the patient. */
		Order,
		/** The helper or the patient was displaced (> move_tolerance_cm) from his treatment spot (a formation move, a push). */
		Moved,
		/** The helper is no longer within reach_cm of the patient. */
		OutOfReach,
		/** The helper died. */
		HelperDied,
		/** The helper was knocked down. */
		HelperDown,
		/** The patient died. */
		PatientDied,
		/** The helper's walk to the patient failed / took longer than approach_timeout_s. */
		ApproachFailed,
		/** An actor went away (level change, destroyed) - no crash, just stop. */
		Lost
	};

	/** One item's data ("items.medkit" / "items.medical_bag"). */
	struct FItemConfig
	{
		/** "heal": HP added at the apply moment. */
		float Heal = 80.f;
		/** "tier_steps": how many tiers the wound improves at least (the health is raised to that tier's floor if the heal is not enough). */
		int32 TierSteps = 1;
		/** "self_seconds": the treatment's length on himself per tier (<= 0 / missing = not allowed on himself at that tier). */
		float SelfSeconds[TierCount] = { 3.f, 3.5f, 5.f, 0.f };
		/** "aid_seconds": the treatment's length on a mate per tier (<= 0 / missing = not allowed). */
		float AidSeconds[TierCount] = { 0.f, 0.f, 0.f, 6.f };
		/** "apply_at": the share of the treatment (0..1) at which the heal lands and the item is used up. */
		float ApplyAt = 0.8f;
		/** "max_carried": the most one operative carries (0 = no limit). */
		int32 MaxCarried = 0;
		/** "weight_kg": the inventory weight (data only - the inventory has no weight limit yet). */
		float WeightKg = 0.5f;
		/** "clips": the healing pack's clip names (the retargeted copies' names), per role. */
		TArray<FString> SelfStandingClips;
		TArray<FString> SelfKneelingClips;
		TArray<FString> SelfLyingClips;
		TArray<FString> HelperClips;
		TArray<FString> PatientClips;
	};

	/** Content/Data/Survival/first_aid.json. */
	struct CODEXTACTICS_API FFirstAidConfig
	{
		/** "enabled": the timed treatment (false = the medkit's old instant heal). */
		bool bEnabled = true;
		/** "consume_on_cancel": a treatment cancelled BEFORE the apply moment still uses the item up. */
		bool bConsumeOnCancel = false;
		/** "cancel_min_damage": the damage in one tick (HP) that interrupts a treatment (cold's per-frame loss stays under it). */
		float CancelMinDamage = 1.f;
		/** "cancel_on_damage_while_approaching": damage while the helper still walks over cancels too. */
		bool bCancelOnDamageWhileApproaching = false;
		/** "reach_cm": the helper treats within this distance of the patient (and must stay within it). */
		float ReachCm = 160.f;
		/** "approach_stop_cm": the helper walks up to this distance from the patient. */
		float ApproachStopCm = 85.f;
		/** "approach_timeout_s": the walk over gives up after this long. */
		float ApproachTimeoutSeconds = 25.f;
		/** "move_tolerance_cm": either body displaced more than this from its spot while treating = interrupted. */
		float MoveToleranceCm = 45.f;
		/** "treat_on_drop_tiers": a medkit / bag dragged onto a mate of these tiers treats him (else it is handed over; Shift = always hand over). */
		bool TreatOnDropTiers[TierCount] = { false, false, false, true };
		/** "helper_may_be_medium": a helper with a MEDIUM wound may still treat a mate (a HEAVY one never can). */
		bool bHelperMayBeMedium = true;
		/** "min_play_rate" / "max_play_rate": the clip's play rate is fitted to the treatment's length within these (a short clip loops). */
		float MinPlayRate = 0.6f;
		float MaxPlayRate = 1.6f;
		/** "blend_in_s" / "blend_out_s": the FullBody slot blends. */
		float BlendInSeconds = 0.25f;
		float BlendOutSeconds = 0.35f;
		/** "hide_weapon": the weapon in hand is hidden while treating (both hands work). */
		bool bHideWeapon = true;
		/** "start_medical_bags": medical bags an operative of this role starts with (role names: Commander, Engineer, MedicSapper, Recruit). */
		TMap<FString, int32> StartMedicalBags;
		FItemConfig Medkit;
		FItemConfig MedicalBag;

		FFirstAidConfig();

		const FItemConfig& Item(EFirstAidItem Which) const { return Which == EFirstAidItem::MedicalBag ? MedicalBag : Medkit; }
		FItemConfig& Item(EFirstAidItem Which) { return Which == EFirstAidItem::MedicalBag ? MedicalBag : Medkit; }
	};

	/** The health shares that bound the tiers (MovementConfig.WoundedHealthThreshold, critical_wound.json heavy_from / critical_from). */
	struct FTierBounds
	{
		/** Under it: LIGHT (the wounded threshold, 0.5). */
		float LightFrom = 0.5f;
		/** Under it: MEDIUM (critical_wound.json "heavy_from", 0.35). */
		float MediumFrom = 0.35f;
		/** Under it: HEAVY (critical_wound.json "critical_from", 0.2). */
		float HeavyFrom = 0.2f;
	};

	/** The designer's tier of a wound tier (Light -> Light, Heavy -> Medium, Critical -> Heavy); NoneButHurt when not wounded. */
	CODEXTACTICS_API ETier FromWoundTier(CriticalWoundRules::EWoundTier Tier);

	/** "none" / "light" / "medium" / "heavy" (the JSON keys). */
	CODEXTACTICS_API const TCHAR* TierKey(ETier Tier);

	/** A JSON tier key (case-insensitive) -> tier; false for anything else. */
	CODEXTACTICS_API bool ParseTierKey(const FString& Key, ETier& Out);

	/** "medkit" / "medical_bag" (the JSON keys, the feed / log names). */
	CODEXTACTICS_API const TCHAR* ItemKey(EFirstAidItem Item);

	/** The OnItemUsed event id: "MEDKIT" (the tutorial's medkit lesson) / "MEDICAL_BAG". */
	CODEXTACTICS_API const TCHAR* ItemEventId(EFirstAidItem Item);

	/** The self-treatment's way by tier: heavy -> None (he cannot); prone -> lying; none / light -> standing, medium -> kneeling. */
	CODEXTACTICS_API EMode ChooseSelfMode(ETier Tier, bool bProne = false);

	/** The treatment's length (s) for the item on himself / on a mate at this tier; 0 = not allowed. */
	CODEXTACTICS_API float TreatmentSeconds(const FFirstAidConfig& Config, EFirstAidItem Item, ETier Tier, bool bSelf);

	/** The apply moment (s from the start): Seconds x ApplyAt, ApplyAt clamped to [0, 1]. */
	CODEXTACTICS_API float ApplyDelay(float Seconds, float ApplyAt);

	/** Self-treatment check (everything the character can tell in one call; bBusy = ladder / vault / knockdown / cover / already treating). */
	CODEXTACTICS_API ERefusal CheckSelf(const FFirstAidConfig& Config, EFirstAidItem Item, ETier Tier, float Health, float MaxHealth, int32 ItemCount,
		bool bBusy, bool bProne = false);

	/**
	 * Aid check: the helper (alive, not knocked down, not HEAVY - MEDIUM only with helper_may_be_medium, carries the item, not busy) on the patient
	 * (alive, hurt, a tier the item has an aid time for), different operatives.
	 */
	CODEXTACTICS_API ERefusal CheckAid(const FFirstAidConfig& Config, EFirstAidItem Item, bool bSamePerson, bool bHelperAlive, bool bHelperKnockedDown,
		ETier HelperTier, int32 HelperItemCount, bool bHelperBusy, bool bPatientAlive, ETier PatientTier, float PatientHealth, float PatientMaxHealth,
		bool bPatientBusy);

	/** A medkit / bag dropped on a mate: a treatment (his tier is in treat_on_drop_tiers, no Shift) instead of a hand-over. */
	CODEXTACTICS_API bool TreatsOnDrop(const FFirstAidConfig& Config, ETier PatientTier, bool bHandOverOnly);

	/** The tier after Steps improvements (toward None). */
	CODEXTACTICS_API ETier TierAfter(ETier Tier, int32 Steps);

	/** The lowest health share of a tier (None -> LightFrom, Light -> MediumFrom, Medium -> HeavyFrom, Heavy -> 0). */
	CODEXTACTICS_API float TierFloorShare(const FTierBounds& Bounds, ETier Tier);

	/**
	 * The health after the treatment: Health + Heal, raised at least to the floor of TierAfter(Tier, TierSteps) (+ 0.5 HP: safely inside it), clamped
	 * to MaxHealth; never below Health. A dead body (Health <= 0) or no max health stays as it is.
	 */
	CODEXTACTICS_API float HealedHealth(const FItemConfig& Item, const FTierBounds& Bounds, ETier Tier, float Health, float MaxHealth);

	/** The clip set of a treatment role: the self sets by mode; the aid's helper / patient sets. */
	CODEXTACTICS_API const TArray<FString>& ClipSet(const FItemConfig& Item, EMode Mode, bool bPatient);

	/** A random clip index among Count (Random01 in [0, 1)); avoids Previous when there is a choice; INDEX_NONE when Count <= 0. */
	CODEXTACTICS_API int32 PickClipIndex(int32 Count, float Random01, int32 Previous = INDEX_NONE);

	/** The play rate that fits a clip into the treatment: ClipSeconds / Seconds clamped to [MinRate, MaxRate] (1 for a bad clip / time). */
	CODEXTACTICS_API float FitPlayRate(float ClipSeconds, float Seconds, float MinRate, float MaxRate);

	/** How many loops of the clip at Rate cover Seconds (>= 1). */
	CODEXTACTICS_API int32 LoopCount(float ClipSeconds, float Seconds, float Rate);

	/** The treatment's clock. */
	struct FClock
	{
		float Elapsed = 0.f;
		float Seconds = 0.f;
		float ApplyDelay = 0.f;
		bool bApplied = false;
	};

	/** Advances the clock; true exactly once: the step that reaches the apply moment (sets bApplied). */
	CODEXTACTICS_API bool AdvanceClock(FClock& Clock, float DeltaSeconds);

	/** The treatment has run its length (and applied). */
	CODEXTACTICS_API bool IsFinished(const FClock& Clock);

	/** What the subsystem sees this tick for CheckInterrupt. */
	struct FInterruptInput
	{
		/** The helper is still walking over (no treatment clip yet). */
		bool bApproaching = false;
		/** A self-treatment (helper == patient). */
		bool bSelf = false;
		bool bHelperValid = true;
		bool bPatientValid = true;
		bool bHelperAlive = true;
		bool bHelperKnockedDown = false;
		bool bPatientAlive = true;
		/** The health lost this tick (HP, helper / patient). */
		float HelperDamage = 0.f;
		float PatientDamage = 0.f;
		/** A move / stance order since the treatment started (or since the approach move was issued). */
		bool bHelperOrdered = false;
		bool bPatientOrdered = false;
		/** Displacement from the treatment spot (cm, treating only). */
		float HelperMovedCm = 0.f;
		float PatientMovedCm = 0.f;
		/** Helper - patient distance (cm, horizontal). */
		float DistanceCm = 0.f;
		/** The approach: its seconds so far and whether the walk ended without reaching (no own move order left, still out of reach). */
		float ApproachSeconds = 0.f;
		bool bApproachStalled = false;
	};

	/** The first reason to stop (in this order: lost, deaths, knockdown, damage, orders, approach failure, displacement, reach), else None. */
	CODEXTACTICS_API EInterrupt CheckInterrupt(const FFirstAidConfig& Config, const FInterruptInput& Input);

	/** An interrupted treatment uses the item up: never after the apply moment (it was used then), else consume_on_cancel. */
	CODEXTACTICS_API bool ConsumesOnInterrupt(const FFirstAidConfig& Config, bool bApplied);

	/** How many more of the item an operative carrying Count may take (max_carried; MAX_int32 without a limit). */
	CODEXTACTICS_API int32 RoomFor(const FFirstAidConfig& Config, EFirstAidItem Item, int32 Count);

	/** The medical bags a role starts with ("start_medical_bags"; 0 when not listed). */
	CODEXTACTICS_API int32 StartMedicalBags(const FFirstAidConfig& Config, const FString& RoleName);

	/** Feed / log names. */
	CODEXTACTICS_API const TCHAR* InterruptName(EInterrupt Reason);
	CODEXTACTICS_API const TCHAR* RefusalName(ERefusal Reason);
	CODEXTACTICS_API const TCHAR* ModeName(EMode Mode);

	/** Parses first_aid.json onto Out (missing keys keep Out's values); false when the text is not a JSON object. */
	CODEXTACTICS_API bool ParseConfig(const FString& JsonText, FFirstAidConfig& Out);

	/** Content/Data/Survival/first_aid.json. */
	CODEXTACTICS_API FString GetConfigPath();

	/** The config in force (first_aid.json read once; the code defaults when it is missing). */
	CODEXTACTICS_API const FFirstAidConfig& GetConfig();

	/** Replaces the config in force (tests / smokes); ResetConfig re-reads the file next time. */
	CODEXTACTICS_API void SetConfig(const FFirstAidConfig& Config);
	CODEXTACTICS_API void ResetConfig();
}
