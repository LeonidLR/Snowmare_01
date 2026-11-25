#include "Interactables/DoorRules.h"

#include "CodexTactics.h"
#include "Data/CombatTypes.h"
#include "Dom/JsonObject.h"
#include "HAL/IConsoleManager.h"
#include "Math/RandomStream.h"
#include "Misc/CommandLine.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace DoorRulesLocal
{
	TAutoConsoleVariable<int32> CVarDoorsOpenable(TEXT("Codex.Doors.Openable"), -1,
		TEXT("Openable doors R1 (designer 2026-11-21; behind a switch, off by default): -1 = Content/Data/Interactables/doors.json \"enabled\" ")
		TEXT("(default 0), 1 = closed / locked leaves of generated buildings become doors (open / close by click, auto-open on the way, locked ")
		TEXT("blocks the navmesh, kick), 0 = the leaves stay decorative (walk-through, block sight) exactly as before R1. Read every tick: ")
		TEXT("switching at runtime adopts / releases the leaves."));

	TAutoConsoleVariable<int32> CVarDoorsSeed(TEXT("Codex.Doors.Seed"), -1,
		TEXT("Openable doors R2: the seed of the door rolls (kick, injury, lock shot; deterministic per seed, door, pawn and attempt). ")
		TEXT("-1 = -DoorSeed= on the command line, else Content/Data/Interactables/doors.json roll_seed."));

	FDoorTuning DoorTuningCurrent;
	bool bDoorTuningLoaded = false;

	void ReadDoorNumber(const FJsonObject& Json, const TCHAR* Key, float& Out, float Min, float Max, float Scale = 1.f)
	{
		double Value = 0.0;
		if (Json.TryGetNumberField(Key, Value))
		{
			Out = FMath::Clamp(static_cast<float>(Value) * Scale, Min, Max);
		}
	}

	void ReadDoorFlag(const FJsonObject& Json, const TCHAR* Key, bool& Out)
	{
		bool bFlag = false;
		double Number = 0.0;
		if (Json.TryGetBoolField(Key, bFlag))
		{
			Out = bFlag;
		}
		else if (Json.TryGetNumberField(Key, Number))
		{
			Out = Number != 0.0;
		}
	}
}

bool DoorRules::ParseState(const FString& Text, EDoorLeafState& OutState)
{
	static const TPair<const TCHAR*, EDoorLeafState> Names[] = {
		{ TEXT("open"), EDoorLeafState::Open }, { TEXT("closed"), EDoorLeafState::Closed }, { TEXT("locked"), EDoorLeafState::Locked },
		{ TEXT("barricaded"), EDoorLeafState::Barricaded }, { TEXT("sealed"), EDoorLeafState::Sealed } };
	const FString Trimmed = Text.TrimStartAndEnd();
	for (const TPair<const TCHAR*, EDoorLeafState>& Name : Names)
	{
		if (Trimmed.Equals(Name.Key, ESearchCase::IgnoreCase))
		{
			OutState = Name.Value;
			return true;
		}
	}
	OutState = EDoorLeafState::Open;
	return false;
}

const TCHAR* DoorRules::StateName(EDoorLeafState State)
{
	switch (State)
	{
	case EDoorLeafState::Closed: return TEXT("closed");
	case EDoorLeafState::Locked: return TEXT("locked");
	case EDoorLeafState::Barricaded: return TEXT("barricaded");
	case EDoorLeafState::Sealed: return TEXT("sealed");
	default: return TEXT("open");
	}
}

bool DoorRules::IsAdoptable(EDoorLeafState State)
{
	return State == EDoorLeafState::Closed || State == EDoorLeafState::Locked;
}

bool DoorRules::ApplyAction(EDoorLeafState Current, EDoorAction Action, const FDoorTuning& Tuning, EDoorLeafState& OutNext)
{
	OutNext = Current;
	switch (Action)
	{
	case EDoorAction::Open:
	case EDoorAction::AutoOpen:
		if (Current != EDoorLeafState::Closed)
		{
			return false; // open already, or locked / barricaded / sealed: only a breach opens those
		}
		OutNext = EDoorLeafState::Open;
		return true;
	case EDoorAction::Close:
		if (Current != EDoorLeafState::Open)
		{
			return false;
		}
		OutNext = EDoorLeafState::Closed;
		return true;
	case EDoorAction::Kick:
		if (!Tuning.bKickEnabled || (Current != EDoorLeafState::Locked && Current != EDoorLeafState::Closed))
		{
			return false;
		}
		OutNext = EDoorLeafState::Open;
		return true;
	case EDoorAction::ShootLock:
		if (Current != EDoorLeafState::Locked && Current != EDoorLeafState::Closed)
		{
			return false;
		}
		OutNext = EDoorLeafState::Open;
		return true;
	case EDoorAction::Unlock:
		if (Current != EDoorLeafState::Locked)
		{
			return false;
		}
		OutNext = EDoorLeafState::Closed;
		return true;
	case EDoorAction::Blast:
		if (Current == EDoorLeafState::Barricaded || Current == EDoorLeafState::Sealed)
		{
			return false; // their builder pieces are not doors (P1: charges open a barricade)
		}
		OutNext = EDoorLeafState::Open;
		return true;
	}
	return false;
}

EDoorPassage DoorRules::GetPassage(EDoorLeafState State)
{
	switch (State)
	{
	case EDoorLeafState::Open: return EDoorPassage::Free;
	case EDoorLeafState::Closed: return EDoorPassage::Priced;
	default: return EDoorPassage::Blocked;
	}
}

float DoorRules::GetNavEnteringCost(EDoorLeafState State, const FDoorTuning& Tuning)
{
	return GetPassage(State) == EDoorPassage::Priced ? Tuning.NavEnteringCostCm : 0.f;
}

bool DoorRules::BlocksSightAndFire(EDoorLeafState State)
{
	return State != EDoorLeafState::Open;
}

int32 DoorRules::CountLeaves(float WidthCm, const FDoorTuning& Tuning)
{
	return WidthCm > Tuning.SingleLeafMaxCm + KINDA_SMALL_NUMBER ? 2 : 1;
}

float DoorRules::SwingSign(float InstigatorLocalY)
{
	return InstigatorLocalY > 0.f ? -1.f : 1.f;
}

bool DoorRules::IsInZone(const FVector2D& Local, float HalfWidthCm, const FDoorTuning& Tuning)
{
	return FMath::Abs(Local.X) <= HalfWidthCm + Tuning.ZoneSlackCm && FMath::Abs(Local.Y) <= Tuning.ZoneDepthCm;
}

bool DoorRules::PathCrossesDoorway(const TArray<FVector2D>& LocalPath, float HalfWidthCm, float LookAheadCm)
{
	double Walked = 0.0;
	for (int32 Index = 1; Index < LocalPath.Num() && Walked <= LookAheadCm; ++Index)
	{
		const FVector2D A = LocalPath[Index - 1];
		const FVector2D B = LocalPath[Index];
		const double Length = FVector2D::Distance(A, B);
		// From one side of the leaf plane to the other (touching it counts; running along it does not).
		const bool bCrosses = (A.Y <= 0.0 && B.Y > 0.0) || (A.Y >= 0.0 && B.Y < 0.0);
		if (bCrosses)
		{
			const double T = A.Y / (A.Y - B.Y);
			const double X = A.X + (B.X - A.X) * T;
			if (FMath::Abs(X) <= HalfWidthCm && Walked + Length * T <= LookAheadCm)
			{
				return true;
			}
		}
		Walked += Length;
	}
	return false;
}

bool DoorRules::IsHeadingInto(const FVector2D& Local, const FVector2D& LocalVelocity, const FDoorTuning& Tuning)
{
	if (LocalVelocity.Size() < Tuning.HeadingSpeedCmS || FMath::IsNearlyZero(Local.Y))
	{
		return false;
	}
	// Towards the plane: velocity across it points against his side.
	return Local.Y * LocalVelocity.Y < 0.0 && FMath::Abs(LocalVelocity.Y) >= 0.5 * Tuning.HeadingSpeedCmS;
}

float DoorRules::AdvanceAutoOpen(EDoorLeafState State, float Held, bool bSomeoneHeading, float DeltaSeconds, const FDoorTuning& Tuning,
	bool& bOutOpen)
{
	bOutOpen = false;
	if (State != EDoorLeafState::Closed || !bSomeoneHeading)
	{
		return 0.f;
	}
	const float NewHeld = Held + FMath::Max(0.f, DeltaSeconds);
	bOutOpen = NewHeld >= Tuning.AutoOpenDelaySeconds;
	return NewHeld;
}

bool DoorRules::CanClose(EDoorLeafState State, bool bDoorwayOccupied, bool bBroken)
{
	return State == EDoorLeafState::Open && !bDoorwayOccupied && !bBroken;
}

bool DoorRules::IsHeavy(float WidthCm, const FDoorTuning& Tuning)
{
	return WidthCm >= Tuning.HeavyMinWidthCm;
}

bool DoorRules::ParseStrength(const FString& Text, EDoorStrength& OutStrength)
{
	const FString Name = Text.TrimStartAndEnd();
	for (const EDoorStrength Strength : { EDoorStrength::Wood, EDoorStrength::Steel, EDoorStrength::Reinforced })
	{
		if (Name.Equals(StrengthName(Strength), ESearchCase::IgnoreCase))
		{
			OutStrength = Strength;
			return true;
		}
	}
	return false;
}

const TCHAR* DoorRules::StrengthName(EDoorStrength Strength)
{
	switch (Strength)
	{
	case EDoorStrength::Steel: return TEXT("steel");
	case EDoorStrength::Reinforced: return TEXT("reinforced");
	default: return TEXT("wood");
	}
}

float DoorRules::BreakChance(float Power, EDoorStrength Strength, const FDoorTuning& Tuning)
{
	const float Resist = FMath::Clamp(Tuning.Strengths[FMath::Clamp(static_cast<int32>(Strength), 0, 2)].Resist, 0.f, 0.99f);
	return FMath::Clamp((Power - Resist) / (1.f - Resist), 0.f, 1.f);
}

float DoorRules::GetLockPower(const FString& WeaponId, const FDoorTuning& Tuning)
{
	if (WeaponId.IsEmpty())
	{
		return 0.f;
	}
	for (const TPair<FString, float>& Entry : Tuning.LockPower)
	{
		if (Entry.Key.Equals(WeaponId, ESearchCase::IgnoreCase))
		{
			return FMath::Clamp(Entry.Value, 0.f, 1.f);
		}
	}
	return FMath::Clamp(Tuning.LockPowerDefault, 0.f, 1.f);
}

float DoorRules::GetWeakestLockPower(const FDoorTuning& Tuning)
{
	float Weakest = FMath::Clamp(Tuning.LockPowerDefault, 0.f, 1.f);
	for (const TPair<FString, float>& Entry : Tuning.LockPower)
	{
		Weakest = FMath::Min(Weakest, FMath::Clamp(Entry.Value, 0.f, 1.f));
	}
	return Weakest;
}

float DoorRules::GetKickPower(const FDoorTuning& Tuning)
{
	return FMath::Clamp(FMath::Min(Tuning.KickPower, GetWeakestLockPower(Tuning)), 0.f, 1.f);
}

float DoorRules::GetKickChance(EDoorLeafState State, EDoorStrength Strength, const FDoorTuning& Tuning)
{
	if ((State != EDoorLeafState::Closed && State != EDoorLeafState::Locked) || !Tuning.Strengths[FMath::Clamp(static_cast<int32>(Strength), 0, 2)].bKickable)
	{
		return 0.f;
	}
	const float Power = GetKickPower(Tuning) + (State == EDoorLeafState::Closed ? FMath::Max(0.f, Tuning.KickLatchBonus) : 0.f);
	return BreakChance(FMath::Min(Power, 1.f), Strength, Tuning);
}

float DoorRules::GetLockShotChance(const FString& WeaponId, EDoorStrength Strength, const FDoorTuning& Tuning)
{
	const float Power = GetLockPower(WeaponId, Tuning);
	return Power > 0.f ? BreakChance(Power, Strength, Tuning) : 0.f;
}

EDoorStrength DoorRules::ResolveStrength(const FString& TagValue, const FString& Role, bool bStoryLocked, const FString& OpeningId, const FDoorTuning& Tuning)
{
	EDoorStrength Out = Tuning.StrengthDefault;
	if (ParseStrength(TagValue, Out))
	{
		return Out;
	}
	if (bStoryLocked && ParseStrength(Tuning.StrengthStoryLocked, Out))
	{
		return Out;
	}
	if (const FString* Rule = Role.IsEmpty() ? nullptr : Tuning.StrengthByRole.Find(Role))
	{
		FString First;
		FString Second;
		if (Rule->Split(TEXT("/"), &First, &Second))
		{
			// The same door always gets the same: a hash of its opening id picks the second for strength_mix_second_share of the doors.
			const float Pick = static_cast<float>(FCrc::StrCrc32(*OpeningId) % 10000u) / 10000.f;
			if (ParseStrength(Pick < Tuning.StrengthMixSecondShare ? Second : First, Out))
			{
				return Out;
			}
		}
		else if (ParseStrength(*Rule, Out))
		{
			return Out;
		}
	}
	return Tuning.StrengthDefault;
}

TMap<FString, DoorRules::FLayoutDoor> DoorRules::ParseLayoutDoors(const FJsonObject& Layout)
{
	TMap<FString, FLayoutDoor> Out;
	const TArray<TSharedPtr<FJsonValue>>* Buildings = nullptr;
	if (!Layout.TryGetArrayField(TEXT("buildings"), Buildings) || !Buildings)
	{
		return Out;
	}
	for (const TSharedPtr<FJsonValue>& BuildingValue : *Buildings)
	{
		const TSharedPtr<FJsonObject> Building = BuildingValue.IsValid() ? BuildingValue->AsObject() : nullptr;
		if (!Building.IsValid())
		{
			continue;
		}
		const FString BuildingId = Building->GetStringField(TEXT("id"));
		TSet<FString> StoryLocked;
		const TArray<TSharedPtr<FJsonValue>>* Stories = nullptr;
		if (Building->TryGetArrayField(TEXT("stories"), Stories) && Stories)
		{
			for (const TSharedPtr<FJsonValue>& StoryValue : *Stories)
			{
				const TSharedPtr<FJsonObject> Story = StoryValue.IsValid() ? StoryValue->AsObject() : nullptr;
				const TArray<TSharedPtr<FJsonValue>>* Bricks = nullptr;
				if (!Story.IsValid() || !Story->TryGetArrayField(TEXT("bricks"), Bricks) || !Bricks)
				{
					continue;
				}
				for (const TSharedPtr<FJsonValue>& Brick : *Bricks)
				{
					// "door_state(<opening id>,locked)"
					FString Text = Brick.IsValid() ? Brick->AsString() : FString();
					FString Args;
					if (Text.StartsWith(TEXT("door_state(")) && Text.EndsWith(TEXT(")")))
					{
						Args = Text.Mid(11, Text.Len() - 12);
						FString Id;
						FString State;
						if (Args.Split(TEXT(","), &Id, &State) && State.TrimStartAndEnd().Equals(TEXT("locked"), ESearchCase::IgnoreCase))
						{
							StoryLocked.Add(Id.TrimStartAndEnd());
						}
					}
				}
			}
		}
		const auto AddOpenings = [&Out, &BuildingId, &StoryLocked](const FJsonObject& Holder)
		{
			const TArray<TSharedPtr<FJsonValue>>* Openings = nullptr;
			if (!Holder.TryGetArrayField(TEXT("openings"), Openings) || !Openings)
			{
				return;
			}
			for (const TSharedPtr<FJsonValue>& OpeningValue : *Openings)
			{
				const TSharedPtr<FJsonObject> Opening = OpeningValue.IsValid() ? OpeningValue->AsObject() : nullptr;
				FString Id;
				if (!Opening.IsValid() || Opening->GetStringField(TEXT("kind")) != TEXT("door") || !Opening->TryGetStringField(TEXT("id"), Id) || Id.IsEmpty())
				{
					continue;
				}
				FLayoutDoor& Door = Out.FindOrAdd(Id);
				Door.BuildingId = BuildingId;
				Opening->TryGetStringField(TEXT("role"), Door.Role);
				Opening->TryGetStringField(TEXT("strength"), Door.Strength);
				Door.bStoryLocked = StoryLocked.Contains(Id);
			}
		};
		AddOpenings(*Building);
		const TArray<TSharedPtr<FJsonValue>>* Partitions = nullptr;
		if (Building->TryGetArrayField(TEXT("partitions"), Partitions) && Partitions)
		{
			for (const TSharedPtr<FJsonValue>& PartitionValue : *Partitions)
			{
				if (const TSharedPtr<FJsonObject> Partition = PartitionValue.IsValid() ? PartitionValue->AsObject() : nullptr; Partition.IsValid())
				{
					AddOpenings(*Partition);
				}
			}
		}
	}
	return Out;
}

void DoorRules::ParseLayoutPlaces(const FJsonObject& Layout, TArray<FLayoutPlace>& OutPlaces, TMap<FString, FBox2D>& OutFootprints)
{
	OutPlaces.Reset();
	OutFootprints.Reset();
	const double GroundZ = Layout.HasField(TEXT("ground_z")) ? Layout.GetNumberField(TEXT("ground_z")) : 0.0;
	const TArray<TSharedPtr<FJsonValue>>* Buildings = nullptr;
	if (!Layout.TryGetArrayField(TEXT("buildings"), Buildings) || !Buildings)
	{
		return;
	}
	const auto Point = [](const TSharedPtr<FJsonValue>& Value, FVector2D& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Pair = nullptr;
		if (!Value.IsValid() || !Value->TryGetArray(Pair) || !Pair || Pair->Num() < 2)
		{
			return false;
		}
		Out = FVector2D((*Pair)[0]->AsNumber(), (*Pair)[1]->AsNumber());
		return true;
	};
	for (const TSharedPtr<FJsonValue>& BuildingValue : *Buildings)
	{
		const TSharedPtr<FJsonObject> Building = BuildingValue.IsValid() ? BuildingValue->AsObject() : nullptr;
		if (!Building.IsValid())
		{
			continue;
		}
		const FString BuildingId = Building->GetStringField(TEXT("id"));
		const TArray<TSharedPtr<FJsonValue>>* Footprint = nullptr;
		FVector2D A;
		FVector2D B;
		if (Building->TryGetArrayField(TEXT("footprint"), Footprint) && Footprint && Footprint->Num() >= 2 && Point((*Footprint)[0], A) && Point((*Footprint)[1], B))
		{
			OutFootprints.Add(BuildingId, FBox2D(FVector2D(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y)), FVector2D(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y))));
		}
		// Storey heights: floors_detail[].height_z (absolute), storey 0 on the ground.
		TMap<int32, double> FloorZ;
		FloorZ.Add(0, GroundZ);
		const TArray<TSharedPtr<FJsonValue>>* Details = nullptr;
		if (Building->TryGetArrayField(TEXT("floors_detail"), Details) && Details)
		{
			for (const TSharedPtr<FJsonValue>& DetailValue : *Details)
			{
				const TSharedPtr<FJsonObject> Detail = DetailValue.IsValid() ? DetailValue->AsObject() : nullptr;
				if (Detail.IsValid() && Detail->HasField(TEXT("floor")) && Detail->HasField(TEXT("height_z")))
				{
					FloorZ.Add(static_cast<int32>(Detail->GetNumberField(TEXT("floor"))), Detail->GetNumberField(TEXT("height_z")));
				}
			}
		}
		const auto ZOf = [&FloorZ, GroundZ](int32 Floor)
		{
			const double* Z = FloorZ.Find(Floor);
			return Z ? *Z : GroundZ + 300.0 * Floor;
		};
		const auto AddOpenings = [&](const FJsonObject& Holder)
		{
			const TArray<TSharedPtr<FJsonValue>>* Openings = nullptr;
			if (!Holder.TryGetArrayField(TEXT("openings"), Openings) || !Openings)
			{
				return;
			}
			for (const TSharedPtr<FJsonValue>& OpeningValue : *Openings)
			{
				const TSharedPtr<FJsonObject> Opening = OpeningValue.IsValid() ? OpeningValue->AsObject() : nullptr;
				const TArray<TSharedPtr<FJsonValue>>* Pos = nullptr;
				FVector2D P;
				if (!Opening.IsValid() || Opening->GetStringField(TEXT("kind")) != TEXT("door") || !Opening->TryGetArrayField(TEXT("pos"), Pos) || !Pos
					|| Pos->Num() < 2)
				{
					continue;
				}
				P = FVector2D((*Pos)[0]->AsNumber(), (*Pos)[1]->AsNumber());
				FLayoutPlace& Place = OutPlaces.AddDefaulted_GetRef();
				Place.Id = Opening->GetStringField(TEXT("id"));
				Place.BuildingId = BuildingId;
				const int32 Floor = Opening->HasField(TEXT("floor")) ? static_cast<int32>(Opening->GetNumberField(TEXT("floor"))) : 0;
				Place.Center = FVector(P.X, P.Y, ZOf(Floor));
				const double Yaw = Opening->HasField(TEXT("yaw")) ? Opening->GetNumberField(TEXT("yaw")) : 0.0;
				Place.Normal = FVector2D(FMath::Cos(FMath::DegreesToRadians(Yaw)), FMath::Sin(FMath::DegreesToRadians(Yaw)));
				Place.WidthCm = Opening->HasField(TEXT("width")) ? static_cast<float>(Opening->GetNumberField(TEXT("width"))) : 100.f;
			}
		};
		AddOpenings(*Building);
		const TArray<TSharedPtr<FJsonValue>>* Partitions = nullptr;
		if (Building->TryGetArrayField(TEXT("partitions"), Partitions) && Partitions)
		{
			for (const TSharedPtr<FJsonValue>& PartitionValue : *Partitions)
			{
				if (const TSharedPtr<FJsonObject> Partition = PartitionValue.IsValid() ? PartitionValue->AsObject() : nullptr; Partition.IsValid())
				{
					AddOpenings(*Partition);
				}
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* Stairs = nullptr;
		if (Building->TryGetArrayField(TEXT("stairs"), Stairs) && Stairs)
		{
			for (const TSharedPtr<FJsonValue>& StairValue : *Stairs)
			{
				const TSharedPtr<FJsonObject> Stair = StairValue.IsValid() ? StairValue->AsObject() : nullptr;
				if (!Stair.IsValid())
				{
					continue;
				}
				const float Width = Stair->HasField(TEXT("width")) ? static_cast<float>(Stair->GetNumberField(TEXT("width"))) : 150.f;
				const int32 From = Stair->HasField(TEXT("from")) ? static_cast<int32>(Stair->GetNumberField(TEXT("from"))) : 0;
				const int32 To = Stair->HasField(TEXT("to")) ? static_cast<int32>(Stair->GetNumberField(TEXT("to"))) : From + 1;
				const TPair<const TCHAR*, int32> Mouths[] = { { TEXT("pos"), From }, { TEXT("top"), To } };
				for (const TPair<const TCHAR*, int32>& Mouth : Mouths)
				{
					const TArray<TSharedPtr<FJsonValue>>* Pos = nullptr;
					if (Stair->TryGetArrayField(Mouth.Key, Pos) && Pos && Pos->Num() >= 2)
					{
						FLayoutPlace& Place = OutPlaces.AddDefaulted_GetRef();
						Place.Id = Stair->GetStringField(TEXT("id")) + TEXT(":") + Mouth.Key;
						Place.BuildingId = BuildingId;
						Place.Center = FVector((*Pos)[0]->AsNumber(), (*Pos)[1]->AsNumber(), ZOf(Mouth.Value));
						Place.WidthCm = Width;
						Place.bStair = true;
					}
				}
			}
		}
	}
}

void DoorRules::OpenLeafSegments(float WidthCm, int32 LeafCount, float Sign, float Deg, TArray<TPair<FVector2D, FVector2D>>& OutSegments)
{
	OutSegments.Reset();
	const int32 Count = FMath::Clamp(LeafCount, 1, 2);
	const float Leaf = WidthCm / Count;
	const float Rad = FMath::DegreesToRadians(Deg);
	const float S = Sign >= 0.f ? 1.f : -1.f;
	constexpr float HingeGapCm = 4.f; // the leaf's own hinge edge touches the jamb
	const FVector2D HingeA(-WidthCm * 0.5f, 0.f);
	const FVector2D DirA(FMath::Cos(Rad), S * FMath::Sin(Rad));
	OutSegments.Emplace(HingeA + DirA * HingeGapCm, HingeA + DirA * Leaf);
	if (Count == 2)
	{
		const FVector2D HingeB(WidthCm * 0.5f, 0.f);
		const FVector2D DirB(-FMath::Cos(Rad), S * FMath::Sin(Rad));
		OutSegments.Emplace(HingeB + DirB * HingeGapCm, HingeB + DirB * Leaf);
	}
}

bool DoorRules::SegmentHitsBox(const FVector2D& A, const FVector2D& B, const FBox2D& Box)
{
	if (!Box.bIsValid)
	{
		return false;
	}
	// Liang-Barsky clip of A-B against the box.
	double T0 = 0.0;
	double T1 = 1.0;
	const FVector2D D = B - A;
	const double P[4] = { -D.X, D.X, -D.Y, D.Y };
	const double Q[4] = { A.X - Box.Min.X, Box.Max.X - A.X, A.Y - Box.Min.Y, Box.Max.Y - A.Y };
	for (int32 I = 0; I < 4; ++I)
	{
		if (FMath::IsNearlyZero(P[I]))
		{
			if (Q[I] < 0.0)
			{
				return false;
			}
			continue;
		}
		const double T = Q[I] / P[I];
		if (P[I] < 0.0)
		{
			T0 = FMath::Max(T0, T);
		}
		else
		{
			T1 = FMath::Min(T1, T);
		}
		if (T0 > T1)
		{
			return false;
		}
	}
	return true;
}

float DoorRules::FreeSwingDeg(float WidthCm, int32 LeafCount, float Sign, const TArray<FDoorSwingObstacle>& Obstacles, float MaxDeg, int32* OutKind)
{
	constexpr float StepDeg = 3.f;
	if (OutKind)
	{
		*OutKind = -1;
	}
	TArray<TPair<FVector2D, FVector2D>> Segments;
	float Free = 0.f;
	for (float Deg = StepDeg; Deg <= MaxDeg + 0.01f; Deg += StepDeg)
	{
		OpenLeafSegments(WidthCm, LeafCount, Sign, FMath::Min(Deg, MaxDeg), Segments);
		for (const FDoorSwingObstacle& Obstacle : Obstacles)
		{
			for (const TPair<FVector2D, FVector2D>& Segment : Segments)
			{
				if (SegmentHitsBox(Segment.Key, Segment.Value, Obstacle.Box))
				{
					if (OutKind)
					{
						*OutKind = Obstacle.Kind;
					}
					return Free;
				}
			}
		}
		Free = FMath::Min(Deg, MaxDeg);
	}
	return MaxDeg;
}

DoorRules::FDoorSwingChoice DoorRules::ChooseSwing(float WidthCm, int32 LeafCount, float PreferredSign, const TArray<FDoorSwingObstacle>& Obstacles, float MaxDeg,
	float MinDeg)
{
	FDoorSwingChoice Choice;
	int32 KindPlus = -1;
	int32 KindMinus = -1;
	Choice.FreeDegPlus = FreeSwingDeg(WidthCm, LeafCount, 1.f, Obstacles, MaxDeg, &KindPlus);
	Choice.FreeDegMinus = FreeSwingDeg(WidthCm, LeafCount, -1.f, Obstacles, MaxDeg, &KindMinus);
	const float Preferred = PreferredSign > 0.f ? 1.f : PreferredSign < 0.f ? -1.f : 0.f;
	const float PreferredFree = Preferred > 0.f ? Choice.FreeDegPlus : Choice.FreeDegMinus;
	if (Preferred != 0.f && PreferredFree >= MaxDeg - 0.01f)
	{
		Choice.Sign = Preferred;
	}
	else if (!FMath::IsNearlyEqual(Choice.FreeDegPlus, Choice.FreeDegMinus, 0.01f))
	{
		Choice.Sign = Choice.FreeDegPlus > Choice.FreeDegMinus ? 1.f : -1.f;
	}
	else
	{
		Choice.Sign = Preferred != 0.f ? Preferred : 1.f;
	}
	const float Free = Choice.Sign > 0.f ? Choice.FreeDegPlus : Choice.FreeDegMinus;
	Choice.BlockKind = Choice.Sign > 0.f ? KindPlus : KindMinus;
	Choice.OpenDeg = FMath::Clamp(Free, FMath::Min(MinDeg, MaxDeg), MaxDeg);
	return Choice;
}

DoorRules::FDoorSwingSides DoorRules::ChooseSwingSides(float WidthCm, int32 LeafCount, const TArray<FDoorSwingObstacle>& Obstacles, float MaxDeg, float MinDeg)
{
	TArray<FDoorSwingObstacle> Walls;
	for (const FDoorSwingObstacle& Obstacle : Obstacles)
	{
		if (Obstacle.Kind == 0)
		{
			Walls.Add(Obstacle);
		}
	}
	FDoorSwingSides Sides;
	const float Low = FMath::Min(MinDeg, MaxDeg);
	for (const float Sign : { 1.f, -1.f })
	{
		const float WallDeg = FreeSwingDeg(WidthCm, LeafCount, Sign, Walls, MaxDeg);
		const float AllDeg = FreeSwingDeg(WidthCm, LeafCount, Sign, Obstacles, MaxDeg);
		const float Deg = FMath::Clamp(WallDeg, Low, MaxDeg);
		// A doorway / stair met before the wall would stop the leaf (a wall-limited side is not "blocking": the wall stops it first).
		const bool bBlocks = AllDeg < WallDeg - 0.5f;
		(Sign > 0.f ? Sides.DegPlus : Sides.DegMinus) = Deg;
		(Sign > 0.f ? Sides.bBlocksPlus : Sides.bBlocksMinus) = bBlocks;
	}
	return Sides;
}

bool DoorRules::ShouldCloseBehind(bool bSideBlocks, bool bDoorwayOccupied, bool bSomebodyToCome, float QuietSeconds, float NeedSeconds)
{
	return bSideBlocks && !bDoorwayOccupied && !bSomebodyToCome && QuietSeconds >= NeedSeconds;
}

bool DoorRules::IsQuietInnerFloor(int32 Floor, int32 EntryCount, int32 QuietFloor, int32 QuietEntries)
{
	return QuietEntries > 0 && EntryCount >= QuietEntries && Floor <= QuietFloor;
}

bool DoorRules::MayRandomLockInner(int32 Floor, int32 EntryCount, float Roll, float Share, int32 QuietFloor, int32 QuietEntries)
{
	if (IsQuietInnerFloor(Floor, EntryCount, QuietFloor, QuietEntries))
	{
		return false;
	}
	return Roll < Share;
}

FDoorKickOutcome DoorRules::ResolveKick(EDoorLeafState State, EDoorStrength Strength, bool bHeavy, float HealthFraction, float RollOpen, float RollInjury,
	const FDoorTuning& Tuning)
{
	FDoorKickOutcome Outcome;
	if (State != EDoorLeafState::Closed && State != EDoorLeafState::Locked)
	{
		return Outcome;
	}
	Outcome.OpenChance = GetKickChance(State, Strength, Tuning);
	Outcome.InjuryChance = FMath::Clamp(Tuning.KickInjuryBase + (HealthFraction < Tuning.KickWoundedBelow ? Tuning.KickInjuryWounded : 0.f)
		+ (bHeavy ? Tuning.KickInjuryHeavy : 0.f), 0.f, 1.f);
	Outcome.bOpens = RollOpen < Outcome.OpenChance;
	Outcome.bInjures = RollInjury < Outcome.InjuryChance;
	return Outcome;
}

bool DoorRules::KeyFits(const TSet<FString>& Keys, const FString& BuildingId, const FString& OpeningId, const FString& DoorKeyId)
{
	return Keys.Contains(TEXT("*")) || (!OpeningId.IsEmpty() && Keys.Contains(OpeningId)) || (!BuildingId.IsEmpty() && Keys.Contains(BuildingId))
		|| (!DoorKeyId.IsEmpty() && Keys.Contains(DoorKeyId));
}

bool DoorRules::ChooseClickAction(EDoorLeafState State, bool bDoubleClick, bool bAttack, bool bHasKey, bool bBroken, EDoorAction& OutAction,
	bool bKnownLocked)
{
	switch (State)
	{
	case EDoorLeafState::Open:
		OutAction = EDoorAction::Close;
		return !bAttack && !bBroken; // a blown-off leaf cannot shut; Ctrl + click is a shot as ever
	case EDoorLeafState::Closed:
		if (bAttack)
		{
			return false; // nothing to shoot open: the quick fire at the leaf as ever
		}
		OutAction = bDoubleClick ? EDoorAction::Kick : EDoorAction::Open;
		return true;
	case EDoorLeafState::Locked:
		if (bAttack)
		{
			OutAction = EDoorAction::ShootLock;
			return true;
		}
		if (bDoubleClick)
		{
			OutAction = EDoorAction::Kick;
			return true;
		}
		if (bHasKey)
		{
			OutAction = EDoorAction::Unlock;
			return true;
		}
		// No key (designer 2026-11-22): he walks up and tries the handle first; once the squad knows it is locked the caller says how else.
		OutAction = EDoorAction::TryOpen;
		return !bKnownLocked;
	default:
		return false;
	}
}

bool DoorRules::FindTagValue(const TArray<FName>& Tags, const TCHAR* Key, FString& OutValue)
{
	const FString Prefix = FString(Key) + TEXT("=");
	for (const FName& Tag : Tags)
	{
		const FString Text = Tag.ToString();
		if (Text.StartsWith(Prefix, ESearchCase::IgnoreCase))
		{
			OutValue = Text.Mid(Prefix.Len());
			return true;
		}
	}
	return false;
}

EDoorSide DoorRules::ChooseHandSide(const FVector2D& LocalOperative, float HandleLocalX, bool bSwap)
{
	// Facing the leaf plane from side s (= sign of his Y) he looks along -s Y; his right hand points along +s X (UE: right = forward
	// turned +90 deg yaw). The handle is to his right when (HandleX - his X) * s >= 0.
	const float Side = LocalOperative.Y >= 0.f ? 1.f : -1.f;
	const bool bRight = (HandleLocalX - LocalOperative.X) * Side >= 0.f;
	return bRight != bSwap ? EDoorSide::Right : EDoorSide::Left;
}

EDoorSide DoorRules::ChooseKickLeg(EDoorSide First, int32 AttemptIndex)
{
	const bool bSame = (FMath::Max(0, AttemptIndex) % 2) == 0;
	return bSame ? First : (First == EDoorSide::Left ? EDoorSide::Right : EDoorSide::Left);
}

EDoorClip DoorRules::ChooseOpenClip(bool bForced)
{
	return bForced ? EDoorClip::Push : EDoorClip::Open;
}

const TCHAR* DoorRules::ClipKindName(EDoorClip Clip)
{
	switch (Clip)
	{
	case EDoorClip::Kick: return TEXT("kick");
	case EDoorClip::Push: return TEXT("push");
	default: return TEXT("open");
	}
}

FString DoorRules::ClipKey(EDoorClip Clip, EDoorSide Side)
{
	return FString::Printf(TEXT("%s_%s"), ClipKindName(Clip), Side == EDoorSide::Left ? TEXT("l") : TEXT("r"));
}

FString DoorRules::FindClipPath(const FDoorTuning& Tuning, const FString& SkeletonName, bool bUseDefault, EDoorClip Clip, EDoorSide Side)
{
	const FString Key = ClipKey(Clip, Side);
	if (const TMap<FString, FString>* Set = Tuning.MontageSets.Find(SkeletonName))
	{
		if (const FString* Path = Set->Find(Key))
		{
			return *Path;
		}
	}
	if (!bUseDefault)
	{
		return FString();
	}
	if (const TMap<FString, FString>* Set = Tuning.MontageSets.Find(TEXT("default")))
	{
		if (const FString* Path = Set->Find(Key))
		{
			return *Path;
		}
	}
	return Clip == EDoorClip::Kick ? Tuning.KickMontage : Tuning.HandOpenMontage;
}

float DoorRules::GetClipApproachCm(EDoorClip Clip, const FDoorTuning& Tuning)
{
	const float* Cm = Tuning.MontageApproachCm.Find(ClipKindName(Clip));
	return Cm ? *Cm : Tuning.ApproachCm;
}

bool DoorRules::HasClipApproach(EDoorClip Clip, const FDoorTuning& Tuning)
{
	return Tuning.MontageApproachCm.Contains(ClipKindName(Clip));
}

bool DoorRules::ClipMovesPawn(bool bOperative, bool bOwnSet, bool bClipOnPawnSkeleton, bool bClipHasRootMotion)
{
	return bOperative && (!bOwnSet || (bClipOnPawnSkeleton && bClipHasRootMotion));
}

float DoorRules::GetContactSeconds(EDoorClip Clip, bool bClipPlays, const FDoorTuning& Tuning)
{
	if (bClipPlays)
	{
		if (const float* Seconds = Tuning.MontageContactSeconds.Find(ClipKindName(Clip)))
		{
			return *Seconds;
		}
	}
	switch (Clip)
	{
	case EDoorClip::Kick: return Tuning.KickSeconds;
	case EDoorClip::Push: return Tuning.PushOpenSeconds;
	default: return Tuning.HandOpenSeconds;
	}
}

bool DoorRules::ParseBreachAction(const FString& Text, EDoorAction& OutAction)
{
	static const TPair<const TCHAR*, EDoorAction> Names[] = {
		{ TEXT("key"), EDoorAction::Unlock }, { TEXT("shoot"), EDoorAction::ShootLock }, { TEXT("kick"), EDoorAction::Kick },
		{ TEXT("blast"), EDoorAction::Blast } };
	const FString Trimmed = Text.TrimStartAndEnd();
	for (const TPair<const TCHAR*, EDoorAction>& Name : Names)
	{
		if (Trimmed.Equals(Name.Key, ESearchCase::IgnoreCase))
		{
			OutAction = Name.Value;
			return true;
		}
	}
	return false;
}

FString DoorRules::GetArchetypeKey(bool bHostileOperative, EEnemyArchetype Archetype)
{
	if (bHostileOperative)
	{
		return TEXT("pmc");
	}
	switch (Archetype)
	{
	case EEnemyArchetype::FrostHound: return TEXT("frost_hound");
	case EEnemyArchetype::Brute: return TEXT("brute");
	case EEnemyArchetype::Frostbitten: return TEXT("frostbitten");
	case EEnemyArchetype::Cutter: return TEXT("cutter");
	case EEnemyArchetype::Marksman: return TEXT("marksman");
	case EEnemyArchetype::Base: return TEXT("base");
	default: return TEXT("default");
	}
}

const TArray<EDoorAction>& DoorRules::GetBreachActions(const FDoorTuning& Tuning, const FString& ArchetypeKey)
{
	const TArray<EDoorAction>* Found = Tuning.BreachActions.Find(ArchetypeKey.ToLower());
	return Found ? *Found : Tuning.BreachDefault;
}

bool DoorRules::NextBreachAction(const TArray<EDoorAction>& Actions, const TSet<EDoorAction>& Tried, const FDoorBreachAbility& Ability,
	const FDoorTuning& Tuning, EDoorAction& OutAction)
{
	for (const EDoorAction Action : Actions)
	{
		if (Tried.Contains(Action))
		{
			continue;
		}
		const bool bCan = (Action == EDoorAction::Unlock && Ability.bHasKey) || (Action == EDoorAction::ShootLock && Ability.bCanShoot)
			|| (Action == EDoorAction::Blast && Ability.bHasGrenade) || (Action == EDoorAction::Kick && Ability.bCanKick && Tuning.bKickEnabled);
		if (bCan)
		{
			OutAction = Action;
			return true;
		}
	}
	return false;
}

bool DoorRules::IsPathStoppedByDoor(const FVector2D& LocalPawn, const FVector2D& LocalPathEnd, const FVector2D& LocalGoal, float HalfWidthCm,
	float DetectCm)
{
	constexpr float GoalBeyondCm = 30.f;
	const bool bEndNear = FMath::Abs(LocalPathEnd.X) <= HalfWidthCm + DetectCm && FMath::Abs(LocalPathEnd.Y) <= DetectCm;
	const float EndSide = LocalPathEnd.Y >= 0.f ? 1.f : -1.f;
	const bool bPawnSide = LocalPawn.Y * EndSide >= -GoalBeyondCm; // he stands on the path end's side (or in the doorway)
	const bool bGoalBeyond = LocalGoal.Y * EndSide <= -GoalBeyondCm;
	return bEndNear && bPawnSide && bGoalBeyond;
}

TArray<FDoorKitDoorway> DoorRules::PairKitOpenLeaves(const TArray<FDoorKitLeafPiece>& Leaves, float ToleranceCm)
{
	TArray<FDoorKitDoorway> Out;
	TArray<bool> Used;
	Used.Init(false, Leaves.Num());
	// Pairs first: leaf A's doorway runs along its -Y; leaf B's hinge sits 2 x width along it, facing the same way.
	for (int32 A = 0; A < Leaves.Num(); ++A)
	{
		if (Used[A])
		{
			continue;
		}
		const FDoorKitLeafPiece& L = Leaves[A];
		const FVector2D Dir = -L.Right.GetSafeNormal();
		const FVector2D Expected = L.Hinge + Dir * (2.f * L.WidthCm);
		for (int32 B = 0; B < Leaves.Num(); ++B)
		{
			if (B == A || Used[B] || FVector2D::DotProduct(L.Along.GetSafeNormal(), Leaves[B].Along.GetSafeNormal()) < 0.95f
				|| FVector2D::Distance(Leaves[B].Hinge, Expected) > ToleranceCm)
			{
				continue;
			}
			FDoorKitDoorway& Door = Out.AddDefaulted_GetRef();
			Door.Center = (L.Hinge + Leaves[B].Hinge) * 0.5f;
			Door.Tangent = Dir;
			Door.WidthCm = static_cast<float>(FVector2D::Distance(L.Hinge, Leaves[B].Hinge));
			Door.LeafA = A;
			Door.LeafB = B;
			Used[A] = Used[B] = true;
			break;
		}
	}
	for (int32 A = 0; A < Leaves.Num(); ++A)
	{
		if (Used[A])
		{
			continue;
		}
		const FVector2D Dir = -Leaves[A].Right.GetSafeNormal();
		FDoorKitDoorway& Door = Out.AddDefaulted_GetRef();
		Door.Center = Leaves[A].Hinge + Dir * (Leaves[A].WidthCm * 0.5f);
		Door.Tangent = Dir;
		Door.WidthCm = Leaves[A].WidthCm;
		Door.LeafA = A;
	}
	return Out;
}

bool DoorRules::RayHitsLeaf(const FVector& LocalStart, const FVector& LocalEnd, float LeafWidthCm, float ThicknessCm, float HeightCm, bool bNegativeX,
	float SlackCm)
{
	const float X0 = bNegativeX ? -LeafWidthCm : 0.f;
	const float X1 = bNegativeX ? 0.f : LeafWidthCm;
	const FBox Box(FVector(X0 - SlackCm, -ThicknessCm * 0.5f - SlackCm, 0.f), FVector(X1 + SlackCm, ThicknessCm * 0.5f + SlackCm, HeightCm + SlackCm));
	const FVector Delta = LocalEnd - LocalStart;
	if (Delta.IsNearlyZero())
	{
		return Box.IsInside(LocalStart);
	}
	return FMath::LineBoxIntersection(Box, LocalStart, LocalEnd, Delta);
}

bool DoorRules::IsInShutDoorwaySnap(const FVector2D& Local, float HalfWidthCm, float DepthCm)
{
	return FMath::Abs(Local.X) <= HalfWidthCm + 10.f && FMath::Abs(Local.Y) <= DepthCm;
}

float DoorRules::FacingErrorDeg(const FVector2D& Forward, const FVector2D& LocalOperativeSide, const FVector2D& DoorNormal)
{
	const float Side = LocalOperativeSide.Y >= 0.f ? 1.f : -1.f;
	const FVector2D Into = (-DoorNormal * Side).GetSafeNormal();
	const float Dot = FMath::Clamp(FVector2D::DotProduct(Forward.GetSafeNormal(), Into), -1.f, 1.f);
	return FMath::RadiansToDegrees(FMath::Acos(Dot));
}

float DoorRules::GoalBehindDoorSide(const FVector2D& LocalGoal, float HalfWidthCm, float ReachCm)
{
	constexpr float GoalBeyondCm = 30.f;
	if (FMath::Abs(LocalGoal.X) > HalfWidthCm + ReachCm || FMath::Abs(LocalGoal.Y) < GoalBeyondCm || FMath::Abs(LocalGoal.Y) > ReachCm)
	{
		return 0.f;
	}
	return LocalGoal.Y > 0.f ? 1.f : -1.f;
}

float DoorRules::DeterministicRoll(int32 Seed, uint32 Salt)
{
	const FRandomStream Stream(static_cast<int32>(HashCombine(GetTypeHash(Seed), Salt)));
	return Stream.GetFraction();
}

uint32 DoorRules::RollSalt(const FString& DoorId, const FString& PawnId, int32 Attempt, const TCHAR* What)
{
	uint32 Hash = FCrc::StrCrc32(*DoorId);
	Hash = HashCombine(Hash, FCrc::StrCrc32(*PawnId));
	Hash = HashCombine(Hash, GetTypeHash(Attempt));
	return HashCombine(Hash, FCrc::StrCrc32(What));
}

FString DoorTuning::GetDefaultPath()
{
	return FPaths::ProjectContentDir() / TEXT("Data/Interactables/doors.json");
}

FDoorTuning DoorTuning::FromJson(const FJsonObject& Json, const FDoorTuning& Base)
{
	using namespace DoorRulesLocal;
	FDoorTuning Tuning = Base;
	ReadDoorFlag(Json, TEXT("enabled"), Tuning.bEnabled);
	ReadDoorNumber(Json, TEXT("open_s"), Tuning.OpenSeconds, 0.05f, 5.f);
	ReadDoorNumber(Json, TEXT("close_s"), Tuning.CloseSeconds, 0.05f, 5.f);
	ReadDoorNumber(Json, TEXT("auto_open_delay_s"), Tuning.AutoOpenDelaySeconds, 0.f, 5.f);
	ReadDoorNumber(Json, TEXT("zone_depth_cm"), Tuning.ZoneDepthCm, 20.f, 500.f);
	ReadDoorNumber(Json, TEXT("zone_slack_cm"), Tuning.ZoneSlackCm, 0.f, 200.f);
	ReadDoorNumber(Json, TEXT("path_look_ahead_cm"), Tuning.PathLookAheadCm, 20.f, 2000.f);
	ReadDoorNumber(Json, TEXT("heading_speed_cm_s"), Tuning.HeadingSpeedCmS, 1.f, 500.f);
	ReadDoorNumber(Json, TEXT("interact_reach_cm"), Tuning.InteractReachCm, 50.f, 500.f);
	ReadDoorNumber(Json, TEXT("approach_cm"), Tuning.ApproachCm, 40.f, 400.f);
	ReadDoorNumber(Json, TEXT("click_snap_cm"), Tuning.ClickSnapCm, 0.f, 150.f);
	ReadDoorNumber(Json, TEXT("interact_timeout_s"), Tuning.InteractTimeoutSeconds, 1.f, 120.f);
	ReadDoorNumber(Json, TEXT("single_leaf_max_cm"), Tuning.SingleLeafMaxCm, 40.f, 400.f);
	ReadDoorNumber(Json, TEXT("swing_deg"), Tuning.SwingDegrees, 10.f, 180.f);
	ReadDoorFlag(Json, TEXT("swing_fixed"), Tuning.bSwingFixed);
	ReadDoorFlag(Json, TEXT("swing_from_opener"), Tuning.bSwingFromOpener);
	const TArray<TSharedPtr<FJsonValue>>* Maps = nullptr;
	if (Json.TryGetArrayField(TEXT("enabled_maps"), Maps) && Maps)
	{
		for (const TSharedPtr<FJsonValue>& Map : *Maps)
		{
			FString Name;
			if (Map.IsValid() && Map->TryGetString(Name) && !Name.IsEmpty())
			{
				Tuning.EnabledMaps.Add(Name);
			}
		}
	}
	ReadDoorFlag(Json, TEXT("start_interior_closed"), Tuning.bStartInteriorClosed);
	ReadDoorNumber(Json, TEXT("start_interior_locked_share"), Tuning.StartInteriorLockedShare, 0.f, 1.f);
	double QuietFloor = 0.0;
	if (Json.TryGetNumberField(TEXT("start_interior_locked_quiet_floor"), QuietFloor))
	{
		Tuning.StartInteriorLockedQuietFloor = FMath::RoundToInt32(QuietFloor);
	}
	double QuietEntries = 0.0;
	if (Json.TryGetNumberField(TEXT("start_interior_locked_quiet_entries"), QuietEntries))
	{
		Tuning.StartInteriorLockedQuietEntries = FMath::Clamp(FMath::RoundToInt32(QuietEntries), 0, 32);
	}
	ReadDoorNumber(Json, TEXT("close_behind_s"), Tuning.CloseBehindSeconds, 0.f, 60.f);
	ReadDoorNumber(Json, TEXT("close_behind_radius_cm"), Tuning.CloseBehindRadiusCm, 0.f, 3000.f);
	ReadDoorNumber(Json, TEXT("swing_min_deg"), Tuning.SwingMinDeg, 0.f, 180.f);
	ReadDoorNumber(Json, TEXT("swing_zone_depth_cm"), Tuning.SwingZoneDepthCm, 0.f, 400.f);
	ReadDoorNumber(Json, TEXT("nav_entering_cost_cm"), Tuning.NavEnteringCostCm, 0.f, 10000.f);
	ReadDoorNumber(Json, TEXT("nav_box_depth_cm"), Tuning.NavBoxDepthCm, 10.f, 300.f);
	ReadDoorFlag(Json, TEXT("kick_enabled"), Tuning.bKickEnabled);
	ReadDoorNumber(Json, TEXT("kick_s"), Tuning.KickSeconds, 0.f, 20.f);
	ReadDoorNumber(Json, TEXT("kick_noise_m"), Tuning.KickNoiseCm, 0.f, 10000.f, 100.f);
	ReadDoorNumber(Json, TEXT("kick_power"), Tuning.KickPower, 0.f, 1.f);
	ReadDoorNumber(Json, TEXT("kick_latch_bonus"), Tuning.KickLatchBonus, 0.f, 1.f);
	const TSharedPtr<FJsonObject>* Strengths = nullptr;
	if (Json.TryGetObjectField(TEXT("strengths"), Strengths) && Strengths && Strengths->IsValid())
	{
		for (const EDoorStrength Strength : { EDoorStrength::Wood, EDoorStrength::Steel, EDoorStrength::Reinforced })
		{
			const TSharedPtr<FJsonObject>* Class = nullptr;
			if ((*Strengths)->TryGetObjectField(DoorRules::StrengthName(Strength), Class) && Class && Class->IsValid())
			{
				FDoorStrengthClass& Out = Tuning.Strengths[static_cast<int32>(Strength)];
				ReadDoorNumber(**Class, TEXT("resist"), Out.Resist, 0.f, 0.99f);
				ReadDoorFlag(**Class, TEXT("kickable"), Out.bKickable);
			}
		}
	}
	const TSharedPtr<FJsonObject>* ByRole = nullptr;
	if (Json.TryGetObjectField(TEXT("strength_by_role"), ByRole) && ByRole && ByRole->IsValid())
	{
		Tuning.StrengthByRole.Reset();
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*ByRole)->Values)
		{
			FString Name;
			if (!Entry.Key.StartsWith(TEXT("_")) && Entry.Value.IsValid() && Entry.Value->TryGetString(Name))
			{
				Tuning.StrengthByRole.Add(Entry.Key, Name);
			}
		}
	}
	ReadDoorNumber(Json, TEXT("strength_mix_second_share"), Tuning.StrengthMixSecondShare, 0.f, 1.f);
	Json.TryGetStringField(TEXT("strength_story_locked"), Tuning.StrengthStoryLocked);
	FString DefaultStrength;
	if (Json.TryGetStringField(TEXT("strength_default"), DefaultStrength))
	{
		DoorRules::ParseStrength(DefaultStrength, Tuning.StrengthDefault);
	}
	ReadDoorNumber(Json, TEXT("locked_try_s"), Tuning.LockedTrySeconds, 0.f, 5.f);
	ReadDoorNumber(Json, TEXT("heavy_min_width_cm"), Tuning.HeavyMinWidthCm, 0.f, 1000.f);
	float Attempts = static_cast<float>(Tuning.KickMaxAttempts);
	ReadDoorNumber(Json, TEXT("kick_max_attempts"), Attempts, 1.f, 20.f);
	Tuning.KickMaxAttempts = FMath::RoundToInt32(Attempts);
	ReadDoorNumber(Json, TEXT("kick_injury_base"), Tuning.KickInjuryBase, 0.f, 1.f);
	ReadDoorNumber(Json, TEXT("kick_injury_wounded"), Tuning.KickInjuryWounded, 0.f, 1.f);
	ReadDoorNumber(Json, TEXT("kick_wounded_below"), Tuning.KickWoundedBelow, 0.f, 1.f);
	ReadDoorNumber(Json, TEXT("kick_injury_heavy"), Tuning.KickInjuryHeavy, 0.f, 1.f);
	ReadDoorNumber(Json, TEXT("kick_injury_damage"), Tuning.KickInjuryDamage, 0.f, 500.f);
	ReadDoorNumber(Json, TEXT("hand_open_s"), Tuning.HandOpenSeconds, 0.f, 10.f);
	ReadDoorNumber(Json, TEXT("unlock_s"), Tuning.UnlockSeconds, 0.f, 20.f);
	const TSharedPtr<FJsonObject>* Shots = nullptr;
	if (Json.TryGetObjectField(TEXT("lock_power"), Shots) && Shots && Shots->IsValid())
	{
		Tuning.LockPower.Reset();
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Shots)->Values)
		{
			double Chance = 0.0;
			if (Entry.Key.StartsWith(TEXT("_")) || !Entry.Value.IsValid() || !Entry.Value->TryGetNumber(Chance))
			{
				continue;
			}
			if (Entry.Key == TEXT("default"))
			{
				Tuning.LockPowerDefault = FMath::Clamp(static_cast<float>(Chance), 0.f, 1.f);
			}
			else
			{
				Tuning.LockPower.Add(Entry.Key, FMath::Clamp(static_cast<float>(Chance), 0.f, 1.f));
			}
		}
	}
	ReadDoorNumber(Json, TEXT("lock_shot_range_cm"), Tuning.LockShotRangeCm, 50.f, 5000.f);
	ReadDoorNumber(Json, TEXT("lock_shot_stand_cm"), Tuning.LockShotStandCm, 50.f, 2000.f);
	ReadDoorNumber(Json, TEXT("lock_shot_resolve_s"), Tuning.LockShotResolveSeconds, 0.f, 10.f);
	ReadDoorNumber(Json, TEXT("blast_reach_cm"), Tuning.BlastReachCm, 0.f, 3000.f);
	const TArray<TSharedPtr<FJsonValue>>* Keys = nullptr;
	if (Json.TryGetArrayField(TEXT("start_keys"), Keys) && Keys)
	{
		Tuning.StartKeys.Reset();
		for (const TSharedPtr<FJsonValue>& Key : *Keys)
		{
			FString Text;
			if (Key.IsValid() && Key->TryGetString(Text) && !Text.IsEmpty())
			{
				Tuning.StartKeys.Add(Text);
			}
		}
	}
	Json.TryGetStringField(TEXT("hand_open_montage"), Tuning.HandOpenMontage);
	Json.TryGetStringField(TEXT("kick_montage"), Tuning.KickMontage);
	Json.TryGetStringField(TEXT("unlock_montage"), Tuning.UnlockMontage);
	// R2 (designer 2026-11-22): clips, the push after a forced lock, enemies vs locked doors, deterministic rolls.
	ReadDoorFlag(Json, TEXT("lock_shot_push"), Tuning.bLockShotPush);
	ReadDoorNumber(Json, TEXT("push_s"), Tuning.PushOpenSeconds, 0.f, 10.f);
	ReadDoorFlag(Json, TEXT("montage_swap_lr"), Tuning.bMontageSwapLR);
	const TSharedPtr<FJsonObject>* Sets = nullptr;
	if (Json.TryGetObjectField(TEXT("montages"), Sets) && Sets && Sets->IsValid())
	{
		Tuning.MontageSets.Reset();
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Set : (*Sets)->Values)
		{
			const TSharedPtr<FJsonObject>* Clips = nullptr;
			if (Set.Key.StartsWith(TEXT("_")) || !Set.Value.IsValid() || !Set.Value->TryGetObject(Clips) || !Clips || !Clips->IsValid())
			{
				continue;
			}
			TMap<FString, FString>& Out = Tuning.MontageSets.Add(Set.Key);
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Clip : (*Clips)->Values)
			{
				FString Path;
				if (!Clip.Key.StartsWith(TEXT("_")) && Clip.Value.IsValid() && Clip.Value->TryGetString(Path) && !Path.IsEmpty())
				{
					Out.Add(Clip.Key.ToLower(), Path);
				}
			}
		}
	}
	const TSharedPtr<FJsonObject>* Contacts = nullptr;
	if (Json.TryGetObjectField(TEXT("montage_contact_s"), Contacts) && Contacts && Contacts->IsValid())
	{
		Tuning.MontageContactSeconds.Reset();
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Contacts)->Values)
		{
			double Seconds = 0.0;
			if (!Entry.Key.StartsWith(TEXT("_")) && Entry.Value.IsValid() && Entry.Value->TryGetNumber(Seconds))
			{
				Tuning.MontageContactSeconds.Add(Entry.Key.ToLower(), FMath::Clamp(static_cast<float>(Seconds), 0.f, 10.f));
			}
		}
	}
	for (const TPair<const TCHAR*, TMap<FString, float>*>& Field : { TPair<const TCHAR*, TMap<FString, float>*>(TEXT("montage_root_cm"), &Tuning.MontageRootCm),
		TPair<const TCHAR*, TMap<FString, float>*>(TEXT("montage_approach_cm"), &Tuning.MontageApproachCm) })
	{
		const TSharedPtr<FJsonObject>* Map = nullptr;
		if (Json.TryGetObjectField(Field.Key, Map) && Map && Map->IsValid())
		{
			Field.Value->Reset();
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Map)->Values)
			{
				double Cm = 0.0;
				if (!Entry.Key.StartsWith(TEXT("_")) && Entry.Value.IsValid() && Entry.Value->TryGetNumber(Cm))
				{
					Field.Value->Add(Entry.Key.ToLower(), FMath::Clamp(static_cast<float>(Cm), 0.f, 1000.f));
				}
			}
		}
	}
	ReadDoorNumber(Json, TEXT("montage_approach_slack_cm"), Tuning.MontageApproachSlackCm, 5.f, 200.f);
	const TSharedPtr<FJsonObject>* Breach = nullptr;
	if (Json.TryGetObjectField(TEXT("breach"), Breach) && Breach && Breach->IsValid())
	{
		const FJsonObject& B = **Breach;
		ReadDoorFlag(B, TEXT("enabled"), Tuning.bBreachEnabled);
		ReadDoorNumber(B, TEXT("detect_cm"), Tuning.BreachDetectCm, 50.f, 1000.f);
		ReadDoorNumber(B, TEXT("interval_s"), Tuning.BreachIntervalSeconds, 0.05f, 5.f);
		ReadDoorNumber(B, TEXT("goal_door_cm"), Tuning.BreachGoalDoorCm, 0.f, 3000.f);
		ReadDoorNumber(B, TEXT("blast_stand_cm"), Tuning.BlastStandCm, 100.f, 3000.f);
		float LockShots = static_cast<float>(Tuning.BreachLockShots);
		ReadDoorNumber(B, TEXT("lock_shots"), LockShots, 1.f, 10.f);
		Tuning.BreachLockShots = FMath::RoundToInt32(LockShots);
		ReadDoorNumber(B, TEXT("give_up_s"), Tuning.BreachGiveUpSeconds, 0.f, 600.f);
		const auto ReadList = [](const TArray<TSharedPtr<FJsonValue>>& Values)
		{
			TArray<EDoorAction> Actions;
			for (const TSharedPtr<FJsonValue>& Value : Values)
			{
				FString Text;
				EDoorAction Action;
				if (Value.IsValid() && Value->TryGetString(Text) && DoorRules::ParseBreachAction(Text, Action))
				{
					Actions.AddUnique(Action);
				}
			}
			return Actions;
		};
		const TArray<TSharedPtr<FJsonValue>>* Default = nullptr;
		if (B.TryGetArrayField(TEXT("default"), Default) && Default)
		{
			Tuning.BreachDefault = ReadList(*Default);
		}
		const TSharedPtr<FJsonObject>* Archetypes = nullptr;
		if (B.TryGetObjectField(TEXT("archetypes"), Archetypes) && Archetypes && Archetypes->IsValid())
		{
			Tuning.BreachActions.Reset();
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Archetypes)->Values)
			{
				const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
				if (!Entry.Key.StartsWith(TEXT("_")) && Entry.Value.IsValid() && Entry.Value->TryGetArray(List) && List)
				{
					Tuning.BreachActions.Add(Entry.Key.ToLower(), ReadList(*List));
				}
			}
		}
	}
	double Seed = 0.0;
	if (Json.TryGetNumberField(TEXT("roll_seed"), Seed))
	{
		Tuning.RollSeed = static_cast<int32>(Seed);
	}
	return Tuning;
}

bool DoorTuning::Reload()
{
	using namespace DoorRulesLocal;
	bDoorTuningLoaded = true;
	DoorTuningCurrent = FDoorTuning();
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *GetDefaultPath()))
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("Doors: %s missing - defaults (switch off)"), *GetDefaultPath());
		return false;
	}
	TSharedPtr<FJsonObject> Json;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid())
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("Doors: %s is not valid JSON - defaults (switch off)"), *GetDefaultPath());
		return false;
	}
	DoorTuningCurrent = FromJson(*Json);
#if WITH_EDITOR
	// A checkout-local switch for the designer's editor only (BGEN 2026-11-22, Lead OK: doors on in CodexTactics-agents, off in main; the
	// [ConsoleVariables] line in Saved/Config/WindowsEditor/Engine.ini did not survive the editor's config save). Saved/ is never committed;
	// -game runs (smokes) ignore it.
	FString LocalText;
	const FString LocalPath = FPaths::ProjectSavedDir() / TEXT("CodexLocal/doors.local.json");
	TSharedPtr<FJsonObject> Local;
	bool bLocalEnabled = false;
	if (GIsEditor && FFileHelper::LoadFileToString(LocalText, *LocalPath) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(LocalText), Local)
		&& Local.IsValid() && Local->TryGetBoolField(TEXT("enabled"), bLocalEnabled))
	{
		DoorTuningCurrent.bEnabled = bLocalEnabled;
		UE_LOG(LogCodexTactics, Display, TEXT("Doors: %s sets enabled=%d for this checkout's editor"), *LocalPath, bLocalEnabled ? 1 : 0);
	}
#endif
	return true;
}

void DoorTuning::SetForTesting(const FDoorTuning& Tuning)
{
	DoorRulesLocal::bDoorTuningLoaded = true;
	DoorRulesLocal::DoorTuningCurrent = Tuning;
}

const FDoorTuning& DoorTuning::Get()
{
	if (!DoorRulesLocal::bDoorTuningLoaded)
	{
		Reload();
	}
	return DoorRulesLocal::DoorTuningCurrent;
}

bool DoorTuning::ResolveSwitch(int32 Console, const FDoorTuning& Tuning)
{
	return Console < 0 ? Tuning.bEnabled : Console > 0;
}

bool DoorTuning::ResolveSwitchForMap(int32 Console, const FDoorTuning& Tuning, const FString& MapName)
{
	if (Console >= 0)
	{
		return Console > 0;
	}
	return Tuning.bEnabled || (!MapName.IsEmpty() && Tuning.EnabledMaps.ContainsByPredicate([&MapName](const FString& Each) { return Each.Equals(MapName, ESearchCase::IgnoreCase); }));
}

bool DoorTuning::IsOpenableOnFor(const FString& MapName)
{
	return ResolveSwitchForMap(DoorRulesLocal::CVarDoorsOpenable.GetValueOnGameThread(), Get(), MapName);
}

bool DoorTuning::IsOpenableOn()
{
	return ResolveSwitch(DoorRulesLocal::CVarDoorsOpenable.GetValueOnGameThread(), Get());
}

int32 DoorTuning::GetRollSeed()
{
	const int32 Console = DoorRulesLocal::CVarDoorsSeed.GetValueOnGameThread();
	if (Console >= 0)
	{
		return Console;
	}
	int32 CommandLine = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("DoorSeed="), CommandLine))
	{
		return CommandLine;
	}
	return Get().RollSeed;
}
