#include "Training/TrainingSectorRules.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Math/RandomStream.h"

namespace TrainingSectorParse
{
	const FJsonObject* ObjectField(const FJsonObject& Json, const TCHAR* Key)
	{
		const TSharedPtr<FJsonObject>* Value = nullptr;
		return Json.TryGetObjectField(Key, Value) && Value ? Value->Get() : nullptr;
	}

	bool ReadSectorNumber(const FJsonObject& Json, const TCHAR* Key, double& Out)
	{
		return Json.TryGetNumberField(Key, Out) && FMath::IsFinite(Out);
	}

	bool VectorField(const FJsonObject& Json, const TCHAR* Key, int32 Dimensions, FVector& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Json.TryGetArrayField(Key, Values) || !Values || Values->Num() != Dimensions)
		{
			return false;
		}
		Out = FVector::ZeroVector;
		for (int32 Axis = 0; Axis < Dimensions; ++Axis)
		{
			const TSharedPtr<FJsonValue>& Value = (*Values)[Axis];
			if (!Value || !Value->TryGetNumber(Out[Axis]) || !FMath::IsFinite(Out[Axis]))
			{
				return false;
			}
		}
		return true;
	}

	TOptional<int32> NumberId(const FJsonObject& Json, const TCHAR* Key)
	{
		double Id = 0.0;
		if (!ReadSectorNumber(Json, Key, Id) || Id < 0.0 || Id > 6.0 || Id != static_cast<int32>(Id))
		{
			return {};
		}
		return static_cast<int32>(Id);
	}

	bool ReadLocation(const FJsonObject& Json, const FTrainingSector& Sector, FTrainingSectorStart& Out)
	{
		return VectorField(Json, TEXT("pos"), 3, Out.Position)
			&& TrainingSectorRules::Contains(Sector.Area, Out.Position)
			&& FMath::IsWithinInclusive(Out.Position.Z, Sector.Heights.X, Sector.Heights.Y)
			&& ReadSectorNumber(Json, TEXT("yaw"), Out.Yaw);
	}

	const TArray<TSharedPtr<FJsonValue>>* CandidateList(const FJsonObject& Json, const TCHAR* Key)
	{
		const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
		return Json.TryGetArrayField(Key, List) && List && FMath::IsWithinInclusive(List->Num(), 2, 3) ? List : nullptr;
	}

	bool ReadSector(const FJsonObject& Json, FTrainingSector& Out, FString& Error)
	{
		Out.Id = NumberId(Json, TEXT("id")).Get(0);
		const FJsonObject* Box = ObjectField(Json, TEXT("box_cm"));
		const FJsonObject* Z = ObjectField(Json, TEXT("z_cm"));
		FVector Min;
		FVector Max;
		if (!Out.Id || !Box || !Z || !VectorField(*Box, TEXT("min"), 2, Min) || !VectorField(*Box, TEXT("max"), 2, Max)
			|| !FMath::IsFinite(Max.X - Min.X) || !FMath::IsFinite(Max.Y - Min.Y)
			|| Max.X - Min.X < TrainingSectorRules::MinSizeCm || Max.Y - Min.Y < TrainingSectorRules::MinSizeCm
			|| !ReadSectorNumber(*Z, TEXT("min"), Out.Heights.X) || !ReadSectorNumber(*Z, TEXT("max"), Out.Heights.Y) || Out.Heights.X >= Out.Heights.Y)
		{
			Error = TEXT("expected an id 1–6, a box at least 60 m on each side and a height interval");
			return false;
		}
		Out.Area = FBox2D(FVector2D(Min.X, Min.Y), FVector2D(Max.X, Max.Y));
		const TArray<TSharedPtr<FJsonValue>>* Starts = CandidateList(Json, TEXT("starts"));
		const TArray<TSharedPtr<FJsonValue>>* Posts = CandidateList(Json, TEXT("pmc_posts"));
		const FJsonObject* Squad = ObjectField(Json, TEXT("pmc_squad"));
		if (!Starts || !Posts || !Squad)
		{
			Error = TEXT("expected two or three starts and posts, and a PMC squad");
			return false;
		}
		if (!PmcEncounterRules::ParseSquad(*Squad, Out.Squad, &Error))
		{
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Entry : *Starts)
		{
			const TSharedPtr<FJsonObject>* Start = nullptr;
			FTrainingSectorStart Candidate;
			if (!Entry || !Entry->TryGetObject(Start) || !Start || !Start->IsValid() || !ReadLocation(**Start, Out, Candidate))
			{
				Error = TEXT("start requires pos and yaw within sector bounds");
				return false;
			}
			Out.Starts.Add(Candidate);
		}
		for (const TSharedPtr<FJsonValue>& Entry : *Posts)
		{
			const TSharedPtr<FJsonObject>* Post = nullptr;
			FTrainingSectorStart Location;
			FPmcEncounterPost Candidate;
			if (!Entry || !Entry->TryGetObject(Post) || !Post || !Post->IsValid() || !ReadLocation(**Post, Out, Location)
				|| !(*Post)->TryGetStringField(TEXT("id"), Candidate.Id) || Candidate.Id.IsEmpty()
				|| !(*Post)->TryGetStringField(TEXT("kind"), Candidate.Kind) || Candidate.Kind.IsEmpty())
			{
				Error = TEXT("post requires id, kind, pos and yaw within sector bounds");
				return false;
			}
			Candidate.Pos = Location.Position;
			Candidate.YawDeg = static_cast<float>(FRotator::NormalizeAxis(Location.Yaw));
			Out.Posts.Add(MoveTemp(Candidate));
		}
		return true;
	}
}

TArray<FTrainingSector> TrainingSectorRules::ParseSectors(const FJsonObject& Json, TArray<FString>& Errors)
{
	using namespace TrainingSectorParse;
	FString Map;
	const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
	if (!Json.TryGetStringField(TEXT("map"), Map) || Map != MapName || !Json.TryGetArrayField(TEXT("sectors"), List) || !List)
	{
		Errors.Add(TEXT("expected the X6 map and a sectors array"));
		return {};
	}
	TArray<FTrainingSector> Result;
	for (int32 Index = 0; Index < List->Num(); ++Index)
	{
		const TSharedPtr<FJsonObject>* Entry = nullptr;
		const TSharedPtr<FJsonValue>& Value = (*List)[Index];
		FTrainingSector Sector;
		FString Error = TEXT("invalid sector object");
		if (!Value || !Value->TryGetObject(Entry) || !Entry || !Entry->IsValid() || !ReadSector(**Entry, Sector, Error))
		{
			Errors.Add(FString::Printf(TEXT("sectors[%d]: %s"), Index, *Error));
			continue;
		}
		if (Result.ContainsByPredicate([&Sector](const FTrainingSector& Seen) { return Seen.Id == Sector.Id; }))
		{
			Errors.Add(FString::Printf(TEXT("duplicate sector id %d"), Sector.Id));
			continue;
		}
		Result.Add(MoveTemp(Sector));
	}
	return Result;
}

TOptional<int32> TrainingSectorRules::ParseId(const FString& Text)
{
	if (Text.Len() == 1 && Text[0] >= '0' && Text[0] <= '6')
	{
		return static_cast<int32>(Text[0] - '0');
	}
	return {};
}

int32 TrainingSectorRules::SavedId(const FJsonObject& Json)
{
	FString Map;
	return Json.TryGetStringField(TEXT("map"), Map) && Map == MapName ? TrainingSectorParse::NumberId(Json, TEXT("sector")).Get(0) : 0;
}

FVector TrainingSectorRules::Clamp(const FBox2D& Box, const FVector& Point, double Inset)
{
	const FVector2D Limit = Box.GetExtent();
	const FVector2D Pad(FMath::Clamp(Inset, 0.0, Limit.X), FMath::Clamp(Inset, 0.0, Limit.Y));
	return FVector(FMath::Clamp(Point.X, Box.Min.X + Pad.X, Box.Max.X - Pad.X),
		FMath::Clamp(Point.Y, Box.Min.Y + Pad.Y, Box.Max.Y - Pad.Y), Point.Z);
}

bool TrainingSectorRules::Contains(const FBox2D& Box, const FVector& Point)
{
	return Point.X >= Box.Min.X && Point.X <= Box.Max.X && Point.Y >= Box.Min.Y && Point.Y <= Box.Max.Y;
}

void TrainingSectorRules::Pick(const FTrainingSector& Sector, int32 Seed, FTrainingSectorStart& Start, FPmcEncounterSpec& Squad)
{
	FRandomStream Stream(Seed);
	Start = Sector.Starts[Stream.RandRange(0, Sector.Starts.Num() - 1)];
	Squad = Sector.Squad;
	Squad.Post = Sector.Posts[Stream.RandRange(0, Sector.Posts.Num() - 1)];
	Squad.bHasPost = true;
	Squad.bRandomSpawn = false;
	Squad.FixedSpot = Squad.Post.Pos;
	Squad.DelaySeconds = 0.f;
	Squad.Seed = Seed != 0 ? Seed : 1;
}

namespace TrainingSectorFight
{
	void ReadFields(const FJsonObject& Json, FSectorFightConfig& Out)
	{
		const auto Read = [&Json](const TCHAR* Key, float Current) -> float
		{
			double Value = 0.0;
			return Json.TryGetNumberField(Key, Value) && FMath::IsFinite(Value) && Value >= 0.0 ? static_cast<float>(Value) : Current;
		};
		Out.RoundRestartDelayS = Read(TEXT("round_restart_delay_s"), Out.RoundRestartDelayS);
		Out.RoundTimeLimitS = Read(TEXT("round_time_limit_s"), Out.RoundTimeLimitS);
		Out.ResupplyDelayS = Read(TEXT("resupply_delay_s"), Out.ResupplyDelayS);
		Out.MinStartPostDistanceM = Read(TEXT("min_start_post_distance_m"), Out.MinStartPostDistanceM);
		Out.RespawnGraceS = Read(TEXT("respawn_grace_s"), Out.RespawnGraceS);
		Out.NoContactRelocateS = Read(TEXT("no_contact_relocate_s"), Out.NoContactRelocateS);
		Out.RelocateDistanceM = Read(TEXT("relocate_distance_m"), Out.RelocateDistanceM);
		Out.RelocateMaxPerRound = FMath::Max(0, FMath::RoundToInt(Read(TEXT("relocate_max_per_round"), static_cast<float>(Out.RelocateMaxPerRound))));
		Out.LauncherRocketsPerRound = FMath::Max(0, FMath::RoundToInt(Read(TEXT("launcher_rockets_per_round"), static_cast<float>(Out.LauncherRocketsPerRound))));
		FString Mode;
		if (Json.TryGetStringField(TEXT("ammo_mode"), Mode))
		{
			Out.AmmoMode = Mode.Equals(TEXT("real"), ESearchCase::IgnoreCase) ? ESectorAmmoMode::Real : ESectorAmmoMode::Resupply;
		}
	}
}

FSectorFightConfig TrainingSectorRules::FightConfig(const FJsonObject& Json, int32 SectorId)
{
	FSectorFightConfig Out;
	TrainingSectorFight::ReadFields(Json, Out);
	const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
	if (!Json.TryGetArrayField(TEXT("sectors"), List) || !List)
	{
		return Out;
	}
	for (const TSharedPtr<FJsonValue>& Value : *List)
	{
		const TSharedPtr<FJsonObject>* Entry = nullptr;
		double Id = 0.0;
		if (Value && Value->TryGetObject(Entry) && Entry && Entry->IsValid() && (*Entry)->TryGetNumberField(TEXT("id"), Id)
			&& static_cast<int32>(Id) == SectorId)
		{
			TrainingSectorFight::ReadFields(**Entry, Out);
			break;
		}
	}
	return Out;
}

ESectorRoundEnd TrainingSectorRules::EvaluateRound(const FSectorSideCount& Player, const FSectorSideCount& Pmc, double ElapsedS, double LimitS)
{
	if (Player.Members <= 0 || Pmc.Members <= 0)
	{
		return ESectorRoundEnd::None;
	}
	if (Player.Fighting <= 0 && Pmc.Fighting <= 0)
	{
		return ESectorRoundEnd::Draw;
	}
	if (Player.Fighting <= 0)
	{
		return ESectorRoundEnd::Defeat;
	}
	if (Pmc.Fighting <= 0)
	{
		return ESectorRoundEnd::Victory;
	}
	return ElapsedS >= LimitS ? ESectorRoundEnd::Draw : ESectorRoundEnd::None;
}

FSectorPair TrainingSectorRules::PickRoundPair(int32 StartCount, int32 PostCount, int32 Seed, int32 RoundIndex, const FSectorPair& Previous)
{
	if (StartCount <= 0 || PostCount <= 0)
	{
		return {};
	}
	const int32 Count = StartCount * PostCount;
	const int32 StreamSeed = static_cast<int32>(static_cast<uint32>(Seed) * 17u + static_cast<uint32>(RoundIndex) * 101u + 3u);
	FRandomStream Stream(StreamSeed);
	int32 Index = Stream.RandRange(0, Count - 1);
	const int32 Prev = Previous.IsValid() ? Previous.StartIndex * PostCount + Previous.PostIndex : INDEX_NONE;
	if (Index == Prev && Count > 1)
	{
		Index = (Index + 1) % Count;
	}
	return { Index / PostCount, Index % PostCount };
}

FSectorPair TrainingSectorRules::PickSafeRoundPair(const FTrainingSector& Sector, int32 Seed, int32 RoundIndex, const FSectorPair& Previous,
	float MinDistanceM, TFunctionRef<bool(const FSectorPair&)> HasLosFn, bool* bOutFallback)
{
	if (bOutFallback)
	{
		*bOutFallback = false;
	}
	if (Sector.Starts.IsEmpty() || Sector.Posts.IsEmpty())
	{
		return {};
	}
	const double MinDistCm = FMath::Max(0.f, MinDistanceM) * 100.0;
	TArray<FSectorPair> Safe;
	FSectorPair Farthest{0, 0};
	double FarthestDist = -1.0;
	for (int32 s = 0; s < Sector.Starts.Num(); ++s)
	{
		for (int32 p = 0; p < Sector.Posts.Num(); ++p)
		{
			const FSectorPair Candidate{s, p};
			const double Dist = FVector::Dist2D(Sector.Starts[s].Position, Sector.Posts[p].Pos);
			if (Dist > FarthestDist || (FMath::IsNearlyEqual(Dist, FarthestDist, 1.0) && Farthest == Previous && !(Candidate == Previous)))
			{
				FarthestDist = Dist;
				Farthest = Candidate;
			}
			if (Dist >= MinDistCm && !HasLosFn(Candidate))
			{
				Safe.Add(Candidate);
			}
		}
	}
	if (Safe.IsEmpty())
	{
		if (bOutFallback)
		{
			*bOutFallback = true;
		}
		return Farthest;
	}
	if (Safe.Num() > 1)
	{
		Safe.RemoveAll([&Previous](const FSectorPair& P) { return P == Previous; });
	}
	const int32 StreamSeed = static_cast<int32>(static_cast<uint32>(Seed) * 17u + static_cast<uint32>(RoundIndex) * 101u + 3u);
	FRandomStream Stream(StreamSeed);
	return Safe[Stream.RandRange(0, Safe.Num() - 1)];
}

void TrainingSectorRules::ApplyPair(const FTrainingSector& Sector, const FSectorPair& Pair, int32 Seed, FTrainingSectorStart& Start, FPmcEncounterSpec& Squad)
{
	if (!Pair.IsValid() || !Sector.Starts.IsValidIndex(Pair.StartIndex) || !Sector.Posts.IsValidIndex(Pair.PostIndex))
	{
		return;
	}
	Start = Sector.Starts[Pair.StartIndex];
	Squad = Sector.Squad;
	Squad.Post = Sector.Posts[Pair.PostIndex];
	Squad.bHasPost = true;
	Squad.bRandomSpawn = false;
	Squad.FixedSpot = Squad.Post.Pos;
	Squad.DelaySeconds = 0.f;
	Squad.Seed = Seed != 0 ? Seed : 1;
}

FSectorPair TrainingSectorRules::PairOf(const FTrainingSector& Sector, const FTrainingSectorStart& Start, const FPmcEncounterSpec& Squad)
{
	FSectorPair Pair;
	for (int32 Index = 0; Index < Sector.Starts.Num(); ++Index)
	{
		const FTrainingSectorStart& Candidate = Sector.Starts[Index];
		if (Candidate.Position.Equals(Start.Position, 1.0) && FMath::IsNearlyEqual(Candidate.Yaw, Start.Yaw, 0.1))
		{
			Pair.StartIndex = Index;
			break;
		}
	}
	for (int32 Index = 0; Index < Sector.Posts.Num(); ++Index)
	{
		if (Sector.Posts[Index].Id == Squad.Post.Id)
		{
			Pair.PostIndex = Index;
			break;
		}
	}
	return Pair;
}

bool TrainingSectorRules::ShouldRefillReserve(const FSectorResupplyQuery& Query, ESectorAmmoMode Mode, double DelayS)
{
	if (Mode != ESectorAmmoMode::Resupply || !Query.bUsesAmmo || !Query.bHasWeapon || Query.bLauncher || Query.Reserve > 0)
	{
		return false;
	}
	const int32 Magazine = FMath::Max(1, Query.Magazine);
	return Query.Clip * 3 <= Magazine && Query.SecondsQuiet >= DelayS;
}

int32 TrainingSectorRules::RefilledReserve(int32 Magazine, int32 WeaponReserve)
{
	return FMath::Max(FMath::Max(1, Magazine), WeaponReserve);
}

double TrainingSectorRules::QuietSeconds(double Now, double EmptySince, double LastShotTime)
{
	return EmptySince < 0.0 ? 0.0 : FMath::Max(0.0, Now - FMath::Max(EmptySince, LastShotTime));
}

FString TrainingSectorRules::RoundBanner(int32 Round, ESectorRoundEnd Result)
{
	const TCHAR* Word = Result == ESectorRoundEnd::Victory ? TEXT("победа") : Result == ESectorRoundEnd::Defeat ? TEXT("поражение") : TEXT("ничья"); // cyrillic-ok: designer 2026-11-24 training round banner
	return FString::Printf(TEXT("Раунд %d: %s"), FMath::Max(1, Round), Word); // cyrillic-ok: designer 2026-11-24 training round banner
}

const TCHAR* TrainingSectorRules::RoundEndName(ESectorRoundEnd Result)
{
	switch (Result)
	{
	case ESectorRoundEnd::Victory: return TEXT("victory");
	case ESectorRoundEnd::Defeat: return TEXT("defeat");
	case ESectorRoundEnd::Draw: return TEXT("draw");
	default: return TEXT("none");
	}
}

bool TrainingSectorRules::IsFighterDown(bool bAlive, bool bIncapacitated)
{
	return !bAlive || bIncapacitated;
}

bool TrainingSectorRules::ResupplyTouchesGrenades()
{
	return false;
}

bool TrainingSectorRules::ShouldRelocateNoContact(double Now, double LastContactAt, float DelayS, int32 RelocatedCount, int32 MaxPerRound, bool bInGrace)
{
	return !bInGrace && DelayS > 0.f && MaxPerRound > 0 && RelocatedCount < MaxPerRound && (Now - LastContactAt) >= static_cast<double>(DelayS);
}

FSectorRelocatePick TrainingSectorRules::PickRelocatePoint(TConstArrayView<FSectorRelocateCandidate> Candidates, float TargetDistM, float BandHalfM,
	float MinPlayerDistM, int32 Seed)
{
	FSectorRelocatePick Out;
	TArray<int32> Allowed;
	Allowed.Reserve(Candidates.Num());
	for (int32 i = 0; i < Candidates.Num(); ++i)
	{
		if (Candidates[i].MinPlayerDistM >= MinPlayerDistM)
		{
			Allowed.Add(i);
		}
	}
	if (Allowed.IsEmpty())
	{
		return Out;
	}
	const float BandMinM = FMath::Max(0.f, TargetDistM - BandHalfM);
	const float BandMaxM = TargetDistM + BandHalfM;
	// Tier priority: 0 = in-band + no LOS + cover, 1 = in-band + no LOS, 2 = in-band + LOS (cover first), 3 = out-of-band fallback (no LOS > cover).
	auto TierOf = [BandMinM, BandMaxM](const FSectorRelocateCandidate& C) -> int32
	{
		const bool bInBand = C.DistToCentroidM >= BandMinM && C.DistToCentroidM <= BandMaxM;
		if (bInBand)
		{
			if (!C.bHasLos)
			{
				return C.bNearCover ? 0 : 1;
			}
			return C.bNearCover ? 2 : 3;
		}
		if (!C.bHasLos)
		{
			return C.bNearCover ? 4 : 5;
		}
		return C.bNearCover ? 6 : 7;
	};
	int32 BestTier = MAX_int32;
	float BestDistErr = TNumericLimits<float>::Max();
	for (const int32 Idx : Allowed)
	{
		const int32 Tier = TierOf(Candidates[Idx]);
		const float Err = FMath::Abs(Candidates[Idx].DistToCentroidM - TargetDistM);
		if (Tier < BestTier || (Tier == BestTier && Err < BestDistErr))
		{
			BestTier = Tier;
			BestDistErr = Err;
		}
	}
	TArray<int32> Pool;
	for (const int32 Idx : Allowed)
	{
		const int32 Tier = TierOf(Candidates[Idx]);
		const float Err = FMath::Abs(Candidates[Idx].DistToCentroidM - TargetDistM);
		if (Tier == BestTier && Err <= BestDistErr + 0.5f)
		{
			Pool.Add(Idx);
		}
	}
	FRandomStream Stream(Seed != 0 ? Seed : 1);
	const int32 ChosenIdx = Pool[Stream.RandRange(0, Pool.Num() - 1)];
	const FSectorRelocateCandidate& Chosen = Candidates[ChosenIdx];
	Out.bValid = true;
	Out.CandidateIndex = ChosenIdx;
	Out.Location = Chosen.Location;
	Out.DistToCentroidM = Chosen.DistToCentroidM;
	Out.bHasLos = Chosen.bHasLos;
	Out.bNearCover = Chosen.bNearCover;
	Out.bFallback = BestTier >= 2;
	return Out;
}
