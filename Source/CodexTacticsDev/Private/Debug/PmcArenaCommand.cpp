// The PMC self-play arena (user request 2026-11-03, docs/systems/pmc-squad-ai.md "Arena"): N rounds of squad vs squad in one process,
// a machine-readable result JSON (schema codex.pmc_arena/1) in Saved/Arena/<timestamp>.json and one summary line. The hook the
// overnight trainer uses. Headless:
//   "C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "<project>/CodexTactics.uproject" /Game/Maps/L_FormationLabCombat
//     -game -nullrhi -nosound -unattended -LevelJson=L_PmcArena.json -UseFixedTimeStep -FPS=60 -FixedSeed
//     -ExecCmds="CodexTactics.PmcArena rounds=3 seed=7 weightsA=pmc_commander.json weightsB=pmc_commander.json opponent=pmc time=120"
// Screenshots: -RenderOffscreen -ResX=1280 -ResY=720 instead of -nullrhi and shot=1 -> Saved/Screenshots/PmcArena/.
// Per round a track Saved/Arena/<run>/round_<n>.track.json (codex.pmc_arena_track/1: every 0.2 s each fighter's side / team / id / position /
// yaw / stance / HP / state, and the events: shots from -> to hit / miss, kills, the commander's decisions with the chosen order + top score) and a
// highlight_score in the result (the formula: HighlightScoreOf). Replay / capture of a round: the same seed + weights with record=1 on a RENDERED
// run (-RenderOffscreen, -UseFixedTimeStep -FPS=60 -FixedSeed) and the clip recorder in front:
//   -ExecCmds="CodexTactics.ClipRecorder pmcarena nodirector,CodexTactics.PmcArena seed=7 rounds=1 record=1 ..." -> Saved/Screenshots/Clips/pmcarena/f_NNNNN.png
//   (Scripts/Tools/clip_assemble.py turns the frames into an mp4 as Scripts/levelgen_record_run.ps1 does). The arena's overview camera follows
//   the fighters' centre. Determinism: the same weights + seed + fixed step reproduce the spawns, looks, weapons and orders; the crowd avoidance,
//   async navmesh work and physics hits can still drift a long round (known limit: check the track's winner / duration against the original).
// Args (keys case-insensitive, the night trainer's template "Map=.. Rounds=.. Seed=.. WeightsA=.. Opponent=Bot|Pmc WeightsB=..|None TimeLimit=.. Out=.."
// fits, docs/tools/night-trainer.md 6): rounds=N seed=S weightsA=<file> weightsB=<file>|None opponent=pmc|player|bot time|timelimit=<s> out=<project-relative
// result path> map=<informational> squad=<men per side> distance=<cm> shot=0|1 smoke=0|1 record=0|1. Round n's seed = seed + n - 1 (Rounds=1 Seed=<it> replays it).
//   opponent=pmc: team 1 (A) vs team 2 (B), the player's squad parked (any number of rounds); player: A vs the player's squad commanded with
//   weights B (one round: the fallen cannot be revived); bot: A vs the player's squad driven by the playtest bot (one round, development builds).
// CodexTactics.PmcArenaSmoke = one pmc round + the JSON validated (Smoke RESULT: PASS / FAIL).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PmcCommanderRules.h"
#include "AI/PmcSquadSubsystem.h"
#include "AI/SquadOrderCatalog.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Bot/PlaytestBotRules.h"
#include "Bot/PlaytestBotSubsystem.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFlow/LevelEncounterSubsystem.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealClient.h"

namespace PmcArena
{
	constexpr float StepSeconds = 0.1f;
	constexpr int32 TeamA = 1;
	constexpr int32 TeamB = 2;

	enum class EOpponent : uint8 { Pmc, Player, Bot };

	struct FOptions
	{
		int32 Rounds = 1;
		int32 Seed = 1;
		FString WeightsA;
		FString WeightsB;
		EOpponent Opponent = EOpponent::Pmc;
		float TimeLimit = 120.f;
		int32 Squad = 3;
		float DistanceCm = 3000.f;
		bool bShots = false;
		bool bSmoke = false;
		/** record=1: the replay / capture mode - the overview camera follows the fight every tick (rendered run + CodexTactics.ClipRecorder). */
		bool bRecord = false;
		/** Out=<project-relative path> of the result JSON (the night trainer, docs/tools/night-trainer.md 6); empty = Saved/Arena/<run>.json. */
		FString OutPath;
		/** Map=<path> as the trainer passes it (informational: the launcher picks the map). */
		FString MapArg;
	};

	struct FSide
	{
		int32 TeamId = 0;
		FString Label;
		FVector Spawn = FVector::ZeroVector;
		FRotator Facing = FRotator::ZeroRotator;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Members;
		float StartHp = 0.f;
	};

	struct FRoundResult
	{
		int32 Index = 0;
		/** The round's own seed (base seed + round - 1): Rounds=1 Seed=<this> replays it. */
		int32 Seed = 0;
		FString EndReason;
		int32 RetreatsSurvived[2] = { 0, 0 };
		float HpMax[2] = { 0.f, 0.f };
		FString Attacker;
		FString Winner;
		float Duration = 0.f;
		int32 Survivors[2] = { 0, 0 };
		float HpLost[2] = { 0.f, 0.f };
		int32 Kills[2] = { 0, 0 };
		FPmcTeamStats Stats[2];
		bool bFightStarted = false;
		/** How many times the lead (alive A - alive B) changed sign in the round (the track's samples). */
		int32 LeadSwings = 0;
		float HighlightScore = 0.f;
		FString TrackFile;
	};

	/**
	 * The round's lightweight track (schema codex.pmc_arena_track/1, the trainer's viewer contract): fighters[] {id, side, team}, frames[] = per
	 * TrackInterval one [x, y] (null when down) per fighter, states[] = the same grid of [yaw, stance, hp, state] (the arena's extra), events[].
	 */
	struct FTrack
	{
		TArray<TWeakObjectPtr<AOperativeCharacter>> Fighters;
		TArray<TSharedPtr<FJsonValue>> Frames;
		TArray<TSharedPtr<FJsonValue>> States;
		TArray<TSharedPtr<FJsonValue>> Events;
		/** Fighters that fell back at least once (retreats_survived = those alive at the end). */
		TSet<TWeakObjectPtr<AOperativeCharacter>> Retreated;
		int32 HealsSeen[2] = { 0, 0 };
		float SampleTimer = 0.f;
		int32 LastLeadSign = 0;
		int32 LeadSwings = 0;
		TArray<FDelegateHandle> FireHandles;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Listened;
		FDelegateHandle OrderHandle;
	};
	constexpr float TrackInterval = 0.2f;

	struct FState
	{
		FOptions Options;
		FPmcWeights WeightsA;
		FPmcWeights WeightsB;
		bool bWeightsALoaded = false;
		bool bWeightsBLoaded = false;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Round = 0;
		int32 Failures = 0;
		FSide Sides[2];
		TArray<FRoundResult> Results;
		TArray<TWeakObjectPtr<AOperativeCharacter>> ParkedSquad;
		TWeakObjectPtr<ACameraActor> Camera;
		int32 ShotsTaken = 0;
		FString OutputPath;
		FDateTime Started;
		FDateTime StartedUtc;
		FString RunName;
		bool bRoundsLimited = false;
		FTrack Track;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	FOptions Parse(const TArray<FString>& Args)
	{
		FOptions Options;
		for (const FString& Arg : Args)
		{
			FString Key;
			FString Value;
			if (!Arg.Split(TEXT("="), &Key, &Value))
			{
				continue;
			}
			Key = Key.ToLower();
			const bool bNone = Value.Equals(TEXT("none"), ESearchCase::IgnoreCase);
			if (Key == TEXT("rounds")) { Options.Rounds = FMath::Max(1, FCString::Atoi(*Value)); }
			else if (Key == TEXT("seed")) { Options.Seed = FCString::Atoi(*Value); }
			else if (Key == TEXT("weightsa")) { Options.WeightsA = bNone ? FString() : Value; }
			else if (Key == TEXT("weightsb")) { Options.WeightsB = bNone ? FString() : Value; }
			else if (Key == TEXT("time") || Key == TEXT("timelimit")) { Options.TimeLimit = FMath::Max(10.f, FCString::Atof(*Value)); }
			else if (Key == TEXT("out")) { Options.OutPath = bNone ? FString() : Value; }
			else if (Key == TEXT("map")) { Options.MapArg = Value; }
			else if (Key == TEXT("squad")) { Options.Squad = FMath::Clamp(FCString::Atoi(*Value), 1, 6); }
			else if (Key == TEXT("distance")) { Options.DistanceCm = FMath::Clamp(FCString::Atof(*Value), 1000.f, 8000.f); }
			else if (Key == TEXT("shot")) { Options.bShots = FCString::Atoi(*Value) != 0; }
			else if (Key == TEXT("smoke")) { Options.bSmoke = FCString::Atoi(*Value) != 0; }
			else if (Key == TEXT("record")) { Options.bRecord = FCString::Atoi(*Value) != 0; }
			else if (Key == TEXT("opponent"))
			{
				const FString Lower = Value.ToLower();
				Options.Opponent = Lower == TEXT("player") ? EOpponent::Player : (Lower == TEXT("bot") ? EOpponent::Bot : EOpponent::Pmc);
			}
		}
		return Options;
	}

	const TCHAR* OpponentName(EOpponent Opponent)
	{
		switch (Opponent)
		{
		case EOpponent::Player: return TEXT("Player");
		case EOpponent::Bot: return TEXT("Bot");
		default: return TEXT("Pmc");
		}
	}

	/** A project-relative path (forward slashes, the trainer's convention) made absolute; an absolute one kept. */
	FString ResolveProjectPath(const FString& Path)
	{
		if (Path.IsEmpty() || FPaths::IsRelative(Path) == false)
		{
			return Path;
		}
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / Path);
	}

	/** Where the result JSON goes (Out=, else Saved/Arena/<run>.json) and the folder of the round tracks next to it. */
	FString ResultPath(const FState& State)
	{
		if (!State.Options.OutPath.IsEmpty())
		{
			return ResolveProjectPath(State.Options.OutPath);
		}
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Arena") / FString::Printf(TEXT("%s.json"), *State.RunName));
	}

	FString TrackDir(const FState& State)
	{
		const FString Result = ResultPath(State);
		return FPaths::GetPath(Result) / FPaths::GetBaseFilename(Result);
	}

	float SumHp(const TArray<TWeakObjectPtr<AOperativeCharacter>>& Members)
	{
		float Total = 0.f;
		for (const TWeakObjectPtr<AOperativeCharacter>& Member : Members)
		{
			const AOperativeCharacter* Operative = Member.Get();
			Total += Operative && Operative->HealthComponent ? FMath::Max(0.f, Operative->HealthComponent->GetCurrentHealth()) : 0.f;
		}
		return Total;
	}

	int32 CountAlive(const TArray<TWeakObjectPtr<AOperativeCharacter>>& Members)
	{
		int32 Alive = 0;
		for (const TWeakObjectPtr<AOperativeCharacter>& Member : Members)
		{
			const AOperativeCharacter* Operative = Member.Get();
			Alive += Operative && !Operative->IsKilledInAction() && Operative->HealthComponent && Operative->HealthComponent->IsAlive() ? 1 : 0;
		}
		return Alive;
	}

	/** The player's start spot and facing (the squad spawned there). */
	bool FindStart(UWorld* World, FVector& OutLocation, FRotator& OutFacing)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		if (Squad && Squad->GetLeader())
		{
			OutLocation = Squad->GetLeader()->GetActorLocation();
			OutFacing = FRotator(0.f, Squad->GetLeader()->GetActorRotation().Yaw, 0.f);
			return true;
		}
		for (TActorIterator<APlayerStart> It(World); It; ++It)
		{
			OutLocation = It->GetActorLocation();
			OutFacing = FRotator(0.f, It->GetActorRotation().Yaw, 0.f);
			return true;
		}
		return false;
	}

	/** A spot DistanceCm from Origin with a navmesh path from it: the start facing first, then the bearings round it. */
	bool FindFarSpot(UWorld* World, const FVector& Origin, float Yaw, float DistanceCm, FVector& OutSpot)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (!Nav)
		{
			return false;
		}
		for (const float Bearing : { 0.f, 45.f, -45.f, 90.f, -90.f, 135.f, -135.f, 180.f })
		{
			for (const float Scale : { 1.f, 0.8f, 0.6f })
			{
				const FVector Candidate = Origin + FRotator(0.f, Yaw + Bearing, 0.f).Vector() * DistanceCm * Scale;
				FNavLocation OnNav;
				if (!Nav->ProjectPointToNavigation(Candidate, OnNav, FVector(600.f, 600.f, 400.f)))
				{
					continue;
				}
				const UNavigationPath* Path = Nav->FindPathToLocationSynchronously(World, Origin, OnNav.Location);
				if (Path && Path->IsValid() && !Path->IsPartial() && FVector::Dist2D(Origin, OnNav.Location) >= DistanceCm * 0.5f)
				{
					OutSpot = OnNav.Location;
					return true;
				}
			}
		}
		return false;
	}

	void ParkPlayerSquad(FState& State, UWorld* World)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		if (!Squad)
		{
			return;
		}
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->StopOperative();
			Member->SetActorHiddenInGame(true);
			Member->SetActorEnableCollision(false);
			Member->CustomTimeDilation = 0.f;
			Member->SetActorLocation(Member->GetActorLocation() + FVector(100000.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
			State.ParkedSquad.Add(Member);
		}
	}

	void AimCamera(FState& State, UWorld* World)
	{
		if (!State.Options.bShots && !State.Options.bRecord)
		{
			return;
		}
		const FVector A = State.Sides[0].Spawn;
		const FVector B = State.Sides[1].Spawn;
		FVector Centre = (A + B) * 0.5f;
		int32 Counted = 0;
		FVector Mean = FVector::ZeroVector;
		for (const FSide& Side : State.Sides)
		{
			for (const TWeakObjectPtr<AOperativeCharacter>& Member : Side.Members)
			{
				if (const AOperativeCharacter* Operative = Member.Get(); Operative && !Operative->IsKilledInAction())
				{
					Mean += Operative->GetActorLocation();
					++Counted;
				}
			}
		}
		if (Counted > 0)
		{
			Centre = Mean / Counted;
		}
		// Once the squads met, frame the fighters (their spread), not the spawn spots.
		float Spread = 0.f;
		if (Counted > 0)
		{
			for (const FSide& Side : State.Sides)
			{
				for (const TWeakObjectPtr<AOperativeCharacter>& Member : Side.Members)
				{
					if (const AOperativeCharacter* Operative = Member.Get(); Operative && !Operative->IsKilledInAction())
					{
						Spread = FMath::Max(Spread, static_cast<float>(FVector::Dist2D(Operative->GetActorLocation(), Centre)));
					}
				}
			}
		}
		const float Span = FMath::Clamp(Spread * 2.2f, 1400.f, FMath::Max(1400.f, static_cast<float>(FVector::Dist2D(A, B))));
		const FVector Eye = Centre + FVector(-0.35f * Span, -0.55f * Span, 0.7f * Span);
		ACameraActor* Camera = State.Camera.Get();
		if (!Camera)
		{
			Camera = World->SpawnActor<ACameraActor>(Eye, FRotator::ZeroRotator);
			State.Camera = Camera;
			if (Camera)
			{
				Camera->GetCameraComponent()->SetFieldOfView(50.f);
				Camera->GetCameraComponent()->bConstrainAspectRatio = false;
				Camera->GetCameraComponent()->PostProcessSettings.bOverride_MotionBlurAmount = true;
				Camera->GetCameraComponent()->PostProcessSettings.MotionBlurAmount = 0.f;
			}
			// The fight, not the HUD: the canvas HUD and the widgets off (the capture's bShowUI only covers Slate).
			UWidgetLayoutLibrary::RemoveAllWidgets(World);
			if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0); PC && PC->GetHUD())
			{
				PC->GetHUD()->bShowHUD = false;
			}
		}
		if (!Camera)
		{
			return;
		}
		Camera->SetActorLocationAndRotation(Eye, (Centre - Eye).Rotation());
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0); PC && PC->GetViewTarget() != Camera)
		{
			PC->SetViewTarget(Camera);
		}
	}

	void Shot(FState& State, UWorld* World, const TCHAR* Name)
	{
		if (!State.Options.bShots)
		{
			return;
		}
		AimCamera(State, World);
		const FString Dir = FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("PmcArena");
		IFileManager::Get().MakeDirectory(*Dir, true);
		const FString File = Dir / FString::Printf(TEXT("round%d_%s.png"), State.Round, Name);
		FScreenshotRequest::RequestScreenshot(File, /*bShowUI*/ false, /*bAddFilenameSuffix*/ false);
		++State.ShotsTaken;
		UE_LOG(LogCodexTactics, Display, TEXT("[PmcArena] shot %s"), *FPaths::ConvertRelativePathToFull(File));
	}

	// --- The track (codex.pmc_arena_track/1) ---------------------------------------------------------------------------------------

	const TCHAR* SideOf(const FState& State, const AOperativeCharacter* Operative)
	{
		for (const FSide& Side : State.Sides)
		{
			if (Side.Members.Contains(Operative))
			{
				return *Side.Label;
			}
		}
		return TEXT("-");
	}

	TSharedRef<FJsonObject> EventJson(const FState& State, const TCHAR* Type)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetNumberField(TEXT("t"), FMath::RoundToFloat(State.StageTime * 100.f) / 100.f);
		Json->SetStringField(TEXT("type"), Type);
		return Json;
	}

	void StartTrack(FState& State, UWorld* World)
	{
		FTrack& Track = State.Track;
		Track.Fighters.Reset();
		Track.Frames.Reset();
		Track.States.Reset();
		Track.Events.Reset();
		Track.Retreated.Reset();
		Track.HealsSeen[0] = Track.HealsSeen[1] = 0;
		Track.SampleTimer = 0.f;
		Track.LastLeadSign = 0;
		Track.LeadSwings = 0;
		FState* StatePtr = &State;
		for (FSide& Side : State.Sides)
		{
			for (const TWeakObjectPtr<AOperativeCharacter>& Member : Side.Members)
			{
				AOperativeCharacter* Operative = Member.Get();
				if (!Operative)
				{
					continue;
				}
				Track.Fighters.Add(Operative);
				if (Track.Listened.Contains(Operative))
				{
					continue;
				}
				Track.Listened.Add(Operative);
				// Shots: from -> to, hit / miss. Kills: the health component's death.
				Track.FireHandles.Add(Operative->OnWeaponFiredNative.AddLambda([StatePtr](AOperativeCharacter* Shooter, AActor* Target, bool bHit)
				{
					TSharedRef<FJsonObject> Json = EventJson(*StatePtr, TEXT("shot"));
					Json->SetStringField(TEXT("from"), Shooter ? Shooter->DisplayName.ToString() : TEXT("?"));
					Json->SetStringField(TEXT("side"), SideOf(*StatePtr, Shooter));
					Json->SetStringField(TEXT("to"), Target ? Target->GetName() : TEXT("?"));
					Json->SetBoolField(TEXT("hit"), bHit);
					StatePtr->Track.Events.Add(MakeShared<FJsonValueObject>(Json));
				}));
				if (Operative->HealthComponent)
				{
					Operative->HealthComponent->OnDiedNative.AddLambda([StatePtr](AActor* Victim, const FString& AttackerSource)
					{
						TSharedRef<FJsonObject> Json = EventJson(*StatePtr, TEXT("kill"));
						const AOperativeCharacter* Fallen = Cast<AOperativeCharacter>(Victim);
						Json->SetStringField(TEXT("target"), Fallen ? Fallen->DisplayName.ToString() : (Victim ? Victim->GetName() : TEXT("?")));
						Json->SetStringField(TEXT("side"), SideOf(*StatePtr, Fallen));
						Json->SetStringField(TEXT("by"), AttackerSource);
						StatePtr->Track.Events.Add(MakeShared<FJsonValueObject>(Json));
					});
				}
			}
		}
		if (UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World); Pmc && !Track.OrderHandle.IsValid())
		{
			// The commander's decisions: the chosen option with its (top) score; heals / flanks / suppressions / ambushes / retreats among them.
			Track.OrderHandle = Pmc->OnFighterOrder.AddLambda([StatePtr](int32 TeamId, AOperativeCharacter* Fighter, EPmcOrder Order, float Score)
			{
				TSharedRef<FJsonObject> Json = EventJson(*StatePtr, TEXT("tactic"));
				Json->SetStringField(TEXT("who"), Fighter ? Fighter->DisplayName.ToString() : TEXT("?"));
				Json->SetStringField(TEXT("side"), SideOf(*StatePtr, Fighter));
				Json->SetNumberField(TEXT("team"), TeamId);
				Json->SetStringField(TEXT("decision"), PmcCommanderRules::OrderName(Order));
				Json->SetNumberField(TEXT("score"), FMath::RoundToFloat(Score * 100.f) / 100.f);
				StatePtr->Track.Events.Add(MakeShared<FJsonValueObject>(Json));
				if (Order == EPmcOrder::FallBack && Fighter)
				{
					StatePtr->Track.Retreated.Add(Fighter);
				}
			});
		}
	}

	const TCHAR* StanceName(EOperativeStance Stance)
	{
		switch (Stance)
		{
		case EOperativeStance::Crouching: return TEXT("crouch");
		case EOperativeStance::Prone: return TEXT("prone");
		default: return TEXT("stand");
		}
	}

	void SampleTrack(FState& State, UWorld* World, float DeltaTime)
	{
		FTrack& Track = State.Track;
		Track.SampleTimer -= DeltaTime;
		if (Track.SampleTimer > 0.f)
		{
			return;
		}
		Track.SampleTimer = TrackInterval;
		const UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
		TArray<TSharedPtr<FJsonValue>> Frame;
		TArray<TSharedPtr<FJsonValue>> Detail;
		for (const TWeakObjectPtr<AOperativeCharacter>& Fighter : Track.Fighters)
		{
			const AOperativeCharacter* Operative = Fighter.Get();
			const bool bAlive = Operative && !Operative->IsKilledInAction() && Operative->HealthComponent && Operative->HealthComponent->IsAlive();
			if (!bAlive)
			{
				Frame.Add(MakeShared<FJsonValueNull>());
				Detail.Add(MakeShared<FJsonValueNull>());
				continue;
			}
			const FVector Location = Operative->GetActorLocation();
			TArray<TSharedPtr<FJsonValue>> Point = { MakeShared<FJsonValueNumber>(FMath::RoundToFloat(Location.X)), MakeShared<FJsonValueNumber>(FMath::RoundToFloat(Location.Y)) };
			Frame.Add(MakeShared<FJsonValueArray>(Point));
			FString StateName = Operative->IsPlayerTeam() && !(Pmc && Pmc->IsTeamCommanded(PmcTeam::Player)) ? TEXT("player")
				: PmcCommanderRules::OrderName(Pmc ? Pmc->GetFighterOrder(Operative) : EPmcOrder::Hold);
			if (Operative->bInCover)
			{
				StateName += TEXT("/cover");
			}
			TArray<TSharedPtr<FJsonValue>> Extra = { MakeShared<FJsonValueNumber>(FMath::RoundToFloat(Operative->GetActorRotation().Yaw)),
				MakeShared<FJsonValueString>(StanceName(Operative->GetStance())), MakeShared<FJsonValueNumber>(FMath::RoundToFloat(Operative->HealthComponent->GetCurrentHealth())),
				MakeShared<FJsonValueString>(StateName) };
			Detail.Add(MakeShared<FJsonValueArray>(Extra));
		}
		Track.Frames.Add(MakeShared<FJsonValueArray>(Frame));
		Track.States.Add(MakeShared<FJsonValueArray>(Detail));
		// Heals: the team statistics' counter moving between two samples.
		for (int32 Index = 0; Index < 2; ++Index)
		{
			const FPmcTeamStats* Stats = Pmc ? Pmc->GetTeamStats(State.Sides[Index].TeamId) : nullptr;
			if (Stats && Stats->Heals > Track.HealsSeen[Index])
			{
				Track.HealsSeen[Index] = Stats->Heals;
				TSharedRef<FJsonObject> Json = EventJson(State, TEXT("heal"));
				Json->SetStringField(TEXT("side"), State.Sides[Index].Label);
				Track.Events.Add(MakeShared<FJsonValueObject>(Json));
			}
		}
		// Lead swings: the sign of (alive A - alive B) changing.
		const int32 Lead = CountAlive(State.Sides[0].Members) - CountAlive(State.Sides[1].Members);
		const int32 Sign = Lead > 0 ? 1 : (Lead < 0 ? -1 : 0);
		if (Sign != 0 && Track.LastLeadSign != 0 && Sign != Track.LastLeadSign)
		{
			++Track.LeadSwings;
		}
		if (Sign != 0)
		{
			Track.LastLeadSign = Sign;
		}
	}

	FString WriteTrack(FState& State, UWorld* World, const FRoundResult& Result)
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("schema"), TEXT("codex.pmc_arena_track/1"));
		Root->SetStringField(TEXT("run"), State.RunName);
		Root->SetNumberField(TEXT("round"), Result.Index);
		Root->SetStringField(TEXT("map"), World->GetMapName());
		Root->SetNumberField(TEXT("seed"), State.Options.Seed);
		Root->SetNumberField(TEXT("round_seed"), Result.Seed);
		Root->SetNumberField(TEXT("dt"), TrackInterval);
		Root->SetStringField(TEXT("units"), TEXT("cm"));
		Root->SetStringField(TEXT("winner"), Result.Winner);
		Root->SetNumberField(TEXT("duration_s"), FMath::RoundToFloat(Result.Duration * 10.f) / 10.f);
		Root->SetNumberField(TEXT("highlight_score"), Result.HighlightScore);
		TArray<TSharedPtr<FJsonValue>> Fighters;
		for (const TWeakObjectPtr<AOperativeCharacter>& Fighter : State.Track.Fighters)
		{
			const AOperativeCharacter* Operative = Fighter.Get();
			TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetStringField(TEXT("id"), Operative ? Operative->DisplayName.ToString() : TEXT("?"));
			Json->SetStringField(TEXT("side"), SideOf(State, Operative));
			Json->SetNumberField(TEXT("team"), Operative ? Operative->TeamId : -1);
			Fighters.Add(MakeShared<FJsonValueObject>(Json));
		}
		Root->SetArrayField(TEXT("fighters"), Fighters);
		Root->SetArrayField(TEXT("frames"), State.Track.Frames);
		Root->SetStringField(TEXT("states_comment"), TEXT("per frame per fighter [yaw, stance, hp, state] (null when down) - the arena's extra beside the contract"));
		Root->SetArrayField(TEXT("states"), State.Track.States);
		Root->SetArrayField(TEXT("events"), State.Track.Events);
		FString Text;
		FJsonSerializer::Serialize(Root, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
		const FString Dir = TrackDir(State);
		IFileManager::Get().MakeDirectory(*Dir, true);
		const FString Path = FPaths::ConvertRelativePathToFull(Dir / FString::Printf(TEXT("round_%d.track.json"), Result.Index));
		FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		return Path;
	}

	/**
	 * How much a round is worth watching (the user wants the juiciest fights): kills x1 + a decided round +1 + a close finish (the winner's
	 * last man standing) +2 + lead swings x1.5 + successful flanks x1 + ambushes sprung x0.5 + heals x0.5, and the duration sanity: a 20-150 s
	 * round +1, shorter or longer -1 (nothing happened, or a stalemate).
	 */
	float HighlightScoreOf(const FRoundResult& Result)
	{
		float Score = static_cast<float>(Result.Kills[0] + Result.Kills[1]);
		const bool bDecided = Result.Winner == TEXT("A") || Result.Winner == TEXT("B");
		Score += bDecided ? 1.f : 0.f;
		const int32 WinnerSurvivors = Result.Winner == TEXT("A") ? Result.Survivors[0] : (Result.Winner == TEXT("B") ? Result.Survivors[1] : 0);
		Score += bDecided && WinnerSurvivors == 1 ? 2.f : 0.f;
		Score += Result.LeadSwings * 1.5f;
		Score += static_cast<float>(Result.Stats[0].FlanksSucceeded + Result.Stats[1].FlanksSucceeded);
		Score += 0.5f * static_cast<float>(Result.Stats[0].Ambushes + Result.Stats[1].Ambushes);
		Score += 0.5f * static_cast<float>(Result.Stats[0].Heals + Result.Stats[1].Heals);
		Score += Result.Duration >= 20.f && Result.Duration <= 150.f ? 1.f : -1.f;
		return FMath::RoundToFloat(Score * 10.f) / 10.f;
	}

	TSharedRef<FJsonObject> StatsJson(const FPmcTeamStats& Stats)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetNumberField(TEXT("flanks_attempted"), Stats.FlanksAttempted);
		Json->SetNumberField(TEXT("flanks_succeeded"), Stats.FlanksSucceeded);
		Json->SetNumberField(TEXT("suppressions"), Stats.Suppressions);
		Json->SetNumberField(TEXT("suppression_s"), FMath::RoundToFloat(Stats.SuppressionSeconds * 10.f) / 10.f);
		Json->SetNumberField(TEXT("ambushes"), Stats.Ambushes);
		Json->SetNumberField(TEXT("retreats"), Stats.Retreats);
		Json->SetNumberField(TEXT("heals"), Stats.Heals);
		Json->SetNumberField(TEXT("relocations"), Stats.Relocations);
		Json->SetNumberField(TEXT("advances"), Stats.Advances);
		Json->SetNumberField(TEXT("covers"), Stats.Covers);
		Json->SetNumberField(TEXT("friendly_fire_hits"), Stats.FriendlyFireHits);
		Json->SetNumberField(TEXT("stuck_events"), Stats.StuckEvents);
		Json->SetNumberField(TEXT("orders"), Stats.OrdersIssued);
		return Json;
	}

	FString WriteJson(FState& State, UWorld* World)
	{
		const FString Path = ResultPath(State);
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("schema"), TEXT("codex.pmc_arena/1"));
		Root->SetStringField(TEXT("map"), State.Options.MapArg.IsEmpty() ? World->GetMapName() : State.Options.MapArg);
		Root->SetStringField(TEXT("opponent"), OpponentName(State.Options.Opponent));
		Root->SetNumberField(TEXT("seed"), State.Options.Seed);
		Root->SetNumberField(TEXT("rounds_requested"), State.Options.Rounds);
		Root->SetBoolField(TEXT("rounds_limited"), State.bRoundsLimited);
		Root->SetNumberField(TEXT("time_limit_s"), State.Options.TimeLimit);
		Root->SetNumberField(TEXT("squad_size"), State.Options.Squad);
		Root->SetStringField(TEXT("run"), State.RunName);
		Root->SetBoolField(TEXT("record"), State.Options.bRecord);
		Root->SetStringField(TEXT("weights_a"), State.Options.WeightsA.IsEmpty() ? TEXT("Content/Data/AI/pmc_commander.json") : State.Options.WeightsA);
		Root->SetStringField(TEXT("weights_b"), State.Options.Opponent == EOpponent::Bot ? TEXT("None") : (State.Options.WeightsB.IsEmpty() ? TEXT("Content/Data/AI/pmc_commander.json") : State.Options.WeightsB));
		Root->SetStringField(TEXT("highlight_formula"), TEXT("kills + decided(1) + last_man_standing(2) + lead_swings*1.5 + flanks_succeeded + 0.5*ambushes + 0.5*heals + duration_20_150s(+1 else -1)"));
		Root->SetStringField(TEXT("started"), State.Started.ToIso8601());
		Root->SetStringField(TEXT("finished"), FDateTime::Now().ToIso8601());
		Root->SetStringField(TEXT("started_utc"), State.StartedUtc.ToIso8601());
		Root->SetStringField(TEXT("finished_utc"), FDateTime::UtcNow().ToIso8601());
		TSharedRef<FJsonObject> Weights = MakeShared<FJsonObject>();
		TSharedRef<FJsonObject> WeightsA = MakeShared<FJsonObject>();
		WeightsA->SetStringField(TEXT("file"), State.Options.WeightsA.IsEmpty() ? TEXT("pmc_commander.json") : State.Options.WeightsA);
		WeightsA->SetStringField(TEXT("name"), State.WeightsA.Name);
		WeightsA->SetBoolField(TEXT("loaded"), State.bWeightsALoaded);
		TSharedRef<FJsonObject> WeightsB = MakeShared<FJsonObject>();
		WeightsB->SetStringField(TEXT("file"), State.Options.WeightsB.IsEmpty() ? TEXT("pmc_commander.json") : State.Options.WeightsB);
		WeightsB->SetStringField(TEXT("name"), State.WeightsB.Name);
		WeightsB->SetBoolField(TEXT("loaded"), State.bWeightsBLoaded);
		Weights->SetObjectField(TEXT("A"), WeightsA);
		Weights->SetObjectField(TEXT("B"), WeightsB);
		Root->SetObjectField(TEXT("weights"), Weights);

		TArray<TSharedPtr<FJsonValue>> Rounds;
		int32 WinsA = 0;
		int32 WinsB = 0;
		int32 Draws = 0;
		float Duration = 0.f;
		for (const FRoundResult& Result : State.Results)
		{
			TSharedRef<FJsonObject> Round = MakeShared<FJsonObject>();
			Round->SetNumberField(TEXT("index"), Result.Index);
			Round->SetNumberField(TEXT("seed"), Result.Seed);
			Round->SetStringField(TEXT("attacker"), Result.Attacker);
			Round->SetStringField(TEXT("winner"), Result.Winner);
			Round->SetNumberField(TEXT("duration_s"), FMath::RoundToFloat(Result.Duration * 10.f) / 10.f);
			Round->SetStringField(TEXT("end_reason"), Result.EndReason);
			Round->SetBoolField(TEXT("fight_started"), Result.bFightStarted);
			Round->SetNumberField(TEXT("lead_swings"), Result.LeadSwings);
			Round->SetNumberField(TEXT("highlight_score"), Result.HighlightScore);
			// The track relative to this result file (the trainer's convention).
			FString TrackRelative = Result.TrackFile;
			FPaths::MakePathRelativeTo(TrackRelative, *Path);
			Round->SetStringField(TEXT("track"), TrackRelative);
			TSharedRef<FJsonObject> Sides = MakeShared<FJsonObject>();
			for (int32 Index = 0; Index < 2; ++Index)
			{
				TSharedRef<FJsonObject> Side = MakeShared<FJsonObject>();
				Side->SetStringField(TEXT("team"), Index == 0 || State.Options.Opponent == EOpponent::Pmc ? TEXT("pmc") : (State.Options.Opponent == EOpponent::Bot ? TEXT("bot") : TEXT("player")));
				Side->SetNumberField(TEXT("survivors"), Result.Survivors[Index]);
				Side->SetNumberField(TEXT("squad_size"), State.Options.Squad);
				Side->SetNumberField(TEXT("hp_lost"), FMath::RoundToFloat(Result.HpLost[Index]));
				Side->SetNumberField(TEXT("hp_max"), FMath::RoundToFloat(Result.HpMax[Index]));
				Side->SetNumberField(TEXT("kills"), Result.Kills[Index]);
				Side->SetNumberField(TEXT("flanks_attempted"), Result.Stats[Index].FlanksAttempted);
				Side->SetNumberField(TEXT("flanks_succeeded"), Result.Stats[Index].FlanksSucceeded);
				Side->SetNumberField(TEXT("suppression_s"), FMath::RoundToFloat(Result.Stats[Index].SuppressionSeconds * 10.f) / 10.f);
				Side->SetNumberField(TEXT("ambushes"), Result.Stats[Index].Ambushes);
				Side->SetNumberField(TEXT("retreats"), Result.Stats[Index].Retreats);
				Side->SetNumberField(TEXT("retreats_survived"), Result.RetreatsSurvived[Index]);
				Side->SetNumberField(TEXT("heals"), Result.Stats[Index].Heals);
				Side->SetNumberField(TEXT("friendly_fire"), Result.Stats[Index].FriendlyFireHits);
				Side->SetNumberField(TEXT("stuck_events"), Result.Stats[Index].StuckEvents);
				Sides->SetObjectField(Index == 0 ? TEXT("A") : TEXT("B"), Side);
			}
			Round->SetObjectField(TEXT("sides"), Sides);
			auto Pair = [&Result](auto Getter)
			{
				TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
				Json->SetNumberField(TEXT("A"), Getter(0));
				Json->SetNumberField(TEXT("B"), Getter(1));
				return Json;
			};
			Round->SetObjectField(TEXT("survivors"), Pair([&Result](int32 Index) { return Result.Survivors[Index]; }));
			Round->SetObjectField(TEXT("hp_lost"), Pair([&Result](int32 Index) { return FMath::RoundToFloat(Result.HpLost[Index]); }));
			Round->SetObjectField(TEXT("kills"), Pair([&Result](int32 Index) { return Result.Kills[Index]; }));
			TSharedRef<FJsonObject> Stats = MakeShared<FJsonObject>();
			Stats->SetObjectField(TEXT("A"), StatsJson(Result.Stats[0]));
			Stats->SetObjectField(TEXT("B"), StatsJson(Result.Stats[1]));
			Round->SetObjectField(TEXT("stats"), Stats);
			Rounds.Add(MakeShared<FJsonValueObject>(Round));
			WinsA += Result.Winner == TEXT("A") ? 1 : 0;
			WinsB += Result.Winner == TEXT("B") ? 1 : 0;
			Draws += Result.Winner == TEXT("draw") ? 1 : 0;
			Duration += Result.Duration;
		}
		Root->SetArrayField(TEXT("rounds"), Rounds);
		TSharedRef<FJsonObject> Summary = MakeShared<FJsonObject>();
		Summary->SetNumberField(TEXT("rounds"), State.Results.Num());
		Summary->SetNumberField(TEXT("A_wins"), WinsA);
		Summary->SetNumberField(TEXT("B_wins"), WinsB);
		Summary->SetNumberField(TEXT("draws"), Draws);
		Summary->SetNumberField(TEXT("avg_duration_s"), State.Results.IsEmpty() ? 0.f : FMath::RoundToFloat(Duration / State.Results.Num() * 10.f) / 10.f);
		Root->SetObjectField(TEXT("summary"), Summary);

		FString Text;
		FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Text));
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		UE_LOG(LogCodexTactics, Display, TEXT("[PmcArena] rounds=%d A_wins=%d B_wins=%d draws=%d avg_duration=%.1fs opponent=%s -> %s"), State.Results.Num(), WinsA, WinsB, Draws,
			State.Results.IsEmpty() ? 0.f : Duration / State.Results.Num(), OpponentName(State.Options.Opponent), *FPaths::ConvertRelativePathToFull(Path));
		return Path;
	}

	/** The smoke's validation of the file it wrote (the schema the trainer reads). */
	void ValidateJson(FState& State, const FString& Path)
	{
		FString Text;
		TSharedPtr<FJsonObject> Json;
		const bool bParsed = FFileHelper::LoadFileToString(Text, *Path) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) && Json.IsValid();
		Check(State, bParsed, TEXT("arena JSON written and parsed"));
		if (!bParsed)
		{
			return;
		}
		Check(State, Json->GetStringField(TEXT("schema")) == TEXT("codex.pmc_arena/1"), TEXT("schema codex.pmc_arena/1"));
		const TArray<TSharedPtr<FJsonValue>>* Rounds = nullptr;
		Check(State, Json->TryGetArrayField(TEXT("rounds"), Rounds) && Rounds->Num() == State.Options.Rounds, TEXT("one round recorded"));
		if (Rounds && Rounds->Num() > 0)
		{
			const TSharedPtr<FJsonObject>* Round = nullptr;
			if ((*Rounds)[0]->TryGetObject(Round))
			{
				const FString Winner = (*Round)->GetStringField(TEXT("winner"));
				Check(State, Winner == TEXT("A") || Winner == TEXT("B") || Winner == TEXT("draw"), FString::Printf(TEXT("winner is A / B / draw (%s)"), *Winner));
				Check(State, (*Round)->GetBoolField(TEXT("fight_started")), TEXT("the fight started (the squads met)"));
				Check(State, (*Round)->GetNumberField(TEXT("duration_s")) > 0.0, TEXT("duration recorded"));
				Check(State, (*Round)->HasField(TEXT("highlight_score")), TEXT("highlight_score present"));
				Check(State, (*Round)->HasField(TEXT("seed")) && (*Round)->HasField(TEXT("end_reason")), TEXT("round seed + end_reason (the trainer contract)"));
				const TSharedPtr<FJsonObject>* Sides = nullptr;
				Check(State, (*Round)->TryGetObjectField(TEXT("sides"), Sides) && (*Sides)->HasField(TEXT("A")) && (*Sides)->HasField(TEXT("B"))
					&& (*Sides)->GetObjectField(TEXT("A"))->HasField(TEXT("retreats_survived")) && (*Sides)->GetObjectField(TEXT("B"))->HasField(TEXT("hp_max")),
					TEXT("sides.A / sides.B with the contract's metrics"));
				FString Track;
				TSharedPtr<FJsonObject> TrackJson;
				FString TrackText;
				const FString TrackPath = (*Round)->TryGetStringField(TEXT("track"), Track) ? FPaths::ConvertRelativePathToFull(FPaths::GetPath(Path) / Track) : FString();
				const bool bTrack = !TrackPath.IsEmpty() && FFileHelper::LoadFileToString(TrackText, *TrackPath)
					&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TrackText), TrackJson) && TrackJson.IsValid()
					&& TrackJson->GetStringField(TEXT("schema")) == TEXT("codex.pmc_arena_track/1");
				Check(State, bTrack, FString::Printf(TEXT("the round's track file written (codex.pmc_arena_track/1, relative '%s')"), *Track));
				const TArray<TSharedPtr<FJsonValue>>* Frames = nullptr;
				const TArray<TSharedPtr<FJsonValue>>* Events = nullptr;
				const TArray<TSharedPtr<FJsonValue>>* Fighters = nullptr;
				const bool bSampled = bTrack && TrackJson->TryGetArrayField(TEXT("frames"), Frames) && Frames->Num() >= 10
					&& TrackJson->TryGetArrayField(TEXT("fighters"), Fighters) && Fighters->Num() == 2 * State.Options.Squad
					&& TrackJson->TryGetArrayField(TEXT("events"), Events) && Events->Num() >= 1 && TrackJson->HasField(TEXT("dt"));
				Check(State, bSampled, FString::Printf(TEXT("track frames (%d) x fighters (%d), events (%d), dt"), Frames ? Frames->Num() : 0, Fighters ? Fighters->Num() : 0, Events ? Events->Num() : 0));
				const TSharedPtr<FJsonObject>* Stats = nullptr;
				Check(State, (*Round)->TryGetObjectField(TEXT("stats"), Stats) && (*Stats)->HasField(TEXT("A")) && (*Stats)->HasField(TEXT("B")), TEXT("per-side stats present"));
				const TSharedPtr<FJsonObject>* HpLost = nullptr;
				Check(State, (*Round)->TryGetObjectField(TEXT("hp_lost"), HpLost) && ((*HpLost)->GetNumberField(TEXT("A")) + (*HpLost)->GetNumberField(TEXT("B")) > 0.0),
					TEXT("somebody lost health (they fought)"));
			}
		}
		const TSharedPtr<FJsonObject>* Summary = nullptr;
		Check(State, Json->TryGetObjectField(TEXT("summary"), Summary) && (*Summary)->HasField(TEXT("A_wins")), TEXT("summary present"));
	}

	bool Finish(FState& State, UWorld* World, bool bComplete)
	{
		if (World)
		{
			if (UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World))
			{
				Pmc->DestroyTeam(TeamA);
				Pmc->DestroyTeam(TeamB);
			}
			State.OutputPath = WriteJson(State, World);
			if (State.Options.bSmoke)
			{
				ValidateJson(State, State.OutputPath);
			}
		}
		if (State.Options.bSmoke)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		}
		FPlatformMisc::RequestExit(false, TEXT("PmcArena"));
		return false;
	}

	void StartRound(FState& State, UWorld* World)
	{
		UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		if (!Pmc || !Squad)
		{
			return;
		}
		++State.Round;
		// The round's own seed (base + round - 1, the trainer's convention): Rounds=1 Seed=<it> replays this round - its spawns, looks,
		// weapons AND its attacker (by the seed's parity, not the round index).
		const int32 Seed = State.Options.Seed + State.Round - 1;
		for (FSide& Side : State.Sides)
		{
			Side.Members.Reset();
		}
		Pmc->ResetTeam(TeamA);
		Pmc->ResetTeam(TeamB);
		// Side A: a PMC squad at the start.
		FSide& A = State.Sides[0];
		for (AOperativeCharacter* Fighter : Pmc->SpawnSquad(TeamA, A.Spawn, A.Facing, State.Options.Squad, Seed))
		{
			A.Members.Add(Fighter);
		}
		Pmc->SetTeamCommanded(TeamA, true, &State.WeightsA);
		// Side B.
		FSide& B = State.Sides[1];
		if (State.Options.Opponent == EOpponent::Pmc)
		{
			for (AOperativeCharacter* Fighter : Pmc->SpawnSquad(TeamB, B.Spawn, B.Facing, State.Options.Squad, Seed + 1000))
			{
				B.Members.Add(Fighter);
			}
			Pmc->SetTeamCommanded(TeamB, true, &State.WeightsB);
		}
		else
		{
			const TArray<AOperativeCharacter*> Members = Squad->GetMembers();
			const FVector Right = B.Facing.RotateVector(FVector::RightVector);
			for (int32 Index = 0; Index < Members.Num(); ++Index)
			{
				AOperativeCharacter* Member = Members[Index];
				Member->StopOperative();
				const FVector Spot = SmokeUtils::FreeSpot(World, B.Spawn + Right * ((Index - (Members.Num() - 1) * 0.5f) * 150.f), Member);
				Member->TeleportTo(Spot, B.Facing, false, true);
				B.Members.Add(Member);
			}
			Squad->SetSquadPosture(ESquadFirePosture::Aggressive);
			Squad->SetFollowersHolding(true);
			if (State.Options.Opponent == EOpponent::Player)
			{
				Pmc->SetTeamCommanded(PmcTeam::Player, true, &State.WeightsB);
			}
			else if (UPlaytestBotSubsystem* Bot = World->GetSubsystem<UPlaytestBotSubsystem>())
			{
				Bot->StartBot(EBotProfile::Veteran, false, false, Seed);
			}
			else
			{
				UE_LOG(LogCodexTactics, Warning, TEXT("[PmcArena] no playtest bot in this build: the player's squad fights on its own fire alone"));
			}
		}
		// The attacker alternates with the round seed's parity: the defender waits in ambush, the attacker advances on the defender's spawn.
		const bool bAAttacks = (Seed % 2) == 0;
		Pmc->SetTeamAssault(TeamA, bAAttacks ? &B.Spawn : nullptr);
		if (State.Options.Opponent == EOpponent::Pmc)
		{
			Pmc->SetTeamAssault(TeamB, bAAttacks ? nullptr : &A.Spawn);
		}
		else if (State.Options.Opponent == EOpponent::Player)
		{
			Pmc->SetTeamAssault(PmcTeam::Player, bAAttacks ? nullptr : &A.Spawn);
		}
		for (FSide& Side : State.Sides)
		{
			Side.StartHp = SumHp(Side.Members);
		}
		FRoundResult& Result = State.Results.AddDefaulted_GetRef();
		Result.Index = State.Round;
		Result.Seed = Seed;
		Result.Attacker = bAAttacks ? TEXT("A") : TEXT("B");
		for (int32 Index = 0; Index < 2; ++Index)
		{
			for (const TWeakObjectPtr<AOperativeCharacter>& Member : State.Sides[Index].Members)
			{
				Result.HpMax[Index] += Member.IsValid() && Member->HealthComponent ? Member->HealthComponent->GetMaxHealth() : 0.f;
			}
		}
		StartTrack(State, World);
		UE_LOG(LogCodexTactics, Display, TEXT("[PmcArena] round %d: A %d men at %s vs B %d men at %s, attacker %s"), State.Round, A.Members.Num(), *A.Spawn.ToCompactString(),
			B.Members.Num(), *B.Spawn.ToCompactString(), *Result.Attacker);
	}

	void EndRound(FState& State, UWorld* World, const TCHAR* Why)
	{
		UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
		FRoundResult& Result = State.Results.Last();
		Result.Duration = State.StageTime;
		for (int32 Index = 0; Index < 2; ++Index)
		{
			const FSide& Side = State.Sides[Index];
			Result.Survivors[Index] = CountAlive(Side.Members);
			Result.HpLost[Index] = FMath::Max(0.f, Side.StartHp - SumHp(Side.Members));
			if (const FPmcTeamStats* Stats = Pmc ? Pmc->GetTeamStats(Side.TeamId) : nullptr)
			{
				Result.Stats[Index] = *Stats;
				Result.Kills[Index] = Stats->Kills;
			}
		}
		Result.bFightStarted = Pmc && (Pmc->HasTeamContact(TeamA) || Pmc->HasTeamContact(State.Sides[1].TeamId));
		if (Result.Survivors[0] > 0 && Result.Survivors[1] == 0)
		{
			Result.Winner = TEXT("A");
		}
		else if (Result.Survivors[1] > 0 && Result.Survivors[0] == 0)
		{
			Result.Winner = TEXT("B");
		}
		else
		{
			Result.Winner = TEXT("draw");
		}
		SampleTrack(State, World, TrackInterval); // the last sample
		Result.EndReason = FCString::Strcmp(Why, TEXT("time limit")) == 0 ? TEXT("time_limit") : TEXT("elimination");
		for (const TWeakObjectPtr<AOperativeCharacter>& Fighter : State.Track.Retreated)
		{
			const AOperativeCharacter* Operative = Fighter.Get();
			if (Operative && !Operative->IsKilledInAction() && Operative->HealthComponent && Operative->HealthComponent->IsAlive())
			{
				++Result.RetreatsSurvived[State.Sides[0].Members.Contains(Operative) ? 0 : 1];
			}
		}
		Result.LeadSwings = State.Track.LeadSwings;
		Result.HighlightScore = HighlightScoreOf(Result);
		Result.TrackFile = WriteTrack(State, World, Result);
		UE_LOG(LogCodexTactics, Display, TEXT("[PmcArena] round %d over (%s): winner %s, survivors A %d / B %d, hp lost A %.0f / B %.0f, kills A %d / B %d, flanks A %d/%d B %d/%d, suppression A %.1fs B %.1fs, heals A %d B %d, retreats A %d B %d, stuck A %d B %d, highlight %.1f -> %s"),
			State.Round, Why, *Result.Winner, Result.Survivors[0], Result.Survivors[1], Result.HpLost[0], Result.HpLost[1], Result.Kills[0], Result.Kills[1],
			Result.Stats[0].FlanksAttempted, Result.Stats[0].FlanksSucceeded, Result.Stats[1].FlanksAttempted, Result.Stats[1].FlanksSucceeded,
			Result.Stats[0].SuppressionSeconds, Result.Stats[1].SuppressionSeconds, Result.Stats[0].Heals, Result.Stats[1].Heals, Result.Stats[0].Retreats, Result.Stats[1].Retreats,
			Result.Stats[0].StuckEvents, Result.Stats[1].StuckEvents, Result.HighlightScore, *Result.TrackFile);
		if (Pmc)
		{
			Pmc->SetTeamCommanded(TeamA, false);
			Pmc->SetTeamCommanded(TeamB, false);
			Pmc->SetTeamCommanded(PmcTeam::Player, false);
			Pmc->SetTeamAssault(PmcTeam::Player, nullptr);
			Pmc->DestroyTeam(TeamA);
			Pmc->DestroyTeam(TeamB);
		}
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
		switch (State.Stage)
		{
		case 0: // the level settled
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			if (!Pmc)
			{
				Check(State, false, TEXT("UPmcSquadSubsystem present"));
				return Finish(State, World, false);
			}
			FVector Start;
			FRotator Facing;
			if (!FindStart(World, Start, Facing))
			{
				Check(State, false, TEXT("a start spot (the squad / a PlayerStart)"));
				return Finish(State, World, false);
			}
			FVector Far;
			if (!FindFarSpot(World, Start, Facing.Yaw, State.Options.DistanceCm, Far))
			{
				Check(State, false, TEXT("a far spot with a navmesh path from the start"));
				return Finish(State, World, false);
			}
			State.Sides[0].TeamId = TeamA;
			State.Sides[0].Label = TEXT("A");
			State.Sides[0].Spawn = Start;
			State.Sides[0].Facing = (Far - Start).GetSafeNormal2D().Rotation();
			State.Sides[1].TeamId = State.Options.Opponent == EOpponent::Pmc ? TeamB : PmcTeam::Player;
			State.Sides[1].Label = TEXT("B");
			State.Sides[1].Spawn = Far;
			State.Sides[1].Facing = (Start - Far).GetSafeNormal2D().Rotation();
			if (State.Options.Opponent == EOpponent::Pmc)
			{
				ParkPlayerSquad(State, World);
			}
			else if (State.Options.Rounds > 1)
			{
				State.bRoundsLimited = true;
				State.Options.Rounds = 1;
				UE_LOG(LogCodexTactics, Warning, TEXT("[PmcArena] opponent=%s: one round only (the player's fallen cannot be revived)"), OpponentName(State.Options.Opponent));
			}
			if (ULevelEncounterSubsystem* Encounter = World->GetSubsystem<ULevelEncounterSubsystem>())
			{
				Encounter->SetCombatStartOverride(ECombatStartMode::Ambush);
			}
			// The lab's immortality / infinite ammo cheats are the player squad's level defaults: off for a real fight.
			for (const TCHAR* Name : { TEXT("Codex.Cheat.Immortal"), TEXT("Codex.Cheat.InfiniteAmmo") })
			{
				if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
				{
					Variable->Set(0, ECVF_SetByConsole);
				}
			}
			State.Started = FDateTime::Now();
			State.StartedUtc = FDateTime::UtcNow();
			State.RunName = FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")) + (State.Options.bRecord ? TEXT("-record") : TEXT(""));
			UE_LOG(LogCodexTactics, Display, TEXT("[PmcArena] %s: %d round(s), seed %d, opponent %s, weights A '%s' (%s) B '%s' (%s), time limit %.0f s, start %s, far spot %s (%.0f cm)"),
				*World->GetMapName(), State.Options.Rounds, State.Options.Seed, OpponentName(State.Options.Opponent), *State.WeightsA.Name,
				State.bWeightsALoaded ? TEXT("file") : TEXT("defaults"), *State.WeightsB.Name, State.bWeightsBLoaded ? TEXT("file") : TEXT("defaults"), State.Options.TimeLimit,
				*Start.ToCompactString(), *Far.ToCompactString(), FVector::Dist2D(Start, Far));
			State.Stage = 1;
			State.StageTime = 0.f;
			return true;
		}
		case 1: // a round starts
			StartRound(State, World);
			State.Stage = 2;
			State.StageTime = 0.f;
			return true;
		case 2: // the fight
		{
			SampleTrack(State, World, StepSeconds);
			if (State.Options.bRecord)
			{
				AimCamera(State, World);
			}
			if (State.Options.bShots && State.Round == 1)
			{
				AimCamera(State, World);
				if ((State.ShotsTaken == 0 && State.StageTime >= 6.f) || (State.ShotsTaken == 1 && State.StageTime >= 11.f) || (State.ShotsTaken == 2 && State.StageTime >= 18.f))
				{
					Shot(State, World, State.ShotsTaken == 0 ? TEXT("contact") : (State.ShotsTaken == 1 ? TEXT("midfight") : TEXT("late")));
				}
			}
			const int32 AliveA = CountAlive(State.Sides[0].Members);
			const int32 AliveB = CountAlive(State.Sides[1].Members);
			const TCHAR* Why = nullptr;
			if (AliveA == 0 || AliveB == 0)
			{
				Why = TEXT("a side is down");
			}
			else if (State.StageTime >= State.Options.TimeLimit)
			{
				Why = TEXT("time limit");
			}
			if (!Why)
			{
				return true;
			}
			EndRound(State, World, Why);
			State.Stage = State.Round >= State.Options.Rounds ? 4 : 3;
			State.StageTime = 0.f;
			return true;
		}
		case 3: // a breather between rounds
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			State.Stage = 1;
			State.StageTime = 0.f;
			return true;
		case 4:
		default:
			if (State.StageTime < 0.5f)
			{
				return true; // the last screenshot request lands
			}
			return Finish(State, World, true);
		}
	}

	void RunWith(const TArray<FString>& Args, UWorld* World, bool bSmoke)
	{
		if (!World)
		{
			return;
		}
		TSharedRef<FState> State = MakeShared<FState>();
		State->Options = Parse(Args);
		State->Options.bSmoke |= bSmoke;
		if (bSmoke)
		{
			State->Options.Rounds = 1;
			State->Options.Opponent = EOpponent::Pmc;
			State->Options.TimeLimit = FMath::Min(State->Options.TimeLimit, 90.f);
		}
		State->bWeightsALoaded = PmcCommanderRules::LoadWeightsFile(State->Options.WeightsA, State->WeightsA);
		State->bWeightsBLoaded = PmcCommanderRules::LoadWeightsFile(State->Options.WeightsB, State->WeightsB);
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		RunWith(Args, World, false);
	}

	void RunSmoke(const TArray<FString>& Args, UWorld* World)
	{
		RunWith(Args, World, true);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.PmcArena"),
		TEXT("PMC self-play arena: rounds=N seed=S weightsA=<file> weightsB=<file> opponent=pmc|player|bot time=<s> squad=<n> distance=<cm> shot=0|1; writes Saved/Arena/<timestamp>.json, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));

	static FAutoConsoleCommandWithWorldAndArgs SmokeCommand(
		TEXT("CodexTactics.PmcArenaSmoke"),
		TEXT("Dev check: one PMC-vs-PMC arena round on this map and the result JSON validated; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunSmoke));
}

#endif // !UE_BUILD_SHIPPING
