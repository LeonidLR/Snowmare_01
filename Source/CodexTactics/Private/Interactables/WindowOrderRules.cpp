// UE-only (no Godot reference). Window order rules: Interactables/WindowOrderRules.h.
#include "Interactables/WindowOrderRules.h"
#include "CodexTactics.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace WindowOrderRulesLocal
{
	FWindowOrderTuning Current;
	bool bLoaded = false;

	void ReadFlag(const TSharedPtr<FJsonObject>& Json, const TCHAR* Key, bool& Out)
	{
		double Value = 0.0;
		if (Json->TryGetNumberField(Key, Value))
		{
			Out = Value != 0.0;
		}
	}

	void ReadCm(const TSharedPtr<FJsonObject>& Json, const TCHAR* Key, float& Out)
	{
		double Value = 0.0;
		if (Json->TryGetNumberField(Key, Value) && Value > 0.0)
		{
			Out = static_cast<float>(Value);
		}
	}
}

FString WindowOrderRules::FilePath()
{
	return FPaths::ProjectContentDir() / TEXT("Data/Interactables/windows.json");
}

FWindowOrderTuning WindowOrderRules::Parse(const FString& JsonText)
{
	using namespace WindowOrderRulesLocal;
	FWindowOrderTuning Tuning;
	TSharedPtr<FJsonObject> Json;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonText), Json) || !Json.IsValid())
	{
		return Tuning;
	}
	ReadFlag(Json, TEXT("solo_group_combat_windows"), Tuning.bSoloGroupCombatWindows);
	ReadFlag(Json, TEXT("solo_group_whole_squad_only"), Tuning.bSoloGroupWholeSquadOnly);
	ReadFlag(Json, TEXT("solo_run_in_fight_windows"), Tuning.bSoloRunInFightWindows);
	ReadFlag(Json, TEXT("formation_run_in_fight_windows"), Tuning.bFormationRunInFightWindows);
	ReadFlag(Json, TEXT("formation_follow_broken_only"), Tuning.bFormationFollowBrokenOnly);
	ReadCm(Json, TEXT("approach_past_cm"), Tuning.ApproachPastCm);
	ReadCm(Json, TEXT("landing_past_cm"), Tuning.LandingPastCm);
	ReadCm(Json, TEXT("approach_match_cm"), Tuning.ApproachMatchCm);
	ReadCm(Json, TEXT("approach_arrive_cm"), Tuning.ApproachArriveCm);
	ReadCm(Json, TEXT("solo_group_queue_spacing_cm"), Tuning.SoloGroupQueueSpacingCm);
	return Tuning;
}

void WindowOrderRules::Reload()
{
	using namespace WindowOrderRulesLocal;
	FString Text;
	Current = FFileHelper::LoadFileToString(Text, *FilePath()) ? Parse(Text) : FWindowOrderTuning();
	bLoaded = true;
}

const FWindowOrderTuning& WindowOrderRules::Get()
{
	using namespace WindowOrderRulesLocal;
	if (!bLoaded)
	{
		Reload();
	}
	return Current;
}

bool WindowOrderRules::MoveOpensWindows(bool bSolo, int32 SelectedCount, int32 SquadCount, bool bInFight, bool bSprint, const FWindowOrderTuning& Tuning)
{
	if (!bInFight)
	{
		return false; // rule 3: out of a fight only the window click opens a window
	}
	if (!bSolo)
	{
		return bSprint && Tuning.bFormationRunInFightWindows; // rule 2: the formation never takes a whole window on its own
	}
	const bool bGroup = SelectedCount >= 2 && (!Tuning.bSoloGroupWholeSquadOnly || SelectedCount >= SquadCount);
	return (bGroup && Tuning.bSoloGroupCombatWindows) || (bSprint && Tuning.bSoloRunInFightWindows);
}

bool WindowOrderRules::MayTakeWholeWindow(bool bWindowOrder, bool bFormationFollower, const FWindowOrderTuning& Tuning)
{
	return bWindowOrder && !(bFormationFollower && Tuning.bFormationFollowBrokenOnly);
}

FWindowSideChoice WindowOrderRules::ChooseSide(const FVector& OperativeLocation, const FVector& Center, const FVector& ActorForward,
	const FVector& ActorRight, float SizeX, float SizeY, float ApproachPastCm, float LandingPastCm)
{
	FWindowSideChoice Choice;
	const FVector Raw = (SizeX < SizeY ? ActorForward : ActorRight).GetSafeNormal2D();
	Choice.Through = Raw.IsNearlyZero() ? FVector::ForwardVector : Raw;
	const float Half = 0.5f * FMath::Min(FMath::Abs(SizeX), FMath::Abs(SizeY));
	const float SideDot = FVector::DotProduct(FVector(OperativeLocation.X - Center.X, OperativeLocation.Y - Center.Y, 0.f), Choice.Through);
	Choice.OperativeSide = SideDot >= 0.f ? 1.f : -1.f;
	const float ApproachSpan = Half + FMath::Max(0.f, ApproachPastCm);
	const float LandingSpan = Half + FMath::Max(0.f, LandingPastCm);
	Choice.Approach = Center + Choice.Through * (Choice.OperativeSide * ApproachSpan);
	Choice.Landing = Center - Choice.Through * (Choice.OperativeSide * LandingSpan);
	Choice.Approach.Z = OperativeLocation.Z;
	Choice.Landing.Z = OperativeLocation.Z;
	Choice.Yaw = static_cast<float>((-Choice.Through * Choice.OperativeSide).Rotation().Yaw);
	return Choice;
}

TOptional<FVector> WindowOrderRules::QueueThroughWindow(const FVector& Approach, const FVector& Landing, const FVector& LeaderLocation,
	int32 Rank, float SpacingCm)
{
	const FVector Through = (Landing - Approach).GetSafeNormal2D();
	if (Through.IsNearlyZero() || Rank < 0 || SpacingCm < 0.f)
	{
		return {};
	}
	const FVector Mid = (Approach + Landing) * 0.5f;
	if (FVector::DotProduct(LeaderLocation - Mid, Through) <= 0.f)
	{
		return {};
	}
	FVector Place = Landing + Through * (static_cast<float>(Rank + 1) * SpacingCm);
	Place.Z = Landing.Z;
	return Place;
}

bool WindowOrderRules::FileSoloGroup(const FVector& Feet, const FVector& Approach, const FVector& Landing, int32 Rank, float SpacingCm,
	FVector& OutApproach, FVector& OutDestination)
{
	const FVector Climb = (Landing - Approach).GetSafeNormal2D();
	if (Climb.IsNearlyZero() || Rank < 0 || SpacingCm < 0.f)
	{
		return false;
	}
	const FVector Mid = (Approach + Landing) * 0.5f;
	const bool bFarSide = FVector::DotProduct(FVector(Feet.X - Mid.X, Feet.Y - Mid.Y, 0.f), Climb) > 0.f;
	const FVector UseApproach = bFarSide ? (Mid * 2.f - Approach) : Approach;
	const FVector UseLanding = bFarSide ? (Mid * 2.f - Landing) : Landing;
	const FVector UseClimb = (UseLanding - UseApproach).GetSafeNormal2D();
	if (UseClimb.IsNearlyZero())
	{
		return false;
	}
	OutApproach = UseApproach;
	OutDestination = UseLanding + UseClimb * (static_cast<float>(Rank) * SpacingCm);
	OutApproach.Z = Feet.Z;
	OutDestination.Z = Feet.Z;
	return true;
}
