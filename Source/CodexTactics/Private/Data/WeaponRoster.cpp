#include "Data/WeaponRoster.h"

#include "Data/WeaponTuning.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace WeaponRosterInternal
{
	static FWeaponRoster& RosterInForce()
	{
		static FWeaponRoster Roster;
		return Roster;
	}

	static bool bRosterLoaded = false;

	void ReadRosterList(const FJsonObject& Block, const TCHAR* Key, TArray<FString>& Out, TArray<FString>* OutErrors)
	{
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (!Block.TryGetArrayField(Key, Array) || !Array)
		{
			return;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Array)
		{
			FString Id;
			if (Value.IsValid() && Value->Type == EJson::String && Value->TryGetString(Id) && !Id.IsEmpty())
			{
				Out.AddUnique(Id);
			}
			else if (OutErrors)
			{
				OutErrors->Add(FString::Printf(TEXT("roster.%s: every entry must be a weapon id string"), Key));
			}
		}
	}

	/** The project file's roster, read once if nothing set it yet (unit tests, tools before StartPlay). */
	void EnsureRosterLoaded()
	{
		if (bRosterLoaded)
		{
			return;
		}
		bRosterLoaded = true;
		FString Text;
		TSharedPtr<FJsonObject> Root;
		const TSharedPtr<FJsonObject>* Block = nullptr;
		if (FFileHelper::LoadFileToString(Text, *WeaponTuning::GetDefaultPath()) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root)
			&& Root.IsValid() && Root->TryGetObjectField(TEXT("roster"), Block) && Block && Block->IsValid())
		{
			WeaponRoster::Parse(**Block, RosterInForce());
		}
	}
}

namespace WeaponRoster
{
	bool Parse(const FJsonObject& RosterBlock, FWeaponRoster& OutRoster, TArray<FString>* OutErrors)
	{
		const int32 Before = OutErrors ? OutErrors->Num() : 0;
		OutRoster = FWeaponRoster();
		WeaponRosterInternal::ReadRosterList(RosterBlock, TEXT("in_play"), OutRoster.InPlay, OutErrors);
		WeaponRosterInternal::ReadRosterList(RosterBlock, TEXT("next"), OutRoster.Next, OutErrors);
		WeaponRosterInternal::ReadRosterList(RosterBlock, TEXT("out"), OutRoster.Out, OutErrors);
		for (const FString& Id : OutRoster.Out)
		{
			if (OutErrors && (OutRoster.InPlay.Contains(Id) || OutRoster.Next.Contains(Id)))
			{
				OutErrors->Add(FString::Printf(TEXT("roster: \"%s\" is both out and in play / next"), *Id));
			}
		}
		return !OutErrors || OutErrors->Num() == Before;
	}

	void Set(const FWeaponRoster& Roster)
	{
		WeaponRosterInternal::bRosterLoaded = true;
		WeaponRosterInternal::RosterInForce() = Roster;
	}

	const FWeaponRoster& Get()
	{
		WeaponRosterInternal::EnsureRosterLoaded();
		return WeaponRosterInternal::RosterInForce();
	}

	bool IsOutOfPlay(const FString& WeaponId)
	{
		return !WeaponId.IsEmpty() && Get().Out.Contains(WeaponId);
	}

	FString FallbackWeaponId()
	{
		const FWeaponRoster& Roster = Get();
		return Roster.InPlay.IsEmpty() ? FString(TEXT("m16")) : Roster.InPlay[0];
	}
}
