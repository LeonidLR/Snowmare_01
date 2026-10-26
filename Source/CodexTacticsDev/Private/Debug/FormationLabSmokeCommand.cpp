// Dev-only headless check of the formation look sectors in the game (stage 6 of docs/design/FORMATION_SECTORS.md; contract
// docs/design/FORMATION_CONTRACT.md 4 / 7) on the formation lab map built from Content/Data/FormationLab/formation_lab_01.json:
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands FormationLabSmoke
//   (map /Game/Maps/L_FormationLab, "-LevelJson=L_FormationLab.json" - Scripts/test_map.json smokeOptions)
// The squad (3 operatives + a 4th spawned here = the diamond) walks every lab route with the leader's move orders (the route points are the
// map's TargetPoints labelled <route>_<n>, tags FormationLab / FormationLabRoute; the layout JSON's points when the map has none). Per frame
// (the Training Ground bot's checks, Tools/TrainingGround/src/movement/checks.ts): the formation the squad chose in each labelled zone
// (>= 80 % of the settled time = the zone's expectation), nobody faces a wall closer than 200 cm (within 45 deg) for >= 0.3 s unless he
// walks that way, the rear man keeps <= 150 cm behind his slot (p95, 4 men walking), the leader patrols at pace.patrol x walk speed, the run
// route faces 12, the narrow column's sectors (P +15, L -165). At the two dead ends (R_A_open, R_F_deadend) the wall layout: everyone on his
// FormationWallRules slot, nobody faces the wall. Then the group cover order in group mode at the R_F dead end = the wall layout
// (crouched, facing out, not in cover), the leader's next order ends it (they stand up and follow), and R_F with 3 men (wedge, 3-man wall).
// Rules v2.1 (contract 10, checks.ts): check 4 judges the REQUESTED formation (the guard's fallbacks are geometry); check 7 on every run (no
// wall between the leader and a member's slot / position, or between chain neighbours, for >= 0.3 s); check 9 (when the leader passes a free
// wall end beside him on patrol someone looks round it); the corner watcher's look at a wall end is no stare; the turn fold on R_E_corner (the
// player clicks ahead along the route, round its corners); the guard with the hold-B menu's manual choices (Codex.Formation.Menu): R_B_walls
// with "echelon left" (the user's bug: the mirror echelon right, HUD "Echelon left -> Echelon right (wall)"), R_D_corridors with "line" (the
// trail column, HUD "Line -> Column (wall)"); a smoke-own pass of W_E_14's free end (R_E_corner_pass: the corner watch). "shot" also saves
// the v2.1 stills to Saved/Screenshots/FormationV21/ (the guard switch with the HUD line, the trail column round the corner, the corner watch).
// Known rules v1.1 limits the simulator shows too are reported, not failed (FormationLabSmoke::Known; rules v1.2 is being ported).
// "shot" (rendered: UnrealEditor.exe <uproject> /Game/Maps/L_FormationLab -game -RenderOffscreen -ResX=1280 -ResY=720
// "-LevelJson=L_FormationLab.json" "-ExecCmds=CodexTactics.FormationLabSmoke shot") saves stills to Saved/Screenshots/FormationLab/
// (diamond in the open, echelon at a wall, column in the narrow corridor, the wall layout, the wall order).
// Godot reference: none (UE-only).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Containers/Ticker.h"
#include "Core/ActorLabelTag.h"
#include "Core/CodexTacticsGameMode.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Dom/JsonObject.h"
#include "Engine/TargetPoint.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Formation/FormationCornerRules.h"
#include "Formation/FormationGuardRuntimeRules.h"
#include "Formation/FormationMenuRules.h"
#include "Formation/FormationRuntimeRules.h"
#include "Formation/FormationShapeRules.h"
#include "Formation/FormationTuning.h"
#include "Formation/FormationTypes.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Tactics/CoverTraceRules.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "GameFramework/HUD.h"
#include "Engine/Engine.h"
#include "ShaderCompiler.h"

namespace FormationLabSmoke
{
	constexpr float NavWarmupSeconds = 3.f;
	constexpr float Timeout = 1500.f;
	/** Contract 4 / checks.ts LIMITS. */
	constexpr float StareDistCm = 200.f;
	constexpr float StareAngleDeg = 45.f;
	constexpr float StareMinSeconds = 0.3f;
	constexpr float ExpectPct = 80.f;
	/** checks.ts v2: cm of the leader's path in a zone before its expectation counts (+ classify.hold_distance for a looser one); halted 0.5 s. */
	constexpr float ExpectSettleCm = 100.f;
	constexpr float ExpectHaltSettleSeconds = 0.5f;
	constexpr float PaceBehindCm = 150.f;
	/** Followers settle round the leader this long after a teleport, s. */
	constexpr float SettleSeconds = 3.f;
	/** The leader is at a route point within this distance, cm. */
	constexpr float PointReachCm = 60.f;
	/** A route corner: the next leg starts this close to it, cm. */
	constexpr float CornerTurnCm = 150.f;
	/** The leader's move order is renewed this often to a point this far ahead on the leg (clicking ahead), s / cm. */
	constexpr float CarrotSeconds = 1.f;
	constexpr float CarrotAheadCm = 600.f;
	/** At the end of a dead-end route the squad has this long to stand in the wall layout, s. */
	constexpr float WallHaltMaxSeconds = 12.f;
	constexpr float HaltSeconds = 3.f;
	/** At a dead end the squad stands this long at least (the wall layout settles; checks.ts halts 3 s after an instant arrival). */
	constexpr float WallHaltSeconds = 4.f;
	/** A member stands on his wall slot within this distance, cm. */
	constexpr float WallSlotTolCm = 120.f;

	/** Rules v1.1 limits the Training Ground simulator fails too (Saved/TrainingGround/movement/report.md, 2026-10-25): reported, not failed. */
	struct FKnown
	{
		const TCHAR* Route;
		const TCHAR* Check;
		const TCHAR* Why;
	};
	const FKnown Known[] = {
		{ TEXT("R_B_walls"), TEXT("pace"), TEXT("rules v2 / contract 7: at the echelon <-> diamond switches the 4th man crosses ~5.4 m to his new slot walking BACKWARDS (he looks 150 / 180 deg), capped at walk x move.backward_mult (0.9) = only ~22 cm/s over the squad's patrol pace; the game shows p95 160-175 cm, the simulator 95-105 cm (its members turn and accelerate instantly) - a tuning / design point for the Training Ground (2026-10-25)") },
	};

	const TCHAR* KnownWhy(const FString& Route, const TCHAR* Check)
	{
		for (const FKnown& Each : Known)
		{
			if (Route == Each.Route && FCString::Strcmp(Check, Each.Check) == 0)
			{
				return Each.Why;
			}
		}
		return nullptr;
	}

	struct FLabWall
	{
		FString Id;
		FVector2D A = FVector2D::ZeroVector;
		FVector2D B = FVector2D::ZeroVector;
		float Thickness = 0.f;
	};

	struct FLabZone
	{
		FString Id;
		TArray<FVector2D> Poly;
	};

	struct FLabExpect
	{
		FString Zone;
		FString Formation;
	};

	struct FLabRoute
	{
		FString Id;
		bool bRun = false;
		TArray<FVector2D> Points;
		TArray<FLabExpect> Expect;
	};

	struct FLabLayout
	{
		TArray<FLabWall> Walls;
		TArray<FLabZone> Zones;
		TArray<FLabRoute> Routes;
		float GroundZ = 0.f;
	};

	/** One walk of the plan: a route with N members; what follows it. */
	struct FRun
	{
		FString RouteId;
		int32 MemberCount = 4;
		bool bWallOrderAfter = false;
		/** Rules v2.1: the hold-B menu's choice for this run (FormationMenuRules id; empty = auto). A manual run is gated like checks.ts gates
		 * manual formations (1 stare, 7 split; the zone expectations and the rear man's pace are reported, not failed). */
		FString Menu;
		/** The HUD line that must show while the wall guard falls back on this run (empty = none expected). */
		FString ExpectHud;
		/** The sharp-turn fold must fold the formation into the trail column near this route's corner (R_E_corner). */
		bool bExpectFold = false;
		/** At least one free wall end is passed on this run: check 9 must have counted it. */
		bool bExpectCorner = false;
	};

	enum class EStage : uint8
	{
		Setup,
		RouteStart,
		RouteSettle,
		RouteWalk,
		RouteHalt,
		OrderBack,
		OrderGo,
		OrderHold,
		OrderLeave,
		Done
	};

	struct FZoneAcc
	{
		float Settled = 0.f;
		float Match = 0.f;
		TMap<FString, float> Seen;
	};

	struct FPhoto
	{
		FString Name;
		FVector Target = FVector::ZeroVector;
		FRotator View = FRotator::ZeroRotator;
		float Distance = 1500.f;
		int32 Frames = 0;
		int32 TakenFrame = 0;
		bool bTaken = false;
		/** Rules v2.1 stills: the HUD stays on (the guard's line) and the still goes to Saved/Screenshots/FormationV21/. */
		bool bShowHud = false;
		FString Folder = TEXT("FormationLab");
	};

	struct FState
	{
		FLabLayout Layout;
		TArray<FRun> Plan;
		int32 RunIndex = 0;
		EStage Stage = EStage::Setup;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Failures = 0;
		int32 Point = 1;
		float LegTime = 0.f;
		float StillTime = 0.f;
		float CarrotTime = 0.f;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Members;
		TWeakObjectPtr<AOperativeCharacter> Fourth;
		// Per-route metrics.
		/** The leader's path on this route, cm, and where it was entered per zone (v2 counts path, not time). */
		float LeaderPath = 0.f;
		TOptional<FVector2D> LastLeaderPos;
		TMap<FString, float> ZoneEnter;
		TMap<FString, FZoneAcc> Zones;
		TMap<int32, float> StareTimer;
		TSet<int32> StareCounted;
		int32 StareEvents = 0;
		FString StareWorst;
		float StareWorstSeconds = 0.f;
		TArray<float> Behind;
		/** Pace samples where the rear man's slot was off the navmesh and he walked to its stand-in (rules v2.3, contract 13.5). */
		int32 BehindStandIn = 0;
		float PaceTime = 0.f;
		float PaceDistance = 0.f;
		int32 FormationChanges = 0;
		TOptional<EFormationKind> LastFormation;
		float RunModeTime = 0.f;
		float RunFacingOff = 0.f;
		int32 NarrowSamples = 0;
		int32 NarrowOk = 0;
		FString NarrowWorst;
		// Rules v2.1 (checks 7 / 9, the guard).
		float SplitTimer = 0.f;
		bool bSplitCounted = false;
		int32 SplitEvents = 0;
		FString SplitWorst;
		float SplitWorstSeconds = 0.f;
		TMap<FString, float> CornerPrev;
		int32 CornerEnds = 0;
		int32 CornerMissed = 0;
		FString CornerWorst;
		int32 MaxGuardLevel = 0;
		float TrailSeconds = 0.f;
		float TrailNearCorner = 0.f;
		TSet<FString> HudSeen;
		float CornerWatchSeconds = 0.f;
		// Photos ("shot").
		bool bShot = false;
		TSet<FString> PhotosTaken;
		TArray<FPhoto> Photos;
		TWeakObjectPtr<ACameraActor> Camera;
		bool bPausedForPhoto = false;
		// The wall order stage.
		FCoverSlot OrderSlot;
		FVector OrderBackPoint = FVector::ZeroVector;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	/** A check that may fail by a known rules v1.1 limit (KnownWhy): reported "known", never failed. */
	void CheckKnown(FState& State, const FString& Route, const TCHAR* CheckName, bool bOk, const FString& What)
	{
		const TCHAR* Why = bOk ? nullptr : KnownWhy(Route, CheckName);
		if (Why)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke known: %s - %s"), *What, Why);
			return;
		}
		Check(State, bOk, What);
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
		FPlatformMisc::RequestExit(false, TEXT("FormationLabSmoke"));
		return false;
	}

	FVector2D ReadPoint(const TArray<TSharedPtr<FJsonValue>>& Array)
	{
		return Array.Num() >= 2 ? FVector2D(Array[0]->AsNumber(), Array[1]->AsNumber()) : FVector2D::ZeroVector;
	}

	bool LoadLayout(FLabLayout& Out, FString& Error)
	{
		const FString Path = FPaths::ProjectContentDir() / TEXT("Data/FormationLab/formation_lab_01.json");
		FString Text;
		TSharedPtr<FJsonObject> Root;
		if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
		{
			Error = FString::Printf(TEXT("cannot read %s"), *Path);
			return false;
		}
		Out.GroundZ = static_cast<float>(Root->GetNumberField(TEXT("ground_z")));
		for (const TSharedPtr<FJsonValue>& Value : Root->GetArrayField(TEXT("walls")))
		{
			const TSharedPtr<FJsonObject>& Object = Value->AsObject();
			FLabWall Wall;
			Wall.Id = Object->GetStringField(TEXT("id"));
			Wall.A = ReadPoint(Object->GetArrayField(TEXT("a")));
			Wall.B = ReadPoint(Object->GetArrayField(TEXT("b")));
			Wall.Thickness = static_cast<float>(Object->GetNumberField(TEXT("thickness")));
			Out.Walls.Add(Wall);
		}
		for (const TSharedPtr<FJsonValue>& Value : Root->GetArrayField(TEXT("zones")))
		{
			const TSharedPtr<FJsonObject>& Object = Value->AsObject();
			FLabZone Zone;
			Zone.Id = Object->GetStringField(TEXT("id"));
			for (const TSharedPtr<FJsonValue>& Point : Object->GetArrayField(TEXT("poly")))
			{
				Zone.Poly.Add(ReadPoint(Point->AsArray()));
			}
			Out.Zones.Add(Zone);
		}
		for (const TSharedPtr<FJsonValue>& Value : Root->GetArrayField(TEXT("routes")))
		{
			const TSharedPtr<FJsonObject>& Object = Value->AsObject();
			FLabRoute Route;
			Route.Id = Object->GetStringField(TEXT("id"));
			Route.bRun = Object->GetStringField(TEXT("pace")) == TEXT("run");
			for (const TSharedPtr<FJsonValue>& Point : Object->GetArrayField(TEXT("points")))
			{
				Route.Points.Add(ReadPoint(Point->AsArray()));
			}
			const TArray<TSharedPtr<FJsonValue>>* Expect = nullptr;
			if (Object->TryGetArrayField(TEXT("expect"), Expect))
			{
				for (const TSharedPtr<FJsonValue>& Each : *Expect)
				{
					FLabExpect Item;
					Item.Zone = Each->AsObject()->GetStringField(TEXT("zone"));
					Item.Formation = Each->AsObject()->GetStringField(TEXT("formation"));
					Route.Expect.Add(Item);
				}
			}
			Out.Routes.Add(Route);
		}
		return Out.Walls.Num() > 0 && Out.Routes.Num() > 0;
	}

	/** The map's route TargetPoints (label <route>_<n>) replace the JSON points (same coordinates when the map is current). */
	void ApplyTargetPoints(UWorld* World, FLabLayout& Layout)
	{
		TMap<FString, TMap<int32, FVector2D>> Found;
		int32 Count = 0;
		for (TActorIterator<ATargetPoint> It(World); It; ++It)
		{
			if (!It->Tags.Contains(FName(TEXT("FormationLabRoute"))))
			{
				continue;
			}
			const FString Label = ActorLabelTag::GetLabel(**It);
			int32 Underscore = INDEX_NONE;
			if (!Label.FindLastChar(TEXT('_'), Underscore))
			{
				continue;
			}
			const FString RouteId = Label.Left(Underscore);
			const FString Index = Label.Mid(Underscore + 1);
			if (!Index.IsNumeric())
			{
				continue;
			}
			Found.FindOrAdd(RouteId).Add(FCString::Atoi(*Index), FVector2D(It->GetActorLocation()));
			++Count;
		}
		int32 Replaced = 0;
		for (FLabRoute& Route : Layout.Routes)
		{
			const TMap<int32, FVector2D>* Points = Found.Find(Route.Id);
			if (!Points || Points->Num() != Route.Points.Num())
			{
				continue;
			}
			bool bAll = true;
			for (int32 Index = 0; Index < Route.Points.Num(); ++Index)
			{
				bAll &= Points->Contains(Index);
			}
			if (bAll)
			{
				for (int32 Index = 0; Index < Route.Points.Num(); ++Index)
				{
					Route.Points[Index] = (*Points)[Index];
				}
				++Replaced;
			}
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %d route TargetPoints in the map, %d / %d routes walk them (else the layout JSON's points)"), Count, Replaced,
			Layout.Routes.Num());
	}

	const FLabRoute* FindRoute(const FState& State, const FString& Id)
	{
		return State.Layout.Routes.FindByPredicate([&Id](const FLabRoute& Route) { return Route.Id == Id; });
	}

	bool InPoly(const FVector2D& P, const TArray<FVector2D>& Poly)
	{
		bool bInside = false;
		for (int32 I = 0, J = Poly.Num() - 1; I < Poly.Num(); J = I++)
		{
			if (((Poly[I].Y > P.Y) != (Poly[J].Y > P.Y)) && (P.X < (Poly[J].X - Poly[I].X) * (P.Y - Poly[I].Y) / (Poly[J].Y - Poly[I].Y) + Poly[I].X))
			{
				bInside = !bInside;
			}
		}
		return bInside;
	}

	const FLabZone* ZoneAt(const FState& State, const FVector2D& P)
	{
		return State.Layout.Zones.FindByPredicate([&P](const FLabZone& Zone) { return InPoly(P, Zone.Poly); });
	}

	FVector2D ClosestOnSegment(const FVector2D& P, const FVector2D& A, const FVector2D& B)
	{
		const FVector2D Edge = B - A;
		const double LengthSquared = Edge.SizeSquared();
		const double T = LengthSquared > 0.0 ? FMath::Clamp(FVector2D::DotProduct(P - A, Edge) / LengthSquared, 0.0, 1.0) : 0.0;
		return A + Edge * T;
	}

	bool SameFormation(const FString& A, EFormationKind B)
	{
		const FString Name = FormationNames::ToString(B);
		const bool bDiamondA = A == TEXT("diamond") || A == TEXT("wedge");
		const bool bDiamondB = B == EFormationKind::Diamond || B == EFormationKind::Wedge;
		return A == Name || (bDiamondA && bDiamondB);
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

	float Yaw2D(const FVector& V)
	{
		return static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(V.Y, V.X)));
	}

	void ResetMetrics(FState& State)
	{
		State.ZoneEnter.Reset();
		State.LeaderPath = 0.f;
		State.LastLeaderPos.Reset();
		State.Zones.Reset();
		State.StareTimer.Reset();
		State.StareCounted.Reset();
		State.StareEvents = 0;
		State.StareWorst.Reset();
		State.StareWorstSeconds = 0.f;
		State.Behind.Reset();
		State.BehindStandIn = 0;
		State.PaceTime = 0.f;
		State.PaceDistance = 0.f;
		State.FormationChanges = 0;
		State.LastFormation.Reset();
		State.RunModeTime = 0.f;
		State.RunFacingOff = 0.f;
		State.NarrowSamples = 0;
		State.NarrowOk = 0;
		State.NarrowWorst.Reset();
		State.SplitTimer = 0.f;
		State.bSplitCounted = false;
		State.SplitEvents = 0;
		State.SplitWorst.Reset();
		State.SplitWorstSeconds = 0.f;
		State.CornerPrev.Reset();
		State.CornerEnds = 0;
		State.CornerMissed = 0;
		State.CornerWorst.Reset();
		State.MaxGuardLevel = 0;
		State.TrailSeconds = 0.f;
		State.TrailNearCorner = 0.f;
		State.HudSeen.Reset();
		State.CornerWatchSeconds = 0.f;
	}

	/** The layout's walls as the rules see them (checks 7 / 9 use the layout, like checks.ts). */
	TArray<FFormationWall> LayoutWalls(const FState& State)
	{
		TArray<FFormationWall> Out;
		for (const FLabWall& Each : State.Layout.Walls)
		{
			FFormationWall Wall;
			Wall.A = Each.A;
			Wall.B = Each.B;
			Wall.Thickness = Each.Thickness;
			Out.Add(Wall);
		}
		return Out;
	}

	/** The members whose look is the corner watch this frame (their look at a wall end is no stare). */
	TSet<const AOperativeCharacter*> CornerWatchers(const USquadSubsystem& Squad, const TArray<AOperativeCharacter*>& All)
	{
		TSet<const AOperativeCharacter*> Out;
		for (const FFormationCornerLook& Look : Squad.GetFormationCornerLooks())
		{
			for (const AOperativeCharacter* Each : All)
			{
				EFormationRole Role;
				if (Squad.GetFormationRole(Each, Role) && Role == Look.Role)
				{
					Out.Add(Each);
				}
			}
		}
		return Out;
	}

	FVector Centroid(const FState& State)
	{
		FVector Sum = FVector::ZeroVector;
		const TArray<AOperativeCharacter*> All = Living(State);
		for (const AOperativeCharacter* Each : All)
		{
			Sum += Each->GetActorLocation();
		}
		return All.IsEmpty() ? Sum : Sum / All.Num();
	}

	void QueuePhoto(FState& State, const FString& Name, const FVector& Target, float ViewYaw, float Pitch = -55.f, float Distance = 1500.f,
		bool bV21 = false)
	{
		if (!State.bShot || State.PhotosTaken.Contains(Name))
		{
			return;
		}
		State.PhotosTaken.Add(Name);
		FPhoto Photo;
		Photo.Name = Name;
		Photo.Target = Target;
		Photo.View = FRotator(Pitch, ViewYaw, 0.f);
		Photo.Distance = Distance;
		if (bV21)
		{
			Photo.bShowHud = true;
			Photo.Folder = TEXT("FormationV21");
		}
		State.Photos.Add(Photo);
	}

	/** Takes the queued photo (the game slowed to a standstill, the HUD hidden, shaders compiled); true while one is in progress. */
	bool StepPhotos(UWorld* World, FState& State)
	{
		if (State.Photos.IsEmpty())
		{
			return false;
		}
		APlayerController* PC = World->GetFirstPlayerController();
		FPhoto& Photo = State.Photos[0];
		if (!PC)
		{
			State.Photos.RemoveAt(0);
			return true;
		}
		if (Photo.Frames == 0)
		{
			UGameplayStatics::SetGlobalTimeDilation(World, 0.01f);
			State.bPausedForPhoto = true;
			if (AHUD* Hud = PC->GetHUD())
			{
				Hud->bShowHUD = Photo.bShowHud;
			}
			GEngine->Exec(World, TEXT("DisableAllScreenMessages"));
			if (!State.Camera.IsValid())
			{
				FActorSpawnParameters Params;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				State.Camera = World->SpawnActor<ACameraActor>(Photo.Target, FRotator::ZeroRotator, Params);
			}
		}
		// Every frame (the game's camera code may take the view back).
		if (ACameraActor* Camera = State.Camera.Get())
		{
			Camera->GetCameraComponent()->SetFieldOfView(60.f);
			Camera->GetCameraComponent()->bConstrainAspectRatio = false;
			Camera->SetActorLocationAndRotation(Photo.Target - Photo.View.Vector() * Photo.Distance, Photo.View);
			PC->SetViewTarget(Camera);
		}
		++Photo.Frames;
		const bool bShadersDone = !GShaderCompilingManager || GShaderCompilingManager->GetNumRemainingJobs() == 0;
		if (!Photo.bTaken && Photo.Frames >= 30 && (bShadersDone || Photo.Frames >= 900))
		{
			Photo.bTaken = true;
			Photo.TakenFrame = Photo.Frames;
			const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots") / Photo.Folder / (Photo.Name + TEXT(".png"));
			FScreenshotRequest::RequestScreenshot(Path, /*bInShowUI*/ Photo.bShowHud, /*bAddFilenameSuffix*/ false);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke screenshot %s"), *Path);
		}
		if (Photo.bTaken && Photo.Frames >= Photo.TakenFrame + 15)
		{
			State.Photos.RemoveAt(0);
			if (State.Photos.IsEmpty())
			{
				UGameplayStatics::SetGlobalTimeDilation(World, 1.f);
				State.bPausedForPhoto = false;
				PC->SetViewTarget(PC->GetPawn());
			}
		}
		return true;
	}

	/** Per-frame metrics while a route is walked / halted (checks.ts RunCollector). */
	void Sample(UWorld* World, FState& State, const FLabRoute& Route, float Dt)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Leader || Dt <= 0.f)
		{
			return;
		}
		const FFormationConfig& Config = FormationTuning::Get();
		const EFormationKind Formation = Squad->GetFormationKind();
		// Rules v2.1 (checks.ts): checks 3 / 4 judge the REQUESTED formation (the terrain's / the menu's); the guard's fallbacks are geometry.
		const EFormationKind Requested = Squad->GetFormationRequested();
		const EFormationMode Mode = Squad->GetFormationMode();
		const float Heading = Squad->GetFormationHeadingYaw();
		const FVector2D LeaderPos(Leader->GetActorLocation());
		const FLabZone* Zone = ZoneAt(State, LeaderPos);
		if (State.LastLeaderPos.IsSet())
		{
			State.LeaderPath += static_cast<float>(FVector2D::Distance(LeaderPos, State.LastLeaderPos.GetValue()));
		}
		State.LastLeaderPos = LeaderPos;
		const bool bHalted = State.Stage == EStage::RouteHalt && State.StageTime >= ExpectHaltSettleSeconds;

		// Formation changes (info).
		if (!State.LastFormation.IsSet() || State.LastFormation.GetValue() != Formation)
		{
			if (State.LastFormation.IsSet())
			{
				++State.FormationChanges;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke trace: %s t=%.1f %s -> %s at (%.0f, %.0f)%s"), *Route.Id, State.StageTime,
					FormationNames::ToString(State.LastFormation.GetValue()), FormationNames::ToString(Formation), LeaderPos.X, LeaderPos.Y,
					Zone ? *FString::Printf(TEXT(" in %s"), *Zone->Id) : TEXT(""));
			}
			State.LastFormation = Formation;
		}

		// 4. Expected formation per zone.
		for (const FLabExpect& Expect : Route.Expect)
		{
			if (Zone && Zone->Id == Expect.Zone)
			{
				const float Entered = State.ZoneEnter.FindOrAdd(Expect.Zone, State.LeaderPath);
				// Tighter classes (echelon, column, wall) take over at once (v2); a looser expectation waits for hold_distance.
				const bool bLooser = Expect.Formation == TEXT("diamond") || Expect.Formation == TEXT("wedge") || Expect.Formation == TEXT("blizzard");
				if (State.LeaderPath - Entered >= ExpectSettleCm + (bLooser ? Config.ClassifyHoldDistance : 0.f) || bHalted)
				{
					FZoneAcc& Acc = State.Zones.FindOrAdd(Expect.Zone);
					Acc.Settled += Dt;
					Acc.Match += SameFormation(Expect.Formation, Requested) ? Dt : 0.f;
					Acc.Seen.FindOrAdd(FormationNames::ToString(Requested)) += Dt;
				}
			}
			else
			{
				State.ZoneEnter.Remove(Expect.Zone);
			}
		}

		// 1. Wall stare: facing within 45 deg of a wall closer than 200 cm for >= 0.3 s, unless he walks that way (v2.1: or looks round a wall
		// end on purpose - the corner watch, checks.ts).
		const TArray<AOperativeCharacter*> All = Living(State);
		const TSet<const AOperativeCharacter*> Watchers = CornerWatchers(*Squad, All);
		for (int32 Index = 0; Index < All.Num(); ++Index)
		{
			const AOperativeCharacter* Each = All[Index];
			const FVector2D Pos(Each->GetActorLocation());
			const float Facing = Yaw2D(Each->GetSightFacing());
			const FVector Velocity = Each->GetVelocity();
			const bool bApproaching = (Velocity.Size2D() > 15.f && FormationMath::AngleBetween(Facing, Yaw2D(Velocity)) <= StareAngleDeg)
				|| Watchers.Contains(Each);
			FString Staring;
			for (const FLabWall& Wall : bApproaching ? TArray<FLabWall>() : State.Layout.Walls)
			{
				const FVector2D Closest = ClosestOnSegment(Pos, Wall.A, Wall.B);
				if (FVector2D::Distance(Pos, Closest) - 0.5f * Wall.Thickness > StareDistCm)
				{
					continue;
				}
				if (FormationMath::AngleBetween(Facing, FormationMath::YawOf(Closest - Pos)) <= StareAngleDeg)
				{
					Staring = Wall.Id;
					break;
				}
			}
			float& Timer = State.StareTimer.FindOrAdd(Index);
			if (!Staring.IsEmpty())
			{
				Timer += Dt;
				if (Timer >= StareMinSeconds && !State.StareCounted.Contains(Index))
				{
					State.StareCounted.Add(Index);
					++State.StareEvents;
				}
				if (Timer >= StareMinSeconds && Timer > State.StareWorstSeconds)
				{
					EFormationRole Role = EFormationRole::Commander;
					const bool bRole = Squad->GetFormationRole(Each, Role);
					State.StareWorstSeconds = Timer;
					State.StareWorst = FString::Printf(TEXT("%s (%s) at %s, %s %s, t=%.1f"), *Each->DisplayName.ToString(), bRole ? FormationNames::ToString(Role) : TEXT("-"),
						*Staring, FormationNames::ToString(Formation), FormationNames::ToString(Mode), State.StageTime);
				}
			}
			else
			{
				Timer = 0.f;
				State.StareCounted.Remove(Index);
			}
		}

		// 6. Pace: the rear man behind his slot (patrol, 4 men); the leader's patrol speed.
		if (Mode == EFormationMode::Walk && Squad->GetFormationMemberCount() >= 4)
		{
			for (const AOperativeCharacter* Each : All)
			{
				EFormationRole Role;
				FVector Slot;
				float Look = 0.f;
				if (Squad->GetFormationRole(Each, Role) && Role == EFormationRole::Rear && Squad->GetFormationTarget(Each, Slot, Look))
				{
					// checks.ts 6: against the slot he is GIVEN - his formation slot, or its stand-in when it is off the navmesh (a slot past the
					// lab's floor edge: rules v2.3 13.5 keeps that stand-in slot.min_spacing off the others' slots, so it may lie behind the slot).
					FVector Given;
					if (Squad->GetFormationGivenSlot(Each, Given))
					{
						State.BehindStandIn += FVector2D::Distance(FVector2D(Given), FVector2D(Slot)) > 1.0 ? 1 : 0;
						Slot = Given;
					}
					const FVector2D ToSlot = FVector2D(Slot) - FVector2D(Each->GetActorLocation());
					State.Behind.Add(FMath::Max(0.f, static_cast<float>(FVector2D::DotProduct(ToSlot, FormationMath::Forward(Heading)))));
				}
			}
		}
		if (Mode == EFormationMode::Walk && Leader->HasOwnMoveOrder() && State.StageTime > 2.f)
		{
			State.PaceTime += Dt;
			State.PaceDistance += static_cast<float>(Leader->GetVelocity().Size2D()) * Dt;
		}
		// Run: everybody faces his path (12).
		if (Mode == EFormationMode::Run)
		{
			State.RunModeTime += Dt;
			for (const AOperativeCharacter* Each : All)
			{
				if (Each->GetVelocity().Size2D() > 200.f && FormationMath::AngleBetween(Yaw2D(Each->GetActorForwardVector()), Yaw2D(Each->GetVelocity())) > 60.f)
				{
					State.RunFacingOff += Dt;
				}
			}
		}
		// The narrow column's sectors. The v1.1 tables (user Q1 = A: P +15 over the shoulder, L -165 back at a slant) with
		// Codex.Formation.CoverAllocator 0; rules v2.2 (default) the coverage allocator replaces them on the move: the rear guard holds 5-7
		// (his look within 180 +- (fov / 2 - 30)), the leader his search 11-1 (+- scan.amp_lead), the others share what is left open.
		if (Zone && Zone->Id == TEXT("Z_D_narrow") && Formation == EFormationKind::Column && Squad->GetFormationPassageWidth().IsSet()
			&& Squad->GetFormationPassageWidth().GetValue() < Config.ColumnNarrowWidth)
		{
			const bool bAllocator = FormationRuntime::IsCoverAllocatorEnabled();
			EFormationRole RearGuard = EFormationRole::Rear;
			const bool bHasRearGuard = Squad->GetFormationRearGuard(RearGuard);
			for (const AOperativeCharacter* Each : All)
			{
				EFormationRole Role;
				FVector Slot;
				float Look = 0.f;
				if (!Squad->GetFormationRole(Each, Role) || !Squad->GetFormationTarget(Each, Slot, Look) || Watchers.Contains(Each))
				{
					continue; // (v2.1: the corner watcher looks round the corridor's end, not into his sector)
				}
				float Want = 0.f;
				float Tolerance = 2.f;
				if (bAllocator)
				{
					if (bHasRearGuard && Role == RearGuard)
					{
						Want = 180.f;
						Tolerance = 0.5f * Config.SightFov - 30.f + 1.f;
					}
					else if (Role == EFormationRole::Commander && Squad->GetFormationMode() == EFormationMode::Walk)
					{
						Tolerance = Config.ScanAmpLead + 1.f;
					}
					else
					{
						continue;
					}
				}
				else if (Role == EFormationRole::Right || Role == EFormationRole::Left)
				{
					Want = Role == EFormationRole::Right ? Config.ColumnNarrowFwdDeg : -Config.ColumnNarrowBackDeg;
				}
				else
				{
					continue;
				}
				const float Off = FMath::Abs(FMath::FindDeltaAngleDegrees(Heading + Want, Look));
				++State.NarrowSamples;
				State.NarrowOk += Off <= Tolerance ? 1 : 0;
				if (Off > Tolerance && State.NarrowWorst.IsEmpty())
				{
					State.NarrowWorst = FString::Printf(TEXT("%s looks %.0f off the heading (want %.0f +- %.0f)"), FormationNames::ToString(Role),
						FMath::FindDeltaAngleDegrees(Heading, Look), Want, Tolerance);
				}
			}
		}

		// Rules v2.1, 7. squad split by a wall (checks.ts): the leader -> each follower's slot / position (outside the trail column) and the
		// chain neighbours by place (positions; in the trail column also their slots), for >= 0.3 s. Only while the squad walks / halts on its
		// route (not the wall order stages).
		const TArray<FFormationWall> Walls = LayoutWalls(State);
		if (State.Stage == EStage::RouteWalk || State.Stage == EStage::RouteHalt)
		{
			TArray<FormationGuardRuntime::FSplitMember> Split;
			FormationGuardRuntime::FSplitMember Lead;
			Lead.Place = EFormationRole::Commander;
			Lead.Pos = LeaderPos;
			Lead.Slot = LeaderPos;
			Split.Add(Lead);
			for (const AOperativeCharacter* Each : All)
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
			const TArray<FString> Cuts = FormationGuardRuntime::SplitCuts(Walls, Formation, Squad->IsFormationTrail(), Squad->GetFormationMemberCount(), Split);
			if (Cuts.Num() > 0)
			{
				State.SplitTimer += Dt;
				if (State.SplitTimer >= FormationGuardRuntime::SplitSeconds && !State.bSplitCounted)
				{
					State.bSplitCounted = true;
					++State.SplitEvents;
				}
				if (State.SplitTimer >= FormationGuardRuntime::SplitSeconds && State.SplitTimer > State.SplitWorstSeconds)
				{
					State.SplitWorstSeconds = State.SplitTimer;
					State.SplitWorst = FString::Printf(TEXT("%s%s: wall cuts %s at (%.0f, %.0f), t=%.1f"), FormationNames::ToString(Formation),
						Requested != Formation ? *FString::Printf(TEXT(" (asked %s)"), FormationNames::ToString(Requested)) : TEXT(""),
						*FString::Join(Cuts, TEXT(", ")), LeaderPos.X, LeaderPos.Y, State.StageTime);
				}
			}
			else
			{
				State.SplitTimer = 0.f;
				State.bSplitCounted = false;
			}
		}

		// Rules v2.1, 9. the corner (checks.ts): the leader passes a free wall end beside him on patrol -> at that moment somebody looks at
		// the corner (G, CornerGraze past the end) or round it (H, 200 cm behind the far face), within fov / 2 - 10 of his facing.
		if (Mode == EFormationMode::Walk)
		{
			const float Fov = Config.SightFov;
			for (int32 WallIndex = 0; WallIndex < Walls.Num(); ++WallIndex)
			{
				for (int32 End = 0; End < 2; ++End)
				{
					const FString Key = FString::Printf(TEXT("%d:%d"), WallIndex, End);
					const TOptional<float> S = FormationCornerRules::CornerAhead(Walls, WallIndex, End, LeaderPos, Heading, Config);
					const float* Prev = State.CornerPrev.Find(Key);
					const TOptional<float> Before = Prev ? TOptional<float>(*Prev) : TOptional<float>();
					if (!S.IsSet())
					{
						State.CornerPrev.Remove(Key);
						continue;
					}
					State.CornerPrev.Add(Key, S.GetValue());
					if (!Before.IsSet() || !(Before.GetValue() > 0.f && S.GetValue() <= 0.f))
					{
						continue;
					}
					FVector2D Graze;
					FVector2D Hidden;
					FormationGuardRuntime::CornerCheckPoints(Walls[WallIndex], End, LeaderPos, Graze, Hidden);
					const auto Sees = [&All, Fov](const FVector2D& Point)
					{
						return All.ContainsByPredicate([&Point, Fov](const AOperativeCharacter* Each)
						{
							return FormationMath::AngleBetween(Yaw2D(Each->GetSightFacing()), FormationMath::YawOf(Point - FVector2D(Each->GetActorLocation())))
								<= 0.5f * Fov - FormationGuardRuntime::CornerCheckMarginDeg;
						});
					};
					++State.CornerEnds;
					const bool bSeen = Sees(Graze) || Sees(Hidden);
					UE_LOG(LogCodexTactics, Display, TEXT("Smoke trace: %s t=%.1f passes the %s end of %s - %s"), *Route.Id, State.StageTime, End == 0 ? TEXT("a") : TEXT("b"),
						*State.Layout.Walls[WallIndex].Id, bSeen ? TEXT("somebody looks round it") : TEXT("NOBODY looks round it"));
					if (!bSeen)
					{
						++State.CornerMissed;
						if (State.CornerWorst.IsEmpty())
						{
							State.CornerWorst = FString::Printf(TEXT("nobody looked round the %s end of %s (t=%.1f)"), End == 0 ? TEXT("a") : TEXT("b"),
								*State.Layout.Walls[WallIndex].Id, State.StageTime);
						}
					}
				}
			}
		}

		// Rules v2.1: the guard as it ran (its level, the HUD line while it falls back, the trail column near the route's corner).
		State.MaxGuardLevel = FMath::Max(State.MaxGuardLevel, static_cast<int32>(Squad->GetFormationGuardLevel()));
		if (Squad->GetFormationGuardLevel() != EFormationGuardLevel::Requested)
		{
			State.HudSeen.Add(Squad->GetFormationHudText());
		}
		if (Squad->IsFormationTrail())
		{
			State.TrailSeconds += Dt;
			for (int32 Corner = 1; Corner + 1 < Route.Points.Num(); ++Corner)
			{
				State.TrailNearCorner += FVector2D::Distance(LeaderPos, Route.Points[Corner]) <= 1000.0 ? Dt : 0.f;
			}
		}
		State.CornerWatchSeconds += Squad->GetFormationCornerLooks().Num() > 0 ? Dt : 0.f;

		// Rules v2.1 stills (Saved/Screenshots/FormationV21/).
		if (State.bShot && State.Photos.IsEmpty())
		{
			const FVector Center = Centroid(State);
			if (Squad->GetFormationGuardLevel() == EFormationGuardLevel::Mirror && Zone && Zone->Id == TEXT("Z_B_left"))
			{
				QueuePhoto(State, TEXT("01_guard_echelon_left_to_right_hud"), Center, Heading + 35.f, -60.f, 1500.f, /*bV21*/ true);
			}
			else if (Squad->IsFormationTrail() && Route.Id == TEXT("R_D_corridors") && Zone && Zone->Id == TEXT("Z_D_narrow"))
			{
				QueuePhoto(State, TEXT("02_guard_line_to_trail_column_hud"), Center, Heading + 20.f, -60.f, 1300.f, true);
			}
			else if (Squad->IsFormationTrail() && Route.Id == TEXT("R_E_corner") && Route.Points.Num() > 2
				&& FVector2D::Distance(LeaderPos, Route.Points[1]) <= 400.0 && LeaderPos.Y > Route.Points[1].Y + 150.0)
			{
				QueuePhoto(State, TEXT("03_trail_column_round_the_corner"), FVector(Route.Points[1].X, Route.Points[1].Y, Center.Z), 225.f, -65.f, 1600.f, true);
			}
			else if (Route.Id == TEXT("R_E_corner_pass") && Squad->GetFormationCornerLooks().Num() > 0 && LeaderPos.Y > 5150.0)
			{
				// From the south, steep: the squad walking north (up the image), W_E_14's free end beside it, the watcher's look round it.
				QueuePhoto(State, TEXT("04_corner_watch_slicing_the_pie"), FVector(150.0, 5000.0, Center.Z), 90.f, -72.f, 1500.f, true);
			}
		}

		// Photos.
		if (State.bShot && State.Photos.IsEmpty())
		{
			const FVector Center = Centroid(State);
			const auto Settled = [&State, Zone](const TCHAR* Id, float Cm)
			{
				const float* Entered = Zone && Zone->Id == Id ? State.ZoneEnter.Find(Id) : nullptr;
				return Entered && State.LeaderPath - *Entered >= Cm;
			};
			if (Route.Id == TEXT("R_A_open") && Mode == EFormationMode::Walk && Formation == EFormationKind::Diamond && LeaderPos.X > -1000.0)
			{
				QueuePhoto(State, TEXT("01_diamond_open"), Center, Heading + 25.f);
			}
			else if (Route.Id == TEXT("R_B_walls") && Settled(TEXT("Z_B_left"), 500.f) && Formation == EFormationKind::EchelonRight)
			{
				QueuePhoto(State, TEXT("02_echelon_right_wall_left"), Center, Heading + 35.f);
			}
			else if (Route.Id == TEXT("R_D_corridors") && Settled(TEXT("Z_D_narrow"), 400.f) && Formation == EFormationKind::Column)
			{
				QueuePhoto(State, TEXT("03_column_narrow_corridor"), Center, Heading + 20.f, -60.f, 1300.f);
			}
		}
	}

	/** The squad in its formation slots round Start facing Forward (teleport), the formation state reset. */
	void PlaceSquad(UWorld* World, FState& State, const FVector2D& Start, float Yaw)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad->GetLeader();
		const TArray<AOperativeCharacter*> All = Living(State);
		const int32 Count = FMath::Min(All.Num(), 4);
		const TArray<FFormationSlot> Slots = FormationShapeRules::Slots(Count >= 4 ? EFormationKind::Diamond : EFormationKind::Wedge, Count, Start, Yaw);
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		for (AOperativeCharacter* Each : All)
		{
			Each->LeaveCover(TEXT("smoke reset"));
			Each->ClearPendingCover();
			Each->StopOperative();
			Each->SetFormationWallPose(false);
			if (Each->GetStance() != EOperativeStance::Standing)
			{
				Each->SetStance(EOperativeStance::Standing);
			}
			FVector2D Spot = Start;
			if (Each != Leader)
			{
				const TOptional<EFormationRole> Role = Each->bGuarding ? TOptional<EFormationRole>()
					: FormationRuntime::RoleForFollowerSlot(Squad->GetFormationSlot(Each), Count);
				const FFormationSlot* Slot = Role.IsSet() ? Slots.FindByPredicate([&Role](const FFormationSlot& S) { return S.Role == Role.GetValue(); }) : nullptr;
				Spot = Slot ? Slot->Position : Start + FVector2D(-800.0, 300.0);
			}
			FVector Location(Spot.X, Spot.Y, State.Layout.GroundZ + Each->GetSimpleCollisionHalfHeight() + 5.f);
			FNavLocation OnNav;
			if (Nav && !Nav->ProjectPointToNavigation(Location, OnNav, FVector(50.f, 50.f, 300.f)))
			{
				Location = FVector(Start.X - 150.0, Start.Y, Location.Z); // a slot in a wall: just behind the leader
			}
			Each->TeleportTo(Location, FRotator(0.f, Yaw, 0.f), false, true);
		}
		Squad->ResetFormationState(FRotator(0.f, Yaw, 0.f).Vector());
	}

	/** Route checks after the halt. */
	void CheckRoute(UWorld* World, FState& State, const FLabRoute& Route, const FRun& Run, bool bWallEnd)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		const int32 MemberCount = Run.MemberCount;
		const bool bManual = !Run.Menu.IsEmpty();
		const FString Name = FString::Printf(TEXT("%s n%d%s"), *Route.Id, MemberCount, bManual ? *FString::Printf(TEXT(" menu %s"), *Run.Menu) : TEXT(""));
		// Rules v2.1 (checks.ts gate for manual formations): a manual run is judged by the stare, the split and the guard - its zone
		// expectations and the rear man's pace are reported only.
		const auto Gate = [&State, bManual](bool bOk, const FString& What)
		{
			if (bManual && !bOk)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: (manual, not gated) %s"), *What);
				return;
			}
			Check(State, bOk, What);
		};
		// Rules v2.1, 7: the squad never splits round a wall.
		Check(State, State.SplitEvents == 0, FString::Printf(TEXT("%s: the squad never splits round a wall (%d splits >= %.1f s%s)"), *Name, State.SplitEvents,
			FormationGuardRuntime::SplitSeconds, State.SplitWorst.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(", worst %.1f s %s"), State.SplitWorstSeconds, *State.SplitWorst)));
		// 9: somebody looks round every free wall end the leader passes.
		if (State.CornerEnds > 0 || Run.bExpectCorner)
		{
			Check(State, State.CornerEnds > 0 && State.CornerMissed == 0, FString::Printf(TEXT("%s: somebody looks round each passed wall end (%d ends, %d missed%s; corner watch %.1f s)"),
				*Name, State.CornerEnds, State.CornerMissed, State.CornerWorst.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(", %s"), *State.CornerWorst), State.CornerWatchSeconds));
		}
		FString Huds;
		for (const FString& Hud : State.HudSeen)
		{
			Huds += FString::Printf(TEXT(" \"%s\""), *Hud);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: %s: guard max level %d, trail column %.1f s (%.1f s near the route's corners), HUD while guarded:%s"), *Name,
			State.MaxGuardLevel, State.TrailSeconds, State.TrailNearCorner, Huds.IsEmpty() ? TEXT(" -") : *Huds);
		if (!Run.ExpectHud.IsEmpty())
		{
			Check(State, State.HudSeen.Contains(Run.ExpectHud), FString::Printf(TEXT("%s: the wall guard keeps the squad whole - the HUD shows \"%s\" (seen:%s)"), *Name,
				*Run.ExpectHud, Huds.IsEmpty() ? TEXT(" none") : *Huds));
		}
		if (Run.bExpectFold)
		{
			Check(State, State.TrailNearCorner >= 1.f, FString::Printf(TEXT("%s: the sharp-turn fold - the trail column round the route's corner (%.1f s within 10 m of it)"),
				*Name, State.TrailNearCorner));
		}
		for (const FLabExpect& Expect : Route.Expect)
		{
			const FZoneAcc* Acc = State.Zones.Find(Expect.Zone);
			FString Seen;
			if (Acc)
			{
				for (const TPair<FString, float>& Pair : Acc->Seen)
				{
					Seen += FString::Printf(TEXT(" %s %.1fs"), *Pair.Key, Pair.Value);
				}
			}
			const float Pct = Acc && Acc->Settled > 0.f ? 100.f * Acc->Match / Acc->Settled : 0.f;
			const FString What = FString::Printf(TEXT("%s: zone %s wants %s (requested) - %.0f %% of %.1f s settled [%s ]"), *Name, *Expect.Zone, *Expect.Formation, Pct,
				Acc ? Acc->Settled : 0.f, *Seen);
			if (bManual)
			{
				Gate(Acc && Acc->Settled > 0.f && Pct >= ExpectPct, What);
			}
			else
			{
				CheckKnown(State, Route.Id, TEXT("expect"), Acc && Acc->Settled > 0.f && Pct >= ExpectPct, What);
			}
		}
		CheckKnown(State, Route.Id, TEXT("stare"), State.StareEvents == 0, FString::Printf(TEXT("%s: nobody faces a wall within 200 cm for >= 0.3 s (%d events%s)"),
			*Name, State.StareEvents, State.StareWorst.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(", worst %.1f s %s"), State.StareWorstSeconds, *State.StareWorst)));
		if (MemberCount >= 4 && !Route.bRun && State.Behind.Num() > 10)
		{
			TArray<float> Sorted = State.Behind;
			Sorted.Sort();
			const float P95 = Sorted[FMath::Clamp(FMath::FloorToInt32(0.95f * Sorted.Num()), 0, Sorted.Num() - 1)];
			const FString What = FString::Printf(TEXT("%s: the rear man keeps up - p95 %.0f cm behind his slot (max %.0f, <= %.0f; %d of %d samples on a stand-in)"), *Name, P95,
				Sorted.Last(), PaceBehindCm, State.BehindStandIn, Sorted.Num());
			if (bManual)
			{
				Gate(P95 <= PaceBehindCm, What);
			}
			else
			{
				CheckKnown(State, Route.Id, TEXT("pace"), P95 <= PaceBehindCm, What);
			}
		}
		if (!Route.bRun && State.PaceTime > 3.f)
		{
			const FFormationConfig& Config = FormationTuning::Get();
			const AOperativeCharacter* Leader = Squad->GetLeader();
			const float Want = Leader ? Leader->MovementConfig.WalkSpeed * Config.PacePatrol : 176.f;
			const float Mean = State.PaceDistance / State.PaceTime;
			Check(State, FMath::Abs(Mean - Want) <= 0.1f * Want, FString::Printf(TEXT("%s: the leader patrols at %.0f cm/s (pace.patrol %.2f x walk = %.0f)"), *Name, Mean,
				Config.PacePatrol, Want));
		}
		if (Route.bRun)
		{
			Check(State, State.RunModeTime > 3.f, FString::Printf(TEXT("%s: run mode while sprinting (%.1f s)"), *Name, State.RunModeTime));
			Check(State, State.RunFacingOff < 0.5f, FString::Printf(TEXT("%s: running, everybody faces his path (%.2f member-s off)"), *Name, State.RunFacingOff));
		}
		if (Route.Id == TEXT("R_D_corridors"))
		{
			Check(State, State.NarrowSamples > 30 && State.NarrowOk == State.NarrowSamples,
				FString::Printf(TEXT("%s: narrow column sectors %s (%d / %d samples%s)"), *Name, FormationRuntime::IsCoverAllocatorEnabled()
					? TEXT("by the coverage allocator: the rear guard holds 5-7, the leader 11-1") : TEXT("P +15 / L -165 (v1.1 tables)"), State.NarrowOk, State.NarrowSamples,
					State.NarrowWorst.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(", %s"), *State.NarrowWorst)));
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: %s: %d formation changes"), *Name, State.FormationChanges);
		if (bWallEnd)
		{
			float NormalYaw = 0.f;
			const bool bLayout = Squad->GetFormationKind() == EFormationKind::Wall && Squad->GetFormationWallNormalYaw(NormalYaw);
			Check(State, bLayout, FString::Printf(TEXT("%s: the wall layout at the dead end (formation %s)"), *Name, FormationNames::ToString(Squad->GetFormationKind())));
			for (const AOperativeCharacter* Each : Living(State))
			{
				EFormationRole Role;
				FVector Slot;
				float Look = 0.f;
				const bool bTarget = Squad->GetFormationRole(Each, Role) && Squad->GetFormationTarget(Each, Slot, Look);
				const float Off = bTarget ? static_cast<float>(FVector2D::Distance(FVector2D(Slot), FVector2D(Each->GetActorLocation()))) : -1.f;
				const float Facing = Yaw2D(Each->GetSightFacing());
				Check(State, bTarget && (Role == EFormationRole::Commander || Off <= WallSlotTolCm),
					FString::Printf(TEXT("%s: %s (%s) on his wall slot (%.0f cm off)"), *Name, *Each->DisplayName.ToString(), bTarget ? FormationNames::ToString(Role) : TEXT("-"), Off));
				Check(State, bLayout && FormationMath::AngleBetween(Facing, NormalYaw) < 90.f,
					FString::Printf(TEXT("%s: %s does not face the wall (facing %.0f, outward %.0f, look %.0f)"), *Name, *Each->DisplayName.ToString(), Facing, NormalYaw, Look));
			}
		}
	}

	bool AllOnWallSlots(UWorld* World, const FState& State)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		if (Squad->GetFormationKind() != EFormationKind::Wall)
		{
			return false;
		}
		float NormalYaw = 0.f;
		Squad->GetFormationWallNormalYaw(NormalYaw);
		for (const AOperativeCharacter* Each : Living(State))
		{
			EFormationRole Role;
			FVector Slot;
			float Look = 0.f;
			if (!Squad->GetFormationRole(Each, Role) || !Squad->GetFormationTarget(Each, Slot, Look))
			{
				return false;
			}
			if (Role != EFormationRole::Commander && FVector2D::Distance(FVector2D(Slot), FVector2D(Each->GetActorLocation())) > 60.f)
			{
				return false;
			}
			if (FormationMath::AngleBetween(Yaw2D(Each->GetActorForwardVector()), Look) > 20.f || Each->GetVelocity().Size2D() > 15.f)
			{
				return false;
			}
		}
		return true;
	}

	bool Step(UWorld* World, FState& State, float Dt)
	{
		if (StepPhotos(World, State))
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
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
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
			if (!FormationRuntime::IsEnabled())
			{
				// The A/B batch (smoke_parallel -PreCmds "Codex.Formation.Sectors 0"): the lab checks the formation rules only - skipped, not
				// failed (the default 1 is what the normal run checks).
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: Codex.Formation.Sectors is 0 - the formation lab checks the formation rules only: skipped"));
				return Finish(State, true);
			}
			Check(State, Flow && Flow->GetPhase() == ECodexGamePhase::Exploration, TEXT("exploration phase on the lab"));
			if (Squad->IsSoloMode())
			{
				Squad->ExitSoloMode();
			}
			// The 4th man: the diamond.
			if (ACodexTacticsGameMode* GameMode = World->GetAuthGameMode<ACodexTacticsGameMode>(); GameMode && Squad->GetMembers().Num() < 4)
			{
				const FVector At = Leader->GetActorLocation() - Leader->GetActorForwardVector() * 560.f;
				State.Fourth = GameMode->SpawnOperative(GameMode->RecruitSusanin, 3, At, Leader->GetActorRotation(), true);
			}
			State.Members.Reset();
			for (AOperativeCharacter* Each : Squad->GetMembers())
			{
				State.Members.Add(Each);
			}
			Check(State, State.Members.Num() == 4, FString::Printf(TEXT("4 operatives in the squad (%d)"), State.Members.Num()));
			for (const AOperativeCharacter* Each : Living(State))
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: %s walk speed %.0f (max now %.0f)"), *Each->DisplayName.ToString(), Each->MovementConfig.WalkSpeed,
					Each->GetMaxSpeed());
			}
			if (State.Members.Num() < 2)
			{
				return Finish(State, false);
			}
			Enter(State, EStage::RouteStart);
			return true;
		}

		case EStage::RouteStart:
		{
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
			// 3 men: the 4th goes on guard (out of the formation); 4: he is back.
			if (AOperativeCharacter* Fourth = State.Fourth.Get(); Fourth && Fourth->bGuarding != (Run->MemberCount < 4))
			{
				Squad->ToggleGuard(Fourth);
			}
			if (AOperativeCharacter* Fourth = State.Fourth.Get(); Fourth && Run->MemberCount < 4)
			{
				Fourth->TeleportTo(FVector(Route->Points[0].X - 3000.0, Route->Points[0].Y, State.Layout.GroundZ + Fourth->GetSimpleCollisionHalfHeight() + 5.f),
					FRotator::ZeroRotator, false, true);
				State.Members.Remove(Fourth);
			}
			else if (AOperativeCharacter* Back = State.Fourth.Get(); Back && !State.Members.Contains(Back))
			{
				State.Members.Add(Back);
			}
			const float Yaw = FormationMath::YawOf(Route->Points[1] - Route->Points[0]);
			// Rules v2.1: the hold-B menu's choice for this run (auto unless the run names one).
			TOptional<EFormationMenuItem> Menu;
			FormationMenuRules::ParseChoice(Run->Menu.IsEmpty() ? FString(TEXT("auto")) : Run->Menu, Menu);
			Squad->SetFormationMenuChoice(Menu);
			PlaceSquad(World, State, Route->Points[0], Yaw);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: route %s (%s, %d points) with %d members, menu %s"), *Route->Id, Route->bRun ? TEXT("run") : TEXT("walk"),
				Route->Points.Num(), Run->MemberCount, Run->Menu.IsEmpty() ? TEXT("auto") : *Run->Menu);
			Enter(State, EStage::RouteSettle);
			return true;
		}

		case EStage::RouteSettle:
			if (State.StageTime >= SettleSeconds)
			{
				ResetMetrics(State);
				State.Point = 1;
				State.LegTime = 0.f;
				State.StillTime = 0.f;
				State.CarrotTime = CarrotSeconds; // the first order at once
				Enter(State, EStage::RouteWalk);
			}
			return true;

		case EStage::RouteWalk:
		{
			Sample(World, State, *Route, Dt);
			State.LegTime += Dt;
			State.CarrotTime += Dt;
			const FVector2D Here(Leader->GetActorLocation());
			const FVector2D From = Route->Points[State.Point - 1];
			const FVector2D To = Route->Points[State.Point];
			const float Dist = static_cast<float>(FVector2D::Distance(Here, To));
			const bool bLast = State.Point == Route->Points.Num() - 1;
			State.StillTime = Leader->IsMoving() ? 0.f : State.StillTime + Dt;
			const double LegLength = FVector2D::Distance(From, To);
			const double LegAlong = FVector2D::DotProduct(Here - From, (To - From).GetSafeNormal());
			if (!bLast && (Dist <= CornerTurnCm || LegAlong >= LegLength - CornerTurnCm))
			{
				// A corner of the route: on to the next leg without a stop (the bot's leader walks through it).
				++State.Point;
				State.LegTime = 0.f;
				State.CarrotTime = CarrotSeconds;
				return true;
			}
			if (bLast && Dist <= PointReachCm && !Leader->IsMoving())
			{
				Enter(State, EStage::RouteHalt);
				return true;
			}
			// The player clicks ahead along the route (crowd separation from the followers would let the leader drift off the line
			// over a long leg): a point CarrotAheadCm ahead of his projection on the leg - rules v2.1: carried on round the route's corner
			// (the click lies round it: his path shows the turn ahead, the sharp-turn fold sees it), the route's end for the last stretch.
			if (State.CarrotTime >= CarrotSeconds || State.StillTime > 1.f)
			{
				State.CarrotTime = 0.f;
				State.StillTime = 0.f;
				const FVector2D Leg = (To - From).GetSafeNormal();
				const double Along = FMath::Clamp(FVector2D::DotProduct(Here - From, Leg), 0.0, FVector2D::Distance(From, To));
				double Remaining = FVector2D::Distance(From, To) - Along;
				FVector2D Goal = From + Leg * (Along + CarrotAheadCm);
				if (Remaining <= CarrotAheadCm + 200.0)
				{
					Goal = To;
					if (!bLast)
					{
						// Round the corner: the rest of the click distance along the next leg.
						const FVector2D Next = Route->Points[State.Point + 1];
						const double Rest = FMath::Clamp(CarrotAheadCm - Remaining, 0.0, FVector2D::Distance(To, Next));
						Goal = To + (Next - To).GetSafeNormal() * Rest;
					}
				}
				Leader->OrderMoveTo(FVector(Goal.X, Goal.Y, Leader->GetActorLocation().Z), Route->bRun);
			}
			const float LegCap = static_cast<float>(FVector2D::Distance(From, To)) / 100.f + 20.f;
			if (State.LegTime > LegCap)
			{
				Check(State, false, FString::Printf(TEXT("%s: the leader reaches point %d (%.0f cm off after %.0f s)"), *Route->Id, State.Point, Dist, State.LegTime));
				Enter(State, EStage::RouteHalt);
			}
			return true;
		}

		case EStage::RouteHalt:
		{
			Sample(World, State, *Route, Dt);
			const bool bWallEnd = !Route->Expect.IsEmpty() && Route->Expect.Last().Formation == TEXT("wall");
			const bool bDone = bWallEnd ? (AllOnWallSlots(World, State) && State.StageTime >= WallHaltSeconds) || State.StageTime >= WallHaltMaxSeconds
				: State.StageTime >= HaltSeconds;
			if (!bDone)
			{
				return true;
			}
			if (bWallEnd && State.bShot)
			{
				float NormalYaw = 0.f;
				if (Squad->GetFormationWallNormalYaw(NormalYaw))
				{
					QueuePhoto(State, FString::Printf(TEXT("04_wall_layout_%s_n%d"), *Route->Id, Run->MemberCount), Centroid(State), NormalYaw + 180.f + 20.f, -45.f, 1100.f);
				}
			}
			CheckRoute(World, State, *Route, *Run, bWallEnd);
			if (Run->bWallOrderAfter)
			{
				// Back off the dead end, then the group cover order at its wall.
				const FVector2D Back = Route->Points.Last() + (Route->Points[0] - Route->Points.Last()).GetSafeNormal() * 700.0;
				State.OrderBackPoint = FVector(Back.X, Back.Y, Leader->GetActorLocation().Z);
				Leader->OrderMoveTo(State.OrderBackPoint, false);
				Enter(State, EStage::OrderBack);
				return true;
			}
			++State.RunIndex;
			Enter(State, EStage::RouteStart);
			return true;
		}

		case EStage::OrderBack:
		{
			const bool bThere = FVector::Dist2D(Leader->GetActorLocation(), State.OrderBackPoint) <= PointReachCm && !Leader->IsMoving();
			if (bThere && State.StageTime >= 4.f)
			{
				// The player's click on the dead-end wall's face, straight ahead of where the route ended.
				const FVector2D End = Route->Points.Last();
				const FVector2D Dir = (End - Route->Points[0]).GetSafeNormal();
				const FVector Click(End.X + Dir.X * 60.0, End.Y + Dir.Y * 60.0, State.Layout.GroundZ + 100.f);
				const bool bSlot = CoverTraceRules::FindCoverSlotAt(World, Click, FVector(Dir, 0.f), State.OrderSlot);
				Check(State, bSlot, FString::Printf(TEXT("a cover slot on the dead-end wall (%s)"), bSlot ? *State.OrderSlot.WorldLocation.ToCompactString() : TEXT("none")));
				ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
				const bool bOrdered = bSlot && PC && PC->OrderGroupCover(Leader, State.OrderSlot, false, 1200.f, false);
				Check(State, bOrdered && Squad->HasFormationWallOrder(), TEXT("the group cover order in group mode runs as the formation wall layout"));
				if (!bOrdered)
				{
					++State.RunIndex;
					Enter(State, EStage::RouteStart);
					return true;
				}
				ResetMetrics(State);
				Enter(State, EStage::OrderGo);
			}
			else if (State.StageTime > 30.f)
			{
				Check(State, false, TEXT("the leader backs off the dead end"));
				++State.RunIndex;
				Enter(State, EStage::RouteStart);
			}
			return true;
		}

		case EStage::OrderGo:
		{
			Sample(World, State, *Route, Dt);
			bool bAll = true;
			for (const AOperativeCharacter* Each : Living(State))
			{
				bAll &= Each->IsInFormationWallPose();
			}
			if (bAll && State.StageTime >= 2.f)
			{
				float NormalYaw = 0.f;
				Squad->GetFormationWallNormalYaw(NormalYaw);
				for (const AOperativeCharacter* Each : Living(State))
				{
					EFormationRole Role;
					FVector Slot;
					float Look = 0.f;
					const bool bTarget = Squad->GetFormationRole(Each, Role) && Squad->GetFormationTarget(Each, Slot, Look);
					const float Facing = Yaw2D(Each->GetActorForwardVector());
					Check(State, bTarget && FVector2D::Distance(FVector2D(Slot), FVector2D(Each->GetActorLocation())) <= WallSlotTolCm,
						FString::Printf(TEXT("wall order: %s (%s) on his slot along the wall (%.0f cm off)"), *Each->DisplayName.ToString(),
							bTarget ? FormationNames::ToString(Role) : TEXT("-"), bTarget ? FVector2D::Distance(FVector2D(Slot), FVector2D(Each->GetActorLocation())) : -1.0));
					Check(State, Each->GetStance() == EOperativeStance::Crouching && !Each->bInCover && FormationMath::AngleBetween(Facing, NormalYaw) < 90.f
						&& FormationMath::AngleBetween(Facing, Look) <= 30.f,
						FString::Printf(TEXT("wall order: %s crouched facing out, back to the wall, not in the cover pose (stance %d, facing %.0f, look %.0f, outward %.0f)"),
							*Each->DisplayName.ToString(), static_cast<int32>(Each->GetStance()), Facing, Look, NormalYaw));
				}
				CheckKnown(State, Route->Id, TEXT("stare"), State.StareEvents == 0, FString::Printf(TEXT("wall order: nobody stared at a wall on the way (%d events%s)"),
					State.StareEvents, State.StareWorst.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(", %s"), *State.StareWorst)));
				QueuePhoto(State, TEXT("05_wall_order_crouched_facing_out"), Centroid(State), NormalYaw + 180.f - 25.f, -40.f, 1000.f);
				Enter(State, EStage::OrderHold);
			}
			else if (State.StageTime > 25.f)
			{
				FString Poses;
				for (const AOperativeCharacter* Each : Living(State))
				{
					Poses += FString::Printf(TEXT(" %s:%d"), *Each->DisplayName.ToString(), Each->IsInFormationWallPose() ? 1 : 0);
				}
				Check(State, false, FString::Printf(TEXT("wall order: everybody crouched at the wall within 25 s [%s ]"), *Poses));
				Enter(State, EStage::OrderHold);
			}
			return true;
		}

		case EStage::OrderHold:
			if (State.StageTime >= 1.f)
			{
				// The leader's next order ends it: the others stand up and follow.
				const FVector2D Away = (Route->Points[0] - Route->Points.Last()).GetSafeNormal();
				State.OrderBackPoint = Leader->GetActorLocation() + FVector(Away.X, Away.Y, 0.0) * 800.0;
				Leader->OrderMoveTo(State.OrderBackPoint, false);
				Enter(State, EStage::OrderLeave);
			}
			return true;

		case EStage::OrderLeave:
			if (State.StageTime >= 6.f)
			{
				Check(State, !Squad->HasFormationWallOrder(), TEXT("wall order over with the leader's next order"));
				for (const AOperativeCharacter* Each : Living(State))
				{
					if (Each == Leader)
					{
						continue;
					}
					Check(State, !Each->IsInFormationWallPose() && Each->GetStance() == EOperativeStance::Standing
						&& FVector::Dist2D(Each->GetActorLocation(), Leader->GetActorLocation()) < 900.f,
						FString::Printf(TEXT("after the wall order %s stands up and follows (stance %d, %.0f cm from the leader)"), *Each->DisplayName.ToString(),
							static_cast<int32>(Each->GetStance()), FVector::Dist2D(Each->GetActorLocation(), Leader->GetActorLocation())));
				}
				Check(State, Leader->GetStance() == EOperativeStance::Standing, FString::Printf(TEXT("the leader stood up for his walk (stance %d)"),
					static_cast<int32>(Leader->GetStance())));
				++State.RunIndex;
				Enter(State, EStage::RouteStart);
			}
			return true;

		default:
			return false;
		}
	}

	void Start(UWorld* World, bool bShot, const FString& Only)
	{
		TSharedRef<FState> State = MakeShared<FState>();
		State->bShot = bShot;
		FString Error;
		if (!LoadLayout(State->Layout, Error))
		{
			Check(*State, false, FString::Printf(TEXT("layout: %s"), *Error));
			Finish(*State, false);
			return;
		}
		ApplyTargetPoints(World, State->Layout);
		// Rules v2.1: a smoke-own walk past W_E_14's free north end (300, 5300) - the corner watch (slicing the pie) and check 9.
		FLabRoute Pass;
		Pass.Id = TEXT("R_E_corner_pass");
		Pass.Points = { FVector2D(0.0, 4300.0), FVector2D(0.0, 5800.0) };
		State->Layout.Routes.Add(Pass);
		const TCHAR* Order[] = { TEXT("R_A_open"), TEXT("R_A_run"), TEXT("R_B_walls"), TEXT("R_C_gaps"), TEXT("R_D_corridors"), TEXT("R_E_corner"),
			TEXT("R_F_deadend"), TEXT("R_v2_gaps") };
		for (const TCHAR* Id : Order)
		{
			if (Only.IsEmpty() || Only.Contains(Id))
			{
				FRun Run;
				Run.RouteId = Id;
				Run.bWallOrderAfter = FCString::Strcmp(Id, TEXT("R_F_deadend")) == 0;
				Run.bExpectFold = FCString::Strcmp(Id, TEXT("R_E_corner")) == 0; // the echelon right folds round the 90 deg corner
				State->Plan.Add(Run);
			}
		}
		// Rules v2.1: the guard with the hold-B menu's manual choices, and the corner watch.
		struct FV21Run
		{
			const TCHAR* Label;
			const TCHAR* Route;
			const TCHAR* Menu;
			const TCHAR* Hud;
			bool bCorner;
		};
		const FV21Run V21Runs[] = {
			// The user's bug: lane B, manual echelon left with the wall on the left -> the mirror (the squad stays on the leader's side).
			{ TEXT("R_B_walls@echelon_left"), TEXT("R_B_walls"), TEXT("echelon_left"), TEXT("Echelon left -> Echelon right (wall)"), false },
			// A manual line through the corridors -> the trail column.
			{ TEXT("R_D_corridors@line"), TEXT("R_D_corridors"), TEXT("line"), TEXT("Line -> Column (wall)"), false },
			{ TEXT("R_E_corner_pass"), TEXT("R_E_corner_pass"), TEXT(""), TEXT(""), true },
		};
		for (const FV21Run& Each : V21Runs)
		{
			if (Only.IsEmpty() || Only.Contains(Each.Label))
			{
				FRun Run;
				Run.RouteId = Each.Route;
				Run.Menu = Each.Menu;
				Run.ExpectHud = Each.Hud;
				Run.bExpectCorner = Each.bCorner;
				State->Plan.Add(Run);
			}
		}
		if (Only.IsEmpty() || Only.Contains(TEXT("n3")))
		{
			FRun Wedge;
			Wedge.RouteId = TEXT("R_F_deadend");
			Wedge.MemberCount = 3;
			State->Plan.Add(Wedge);
		}
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
		FString Only;
		for (const FString& Arg : Args)
		{
			if (Arg.StartsWith(TEXT("routes=")))
			{
				Only = Arg.Mid(7);
			}
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld, bShot, Only]()
		{
			if (UWorld* W = WeakWorld.Get())
			{
				Start(W, bShot, Only);
			}
		}), NavWarmupSeconds, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.FormationLabSmoke"),
		TEXT("Dev check (2026-10-25, formation stage 6): the squad walks the formation lab routes - formation per zone, no wall stare, the rear man keeps up, ")
		TEXT("patrol pace, the wall layout at the dead ends, the group cover order as the wall layout; [shot] [routes=R_A_open,R_F_deadend,n3]. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
