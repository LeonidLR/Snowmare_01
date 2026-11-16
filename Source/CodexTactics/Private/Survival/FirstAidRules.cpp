#include "Survival/FirstAidRules.h"

#include "CodexTactics.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace FirstAidRules
{
	namespace
	{
		FFirstAidConfig& FirstAidConfigInForce()
		{
			static FFirstAidConfig Config;
			return Config;
		}
		bool bFirstAidConfigLoaded = false;

		int32 TierIndex(ETier Tier)
		{
			return FMath::Clamp(static_cast<int32>(Tier), 0, TierCount - 1);
		}

		void ReadSeconds(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, float (&Out)[TierCount])
		{
			const TSharedPtr<FJsonObject>* Map = nullptr;
			if (!Object->TryGetObjectField(Field, Map) || !Map || !Map->IsValid())
			{
				return;
			}
			// A listed map replaces the whole row: a tier left out is not allowed (0).
			for (float& Seconds : Out)
			{
				Seconds = 0.f;
			}
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Map)->Values)
			{
				ETier Tier = ETier::None;
				double Value = 0.0;
				if (ParseTierKey(Pair.Key, Tier) && Pair.Value.IsValid() && Pair.Value->TryGetNumber(Value) && FMath::IsFinite(Value))
				{
					Out[TierIndex(Tier)] = FMath::Clamp(static_cast<float>(Value), 0.f, 120.f);
				}
			}
		}

		void ReadClips(const TSharedPtr<FJsonObject>& Clips, const TCHAR* Field, TArray<FString>& Out)
		{
			const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
			if (!Clips->TryGetArrayField(Field, List) || !List)
			{
				return;
			}
			Out.Reset();
			for (const TSharedPtr<FJsonValue>& Value : *List)
			{
				FString Name;
				if (Value.IsValid() && Value->TryGetString(Name) && !Name.TrimStartAndEnd().IsEmpty())
				{
					Out.AddUnique(Name.TrimStartAndEnd());
				}
			}
		}

		void ReadItem(const TSharedPtr<FJsonObject>& Items, const TCHAR* Key, FItemConfig& Out)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Items->TryGetObjectField(Key, Object) || !Object || !Object->IsValid())
			{
				return;
			}
			const TSharedPtr<FJsonObject>& Item = *Object;
			double Value = 0.0;
			if (Item->TryGetNumberField(TEXT("heal"), Value) && FMath::IsFinite(Value)) { Out.Heal = FMath::Max(0.f, static_cast<float>(Value)); }
			if (Item->TryGetNumberField(TEXT("tier_steps"), Value) && FMath::IsFinite(Value)) { Out.TierSteps = FMath::Clamp(FMath::RoundToInt(Value), 0, 3); }
			if (Item->TryGetNumberField(TEXT("apply_at"), Value) && FMath::IsFinite(Value)) { Out.ApplyAt = FMath::Clamp(static_cast<float>(Value), 0.f, 1.f); }
			if (Item->TryGetNumberField(TEXT("max_carried"), Value) && FMath::IsFinite(Value)) { Out.MaxCarried = FMath::Max(0, FMath::RoundToInt(Value)); }
			if (Item->TryGetNumberField(TEXT("weight_kg"), Value) && FMath::IsFinite(Value)) { Out.WeightKg = FMath::Max(0.f, static_cast<float>(Value)); }
			ReadSeconds(Item, TEXT("self_seconds"), Out.SelfSeconds);
			ReadSeconds(Item, TEXT("aid_seconds"), Out.AidSeconds);
			const TSharedPtr<FJsonObject>* Clips = nullptr;
			if (Item->TryGetObjectField(TEXT("clips"), Clips) && Clips && Clips->IsValid())
			{
				ReadClips(*Clips, TEXT("self_standing"), Out.SelfStandingClips);
				ReadClips(*Clips, TEXT("self_kneeling"), Out.SelfKneelingClips);
				ReadClips(*Clips, TEXT("self_lying"), Out.SelfLyingClips);
				ReadClips(*Clips, TEXT("helper"), Out.HelperClips);
				ReadClips(*Clips, TEXT("patient"), Out.PatientClips);
			}
		}
	}

	FFirstAidConfig::FFirstAidConfig()
	{
		// The code defaults mirror Content/Data/Survival/first_aid.json (test CodexTactics.Survival.FirstAid.DataFile).
		Medkit.Heal = 80.f; // Godot player.gd heal_with_item: medkit 80 HP (PersonalItemRules)
		Medkit.TierSteps = 1;
		const float MedkitSelf[TierCount] = { 3.5f, 4.f, 5.5f, 0.f };
		const float MedkitAid[TierCount] = { 4.f, 4.f, 5.f, 6.f };
		FMemory::Memcpy(Medkit.SelfSeconds, MedkitSelf, sizeof(MedkitSelf));
		FMemory::Memcpy(Medkit.AidSeconds, MedkitAid, sizeof(MedkitAid));
		Medkit.ApplyAt = 0.8f;
		Medkit.MaxCarried = 0;
		Medkit.WeightKg = 0.5f;
		Medkit.SelfStandingClips = { TEXT("AS_BandageTreatment"), TEXT("AS_BandageTreatment2"), TEXT("AS_InjectionInTheHand"), TEXT("AS_InjectionInTheShoulder") };
		Medkit.SelfKneelingClips = { TEXT("AS_BandageTreatmentOnKnee"), TEXT("AS_BandageTreatmentOnKnee2"), TEXT("AS_InjectionInLegSquating"), TEXT("AS_HandInjectionSquating") };
		Medkit.HelperClips = { TEXT("AS_BanadageHealingFriendFast") };
		Medkit.SelfLyingClips = { TEXT("AS_InjectionInArmSitting"), TEXT("AS_HealingBandageSitting") };
		Medkit.PatientClips = { TEXT("AS_WoundedGetInjection") };

		MedicalBag.Heal = 140.f;
		MedicalBag.TierSteps = 2;
		const float BagSelf[TierCount] = { 6.f, 7.f, 9.f, 0.f };
		const float BagAid[TierCount] = { 7.f, 7.f, 9.f, 11.f };
		FMemory::Memcpy(MedicalBag.SelfSeconds, BagSelf, sizeof(BagSelf));
		FMemory::Memcpy(MedicalBag.AidSeconds, BagAid, sizeof(BagAid));
		MedicalBag.ApplyAt = 0.85f;
		MedicalBag.MaxCarried = 1;
		MedicalBag.WeightKg = 3.f;
		MedicalBag.SelfStandingClips = { TEXT("AS_BandageTreatment"), TEXT("AS_InjectionInTheLeg") };
		MedicalBag.SelfKneelingClips = { TEXT("AS_SquatLegBandage"), TEXT("AS_BandageArmSitting") };
		MedicalBag.HelperClips = { TEXT("AS_BandageHealingFriend"), TEXT("AS_InjectionHealingFriend") };
		MedicalBag.SelfLyingClips = { TEXT("AS_HealingBandageSitting"), TEXT("AS_InjectionInArmSitting") };
		MedicalBag.PatientClips = { TEXT("AS_WoundedGetInjection"), TEXT("AS_WoundedLying") };

		StartMedicalBags.Add(TEXT("MedicSapper"), 1);
	}

	ETier FromWoundTier(CriticalWoundRules::EWoundTier Tier)
	{
		switch (Tier)
		{
		case CriticalWoundRules::EWoundTier::Light: return ETier::Light;
		case CriticalWoundRules::EWoundTier::Heavy: return ETier::Medium;
		case CriticalWoundRules::EWoundTier::Critical: return ETier::Heavy;
		default: return ETier::None;
		}
	}

	const TCHAR* TierKey(ETier Tier)
	{
		switch (Tier)
		{
		case ETier::Light: return TEXT("light");
		case ETier::Medium: return TEXT("medium");
		case ETier::Heavy: return TEXT("heavy");
		default: return TEXT("none");
		}
	}

	bool ParseTierKey(const FString& Key, ETier& Out)
	{
		for (int32 Index = 0; Index < TierCount; ++Index)
		{
			if (Key.TrimStartAndEnd().Equals(TierKey(static_cast<ETier>(Index)), ESearchCase::IgnoreCase))
			{
				Out = static_cast<ETier>(Index);
				return true;
			}
		}
		return false;
	}

	const TCHAR* ItemKey(EFirstAidItem Item)
	{
		return Item == EFirstAidItem::MedicalBag ? TEXT("medical_bag") : TEXT("medkit");
	}

	const TCHAR* ItemEventId(EFirstAidItem Item)
	{
		return Item == EFirstAidItem::MedicalBag ? TEXT("MEDICAL_BAG") : TEXT("MEDKIT");
	}

	EMode ChooseSelfMode(ETier Tier, bool bProne)
	{
		if (Tier == ETier::Heavy)
		{
			return EMode::None;
		}
		if (bProne)
		{
			return EMode::SelfLying;
		}
		return Tier == ETier::Medium ? EMode::SelfKneeling : EMode::SelfStanding;
	}

	float TreatmentSeconds(const FFirstAidConfig& Config, EFirstAidItem Item, ETier Tier, bool bSelf)
	{
		const FItemConfig& Data = Config.Item(Item);
		return FMath::Max(0.f, bSelf ? Data.SelfSeconds[TierIndex(Tier)] : Data.AidSeconds[TierIndex(Tier)]);
	}

	float ApplyDelay(float Seconds, float ApplyAt)
	{
		return FMath::Max(0.f, Seconds) * FMath::Clamp(ApplyAt, 0.f, 1.f);
	}

	ERefusal CheckSelf(const FFirstAidConfig& Config, EFirstAidItem Item, ETier Tier, float Health, float MaxHealth, int32 ItemCount, bool bBusy,
		bool bProne)
	{
		if (!Config.bEnabled)
		{
			return ERefusal::Disabled;
		}
		if (Health <= 0.f || MaxHealth <= 0.f)
		{
			return ERefusal::Dead;
		}
		if (ItemCount <= 0)
		{
			return ERefusal::NoItem;
		}
		if (Health >= MaxHealth)
		{
			return ERefusal::FullHealth;
		}
		if (ChooseSelfMode(Tier, bProne) == EMode::None)
		{
			return ERefusal::CannotTreatSelf;
		}
		if (TreatmentSeconds(Config, Item, Tier, true) <= 0.f)
		{
			return ERefusal::TierNotAllowed;
		}
		return bBusy ? ERefusal::Busy : ERefusal::None;
	}

	ERefusal CheckAid(const FFirstAidConfig& Config, EFirstAidItem Item, bool bSamePerson, bool bHelperAlive, bool bHelperKnockedDown, ETier HelperTier,
		int32 HelperItemCount, bool bHelperBusy, bool bPatientAlive, ETier PatientTier, float PatientHealth, float PatientMaxHealth, bool bPatientBusy)
	{
		if (!Config.bEnabled)
		{
			return ERefusal::Disabled;
		}
		if (bSamePerson)
		{
			return ERefusal::SamePerson;
		}
		if (!bHelperAlive || !bPatientAlive || PatientHealth <= 0.f || PatientMaxHealth <= 0.f)
		{
			return ERefusal::Dead;
		}
		if (bHelperKnockedDown || HelperTier == ETier::Heavy || (HelperTier == ETier::Medium && !Config.bHelperMayBeMedium))
		{
			return ERefusal::HelperUnfit;
		}
		if (HelperItemCount <= 0)
		{
			return ERefusal::NoItem;
		}
		if (PatientHealth >= PatientMaxHealth)
		{
			return ERefusal::FullHealth;
		}
		if (TreatmentSeconds(Config, Item, PatientTier, false) <= 0.f)
		{
			return ERefusal::TierNotAllowed;
		}
		return bHelperBusy || bPatientBusy ? ERefusal::Busy : ERefusal::None;
	}

	bool TreatsOnDrop(const FFirstAidConfig& Config, ETier PatientTier, bool bHandOverOnly)
	{
		return Config.bEnabled && !bHandOverOnly && Config.TreatOnDropTiers[TierIndex(PatientTier)];
	}

	ETier TierAfter(ETier Tier, int32 Steps)
	{
		return static_cast<ETier>(FMath::Max(0, static_cast<int32>(Tier) - FMath::Max(0, Steps)));
	}

	float TierFloorShare(const FTierBounds& Bounds, ETier Tier)
	{
		switch (Tier)
		{
		case ETier::None: return Bounds.LightFrom;
		case ETier::Light: return Bounds.MediumFrom;
		case ETier::Medium: return Bounds.HeavyFrom;
		default: return 0.f;
		}
	}

	float HealedHealth(const FItemConfig& Item, const FTierBounds& Bounds, ETier Tier, float Health, float MaxHealth)
	{
		if (Health <= 0.f || MaxHealth <= 0.f)
		{
			return Health;
		}
		float Result = Health + FMath::Max(0.f, Item.Heal);
		const ETier Improved = TierAfter(Tier, Item.TierSteps);
		if (Improved != Tier)
		{
			const float Floor = TierFloorShare(Bounds, Improved) * MaxHealth;
			if (Floor > 0.f)
			{
				Result = FMath::Max(Result, Floor + 0.5f);
			}
		}
		return FMath::Max(Health, FMath::Min(Result, MaxHealth));
	}

	const TArray<FString>& ClipSet(const FItemConfig& Item, EMode Mode, bool bPatient)
	{
		static const TArray<FString> Empty;
		switch (Mode)
		{
		case EMode::SelfStanding: return Item.SelfStandingClips;
		case EMode::SelfKneeling: return Item.SelfKneelingClips;
		case EMode::SelfLying: return Item.SelfLyingClips;
		case EMode::Aid: return bPatient ? Item.PatientClips : Item.HelperClips;
		default: return Empty;
		}
	}

	int32 PickClipIndex(int32 Count, float Random01, int32 Previous)
	{
		if (Count <= 0)
		{
			return INDEX_NONE;
		}
		const float Unit = FMath::IsFinite(Random01) ? FMath::Clamp(Random01, 0.f, 0.99999f) : 0.f;
		if (Count == 1 || Previous < 0 || Previous >= Count)
		{
			return FMath::Clamp(FMath::FloorToInt(Unit * Count), 0, Count - 1);
		}
		// One of the OTHER clips: Count - 1 choices, skipping Previous.
		const int32 Pick = FMath::Clamp(FMath::FloorToInt(Unit * (Count - 1)), 0, Count - 2);
		return Pick >= Previous ? Pick + 1 : Pick;
	}

	float FitPlayRate(float ClipSeconds, float Seconds, float MinRate, float MaxRate)
	{
		if (!(ClipSeconds > 0.f) || !(Seconds > 0.f))
		{
			return 1.f;
		}
		const float Low = FMath::Max(0.05f, FMath::Min(MinRate, MaxRate));
		const float High = FMath::Max(Low, FMath::Max(MinRate, MaxRate));
		return FMath::Clamp(ClipSeconds / Seconds, Low, High);
	}

	int32 LoopCount(float ClipSeconds, float Seconds, float Rate)
	{
		if (!(ClipSeconds > 0.f) || !(Seconds > 0.f) || !(Rate > 0.f))
		{
			return 1;
		}
		return FMath::Clamp(FMath::CeilToInt(Seconds * Rate / ClipSeconds - 0.01f), 1, 50);
	}

	bool AdvanceClock(FClock& Clock, float DeltaSeconds)
	{
		Clock.Elapsed += FMath::Max(0.f, DeltaSeconds);
		if (!Clock.bApplied && Clock.Elapsed >= Clock.ApplyDelay)
		{
			Clock.bApplied = true;
			return true;
		}
		return false;
	}

	bool IsFinished(const FClock& Clock)
	{
		return Clock.bApplied && Clock.Elapsed >= Clock.Seconds;
	}

	EInterrupt CheckInterrupt(const FFirstAidConfig& Config, const FInterruptInput& Input)
	{
		if (!Input.bHelperValid || !Input.bPatientValid)
		{
			return EInterrupt::Lost;
		}
		if (!Input.bPatientAlive)
		{
			return EInterrupt::PatientDied;
		}
		if (!Input.bHelperAlive)
		{
			return EInterrupt::HelperDied;
		}
		if (Input.bHelperKnockedDown)
		{
			return EInterrupt::HelperDown;
		}
		const float MinDamage = FMath::Max(0.01f, Config.CancelMinDamage);
		const bool bDamageCounts = !Input.bApproaching || Config.bCancelOnDamageWhileApproaching;
		if (bDamageCounts && (Input.HelperDamage >= MinDamage || (!Input.bSelf && Input.PatientDamage >= MinDamage)))
		{
			return EInterrupt::Damaged;
		}
		if (Input.bHelperOrdered || (!Input.bSelf && Input.bPatientOrdered))
		{
			return EInterrupt::Order;
		}
		if (Input.bApproaching)
		{
			return Input.bApproachStalled || Input.ApproachSeconds > FMath::Max(1.f, Config.ApproachTimeoutSeconds) ? EInterrupt::ApproachFailed
				: EInterrupt::None;
		}
		const float Tolerance = FMath::Max(1.f, Config.MoveToleranceCm);
		if (Input.HelperMovedCm > Tolerance || (!Input.bSelf && Input.PatientMovedCm > Tolerance))
		{
			return EInterrupt::Moved;
		}
		if (!Input.bSelf && Input.DistanceCm > FMath::Max(Config.ReachCm, Config.ApproachStopCm) + Tolerance)
		{
			return EInterrupt::OutOfReach;
		}
		return EInterrupt::None;
	}

	bool ConsumesOnInterrupt(const FFirstAidConfig& Config, bool bApplied)
	{
		return !bApplied && Config.bConsumeOnCancel;
	}

	int32 RoomFor(const FFirstAidConfig& Config, EFirstAidItem Item, int32 Count)
	{
		const int32 Max = Config.Item(Item).MaxCarried;
		return Max <= 0 ? MAX_int32 : FMath::Max(0, Max - FMath::Max(0, Count));
	}

	int32 StartMedicalBags(const FFirstAidConfig& Config, const FString& RoleName)
	{
		for (const TPair<FString, int32>& Pair : Config.StartMedicalBags)
		{
			if (Pair.Key.Equals(RoleName, ESearchCase::IgnoreCase))
			{
				return FMath::Max(0, Pair.Value);
			}
		}
		return 0;
	}

	const TCHAR* InterruptName(EInterrupt Reason)
	{
		switch (Reason)
		{
		case EInterrupt::Damaged: return TEXT("damage");
		case EInterrupt::Order: return TEXT("a new order");
		case EInterrupt::Moved: return TEXT("moved away");
		case EInterrupt::OutOfReach: return TEXT("out of reach");
		case EInterrupt::HelperDied: return TEXT("the helper died");
		case EInterrupt::HelperDown: return TEXT("the helper was knocked down");
		case EInterrupt::PatientDied: return TEXT("the patient died");
		case EInterrupt::ApproachFailed: return TEXT("could not reach him");
		case EInterrupt::Lost: return TEXT("lost");
		default: return TEXT("none");
		}
	}

	const TCHAR* RefusalName(ERefusal Reason)
	{
		switch (Reason)
		{
		case ERefusal::Disabled: return TEXT("disabled");
		case ERefusal::NoItem: return TEXT("no item");
		case ERefusal::FullHealth: return TEXT("full health");
		case ERefusal::Dead: return TEXT("dead");
		case ERefusal::CannotTreatSelf: return TEXT("cannot treat himself");
		case ERefusal::TierNotAllowed: return TEXT("tier not allowed");
		case ERefusal::HelperUnfit: return TEXT("helper unfit");
		case ERefusal::SamePerson: return TEXT("same person");
		case ERefusal::Busy: return TEXT("busy");
		default: return TEXT("none");
		}
	}

	const TCHAR* ModeName(EMode Mode)
	{
		switch (Mode)
		{
		case EMode::SelfStanding: return TEXT("self standing");
		case EMode::SelfKneeling: return TEXT("self kneeling");
		case EMode::SelfLying: return TEXT("self lying");
		case EMode::Aid: return TEXT("aid");
		default: return TEXT("none");
		}
	}

	bool ParseConfig(const FString& JsonText, FFirstAidConfig& Out)
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			return false;
		}
		bool bFlag = false;
		double Value = 0.0;
		if (Root->TryGetBoolField(TEXT("enabled"), bFlag)) { Out.bEnabled = bFlag; }
		if (Root->TryGetBoolField(TEXT("consume_on_cancel"), bFlag)) { Out.bConsumeOnCancel = bFlag; }
		if (Root->TryGetBoolField(TEXT("cancel_on_damage_while_approaching"), bFlag)) { Out.bCancelOnDamageWhileApproaching = bFlag; }
		if (Root->TryGetBoolField(TEXT("helper_may_be_medium"), bFlag)) { Out.bHelperMayBeMedium = bFlag; }
		if (Root->TryGetBoolField(TEXT("hide_weapon"), bFlag)) { Out.bHideWeapon = bFlag; }
		auto Number = [&Root, &Value](const TCHAR* Field, float& Target, float Min, float Max)
		{
			if (Root->TryGetNumberField(Field, Value) && FMath::IsFinite(Value))
			{
				Target = FMath::Clamp(static_cast<float>(Value), Min, Max);
			}
		};
		Number(TEXT("cancel_min_damage"), Out.CancelMinDamage, 0.01f, 1000.f);
		Number(TEXT("reach_cm"), Out.ReachCm, 50.f, 1000.f);
		Number(TEXT("approach_stop_cm"), Out.ApproachStopCm, 30.f, 1000.f);
		Number(TEXT("approach_timeout_s"), Out.ApproachTimeoutSeconds, 1.f, 300.f);
		Number(TEXT("move_tolerance_cm"), Out.MoveToleranceCm, 1.f, 1000.f);
		Number(TEXT("min_play_rate"), Out.MinPlayRate, 0.05f, 5.f);
		Number(TEXT("max_play_rate"), Out.MaxPlayRate, 0.05f, 5.f);
		Number(TEXT("blend_in_s"), Out.BlendInSeconds, 0.f, 2.f);
		Number(TEXT("blend_out_s"), Out.BlendOutSeconds, 0.f, 2.f);
		const TArray<TSharedPtr<FJsonValue>>* DropTiers = nullptr;
		if (Root->TryGetArrayField(TEXT("treat_on_drop_tiers"), DropTiers) && DropTiers)
		{
			for (bool& bTier : Out.TreatOnDropTiers)
			{
				bTier = false;
			}
			for (const TSharedPtr<FJsonValue>& Entry : *DropTiers)
			{
				FString Key;
				ETier Tier = ETier::None;
				if (Entry.IsValid() && Entry->TryGetString(Key) && ParseTierKey(Key, Tier))
				{
					Out.TreatOnDropTiers[TierIndex(Tier)] = true;
				}
			}
		}
		const TSharedPtr<FJsonObject>* Bags = nullptr;
		if (Root->TryGetObjectField(TEXT("start_medical_bags"), Bags) && Bags && Bags->IsValid())
		{
			Out.StartMedicalBags.Reset();
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Bags)->Values)
			{
				if (Pair.Value.IsValid() && Pair.Value->TryGetNumber(Value) && FMath::IsFinite(Value))
				{
					Out.StartMedicalBags.Add(Pair.Key, FMath::Max(0, FMath::RoundToInt(Value)));
				}
			}
		}
		const TSharedPtr<FJsonObject>* Items = nullptr;
		if (Root->TryGetObjectField(TEXT("items"), Items) && Items && Items->IsValid())
		{
			ReadItem(*Items, TEXT("medkit"), Out.Medkit);
			ReadItem(*Items, TEXT("medical_bag"), Out.MedicalBag);
		}
		return true;
	}

	FString GetConfigPath()
	{
		return FPaths::ProjectContentDir() / TEXT("Data/Survival/first_aid.json");
	}

	const FFirstAidConfig& GetConfig()
	{
		if (!bFirstAidConfigLoaded)
		{
			bFirstAidConfigLoaded = true;
			FString Text;
			if (FFileHelper::LoadFileToString(Text, *GetConfigPath()))
			{
				FFirstAidConfig Parsed;
				if (ParseConfig(Text, Parsed))
				{
					FirstAidConfigInForce() = MoveTemp(Parsed);
				}
				else
				{
					UE_LOG(LogCodexTactics, Warning, TEXT("[FirstAid] %s is not a JSON object - the code defaults are used"), *GetConfigPath());
				}
			}
		}
		return FirstAidConfigInForce();
	}

	void SetConfig(const FFirstAidConfig& Config)
	{
		bFirstAidConfigLoaded = true;
		FirstAidConfigInForce() = Config;
	}

	void ResetConfig()
	{
		bFirstAidConfigLoaded = false;
		FirstAidConfigInForce() = FFirstAidConfig();
	}
}
