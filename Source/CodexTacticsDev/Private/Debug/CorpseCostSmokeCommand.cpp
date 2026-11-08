// Dev-only measurement of what kept enemy corpses cost (game designer 2026-11-08 "corpses stay"; UCorpseSubsystem) on L_Gen_Depot_1, RENDERED
// (a real RHI offscreen 1280x720, test_map.json "rendered"):
//   Scripts/smoke_parallel.ps1 -Commands CorpseCostSmoke
// Bodies of hounds / frostbitten / Military Zombies / cutters are laid in a spiral 2-16 m round the squad (in the camera view) and measured
// for 240 frames each: 0 bodies, 50 frozen, 150 frozen, then 150 NOT frozen (Codex.Corpse.Freeze 0: the cost freezing saves). Per sample:
// wall-clock frame ms (mean / p95), game / render thread ms, GPU ms, draw calls, used physical memory. Lines "[CorpseCost] ..."; PASS when
// every sample was taken, the 100 bodies from 50 to 150 frozen add < 0.5 ms of game-thread time (the first bodies also load the enemy
// Blueprints' assets: 0 -> 50 is no per-body cost) and freezing saves at least 3 ms against the same 150 bodies left ticking.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/TacticalCameraPawn.h"
#include "Characters/CorpseSubsystem.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/CombatPressureRules.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "DynamicRHI.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMemory.h"
#include "NavigationSystem.h"
#include "RenderTimer.h"
#include "RHI.h"
#include "RHIStats.h"

namespace CorpseCostSmoke
{
	constexpr int32 SampleFrames = 600;
	constexpr int32 WarmupFrames = 30;

	struct FSample
	{
		FString Label;
		int32 Bodies = 0;
		int32 Frozen = 0;
		int32 InView = 0;
		/** Bodies whose root projects inside the viewport (the camera frames them). */
		int32 OnScreen = 0;
		double FrameMs = 0.0;
		double FrameP95Ms = 0.0;
		double GameMs = 0.0;
		double RenderMs = 0.0;
		double GpuMs = 0.0;
		double Draws = 0.0;
		double MemoryMB = 0.0;
	};

	struct FState
	{
		int32 Stage = 0;
		int32 Frame = 0;
		double LastTime = 0.0;
		double WaitUntil = 0.0;
		int32 Spawned = 0;
		TArray<double> Frames;
		FSample Current;
		TArray<FSample> Samples;
		TArray<TWeakObjectPtr<AEnemyCharacter>> Pending;
		int32 Failures = 0;
	};

	void SetCVar(const TCHAR* Name, float Value)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Variable->Set(Value, ECVF_SetByConsole);
		}
	}

	bool Finish(FState& State, bool bComplete)
	{
		SetCVar(TEXT("Codex.Corpse.Cap"), -1.f);
		SetCVar(TEXT("Codex.Corpse.Freeze"), 1.f);
		SetCVar(TEXT("Codex.Pressure.Enabled"), -1.f);
		const FSample* Frozen50 = State.Samples.FindByPredicate([](const FSample& S) { return S.Bodies == 50 && S.Frozen >= 45; });
		const FSample* Frozen150 = State.Samples.FindByPredicate([](const FSample& S) { return S.Bodies == 150 && S.Frozen >= 140; });
		const FSample* Ticking150 = State.Samples.FindByPredicate([](const FSample& S) { return S.Bodies >= 140 && S.Frozen == 0; });
		bool bOk = bComplete && State.Samples.Num() == 4 && Frozen50 && Frozen150 && Ticking150;
		if (bOk)
		{
			const double PerHundred = Frozen150->GameMs - Frozen50->GameMs;
			const double Saved = Ticking150->GameMs - Frozen150->GameMs;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: 100 more frozen bodies (50 -> 150) add %.2f ms of game-thread time (budget 0.5 ms)"),
				PerHundred < 0.5 ? TEXT("ok  ") : TEXT("FAIL"), PerHundred);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: freezing saves %.2f ms of game-thread time on 150 bodies (at least 3 ms)"),
				Saved >= 3.0 ? TEXT("ok  ") : TEXT("FAIL"), Saved);
			bOk &= PerHundred < 0.5 && Saved >= 3.0;
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), bOk && State.Failures == 0 ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("CorpseCostSmoke"));
		return false;
	}

	/** Lays Count more bodies in the spiral round the leader (spawned and killed at once). */
	void LayBodies(UWorld* World, FState& State, int32 Count)
	{
		UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
		const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		const AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (!Waves || !Leader)
		{
			return;
		}
		const FPressureEnemyType Kinds[] = { FPressureEnemyType(EEnemyArchetype::FrostHound), FPressureEnemyType(EEnemyArchetype::Frostbitten),
			FPressureEnemyType(EEnemyArchetype::Frostbitten, CombatPressureRules::MilitaryZombieVariant()), FPressureEnemyType(EEnemyArchetype::Cutter) };
		const FVector Centre = Leader->GetActorLocation();
		for (int32 Made = 0; Made < Count; ++Made)
		{
			const int32 Index = State.Spawned++;
			// Sunflower spiral, 2..16 m.
			const float Radius = 200.f + 1400.f * FMath::Sqrt(static_cast<float>(Index) / 150.f);
			const FVector Probe = Centre + FVector(Radius, 0.f, 0.f).RotateAngleAxis(137.50776f * Index, FVector::UpVector);
			FNavLocation OnNav;
			const FVector Spot = Nav && Nav->ProjectPointToNavigation(Probe, OnNav, FVector(200.f, 200.f, 500.f)) ? OnNav.Location + FVector(0.f, 0.f, 95.f) : Probe;
			const FPressureEnemyType& Kind = Kinds[Index % UE_ARRAY_COUNT(Kinds)];
			if (AEnemyCharacter* Enemy = Waves->SpawnEnemyVariant(Kind.Type, Spot, FRotator(0.f, 37.f * Index, 0.f), Kind.Variant))
			{
				State.Pending.Add(Enemy);
			}
		}
	}

	void KillPending(FState& State)
	{
		for (const TWeakObjectPtr<AEnemyCharacter>& Weak : State.Pending)
		{
			if (AEnemyCharacter* Enemy = Weak.Get(); Enemy && Enemy->GetHealthComponent())
			{
				FDamageSpec Spec;
				Spec.Amount = 1000000.f;
				Spec.ArmorPenetration = 1.f;
				Enemy->GetHealthComponent()->TakeDamage(Spec);
			}
		}
		State.Pending.Reset();
	}

	void BeginSample(FState& State, const FString& Label, const UCorpseSubsystem& Corpses)
	{
		State.Current = FSample();
		State.Current.Label = Label;
		State.Current.Bodies = Corpses.GetCorpseCount();
		State.Current.Frozen = Corpses.GetFrozenCount();
		const APlayerController* PC = Corpses.GetWorld() ? Corpses.GetWorld()->GetFirstPlayerController() : nullptr;
		int32 SizeX = 0;
		int32 SizeY = 0;
		if (PC)
		{
			PC->GetViewportSize(SizeX, SizeY);
		}
		for (const AEnemyCharacter* Body : Corpses.GetCorpses())
		{
			State.Current.InView += Body->GetMesh() && Body->GetMesh()->WasRecentlyRendered(0.2f) ? 1 : 0;
			FVector2D Screen;
			State.Current.OnScreen += PC && PC->ProjectWorldLocationToScreen(Body->GetActorLocation(), Screen)
				&& Screen.X >= 0.f && Screen.Y >= 0.f && Screen.X <= SizeX && Screen.Y <= SizeY ? 1 : 0;
		}
		State.Frames.Reset();
		State.Frame = 0;
	}

	/** One frame of the running sample; true when it is complete (logged and stored). */
	bool SampleFrame(FState& State, double FrameMs)
	{
		++State.Frame;
		if (State.Frame <= WarmupFrames)
		{
			return false;
		}
		State.Frames.Add(FrameMs);
		State.Current.GameMs += FPlatformTime::ToMilliseconds(GGameThreadTime);
		State.Current.RenderMs += FPlatformTime::ToMilliseconds(GRenderThreadTime);
		State.Current.GpuMs += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0));
		State.Current.Draws += GNumDrawCallsRHI[0];
		if (State.Frames.Num() < SampleFrames)
		{
			return false;
		}
		const double N = State.Frames.Num();
		double Sum = 0.0;
		for (const double Each : State.Frames)
		{
			Sum += Each;
		}
		State.Frames.Sort();
		FSample& S = State.Current;
		S.FrameMs = Sum / N;
		S.FrameP95Ms = State.Frames[FMath::Clamp(FMath::FloorToInt(N * 0.95), 0, State.Frames.Num() - 1)];
		S.GameMs /= N;
		S.RenderMs /= N;
		S.GpuMs /= N;
		S.Draws /= N;
		S.MemoryMB = FPlatformMemory::GetStats().UsedPhysical / (1024.0 * 1024.0);
		UE_LOG(LogCodexTactics, Display, TEXT("[CorpseCost] %-22s bodies %3d (frozen %3d, rendered %3d, on screen %3d): frame %.2f ms (p95 %.2f), game %.2f ms, render %.2f ms, "
			"gpu %.2f ms, draws %.0f, memory %.0f MB"), *S.Label, S.Bodies, S.Frozen, S.InView, S.OnScreen, S.FrameMs, S.FrameP95Ms, S.GameMs, S.RenderMs, S.GpuMs,
			S.Draws, S.MemoryMB);
		State.Samples.Add(S);
		return true;
	}

	bool Tick(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		const double Now = FPlatformTime::Seconds();
		const double FrameMs = State.LastTime > 0.0 ? (Now - State.LastTime) * 1000.0 : 0.0;
		State.LastTime = Now;
		UWorld* World = WeakWorld.Get();
		UCorpseSubsystem* Corpses = World ? World->GetSubsystem<UCorpseSubsystem>() : nullptr;
		const USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
		if (!World || !Corpses || !Squad)
		{
			return Finish(State, false);
		}
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->bTacticalCeaseFire = true;
			Member->HealthComponent->SetMaxHealth(100000.f, false);
			Member->HealthComponent->Heal(100000.f);
		}
		if (Now < State.WaitUntil)
		{
			return true;
		}
		auto WaitFor = [&State, Now](double Seconds) { State.WaitUntil = Now + Seconds; };
		auto Settled = [Corpses]() { return Corpses->GetFrozenCount() >= Corpses->GetCorpseCount(); };
		switch (State.Stage)
		{
		case 0: // the world settles; the tactical view on the leader (the bodies round him are in the frame)
			if (const APlayerController* PC = World->GetFirstPlayerController())
			{
				if (ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(PC->GetPawn()); Camera && Squad->GetLeader())
				{
					Camera->Config.EdgeScrollMargin = -1.f; // the offscreen cursor on the viewport edge must not pan the view
					Camera->SnapFocusOnTarget(Squad->GetLeader());
				}
			}
			SetCVar(TEXT("Codex.Pressure.Enabled"), 0.f);
			SetCVar(TEXT("Codex.Corpse.Cap"), 1000.f);
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->Destroy();
			}
			WaitFor(6.0);
			++State.Stage;
			return true;
		case 1:
			BeginSample(State, TEXT("no bodies"), *Corpses);
			++State.Stage;
			return true;
		case 2:
			if (SampleFrame(State, FrameMs))
			{
				LayBodies(World, State, 50);
				++State.Stage;
			}
			return true;
		case 3:
			KillPending(State);
			WaitFor(1.0);
			++State.Stage;
			return true;
		case 4:
		case 7:
		case 10:
			// Every body settled and frozen (at most the 8 s settle limit), plus a breath for the GC.
			if (!Settled() && Now - State.WaitUntil < 15.0)
			{
				return true;
			}
			WaitFor(2.0);
			++State.Stage;
			return true;
		case 5:
			BeginSample(State, TEXT("50 frozen"), *Corpses);
			++State.Stage;
			return true;
		case 6:
			if (SampleFrame(State, FrameMs))
			{
				LayBodies(World, State, 100);
				KillPending(State);
				WaitFor(1.0);
				++State.Stage;
			}
			return true;
		case 8:
			BeginSample(State, TEXT("150 frozen"), *Corpses);
			++State.Stage;
			return true;
		case 9:
			if (SampleFrame(State, FrameMs))
			{
				// The A/B: the same 150 bodies left ticking (no freeze).
				for (AEnemyCharacter* Body : Corpses->GetCorpses())
				{
					Body->Destroy();
				}
				SetCVar(TEXT("Codex.Corpse.Freeze"), 0.f);
				State.Spawned = 0;
				LayBodies(World, State, 150);
				KillPending(State);
				WaitFor(10.0); // the death clips end; nothing freezes
				State.Stage = 11;
			}
			return true;
		case 11:
			BeginSample(State, TEXT("150 NOT frozen (A/B)"), *Corpses);
			++State.Stage;
			return true;
		case 12:
			if (SampleFrame(State, FrameMs))
			{
				return Finish(State, true);
			}
			return true;
		default:
			return Finish(State, false);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Tick(WeakWorld, *State);
		}), 0.f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.CorpseCostSmoke"),
		TEXT("Dev measurement: frame / game / render / GPU time and memory with 0, 50, 150 frozen enemy corpses and 150 unfrozen (rendered run); then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
