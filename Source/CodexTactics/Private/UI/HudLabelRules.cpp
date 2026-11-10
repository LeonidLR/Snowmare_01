#include "UI/HudLabelRules.h"

#include "CodexTactics.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace HudLabelRules
{
	namespace
	{
		FHudLabelConfig& ConfigInForce()
		{
			static FHudLabelConfig Config;
			return Config;
		}
		bool bConfigLoaded = false;
		/** The file's timestamp at the last read (FDateTime::MinValue = none) and the real time of the last poll. */
		FDateTime LoadedStamp = FDateTime::MinValue();
		double LastPollSeconds = -1.0;

		bool LoadFromDisk()
		{
			const FString Path = GetConfigPath();
			LoadedStamp = IFileManager::Get().GetTimeStamp(*Path);
			FString Text;
			FHudLabelConfig Loaded;
			TArray<FString> Unknown;
			if (!FFileHelper::LoadFileToString(Text, *Path))
			{
				UE_LOG(LogCodexTactics, Display, TEXT("HUD labels: %s missing - every label at its default (on)"), *Path);
				ConfigInForce() = Loaded;
				return false;
			}
			if (!ParseConfig(Text, Loaded, &Unknown))
			{
				UE_LOG(LogCodexTactics, Warning, TEXT("HUD labels: %s unreadable or not schema %s - every label at its default (on)"), *Path, SchemaId);
				ConfigInForce() = FHudLabelConfig();
				return false;
			}
			TArray<FString> Off;
			for (const FLabelKey& Key : GetKeys())
			{
				if (!Loaded.IsShown(Key.Label))
				{
					Off.Add(Key.Key);
				}
			}
			UE_LOG(LogCodexTactics, Display, TEXT("HUD labels: %s read - off: %s%s"), *Path, Off.IsEmpty() ? TEXT("none") : *FString::Join(Off, TEXT(", ")),
				Unknown.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (unknown keys ignored: %s)"), *FString::Join(Unknown, TEXT(", "))));
			ConfigInForce() = Loaded;
			return true;
		}
	}

	const TArray<FLabelKey>& GetKeys()
	{
		// The order = EHudLabel's. Every default is on: the HUD as it was before the switches (2026-11-10).
		static const TArray<FLabelKey> Keys = {
			{ EHudLabel::OperativeName, TEXT("operative_name"), true },
			{ EHudLabel::OperativeStance, TEXT("operative_stance"), true },
			{ EHudLabel::FrostbiteTag, TEXT("frostbite_tag"), true },
			{ EHudLabel::WoundTag, TEXT("wound_tag"), true },
			{ EHudLabel::CriticalTag, TEXT("critical_tag"), true },
			{ EHudLabel::PanicBadge, TEXT("panic_badge"), true },
			{ EHudLabel::RageBadge, TEXT("rage_badge"), true },
			{ EHudLabel::PostureLetters, TEXT("posture_letters"), true },
			{ EHudLabel::KnockdownBadges, TEXT("knockdown_badges"), true },
			{ EHudLabel::SuppressionBadges, TEXT("suppression_badges"), true },
			{ EHudLabel::RecruitPrompt, TEXT("recruit_prompt"), true },
			{ EHudLabel::EnemyNames, TEXT("enemy_names"), true },
			{ EHudLabel::EnemyStatus, TEXT("enemy_status"), true },
			{ EHudLabel::EnemyHealth, TEXT("enemy_health"), true },
			{ EHudLabel::EnemyArmorMarker, TEXT("enemy_armor_marker"), true },
			{ EHudLabel::EnemyAlertMarkers, TEXT("enemy_alert_markers"), true },
			{ EHudLabel::InteractableLabels, TEXT("interactable_labels"), true },
			{ EHudLabel::GateLabels, TEXT("gate_labels"), true },
			{ EHudLabel::VehicleHullLabels, TEXT("vehicle_hull_labels"), true },
			{ EHudLabel::FloatingTexts, TEXT("floating_texts"), true },
			{ EHudLabel::CoverTexts, TEXT("cover_texts"), true },
			{ EHudLabel::DefenseMarkers, TEXT("defense_markers"), true },
			{ EHudLabel::HitChance, TEXT("hit_chance"), true },
			{ EHudLabel::HordeWarning, TEXT("horde_warning"), true },
			{ EHudLabel::CombatModeBadge, TEXT("combat_mode_badge"), true },
			{ EHudLabel::SquadStatusPanel, TEXT("squad_status_panel"), true },
		};
		static_assert(LabelCount == 26, "GetKeys lists every EHudLabel");
		return Keys;
	}

	const TCHAR* GetKey(EHudLabel Label)
	{
		const int32 Index = static_cast<int32>(Label);
		return GetKeys().IsValidIndex(Index) ? GetKeys()[Index].Key : TEXT("");
	}

	FHudLabelConfig::FHudLabelConfig()
	{
		for (const FLabelKey& Key : GetKeys())
		{
			Shown[static_cast<int32>(Key.Label)] = Key.bDefaultShow;
		}
	}

	bool FHudLabelConfig::IsEnemyPlateDefault() const
	{
		const FHudLabelConfig Defaults;
		for (const EHudLabel Label : { EHudLabel::EnemyNames, EHudLabel::EnemyStatus, EHudLabel::EnemyHealth, EHudLabel::EnemyArmorMarker })
		{
			if (IsShown(Label) != Defaults.IsShown(Label))
			{
				return false;
			}
		}
		return true;
	}

	bool ParseConfig(const FString& JsonText, FHudLabelConfig& Out, TArray<FString>* OutUnknown)
	{
		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonText), Root) || !Root.IsValid())
		{
			return false;
		}
		FString Schema;
		if (!Root->TryGetStringField(TEXT("schema"), Schema) || Schema != SchemaId)
		{
			return false;
		}
		FHudLabelConfig Parsed = Out;
		const TSharedPtr<FJsonObject>* Labels = nullptr;
		if (Root->TryGetObjectField(TEXT("labels"), Labels) && Labels)
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Labels)->Values)
			{
				const FLabelKey* Key = GetKeys().FindByPredicate([&Entry](const FLabelKey& Each) { return Entry.Key.Equals(Each.Key, ESearchCase::IgnoreCase); });
				if (!Key)
				{
					if (OutUnknown)
					{
						OutUnknown->Add(Entry.Key);
					}
					continue;
				}
				// Strict: only a JSON bool switches (FJsonValue::TryGetBool would read a number / "true" string too).
				TSharedPtr<FJsonValue> Show = Entry.Value;
				if (Show.IsValid() && Show->Type == EJson::Object)
				{
					Show = Show->AsObject()->TryGetField(TEXT("show"));
				}
				if (Show.IsValid() && Show->Type == EJson::Boolean)
				{
					Parsed.SetShown(Key->Label, Show->AsBool()); // { "show": bool, ... } or the short form "key": bool
				}
			}
		}
		Out = Parsed;
		return true;
	}

	FHudLabelConfig ApplyWoundTagOverride(const FHudLabelConfig& Config, int32 Override)
	{
		FHudLabelConfig Out = Config;
		if (Override >= 0)
		{
			Out.SetShown(EHudLabel::WoundTag, Override > 0);
			Out.SetShown(EHudLabel::CriticalTag, Override > 0);
		}
		return Out;
	}

	FString WoundTagText(const FHudLabelConfig& Config, CriticalWoundRules::EWoundTier Tier)
	{
		using CriticalWoundRules::EWoundTier;
		switch (Tier)
		{
		case EWoundTier::Critical:
			return Config.IsShown(EHudLabel::CriticalTag) ? TEXT("CRITICAL") : TEXT("");
		case EWoundTier::Heavy:
			return Config.IsShown(EHudLabel::WoundTag) ? TEXT("BADLY WOUNDED") : TEXT("");
		case EWoundTier::Light:
			return Config.IsShown(EHudLabel::WoundTag) ? TEXT("WOUNDED") : TEXT("");
		default:
			return FString();
		}
	}

	FString ComposeOperativePlate(const FHudLabelConfig& Config, const FString& Name, const FString& Stance, bool bFrostbitten,
		CriticalWoundRules::EWoundTier Tier)
	{
		TArray<FString> Parts;
		if (Config.IsShown(EHudLabel::OperativeName) && !Name.IsEmpty())
		{
			Parts.Add(Name);
		}
		if (Config.IsShown(EHudLabel::OperativeStance) && !Stance.IsEmpty())
		{
			Parts.Add(Stance);
		}
		if (bFrostbitten && Config.IsShown(EHudLabel::FrostbiteTag))
		{
			Parts.Add(TEXT("FROSTBITTEN"));
		}
		const FString Wound = WoundTagText(Config, Tier);
		if (!Wound.IsEmpty())
		{
			Parts.Add(Wound);
		}
		return FString::Join(Parts, TEXT(" · "));
	}

	FString ComposeEnemyLabel(const FHudLabelConfig& Config, const FString& Name, const FString& Status, int32 Health, int32 MaxHealth)
	{
		FString First = Config.IsShown(EHudLabel::EnemyNames) ? Name : FString();
		if (Config.IsShown(EHudLabel::EnemyStatus))
		{
			First += Status;
		}
		First.TrimStartAndEndInline();
		const FString Second = Config.IsShown(EHudLabel::EnemyHealth) ? FString::Printf(TEXT("%d/%d"), Health, MaxHealth) : FString();
		if (First.IsEmpty())
		{
			return Second;
		}
		return Second.IsEmpty() ? First : FString::Printf(TEXT("%s\n%s"), *First, *Second);
	}

	bool IsCoverText(const FString& Text)
	{
		return Text.Contains(TEXT("IN COVER"), ESearchCase::CaseSensitive);
	}

	FString GetConfigPath()
	{
		return FPaths::ProjectContentDir() / TEXT("Data/UI/hud_labels.json");
	}

	const FHudLabelConfig& GetConfig()
	{
		if (!bConfigLoaded)
		{
			bConfigLoaded = true;
			LoadFromDisk();
		}
		return ConfigInForce();
	}

	bool ReloadConfig()
	{
		bConfigLoaded = true;
		return LoadFromDisk();
	}

	bool PollConfigFile(double NowSeconds, double PollSeconds)
	{
#if UE_BUILD_SHIPPING
		return false;
#else
		if (!bConfigLoaded)
		{
			GetConfig();
			LastPollSeconds = NowSeconds;
			return false;
		}
		if (LastPollSeconds >= 0.0 && NowSeconds >= LastPollSeconds && NowSeconds - LastPollSeconds < PollSeconds)
		{
			return false;
		}
		LastPollSeconds = NowSeconds;
		if (IFileManager::Get().GetTimeStamp(*GetConfigPath()) == LoadedStamp)
		{
			return false;
		}
		LoadFromDisk(); // edited (the Training Ground panel, a text editor): the switches change at once, no restart
		return true;
#endif
	}

	void SetConfig(const FHudLabelConfig& Config)
	{
		bConfigLoaded = true;
		ConfigInForce() = Config;
		LoadedStamp = IFileManager::Get().GetTimeStamp(*GetConfigPath());
	}
}
