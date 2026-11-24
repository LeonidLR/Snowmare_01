// Copyright CodexTactics. Unreal-only feature (designer 2026-11-22, docs/design/INDOOR_COVER_ANIMATIONS.md).

#include "Characters/CoverAnimSetRules.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	void ReadStringMap(const FJsonObject& Object, TMap<FString, FString>& Out)
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : Object.Values)
		{
			FString Value;
			if (!Entry.Key.StartsWith(TEXT("_")) && Entry.Value.IsValid() && Entry.Value->TryGetString(Value))
			{
				Out.Add(Entry.Key, Value);
			}
		}
	}
}

bool CoverAnimSetRules::ParseConfig(const FString& JsonText, FCoverAnimSetConfig& OutConfig, FString& OutError)
{
	OutConfig = FCoverAnimSetConfig();
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("not a JSON object");
		return false;
	}
	bool bEnabled = false;
	if (Root->TryGetBoolField(TEXT("enabled"), bEnabled))
	{
		OutConfig.bEnabled = bEnabled;
	}
	double MinCm = 0.0;
	if (Root->TryGetNumberField(TEXT("shimmy_transition_min_cm"), MinCm) && MinCm >= 0.0)
	{
		OutConfig.ShimmyTransitionMinCm = static_cast<float>(MinCm);
	}
	const TSharedPtr<FJsonObject>* Contexts = nullptr;
	if (Root->TryGetObjectField(TEXT("contexts"), Contexts) && Contexts && Contexts->IsValid())
	{
		ReadStringMap(**Contexts, OutConfig.Contexts);
	}
	const TSharedPtr<FJsonObject>* Operatives = nullptr;
	if (Root->TryGetObjectField(TEXT("operatives"), Operatives) && Operatives && Operatives->IsValid())
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Operatives)->Values)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Entry.Key.StartsWith(TEXT("_")) && Entry.Value.IsValid() && Entry.Value->TryGetObject(Object) && Object && Object->IsValid())
			{
				ReadStringMap(**Object, OutConfig.Operatives.Add(Entry.Key));
			}
		}
	}
	const TSharedPtr<FJsonObject>* Sets = nullptr;
	if (Root->TryGetObjectField(TEXT("sets"), Sets) && Sets && Sets->IsValid())
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& SetEntry : (*Sets)->Values)
		{
			const TSharedPtr<FJsonObject>* SetObject = nullptr;
			if (SetEntry.Key.StartsWith(TEXT("_")) || !SetEntry.Value.IsValid() || !SetEntry.Value->TryGetObject(SetObject) || !SetObject || !SetObject->IsValid())
			{
				continue;
			}
			FCoverAnimSetData& Set = OutConfig.Sets.Add(SetEntry.Key);
			const TSharedPtr<FJsonObject>* Bodies = nullptr;
			if ((*SetObject)->TryGetObjectField(TEXT("bodies"), Bodies) && Bodies && Bodies->IsValid())
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& BodyEntry : (*Bodies)->Values)
				{
					const TSharedPtr<FJsonObject>* Clips = nullptr;
					if (BodyEntry.Value.IsValid() && BodyEntry.Value->TryGetObject(Clips) && Clips && Clips->IsValid())
					{
						ReadStringMap(**Clips, Set.Bodies.Add(BodyEntry.Key));
					}
				}
			}
			auto ReadNumbers = [&SetObject](const TCHAR* Field, TMap<FString, float>& Out)
			{
				const TSharedPtr<FJsonObject>* Numbers = nullptr;
				if ((*SetObject)->TryGetObjectField(Field, Numbers) && Numbers && Numbers->IsValid())
				{
					for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Numbers)->Values)
					{
						double Value = 0.0;
						if (Entry.Value.IsValid() && Entry.Value->TryGetNumber(Value) && Value > 0.0)
						{
							Out.Add(Entry.Key, static_cast<float>(Value));
						}
					}
				}
			};
			ReadNumbers(TEXT("grenade_release_s"), Set.GrenadeReleaseSeconds);
			ReadNumbers(TEXT("shimmy_clip_speed"), Set.ShimmyClipSpeed);
		}
	}
	return true;
}

const TCHAR* CoverAnimSetRules::ContextName(ECoverAnimContext Context)
{
	switch (Context)
	{
	case ECoverAnimContext::Indoor: return TEXT("indoor");
	case ECoverAnimContext::Stealth: return TEXT("stealth");
	default: return TEXT("outdoor");
	}
}

ECoverAnimContext CoverAnimSetRules::DecideContext(bool bInBuilding, bool bStealth)
{
	return bStealth ? ECoverAnimContext::Stealth : bInBuilding ? ECoverAnimContext::Indoor : ECoverAnimContext::Outdoor;
}

FString CoverAnimSetRules::ChooseSet(const FCoverAnimSetConfig& Config, ECoverAnimContext Context, const TArray<FString>& OperativeKeys,
	bool bRiffleOutdoor)
{
	if (!Config.bEnabled)
	{
		return FString();
	}
	if (bRiffleOutdoor && Context == ECoverAnimContext::Outdoor && Config.Sets.Contains(TEXT("riffle")))
	{
		return TEXT("riffle");
	}
	const FString ContextKey = ContextName(Context);
	const FString* Chosen = nullptr;
	for (const FString& Key : OperativeKeys)
	{
		if (const TMap<FString, FString>* Own = Key.IsEmpty() ? nullptr : Config.Operatives.Find(Key))
		{
			Chosen = Own->Find(ContextKey);
			if (Chosen)
			{
				break;
			}
		}
	}
	if (!Chosen)
	{
		Chosen = Config.Contexts.Find(ContextKey);
	}
	if (!Chosen || Chosen->IsEmpty() || Chosen->Equals(NativeSetName, ESearchCase::IgnoreCase) || !Config.Sets.Contains(*Chosen))
	{
		return FString();
	}
	return *Chosen;
}

const TMap<FString, FString>* CoverAnimSetRules::FindBodyClips(const FCoverAnimSetConfig& Config, const FString& Set, const FString& SkeletonName)
{
	const FCoverAnimSetData* Data = Config.Sets.Find(Set);
	if (!Data)
	{
		return nullptr;
	}
	if (const TMap<FString, FString>* Own = SkeletonName.IsEmpty() ? nullptr : Data->Bodies.Find(SkeletonName))
	{
		return Own;
	}
	return Data->Bodies.Find(TEXT("default"));
}

FString CoverAnimSetRules::ToObjectPath(const FString& Path)
{
	FString Package = Path;
	int32 Slash = INDEX_NONE;
	Package.FindLastChar(TEXT('/'), Slash);
	if (Path.IsEmpty() || Path.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromEnd) > Slash)
	{
		return Path;
	}
	return Path + TEXT(".") + Path.Mid(Slash + 1);
}

FString CoverAnimSetRules::SideKey(const FString& Base, ECoverFacing Facing)
{
	return Base + (Facing == ECoverFacing::Right ? TEXT("_right") : TEXT("_left"));
}

ECoverFireSet CoverAnimSetRules::DecideFireSet(bool bCrouching, ECoverHeight Height, bool bAtCorner, bool bSetHasOverClips)
{
	return bSetHasOverClips && bCrouching && Height == ECoverHeight::LowCover && !bAtCorner ? ECoverFireSet::Over : ECoverFireSet::Side;
}

ECoverOverTopPhase CoverAnimSetRules::AdvanceOverTopPhase(ECoverOverTopPhase Phase, ECoverOverTopEvent Event)
{
	if (Event == ECoverOverTopEvent::Cancelled)
	{
		return ECoverOverTopPhase::Idle;
	}
	switch (Phase)
	{
	case ECoverOverTopPhase::Idle:
		return Event == ECoverOverTopEvent::SwitchInStarted ? ECoverOverTopPhase::SwitchingIn : Phase;
	case ECoverOverTopPhase::SwitchingIn:
		return Event == ECoverOverTopEvent::FireStarted ? ECoverOverTopPhase::Firing : Phase;
	case ECoverOverTopPhase::Firing:
		return Event == ECoverOverTopEvent::FireEnded ? ECoverOverTopPhase::SwitchingOut : Phase;
	case ECoverOverTopPhase::SwitchingOut:
		return Event == ECoverOverTopEvent::SwitchOutFinished ? ECoverOverTopPhase::Idle : Phase;
	}
	return ECoverOverTopPhase::Idle;
}

UAnimSequenceBase* CoverAnimSetRules::ResolveFireClip(ECoverFireSet FireSet, UAnimSequenceBase* Over,
	UAnimSequenceBase* Side, UAnimSequenceBase* Native)
{
	if (FireSet == ECoverFireSet::Over && Over)
	{
		return Over;
	}
	return Side ? Side : Native;
}

bool CoverAnimSetRules::UseShimmyTransitions(float WalkCm, float MinCm, bool bHasClip)
{
	return bHasClip && WalkCm >= MinCm;
}

bool CoverAnimSetRules::ShouldStartShimmyStop(float RemainingCm, float SpeedCmPerSecond, float StopClipSeconds)
{
	return RemainingCm <= FMath::Max(SpeedCmPerSecond, 0.f) * FMath::Max(StopClipSeconds, 0.f);
}
