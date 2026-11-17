#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Survival/FirstAidRules.h"
#include "FirstAidSubsystem.generated.h"

class AOperativeCharacter;
class UAnimSequenceBase;

/** The outcome of one treatment (smokes, logs). */
struct CODEXTACTICS_API FFirstAidRecord
{
	FString Helper;
	FString Patient;
	EFirstAidItem Item = EFirstAidItem::Medkit;
	FirstAidRules::EMode Mode = FirstAidRules::EMode::None;
	FirstAidRules::ETier TierBefore = FirstAidRules::ETier::None;
	FirstAidRules::ETier TierAfter = FirstAidRules::ETier::None;
	/** The clips that played (empty: none found / not retargeted yet). */
	FString HelperClip;
	FString PatientClip;
	/** The planned length and the seconds it really ran (s). */
	float Seconds = 0.f;
	float Elapsed = 0.f;
	float HealthBefore = 0.f;
	float HealthAfter = 0.f;
	bool bApplied = false;
	bool bCompleted = false;
	bool bConsumed = false;
	FirstAidRules::EInterrupt Interrupt = FirstAidRules::EInterrupt::None;
	/** The helper walked over first. */
	bool bApproached = false;
	/** Frames the treatment ran with a first-aid clip on the helper's / patient's FullBody slot. */
	int32 HelperClipFrames = 0;
	int32 PatientClipFrames = 0;
	int32 Frames = 0;
};

/**
 * FIRST AID BY WOUND TIER (the game designer 2026-11-16; FirstAidRules, Content/Data/Survival/first_aid.json). Runs the timed treatments:
 * - SELF (H / the inventory's medkit or medical-bag line -> ACodexTacticsPlayerController::UseFirstAidItem): light / none = a standing clip,
 *   medium = a kneeling / sitting clip, heavy = refused ("he cannot treat himself").
 * - AID (a medkit / bag dragged from the drawer onto a HEAVY-wounded mate's model or portrait -> ACodexTacticsHUD::HandleTransferDropOnActor):
 *   the helper walks up (OrderMoveTo), faces him, the helper plays the squat / sitting help clip, the patient the lying clip.
 * The clip plays on the ABP's FullBody slot (UOperativeAnimInstance::PlayFirstAidClip), the heal lands at the item's apply moment, the item is used
 * up there; damage / an order / displacement / a death / the helper knocked down or out of reach stop it (FirstAidRules::CheckInterrupt).
 * Turn-based combat keeps the old instant medkit (it ends the turn). The squad AI medic (autonomy HealAlly) is unchanged.
 */
UCLASS()
class CODEXTACTICS_API UFirstAidSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;
	virtual TStatId GetStatId() const override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickableWhenPaused() const override { return false; }

	static UFirstAidSubsystem* Get(const UObject* WorldContext);

	/** He treats himself with Item (feed lines on refusal when bPostFeed). */
	FirstAidRules::ERefusal RequestSelf(AOperativeCharacter* Operative, EFirstAidItem Item, bool bPostFeed = true);

	/** Helper treats Patient with Item (walks up first when farther than reach_cm; feed lines on refusal when bPostFeed). */
	FirstAidRules::ERefusal RequestAid(AOperativeCharacter* Helper, AOperativeCharacter* Patient, EFirstAidItem Item, bool bPostFeed = true);

	/** A medkit / bag dragged onto Patient treats him (tier listed, Shift not held, HP below max) instead of a hand-over. */
	bool ShouldTreatOnDrop(const AOperativeCharacter* Patient, bool bHandOverOnly) const;

	/** Stops the treatment Operative takes part in (helper or patient); false when there is none. */
	bool Cancel(const AOperativeCharacter* Operative, FirstAidRules::EInterrupt Reason);

	/** Operative treats or is treated now (walking over counts). */
	bool IsInTreatment(const AOperativeCharacter* Operative) const;

	/** The designer's tier of an operative now. */
	static FirstAidRules::ETier TierOf(const AOperativeCharacter& Operative);

	/** The health bounds of the tiers for an operative (his wounded threshold, critical_wound.json). */
	static FirstAidRules::FTierBounds BoundsOf(const AOperativeCharacter& Operative);

	/** His count of Item (MedkitsCount / MedicalBagsCount). */
	static int32 CountOf(const AOperativeCharacter& Operative, EFirstAidItem Item);

	/** The retargeted copy of a pack clip for this body (/Game/Characters/Animations/FirstAid/{Male,Female}/<Name>); null when missing. */
	static UAnimSequenceBase* ResolveClip(const AOperativeCharacter& Operative, const FString& Name);

	/** Every clip path first_aid.json names, both bodies (the preload catalog). */
	static void GatherClipPaths(TArray<FSoftObjectPath>& OutPaths);

	/** Finished / interrupted treatments, oldest first (smokes). */
	const TArray<FFirstAidRecord>& GetRecords() const { return Records; }

	/** The running treatment's record of Operative (null when none). */
	const FFirstAidRecord* FindRunning(const AOperativeCharacter* Operative) const;

private:
	struct FTreatment
	{
		TWeakObjectPtr<AOperativeCharacter> Helper;
		TWeakObjectPtr<AOperativeCharacter> Patient;
		EFirstAidItem Item = EFirstAidItem::Medkit;
		FirstAidRules::EMode Mode = FirstAidRules::EMode::None;
		FirstAidRules::ETier Tier = FirstAidRules::ETier::None;
		bool bApproaching = false;
		float ApproachSeconds = 0.f;
		FirstAidRules::FClock Clock;
		FVector HelperSpot = FVector::ZeroVector;
		FVector PatientSpot = FVector::ZeroVector;
		float HelperHealth = 0.f;
		float PatientHealth = 0.f;
		uint32 HelperOrderSerial = 0;
		uint32 PatientOrderSerial = 0;
		uint8 HelperStance = 0;
		uint8 PatientStance = 0;
		TWeakObjectPtr<UAnimSequenceBase> HelperClip;
		TWeakObjectPtr<UAnimSequenceBase> PatientClip;
		int32 HelperRestarts = 0;
		int32 PatientRestarts = 0;
		FFirstAidRecord Record;
	};

	FirstAidRules::ERefusal Refuse(AOperativeCharacter* Who, FirstAidRules::ERefusal Reason, const AOperativeCharacter* Patient, EFirstAidItem Item,
		bool bPostFeed) const;
	bool IsBusy(const AOperativeCharacter& Operative) const;
	void BeginTreating(FTreatment& Treatment);
	void PlayClips(FTreatment& Treatment, bool bRestart, bool bHelperSide = true, bool bPatientSide = true);
	void ApplyEffect(FTreatment& Treatment);
	void Finish(int32 Index, FirstAidRules::EInterrupt Reason);
	void Post(const AOperativeCharacter* Speaker, const FString& Text) const;

	TArray<FTreatment> Treatments;
	TArray<FFirstAidRecord> Records;
	/** The last clip index per (set pointer) so a repeat picks another clip. */
	TMap<const void*, int32> LastClipIndex;
};
