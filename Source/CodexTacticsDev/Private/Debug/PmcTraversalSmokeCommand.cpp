// Dev check (smart enemies T-12, docs/design/gdd/smart-enemies-roadmap.md):
// Can an enemy fighter (hostile operative, team 1, commander off) walk every stair, ladder (>= 7 rungs), mantle link,
// and drop link of a generated map (L_Gen_Depot_X4)?
//
// Acceptance:
//  - One hostile operative (team 1, commander off) is ordered across every stair flight, ladder (>= 7 rungs),
//    mantle link, and drop link (mantle and drop sampled deterministically with stride).
//  - Prints per link kind "N of M crossed" + failed ids with reason (no path / stuck / refused).
//  - Passes at >= 95 % per kind. If today's rate is lower, register knownFail in Scripts/test_map.json with numbers.
//
// Arguments:
//   -TraversalMantleSample=N (default 120)
//   -TraversalDropSample=N   (default 80)
//   -TraversalKinds=stairs,ladders,mantles,drops
//   -TraversalOnly=<id>
//   -TraversalControlSample=N (default 5 of each kind)
//   -TraversalNoControl
//
// powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -NoRetry -ExtraArgs -Multiprocess -Commands PmcTraversalSmoke

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AIController.h"
#include "AI/PmcSquadSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/KnockdownComponent.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Falls/DropLinkSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Interactables/MantleNavigation.h"
#include "Ladders/LadderClimbComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Survival/ColdSurvivalComponent.h"

namespace PmcTraversalSmoke
{
	constexpr float StepSeconds = 0.05f;
	constexpr float LevelHeightCm = 300.f;

	struct FPmcTraversalConfig
	{
		float ArriveRadiusCm = 150.f;
		float LevelToleranceCm = 70.f;
		float StuckMoveCm = 50.f;
		float StuckSeconds = 4.f;
		float LegTimeoutSeconds = 15.f;
		float LadderArriveRadiusCm = 200.f;
		float StairInwardCm = 200.f;
		float MantleApproachOffsetCm = 120.f;
		float MantleGoalOffsetCm = 120.f;
		float DropApproachOffsetCm = 150.f;
		float DropGoalOffsetCm = 100.f;
		int32 DefaultMantleSample = 120;
		int32 DefaultDropSample = 80;

		static FPmcTraversalConfig Load()
		{
			FPmcTraversalConfig Config;
			FString Path = FPaths::ProjectContentDir() / TEXT("Data/AI/pmc_traversal.json");
			if (!FPaths::FileExists(Path))
			{
				Path = FPaths::ProjectDir() / TEXT("docs/port/gemini/pmc_traversal_config.json");
			}
			FString JsonText;
			TSharedPtr<FJsonObject> Obj;
			if (FFileHelper::LoadFileToString(JsonText, *Path) &&
				FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonText), Obj) &&
				Obj.IsValid())
			{
				Obj->TryGetNumberField(TEXT("arrive_radius_cm"), Config.ArriveRadiusCm);
				Obj->TryGetNumberField(TEXT("level_tolerance_cm"), Config.LevelToleranceCm);
				Obj->TryGetNumberField(TEXT("stuck_move_cm"), Config.StuckMoveCm);
				Obj->TryGetNumberField(TEXT("stuck_seconds"), Config.StuckSeconds);
				Obj->TryGetNumberField(TEXT("leg_timeout_seconds"), Config.LegTimeoutSeconds);
				Obj->TryGetNumberField(TEXT("ladder_arrive_radius_cm"), Config.LadderArriveRadiusCm);
				Obj->TryGetNumberField(TEXT("stair_inward_cm"), Config.StairInwardCm);
				Obj->TryGetNumberField(TEXT("mantle_approach_offset_cm"), Config.MantleApproachOffsetCm);
				Obj->TryGetNumberField(TEXT("mantle_goal_offset_cm"), Config.MantleGoalOffsetCm);
				Obj->TryGetNumberField(TEXT("drop_approach_offset_cm"), Config.DropApproachOffsetCm);
				Obj->TryGetNumberField(TEXT("drop_goal_offset_cm"), Config.DropGoalOffsetCm);
				int32 Val = 0;
				if (Obj->TryGetNumberField(TEXT("default_mantle_sample"), Val)) { Config.DefaultMantleSample = Val; }
				if (Obj->TryGetNumberField(TEXT("default_drop_sample"), Val)) { Config.DefaultDropSample = Val; }
			}
			return Config;
		}
	};

	struct FProgress
	{
		FVector Mark = FVector::ZeroVector;
		double MarkTime = 0.0;
		bool bStarted = false;

		void Reset(double Now, const FVector& Position)
		{
			Mark = Position;
			MarkTime = Now;
			bStarted = true;
		}

		bool IsStuck(double Now, const FVector& Position, double MoveCm, double Seconds)
		{
			if (!bStarted || FVector::Dist(Position, Mark) >= MoveCm)
			{
				Reset(Now, Position);
				return false;
			}
			return (Now - MarkTime) >= Seconds;
		}
	};

	enum class ELinkKind : uint8
	{
		Stair,
		Ladder,
		Mantle,
		Drop,
		Count
	};

	const TCHAR* KindName(ELinkKind Kind)
	{
		switch (Kind)
		{
		case ELinkKind::Stair: return TEXT("stairs");
		case ELinkKind::Ladder: return TEXT("ladders");
		case ELinkKind::Mantle: return TEXT("mantles");
		case ELinkKind::Drop: return TEXT("drops");
		default: return TEXT("unknown");
		}
	}

	struct FLeg
	{
		ELinkKind Kind = ELinkKind::Stair;
		FString Id;
		FVector StartSpot = FVector::ZeroVector;
		FRotator StartFacing = FRotator::ZeroRotator;
		FVector Goal = FVector::ZeroVector;
		FVector NavGoal = FVector::ZeroVector;
		float GoalZ = 0.f;
		FVector LinkStart = FVector::ZeroVector;
		FVector LinkMid = FVector::ZeroVector;
		FVector LinkEnd = FVector::ZeroVector;
		bool bMantleDown = false;
		float TimeoutSeconds = 15.f;
	};

	struct FFailure
	{
		FString Id;
		FString Reason;
		FVector Start = FVector::ZeroVector;
		FVector End = FVector::ZeroVector;
	};

	struct FKindStats
	{
		int32 TotalInMap = 0;
		int32 Sampled = 0;
		int32 Crossed = 0;
		int32 MantlesUpSampled = 0;
		int32 MantlesUpCrossed = 0;
		int32 MantlesDownSampled = 0;
		int32 MantlesDownCrossed = 0;
		TArray<FFailure> Failures;
	};

	enum class EPhase : uint8
	{
		WaitSubsystems,
		Setup,
		PrepareLeg,
		OrderLeg,
		RunLeg,
		Done
	};

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		FString MapName;
		FPmcTraversalConfig Config;
		EPhase Phase = EPhase::WaitSubsystems;
		float Time = 0.f;
		float PhaseTime = 0.f;
		double LegStartTime = 0.0;
		TWeakObjectPtr<AOperativeCharacter> Hostile;
		TArray<FLeg> Legs;
		int32 LegIndex = 0;
		FProgress Progress;
		bool bLadderClimbed = false;
		bool bLinkStartPassed = false;
		bool bLinkMidPassed = false;
		int32 InitialMantleCount = 0;
		bool bSawMantling = false;
		FKindStats Stats[static_cast<int32>(ELinkKind::Count)];
		FString OnlyFilter;
		TSet<FString> KindsFilter;
		int32 MantleSampleCount = 0;
		int32 DropSampleCount = 0;

		// Control run (Team 0 Player operative)
		bool bIsControlRun = false;
		bool bRunControl = true;
		int32 ControlSamplePerKind = 5;
		TArray<FLeg> ControlLegs;
		FKindStats ControlStats[static_cast<int32>(ELinkKind::Count)];
	};

	FVector2D ReadPoint(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		return Object->TryGetArrayField(Field, Array) && Array->Num() >= 2
			? FVector2D((*Array)[0]->AsNumber(), (*Array)[1]->AsNumber())
			: FVector2D::ZeroVector;
	}

	TSharedPtr<FJsonObject> LoadLayout(const FString& MapName, FString& OutError)
	{
		FString LevelText;
		TSharedPtr<FJsonObject> Level;
		FString File;
		const FString LevelFile = FPaths::ProjectContentDir() / TEXT("Data/LevelJson") / MapName + TEXT(".json");
		if (FFileHelper::LoadFileToString(LevelText, *LevelFile) &&
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(LevelText), Level) &&
			Level.IsValid() &&
			Level->HasField(TEXT("levelgen_layout")))
		{
			File = FPaths::ProjectDir() / Level->GetStringField(TEXT("levelgen_layout"));
		}
		FString Text;
		TSharedPtr<FJsonObject> Layout;
		if (File.IsEmpty() || !FFileHelper::LoadFileToString(Text, *File) ||
			!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Layout) ||
			!Layout.IsValid())
		{
			OutError = FString::Printf(TEXT("no accepted layout for %s (%s)"), *MapName, *File);
			return nullptr;
		}
		return Layout;
	}

	bool ProjectNav(UWorld* World, const FVector& Spot, FVector& OutNav, const FVector& Extent = FVector(150.f, 150.f, 200.f))
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		FNavLocation Out;
		if (Nav && Nav->ProjectPointToNavigation(Spot, Out, Extent))
		{
			OutNav = Out.Location;
			return true;
		}
		return false;
	}

	float FeetZ(const AOperativeCharacter& Operative)
	{
		return static_cast<float>(Operative.GetActorLocation().Z) - Operative.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	}

	void RecordFailure(FState& State, ELinkKind Kind, const FString& Id, const FString& Reason, const FVector& Start = FVector::ZeroVector, const FVector& End = FVector::ZeroVector)
	{
		FKindStats& KStats = State.bIsControlRun ? State.ControlStats[static_cast<int32>(Kind)] : State.Stats[static_cast<int32>(Kind)];
		KStats.Failures.Add({ Id, Reason, Start, End });
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s[%s] %s FAIL (%s) Start=(%.0f,%.0f,%.0f) Goal=(%.0f,%.0f,%.0f)"),
			State.bIsControlRun ? TEXT("Team0") : TEXT("Team1"), KindName(Kind), *Id, *Reason, Start.X, Start.Y, Start.Z, End.X, End.Y, End.Z);
	}

	void BuildLegs(FState& State, UWorld* World)
	{
		FString LayoutError;
		const TSharedPtr<FJsonObject> Layout = LoadLayout(State.MapName, LayoutError);
		if (!Layout.IsValid())
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("Smoke: Layout load error: %s"), *LayoutError);
		}

		float GroundZ = 0.f;
		if (Layout.IsValid() && Layout->HasField(TEXT("ground_z")))
		{
			GroundZ = static_cast<float>(Layout->GetNumberField(TEXT("ground_z")));
		}

		const bool bRunStairs = State.KindsFilter.IsEmpty() || State.KindsFilter.Contains(TEXT("stairs"));
		const bool bRunLadders = State.KindsFilter.IsEmpty() || State.KindsFilter.Contains(TEXT("ladders"));
		const bool bRunMantles = State.KindsFilter.IsEmpty() || State.KindsFilter.Contains(TEXT("mantles"));
		const bool bRunDrops = State.KindsFilter.IsEmpty() || State.KindsFilter.Contains(TEXT("drops"));

		// 1. Stairs (ALL stairs from layout)
		if (bRunStairs && Layout.IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* Buildings = nullptr;
			if (Layout->TryGetArrayField(TEXT("buildings"), Buildings))
			{
				for (const TSharedPtr<FJsonValue>& BV : *Buildings)
				{
					const TSharedPtr<FJsonObject> B = BV->AsObject();
					const TArray<TSharedPtr<FJsonValue>>* Stairs = nullptr;
					if (!B->TryGetArrayField(TEXT("stairs"), Stairs))
					{
						continue;
					}
					for (const TSharedPtr<FJsonValue>& SV : *Stairs)
					{
						const TSharedPtr<FJsonObject> S = SV->AsObject();
						const FString Id = S->GetStringField(TEXT("id"));
						State.Stats[static_cast<int32>(ELinkKind::Stair)].TotalInMap++;
						if (!State.OnlyFilter.IsEmpty() && State.OnlyFilter != Id)
						{
							continue;
						}

						const FVector2D Foot2D = ReadPoint(S, TEXT("pos"));
						const FVector2D Top2D = ReadPoint(S, TEXT("top"));
						const FVector2D Mid2D = S->HasField(TEXT("mid")) ? ReadPoint(S, TEXT("mid")) : Top2D;
						const FVector2D Dir = (Mid2D - Foot2D).GetSafeNormal();
						const FString KindStr = S->GetStringField(TEXT("kind"));
						const FVector2D Inward = KindStr == TEXT("external") ? (Top2D - Mid2D).GetSafeNormal() : Dir;
						const int32 LevelFrom = static_cast<int32>(S->GetNumberField(TEXT("from")));
						const int32 LevelTo = static_cast<int32>(S->GetNumberField(TEXT("to")));
						const float LowZ = GroundZ + LevelFrom * LevelHeightCm;
						const float HighZ = GroundZ + LevelTo * LevelHeightCm;

						FLeg Leg;
						Leg.Kind = ELinkKind::Stair;
						Leg.Id = Id;
						Leg.StartSpot = FVector(Foot2D.X - Dir.X * 100.f, Foot2D.Y - Dir.Y * 100.f, LowZ);
						Leg.StartFacing = FVector(Dir.X, Dir.Y, 0.f).Rotation();
						Leg.Goal = FVector(Top2D.X + Inward.X * State.Config.StairInwardCm, Top2D.Y + Inward.Y * State.Config.StairInwardCm, HighZ + 50.f);
						Leg.GoalZ = HighZ;
						Leg.LinkStart = FVector(Foot2D.X, Foot2D.Y, LowZ);
						Leg.LinkMid = FVector(Mid2D.X, Mid2D.Y, (LowZ + HighZ) * 0.5f);
						Leg.LinkEnd = FVector(Top2D.X, Top2D.Y, HighZ);
						const float StairDist = FVector::Dist(Leg.StartSpot, Leg.Goal);
						Leg.TimeoutSeconds = FMath::Max(State.Config.LegTimeoutSeconds, 10.f + (StairDist / 80.f));
						State.Legs.Add(Leg);
						State.Stats[static_cast<int32>(ELinkKind::Stair)].Sampled++;
					}
				}
			}
		}

		// 2. Ladders (ALL layout ladders with rungs >= 7)
		if (bRunLadders && Layout.IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* Ladders = nullptr;
			if (Layout->TryGetArrayField(TEXT("ladders"), Ladders))
			{
				for (const TSharedPtr<FJsonValue>& LV : *Ladders)
				{
					const TSharedPtr<FJsonObject> L = LV->AsObject();
					const int32 Rungs = static_cast<int32>(L->GetNumberField(TEXT("rungs")));
					if (Rungs < 7)
					{
						continue;
					}
					const FString Id = L->GetStringField(TEXT("id"));
					State.Stats[static_cast<int32>(ELinkKind::Ladder)].TotalInMap++;
					if (!State.OnlyFilter.IsEmpty() && State.OnlyFilter != Id)
					{
						continue;
					}
					const FVector2D Pos = ReadPoint(L, TEXT("pos"));
					const FVector2D Foot = ReadPoint(L, TEXT("foot"));
					const FVector2D Top = ReadPoint(L, TEXT("top"));
					const FVector2D Out = (Foot - Pos).GetSafeNormal();
					const int32 LevelFrom = static_cast<int32>(L->GetNumberField(TEXT("level_from")));
					const int32 LevelTo = static_cast<int32>(L->GetNumberField(TEXT("level_to")));
					const float LowZ = GroundZ + LevelFrom * LevelHeightCm;
					const float HighZ = GroundZ + LevelTo * LevelHeightCm;
					const FVector2D Below = Foot + Out * 150.f;
					const FVector2D Above = Top - Out * 50.f;

					FLeg Leg;
					Leg.Kind = ELinkKind::Ladder;
					Leg.Id = Id;
					Leg.StartSpot = FVector(Below.X, Below.Y, LowZ);
					Leg.StartFacing = (-FVector(Out.X, Out.Y, 0.f)).Rotation();
					Leg.Goal = FVector(Above.X, Above.Y, HighZ + 50.f);
					Leg.GoalZ = HighZ;
					Leg.LinkStart = FVector(Foot.X, Foot.Y, LowZ);
					Leg.LinkMid = FVector((Pos.X + Foot.X) * 0.5f, (Pos.Y + Foot.Y) * 0.5f, (LowZ + HighZ) * 0.5f);
					Leg.LinkEnd = FVector(Top.X, Top.Y, HighZ);
					const float LadderHeightCm = FMath::Abs(HighZ - LowZ);
					Leg.TimeoutSeconds = FMath::Max(State.Config.LegTimeoutSeconds, 10.f + (LadderHeightCm / 40.f));
					State.Legs.Add(Leg);
					State.Stats[static_cast<int32>(ELinkKind::Ladder)].Sampled++;
				}
			}
		}

		// 3. Mantle Links (deterministic sample from UMantleSubsystem)
		if (bRunMantles)
		{
			if (const UMantleSubsystem* MantleSub = World->GetSubsystem<UMantleSubsystem>())
			{
				TArray<FMantleLink> Mantles = MantleSub->GetLinks();
				State.Stats[static_cast<int32>(ELinkKind::Mantle)].TotalInMap = Mantles.Num();
				Mantles.Sort([](const FMantleLink& A, const FMantleLink& B)
				{
					const int64 Asx = FMath::RoundToInt64(A.Start.X);
					const int64 Bsx = FMath::RoundToInt64(B.Start.X);
					if (Asx != Bsx) return Asx < Bsx;
					const int64 Asy = FMath::RoundToInt64(A.Start.Y);
					const int64 Bsy = FMath::RoundToInt64(B.Start.Y);
					if (Asy != Bsy) return Asy < Bsy;
					const int64 Asz = FMath::RoundToInt64(A.Start.Z);
					const int64 Bsz = FMath::RoundToInt64(B.Start.Z);
					if (Asz != Bsz) return Asz < Bsz;
					const int64 Aex = FMath::RoundToInt64(A.End.X);
					const int64 Bex = FMath::RoundToInt64(B.End.X);
					if (Aex != Bex) return Aex < Bex;
					const int64 Aey = FMath::RoundToInt64(A.End.Y);
					const int64 Bey = FMath::RoundToInt64(B.End.Y);
					if (Aey != Bey) return Aey < Bey;
					return FMath::RoundToInt64(A.End.Z) < FMath::RoundToInt64(B.End.Z);
				});

				const int32 Total = Mantles.Num();
				const int32 TargetCount = State.MantleSampleCount > 0 ? State.MantleSampleCount : State.Config.DefaultMantleSample;
				const int32 SampleCount = FMath::Min(TargetCount, Total);
				const double Stride = SampleCount > 0 ? static_cast<double>(Total) / static_cast<double>(SampleCount) : 1.0;

				for (int32 i = 0; i < SampleCount; ++i)
				{
					const int32 Index = FMath::Clamp(static_cast<int32>(i * Stride), 0, Total - 1);
					const FMantleLink& Link = Mantles[Index];
					const FString Id = FString::Printf(TEXT("mantle_%d"), Index);
					if (!State.OnlyFilter.IsEmpty() && State.OnlyFilter != Id)
					{
						continue;
					}

					FLeg Leg;
					Leg.Kind = ELinkKind::Mantle;
					Leg.Id = Id;
					Leg.StartSpot = Link.Start - Link.Dir * State.Config.MantleApproachOffsetCm;
					Leg.StartFacing = Link.Dir.Rotation();
					Leg.Goal = Link.End + Link.Dir * State.Config.MantleGoalOffsetCm;
					Leg.GoalZ = Link.End.Z;
					Leg.LinkStart = Link.Start;
					Leg.LinkMid = (Link.Start + Link.End) * 0.5f;
					Leg.LinkEnd = Link.End;
					Leg.bMantleDown = Link.bDown;
					Leg.TimeoutSeconds = State.Config.LegTimeoutSeconds;
					State.Legs.Add(Leg);
					State.Stats[static_cast<int32>(ELinkKind::Mantle)].Sampled++;
					if (Link.bDown)
					{
						State.Stats[static_cast<int32>(ELinkKind::Mantle)].MantlesDownSampled++;
					}
					else
					{
						State.Stats[static_cast<int32>(ELinkKind::Mantle)].MantlesUpSampled++;
					}
				}
			}
		}

		// 4. Drop Links (deterministic sample from UDropLinkSubsystem)
		if (bRunDrops)
		{
			if (const UDropLinkSubsystem* DropSub = World->GetSubsystem<UDropLinkSubsystem>())
			{
				TArray<FDropLink> Drops = DropSub->GetLinks();
				State.Stats[static_cast<int32>(ELinkKind::Drop)].TotalInMap = Drops.Num();
				Drops.Sort([](const FDropLink& A, const FDropLink& B)
				{
					const int64 Asx = FMath::RoundToInt64(A.Start.X);
					const int64 Bsx = FMath::RoundToInt64(B.Start.X);
					if (Asx != Bsx) return Asx < Bsx;
					const int64 Asy = FMath::RoundToInt64(A.Start.Y);
					const int64 Bsy = FMath::RoundToInt64(B.Start.Y);
					if (Asy != Bsy) return Asy < Bsy;
					const int64 Asz = FMath::RoundToInt64(A.Start.Z);
					const int64 Bsz = FMath::RoundToInt64(B.Start.Z);
					if (Asz != Bsz) return Asz < Bsz;
					const int64 Aex = FMath::RoundToInt64(A.End.X);
					const int64 Bex = FMath::RoundToInt64(B.End.X);
					if (Aex != Bex) return Aex < Bex;
					const int64 Aey = FMath::RoundToInt64(A.End.Y);
					const int64 Bey = FMath::RoundToInt64(B.End.Y);
					if (Aey != Bey) return Aey < Bey;
					return FMath::RoundToInt64(A.End.Z) < FMath::RoundToInt64(B.End.Z);
				});

				const int32 Total = Drops.Num();
				const int32 TargetCount = State.DropSampleCount > 0 ? State.DropSampleCount : State.Config.DefaultDropSample;
				const int32 SampleCount = FMath::Min(TargetCount, Total);
				const double Stride = SampleCount > 0 ? static_cast<double>(Total) / static_cast<double>(SampleCount) : 1.0;

				for (int32 i = 0; i < SampleCount; ++i)
				{
					const int32 Index = FMath::Clamp(static_cast<int32>(i * Stride), 0, Total - 1);
					const FDropLink& Link = Drops[Index];
					const FString Id = FString::Printf(TEXT("drop_%d"), Index);
					if (!State.OnlyFilter.IsEmpty() && State.OnlyFilter != Id)
					{
						continue;
					}

					FLeg Leg;
					Leg.Kind = ELinkKind::Drop;
					Leg.Id = Id;
					Leg.StartSpot = Link.Start - Link.Dir * State.Config.DropApproachOffsetCm;
					Leg.StartFacing = Link.Dir.Rotation();
					Leg.Goal = Link.End + Link.Dir * State.Config.DropGoalOffsetCm;
					Leg.GoalZ = Link.End.Z;
					Leg.LinkStart = Link.Start;
					Leg.LinkMid = (Link.Start + Link.End) * 0.5f;
					Leg.LinkEnd = Link.End;
					Leg.TimeoutSeconds = State.Config.LegTimeoutSeconds;
					State.Legs.Add(Leg);
					State.Stats[static_cast<int32>(ELinkKind::Drop)].Sampled++;
				}
			}
		}

		// Build ControlLegs for Team 0 comparison (balanced subsample across kinds)
		if (State.bRunControl)
		{
			TMap<ELinkKind, int32> SampleCounts;
			int32 ControlMantlesUp = 0;
			int32 ControlMantlesDown = 0;
			for (const FLeg& Leg : State.Legs)
			{
				if (Leg.Kind == ELinkKind::Mantle)
				{
					if (Leg.bMantleDown && ControlMantlesDown < State.ControlSamplePerKind)
					{
						State.ControlLegs.Add(Leg);
						State.ControlStats[static_cast<int32>(ELinkKind::Mantle)].Sampled++;
						State.ControlStats[static_cast<int32>(ELinkKind::Mantle)].MantlesDownSampled++;
						ControlMantlesDown++;
					}
					else if (!Leg.bMantleDown && ControlMantlesUp < State.ControlSamplePerKind)
					{
						State.ControlLegs.Add(Leg);
						State.ControlStats[static_cast<int32>(ELinkKind::Mantle)].Sampled++;
						State.ControlStats[static_cast<int32>(ELinkKind::Mantle)].MantlesUpSampled++;
						ControlMantlesUp++;
					}
				}
				else
				{
					int32& Count = SampleCounts.FindOrAdd(Leg.Kind);
					if (Count < State.ControlSamplePerKind)
					{
						State.ControlLegs.Add(Leg);
						State.ControlStats[static_cast<int32>(Leg.Kind)].Sampled++;
						Count++;
					}
				}
			}
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: Built %d Control Legs for Team 0 comparison (%d stairs, %d ladders, %d mantles (%d up, %d down), %d drops)"),
				State.ControlLegs.Num(),
				SampleCounts.FindRef(ELinkKind::Stair),
				SampleCounts.FindRef(ELinkKind::Ladder),
				ControlMantlesUp + ControlMantlesDown, ControlMantlesUp, ControlMantlesDown,
				SampleCounts.FindRef(ELinkKind::Drop));
		}

		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: Traversal plan built: %d total legs (%d stairs of %d, %d ladders of %d, %d mantles of %d [%d up, %d down], %d drops of %d)"),
			State.Legs.Num(),
			State.Stats[static_cast<int32>(ELinkKind::Stair)].Sampled, State.Stats[static_cast<int32>(ELinkKind::Stair)].TotalInMap,
			State.Stats[static_cast<int32>(ELinkKind::Ladder)].Sampled, State.Stats[static_cast<int32>(ELinkKind::Ladder)].TotalInMap,
			State.Stats[static_cast<int32>(ELinkKind::Mantle)].Sampled, State.Stats[static_cast<int32>(ELinkKind::Mantle)].TotalInMap,
			State.Stats[static_cast<int32>(ELinkKind::Mantle)].MantlesUpSampled, State.Stats[static_cast<int32>(ELinkKind::Mantle)].MantlesDownSampled,
			State.Stats[static_cast<int32>(ELinkKind::Drop)].Sampled, State.Stats[static_cast<int32>(ELinkKind::Drop)].TotalInMap);
	}

	bool Finish(FState& State)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("=== PMC TRAVERSAL SMOKE SUMMARY ==="));
		bool bAllPassed = true;

		for (int32 k = 0; k < static_cast<int32>(ELinkKind::Count); ++k)
		{
			const ELinkKind Kind = static_cast<ELinkKind>(k);
			const FKindStats& KS = State.Stats[k];
			const double Rate = KS.Sampled > 0 ? (100.0 * static_cast<double>(KS.Crossed) / static_cast<double>(KS.Sampled)) : 0.0;
			const bool bFilterExcluded = (!State.OnlyFilter.IsEmpty() || (!State.KindsFilter.IsEmpty() && !State.KindsFilter.Contains(FString(KindName(Kind)).ToLower())));
			const bool bKindPass = bFilterExcluded ? (KS.Sampled == 0 || Rate >= 95.0) : (KS.Sampled > 0 && Rate >= 95.0);
			bAllPassed &= bKindPass;

			if (Kind == ELinkKind::Mantle)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke summary: %s: %d of %d crossed (%.1f%%) [up/over: %d of %d, down: %d of %d] [total in map: %d] - %s"),
					KindName(Kind), KS.Crossed, KS.Sampled, Rate,
					KS.MantlesUpCrossed, KS.MantlesUpSampled,
					KS.MantlesDownCrossed, KS.MantlesDownSampled,
					KS.TotalInMap, bKindPass ? TEXT("PASS") : TEXT("FAIL"));
			}
			else
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke summary: %s: %d of %d crossed (%.1f%%) [total in map: %d] - %s"),
					KindName(Kind), KS.Crossed, KS.Sampled, Rate, KS.TotalInMap, bKindPass ? TEXT("PASS") : TEXT("FAIL"));
			}

			if (!KS.Failures.IsEmpty())
			{
				FString FailSummary;
				for (int32 i = 0; i < KS.Failures.Num(); ++i)
				{
					if (i > 0)
					{
						FailSummary += TEXT(", ");
					}
					FailSummary += FString::Printf(TEXT("[%s: %s]"), *KS.Failures[i].Id, *KS.Failures[i].Reason);
				}
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s failed %d: %s"), KindName(Kind), KS.Failures.Num(), *FailSummary);
			}
		}

		// Programmatic failure grouping
		UE_LOG(LogCodexTactics, Display, TEXT("=== FAILURE GROUPS (Programmatic) ==="));
		TMap<FString, int32> GroupCounts;
		for (int32 k = 0; k < static_cast<int32>(ELinkKind::Count); ++k)
		{
			for (const FFailure& Fail : State.Stats[k].Failures)
			{
				const FString Key = FString::Printf(TEXT("%s: %s"), KindName(static_cast<ELinkKind>(k)), *Fail.Reason);
				GroupCounts.FindOrAdd(Key)++;
			}
		}
		GroupCounts.ValueSort([](int32 A, int32 B) { return A > B; });
		int32 TotalFailures = 0;
		for (const auto& Pair : GroupCounts)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("  %s -> %d"), *Pair.Key, Pair.Value);
			TotalFailures += Pair.Value;
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Total failures: %d"), TotalFailures);

		// Side-by-Side Control Comparison
		if (State.bRunControl && !State.ControlLegs.IsEmpty())
		{
			UE_LOG(LogCodexTactics, Display, TEXT("=== TRAVERSAL COMPARISON (Team 1 Hostile vs Team 0 Player Control) ==="));
			UE_LOG(LogCodexTactics, Display, TEXT("%-10s | %-16s | %-12s | %-16s | %-12s"),
				TEXT("Kind"), TEXT("Team 1 Crossed"), TEXT("Team 1 Rate"), TEXT("Team 0 Crossed"), TEXT("Team 0 Rate"));
			for (int32 k = 0; k < static_cast<int32>(ELinkKind::Count); ++k)
			{
				const ELinkKind Kind = static_cast<ELinkKind>(k);
				const FKindStats& T1 = State.Stats[k];
				const FKindStats& T0 = State.ControlStats[k];
				const double R1 = T1.Sampled > 0 ? (100.0 * static_cast<double>(T1.Crossed) / static_cast<double>(T1.Sampled)) : 0.0;
				const double R0 = T0.Sampled > 0 ? (100.0 * static_cast<double>(T0.Crossed) / static_cast<double>(T0.Sampled)) : 0.0;
				FString T1Str = FString::Printf(TEXT("%d / %d"), T1.Crossed, T1.Sampled);
				FString T0Str = FString::Printf(TEXT("%d / %d"), T0.Crossed, T0.Sampled);
				FString R1Str = FString::Printf(TEXT("%.1f%%"), R1);
				FString R0Str = FString::Printf(TEXT("%.1f%%"), R0);
				UE_LOG(LogCodexTactics, Display, TEXT("%-10s | %-16s | %-12s | %-16s | %-12s"),
					KindName(Kind), *T1Str, *R1Str, *T0Str, *R0Str);
			}
		}

		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), bAllPassed ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("PmcTraversalSmoke"));
		return false;
	}

	bool Step(TSharedRef<FState> StateRef, float DeltaTime)
	{
		FState& State = *StateRef;
		UWorld* World = State.World.Get();
		if (!World)
		{
			UE_LOG(LogCodexTactics, Error, TEXT("Smoke FAIL: World is null during Step"));
			FPlatformMisc::RequestExit(true, TEXT("PmcTraversalSmokeWorldNull"));
			return false;
		}

		State.Time += DeltaTime;
		State.PhaseTime += DeltaTime;

		switch (State.Phase)
		{
		case EPhase::WaitSubsystems:
		{
			const UDropLinkSubsystem* DropSub = World->GetSubsystem<UDropLinkSubsystem>();
			const UMantleSubsystem* MantleSub = World->GetSubsystem<UMantleSubsystem>();
			const bool bDropReady = DropSub && DropSub->HasGenerated();
			const bool bMantleReady = MantleSub && MantleSub->HasGenerated();

			if (!bDropReady || !bMantleReady)
			{
				if (State.PhaseTime < 180.f)
				{
					return true;
				}
				UE_LOG(LogCodexTactics, Error, TEXT("Smoke FAIL: subsystems failed to generate within 180s (drop ready: %d, mantle ready: %d)"),
					bDropReady ? 1 : 0, bMantleReady ? 1 : 0);
				return Finish(State);
			}

			// Disable background combat pressure and random PMC encounters during traversal test
			if (IConsoleVariable* Pressure = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Pressure.Enabled")))
			{
				Pressure->Set(0, ECVF_SetByConsole);
			}
			if (IConsoleVariable* Encounters = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Pmc.Encounters")))
			{
				Encounters->Set(0, ECVF_SetByConsole);
			}

			State.Phase = EPhase::Setup;
			State.PhaseTime = 0.f;
			return true;
		}
		case EPhase::Setup:
		{
			UPmcSquadSubsystem* Pmc = World->GetSubsystem<UPmcSquadSubsystem>();
			USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
			if (!Pmc)
			{
				UE_LOG(LogCodexTactics, Error, TEXT("Smoke FAIL: no UPmcSquadSubsystem"));
				return Finish(State);
			}

			// Keep squad followers holding and cease-fire so they do not attack the hostile operative
			if (Squad)
			{
				Squad->SetFollowersHolding(true);
				for (AOperativeCharacter* Member : Squad->GetMembers())
				{
					if (Member)
					{
						Member->ColdLevel = 0.f;
						if (Member->ColdSurvival)
						{
							Member->ColdSurvival->SetComponentTickEnabled(false);
						}
						Member->bTacticalCeaseFire = true;
					}
				}
			}

			// Spawn 1 hostile operative on Team 1
			const FVector SpawnOrigin(2356.f, -8700.f, 50.f);
			FVector NavSpawn = SpawnOrigin;
			ProjectNav(World, SpawnOrigin, NavSpawn);
			const TArray<AOperativeCharacter*> Spawned = Pmc->SpawnSquad(1, NavSpawn, FRotator::ZeroRotator, 1, 1);
			if (Spawned.IsEmpty() || !Spawned[0])
			{
				UE_LOG(LogCodexTactics, Error, TEXT("Smoke FAIL: failed to spawn hostile operative on Team 1"));
				return Finish(State);
			}

			State.Hostile = Spawned[0];
			Pmc->SetTeamCommanded(1, false);

			// Disable cold survival on hostile operative to isolate traversal mechanics from frostbite/panic
			State.Hostile->ColdLevel = 0.f;
			if (State.Hostile->ColdSurvival)
			{
				State.Hostile->ColdSurvival->SetComponentTickEnabled(false);
			}
			if (State.Hostile->KnockdownComponent)
			{
				State.Hostile->KnockdownComponent->bCanBeKnockedDown = false;
			}
			State.Hostile->bTacticalCeaseFire = true;

			BuildLegs(State, World);
			State.LegIndex = 0;
			State.Phase = EPhase::PrepareLeg;
			State.PhaseTime = 0.f;
			return true;
		}
		case EPhase::PrepareLeg:
		{
			TArray<FLeg>& CurrentLegs = State.bIsControlRun ? State.ControlLegs : State.Legs;
			if (State.LegIndex >= CurrentLegs.Num())
			{
				if (!State.bIsControlRun && State.bRunControl && !State.ControlLegs.IsEmpty())
				{
					USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
					if (Squad && !Squad->GetMembers().IsEmpty() && Squad->GetMembers()[0])
					{
						State.bIsControlRun = true;
						State.LegIndex = 0;
						State.Hostile = Squad->GetMembers()[0];
						AOperativeCharacter* PlayerOp = State.Hostile.Get();
						if (PlayerOp)
						{
							if (PlayerOp->ColdSurvival) { PlayerOp->ColdSurvival->SetComponentTickEnabled(false); }
							if (PlayerOp->HealthComponent) { PlayerOp->HealthComponent->SetMaxHealth(100000.f, true); }
							if (PlayerOp->KnockdownComponent)
							{
								PlayerOp->KnockdownComponent->bCanBeKnockedDown = false;
								PlayerOp->KnockdownComponent->ResetForLoad();
							}
							PlayerOp->bTacticalCeaseFire = true;
						}
						UE_LOG(LogCodexTactics, Display, TEXT("Smoke: === STARTING TEAM 0 CONTROL RUN (%d legs) ==="), State.ControlLegs.Num());
						State.Phase = EPhase::PrepareLeg;
						State.PhaseTime = 0.f;
						return true;
					}
				}
				State.Phase = EPhase::Done;
				return Finish(State);
			}

			AOperativeCharacter* Hostile = State.Hostile.Get();
			if (!Hostile || Hostile->IsKilledInAction() || !Hostile->GetController() || (Hostile->HealthComponent && !Hostile->HealthComponent->IsAlive()))
			{
				if (State.bIsControlRun)
				{
					USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
					if (Squad && !Squad->GetMembers().IsEmpty() && Squad->GetMembers()[0])
					{
						State.Hostile = Squad->GetMembers()[0];
						Hostile = State.Hostile.Get();
					}
				}
				else
				{
					UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
					if (Pmc)
					{
						const FVector SpawnOrigin(2356.f, -8700.f, 50.f);
						FVector NavSpawn = SpawnOrigin;
						ProjectNav(World, SpawnOrigin, NavSpawn);
						const TArray<AOperativeCharacter*> Spawned = Pmc->SpawnSquad(1, NavSpawn, FRotator::ZeroRotator, 1, 1);
						if (!Spawned.IsEmpty() && Spawned[0])
						{
							State.Hostile = Spawned[0];
							Hostile = Spawned[0];
							if (Hostile->ColdSurvival)
							{
								Hostile->ColdSurvival->Deactivate();
								Hostile->ColdSurvival->SetComponentTickEnabled(false);
							}
						}
					}
				}
			}

			if (!Hostile)
			{
				UE_LOG(LogCodexTactics, Error, TEXT("Smoke FAIL: operative is gone"));
				return Finish(State);
			}

			// Thoroughly reset character state before each leg to prevent failure cascade
			Hostile->ColdLevel = 0.f;
			if (Hostile->LadderClimb && Hostile->LadderClimb->IsClimbing())
			{
				Hostile->LadderClimb->AbortClimb(ELadderAbortReason::None);
			}
			if (Hostile->HealthComponent)
			{
				Hostile->HealthComponent->SetMaxHealth(100000.f, true);
			}
			if (Hostile->KnockdownComponent)
			{
				Hostile->KnockdownComponent->bCanBeKnockedDown = false;
				Hostile->KnockdownComponent->ResetForLoad();
			}
			Hostile->SetActorEnableCollision(true);
			Hostile->SetStance(EOperativeStance::Standing);

			const FLeg& Leg = CurrentLegs[State.LegIndex];
			FVector NavStart = Leg.StartSpot;
			if (!ProjectNav(World, Leg.StartSpot, NavStart))
			{
				RecordFailure(State, Leg.Kind, Leg.Id, TEXT("no path (start off navmesh)"), Leg.LinkStart, Leg.LinkEnd);
				State.LegIndex++;
				return true;
			}

			FVector NavGoal = Leg.Goal;
			if (!ProjectNav(World, Leg.Goal, NavGoal))
			{
				RecordFailure(State, Leg.Kind, Leg.Id, TEXT("no path (goal off navmesh)"), Leg.LinkStart, Leg.LinkEnd);
				State.LegIndex++;
				return true;
			}
			CurrentLegs[State.LegIndex].NavGoal = NavGoal;

			const float HalfHeight = Hostile->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			Hostile->TeleportTo(NavStart + FVector(0.f, 0.f, HalfHeight + 5.f), Leg.StartFacing, false, true);
			if (UCharacterMovementComponent* MoveComp = Hostile->GetCharacterMovement())
			{
				MoveComp->StopMovementImmediately();
				MoveComp->Velocity = FVector::ZeroVector;
			}
			if (AAIController* AI = Cast<AAIController>(Hostile->GetController()))
			{
				AI->StopMovement();
			}

			State.InitialMantleCount = Hostile->GetMantleCount();
			State.bSawMantling = false;

			USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
			if (Leg.Kind == ELinkKind::Mantle)
			{
				UE_LOG(LogCodexTactics, Log, TEXT("Smoke: Leg %s[%s] (%s) - SquadSolo=%d, IsMantlePathAllowed=%d, InitialMantleCount=%d"),
					State.bIsControlRun ? TEXT("Team0") : TEXT("Team1"),
					*Leg.Id, Leg.bMantleDown ? TEXT("down") : TEXT("up/over"),
					Squad ? (Squad->IsSoloMode() ? 1 : 0) : -1,
					Hostile->IsMantlePathAllowed() ? 1 : 0,
					State.InitialMantleCount);
			}

			State.Phase = EPhase::OrderLeg;
			State.PhaseTime = 0.f;
			return true;
		}
		case EPhase::OrderLeg:
		{
			AOperativeCharacter* Hostile = State.Hostile.Get();
			if (!Hostile)
			{
				UE_LOG(LogCodexTactics, Error, TEXT("Smoke FAIL: operative is gone"));
				return Finish(State);
			}

			TArray<FLeg>& CurrentLegs = State.bIsControlRun ? State.ControlLegs : State.Legs;
			const FLeg& Leg = CurrentLegs[State.LegIndex];
			const EOperativeOrderResult OrderRes = Hostile->OrderMoveTo(Leg.NavGoal, false);
			if (OrderRes == EOperativeOrderResult::Unreachable || OrderRes == EOperativeOrderResult::NoController)
			{
				RecordFailure(State, Leg.Kind, Leg.Id, TEXT("no path"), Leg.LinkStart, Leg.LinkEnd);
				State.LegIndex++;
				State.Phase = EPhase::PrepareLeg;
				return true;
			}
			if (OrderRes == EOperativeOrderResult::Refused)
			{
				RecordFailure(State, Leg.Kind, Leg.Id, TEXT("refused"), Leg.LinkStart, Leg.LinkEnd);
				State.LegIndex++;
				State.Phase = EPhase::PrepareLeg;
				return true;
			}

			State.Progress.Reset(World->GetTimeSeconds(), Hostile->GetActorLocation());
			State.LegStartTime = World->GetTimeSeconds();
			State.bLadderClimbed = false;
			State.bLinkStartPassed = false;
			State.bLinkMidPassed = false;
			State.bSawMantling = false;
			State.InitialMantleCount = Hostile->GetMantleCount();
			State.Phase = EPhase::RunLeg;
			State.PhaseTime = 0.f;
			return true;
		}
		case EPhase::RunLeg:
		{
			AOperativeCharacter* Hostile = State.Hostile.Get();
			if (!Hostile)
			{
				UE_LOG(LogCodexTactics, Error, TEXT("Smoke FAIL: operative is gone"));
				return Finish(State);
			}

			TArray<FLeg>& CurrentLegs = State.bIsControlRun ? State.ControlLegs : State.Legs;
			const FLeg& Leg = CurrentLegs[State.LegIndex];
			const bool bAlive = !Hostile->IsKilledInAction() && Hostile->HealthComponent && Hostile->HealthComponent->IsAlive();
			if (!bAlive)
			{
				RecordFailure(State, Leg.Kind, Leg.Id, TEXT("died"), Leg.LinkStart, Leg.LinkEnd);
				State.LegIndex++;
				State.Phase = EPhase::PrepareLeg;
				State.PhaseTime = 0.f;
				return true;
			}

			const double Now = World->GetTimeSeconds();
			const FVector CurrentPos = Hostile->GetActorLocation();
			const float CurrentFeetZ = FeetZ(*Hostile);

			// Track link traversal to verify the physical link was actually traversed
			if (FVector::Dist2D(CurrentPos, Leg.LinkStart) <= 150.f)
			{
				State.bLinkStartPassed = true;
			}
			if (Leg.Kind == ELinkKind::Mantle)
			{
				if (FVector::Dist(CurrentPos, Leg.LinkMid) <= 150.f)
				{
					State.bLinkMidPassed = true;
				}
				if (Hostile->IsMantling())
				{
					State.bSawMantling = true;
				}
			}
			else if (Leg.Kind == ELinkKind::Stair)
			{
				if (FVector::Dist2D(CurrentPos, Leg.LinkMid) <= 180.f)
				{
					State.bLinkMidPassed = true;
				}
			}
			else if (Leg.Kind == ELinkKind::Drop)
			{
				if (State.bLinkStartPassed && CurrentFeetZ <= Leg.LinkEnd.Z + State.Config.LevelToleranceCm)
				{
					State.bLinkMidPassed = true;
				}
			}

			if (Hostile->LadderClimb && Hostile->LadderClimb->IsClimbing())
			{
				State.bLadderClimbed = true;
			}

			const float Dist2D = static_cast<float>(FVector::Dist2D(CurrentPos, Leg.NavGoal));
			const float DistZ = FMath::Abs(CurrentFeetZ - Leg.NavGoal.Z);
			const float Radius = (Leg.Kind == ELinkKind::Ladder) ? State.Config.LadderArriveRadiusCm : State.Config.ArriveRadiusCm;
			bool bArrived = (Dist2D <= Radius) && (DistZ <= State.Config.LevelToleranceCm);

			if (Leg.Kind == ELinkKind::Drop)
			{
				UCharacterMovementComponent* MoveComp = Hostile->GetCharacterMovement();
				const bool bGrounded = MoveComp && MoveComp->IsMovingOnGround();
				const bool bNearZeroZVel = MoveComp && FMath::Abs(MoveComp->Velocity.Z) < 30.f;
				bArrived = bArrived && bGrounded && bNearZeroZVel;
			}

			if (bArrived)
			{
				bool bVerifiedLink = false;
				if (Leg.Kind == ELinkKind::Ladder)
				{
					if (!State.bLadderClimbed)
					{
						RecordFailure(State, Leg.Kind, Leg.Id, TEXT("bypassed ladder"), Leg.LinkStart, Leg.LinkEnd);
						State.LegIndex++;
						State.Phase = EPhase::PrepareLeg;
						State.PhaseTime = 0.f;
						return true;
					}
					bVerifiedLink = true;
				}
				else if (Leg.Kind == ELinkKind::Mantle)
				{
					const bool bDidMantle = State.bSawMantling || (Hostile->GetMantleCount() > State.InitialMantleCount);
					if (!bDidMantle)
					{
						const FString BypassReason = Leg.bMantleDown ? TEXT("bypassed mantle down") : TEXT("bypassed mantle up");
						RecordFailure(State, Leg.Kind, Leg.Id, BypassReason, Leg.LinkStart, Leg.LinkEnd);
						State.LegIndex++;
						State.Phase = EPhase::PrepareLeg;
						State.PhaseTime = 0.f;
						return true;
					}
					bVerifiedLink = true;
				}
				else if (Leg.Kind == ELinkKind::Drop)
				{
					if (!State.bLinkMidPassed)
					{
						RecordFailure(State, Leg.Kind, Leg.Id, TEXT("bypassed drop"), Leg.LinkStart, Leg.LinkEnd);
						State.LegIndex++;
						State.Phase = EPhase::PrepareLeg;
						State.PhaseTime = 0.f;
						return true;
					}
					bVerifiedLink = true;
				}
				else // Stair
				{
					if (!State.bLinkMidPassed)
					{
						RecordFailure(State, Leg.Kind, Leg.Id, TEXT("bypassed stair"), Leg.LinkStart, Leg.LinkEnd);
						State.LegIndex++;
						State.Phase = EPhase::PrepareLeg;
						State.PhaseTime = 0.f;
						return true;
					}
					bVerifiedLink = true;
				}

				if (bVerifiedLink)
				{
					FKindStats& KStats = State.bIsControlRun ? State.ControlStats[static_cast<int32>(Leg.Kind)] : State.Stats[static_cast<int32>(Leg.Kind)];
					KStats.Crossed++;
					if (Leg.Kind == ELinkKind::Mantle)
					{
						if (Leg.bMantleDown)
						{
							KStats.MantlesDownCrossed++;
						}
						else
						{
							KStats.MantlesUpCrossed++;
						}
					}
					State.LegIndex++;
					State.Phase = EPhase::PrepareLeg;
					State.PhaseTime = 0.f;
					return true;
				}
			}

			if (State.Progress.IsStuck(Now, CurrentPos, State.Config.StuckMoveCm, State.Config.StuckSeconds))
			{
				RecordFailure(State, Leg.Kind, Leg.Id, TEXT("stuck"), Leg.LinkStart, Leg.LinkEnd);
				State.LegIndex++;
				State.Phase = EPhase::PrepareLeg;
				State.PhaseTime = 0.f;
				return true;
			}

			if (Now - State.LegStartTime > Leg.TimeoutSeconds)
			{
				RecordFailure(State, Leg.Kind, Leg.Id, TEXT("stuck (timed out)"), Leg.LinkStart, Leg.LinkEnd);
				State.LegIndex++;
				State.Phase = EPhase::PrepareLeg;
				State.PhaseTime = 0.f;
				return true;
			}

			return true;
		}
		case EPhase::Done:
		default:
			return false;
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}

		TSharedRef<FState> State = MakeShared<FState>();
		State->World = World;
		State->MapName = World->GetMapName();
		State->MapName.RemoveFromStart(World->StreamingLevelsPrefix);
		State->Config = FPmcTraversalConfig::Load();

		for (const FString& Arg : Args)
		{
			if (Arg.StartsWith(TEXT("only=")))
			{
				State->OnlyFilter = Arg.Mid(5);
			}
			else if (Arg.StartsWith(TEXT("kinds=")))
			{
				TArray<FString> KindStrs;
				Arg.Mid(6).ParseIntoArray(KindStrs, TEXT(","), true);
				for (const FString& K : KindStrs)
				{
					State->KindsFilter.Add(K.ToLower());
				}
			}
			else if (Arg.StartsWith(TEXT("mantlesample=")))
			{
				State->MantleSampleCount = FCString::Atoi(*Arg.Mid(13));
			}
			else if (Arg.StartsWith(TEXT("dropsample=")))
			{
				State->DropSampleCount = FCString::Atoi(*Arg.Mid(11));
			}
			else if (Arg.StartsWith(TEXT("controlsample=")))
			{
				State->ControlSamplePerKind = FCString::Atoi(*Arg.Mid(14));
			}
			else if (Arg == TEXT("nocontrol"))
			{
				State->bRunControl = false;
			}
		}

		int32 MantleCli = 0;
		if (FParse::Value(FCommandLine::Get(), TEXT("-TraversalMantleSample="), MantleCli) && MantleCli > 0)
		{
			State->MantleSampleCount = MantleCli;
		}
		int32 DropCli = 0;
		if (FParse::Value(FCommandLine::Get(), TEXT("-TraversalDropSample="), DropCli) && DropCli > 0)
		{
			State->DropSampleCount = DropCli;
		}
		int32 ControlSampleCli = 0;
		if (FParse::Value(FCommandLine::Get(), TEXT("-TraversalControlSample="), ControlSampleCli) && ControlSampleCli > 0)
		{
			State->ControlSamplePerKind = ControlSampleCli;
		}
		if (FParse::Param(FCommandLine::Get(), TEXT("TraversalNoControl")))
		{
			State->bRunControl = false;
		}
		FString KindsCli;
		if (FParse::Value(FCommandLine::Get(), TEXT("-TraversalKinds="), KindsCli))
		{
			TArray<FString> KindStrs;
			KindsCli.ParseIntoArray(KindStrs, TEXT(","), true);
			for (const FString& K : KindStrs)
			{
				State->KindsFilter.Add(K.ToLower());
			}
		}
		FString OnlyCli;
		if (FParse::Value(FCommandLine::Get(), TEXT("-TraversalOnly="), OnlyCli))
		{
			State->OnlyFilter = OnlyCli;
		}

		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float DeltaTime)
		{
			return Step(State, DeltaTime);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.PmcTraversalSmoke"),
		TEXT("Dev check (smart enemies T-12): Hostile operative (team 1, commander off) traverses stairs, ladders, mantles, and drops. Logs PASS/FAIL then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
