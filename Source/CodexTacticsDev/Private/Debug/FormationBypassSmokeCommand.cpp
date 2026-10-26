// Dev-only headless check of the rules v2.1 bypass "B" (docs/design/FORMATION_SECTORS.md v2.1, contract docs/design/FORMATION_CONTRACT.md
// 10.2 / 10.5; the bot's check 8, Tools/TrainingGround/src/movement/checks.ts) on the formation lab, lane G:
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands FormationBypassSmoke -Jobs 2
//   (map /Game/Maps/L_FormationLab, "-LevelJson=L_FormationLab.json" - Scripts/test_map.json smokeOptions; [-PreCmds "Codex.Formation.Sectors 0"])
// The squad (4 men) walks each lane G route with ONE move order of the leader to its end (a player's click behind a free-standing wall). The
// leader's side round the wall: no threat -> the shortest (R_G_free: the wall on the right); a threat known (the route's threat TargetPoints,
// tag FormationLabThreat, reported to the squad - USquadSubsystem::ReportFormationThreat): the side where the wall shields the squad when that
// detour is at most bypass.max_detour longer (R_G_threat_left: the wall on the left), the shortest when it hides the threat anyway
// (R_G_threat_right) or the shielded side is too long (R_G_far). A last run takes the threat from the squad's own SIGHT: a parked hound at
// R_G_threat_left's threat point, seen by the squad (UTacticalSightSubsystem), nothing reported -> the shielded side again. Per run: the
// decision (wall, side, reason, known threats), the side the leader really passed (FormationBypassRules::WallSideOf on his recorded path,
// like checks.ts), no squad split (check 7, FormationGuardRuntime::SplitCuts) on the way. "shot": a still of the shielded side with the
// threat marked -> Saved/Screenshots/FormationV21/05_bypass_shielded_side.png. Codex.Formation.Sectors 0 / Codex.Formation.V21 0: no bypass
// rules - the nav mesh's own way, reported (the decision checks are skipped, the run passes when the squad gets there).
// Godot reference: none (UE-only).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/ActorLabelTag.h"
#include "Core/CodexTacticsGameMode.h"
#include "Debug/SmokeUtils.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TargetPoint.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Formation/FormationBypassRules.h"
#include "Formation/FormationGuardRuntimeRules.h"
#include "Formation/FormationRuntimeRules.h"
#include "Formation/FormationShapeRules.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/HUD.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "ShaderCompiler.h"
#include "TimerManager.h"
#include "UnrealClient.h"

namespace FormationBypassSmoke
{
	constexpr float NavWarmupSeconds = 3.f;
	constexpr float Timeout = 600.f;
	constexpr float SettleSeconds = 3.f;
	constexpr float ArriveCm = 80.f;
	constexpr float WalkCapSeconds = 75.f;
	/** The parked hound's spot must be seen by the squad within this, s. */
	constexpr float SightWaitSeconds = 3.f;

	struct FLabWall
	{
		FString Id;
		FFormationWall Wall;
	};

	struct FLabThreat
	{
		FVector2D Pos = FVector2D::ZeroVector;
		float SinceS = 0.f;
	};

	struct FLabRoute
	{
		FString Id;
		TArray<FVector2D> Points;
		TArray<FLabThreat> Threats;
		FString ExpectWall;
		FString ExpectSide;
	};

	/** One walk: a route, how its threat is known, what the rules must decide. */
	struct FRun
	{
		FString Label;
		FString RouteId;
		/** The threat comes from the squad's own sight (a parked hound at the route's threat point), not a report. */
		bool bSeenEnemy = false;
		EFormationBypassReason Reason = EFormationBypassReason::Shortest;
	};

	enum class EStage : uint8
	{
		Setup,
		RunStart,
		RunSettle,
		RunWalk,
		Done
	};

	struct FState
	{
		TArray<FLabWall> Walls;
		TArray<FLabRoute> Routes;
		float GroundZ = 0.f;
		TArray<FRun> Plan;
		int32 RunIndex = 0;
		EStage Stage = EStage::Setup;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Failures = 0;
		bool bShot = false;
		bool bRules = true;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Members;
		// Per run.
		int32 BypassesBefore = 0;
		TArray<FVector2D> Trail;
		float SplitTimer = 0.f;
		int32 SplitEvents = 0;
		FString SplitWorst;
		float TrailColumnSeconds = 0.f;
		TWeakObjectPtr<AEnemyCharacter> Hound;
		TWeakObjectPtr<AStaticMeshActor> Marker;
		bool bOrdered = false;
		// Photo.
		bool bPhotoQueued = false;
		bool bPhotoTaken = false;
		int32 PhotoFrames = 0;
		FVector PhotoTarget = FVector::ZeroVector;
		TWeakObjectPtr<ACameraActor> Camera;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	void Enter(FState& State, EStage Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
	}

	bool Finish(FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		State.Stage = EStage::Done;
		FPlatformMisc::RequestExit(false, TEXT("FormationBypassSmoke"));
		return false;
	}

	FVector2D ReadPoint(const TArray<TSharedPtr<FJsonValue>>& Array)
	{
		return Array.Num() >= 2 ? FVector2D(Array[0]->AsNumber(), Array[1]->AsNumber()) : FVector2D::ZeroVector;
	}

	bool LoadLayout(FState& State)
	{
		const FString Path = FPaths::ProjectContentDir() / TEXT("Data/FormationLab/formation_lab_01.json");
		FString Text;
		TSharedPtr<FJsonObject> Root;
		if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
		{
			return false;
		}
		State.GroundZ = static_cast<float>(Root->GetNumberField(TEXT("ground_z")));
		for (const TSharedPtr<FJsonValue>& Value : Root->GetArrayField(TEXT("walls")))
		{
			const TSharedPtr<FJsonObject>& Object = Value->AsObject();
			FLabWall Wall;
			Wall.Id = Object->GetStringField(TEXT("id"));
			Wall.Wall.A = ReadPoint(Object->GetArrayField(TEXT("a")));
			Wall.Wall.B = ReadPoint(Object->GetArrayField(TEXT("b")));
			Wall.Wall.Thickness = static_cast<float>(Object->GetNumberField(TEXT("thickness")));
			State.Walls.Add(Wall);
		}
		for (const TSharedPtr<FJsonValue>& Value : Root->GetArrayField(TEXT("routes")))
		{
			const TSharedPtr<FJsonObject>& Object = Value->AsObject();
			const TArray<TSharedPtr<FJsonValue>>* Expect = nullptr;
			if (!Object->TryGetArrayField(TEXT("expect_bypass"), Expect) || Expect->Num() == 0)
			{
				continue; // lane G only
			}
			FLabRoute Route;
			Route.Id = Object->GetStringField(TEXT("id"));
			for (const TSharedPtr<FJsonValue>& Point : Object->GetArrayField(TEXT("points")))
			{
				Route.Points.Add(ReadPoint(Point->AsArray()));
			}
			const TArray<TSharedPtr<FJsonValue>>* Threats = nullptr;
			if (Object->TryGetArrayField(TEXT("threats"), Threats))
			{
				for (const TSharedPtr<FJsonValue>& Each : *Threats)
				{
					FLabThreat Threat;
					Threat.Pos = ReadPoint(Each->AsObject()->GetArrayField(TEXT("pos")));
					Threat.SinceS = static_cast<float>(Each->AsObject()->GetNumberField(TEXT("since_s")));
					Route.Threats.Add(Threat);
				}
			}
			Route.ExpectWall = (*Expect)[0]->AsObject()->GetStringField(TEXT("wall"));
			Route.ExpectSide = (*Expect)[0]->AsObject()->GetStringField(TEXT("wall_on"));
			State.Routes.Add(Route);
		}
		return State.Walls.Num() > 0 && State.Routes.Num() > 0;
	}

	/** The map's route / threat TargetPoints replace the JSON's (same coordinates while the map is current). */
	void ApplyTargetPoints(UWorld* World, FState& State)
	{
		TMap<FString, TArray<FLabThreat>> Threats;
		TMap<FString, TMap<int32, FVector2D>> Points;
		for (TActorIterator<ATargetPoint> It(World); It; ++It)
		{
			const FString Label = ActorLabelTag::GetLabel(**It);
			if (It->Tags.Contains(FName(TEXT("FormationLabThreat"))))
			{
				const int32 At = Label.Find(TEXT("_threat_"));
				if (At == INDEX_NONE)
				{
					continue;
				}
				FLabThreat Threat;
				Threat.Pos = FVector2D(It->GetActorLocation());
				for (const FName& Tag : It->Tags)
				{
					const FString Text = Tag.ToString();
					if (Text.StartsWith(TEXT("since_s=")))
					{
						Threat.SinceS = FCString::Atof(*Text.Mid(8));
					}
				}
				Threats.FindOrAdd(Label.Left(At)).Add(Threat);
			}
			else if (It->Tags.Contains(FName(TEXT("FormationLabRoute"))))
			{
				int32 Underscore = INDEX_NONE;
				if (Label.FindLastChar(TEXT('_'), Underscore) && Label.Mid(Underscore + 1).IsNumeric())
				{
					Points.FindOrAdd(Label.Left(Underscore)).Add(FCString::Atoi(*Label.Mid(Underscore + 1)), FVector2D(It->GetActorLocation()));
				}
			}
		}
		int32 FromMap = 0;
		for (FLabRoute& Route : State.Routes)
		{
			if (const TArray<FLabThreat>* Found = Threats.Find(Route.Id); Found && Found->Num() == Route.Threats.Num())
			{
				Route.Threats = *Found;
				++FromMap;
			}
			if (const TMap<int32, FVector2D>* Found = Points.Find(Route.Id); Found && Found->Num() == Route.Points.Num())
			{
				for (int32 Index = 0; Index < Route.Points.Num(); ++Index)
				{
					if (const FVector2D* Point = Found->Find(Index))
					{
						Route.Points[Index] = *Point;
					}
				}
			}
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: lane G - %d routes, %d with the map's threat TargetPoints (FormationLabThreat)"), State.Routes.Num(), FromMap);
	}

	const FLabRoute* FindRoute(const FState& State, const FString& Id)
	{
		return State.Routes.FindByPredicate([&Id](const FLabRoute& Route) { return Route.Id == Id; });
	}

	const FLabWall* FindWall(const FState& State, const FString& Id)
	{
		return State.Walls.FindByPredicate([&Id](const FLabWall& Wall) { return Wall.Id == Id; });
	}

	TArray<FFormationWall> AllWalls(const FState& State)
	{
		TArray<FFormationWall> Out;
		for (const FLabWall& Each : State.Walls)
		{
			Out.Add(Each.Wall);
		}
		return Out;
	}

	TArray<AOperativeCharacter*> Living(const FState& State)
	{
		TArray<AOperativeCharacter*> Out;
		for (const TWeakObjectPtr<AOperativeCharacter>& Member : State.Members)
		{
			if (AOperativeCharacter* Each = Member.Get())
			{
				Out.Add(Each);
			}
		}
		return Out;
	}

	void PlaceSquad(UWorld* World, FState& State, const FVector2D& Start, float Yaw)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad->GetLeader();
		const TArray<FFormationSlot> Slots = FormationShapeRules::Slots(EFormationKind::Diamond, 4, Start, Yaw);
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		for (AOperativeCharacter* Each : Living(State))
		{
			Each->StopOperative();
			FVector2D Spot = Start;
			if (Each != Leader)
			{
				const TOptional<EFormationRole> Role = FormationRuntime::RoleForFollowerSlot(Squad->GetFormationSlot(Each), 4);
				const FFormationSlot* Slot = Role.IsSet() ? Slots.FindByPredicate([&Role](const FFormationSlot& S) { return S.Role == Role.GetValue(); }) : nullptr;
				Spot = Slot ? Slot->Position : Start - FVector2D(300.0, 0.0);
			}
			FVector Location(Spot.X, Spot.Y, State.GroundZ + Each->GetSimpleCollisionHalfHeight() + 5.f);
			FNavLocation OnNav;
			if (Nav && !Nav->ProjectPointToNavigation(Location, OnNav, FVector(50.f, 50.f, 300.f)))
			{
				Location = FVector(Start.X - 150.0, Start.Y, Location.Z);
			}
			Each->TeleportTo(Location, FRotator(0.f, Yaw, 0.f), false, true);
		}
		Squad->ResetFormationState(FRotator(0.f, Yaw, 0.f).Vector());
	}

	/** The parked hound at the threat point (tick off: it neither moves nor perceives; the squad's sight sees it). */
	AEnemyCharacter* SpawnParkedHound(UWorld* World, const FVector& At)
	{
		const ACodexTacticsGameMode* GameMode = World->GetAuthGameMode<ACodexTacticsGameMode>();
		const TSoftClassPtr<AEnemyCharacter>* Soft = GameMode ? GameMode->EnemyClasses.Find(EEnemyArchetype::FrostHound) : nullptr;
		UClass* Class = Soft ? Soft->LoadSynchronous() : nullptr;
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		AEnemyCharacter* Hound = nullptr;
		{
			const AEnemyCharacter::FScopedSpawnArchetype Scope(EEnemyArchetype::FrostHound);
			Hound = World->SpawnActor<AEnemyCharacter>(Class ? Class : AEnemyCharacter::StaticClass(), At, FRotator::ZeroRotator, Params);
		}
		if (Hound)
		{
			Hound->InitializeArchetype(EEnemyArchetype::FrostHound);
			Hound->SetActorTickEnabled(false);
			Hound->CustomTimeDilation = 0.f;
		}
		return Hound;
	}

	/** A visible marker at the threat for the still (the TargetPoint is invisible in the game). */
	AStaticMeshActor* SpawnMarker(UWorld* World, const FVector& At)
	{
		UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* Marker = World->SpawnActor<AStaticMeshActor>(At, FRotator::ZeroRotator, Params);
		if (Marker && Sphere)
		{
			Marker->SetMobility(EComponentMobility::Movable);
			Marker->GetStaticMeshComponent()->SetStaticMesh(Sphere);
			Marker->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Marker->SetActorScale3D(FVector(0.8f));
		}
		return Marker;
	}

	void StartRun(UWorld* World, FState& State, const FRun& Run, const FLabRoute& Route)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		Squad->ClearFormationThreats();
		Squad->SetFormationMenuChoice(TOptional<EFormationMenuItem>());
		const float Yaw = FormationMath::YawOf(Route.Points.Last() - Route.Points[0]);
		PlaceSquad(World, State, Route.Points[0], Yaw);
		State.Trail.Reset();
		State.SplitTimer = 0.f;
		State.SplitEvents = 0;
		State.SplitWorst.Reset();
		State.TrailColumnSeconds = 0.f;
		State.bOrdered = false;
		State.BypassesBefore = Squad->GetFormationBypassCount();
		// The threat: reported (the TargetPoint, since_s 0 = known from the start), or the squad's own sight of a parked hound there.
		if (Run.bSeenEnemy && Route.Threats.Num() > 0)
		{
			Squad->SetSquadPosture(ESquadFirePosture::Passive); // nobody opens fire at it (the fight would end the formation)
			State.Hound = SpawnParkedHound(World, FVector(Route.Threats[0].Pos.X, Route.Threats[0].Pos.Y, State.GroundZ + 60.f));
			Check(State, State.Hound.IsValid(), FString::Printf(TEXT("%s: a parked hound at the threat point (%.0f, %.0f), nothing reported"), *Run.Label,
				Route.Threats[0].Pos.X, Route.Threats[0].Pos.Y));
		}
		else
		{
			for (const FLabThreat& Threat : Route.Threats)
			{
				Squad->ReportFormationThreat(FVector(Threat.Pos.X, Threat.Pos.Y, State.GroundZ));
			}
		}
		if (State.bShot && Route.Threats.Num() > 0 && !State.Marker.IsValid())
		{
			State.Marker = SpawnMarker(World, FVector(Route.Threats[0].Pos.X, Route.Threats[0].Pos.Y, State.GroundZ + 80.f));
		}
		else if (State.Marker.IsValid() && Route.Threats.Num() > 0)
		{
			State.Marker->SetActorLocation(FVector(Route.Threats[0].Pos.X, Route.Threats[0].Pos.Y, State.GroundZ + 80.f));
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: run %s (%s, %d threats%s)"), *Run.Label, *Route.Id, Route.Threats.Num(), Run.bSeenEnemy ? TEXT(", seen by the squad") : TEXT(""));
	}

	void SampleWalk(UWorld* World, FState& State, float Dt)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad->GetLeader();
		const FVector2D LeaderPos(Leader->GetActorLocation());
		FormationGuardRuntime::PushTrail(State.Trail, LeaderPos);
		State.TrailColumnSeconds += Squad->IsFormationTrail() ? Dt : 0.f;
		// Check 7 (checks.ts): no wall between the leader and a member's slot / position, or chain neighbours, for >= 0.3 s.
		TArray<FormationGuardRuntime::FSplitMember> Split;
		FormationGuardRuntime::FSplitMember Lead;
		Lead.Pos = LeaderPos;
		Lead.Slot = LeaderPos;
		Split.Add(Lead);
		for (const AOperativeCharacter* Each : Living(State))
		{
			EFormationRole Role;
			FVector Slot;
			float Look = 0.f;
			if (Each != Leader && Squad->GetFormationRole(Each, Role) && Squad->GetFormationTarget(Each, Slot, Look))
			{
				FormationGuardRuntime::FSplitMember Member;
				Member.Place = Role;
				Member.Pos = FVector2D(Each->GetActorLocation());
				Member.Slot = FVector2D(Slot);
				Split.Add(Member);
			}
		}
		const TArray<FString> Cuts = Split.Num() > 1 ? FormationGuardRuntime::SplitCuts(AllWalls(State), Squad->GetFormationKind(), Squad->IsFormationTrail(),
			Squad->GetFormationMemberCount(), Split) : TArray<FString>();
		if (Cuts.Num() > 0)
		{
			State.SplitTimer += Dt;
			if (State.SplitTimer >= FormationGuardRuntime::SplitSeconds && State.SplitTimer - Dt < FormationGuardRuntime::SplitSeconds)
			{
				++State.SplitEvents;
				if (State.SplitWorst.IsEmpty())
				{
					State.SplitWorst = FString::Printf(TEXT("%s: wall cuts %s at (%.0f, %.0f)"), FormationNames::ToString(Squad->GetFormationKind()),
						*FString::Join(Cuts, TEXT(", ")), LeaderPos.X, LeaderPos.Y);
				}
			}
		}
		else
		{
			State.SplitTimer = 0.f;
		}
	}

	void CheckRun(UWorld* World, FState& State, const FRun& Run, const FLabRoute& Route)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		const FLabWall* Wall = FindWall(State, Route.ExpectWall);
		if (!Wall)
		{
			Check(State, false, FString::Printf(TEXT("%s: wall %s in the layout"), *Run.Label, *Route.ExpectWall));
			return;
		}
		// The side the leader really passed (checks.ts check 8: wallSideOf on his recorded path).
		TArray<FVector2D> Path = State.Trail;
		if (Path.Num() < 2)
		{
			Path.Add(Path.Num() > 0 ? Path[0] : FVector2D::ZeroVector);
		}
		const FString Passed = FormationGuardRuntime::WallSideText(FormationBypassRules::WallSideOf(Path, Wall->Wall));
		FFormationBypassRecord Record;
		const bool bDecided = Squad->GetFormationBypassCount() > State.BypassesBefore && Squad->GetLastFormationBypass(Record);
		if (!State.bRules)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: %s: the bypass rules are off - the leader passed %s with the wall on the %s (wants %s with the rules)"), *Run.Label,
				*Route.ExpectWall, *Passed, *Route.ExpectSide);
			Check(State, !bDecided, FString::Printf(TEXT("%s: no bypass decision with the rules off"), *Run.Label));
			return;
		}
		const bool bSameWall = bDecided && FVector2D::Distance((Record.Wall.A + Record.Wall.B) * 0.5, (Wall->Wall.A + Wall->Wall.B) * 0.5) <= 60.0;
		Check(State, bDecided && bSameWall, FString::Printf(TEXT("%s: a bypass decision for %s (%s)"), *Run.Label, *Route.ExpectWall,
			bDecided ? *FString::Printf(TEXT("wall (%.0f, %.0f)-(%.0f, %.0f) %.0f cm"), Record.Wall.A.X, Record.Wall.A.Y, Record.Wall.B.X, Record.Wall.B.Y, Record.Wall.Thickness)
			: TEXT("none")));
		if (bDecided)
		{
			const FString Side = FormationGuardRuntime::WallSideText(Record.Choice.WallOn);
			Check(State, Side == Route.ExpectSide && Record.Choice.Reason == Run.Reason,
				FString::Printf(TEXT("%s: the rules pick the wall on the %s (%s, detour %.1f %%, the shortest on the %s) - want %s (%s)"), *Run.Label, *Side,
					FormationGuardRuntime::BypassReasonText(Record.Choice.Reason), 100.f * Record.Choice.Detour, FormationGuardRuntime::WallSideText(Record.Choice.Shortest),
					*Route.ExpectSide, FormationGuardRuntime::BypassReasonText(Run.Reason)));
			Check(State, (Route.Threats.Num() > 0) == (Record.Threats > 0), FString::Printf(TEXT("%s: %d known threats counted (%s)"), *Run.Label, Record.Threats,
				Route.Threats.Num() > 0 ? (Run.bSeenEnemy ? TEXT("the squad's own sight") : TEXT("reported")) : TEXT("none")));
			Check(State, Record.bSteered, FString::Printf(TEXT("%s: the leader is steered round the wall's end (%d corners)"), *Run.Label,
				FormationGuardRuntime::BypassWaypoints(Record.Choice).Num()));
		}
		Check(State, Passed == Route.ExpectSide, FString::Printf(TEXT("%s: the leader passed %s with the wall on the %s (want %s; checks.ts 8)"), *Run.Label,
			*Route.ExpectWall, *Passed, *Route.ExpectSide));
		Check(State, State.SplitEvents == 0, FString::Printf(TEXT("%s: the squad never splits round the wall (%d splits%s; trail column %.1f s)"), *Run.Label,
			State.SplitEvents, State.SplitWorst.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(", %s"), *State.SplitWorst), State.TrailColumnSeconds));
	}

	/** The still of the shielded side (shot): the squad beside the wall, the threat marked; the game slowed while it is taken. */
	bool StepPhoto(UWorld* World, FState& State)
	{
		if (!State.bPhotoQueued || State.bPhotoTaken)
		{
			return false;
		}
		APlayerController* PC = World->GetFirstPlayerController();
		if (!PC)
		{
			State.bPhotoTaken = true;
			return false;
		}
		if (State.PhotoFrames == 0)
		{
			UGameplayStatics::SetGlobalTimeDilation(World, 0.01f);
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			State.Camera = World->SpawnActor<ACameraActor>(State.PhotoTarget, FRotator::ZeroRotator, Params);
		}
		if (ACameraActor* Camera = State.Camera.Get())
		{
			const FRotator View(-62.f, 180.f + 20.f, 0.f);
			Camera->GetCameraComponent()->SetFieldOfView(60.f);
			Camera->GetCameraComponent()->bConstrainAspectRatio = false;
			Camera->SetActorLocationAndRotation(State.PhotoTarget - View.Vector() * 2200.f, View);
			PC->SetViewTarget(Camera);
		}
		++State.PhotoFrames;
		const bool bShadersDone = !GShaderCompilingManager || GShaderCompilingManager->GetNumRemainingJobs() == 0;
		if (State.PhotoFrames >= 30 && (bShadersDone || State.PhotoFrames >= 900) && State.PhotoFrames < 1000)
		{
			const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("FormationV21") / TEXT("05_bypass_shielded_side.png");
			FScreenshotRequest::RequestScreenshot(Path, /*bInShowUI*/ true, /*bAddFilenameSuffix*/ false);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke screenshot %s"), *Path);
			State.PhotoFrames = 1000;
		}
		if (State.PhotoFrames >= 1015)
		{
			State.bPhotoTaken = true;
			UGameplayStatics::SetGlobalTimeDilation(World, 1.f);
			PC->SetViewTarget(PC->GetPawn());
		}
		return true;
	}

	bool Step(UWorld* World, FState& State, float Dt)
	{
		if (StepPhoto(World, State))
		{
			return true;
		}
		State.Time += Dt;
		State.StageTime += Dt;
		if (State.Time > Timeout)
		{
			Check(State, false, FString::Printf(TEXT("timeout in stage %d (run %d)"), static_cast<int32>(State.Stage), State.RunIndex));
			return Finish(State, false);
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Squad || !Leader)
		{
			return true;
		}
		const FRun* Run = State.Plan.IsValidIndex(State.RunIndex) ? &State.Plan[State.RunIndex] : nullptr;
		const FLabRoute* Route = Run ? FindRoute(State, Run->RouteId) : nullptr;
		switch (State.Stage)
		{
		case EStage::Setup:
		{
			const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
			Check(State, Flow && Flow->GetPhase() == ECodexGamePhase::Exploration, TEXT("exploration phase on the lab"));
			State.bRules = FormationRuntime::IsEnabled() && FormationGuardRuntime::IsEnabled();
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: the bypass rules are %s (Codex.Formation.Sectors %d, Codex.Formation.V21 %d)"), State.bRules ? TEXT("on") : TEXT("off"),
				FormationRuntime::IsEnabled() ? 1 : 0, FormationGuardRuntime::IsEnabled() ? 1 : 0);
			if (Squad->IsSoloMode())
			{
				Squad->ExitSoloMode();
			}
			if (ACodexTacticsGameMode* GameMode = World->GetAuthGameMode<ACodexTacticsGameMode>(); GameMode && Squad->GetMembers().Num() < 4)
			{
				GameMode->SpawnOperative(GameMode->RecruitSusanin, 3, Leader->GetActorLocation() - Leader->GetActorForwardVector() * 560.f, Leader->GetActorRotation(), true);
			}
			for (AOperativeCharacter* Each : Squad->GetMembers())
			{
				State.Members.Add(Each);
			}
			Check(State, State.Members.Num() == 4, FString::Printf(TEXT("4 operatives in the squad (%d)"), State.Members.Num()));
			Enter(State, EStage::RunStart);
			return true;
		}

		case EStage::RunStart:
			if (!Run)
			{
				return Finish(State, true);
			}
			if (!Route || Route->Points.Num() < 2)
			{
				Check(State, false, FString::Printf(TEXT("route %s in the layout"), *Run->RouteId));
				++State.RunIndex;
				return true;
			}
			StartRun(World, State, *Run, *Route);
			Enter(State, EStage::RunSettle);
			return true;

		case EStage::RunSettle:
		{
			// The seen-enemy run: the squad's sight must know the hound before the click (else the test means nothing).
			bool bReady = State.StageTime >= SettleSeconds;
			if (Run->bSeenEnemy && bReady)
			{
				const UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>();
				TArray<FSquadKnownEnemy> Known;
				const bool bSightRuns = Sight && Sight->GetSquadKnownEnemies(Known);
				const bool bKnown = !bSightRuns || Known.ContainsByPredicate([](const FSquadKnownEnemy& Each) { return Each.SecondsSince <= 1.f; });
				bReady = bKnown || State.StageTime >= SettleSeconds + SightWaitSeconds;
				if (bReady)
				{
					Check(State, bKnown, FString::Printf(TEXT("%s: the squad %s the hound (%d known enemies)"), *Run->Label, bSightRuns ? TEXT("sees") : TEXT("has a clear line to"),
						Known.Num()));
				}
			}
			if (bReady)
			{
				// One click at the route's end (behind the wall).
				const FVector2D End = Route->Points.Last();
				Leader->OrderMoveTo(FVector(End.X, End.Y, Leader->GetActorLocation().Z), false);
				State.bOrdered = true;
				Enter(State, EStage::RunWalk);
			}
			return true;
		}

		case EStage::RunWalk:
		{
			SampleWalk(World, State, Dt);
			const FVector2D Here(Leader->GetActorLocation());
			const bool bArrived = FVector2D::Distance(Here, Route->Points.Last()) <= ArriveCm && !Leader->IsMoving() && State.StageTime > 1.f;
			// The still: the shielded run, the leader abreast of the wall.
			if (State.bShot && !State.bPhotoQueued && Run->RouteId == TEXT("R_G_threat_left") && !Run->bSeenEnemy)
			{
				const FLabWall* Wall = FindWall(State, Route->ExpectWall);
				if (Wall && FMath::Abs(Here.X - 0.5 * (Wall->Wall.A.X + Wall->Wall.B.X)) <= 100.0)
				{
					State.bPhotoQueued = true;
					State.PhotoTarget = FVector(0.5 * (Wall->Wall.A + Wall->Wall.B), State.GroundZ);
				}
			}
			if (bArrived || State.StageTime > WalkCapSeconds)
			{
				Check(State, bArrived, FString::Printf(TEXT("%s: the leader reaches the route's end (%.0f cm off after %.0f s)"), *Run->Label,
					FVector2D::Distance(Here, Route->Points.Last()), State.StageTime));
				CheckRun(World, State, *Run, *Route);
				if (AEnemyCharacter* Hound = State.Hound.Get())
				{
					Hound->Destroy();
					Squad->SetSquadPosture(ESquadFirePosture::Aggressive);
				}
				State.Hound.Reset();
				++State.RunIndex;
				Enter(State, EStage::RunStart);
			}
			return true;
		}

		default:
			return false;
		}
	}

	void Start(UWorld* World, bool bShot)
	{
		TSharedRef<FState> State = MakeShared<FState>();
		State->bShot = bShot;
		if (!LoadLayout(*State))
		{
			Check(*State, false, TEXT("the lab layout with lane G (formation_lab_01.json expect_bypass routes)"));
			Finish(*State, false);
			return;
		}
		ApplyTargetPoints(World, *State);
		const auto Add = [&State](const TCHAR* Label, const TCHAR* Route, bool bSeen, EFormationBypassReason Reason)
		{
			FRun Run;
			Run.Label = Label;
			Run.RouteId = Route;
			Run.bSeenEnemy = bSeen;
			Run.Reason = Reason;
			State->Plan.Add(Run);
		};
		// Contract 10.5: G1 W_G_19 oblique across the route, G2 W_G_20 square to it and lopsided.
		Add(TEXT("R_G_free"), TEXT("R_G_free"), false, EFormationBypassReason::Shortest);
		Add(TEXT("R_G_threat_right"), TEXT("R_G_threat_right"), false, EFormationBypassReason::Shortest);
		Add(TEXT("R_G_threat_left"), TEXT("R_G_threat_left"), false, EFormationBypassReason::Shielded);
		Add(TEXT("R_G_far"), TEXT("R_G_far"), false, EFormationBypassReason::TooLong);
		Add(TEXT("R_G_threat_left (seen hound)"), TEXT("R_G_threat_left"), true, EFormationBypassReason::Shielded);
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			UWorld* W = WeakWorld.Get();
			return W && State->Stage != EStage::Done && Step(W, *State, W->IsPaused() ? 0.f : W->GetDeltaSeconds());
		}), 0.f);
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		const bool bShot = Args.Contains(TEXT("shot"));
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld, bShot]()
		{
			if (UWorld* W = WeakWorld.Get())
			{
				Start(W, bShot);
			}
		}), NavWarmupSeconds, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.FormationBypassSmoke"),
		TEXT("Dev check (rules v2.1 bypass B): on the formation lab's lane G the leader's click behind a free-standing wall passes it on the shortest side, or - ")
		TEXT("a threat known (reported / seen by the squad) - on the side the wall shields when the detour is <= bypass.max_detour; the squad never splits. ")
		TEXT("[shot]. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
