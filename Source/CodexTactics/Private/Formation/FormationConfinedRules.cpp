#include "Formation/FormationConfinedRules.h"

#include "CodexTactics.h"
#include "Dom/JsonObject.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace FormationConfinedLocal
{
	TAutoConsoleVariable<int32> CVarFormationConfined(TEXT("Codex.Formation.Confined"), -1,
		TEXT("The game designer's decision 2026-11-21 (\"the whole squad must enter\"): -1 = Content/Data/AI/squad_confined.json \"enabled\" (default), ")
		TEXT("1 = in a confined navmesh pocket (a flat hall, a 2.5 m corridor) the formation's slots compress into it round the leader's arrival, ")
		TEXT("0 = the formation's own slots everywhere (the column parked its men outside the hall door - A/B)"));

	TAutoConsoleVariable<int32> CVarFormationConfinedQueue(TEXT("Codex.Formation.ConfinedQueue"), -1,
		TEXT("Entry order 2026-11-22: -1 = Content/Data/AI/squad_confined.json \"entry_queue\" (default), 1 = the depth queue (the man first through ")
		TEXT("the door takes the deepest place, the next the next), 0 = the round-3 cost rule (A/B)"));

	FFormationConfinedTuning ConfinedTuningCurrent;
	bool bConfinedTuningLoaded = false;

	void ReadConfinedNumber(const FJsonObject& Json, const TCHAR* Key, float& Out, float Min, float Max, float Scale = 1.f)
	{
		double Value = 0.0;
		if (Json.TryGetNumberField(Key, Value))
		{
			Out = FMath::Clamp(static_cast<float>(Value) * Scale, Min, Max);
		}
	}

	/** Ties within this many cm count as equal (deterministic: the lower index wins). */
	constexpr float TieCm = 0.01f;
}

bool FormationConfinedRules::IsEnabled(const FFormationConfinedTuning& Tuning)
{
	const int32 Console = FormationConfinedLocal::CVarFormationConfined.GetValueOnAnyThread();
	return Console < 0 ? Tuning.bEnabled : Console != 0;
}

bool FormationConfinedRules::IsQueueEnabled(const FFormationConfinedTuning& Tuning)
{
	const int32 Console = FormationConfinedLocal::CVarFormationConfinedQueue.GetValueOnAnyThread();
	return Console < 0 ? Tuning.bEntryQueue : Console != 0;
}

FVector2D FormationConfinedRules::RayDir(int32 Index, int32 Count)
{
	const double Angle = Count > 0 ? 2.0 * UE_DOUBLE_PI * static_cast<double>(Index) / static_cast<double>(Count) : 0.0;
	return FVector2D(FMath::Cos(Angle), FMath::Sin(Angle));
}

float FormationConfinedRules::Median(TArrayView<const float> Values)
{
	if (Values.IsEmpty())
	{
		return 0.f;
	}
	TArray<float> Sorted(Values.GetData(), Values.Num());
	Sorted.Sort();
	const int32 Mid = Sorted.Num() / 2;
	return Sorted.Num() % 2 == 1 ? Sorted[Mid] : 0.5f * (Sorted[Mid - 1] + Sorted[Mid]);
}

bool FormationConfinedRules::IsConfined(const FFormationConfinedPocket& Pocket, const FFormationConfinedTuning& Tuning)
{
	return Pocket.Free.Num() >= 4 && Median(Pocket.Free) <= Tuning.ConfinedMedianCm;
}

float FormationConfinedRules::PocketRadius(const FFormationConfinedPocket& Pocket, const FFormationConfinedTuning& Tuning)
{
	return FMath::Max3(Median(Pocket.Free), Tuning.MinSpacingCm, Tuning.MinRadiusCm);
}

float FormationConfinedRules::FreeToward(const FFormationConfinedPocket& Pocket, const FVector2D& Dir)
{
	const int32 Count = Pocket.Free.Num();
	if (Count == 0)
	{
		return 0.f;
	}
	double Angle = FMath::Atan2(Dir.Y, Dir.X);
	if (Angle < 0.0)
	{
		Angle += 2.0 * UE_DOUBLE_PI;
	}
	const double Steps = Angle / (2.0 * UE_DOUBLE_PI / static_cast<double>(Count));
	const int32 Lower = FMath::FloorToInt32(Steps) % Count;
	const int32 Upper = (Lower + 1) % Count;
	const double Fraction = Steps - FMath::FloorToDouble(Steps);
	if (Fraction < 1e-3)
	{
		return Pocket.Free[Lower]; // on a ray: that ray alone
	}
	if (Fraction > 1.0 - 1e-3)
	{
		return Pocket.Free[Upper];
	}
	return FMath::Min(Pocket.Free[Lower], Pocket.Free[Upper]);
}

bool FormationConfinedRules::IsInside(const FFormationConfinedPocket& Pocket, const FVector2D& Point, const FFormationConfinedTuning& Tuning)
{
	const FVector2D Offset = Point - Pocket.Center;
	const float Distance = static_cast<float>(Offset.Size());
	if (Distance < 1.f)
	{
		return true;
	}
	if (Distance > PocketRadius(Pocket, Tuning) || Distance > FreeToward(Pocket, Offset / Distance) - Tuning.WallMarginCm)
	{
		return false;
	}
	// Not beyond a gap ray's reach (a door): the nearest ray's own cap.
	const int32 Count = Pocket.Free.Num();
	double Angle = FMath::Atan2(Offset.Y, Offset.X);
	if (Angle < 0.0)
	{
		Angle += 2.0 * UE_DOUBLE_PI;
	}
	const int32 Nearest = FMath::RoundToInt32(Angle / (2.0 * UE_DOUBLE_PI / static_cast<double>(Count))) % Count;
	return Distance <= RayReach(Pocket, Nearest, Tuning) + 0.01f;
}

float FormationConfinedRules::RayReach(const FFormationConfinedPocket& Pocket, int32 Ray, const FFormationConfinedTuning& Tuning)
{
	const int32 Count = Pocket.Free.Num();
	if (Count == 0 || !Pocket.Free.IsValidIndex(Ray))
	{
		return 0.f;
	}
	float Reach = FMath::Min(PocketRadius(Pocket, Tuning), Pocket.Free[Ray] - Tuning.WallMarginCm);
	const float Before = Pocket.Free[(Ray + Count - 1) % Count];
	const float After = Pocket.Free[(Ray + 1) % Count];
	if (Tuning.GapFactor > 0.f && Pocket.Free[Ray] > Tuning.GapFactor * FMath::Max(Before, After)
		&& FMath::Min(Before, After) <= Tuning.GapFactor * Median(Pocket.Free))
	{
		Reach = FMath::Min(Reach, FMath::Min(Before, After) - Tuning.WallMarginCm); // a door seen through by this ray alone: its places stay in the room
	}
	return Reach;
}

bool FormationConfinedRules::IsOnEntry(const FFormationConfinedPocket& Pocket, const TOptional<FVector2D>& EntryFrom, const FVector2D& Point,
	const FFormationConfinedTuning& Tuning)
{
	if (!EntryFrom.IsSet())
	{
		return false;
	}
	const FVector2D Toward = EntryFrom.GetValue() - Pocket.Center;
	const double Length = Toward.Size();
	if (Length < 1.0)
	{
		return false;
	}
	const double LineLength = FMath::Max(Length, static_cast<double>(PocketRadius(Pocket, Tuning)));
	return DistanceToSegment(Point, Pocket.Center, Pocket.Center + Toward / Length * LineLength) < Tuning.WayHalfWidthCm;
}

bool FormationConfinedRules::IsPastLeader(const FFormationConfinedPocket& Pocket, const TOptional<FVector2D>& EntryFrom, const FVector2D& Point,
	const FFormationConfinedTuning& Tuning)
{
	const int32 Count = Pocket.Free.Num();
	if (!EntryFrom.IsSet() || Count < 4 || Count % 2 != 0)
	{
		return false;
	}
	// The narrowest width through the centre: wide enough there, a man walks past the standing leader.
	int32 Narrow = INDEX_NONE;
	float Width = TNumericLimits<float>::Max();
	for (int32 Ray = 0; Ray < Count / 2; ++Ray)
	{
		const float Across = Pocket.Free[Ray] + Pocket.Free[Ray + Count / 2];
		if (Across < Width)
		{
			Width = Across;
			Narrow = Ray;
		}
	}
	if (Narrow == INDEX_NONE || Width >= 2.f * Tuning.PassFreeCm)
	{
		return false;
	}
	const FVector2D NarrowDir = RayDir(Narrow, Count);
	const FVector2D Long(-NarrowDir.Y, NarrowDir.X);
	const double EntrySide = FVector2D::DotProduct(EntryFrom.GetValue() - Pocket.Center, Long);
	if (FMath::Abs(EntrySide) < 0.25 * Tuning.MinSpacingCm)
	{
		return false; // he came in square to the long axis: no far half
	}
	return FVector2D::DotProduct(Point - Pocket.Center, Long) * FMath::Sign(EntrySide) < -0.25 * Tuning.MinSpacingCm;
}

float FormationConfinedRules::DistanceToSegment(const FVector2D& Point, const FVector2D& A, const FVector2D& B)
{
	const FVector2D AB = B - A;
	const double LengthSquared = AB.SizeSquared();
	const double T = LengthSquared > UE_DOUBLE_SMALL_NUMBER ? FMath::Clamp(FVector2D::DotProduct(Point - A, AB) / LengthSquared, 0.0, 1.0) : 0.0;
	return static_cast<float>(FVector2D::Distance(Point, A + AB * T));
}

TArray<FVector2D> FormationConfinedRules::Candidates(const FFormationConfinedPocket& Pocket, const FFormationConfinedTuning& Tuning)
{
	TArray<FVector2D> Out;
	const int32 Count = Pocket.Free.Num();
	const float Radius = PocketRadius(Pocket, Tuning);
	const float Step = FMath::Max(Tuning.RingStepCm, 1.f);
	for (int32 Ray = 0; Ray < Count; ++Ray)
	{
		const FVector2D Dir = RayDir(Ray, Count);
		const float Reach = RayReach(Pocket, Ray, Tuning);
		float Last = 0.f;
		for (float R = Tuning.MinSpacingCm; R <= Reach + 0.01f; R += Step)
		{
			Out.Add(Pocket.Center + Dir * R);
			Last = R;
		}
		// Two bodies out (a column of three along a narrow hall), when the steps skip it.
		const float Double = 2.f * Tuning.MinSpacingCm;
		if (Double <= Reach + 0.01f && FMath::Abs(Double - Last) > 0.01f && FMath::Fmod(Double - Tuning.MinSpacingCm, Step) > 0.01f)
		{
			Out.Add(Pocket.Center + Dir * Double);
		}
	}
	return Out;
}

namespace FormationConfinedLocal
{
	/** A man's straight way in: from the inside entry point (or from where he stands, through the door already) to his place. */
	struct FConfinedWay
	{
		FVector2D From = FVector2D::ZeroVector;
		FVector2D To = FVector2D::ZeroVector;
	};

	/**
	 * The depth queue (FFormationConfinedTuning::bEntryQueue; Result already confined, its places the slots). A man through the door keeps
	 * his (still valid) held place; the others in queue order take the deepest free place from the inside entry point that keeps the spacing,
	 * lies off every earlier man's way in and has his own way in clear of the places given out (tiers relax these in turn).
	 */
	FFormationConfinedResult QueueSlots(const FFormationConfinedPocket& Pocket, TArrayView<const FFormationConfinedSlot> Slots, const TOptional<FVector2D>& LeaderAt,
		const TOptional<FVector2D>& EntryFrom, const FFormationConfinedTuning& Tuning, const FFormationConfinedLineTests* Lines,
		const FFormationConfinedQueue& Queue, FFormationConfinedResult Result)
	{
		using namespace FormationConfinedRules;
		Result.bQueued = true;
		const FVector2D In = Queue.EntryInside;
		const bool bWay = LeaderAt.IsSet() && FVector2D::Distance(LeaderAt.GetValue(), Pocket.Center) > Tuning.WayMinCm;
		const auto OnWay = [&](const FVector2D& Point) { return bWay && DistanceToSegment(Point, LeaderAt.GetValue(), Pocket.Center) < Tuning.WayHalfWidthCm; };
		const bool bEntryLines = Lines && Lines->FromEntry && EntryFrom.IsSet();
		const auto SeenFromEntry = [&](const FVector2D& Point) { return !bEntryLines || Lines->FromEntry(Point); };
		const auto InRoom = [&](const FVector2D& Point) { return !Lines || !Lines->InRoom || Lines->InRoom(Point); };
		const auto Valid = [&](const FVector2D& Point)
		{
			return IsInside(Pocket, Point, Tuning) && !OnWay(Point) && !IsOnEntry(Pocket, EntryFrom, Point, Tuning) && !IsPastLeader(Pocket, EntryFrom, Point, Tuning)
				&& SeenFromEntry(Point) && InRoom(Point);
		};
		// The door mouth (DoorMouthCm round the inside entry point): the way in of every man behind.
		const auto InMouth = [&](const FVector2D& Point) { return Tuning.DoorMouthCm > 0.f && FVector2D::Distance(Point, In) < Tuning.DoorMouthCm; };
		// A man standing in the door mouth is not through yet: dealt from there (his own straight line to every place cut by the jamb) he took
		// the entry point itself and stood in the door for good, the man behind him on the landing (the walk smoke's STUCK hall legs).
		const auto Through = [&](int32 Index)
		{
			const TOptional<FVector2D>& At = Slots[Index].Occupant;
			return At.IsSet() && IsInside(Pocket, At.GetValue(), Tuning) && InRoom(At.GetValue()) && !InMouth(At.GetValue());
		};
		TArray<FVector2D> Given{ Pocket.Center };
		TArray<FConfinedWay> Ways;
		const auto Spaced = [&](const FVector2D& Point, float Spacing)
		{
			for (const FVector2D& Each : Given)
			{
				if (FVector2D::Distance(Each, Point) < Spacing - TieCm)
				{
					return false;
				}
			}
			return true;
		};

		// The held places with the block swaps applied (a man standing on his team-mate's way takes the place beyond him, the blocked man his).
		TArray<TOptional<FVector2D>> Held;
		for (const FFormationConfinedSlot& Slot : Slots)
		{
			Held.Add(Slot.Held);
		}
		for (int32 Index = 0; Index < Slots.Num(); ++Index)
		{
			const int32 Other = Slots[Index].BlockedBy;
			if (Other != Index && Slots.IsValidIndex(Other) && Held[Index].IsSet() && Held[Other].IsSet() && !Result.Swapped[Index] && !Result.Swapped[Other])
			{
				Swap(Held[Index], Held[Other]);
				Result.Swapped[Index] = true;
				Result.Swapped[Other] = true;
			}
		}

		// 1. Through the door: he keeps his place while it is valid and spaced.
		int32 NextRank = 0;
		TArray<int32> Open;
		for (int32 Index = 0; Index < Slots.Num(); ++Index)
		{
			if (Through(Index) && Held[Index].IsSet() && Valid(Held[Index].GetValue()) && Spaced(Held[Index].GetValue(), Tuning.MinSpacingCm))
			{
				Result.Places[Index] = Held[Index].GetValue();
				Result.Replaced[Index] = true;
				Result.Locked[Index] = true;
				Result.Rank[Index] = NextRank++;
				++Result.ReplacedCount;
				Given.Add(Held[Index].GetValue());
				Ways.Add({ Slots[Index].Occupant.GetValue(), Held[Index].GetValue() });
			}
			else
			{
				Open.Add(Index);
			}
		}
		if (Open.IsEmpty())
		{
			return Result;
		}

		// 2. The queue: who comes through the door next (a man inside already: the deeper the earlier).
		TArray<float> Keys;
		TArray<int32> PrevRanks;
		for (const int32 Index : Open)
		{
			const FVector2D Ref = Slots[Index].Occupant.Get(Slots[Index].Desired);
			Keys.Add(Through(Index) ? -static_cast<float>(FVector2D::Distance(Ref, In))
				: Slots[Index].ToEntryCm.Get(static_cast<float>(FVector2D::Distance(Ref, In))));
			PrevRanks.Add(Slots[Index].PrevRank);
		}
		const TArray<int32> Order = QueueOrder(Keys, PrevRanks, Tuning.QueueHysteresisCm);

		// 3. The places, deepest from the inside entry point first, in three pools: seen from the door and in the room; in the room; any (a
		// hall seen through a narrow door has few places in the door's view off the entry line - the walk smoke left a man outside on his
		// formation slot when the one pool held no second spaced place).
		TArray<FVector2D> All = Candidates(Pocket, Tuning);
		All.RemoveAll([&](const FVector2D& Point) { return OnWay(Point) || IsPastLeader(Pocket, EntryFrom, Point, Tuning); });
		All.StableSort([&In](const FVector2D& A, const FVector2D& B) { return FVector2D::DistSquared(A, In) > FVector2D::DistSquared(B, In); });
		TArray<FVector2D> Pools[3];
		Pools[2] = All;
		Pools[1] = All.FilterByPredicate([&](const FVector2D& Point) { return InRoom(Point); });
		Pools[0] = Pools[1].FilterByPredicate([&](const FVector2D& Point) { return SeenFromEntry(Point); });
		TArray<bool> OnEntryLines[3];
		for (int32 Each = 0; Each < 3; ++Each)
		{
			for (const FVector2D& Point : Pools[Each])
			{
				OnEntryLines[Each].Add(IsOnEntry(Pocket, EntryFrom, Point, Tuning));
			}
		}

		for (int32 Turn = 0; Turn < Order.Num(); ++Turn)
		{
			const int32 Index = Open[Order[Turn]];
			const bool bThrough = Through(Index);
			// A place in the door mouth only for the last man in (nobody walks in behind him).
			const bool bMouthBarred = Turn + 1 < Order.Num();
			const auto Barred = [&](const FVector2D& Point) { return bMouthBarred && InMouth(Point); };
			const FVector2D Ref = bThrough ? Slots[Index].Occupant.GetValue() : In;
			const auto WayClear = [&](const FVector2D& Point)
			{
				if (bThrough && Lines && Lines->Straight && !Lines->Straight(Ref, Point))
				{
					return false;
				}
				// The standing leader (Given[0], the pocket centre) is walked round by the runtime's step round (BodyClearCm): only the
				// team-mates' places block a way here (else the first man in, every way past the leader "blocked", took the shallowest
				// place - in the door's inner mouth).
				for (int32 Each = Tuning.BodyClearCm > 0.f ? 1 : 0; Each < Given.Num(); ++Each)
				{
					if (FVector2D::Distance(Given[Each], Ref) >= Tuning.PassClearCm && DistanceToSegment(Given[Each], Ref, Point) < Tuning.PassClearCm)
					{
						return false;
					}
				}
				return true;
			};
			const auto OffEarlierWays = [&](const FVector2D& Point)
			{
				for (const FConfinedWay& Way : Ways)
				{
					if (DistanceToSegment(Point, Way.From, Way.To) < Tuning.PassClearCm)
					{
						return false;
					}
				}
				return true;
			};
			// Per pool (the strictest first), tiers: off the earlier ways + off the entry line + his way clear; off the entry line + his way
			// clear; his way clear; spaced only. The last tier (his way passes a place given out) takes the SHALLOWEST place for a later man: he
			// stops short instead of pushing past a team-mate to the deepest one (the walk smoke's second man on the first man's way in).
			TOptional<FVector2D> Chosen;
			for (int32 PoolIndex = 0; PoolIndex < 3 && !Chosen.IsSet(); ++PoolIndex)
			{
				const TArray<FVector2D>& Pool = Pools[PoolIndex];
				for (int32 Tier = 0; Tier < 4 && !Chosen.IsSet(); ++Tier)
				{
					for (int32 Each = 0; Each < Pool.Num(); ++Each)
					{
						// The first man in (no way given before him) never takes the shallowest: nobody stands in his way, the step round
						// takes him past the leader (the walk smoke's first man took the entry point itself when his own line tests failed).
						const int32 Candidate = Tier == 3 && !Ways.IsEmpty() ? Pool.Num() - 1 - Each : Each;
						const FVector2D& Point = Pool[Candidate];
						if (Barred(Point) || !Spaced(Point, Tuning.MinSpacingCm) || (Tier <= 1 && OnEntryLines[PoolIndex][Candidate]) || (Tier == 0 && !OffEarlierWays(Point))
							|| (Tier <= 2 && !WayClear(Point)))
						{
							continue;
						}
						Chosen = Point; // the pool is deepest first (the last tier: walked from its shallow end)
						break;
					}
				}
			}
			if (!Chosen.IsSet())
			{
				// Too tight for the spacing: the place farthest from everyone given out, if it keeps half the spacing; else his slot stays.
				float BestRoom = -1.f;
				for (const FVector2D& Point : All)
				{
					if (Barred(Point))
					{
						continue;
					}
					float Room = TNumericLimits<float>::Max();
					for (const FVector2D& Each : Given)
					{
						Room = FMath::Min(Room, static_cast<float>(FVector2D::Distance(Each, Point)));
					}
					if (Room > BestRoom + TieCm)
					{
						Chosen = Point;
						BestRoom = Room;
					}
				}
				if (BestRoom < Tuning.TightMinShare * Tuning.MinSpacingCm)
				{
					Given.Add(Result.Places[Index]);
					continue;
				}
			}
			Result.Places[Index] = Chosen.GetValue();
			Result.Replaced[Index] = true;
			Result.Rank[Index] = NextRank++;
			++Result.ReplacedCount;
			Given.Add(Chosen.GetValue());
			Ways.Add({ Ref, Chosen.GetValue() });
		}
		return Result;
	}
}

TOptional<FFormationConfinedEntry> FormationConfinedRules::EntryOnPath(const FFormationConfinedPocket& Pocket, TArrayView<const FVector2D> Path,
	const FFormationConfinedTuning& Tuning, const TFunction<bool(const FVector2D&)>& InRoom)
{
	if (Path.Num() < 2 || Pocket.Free.Num() < 4)
	{
		return TOptional<FFormationConfinedEntry>();
	}
	// Samples along the way with their distance from its start.
	TArray<FVector2D> Samples;
	TArray<double> Along;
	const double Step = FMath::Max(Tuning.EntryStepCm, 1.f);
	double Walked = 0.0;
	Samples.Add(Path[0]);
	Along.Add(0.0);
	for (int32 Index = 1; Index < Path.Num(); ++Index)
	{
		const FVector2D From = Path[Index - 1];
		const double Length = FVector2D::Distance(From, Path[Index]);
		for (double At = Step; At < Length; At += Step)
		{
			Samples.Add(From + (Path[Index] - From) * (At / Length));
			Along.Add(Walked + At);
		}
		Walked += Length;
		Samples.Add(Path[Index]);
		Along.Add(Walked);
	}
	const auto In = [&](const FVector2D& Point) { return IsInside(Pocket, Point, Tuning) && (!InRoom || InRoom(Point)); };
	// The last run of samples inside: walk back from the way's end.
	int32 First = INDEX_NONE;
	for (int32 Index = Samples.Num() - 1; Index >= 0 && In(Samples[Index]); --Index)
	{
		First = Index;
	}
	if (First == INDEX_NONE || First == 0)
	{
		return TOptional<FFormationConfinedEntry>(); // never inside, or already inside at the start: no door crossed on this way
	}
	FFormationConfinedEntry Entry;
	Entry.Inside = Samples[First];
	// Back along the way to the door side.
	const double Back = FMath::Max(0.0, Along[First] - static_cast<double>(Tuning.EntryBackCm));
	Entry.Outside = Samples[0];
	for (int32 Index = First; Index > 0; --Index)
	{
		if (Along[Index - 1] <= Back)
		{
			const double Span = Along[Index] - Along[Index - 1];
			const double T = Span > UE_DOUBLE_SMALL_NUMBER ? (Back - Along[Index - 1]) / Span : 0.0;
			Entry.Outside = Samples[Index - 1] + (Samples[Index] - Samples[Index - 1]) * T;
			break;
		}
	}
	return Entry;
}

bool FormationConfinedRules::IsBlockedBy(const FVector2D& Here, const FVector2D& Place, const FVector2D& Other, const FFormationConfinedTuning& Tuning)
{
	const FVector2D ToPlace = Place - Here;
	const double Length = ToPlace.Size();
	if (Length < 1.0 || FVector2D::Distance(Here, Other) > Tuning.BlockRadiusCm)
	{
		return false;
	}
	return FVector2D::DotProduct(Other - Here, ToPlace / Length) > 0.0 && DistanceToSegment(Other, Here, Place) < Tuning.PassClearCm;
}

TArray<int32> FormationConfinedRules::QueueOrder(TArrayView<const float> Keys, TArrayView<const int32> PrevRank, float HysteresisCm)
{
	TArray<int32> Order;
	for (int32 Index = 0; Index < Keys.Num(); ++Index)
	{
		Order.Add(Index);
	}
	const auto RankOf = [&PrevRank](int32 Index) { return PrevRank.IsValidIndex(Index) && PrevRank[Index] != INDEX_NONE ? PrevRank[Index] : TNumericLimits<int32>::Max(); };
	// The last order first (new men behind it, by key; ties: the lower index).
	Order.StableSort([&](int32 A, int32 B)
	{
		const int32 RankA = RankOf(A);
		const int32 RankB = RankOf(B);
		if (RankA != RankB)
		{
			return RankA < RankB;
		}
		return RankA == TNumericLimits<int32>::Max() && Keys[A] < Keys[B] - FormationConfinedLocal::TieCm;
	});
	// Neighbours flip only past the hysteresis (bubble passes: deterministic, at most N - 1 passes).
	for (int32 Pass = 0; Pass < Order.Num(); ++Pass)
	{
		bool bSwapped = false;
		for (int32 Each = 0; Each + 1 < Order.Num(); ++Each)
		{
			if (Keys[Order[Each + 1]] < Keys[Order[Each]] - HysteresisCm)
			{
				Order.Swap(Each, Each + 1);
				bSwapped = true;
			}
		}
		if (!bSwapped)
		{
			break;
		}
	}
	return Order;
}

int32 FormationConfinedRules::FirstBodyOnWay(const FVector2D& Here, const FVector2D& Place, TArrayView<const FVector2D> Bodies, const FFormationConfinedTuning& Tuning)
{
	const FVector2D Way = Place - Here;
	const double Length = Way.Size();
	if (Tuning.BodyClearCm <= 0.f || Length < 1.0)
	{
		return INDEX_NONE;
	}
	const FVector2D Dir = Way / Length;
	// The first body ahead on the way, closer to it than the clearance.
	int32 First = INDEX_NONE;
	double FirstAlong = TNumericLimits<double>::Max();
	for (int32 Index = 0; Index < Bodies.Num(); ++Index)
	{
		const double Along = FVector2D::DotProduct(Bodies[Index] - Here, Dir);
		if (Along <= 0.0 || Along >= Length || DistanceToSegment(Bodies[Index], Here, Place) >= Tuning.BodyClearCm)
		{
			continue;
		}
		if (Along < FirstAlong - FormationConfinedLocal::TieCm)
		{
			First = Index;
			FirstAlong = Along;
		}
	}
	return First;
}

TOptional<FVector2D> FormationConfinedRules::LeaderRoomStep(const FVector2D& LeaderAt, const FVector2D& EntryInside, float FreeAheadCm, const FFormationConfinedTuning& Tuning)
{
	const FVector2D Away = LeaderAt - EntryInside;
	const double Distance = Away.Size();
	if (Tuning.LeaderRoomCm <= 0.f || Distance < 1.0)
	{
		return TOptional<FVector2D>(); // off, or he stands on the entry point itself (no way to tell "away")
	}
	const double Step = FMath::Min3(static_cast<double>(Tuning.LeaderRoomCm) - Distance, static_cast<double>(Tuning.LeaderRoomMaxCm),
		static_cast<double>(FreeAheadCm - Tuning.WallMarginCm));
	if (Step < LeaderRoomMinStepCm)
	{
		return TOptional<FVector2D>();
	}
	return LeaderAt + Away / Distance * Step;
}

TOptional<FVector2D> FormationConfinedRules::StepRoundBody(const FVector2D& Here, const FVector2D& Place, TArrayView<const FVector2D> Bodies,
	const FFormationConfinedTuning& Tuning, const TFunction<bool(const FVector2D&)>& IsWalkable)
{
	const int32 First = FirstBodyOnWay(Here, Place, Bodies, Tuning);
	if (First == INDEX_NONE)
	{
		return TOptional<FVector2D>();
	}
	const FVector2D Dir = (Place - Here).GetSafeNormal();
	const double FirstAlong = FVector2D::DotProduct(Bodies[First] - Here, Dir);
	// Out from the body toward the way (its own side first; on the line: the left of his walk first).
	const FVector2D Body = Bodies[First];
	const FVector2D Closest = Here + Dir * FirstAlong;
	FVector2D Side = Closest - Body;
	Side = Side.Size() > 1.0 ? Side.GetSafeNormal() : FVector2D(-Dir.Y, Dir.X);
	const double Out = FMath::Max(Tuning.BodyClearCm, Tuning.MinSpacingCm) + StepRoundMarginCm;
	for (const FVector2D& Each : { Side, -Side })
	{
		const FVector2D Point = Body + Each * Out;
		if (!IsWalkable || IsWalkable(Point))
		{
			return Point;
		}
	}
	return TOptional<FVector2D>();
}

FFormationConfinedResult FormationConfinedRules::CompressSlots(const FFormationConfinedPocket& Pocket, TArrayView<const FFormationConfinedSlot> Slots,
	const TOptional<FVector2D>& LeaderAt, const TOptional<FVector2D>& EntryFrom, const FFormationConfinedTuning& Tuning,
	const FFormationConfinedLineTests* Lines, const FFormationConfinedQueue* Queue)
{
	using namespace FormationConfinedLocal;
	FFormationConfinedResult Result;
	Result.Places.Reserve(Slots.Num());
	for (const FFormationConfinedSlot& Slot : Slots)
	{
		Result.Places.Add(Slot.Desired);
	}
	Result.Replaced.Init(false, Slots.Num());
	Result.Rank.Init(INDEX_NONE, Slots.Num());
	Result.Locked.Init(false, Slots.Num());
	Result.Swapped.Init(false, Slots.Num());
	if (!IsConfined(Pocket, Tuning))
	{
		return Result;
	}
	Result.bConfined = true;
	if (Queue && IsQueueEnabled(Tuning))
	{
		return QueueSlots(Pocket, Slots, LeaderAt, EntryFrom, Tuning, Lines, *Queue, MoveTemp(Result));
	}

	const bool bWay = LeaderAt.IsSet() && FVector2D::Distance(LeaderAt.GetValue(), Pocket.Center) > Tuning.WayMinCm;
	const auto OnWay = [&](const FVector2D& Point)
	{
		return bWay && DistanceToSegment(Point, LeaderAt.GetValue(), Pocket.Center) < Tuning.WayHalfWidthCm;
	};
	// Lead review 2026-11-22: a place round a door jamb's corner (not in a clear line with clearance from the entry point) is no place.
	const bool bEntryLines = Lines && Lines->FromEntry && EntryFrom.IsSet();
	const auto SeenFromEntry = [&](const FVector2D& Point) { return !bEntryLines || Lines->FromEntry(Point); };
	const auto InRoom = [&](const FVector2D& Point) { return !Lines || !Lines->InRoom || Lines->InRoom(Point); };
	// A valid place for him now: inside, off the leader's way and the entry line, not past the leader, seen from the door, in the room.
	const auto Valid = [&](const FVector2D& Point)
	{
		return IsInside(Pocket, Point, Tuning) && !OnWay(Point) && !IsOnEntry(Pocket, EntryFrom, Point, Tuning) && !IsPastLeader(Pocket, EntryFrom, Point, Tuning)
			&& SeenFromEntry(Point) && InRoom(Point);
	};
	TArray<FVector2D> Given{ Pocket.Center };
	const auto Nearest = [&Given](const FVector2D& Point)
	{
		float Best = TNumericLimits<float>::Max();
		for (const FVector2D& Each : Given)
		{
			Best = FMath::Min(Best, static_cast<float>(FVector2D::Distance(Each, Point)));
		}
		return Best;
	};

	// 1. A slot already a valid place stays; then a place held from last frame that is still valid (no frame-to-frame swaps).
	TArray<int32> Open;
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		const FVector2D& Desired = Slots[Index].Desired;
		if (Valid(Desired) && Nearest(Desired) >= Tuning.MinSpacingCm - TieCm)
		{
			Given.Add(Desired);
		}
		else
		{
			Open.Add(Index);
		}
	}
	for (int32 Each = Open.Num() - 1; Each >= 0; --Each)
	{
		const int32 Index = Open[Each];
		const TOptional<FVector2D>& Held = Slots[Index].Held;
		if (Held.IsSet() && Valid(Held.GetValue()) && Nearest(Held.GetValue()) >= Tuning.MinSpacingCm - TieCm)
		{
			Result.Places[Index] = Held.GetValue();
			Result.Replaced[Index] = true;
			++Result.ReplacedCount;
			Given.Add(Held.GetValue());
			Open.RemoveAt(Each);
		}
	}
	if (Open.IsEmpty())
	{
		return Result;
	}

	// 2. The others: the man nearest the centre chooses first, each the candidate nearest to him.
	const auto RefOf = [&Slots](int32 Index) { return Slots[Index].Occupant.Get(Slots[Index].Desired); };
	Open.StableSort([&](int32 A, int32 B)
	{
		return FVector2D::DistSquared(RefOf(A), Pocket.Center) < FVector2D::DistSquared(RefOf(B), Pocket.Center);
	});
	TArray<FVector2D> Pool = Candidates(Pocket, Tuning);
	Pool.RemoveAll(OnWay);
	Pool.RemoveAll([&](const FVector2D& Point) { return IsPastLeader(Pocket, EntryFrom, Point, Tuning); }); // never: he would push into the leader for good
	if (bEntryLines || (Lines && Lines->InRoom))
	{
		TArray<FVector2D> Seen = Pool;
		Seen.RemoveAll([&](const FVector2D& Point) { return !SeenFromEntry(Point) || !InRoom(Point); });
		if (!Seen.IsEmpty())
		{
			Pool = MoveTemp(Seen); // none seen from the door (a bad entry point): the filter is dropped, never worse than without it
		}
	}
	TArray<bool> OnEntryLine;
	for (const FVector2D& Point : Pool)
	{
		OnEntryLine.Add(IsOnEntry(Pocket, EntryFrom, Point, Tuning));
	}
	for (const int32 Index : Open)
	{
		if (Pool.IsEmpty())
		{
			Given.Add(Result.Places[Index]); // nowhere inside: the slot stays
			continue;
		}
		const FVector2D Ref = RefOf(Index);
		// He walks there straight past nobody: his line keeps PassClearCm from every place given out (the leader's too) he is not already beside.
		// He still stands farther out than the entry point: he walks in through the door (the entry test covers his way); else his own line.
		const bool bWalksInThroughDoor = EntryFrom.IsSet()
			&& FVector2D::Distance(Ref, Pocket.Center) > FVector2D::Distance(EntryFrom.GetValue(), Pocket.Center);
		const auto Reachable = [&](const FVector2D& Point)
		{
			if (Lines && Lines->Straight && !bWalksInThroughDoor && !Lines->Straight(Ref, Point))
			{
				return false;
			}
			for (const FVector2D& Each : Given)
			{
				if (FVector2D::Distance(Each, Ref) >= Tuning.PassClearCm && DistanceToSegment(Each, Ref, Point) < Tuning.PassClearCm)
				{
					return false;
				}
			}
			return true;
		};
		// Tiers: reachable and off the entry line, reachable, off the entry line, any - the first with a spaced place wins.
		int32 Best = INDEX_NONE;
		for (int32 Tier = 0; Tier < 4 && Best == INDEX_NONE; ++Tier)
		{
			float BestDistance = TNumericLimits<float>::Max();
			for (int32 Candidate = 0; Candidate < Pool.Num(); ++Candidate)
			{
				const bool bNeedReach = Tier <= 1;
				const bool bNeedOffEntry = Tier == 0 || Tier == 2;
				if ((bNeedOffEntry && OnEntryLine[Candidate]) || Nearest(Pool[Candidate]) < Tuning.MinSpacingCm - TieCm
					|| (bNeedReach && !Reachable(Pool[Candidate])))
				{
					continue;
				}
				// The first man in goes deepest (farthest from where they come in): the next never has to pass him.
				const float Distance = static_cast<float>(FVector2D::Distance(Pool[Candidate], Ref)
					- (EntryFrom.IsSet() ? Tuning.DepthWeight * FVector2D::Distance(Pool[Candidate], EntryFrom.GetValue()) : 0.0));
				if (Distance < BestDistance - TieCm)
				{
					Best = Candidate;
					BestDistance = Distance;
				}
			}
		}
		if (Best == INDEX_NONE)
		{
			// Too tight for the spacing: the place farthest from everyone given out (ties: the nearest to him).
			float BestRoom = -1.f;
			float BestDistance = TNumericLimits<float>::Max();
			for (int32 Candidate = 0; Candidate < Pool.Num(); ++Candidate)
			{
				const float Room = Nearest(Pool[Candidate]);
				const float Distance = static_cast<float>(FVector2D::Distance(Pool[Candidate], Ref));
				if (Room > BestRoom + TieCm || (FMath::Abs(Room - BestRoom) <= TieCm && Distance < BestDistance - TieCm))
				{
					Best = Candidate;
					BestRoom = Room;
					BestDistance = Distance;
				}
			}
			if (BestRoom < 0.5f * Tuning.MinSpacingCm)
			{
				Given.Add(Result.Places[Index]); // no room inside: he waits at his slot rather than on a team-mate
				continue;
			}
		}
		Result.Places[Index] = Pool[Best];
		Result.Replaced[Index] = true;
		++Result.ReplacedCount;
		Given.Add(Pool[Best]);
	}
	return Result;
}

FString FormationConfinedTuning::GetDefaultPath()
{
	return FPaths::ProjectContentDir() / TEXT("Data/AI/squad_confined.json");
}

FFormationConfinedTuning FormationConfinedTuning::FromJson(const FJsonObject& Json, const FFormationConfinedTuning& Base)
{
	using namespace FormationConfinedLocal;
	FFormationConfinedTuning Tuning = Base;
	bool bFlag = false;
	double Number = 0.0;
	if (Json.TryGetBoolField(TEXT("enabled"), bFlag))
	{
		Tuning.bEnabled = bFlag;
	}
	else if (Json.TryGetNumberField(TEXT("enabled"), Number))
	{
		Tuning.bEnabled = Number != 0.0;
	}
	if (Json.TryGetBoolField(TEXT("hold_places"), bFlag))
	{
		Tuning.bHoldPlaces = bFlag;
	}
	else if (Json.TryGetNumberField(TEXT("hold_places"), Number))
	{
		Tuning.bHoldPlaces = Number != 0.0;
	}
	if (Json.TryGetNumberField(TEXT("probe_rays"), Number))
	{
		Tuning.ProbeRays = FMath::Clamp(FMath::RoundToInt32(Number), 8, 64);
	}
	ReadConfinedNumber(Json, TEXT("probe_m"), Tuning.ProbeCm, 100.f, 2000.f, 100.f);
	ReadConfinedNumber(Json, TEXT("confined_median_m"), Tuning.ConfinedMedianCm, 0.f, 1000.f, 100.f);
	ReadConfinedNumber(Json, TEXT("min_spacing_cm"), Tuning.MinSpacingCm, 40.f, 300.f);
	ReadConfinedNumber(Json, TEXT("wall_margin_cm"), Tuning.WallMarginCm, 0.f, 100.f);
	ReadConfinedNumber(Json, TEXT("ring_step_cm"), Tuning.RingStepCm, 5.f, 100.f);
	ReadConfinedNumber(Json, TEXT("min_radius_cm"), Tuning.MinRadiusCm, 0.f, 500.f);
	ReadConfinedNumber(Json, TEXT("arrive_m"), Tuning.ArriveCm, 0.f, 2000.f, 100.f);
	ReadConfinedNumber(Json, TEXT("level_dz_cm"), Tuning.LevelDzCm, 10.f, 500.f);
	ReadConfinedNumber(Json, TEXT("recenter_cm"), Tuning.RecenterCm, 1.f, 500.f);
	ReadConfinedNumber(Json, TEXT("refresh_s"), Tuning.RefreshSeconds, 0.1f, 30.f);
	ReadConfinedNumber(Json, TEXT("way_half_width_cm"), Tuning.WayHalfWidthCm, 0.f, 300.f);
	ReadConfinedNumber(Json, TEXT("way_min_cm"), Tuning.WayMinCm, 0.f, 500.f);
	ReadConfinedNumber(Json, TEXT("pass_clear_cm"), Tuning.PassClearCm, 0.f, 200.f);
	ReadConfinedNumber(Json, TEXT("depth_weight"), Tuning.DepthWeight, 0.f, 10.f);
	ReadConfinedNumber(Json, TEXT("pass_free_cm"), Tuning.PassFreeCm, 0.f, 300.f);
	ReadConfinedNumber(Json, TEXT("face_walk_cm"), Tuning.FaceWalkCm, 0.f, 1000.f);
	ReadConfinedNumber(Json, TEXT("jamb_clear_cm"), Tuning.JambClearCm, 0.f, 100.f);
	ReadConfinedNumber(Json, TEXT("jamb_sample_cm"), Tuning.JambSampleCm, 5.f, 100.f);
	ReadConfinedNumber(Json, TEXT("gap_factor"), Tuning.GapFactor, 0.f, 10.f);
	ReadConfinedNumber(Json, TEXT("throat_clear_cm"), Tuning.ThroatClearCm, 0.f, 200.f);
	if (Json.TryGetBoolField(TEXT("entry_queue"), bFlag))
	{
		Tuning.bEntryQueue = bFlag;
	}
	else if (Json.TryGetNumberField(TEXT("entry_queue"), Number))
	{
		Tuning.bEntryQueue = Number != 0.0;
	}
	ReadConfinedNumber(Json, TEXT("entry_back_cm"), Tuning.EntryBackCm, 0.f, 300.f);
	ReadConfinedNumber(Json, TEXT("entry_step_cm"), Tuning.EntryStepCm, 2.f, 50.f);
	ReadConfinedNumber(Json, TEXT("queue_hysteresis_cm"), Tuning.QueueHysteresisCm, 0.f, 300.f);
	ReadConfinedNumber(Json, TEXT("block_speed_cm_s"), Tuning.BlockSpeedCmS, 0.f, 200.f);
	ReadConfinedNumber(Json, TEXT("block_s"), Tuning.BlockSeconds, 0.05f, 10.f);
	ReadConfinedNumber(Json, TEXT("block_radius_cm"), Tuning.BlockRadiusCm, 40.f, 300.f);
	ReadConfinedNumber(Json, TEXT("block_cooldown_s"), Tuning.BlockCooldownSeconds, 0.f, 30.f);
	ReadConfinedNumber(Json, TEXT("body_clear_cm"), Tuning.BodyClearCm, 0.f, 200.f);
	ReadConfinedNumber(Json, TEXT("door_mouth_cm"), Tuning.DoorMouthCm, 0.f, 200.f);
	ReadConfinedNumber(Json, TEXT("tight_min_share"), Tuning.TightMinShare, 0.f, 1.f);
	ReadConfinedNumber(Json, TEXT("leader_room_cm"), Tuning.LeaderRoomCm, 0.f, 400.f);
	ReadConfinedNumber(Json, TEXT("leader_room_max_cm"), Tuning.LeaderRoomMaxCm, 0.f, 200.f);
	return Tuning;
}

bool FormationConfinedTuning::Reload()
{
	using namespace FormationConfinedLocal;
	bConfinedTuningLoaded = true;
	FString Text;
	TSharedPtr<FJsonObject> Json;
	if (!FFileHelper::LoadFileToString(Text, *GetDefaultPath()) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid())
	{
		return false;
	}
	ConfinedTuningCurrent = FromJson(*Json);
	UE_LOG(LogCodexTactics, Display, TEXT("Formation confined tuning from %s: enabled %d, %d rays x %.0f cm, confined at median <= %.0f cm, spacing %.0f cm"),
		*GetDefaultPath(), ConfinedTuningCurrent.bEnabled ? 1 : 0, ConfinedTuningCurrent.ProbeRays, ConfinedTuningCurrent.ProbeCm,
		ConfinedTuningCurrent.ConfinedMedianCm, ConfinedTuningCurrent.MinSpacingCm);
	return true;
}

const FFormationConfinedTuning& FormationConfinedTuning::Get()
{
	using namespace FormationConfinedLocal;
	if (!bConfinedTuningLoaded)
	{
		Reload();
	}
	return ConfinedTuningCurrent;
}
