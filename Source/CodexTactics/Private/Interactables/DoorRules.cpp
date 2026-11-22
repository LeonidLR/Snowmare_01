#include "Interactables/DoorRules.h"

#include "CodexTactics.h"
#include "Dom/JsonObject.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
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

FDoorKickOutcome DoorRules::ResolveKick(EDoorLeafState State, bool bHeavy, float HealthFraction, float RollOpen, float RollInjury,
	const FDoorTuning& Tuning)
{
	FDoorKickOutcome Outcome;
	if (State != EDoorLeafState::Closed && State != EDoorLeafState::Locked)
	{
		return Outcome;
	}
	const float Base = State == EDoorLeafState::Locked ? Tuning.KickChanceLocked : Tuning.KickChanceClosed;
	Outcome.OpenChance = FMath::Clamp(Base * (bHeavy ? Tuning.KickHeavyFactor : 1.f), 0.f, 1.f);
	Outcome.InjuryChance = FMath::Clamp(Tuning.KickInjuryBase + (HealthFraction < Tuning.KickWoundedBelow ? Tuning.KickInjuryWounded : 0.f)
		+ (bHeavy ? Tuning.KickInjuryHeavy : 0.f), 0.f, 1.f);
	Outcome.bOpens = RollOpen < Outcome.OpenChance;
	Outcome.bInjures = RollInjury < Outcome.InjuryChance;
	return Outcome;
}

float DoorRules::GetLockShotChance(const FString& WeaponId, const FDoorTuning& Tuning)
{
	if (WeaponId.IsEmpty())
	{
		return 0.f;
	}
	for (const TPair<FString, float>& Entry : Tuning.LockShotChance)
	{
		if (Entry.Key.Equals(WeaponId, ESearchCase::IgnoreCase))
		{
			return FMath::Clamp(Entry.Value, 0.f, 1.f);
		}
	}
	return FMath::Clamp(Tuning.LockShotChanceDefault, 0.f, 1.f);
}

bool DoorRules::KeyFits(const TSet<FString>& Keys, const FString& BuildingId, const FString& OpeningId, const FString& DoorKeyId)
{
	return Keys.Contains(TEXT("*")) || (!OpeningId.IsEmpty() && Keys.Contains(OpeningId)) || (!BuildingId.IsEmpty() && Keys.Contains(BuildingId))
		|| (!DoorKeyId.IsEmpty() && Keys.Contains(DoorKeyId));
}

bool DoorRules::ChooseClickAction(EDoorLeafState State, bool bDoubleClick, bool bAttack, bool bHasKey, bool bBroken, EDoorAction& OutAction)
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
		OutAction = EDoorAction::Unlock;
		return bHasKey; // no key: the caller says how else
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
	ReadDoorNumber(Json, TEXT("nav_entering_cost_cm"), Tuning.NavEnteringCostCm, 0.f, 10000.f);
	ReadDoorNumber(Json, TEXT("nav_box_depth_cm"), Tuning.NavBoxDepthCm, 10.f, 300.f);
	ReadDoorFlag(Json, TEXT("kick_enabled"), Tuning.bKickEnabled);
	ReadDoorNumber(Json, TEXT("kick_s"), Tuning.KickSeconds, 0.f, 20.f);
	ReadDoorNumber(Json, TEXT("kick_noise_m"), Tuning.KickNoiseCm, 0.f, 10000.f, 100.f);
	ReadDoorNumber(Json, TEXT("kick_chance_closed"), Tuning.KickChanceClosed, 0.f, 1.f);
	ReadDoorNumber(Json, TEXT("kick_chance_locked"), Tuning.KickChanceLocked, 0.f, 1.f);
	ReadDoorNumber(Json, TEXT("kick_heavy_factor"), Tuning.KickHeavyFactor, 0.f, 1.f);
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
	if (Json.TryGetObjectField(TEXT("lock_shot_chance"), Shots) && Shots && Shots->IsValid())
	{
		Tuning.LockShotChance.Reset();
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Shots)->Values)
		{
			double Chance = 0.0;
			if (Entry.Key.StartsWith(TEXT("_")) || !Entry.Value.IsValid() || !Entry.Value->TryGetNumber(Chance))
			{
				continue;
			}
			if (Entry.Key == TEXT("default"))
			{
				Tuning.LockShotChanceDefault = FMath::Clamp(static_cast<float>(Chance), 0.f, 1.f);
			}
			else
			{
				Tuning.LockShotChance.Add(Entry.Key, FMath::Clamp(static_cast<float>(Chance), 0.f, 1.f));
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

bool DoorTuning::IsOpenableOn()
{
	return ResolveSwitch(DoorRulesLocal::CVarDoorsOpenable.GetValueOnGameThread(), Get());
}
