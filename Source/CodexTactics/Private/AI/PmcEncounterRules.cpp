// PMC squads as level encounters (user request 2026-11-03; docs/systems/pmc-squad-ai.md 7). UE-only, no Godot reference.

#include "AI/PmcEncounterRules.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Math/RandomStream.h"

namespace PmcEncounterRules
{
	const TCHAR* PostureName(EPmcPosture Posture)
	{
		switch (Posture)
		{
		case EPmcPosture::Defend: return TEXT("defend");
		case EPmcPosture::Patrol: return TEXT("patrol");
		default: return TEXT("hunt");
		}
	}

	bool ParsePosture(const FString& Text, EPmcPosture& Out)
	{
		const FString Word = Text.TrimStartAndEnd().ToLower();
		if (Word == TEXT("defend") || Word == TEXT("ambush"))
		{
			Out = EPmcPosture::Defend;
			return true;
		}
		if (Word == TEXT("hunt") || Word == TEXT("attack"))
		{
			Out = EPmcPosture::Hunt;
			return true;
		}
		if (Word == TEXT("patrol"))
		{
			Out = EPmcPosture::Patrol;
			return true;
		}
		return false;
	}

	bool ParseSquad(const FJsonObject& Json, FPmcEncounterSpec& Out, FString* OutError)
	{
		auto Fail = [OutError](const FString& Why)
		{
			if (OutError)
			{
				*OutError = Why;
			}
			return false;
		};
		double Number = 0.0;
		if (Json.TryGetNumberField(TEXT("count"), Number))
		{
			if (Number < 1.0)
			{
				return Fail(TEXT("count must be >= 1"));
			}
			Out.Count = FMath::Clamp(FMath::RoundToInt(Number), 1, 6);
		}
		FString Text;
		if (Json.TryGetStringField(TEXT("weights"), Text) && !Text.TrimStartAndEnd().IsEmpty())
		{
			Out.Weights = Text.TrimStartAndEnd();
		}
		const TArray<TSharedPtr<FJsonValue>>* Spot = nullptr;
		if (Json.TryGetArrayField(TEXT("spawn"), Spot))
		{
			if (Spot->Num() < 2)
			{
				return Fail(TEXT("spawn: [x, y] or [x, y, z] in cm"));
			}
			Out.bRandomSpawn = false;
			Out.FixedSpot = FVector((*Spot)[0]->AsNumber(), (*Spot)[1]->AsNumber(), Spot->Num() > 2 ? (*Spot)[2]->AsNumber() : 0.0);
		}
		else if (Json.TryGetStringField(TEXT("spawn"), Text))
		{
			if (!Text.Equals(TEXT("random"), ESearchCase::IgnoreCase))
			{
				return Fail(FString::Printf(TEXT("spawn \"%s\": \"random\" or [x, y(, z)]"), *Text));
			}
			Out.bRandomSpawn = true;
		}
		if (Json.TryGetNumberField(TEXT("min_m"), Number))
		{
			Out.MinCm = FMath::Max(5.f, static_cast<float>(Number)) * 100.f;
		}
		if (Json.TryGetNumberField(TEXT("max_m"), Number))
		{
			Out.MaxCm = static_cast<float>(Number) * 100.f;
		}
		Out.MaxCm = FMath::Max(Out.MaxCm, Out.MinCm + 500.f);
		if (Json.TryGetNumberField(TEXT("delay_s"), Number))
		{
			Out.DelaySeconds = FMath::Max(0.f, static_cast<float>(Number));
		}
		if (Json.TryGetStringField(TEXT("posture"), Text) && !ParsePosture(Text, Out.Posture))
		{
			return Fail(FString::Printf(TEXT("posture \"%s\": defend, hunt or patrol"), *Text));
		}
		if (Json.TryGetNumberField(TEXT("seed"), Number))
		{
			Out.Seed = FMath::RoundToInt(Number);
		}
		// Smart-enemies T-11: a generator post. It replaces "spawn" (the squad stands on it, no ring); a defend post is there from the level
		// start (delay 0 whatever the entry says; a hunt post keeps its delay_s, 0 when absent); with no posture the entry is a defence.
		const TSharedPtr<FJsonObject>* PostJson = nullptr;
		if (Json.TryGetObjectField(TEXT("post"), PostJson) && PostJson && PostJson->IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* Pos = nullptr;
			if (!(*PostJson)->TryGetArrayField(TEXT("pos"), Pos) || Pos->Num() < 2)
			{
				return Fail(TEXT("post: {\"pos\": [x, y, z] in cm, \"yaw\": degrees, \"kind\": text}"));
			}
			Out.bHasPost = true;
			Out.Post.Pos = FVector((*Pos)[0]->AsNumber(), (*Pos)[1]->AsNumber(), Pos->Num() > 2 ? (*Pos)[2]->AsNumber() : 0.0);
			Out.Post.YawDeg = (*PostJson)->TryGetNumberField(TEXT("yaw"), Number) ? static_cast<float>(Number) : 0.f;
			Out.Post.Id.Reset();
			Out.Post.Kind.Reset();
			(*PostJson)->TryGetStringField(TEXT("id"), Out.Post.Id);
			(*PostJson)->TryGetStringField(TEXT("kind"), Out.Post.Kind);
			Out.bRandomSpawn = false;
			Out.FixedSpot = Out.Post.Pos;
			if (!Json.HasField(TEXT("posture")))
			{
				Out.Posture = EPmcPosture::Defend;
			}
			if (Out.Posture == EPmcPosture::Defend || !Json.HasField(TEXT("delay_s")))
			{
				Out.DelaySeconds = 0.f;
			}
		}
		return true;
	}

	TArray<FVector> PostSlots(const FPmcEncounterPost& Post, int32 Man, const FPmcPostSpawnConfig& Config)
	{
		TArray<FVector> Out;
		const FVector Forward = FRotator(0.f, Post.YawDeg, 0.f).Vector();
		// UE yaw turns clockwise seen from above: the facing's right-hand side is the facing turned 90 degrees.
		const FVector RightHand = FRotator(0.f, Post.YawDeg + 90.f, 0.f).Vector();
		const int32 Steps = (Man + 1) / 2;
		const float Side = Man % 2 == 1 ? 1.f : -1.f;
		const FVector Slot = Post.Pos + RightHand * (Side * Steps * Config.LineSpacingCm);
		for (int32 Rank = 0; Rank <= FMath::Max(0, Config.RankCount); ++Rank)
		{
			Out.Add(Slot - Forward * (Rank * Config.RankSpacingCm));
		}
		for (int32 Bearing = 0; Bearing < 8; ++Bearing)
		{
			const float Angle = (Bearing + Man * 0.5f) * (PI / 4.f);
			Out.Add(Post.Pos + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * Config.RingCm);
		}
		Out.Add(Post.Pos);
		return Out;
	}

	FRotator PostFacing(const FPmcEncounterPost& Post)
	{
		return FRotator(0.f, FRotator::NormalizeAxis(Post.YawDeg), 0.f);
	}

	FPmcPostSpawnConfig PostSpawnConfigFromJson(const FJsonObject& Json)
	{
		FPmcPostSpawnConfig Out;
		const TSharedPtr<FJsonObject>* Block = nullptr;
		if (!Json.TryGetObjectField(TEXT("post_spawn"), Block) || !Block || !Block->IsValid())
		{
			return Out;
		}
		double Number = 0.0;
		if ((*Block)->TryGetNumberField(TEXT("line_spacing_cm"), Number))
		{
			Out.LineSpacingCm = FMath::Max(0.f, static_cast<float>(Number));
		}
		if ((*Block)->TryGetNumberField(TEXT("rank_spacing_cm"), Number))
		{
			Out.RankSpacingCm = FMath::Max(0.f, static_cast<float>(Number));
		}
		if ((*Block)->TryGetNumberField(TEXT("rank_count"), Number))
		{
			Out.RankCount = FMath::Clamp(FMath::RoundToInt(Number), 0, 6);
		}
		if ((*Block)->TryGetNumberField(TEXT("ring_cm"), Number))
		{
			Out.RingCm = FMath::Max(0.f, static_cast<float>(Number));
		}
		return Out;
	}

	FPmcPostSpawnConfig LoadPostSpawnConfig(const FString& Path)
	{
		const FString File = Path.IsEmpty() ? FPaths::ProjectContentDir() / TEXT("Data/AI/pmc_operators.json") : Path;
		FString Text;
		TSharedPtr<FJsonObject> Json;
		if (!FPaths::FileExists(File) || !FFileHelper::LoadFileToString(Text, *File) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json)
			|| !Json.IsValid())
		{
			return FPmcPostSpawnConfig();
		}
		return PostSpawnConfigFromJson(*Json);
	}

	TArray<FPmcEncounterSpec> ParseSquads(const TArray<TSharedPtr<FJsonValue>>& Array, TArray<FString>* OutWarnings)
	{
		TArray<FPmcEncounterSpec> Out;
		for (int32 Index = 0; Index < Array.Num(); ++Index)
		{
			const TSharedPtr<FJsonObject> Entry = Array[Index].IsValid() ? Array[Index]->AsObject() : nullptr;
			FPmcEncounterSpec Spec;
			FString Error;
			if (!Entry.IsValid())
			{
				Error = TEXT("not an object");
			}
			else if (ParseSquad(*Entry, Spec, &Error))
			{
				Out.Add(Spec);
				continue;
			}
			if (OutWarnings)
			{
				OutWarnings->Add(FString::Printf(TEXT("pmc_squads[%d] skipped: %s"), Index, *Error));
			}
		}
		return Out;
	}

	int32 PickEncounterSpot(const TArray<FPmcSpawnCandidate>& Candidates, const FVector& Anchor, float MinCm, float MaxCm, int32 Seed,
		TFunctionRef<bool(int32 Index)> IsAcceptable, int32 MaxChecks)
	{
		struct FKeyed
		{
			int32 Index = 0;
			float Key = 0.f;
		};
		FRandomStream Stream(Seed);
		TArray<FKeyed> Keyed;
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			const float Draw = Stream.FRand(); // one draw per candidate: the keys do not depend on which ones are in range
			const float Distance = static_cast<float>(FVector::Dist2D(Candidates[Index].Location, Anchor));
			if (Distance >= MinCm && Distance <= MaxCm)
			{
				Keyed.Add({ Index, Draw + (Candidates[Index].Level > 0 ? 0.15f : 0.f) });
			}
		}
		Keyed.StableSort([](const FKeyed& Left, const FKeyed& Right) { return Left.Key > Right.Key; });
		const int32 Tries = FMath::Min(Keyed.Num(), FMath::Max(1, MaxChecks));
		for (int32 Try = 0; Try < Tries; ++Try)
		{
			if (IsAcceptable(Keyed[Try].Index))
			{
				return Keyed[Try].Index;
			}
		}
		return INDEX_NONE;
	}

	FVector HuntSearchPoint(bool bHasBelief, const FVector& Belief, int32 Leg, const FVector& PlayerStart, const FVector& SquadCentre, float JitterCm, int32 Seed)
	{
		if (bHasBelief)
		{
			return Belief;
		}
		if (Leg <= 0)
		{
			return PlayerStart;
		}
		FRandomStream Stream(Seed * 31 + Leg * 977);
		const float Angle = Stream.FRandRange(0.f, 2.f * PI);
		const float Radius = JitterCm * Stream.FRandRange(0.5f, 1.f);
		return SquadCentre + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.f);
	}

	bool ShouldResumeHunt(bool bHuntTeam, bool bEverContact, float SecondsSinceSeen, bool bAnySeenNow, float ResumeSeconds)
	{
		return bHuntTeam && bEverContact && !bAnySeenNow && SecondsSinceSeen >= ResumeSeconds;
	}

	FVector PatrolPoint(const FVector& Anchor, float RadiusCm, int32 Seed, int32 Leg)
	{
		FRandomStream Stream(Seed * 131 + Leg * 7919 + 17);
		const float Angle = Stream.FRandRange(0.f, 2.f * PI);
		const float Radius = RadiusCm * Stream.FRandRange(0.4f, 1.f);
		return Anchor + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.f);
	}
}
