// Task 139: the X6 gallery roof click must use its ladder; diagnose real link ends and navmesh projections. UE-only.
#include "CoreMinimal.h"
#if !UE_BUILD_SHIPPING
#include "SmokeUtils.h"
#include "AIController.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Interactables/MantleNavigation.h"
#include "Kismet/GameplayStatics.h"
#include "Ladders/LadderActor.h"
#include "Ladders/LadderClimbComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "NavLinkCustomComponent.h"

namespace RoofLadderSmoke
{
    const FVector RoofGoal(11510.f, -7621.f, 300.f);
    struct FState
    {
        SmokeUtils::FPreloadGate Preload;
        TWeakObjectPtr<ALadderActor> Ladder;
        float Time = 0.f;
        int32 Stage = 0;
        int32 ClimbsBefore = 0;
    };
    bool Finish(bool bPass, const FString& Why)
    {
        UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bPass ? TEXT("ok") : TEXT("FAIL"), *Why);
        UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s RoofLadderSmoke"), bPass ? TEXT("PASS") : TEXT("FAIL"));
        FPlatformMisc::RequestExit(false, TEXT("RoofLadderSmoke"));
        return false;
    }
    void Projection(UNavigationSystemV1& Nav, const TCHAR* Name, const FVector& Point)
    {
        FNavLocation Out;
        const bool bOk = Nav.ProjectPointToNavigation(Point, Out, FVector(50.f, 50.f, 60.f));
        UE_LOG(LogCodexTactics, Display, TEXT("[RoofLadder] %s %s -> nav %s (offset %.1f cm)"), Name, *Point.ToCompactString(),
            bOk ? *Out.Location.ToCompactString() : TEXT("NONE"), bOk ? FVector::Dist(Point, Out.Location) : -1.0);
    }
    void Path(UWorld& World, AOperativeCharacter& Leader, const FVector& From, const FVector& To, bool bNoWindow)
    {
        const UNavigationPath* P = UNavigationSystemV1::FindPathToLocationSynchronously(&World, From, To, &Leader,
            bNoWindow ? MantleNavigation::NoWindowVariant(nullptr, true) : nullptr);
        UE_LOG(LogCodexTactics, Display, TEXT("[RoofLadder] path no-window=%d from %s to %s: valid=%d partial=%d end=%s"), bNoWindow,
            *From.ToCompactString(), *To.ToCompactString(), P && P->IsValid(), P && P->IsPartial(),
            P && !P->PathPoints.IsEmpty() ? *P->PathPoints.Last().ToCompactString() : TEXT("NONE"));
    }
    bool Tick(const TSharedRef<FState>& State)
    {
        UWorld* World = nullptr;
        for (const FWorldContext& Context : GEngine->GetWorldContexts())
        {
            if (Context.World() && (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE)) { World = Context.World(); }
        }
        if (!World) { return true; }
        const SmokeUtils::EPreloadGate Gate = State->Preload.Poll(World);
        if (Gate == SmokeUtils::EPreloadGate::Waiting) { return true; }
        if (Gate == SmokeUtils::EPreloadGate::Failed) { return Finish(false, State->Preload.FailReason); }
        State->Time += World->GetDeltaSeconds();
        USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
        AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
        UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
        ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
        if (!Leader || !PC || !Nav) { return State->Time < 30.f || Finish(false, TEXT("squad/controller/nav available")); }
        if (State->Stage == 0)
        {
            SmokeUtils::StopLevelPmc(World);
            if (State->Time < 3.f || Nav->IsNavigationBuildInProgress()) { return State->Time < 30.f || Finish(false, TEXT("navigation settles")); }
            ALadderActor* Nearest = nullptr;
            double Best = TNumericLimits<double>::Max();
            for (TActorIterator<ALadderActor> It(World); It; ++It)
            {
                const double D = FVector::DistSquared2D(It->GetActorLocation(), RoofGoal);
                if (D < Best) { Best = D; Nearest = *It; }
            }
            if (!Nearest) { return Finish(false, TEXT("gallery ladder exists")); }
            State->Ladder = Nearest;
            UE_LOG(LogCodexTactics, Display, TEXT("[RoofLadder] nearest %s tags=%s at %s yaw=%.1f top-standoff=%.1f exit-standoff=%.1f enabled=%d"),
                *Nearest->GetName(), *FString::JoinBy(Nearest->Tags, TEXT(","), [](FName N) { return N.ToString(); }),
                *Nearest->GetActorLocation().ToCompactString(), Nearest->GetActorRotation().Yaw,
                Nearest->TopStandoff, Nearest->TopExitStandoff, Nearest->GetNavLink() && Nearest->GetNavLink()->IsEnabled());
            Projection(*Nav, TEXT("bottom"), Nearest->GetBottomLinkWorld());
            Projection(*Nav, TEXT("top"), Nearest->GetTopLinkWorld());
            Projection(*Nav, TEXT("exit"), Nearest->GetTopExitWorld());
            Projection(*Nav, TEXT("roof goal"), RoofGoal);
            Path(*World, *Leader, Nearest->GetBottomLinkWorld(), Nearest->GetTopLinkWorld(), false);
            Path(*World, *Leader, Nearest->GetBottomLinkWorld(), RoofGoal, false);
            Path(*World, *Leader, Nearest->GetBottomLinkWorld(), RoofGoal, true);
            if (Squad->IsSoloMode()) { Squad->ExitSoloMode(); }
            int32 Index = 0;
            for (AOperativeCharacter* Member : Squad->GetMembers())
            {
                const FVector Desired = Nearest->GetBottomLinkWorld() + Nearest->GetOutward() * (200.f + Index * 150.f);
                FNavLocation Stand;
                if (!Nav->ProjectPointToNavigation(Desired, Stand, FVector(80.f, 80.f, 80.f))) { return Finish(false, TEXT("ground near ladder foot")); }
                Member->StopOperative();
                Member->TeleportTo(Stand.Location + FVector(0.f, 0.f, Member->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f), FRotator::ZeroRotator, false, true);
                ++Index;
            }
            State->Stage = 1; State->Time = 0.f;
            return true;
        }
        if (State->Stage == 1)
        {
            if (State->Time < 1.f || Nav->IsNavigationBuildInProgress()) { return true; }
            State->ClimbsBefore = Leader->LadderClimb ? Leader->LadderClimb->GetClimbsUp() : 0;
            FHitResult Hit(nullptr, nullptr, RoofGoal, FVector::UpVector);
            Hit.bBlockingHit = true; Hit.Location = RoofGoal; Hit.TraceStart = RoofGoal + FVector(0.f, 0.f, 1000.f);
            PC->HandlePlayerClick(Hit);
            const AAIController* AI = Cast<AAIController>(Leader->GetController());
            const UPathFollowingComponent* Follow = AI ? AI->GetPathFollowingComponent() : nullptr;
            const FNavPathSharedPtr P = Follow ? Follow->GetPath() : nullptr;
            UE_LOG(LogCodexTactics, Display, TEXT("[RoofLadder] actual formation order: goal=%s valid=%d partial=%d end=%s"),
                *Leader->GetLastMoveDestination().ToCompactString(), P.IsValid() && P->IsValid(), P.IsValid() && P->IsPartial(),
                P.IsValid() && !P->GetPathPoints().IsEmpty() ? *P->GetEndLocation().ToCompactString() : TEXT("NONE"));
            State->Stage = 2; State->Time = 0.f;
            return true;
        }
        const float Feet = Leader->GetActorLocation().Z - Leader->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        const bool bClimbed = Leader->LadderClimb && Leader->LadderClimb->GetClimbsUp() > State->ClimbsBefore;
        if (Feet >= 290.f && bClimbed) { return Finish(true, FString::Printf(TEXT("formation roof click climbs the gallery ladder; feet %.1f in %.1f s"), Feet, State->Time)); }
        if (State->Time >= 60.f) { return Finish(false, FString::Printf(TEXT("formation roof click reaches z>=290 by ladder in 60 s: feet %.1f climbs %d at %s"),
            Feet, bClimbed, *Leader->GetActorLocation().ToCompactString())); }
        return true;
    }
    void Run(const TArray<FString>&, UWorld* World)
    {
        SmokeUtils::StopLevelPmc(World);
        if (IConsoleVariable* Pressure = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Pressure.Enabled"))) { Pressure->Set(0, ECVF_SetByConsole); }
        const TSharedRef<FState> State = MakeShared<FState>();
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float) { return Tick(State); }));
        UE_LOG(LogCodexTactics, Display, TEXT("RoofLadderSmoke: start (X6 gallery roof 11510,-7621,300)"));
    }
    static FAutoConsoleCommandWithWorldAndArgs Command(TEXT("CodexTactics.RoofLadderSmoke"), TEXT("X6 gallery roof-via-ladder regression and link projection diagnostics."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}
#endif
