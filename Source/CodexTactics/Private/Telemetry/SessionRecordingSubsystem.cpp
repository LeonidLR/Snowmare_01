#include "Telemetry/SessionRecordingSubsystem.h"
#include "Training/TrainingSectorSubsystem.h"
#include "UI/FloatingTextSubsystem.h"
#include "EngineUtils.h"

#include "AI/PmcSquadSubsystem.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/CombatCheats.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Combat/SightRules.h"
#include "Data/WeaponDataAsset.h"
#include "Engine/World.h"
#include "Characters/StealthModeRules.h"
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
	FAutoConsoleCommandWithWorldAndArgs MarkCommand(TEXT("Codex.Demo.Mark"), TEXT("Mark a round: <value> [prev | round <index>]."),
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
		const AOperativeCharacter* Op = Cast<AOperativeCharacter>(&Actor);
		return { Actor.GetFName(), Archetype, Actor.GetActorLocation(), Actor.GetActorRotation().Yaw,
			Op ? TOptional<bool>(Op->bInCover) : TOptional<bool>() };
	}

	// Observer-specific optical evidence, independent of shared team memory or current target.
	bool Sees(const UWorld& World, const AActor& Observer, const AActor& Target)
	{
		const UTacticalSightSubsystem* Sight = World.GetSubsystem<UTacticalSightSubsystem>();
		if (!Sight) { return false; }
		if (const AOperativeCharacter* Op = Cast<AOperativeCharacter>(&Observer))
		{
			const AOperativeCharacter* TargetOp = Cast<AOperativeCharacter>(&Target);
			const float Distance = FVector::Dist2D(Observer.GetActorLocation(), Target.GetActorLocation());
			const float Range = Sight->GetSightRangeCm(Observer) * StealthModeRules::DetectionRangeScale(
				TargetOp && TargetOp->IsInStealthMode(), TargetOp && TargetOp->IsStealthFast(), StealthModeRules::GetConfig());
			if ((TargetOp && TargetOp->IsHiddenInCoverFrom(Observer.GetActorLocation())) || Distance > Range) { return false; }
			if (SightRules::IsWithinTouchReveal(Distance)
				&& Sight->HasClearSight(UTacticalSightSubsystem::EyePoint(Observer), UTacticalSightSubsystem::ProfilePoint(Target))) { return true; }
			return Sight->CanOperativeSee(*Op, UTacticalSightSubsystem::ProfilePoint(Target));
		}
		const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(&Observer);
		const AOperativeCharacter* Op = Cast<AOperativeCharacter>(&Target);
		if (!Enemy || !Op || Op->IsHiddenInCoverFrom(Observer.GetActorLocation())) { return false; }
		FEnemyPerceptionParams Params = Enemy->GetPerception();
		Params.SightRangeCm *= Enemy->GetSightRangeScale();
		const float StealthScale = StealthModeRules::DetectionRangeScale(Op->IsInStealthMode(), Op->IsStealthFast(), StealthModeRules::GetConfig());
		Params.SightRangeCm *= StealthScale;
		Params.ProximityCm *= StealthScale;
		return PerceptionRules::CanSee(Params, Observer.GetActorLocation(), Observer.GetActorForwardVector(),
			Target.GetActorLocation(), Op->GetStance(), true)
			&& Sight->HasClearSight(UTacticalSightSubsystem::EyePoint(Observer), UTacticalSightSubsystem::ProfilePoint(Target));
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
	LastHitsByVictim.Reset();
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

void USessionRecordingSubsystem::MarkSession(const FString& Command)
{
 TArray<FString> Args;
 Command.ParseIntoArrayWS(Args);
 if (Args.IsEmpty() || !GetWorld()) { return; }
 bool bPrevious = false;
 int32 Explicit = INDEX_NONE;
 if (Args.Num() == 2 && Args[1] == TEXT("prev")) { bPrevious = true; }
 else if (Args.Num() == 3 && Args[1] == TEXT("round") && Args[2].IsNumeric())
 { Explicit = FCString::Atoi(*Args[2]); if (Explicit < 0) { return; } }
 else if (Args.Num() != 1) { return; }
 const double Now = GetWorld()->GetTimeSeconds() - GameStart;
 double Started = 0.;
 for (const FDemoRoundEvent& Round : Session.Rounds)
 { if (Round.bStart && !Round.bPmcRelocated) { Started = Round.Time; } }
 bool bContact = false;
 for (const FDemoEvent& Event : Session.Events)
 { if (Event.Time >= Started && (Event.Type == FDemoEvent::EType::ShotHit || Event.Type == FDemoEvent::EType::Damage)) { bContact = true; } }
 for (const FDemoFrame& Frame : Session.Frames)
 {
  if (Frame.Time < Started) { continue; }
  if (Frame.NumThreats > 0) { bContact = true; }
  for (int32 I = Frame.FirstOp; I < Frame.FirstOp + Frame.NumOps; ++I)
  { if (Session.Ops[I].SeenByEnemy > 0) { bContact = true; } }
 }
 if (const UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(GetWorld()))
 { bContact |= Pmc->GetLatestContactActivityTime() > GameStart + Started; }
 for (TActorIterator<AOperativeCharacter> It(GetWorld()); It; ++It)
 { bContact |= It->GetLastShotTime() > GameStart + Started; }
 const int32 Round = SessionRecordingRules::ChooseMarkRound(Session, Now, bContact, bPrevious, Explicit);
 if (Round == INDEX_NONE) { return; }
 Session.Mark = Args[0];
 FDemoEvent& Event = Session.Events.AddDefaulted_GetRef();
 Event.Type = FDemoEvent::EType::RoundMark;
 Event.Time = Now;
 Event.RealTime = FPlatformTime::Seconds() - RealStart;
 Event.Round = Round;
 Event.Value = Args[0];
 if (const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>())
 {
  UFloatingTextSubsystem::SpawnAboveOperative(Squad->GetLeader(),
   FString::Printf(TEXT("Раунд %d помечен: %s"), Round, *Args[0]), FLinearColor::White);
 }
 UE_LOG(LogCodexTactics, Display, TEXT("[DemoRecord] round %d marked: %s"), Round, *Args[0]);
}

void USessionRecordingSubsystem::RecordSectorRound(const FDemoRoundEvent& Event)
{
	if (!bRecording || !Suppressed.IsNone() || !GetWorld())
	{
		return;
	}
	LastHitsByVictim.Reset();
	FDemoRoundEvent& Stored = Session.Rounds.Add_GetRef(Event);
	Stored.Time = FMath::Max(0., GetWorld()->GetTimeSeconds() - GameStart);
	Stored.RealTime = FMath::Max(0., FPlatformTime::Seconds() - RealStart);
}

void USessionRecordingSubsystem::RecordShotHit(const AActor* Shooter, const AActor* Victim, FName WeaponId, float Damage, float DistanceCm)
{
	if (!bRecording || !Suppressed.IsNone() || !GetWorld() || !Shooter || !Victim)
	{
		return;
	}
	const FName Who = Shooter->GetFName();
	const FName Whom = Victim->GetFName();
	const AOperativeCharacter* Op = Cast<AOperativeCharacter>(Shooter);
	LastHitsByVictim.Add(Whom, FLastHitContext{ Who, WeaponId, DistanceCm, Shooter,
		Op ? Op->DisplayName.ToString() : Shooter->GetName(), GetWorld()->GetTimeSeconds() });
	FDemoEvent& Event = Session.Events.AddDefaulted_GetRef();
	Event.Type = FDemoEvent::EType::ShotHit;
	Event.Time = FMath::Max(0., GetWorld()->GetTimeSeconds() - GameStart);
	Event.RealTime = FMath::Max(0., FPlatformTime::Seconds() - RealStart);
	Event.Who = Who;
	Event.Whom = Whom;
	Event.Weapon = WeaponId;
	Event.Amount = Damage;
	Event.DistanceCm = DistanceCm;
}

void USessionRecordingSubsystem::RecordDamageContext(const AActor* Attacker, const AActor* Victim, const FString& Source, FName WeaponId)
{
	if (!bRecording || !Suppressed.IsNone() || !GetWorld() || !Attacker || !Victim) { return; }
	LastHitsByVictim.Add(Victim->GetFName(), FLastHitContext{ Attacker->GetFName(), WeaponId,
		static_cast<float>(FVector::Dist2D(Attacker->GetActorLocation(), Victim->GetActorLocation())),
		Attacker, Source, GetWorld()->GetTimeSeconds() });
}

void USessionRecordingSubsystem::RecordDamage(const FString& AttackerSource, const AActor* Victim, float Damage, const FVector& DamageOrigin)
{
	if (!bRecording || !Suppressed.IsNone() || !GetWorld() || !Victim || Damage <= 0.f)
	{
		return;
	}
	const FName Whom = Victim->GetFName();
	FName Who = AttackerSource.IsEmpty() ? FName(TEXT("Unknown")) : FName(*AttackerSource);
	float DistCm = DamageOrigin.IsZero() ? -1.f : static_cast<float>(FVector::Dist2D(Victim->GetActorLocation(), DamageOrigin));
	if (const FLastHitContext* Hit = LastHitsByVictim.Find(Whom); Hit && Hit->Source == AttackerSource
		&& Hit->Time == GetWorld()->GetTimeSeconds())
	{
		if (Hit->Shooter.IsValid() && DistCm < 0.f)
		{
			DistCm = Hit->DistanceCm;
		}
		if (!Hit->Who.IsNone())
		{
			Who = Hit->Who;
		}
	}
	if (const FLastHitContext* Hit = LastHitsByVictim.Find(Whom);
		Hit && (Hit->Source != AttackerSource || Hit->Time != GetWorld()->GetTimeSeconds()))
	{
		LastHitsByVictim.Remove(Whom);
	}
	FDemoEvent& Event = Session.Events.AddDefaulted_GetRef();
	Event.Type = FDemoEvent::EType::Damage;
	Event.Time = FMath::Max(0., GetWorld()->GetTimeSeconds() - GameStart);
	Event.RealTime = FMath::Max(0., FPlatformTime::Seconds() - RealStart);
	Event.Who = Who;
	Event.Whom = Whom;
	Event.Amount = Damage;
	Event.DistanceCm = DistCm;
}

void USessionRecordingSubsystem::RecordKillOrDown(const AActor* Victim, const FString& AttackerSource, bool bIsDown)
{
	if (!bRecording || !Suppressed.IsNone() || !GetWorld() || !Victim)
	{
		return;
	}
	const UWorld& World = *GetWorld();
	const FName Whom = Victim->GetFName();
	FName Who = AttackerSource.IsEmpty() ? FName(TEXT("Unknown")) : FName(*AttackerSource);
	FName Weapon = NAME_None;
	float DistCm = -1.f;
	const AActor* KillerActor = nullptr;
	if (const FLastHitContext* Hit = LastHitsByVictim.Find(Whom); Hit && Hit->Source == AttackerSource
		&& Hit->Time == GetWorld()->GetTimeSeconds())
	{
		if (!Hit->Who.IsNone()) { Who = Hit->Who; }
		Weapon = Hit->Weapon;
		DistCm = Hit->DistanceCm;
		KillerActor = Hit->Shooter.Get();
	}
	bool bAware = false;
	bool bCoveredByAlly = false;
	const UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(&World);
	if (const AOperativeCharacter* VictimOp = Cast<AOperativeCharacter>(Victim))
	{
		if (VictimOp->IsPlayerTeam())
		{
			bAware = (KillerActor && SessionRecordingRuntime::Sees(World, *VictimOp, *KillerActor));
			if (const USquadSubsystem* Squad = World.GetSubsystem<USquadSubsystem>())
			{
				for (const TWeakObjectPtr<AOperativeCharacter>& Ref : Squad->GetMemberRefs())
				{
					const AOperativeCharacter* Ally = Ref.Get();
					if (Ally && Ally != VictimOp && SessionRecordingRuntime::Alive(*Ally)
						&& KillerActor && SessionRecordingRuntime::Sees(World, *Ally, *KillerActor))
					{
						bCoveredByAlly = true;
						break;
					}
				}
			}
		}
		else
		{
			bAware = (KillerActor && SessionRecordingRuntime::Sees(World, *VictimOp, *KillerActor));
			if (Pmc)
			{
				Pmc->ForEachHostileOperative([&](const AOperativeCharacter& Ally)
				{
					if (&Ally != VictimOp && SessionRecordingRuntime::Alive(Ally)
						&& Ally.TeamId == VictimOp->TeamId
						&& KillerActor && SessionRecordingRuntime::Sees(World, Ally, *KillerActor))
					{
						bCoveredByAlly = true;
					}
				});
			}
		}
	}
	else if (const AEnemyCharacter* VictimEnemy = Cast<AEnemyCharacter>(Victim))
	{
		bAware = KillerActor && SessionRecordingRuntime::Sees(World, *VictimEnemy, *KillerActor);
		UEnemyRegistrySubsystem::ForEachEnemyCharacter(&World, [&](const AEnemyCharacter& Ally)
		{
			if (&Ally != VictimEnemy && Ally.GetHealthComponent() && Ally.GetHealthComponent()->IsAlive()
				&& KillerActor && SessionRecordingRuntime::Sees(World, Ally, *KillerActor))
			{
				bCoveredByAlly = true;
			}
		});
	}
	FDemoEvent& Event = Session.Events.AddDefaulted_GetRef();
	Event.Type = bIsDown ? FDemoEvent::EType::Down : FDemoEvent::EType::Kill;
	Event.Time = FMath::Max(0., World.GetTimeSeconds() - GameStart);
	Event.RealTime = FMath::Max(0., FPlatformTime::Seconds() - RealStart);
	Event.Who = Who;
	Event.Whom = Whom;
	Event.Weapon = Weapon;
	Event.DistanceCm = DistCm;
	Event.bFlag = bAware;
	Event.bCoveredByAlly = bCoveredByAlly;
	Event.bContextKnown = KillerActor != nullptr;
	LastHitsByVictim.Remove(Whom);
}

void USessionRecordingSubsystem::RecordStealth(const AOperativeCharacter* Op, bool bEnabled)
{
	if (!bRecording || !Suppressed.IsNone() || !GetWorld() || !Op)
	{
		return;
	}
	FDemoEvent& Event = Session.Events.AddDefaulted_GetRef();
	Event.Type = FDemoEvent::EType::Stealth;
	Event.Time = FMath::Max(0., GetWorld()->GetTimeSeconds() - GameStart);
	Event.RealTime = FMath::Max(0., FPlatformTime::Seconds() - RealStart);
	Event.Who = Op->GetFName();
	Event.bFlag = bEnabled;
}

void USessionRecordingSubsystem::RecordCover(const AOperativeCharacter* Op, bool bEntered, FName HeightName)
{
	if (!bRecording || !Suppressed.IsNone() || !GetWorld() || !Op)
	{
		return;
	}
	FDemoEvent& Event = Session.Events.AddDefaulted_GetRef();
	Event.Type = FDemoEvent::EType::Cover;
	Event.Time = FMath::Max(0., GetWorld()->GetTimeSeconds() - GameStart);
	Event.RealTime = FMath::Max(0., FPlatformTime::Seconds() - RealStart);
	Event.Who = Op->GetFName();
	Event.Kind = HeightName;
	Event.bFlag = bEntered;
}

void USessionRecordingSubsystem::RecordClimb(const AActor* Op, FName Kind, float HeightCm)
{
	if (!bRecording || !Suppressed.IsNone() || !GetWorld() || !Op)
	{
		return;
	}
	FDemoEvent& Event = Session.Events.AddDefaulted_GetRef();
	Event.Type = FDemoEvent::EType::Climb;
	Event.Time = FMath::Max(0., GetWorld()->GetTimeSeconds() - GameStart);
	Event.RealTime = FMath::Max(0., FPlatformTime::Seconds() - RealStart);
	Event.Who = Op->GetFName();
	Event.Kind = Kind;
	Event.Amount = HeightCm;
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
	TArray<const AOperativeCharacter*, TInlineAllocator<8>> CapturedOps;
	for (const TWeakObjectPtr<AOperativeCharacter>& Ref : Squad->GetMemberRefs())
	{
		const AOperativeCharacter* Op = Ref.Get();
		if (!Op) { continue; }
		const double FeetZ = Op->GetActorLocation().Z - Op->GetSimpleCollisionHalfHeight();
		FDemoOp& View = Session.Ops.AddDefaulted_GetRef();
		CapturedOps.Add(Op);
		View.Id = Op->GetFName();
		View.Position = Op->GetActorLocation();
		View.Yaw = Op->GetActorRotation().Yaw;
		View.LookYaw = Op->GetSightFacing().Rotation().Yaw;
		View.bAlive = Alive(*Op);
		View.bMoving = Op->IsMoving();
		View.bCover = Op->bInCover;
		View.bStealth = Op->IsInStealthMode();
		// Use the sight system's ground column, including when the recording starts on a roof.
		View.HeightAboveGroundCm = FMath::Max(0.f, UTacticalSightSubsystem::MeasureEyeAboveGroundCm(World, *Op)
			- static_cast<float>(UTacticalSightSubsystem::EyePoint(*Op).Z - FeetZ));
		View.UnderFireSeconds = Op->GetUnderFireTime() > -1.0e8 ? FMath::Max(0., World.GetTimeSeconds() - Op->GetUnderFireTime()) : -1.;
		View.ScanPitchDeg = Op->HasManualLookPoint() ? Op->GetManualLookPitchDeg()
			: Op->GetFormationLookPitch().Get(0.f);
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
		if (const UHealthComponent* Health = Enemy.GetHealthComponent(); Health && Health->IsAlive())
		{
			Visit(Enemy);
			for (int32 Index = 0; Index < CapturedOps.Num(); ++Index)
			{
				if (Alive(*CapturedOps[Index]) && Sees(World, Enemy, *CapturedOps[Index]))
				{
					++Session.Ops[Frame.FirstOp + Index].SeenByEnemy;
				}
			}
		}
	});
	if (const UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(&World))
	{
		Pmc->ForEachHostileOperative([&](const AOperativeCharacter& Op)
		{
			if (Alive(Op))
			{
				Visit(Op);
				for (int32 Index = 0; Index < CapturedOps.Num(); ++Index)
				{
					if (Alive(*CapturedOps[Index]) && Sees(World, Op, *CapturedOps[Index]))
					{
						++Session.Ops[Frame.FirstOp + Index].SeenByEnemy;
					}
				}
			}
		});
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

void USessionRecordingSubsystem::RecordEventAt(const FDemoEvent& Event)
{
	Session.Events.Add(Event);
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
