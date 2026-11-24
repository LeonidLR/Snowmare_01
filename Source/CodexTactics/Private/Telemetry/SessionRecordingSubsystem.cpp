#include "Telemetry/SessionRecordingSubsystem.h"
#include "Training/TrainingSectorSubsystem.h"

#include "AI/PmcSquadSubsystem.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/CombatCheats.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Data/WeaponDataAsset.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/IConsoleManager.h"
#include "LevelGen/MissionGoalsSubsystem.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Telemetry/RunTelemetrySubsystem.h"

namespace SessionRecordingRuntime
{
	TAutoConsoleVariable<int32> CVarRecord(TEXT("Codex.Demo.Record"), 1, TEXT("0: off; 1: human game worlds; 2: force for manual checks."));
	FAutoConsoleCommandWithWorldAndArgs MarkCommand(TEXT("Codex.Demo.Mark"), TEXT("Set the current recording mark."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (USessionRecordingSubsystem* Recorder = USessionRecordingSubsystem::GetIfRecording(World))
			{
				Recorder->MarkSession(FString::Join(Args, TEXT(" ")));
			}
		}));

	TOptional<FVector> ActiveGoal(const UWorld& World)
	{
		const UMissionGoalsSubsystem* Goals = World.GetSubsystem<UMissionGoalsSubsystem>();
		if (Goals && Goals->IsActive())
		{
			for (int32 Index = 0; Index < Goals->GetGoals().Num(); ++Index)
			{
				if (!Goals->IsGoalReached(Index)) { return Goals->GetGoals()[Index].Location; }
			}
		}
		return {};
	}

	FDemoThreat Describe(const AActor& Actor)
	{
		const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(&Actor);
		const UEnum* Enum = Enemy ? StaticEnum<EEnemyArchetype>() : nullptr;
		const FName Archetype = Enum ? Enum->GetNameByValue(static_cast<int64>(Enemy->GetArchetype()))
			: Actor.IsA<AOperativeCharacter>() ? FName(TEXT("PMC")) : Actor.GetClass()->GetFName();
		return { Actor.GetFName(), Archetype, Actor.GetActorLocation() };
	}

	bool Alive(const AOperativeCharacter& Op)
	{
		return !Op.IsKilledInAction() && Op.HealthComponent && Op.HealthComponent->IsAlive();
	}
}

bool USessionRecordingSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void USessionRecordingSubsystem::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World);
	using namespace SessionRecordingRuntime;
	const TCHAR* Cmd = FCommandLine::Get();
	const FDemoGate Gate{ CVarRecord.GetValueOnGameThread(), World.IsGameWorld(),
		FApp::IsUnattended(), FParse::Param(Cmd, TEXT("nullrhi")), FParse::Param(Cmd, TEXT("NoTelemetry")),
		IsRunningCommandlet(), GIsAutomationTesting };
	if (const TCHAR* Why = SessionRecordingRules::OffReason(Gate))
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[DemoRecord] off (%s)"), Why);
		return;
	}
	Start(FPaths::ProjectSavedDir() / TEXT("Demos"));
	if (UGameFlowSubsystem* Flow = World.GetSubsystem<UGameFlowSubsystem>())
	{
		Flow->OnGameFlowChanged.AddDynamic(this, &USessionRecordingSubsystem::HandleFlow);
	}
	World.GetTimerManager().SetTimer(SnapshotTimer, this, &USessionRecordingSubsystem::TakeSnapshot,
		static_cast<float>(SessionRecordingRules::SnapshotIntervalSec), true);
	UE_LOG(LogCodexTactics, Display, TEXT("[DemoRecord] recording"));
}

void USessionRecordingSubsystem::Start(const FString& Directory)
{
	Session = FDemoSession{};
	Session.StartedLocal = FDateTime::Now();
	Session.StartedUtc = FDateTime::UtcNow();
	Session.Map = SessionRecordingRules::CleanMapName(FPaths::GetCleanFilename(GetWorld()->GetMapName()));
	if (const UTrainingSectorSubsystem* Training = UTrainingSectorSubsystem::Get(GetWorld()); Training && Training->GetArea())
	{
		Session.SectorId = Training->GetSectorId();
		Session.SectorBox = *Training->GetArea();
	}
	Session.bPie = GetWorld()->WorldType == EWorldType::PIE;
	GameStart = GetWorld()->GetTimeSeconds();
	RealStart = FPlatformTime::Seconds();
	OutputDirectory = Directory;
	constexpr int32 ReserveFrames = 3600;
	Session.Frames.Reserve(ReserveFrames);
	Session.Ops.Reserve(4 * ReserveFrames);
	Session.Threats.Reserve(SessionRecordingRules::MaxThreats * ReserveFrames);
	Session.Units.Reserve(8);
	LastOrders.Reset(8);
	Suppressed = NAME_None;
	bRecording = true;
}

USessionRecordingSubsystem* USessionRecordingSubsystem::GetIfRecording(const UObject* Context)
{
	const UWorld* World = Context ? Context->GetWorld() : nullptr;
	USessionRecordingSubsystem* Recorder = World ? World->GetSubsystem<USessionRecordingSubsystem>() : nullptr;
	return Recorder && Recorder->bRecording && Recorder->Suppressed.IsNone() ? Recorder : nullptr;
}

void USessionRecordingSubsystem::OnTrainingSectorChanged()
{
	if (!bRecording || !Suppressed.IsNone())
	{
		return;
	}
	const FString Directory = OutputDirectory;
	Flush(FPlatformTime::Seconds() - RealStart);
	Start(Directory);
	GetWorld()->GetTimerManager().SetTimer(SnapshotTimer, this, &USessionRecordingSubsystem::TakeSnapshot,
		static_cast<float>(SessionRecordingRules::SnapshotIntervalSec), true);
}

void USessionRecordingSubsystem::SuppressSession(FName Why)
{
	Suppressed = Why;
	GetWorld()->GetTimerManager().ClearTimer(SnapshotTimer);
}

void USessionRecordingSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SnapshotTimer);
		if (UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>())
		{
			Flow->OnGameFlowChanged.RemoveDynamic(this, &USessionRecordingSubsystem::HandleFlow);
		}
	}
	Flush(FPlatformTime::Seconds() - RealStart);
	Super::Deinitialize();
}

void USessionRecordingSubsystem::HandleFlow(ECodexGamePhase Phase, ECodexCombatMode Mode)
{
	if (Phase == ECodexGamePhase::GameOver) { Session.Result = EDemoResult::Defeat; }
	else if (Phase == ECodexGamePhase::PostCombat && Session.Result != EDemoResult::Defeat) { Session.Result = EDemoResult::Victory; }
}

void USessionRecordingSubsystem::Flush(double Duration)
{
	if (!bRecording) { return; }
	bRecording = false;
	const URunTelemetrySubsystem* Telemetry = GetWorld()->GetSubsystem<URunTelemetrySubsystem>();
	const bool bDisabled = !bTestSession && SessionRecordingRuntime::CVarRecord.GetValueOnGameThread() == 0;
	const bool bBot = !bTestSession && Telemetry && Telemetry->GetTesterProfile() != TEXT("HUMAN");
	if (SessionRecordingRules::ShouldDiscard(Duration, Session.AcceptedOrders, !Suppressed.IsNone() || bDisabled || bBot))
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[DemoRecord] discarded (%s)"), !Suppressed.IsNone() ? *Suppressed.ToString()
			: bDisabled ? TEXT("disabled") : bBot ? TEXT("non-human") : Duration < SessionRecordingRules::MinSessionSec
			? TEXT("under 60 s") : TEXT("no accepted orders"));
		return;
	}
	Session.Duration = Duration;
	IFileManager& Files = IFileManager::Get();
	Files.MakeDirectory(*OutputDirectory, true);
	FDateTime StampTime = Session.StartedLocal;
	FString Path;
	int64 ExistingBytes = 0;
	Files.IterateDirectory(*OutputDirectory, [&Files, &ExistingBytes](const TCHAR* File, bool bDirectory)
	{
		if (!bDirectory && FStringView(File).EndsWith(FStringView(TEXT(".jsonl")))) { ExistingBytes += FMath::Max(int64(0), Files.FileSize(File)); }
		return true;
	});
	do
	{
		Path = OutputDirectory / SessionRecordingRules::MakeDemoFileName(StampTime, Session.Map);
		StampTime += FTimespan::FromSeconds(1.);
	} while (Files.FileExists(*Path));
	const FString Text = SessionRecordingRules::SerializeSession(Session, FPaths::GetBaseFilename(Path));
	const FString Temp = Path + FString::Printf(TEXT(".%u.tmp"), FPlatformProcess::GetCurrentProcessId());
	if (!FFileHelper::SaveStringToFile(Text, *Temp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
		|| !Files.Move(*Path, *Temp, false, false, false, true))
	{
		Files.Delete(*Temp, false, true, true);
		UE_LOG(LogCodexTactics, Warning, TEXT("[DemoRecord] could not save %s"), *FPaths::GetCleanFilename(Path));
		return;
	}
	if (ExistingBytes + Files.FileSize(*Path) >= 300LL * 1024 * 1024)
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("[DemoRecord] recordings exceed 300 MB"));
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[DemoRecord] saved %s"), *FPaths::GetCleanFilename(Path));
}

void USessionRecordingSubsystem::Stamp(FDemoFrame& Frame) const
{
	Frame.Time = FMath::Max(0., GetWorld()->GetTimeSeconds() - GameStart);
	Frame.RealTime = FMath::Max(0., FPlatformTime::Seconds() - RealStart);
}

void USessionRecordingSubsystem::RecordSectorRound(const FDemoRoundEvent& Event)
{
	if (!bRecording || !Suppressed.IsNone() || !GetWorld())
	{
		return;
	}
	FDemoRoundEvent& Stored = Session.Rounds.Add_GetRef(Event);
	Stored.Time = FMath::Max(0., GetWorld()->GetTimeSeconds() - GameStart);
	Stored.RealTime = FMath::Max(0., FPlatformTime::Seconds() - RealStart);
}

void USessionRecordingSubsystem::RecordMode(EDemoMode Mode, bool bOn)
{
	if (SuppressDepth > 0) { return; }
	FDemoFrame& Frame = Session.Frames.AddDefaulted_GetRef();
	Stamp(Frame);
	Frame.Type = FDemoFrame::EType::Mode;
	Frame.Mode = Mode;
	Frame.bModeOn = bOn;
}

void USessionRecordingSubsystem::TakeSnapshot()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DemoRecord_Snapshot);
	if (!bRecording || !Suppressed.IsNone()) { return; }
	FDemoFrame Frame;
	Stamp(Frame);
	AppendSnapshot(Frame);
}

void USessionRecordingSubsystem::AppendSnapshot(FDemoFrame Frame)
{
	if (Frame.Time >= SessionRecordingRules::MaxSessionSec)
	{
		Session.bTruncated = true;
		GetWorld()->GetTimerManager().ClearTimer(SnapshotTimer);
		return;
	}
	if (const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>())
	{
		if (Flow->GetCombatMode() == ECodexCombatMode::TacticalPause) { return; }
	}
	Capture(Frame, nullptr);
	Session.Frames.Add(Frame);
}

void USessionRecordingSubsystem::Capture(FDemoFrame& Frame, const AOperativeCharacter* Actor)
{
	using namespace SessionRecordingRuntime;
	const UWorld& World = *GetWorld();
	const USquadSubsystem* Squad = World.GetSubsystem<USquadSubsystem>();
	if (!Squad) { return; }
	const UGameFlowSubsystem* Flow = World.GetSubsystem<UGameFlowSubsystem>();
	const TOptional<FVector> Goal = ActiveGoal(World);
	const bool bHeader = Session.Units.IsEmpty();
	if (bHeader)
	{
		Session.InitialGoal = Goal;
		if (const UTrainingSectorSubsystem* Training = UTrainingSectorSubsystem::Get(&World); Training && Training->GetArea())
		{
			Session.SectorId = Training->GetSectorId();
			Session.SectorBox = *Training->GetArea();
		}
		Session.bImmortal = CombatCheats::IsImmortal(&World);
		Session.bInfiniteAmmo = CombatCheats::IsInfiniteAmmo(&World);
	}
	Frame.bSolo = Squad->IsSoloMode();
	Frame.Formation = FName(FormationNames::ToString(Squad->GetFormationKind()));
	Frame.bAutonomy = Flow && Flow->IsAutonomousSquadCombat();
	Frame.bPaused = World.IsPaused() || (Flow && Flow->GetCombatMode() == ECodexCombatMode::TacticalPause);
	Frame.FirstOp = Session.Ops.Num();
	Frame.FirstThreat = Session.Threats.Num();
	const AOperativeCharacter* Selected = Squad->GetLeader();
	if (!Actor && Frame.Type == FDemoFrame::EType::Order) { Actor = Selected; }
	Frame.Actor = Actor ? Actor->GetFName() : NAME_None;
	bool& bHasSelection = Frame.bBoxSelected;
	for (const TWeakObjectPtr<AOperativeCharacter>& Ref : Squad->GetMemberRefs())
	{
		const AOperativeCharacter* Op = Ref.Get();
		bHasSelection |= Op && Alive(*Op) && Squad->IsGroupSelected(Op);
	}

	FVector Centre = FVector::ZeroVector;
	Session.Alive = 0;
	for (const TWeakObjectPtr<AOperativeCharacter>& Ref : Squad->GetMemberRefs())
	{
		const AOperativeCharacter* Op = Ref.Get();
		if (!Op) { continue; }
		FDemoOp& View = Session.Ops.AddDefaulted_GetRef();
		View.Id = Op->GetFName();
		View.Position = Op->GetActorLocation();
		View.Yaw = Op->GetActorRotation().Yaw;
		View.bAlive = Alive(*Op);
		View.bMoving = Op->IsMoving();
		View.bCover = Op->bInCover;
		View.bSelected = bHasSelection ? View.bAlive && Squad->IsGroupSelected(Op) : Op == Selected;
		View.bLeader = Op == Squad->GetFormationLeader();
		View.Stance = static_cast<uint8>(Op->GetStance());
		View.Health = Op->HealthComponent ? Op->HealthComponent->GetCurrentHealth() : 0.f;
		View.Clip = Op->CurrentClip;
		View.Reserve = Op->CurrentWeapon ? Op->GetReserve(Op->CurrentWeapon->WeaponId) : 0;
		View.GoalDistance = Goal ? FVector::Dist2D(View.Position, *Goal) : -1.;
		if (const FLastOrder* Last = LastOrders.FindByPredicate([&View](const FLastOrder& Order) { return Order.Id == View.Id; }))
		{
			View.LastOrder = Last->Order;
			View.LastOrderTime = Last->Time;
		}
		if (bHeader)
		{
			const FName Role = StaticEnum<EOperativeRole>()->GetNameByValue(static_cast<int64>(Op->SquadRole));
			Session.Units.Add({ View.Id, Role, Op->CurrentWeapon ? FName(*Op->CurrentWeapon->WeaponId) : NAME_None,
				Op->HealthComponent ? Op->HealthComponent->GetMaxHealth() : 0.f, Op->CurrentWeapon ? Op->CurrentWeapon->Stats.Ammo.MagazineSize : 0 });
		}
		if (View.bAlive)
		{
			Centre += View.Position;
			++Session.Alive;
		}
	}
	Frame.NumOps = Session.Ops.Num() - Frame.FirstOp;
	Centre /= FMath::Max(Session.Alive, 1);
	struct FNearest { const AActor* Actor = nullptr; double DistanceSq = 0.; };
	TArray<FNearest, TInlineAllocator<SessionRecordingRules::MaxThreats>> Nearest;
	const UTacticalSightSubsystem* Sight = World.GetSubsystem<UTacticalSightSubsystem>();
	const auto Visit = [&](const AActor& Visible)
	{
		if (!Sight || !Sight->IsVisibleToSquad(&Visible)) { return; }
		const FVector Position = Visible.GetActorLocation();
		const double DistanceSq = FVector::DistSquared2D(Centre, Position);
		int32 Insert = 0;
		while (Insert < Nearest.Num() && Nearest[Insert].DistanceSq <= DistanceSq) { ++Insert; }
		if (Insert < SessionRecordingRules::MaxThreats)
		{
			if (Nearest.Num() == SessionRecordingRules::MaxThreats) { Nearest.Pop(EAllowShrinking::No); }
			Nearest.Insert({ &Visible, DistanceSq }, Insert);
		}
		for (int32 Index = Frame.FirstOp; Index < Session.Ops.Num(); ++Index)
		{
			FDemoOp& View = Session.Ops[Index];
			++View.ThreatCount;
			const double Distance = FVector::Dist2D(View.Position, Position);
			if (View.ThreatDistance >= 0. && Distance >= View.ThreatDistance) { continue; }
			const FVector2D Measured = SessionRecordingRules::ThreatDistanceBearing(View.Position, View.Yaw, &Position);
			View.ThreatDistance = Measured.X;
			View.ThreatBearing = Measured.Y;
			View.NearestThreat = Position;
		}
	};
	UEnemyRegistrySubsystem::ForEachEnemyCharacter(&World, [&](const AEnemyCharacter& Enemy)
	{
		if (const UHealthComponent* Health = Enemy.GetHealthComponent(); Health && Health->IsAlive()) { Visit(Enemy); }
	});
	if (const UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(&World))
	{
		Pmc->ForEachHostileOperative([&](const AOperativeCharacter& Op) { if (Alive(Op)) { Visit(Op); } });
	}
	for (const FNearest& Entry : Nearest) { Session.Threats.Add(Describe(*Entry.Actor)); }
	Frame.NumThreats = Nearest.Num();
	SelectActor(Frame, Frame.Actor);
	if (Goal && Frame.Destination) { Frame.Facts.GoalDistanceNew = FVector::Dist2D(*Goal, *Frame.Destination); }
}

void USessionRecordingSubsystem::SelectActor(FDemoFrame& Frame, FName Actor)
{
	Frame.Actor = Actor;
	for (int32 Index = Frame.FirstOp; Index < Frame.FirstOp + Frame.NumOps; ++Index)
	{
		const FDemoOp& View = Session.Ops[Index];
		if (View.Id != Actor) { continue; }
		Frame.Facts.bMoving = View.bMoving;
		Frame.Facts.bInCover = View.bCover;
		Frame.Facts.ThreatDistance = View.ThreatDistance;
		Frame.Facts.GoalDistance = View.GoalDistance;
		Frame.Facts.ThreatDistanceNew = -1.;
		Frame.Facts.FlankDegrees = 0.;
		if (View.ThreatDistance >= 0. && Frame.Destination)
		{
			Frame.Facts.ThreatDistanceNew = FVector::Dist2D(*Frame.Destination, View.NearestThreat);
			const FVector A = (View.Position - View.NearestThreat).GetSafeNormal2D();
			const FVector B = (*Frame.Destination - View.NearestThreat).GetSafeNormal2D();
			if (!A.IsNearlyZero() && !B.IsNearlyZero())
			{
				Frame.Facts.FlankDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(A, B), -1., 1.)));
			}
		}
		break;
	}
}

int32 USessionRecordingSubsystem::BeginOrder(EDemoOrderKind Kind, const AOperativeCharacter* Actor,
	const TOptional<FVector>& Destination, const FCoverSlot* Cover, const AActor* Target, bool bSprint, bool bPlan)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DemoRecord_Order);
	FDemoFrame Frame;
	Stamp(Frame);
	Frame.Facts.Kind = Kind;
	Frame.Destination = Destination;
	Frame.bSprint = bSprint;
	Frame.bPlan = bPlan;
	if (Cover)
	{
		Frame.CoverPoint = Cover->WorldLocation;
		Frame.CoverYaw = Cover->WallNormal.Rotation().Yaw;
		Frame.CoverHeight = static_cast<uint8>(Cover->Height);
	}
	if (Target) { Frame.Target = SessionRecordingRuntime::Describe(*Target); }
	return AppendOrder(Frame, Actor);
}

int32 USessionRecordingSubsystem::AppendOrder(FDemoFrame Frame, const AOperativeCharacter* Actor)
{
	Frame.Type = FDemoFrame::EType::Order;
	if (!bTestSession) { Capture(Frame, Actor); }
	return Session.Frames.Add(Frame);
}

void USessionRecordingSubsystem::EndOrder(int32 Index)
{
	const FDemoFrame& Frame = Session.Frames[Index];
	if (!Frame.Facts.bAccepted) { return; }
	++Session.AcceptedOrders;
	const FDemoMapping Mapping = SessionRecordingRules::MapToPmcOrder(Frame.Facts);
	for (int32 I = Frame.FirstOp; I < Frame.FirstOp + Frame.NumOps; ++I)
	{
		const FDemoOp& Op = Session.Ops[I];
		const bool bDirect = Op.Id == Frame.Actor;
		const bool bGroupOrder = Frame.Facts.Kind == EDemoOrderKind::Move || Frame.Facts.Kind == EDemoOrderKind::Cover
			|| Frame.Facts.Kind == EDemoOrderKind::CornerPeek || Frame.Facts.Kind == EDemoOrderKind::Breach;
		if (!bDirect && (!bGroupOrder || !Op.bAlive || ((Frame.bBoxSelected && !Op.bSelected) || (Frame.bSolo && !Frame.bBoxSelected)))) { continue; }
		const FLastOrder Last{ Op.Id, FName(bDirect ? (Mapping.Order ? Mapping.Order : SessionRecordingRules::KindName(Frame.Facts.Kind)) : TEXT("Follow")),
			Frame.Time };
		if (FLastOrder* Entry = LastOrders.FindByPredicate([&Op](const FLastOrder& Existing) { return Existing.Id == Op.Id; })) { *Entry = Last; }
		else { LastOrders.Add(Last); }
	}
}

FDemoOrderScope::FDemoOrderScope(const UObject* Context, EDemoOrderKind Kind, const AOperativeCharacter* Actor,
	const TOptional<FVector>& Destination, bool bSprint, bool bPlan, const FCoverSlot* Cover, const AActor* Target)
	: Recorder(USessionRecordingSubsystem::GetIfRecording(Context))
{
	if (Recorder && Recorder->SuppressDepth == 0 && Recorder->OrderDepth++ == 0)
	{
		Frame = Recorder->BeginOrder(Kind, Actor, Destination, Cover, Target, bSprint, bPlan);
	}
	else if (Recorder && Recorder->SuppressDepth > 0) { Recorder = nullptr; }
}

FDemoOrderScope::~FDemoOrderScope()
{
	if (!Recorder) { return; }
	--Recorder->OrderDepth;
	if (Frame != INDEX_NONE) { Recorder->EndOrder(Frame); }
}

bool FDemoOrderScope::SetResult(bool bAccepted)
{
	if (Frame != INDEX_NONE) { Recorder->Session.Frames[Frame].Facts.bAccepted = bAccepted; }
	return bAccepted;
}

void FDemoOrderScope::SetCrouch(bool bCrouch)
{
	if (Frame != INDEX_NONE) { Recorder->Session.Frames[Frame].Facts.bCrouch = bCrouch; }
}

void FDemoOrderScope::SetCease()
{
	if (Frame != INDEX_NONE) { Recorder->Session.Frames[Frame].Facts.bCease = true; }
}

void FDemoOrderScope::SetPlan(bool bPlan)
{
	if (Frame != INDEX_NONE) { Recorder->Session.Frames[Frame].bPlan = bPlan; }
}

void FDemoOrderScope::SetActor(const AOperativeCharacter* Actor)
{
	if (Frame != INDEX_NONE && Actor) { Recorder->SelectActor(Recorder->Session.Frames[Frame], Actor->GetFName()); }
}

void FDemoOrderScope::SetTarget(const AActor* Target, const FVector* KnownPosition)
{
	if (Frame == INDEX_NONE) { return; }
	FDemoThreat& View = Recorder->Session.Frames[Frame].Target;
	View = Target ? SessionRecordingRuntime::Describe(*Target) : FDemoThreat{};
	if (KnownPosition && Target) { View.Position = *KnownPosition; }
}

void FDemoOrderScope::SetSprint(bool bSprint)
{
	if (Frame != INDEX_NONE) { Recorder->Session.Frames[Frame].bSprint = bSprint; }
}

FDemoSuppressScope::FDemoSuppressScope(const UObject* Context) : Recorder(USessionRecordingSubsystem::GetIfRecording(Context))
{
	if (Recorder) { ++Recorder->SuppressDepth; }
}

FDemoSuppressScope::~FDemoSuppressScope()
{
	if (Recorder) { --Recorder->SuppressDepth; }
}

#if WITH_DEV_AUTOMATION_TESTS
void USessionRecordingSubsystem::StartForTest(const FString& Directory)
{
	bTestSession = true;
	Start(Directory);
}

void USessionRecordingSubsystem::RecordOrderAt(double Time, const FDemoFacts& Facts)
{
	FDemoFrame Frame;
	Frame.Time = Time;
	Frame.RealTime = Time;
	Frame.Facts = Facts;
	EndOrder(AppendOrder(Frame, nullptr));
}

void USessionRecordingSubsystem::SnapshotAt(double Time)
{
	FDemoFrame Frame;
	Frame.Time = Time;
	Frame.RealTime = Time;
	AppendSnapshot(Frame);
}

void USessionRecordingSubsystem::FinishAt(double Time)
{
	Flush(Time);
}
#endif
