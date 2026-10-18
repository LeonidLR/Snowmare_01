// Dev-only headless check of the frost hound scent (game designer 2026-10-18, user decisions 1 / 2 / 4; ScentRules,
// docs/design/STEALTH_LURE.md "Scent") on L_PatrolTest (nothing saved; everything spawned at runtime):
//   Scripts/smoke_parallel.ps1 -Commands HoundScentSmoke
//   rendered still: UnrealEditor.exe <uproject> /Game/Maps/L_PatrolTest -game -RenderOffscreen -ResX=1280 -ResY=720
//       -ExecCmds="CodexTactics.HoundScentSmoke Shot"  -> Saved/Screenshots/hound_scent_sniff.png (the sniffing hound's marker)
// A guard hound (its sight switched off for the check, so only its nose works; the operative stands still: no footsteps) and the
// commander; the two other operatives lie 40 m away.
// A. Behind a solid 3 m wall at 3 m: the hound does NOT smell him (one wall cuts 6 m to 1.8 m) - no scent, no detection.
// C. In the open at 5 m: it sniffs (no detection, it stands still, the feed line); he backs off during the sniff -> at its end the
//    hound investigates the last scent point (a short scent search), then walks back to its post. Still undetected.
// D. He whistles behind the wall: the hound comes to the whistle and does not smell him through the wall; it catches the scent
//    only in open air round the wall end and SNIFFS first (no instant detection); he slips away -> nobody detected, back to its post.
// E. A lure during the sniff (re-target rule 2026-10-18): in the open at 5 m it sniffs him; the engineer whistles 10 m away on the
//    other side and slips away: the hound drops the scent, investigates the whistle and its nose is held on the way (no second
//    sniff although he still stands in open air); at the whistle point it searches, then walks back to its post.
// B. In the open at 5 m again, he stays: sniff (~2.5 s) -> it follows the scent slowly; the medic whistles 8 m away while it follows
//    within 3 m: it keeps following (StealthLureRules::ScentHoldsNoiseCm) -> detection only when close (<= ~2 m),
//    cause Smell -> the ambush fight starts.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/ScentRules.h"
#include "AI/StealthLureSubsystem.h"
#include "Camera/TacticalCameraPawn.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/WaveSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Data/EnemyPerception.h"
#include "Debug/SmokeUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFlow/LevelEncounterSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "UI/GameMessageSubsystem.h"
#include "UnrealClient.h"

namespace HoundScentSmoke
{
	constexpr float ScentSearchSeconds = 6.f;
	constexpr float LureSeconds = 8.f;

	struct FState
	{
		float Time = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		bool bShot = false;
		bool bShotTaken = false;
		FVector P = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		FVector Post = FVector::ZeroVector;
		FVector Far = FVector::ZeroVector;
		FVector StoodAt = FVector::ZeroVector;
		FStealthLureParams SavedLure;
		TWeakObjectPtr<AStaticMeshActor> Wall;
		TWeakObjectPtr<AEnemyCharacter> Hound;
		TWeakObjectPtr<AOperativeCharacter> Commander;
		/** Time the current sniff started (stage clock), -1 = none yet. */
		float SniffAt = -1.f;
		float FollowAt = -1.f;
		bool bSlippedAway = false;
		bool bEverOnScent = false;
		bool bScentThroughWall = false;
		float MaxSniffSpeed = 0.f;
		float MaxFollowSpeed = 0.f;
		float ScentStartDistance = 0.f;
		int32 ScentStartWalls = -1;
		/** Seconds the hound has stood still at its post after its search. */
		float SettleTime = 0.f;
		/** E / B: the whistle point, a re-sniff on the way, the whistle during the follow and the hound's reaction to it. */
		FVector WhistleAt = FVector::ZeroVector;
		bool bResniffed = false;
		bool bWhistledInFollow = false;
		bool bKeptScent = false;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State)
	{
		EnemyPerception::SetLure(State.SavedLure);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("HoundScentSmoke"));
		return false;
	}

	void Next(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.Time = 0.f;
		State.SniffAt = -1.f;
		State.FollowAt = -1.f;
		State.bSlippedAway = false;
		State.bEverOnScent = false;
		State.bScentThroughWall = false;
		State.MaxSniffSpeed = 0.f;
		State.MaxFollowSpeed = 0.f;
		State.ScentStartWalls = -1;
		State.SettleTime = 0.f;
		State.bResniffed = false;
	}

	AStaticMeshActor* SpawnWall(UWorld* World, const FVector& Centre, const FRotator& Rotation, const FVector& Scale)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (!Cube)
		{
			return nullptr;
		}
		AStaticMeshActor* Block = World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform(Rotation, Centre, Scale),
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Block)
		{
			return nullptr;
		}
		Block->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Block->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Block->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
		Block->GetStaticMeshComponent()->SetCanEverAffectNavigation(true);
		Block->FinishSpawning(FTransform(Rotation, Centre, Scale));
		return Block;
	}

	FVector Feet(const AActor* Actor)
	{
		const ACharacter* Character = Cast<ACharacter>(Actor);
		return Actor->GetActorLocation() - FVector(0.f, 0.f, Character ? Character->GetSimpleCollisionHalfHeight() : 0.f);
	}

	/** One operative to Ground, stopped, in Stance (no footsteps: he stands still). */
	void Place(UWorld* World, AOperativeCharacter* Member, const FVector& Ground, EOperativeStance Stance)
	{
		if (!Member)
		{
			return;
		}
		const FVector Spot = SmokeUtils::FreeSpot(World, Ground, Member);
		Member->StopOperative();
		Member->SetActorLocation(Spot, false, nullptr, ETeleportType::TeleportPhysics);
		Member->SetStance(Stance);
	}

	bool Detected(UWorld* World, const AEnemyCharacter* Hound)
	{
		const UStealthLureSubsystem* Lure = World->GetSubsystem<UStealthLureSubsystem>();
		return !Hound || !Hound->IsOnPatrol() || (Lure && Lure->IsSquadDetected());
	}

	bool PostedScentLine(UWorld* World)
	{
		const UGameMessageSubsystem* Messages = World->GetSubsystem<UGameMessageSubsystem>();
		const FString Line = ScentRules::GetCaughtScentLine().ToString();
		return Messages && Messages->GetHistory().ContainsByPredicate([&Line](const FGameMessage& Message) { return Message.Text.ToString() == Line; });
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += 0.1f;
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		ULevelEncounterSubsystem* Encounter = World->GetSubsystem<ULevelEncounterSubsystem>();
		UStealthLureSubsystem* Lure = World->GetSubsystem<UStealthLureSubsystem>();
		UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
		if (!Squad || !Flow || !Encounter || !Lure || !Waves)
		{
			Check(State, false, TEXT("squad, flow, encounter, lure and wave subsystems"));
			return Finish(State);
		}
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->ColdLevel = 0.f;
		}
		AEnemyCharacter* Hound = State.Hound.Get();
		AOperativeCharacter* Commander = State.Commander.Get();
		const float Distance = Hound && Commander ? FVector::Dist(Hound->GetActorLocation(), Commander->GetActorLocation()) : -1.f;
		switch (State.Stage)
		{
		case 0:
		{
			if (State.Time < 3.f)
			{
				return true;
			}
			const TArray<AOperativeCharacter*> Members = Squad->GetMembers();
			if (Members.Num() < 3 || !Members[0])
			{
				Check(State, false, TEXT("three operatives"));
				return Finish(State);
			}
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->Destroy(); // the user's map may hold its own enemies: start from a known set
			}
			State.SavedLure = EnemyPerception::GetLure();
			FStealthLureParams Shortened = State.SavedLure;
			Shortened.InvestigateSeconds = LureSeconds;
			EnemyPerception::SetLure(Shortened);
			Lure->ResetCooldowns();
			Commander = Members[0];
			State.Commander = Commander;
			Squad->SetLeader(Commander);
			Squad->SetSquadPosture(ESquadFirePosture::Passive); // nobody opens fire on the hound
			State.F = Commander->GetActorForwardVector().GetSafeNormal2D();
			State.R = FVector::CrossProduct(FVector::UpVector, State.F);
			State.P = Feet(Commander);
			State.Far = State.P - State.F * 4000.f;
			Place(World, Commander, State.P, EOperativeStance::Standing);
			for (int32 Index = 1; Index < Members.Num(); ++Index)
			{
				Place(World, Members[Index], State.Far + State.R * (300.f * Index), EOperativeStance::Prone);
			}
			// A 3 m high, 12 m wide wall 1.5 m ahead; the hound guards 3 m ahead (beyond it), its sight off: only its nose works.
			State.Wall = SpawnWall(World, State.P + State.F * 150.f + FVector(0.f, 0.f, 150.f), State.F.Rotation(), FVector(0.4f, 12.f, 3.f));
			const FVector Spot = SmokeUtils::FreeSpot(World, State.P + State.F * 300.f);
			Hound = Waves->SpawnEnemy(EEnemyArchetype::FrostHound, Spot + FVector(0.f, 0.f, 10.f), (-State.F).Rotation());
			if (!State.Wall.IsValid() || !Hound)
			{
				Check(State, false, TEXT("wall and hound spawned"));
				return Finish(State);
			}
			Encounter->MarkLevelEnemy(Hound); // a placed guard
			FEnemyPerceptionParams Nose = Hound->GetPerception();
			Nose.SightRangeCm = 0.f;
			Nose.ProximityCm = 0.f;
			Nose.ScentSearchSeconds = ScentSearchSeconds;
			Hound->PerceptionOverride = Nose;
			Hound->bOverridePerception = true;
			State.Hound = Hound;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: hound smell %.0f cm, x%.2f per wall, detects within %.0f cm, sniffs %.1f s"), Nose.SmellRadiusCm,
				Nose.SmellOcclusionPerWall, Nose.SmellDetectCm, Nose.SniffSeconds);
			Next(State, 1);
			return true;
		}
		case 1:
		{
			// A. Behind the solid wall at 3 m.
			if (!Hound || !Commander)
			{
				Check(State, false, TEXT("stage A hound / commander"));
				return Finish(State);
			}
			if (State.Time < 1.f)
			{
				State.Post = Hound->GetGuardPostLocation();
				return true;
			}
			State.bEverOnScent |= Hound->IsOnScent();
			if (State.Time < 4.f && !Detected(World, Hound))
			{
				return true;
			}
			const int32 Walls = Hound->CountScentWallsTo(*Commander);
			Check(State, Hound->IsGuardingPost(), TEXT("the hound guards its post"));
			Check(State, Distance > 200.f && Distance < 420.f && Walls >= 1, FString::Printf(TEXT("A: behind the wall at %.0f cm, %d wall(s) on the scent line"), Distance, Walls));
			Check(State, !State.bEverOnScent && !Detected(World, Hound), TEXT("A: behind a solid wall at 3 m the hound does not smell him (no sniff, no detection)"));
			Check(State, Flow->GetPhase() == ECodexGamePhase::Exploration && Encounter->GetAmbushStarts() == 0, TEXT("A: no fight"));
			// C. In the open at 5 m, beyond the wall's side with the hound.
			State.StoodAt = Hound->GetActorLocation() + State.R * 500.f;
			Place(World, Commander, State.StoodAt - FVector(0.f, 0.f, Hound->GetSimpleCollisionHalfHeight()), EOperativeStance::Standing);
			State.StoodAt = Commander->GetActorLocation();
			Next(State, 2);
			return true;
		}
		case 2:
		{
			// C. Sniff, then he backs off during it.
			if (!Hound || !Commander || Detected(World, Hound))
			{
				Check(State, false, FString::Printf(TEXT("C: the hound did not detect him (%.0f cm)"), Distance));
				return Finish(State);
			}
			if (State.SniffAt < 0.f)
			{
				if (Hound->GetScentPhase() == EHoundScentPhase::Sniffing)
				{
					State.SniffAt = State.Time;
					Check(State, Distance > 400.f && Distance < 600.f, FString::Printf(TEXT("C: in the open at %.0f cm the hound caught the scent (%.1f s)"), Distance, State.Time));
					Check(State, PostedScentLine(World), TEXT("C: feed line «The hound caught a scent.»"));
				}
				else if (State.Time > 3.f)
				{
					Check(State, false, FString::Printf(TEXT("C: the hound catches the scent in the open at 5 m (phase %d, %.0f cm)"),
						static_cast<int32>(Hound->GetScentPhase()), Distance));
					return Finish(State);
				}
				return true;
			}
			const float Sniffing = State.Time - State.SniffAt;
			if (Sniffing > 0.4f && Hound->GetScentPhase() == EHoundScentPhase::Sniffing)
			{
				State.MaxSniffSpeed = FMath::Max(State.MaxSniffSpeed, static_cast<float>(Hound->GetVelocity().Size2D()));
			}
			if (!State.bSlippedAway && Sniffing >= 1.f)
			{
				Check(State, Hound->GetScentPhase() == EHoundScentPhase::Sniffing && Hound->IsOnPatrol(), TEXT("C: 1 s into the sniff: still sniffing, not detected"));
				Place(World, Commander, State.Far - State.R * 300.f, EOperativeStance::Prone);
				State.bSlippedAway = true;
				return true;
			}
			if (State.bSlippedAway && !Hound->IsInvestigatingScent())
			{
				if (Sniffing > 6.f)
				{
					Check(State, false, FString::Printf(TEXT("C: the hound investigates the last scent point (phase %d, searching %d)"),
						static_cast<int32>(Hound->GetScentPhase()), Hound->IsSearching() ? 1 : 0));
					return Finish(State);
				}
				return true;
			}
			if (!State.bSlippedAway)
			{
				return true;
			}
			Check(State, State.MaxSniffSpeed < 40.f, FString::Printf(TEXT("C: it stood still while sniffing (max %.0f cm/s)"), State.MaxSniffSpeed));
			Check(State, Sniffing >= 2.f && Sniffing < 4.f, FString::Printf(TEXT("C: the sniff lasted ~2.5 s (%.1f s)"), Sniffing));
			Check(State, FVector::Dist2D(Hound->GetSearchOrigin(), State.StoodAt) < 100.f,
				FString::Printf(TEXT("C: it investigates where he stood (%.0f cm off)"), FVector::Dist2D(Hound->GetSearchOrigin(), State.StoodAt)));
			Next(State, 3);
			return true;
		}
		case 3:
		{
			// C. The short scent search, then back to its post.
			if (!Hound || Detected(World, Hound))
			{
				Check(State, false, TEXT("C: still undetected during the scent search"));
				return Finish(State);
			}
			const float Off = FVector::Dist2D(Hound->GetActorLocation(), State.Post);
			const bool bHome = !Hound->IsSearching() && !Hound->IsOnScent() && Hound->IsGuardingPost() && Off < 210.f
				&& Hound->GetVelocity().Size2D() < 10.f; // settled at its post (a guard walks back from beyond 2 m, to 0.8 m)
			State.SettleTime = bHome ? State.SettleTime + 0.1f : 0.f;
			const bool bBack = State.SettleTime >= 1.f;
			if (!bBack && State.Time < 30.f)
			{
				return true;
			}
			Check(State, bBack, FString::Printf(TEXT("C: search over (%.1f s), the hound walked back to its post (%.0f cm off)"), State.Time, Off));
			Check(State, Flow->GetPhase() == ECodexGamePhase::Exploration && Encounter->GetAmbushStarts() == 0, TEXT("C: still no fight"));
			// D. He stands behind the wall again and whistles.
			Place(World, Commander, State.P, EOperativeStance::Standing);
			Next(State, 4);
			return true;
		}
		case 4:
		{
			if (!Hound || !Commander)
			{
				Check(State, false, TEXT("D: hound / commander"));
				return Finish(State);
			}
			if (State.Time < 1.f)
			{
				return true;
			}
			Check(State, !Hound->IsOnScent() && Hound->IsOnPatrol(), FString::Printf(TEXT("D: behind the wall (%.0f cm): no scent"), Distance));
			Check(State, Lure->Whistle(Commander), TEXT("D: he whistles"));
			Check(State, Hound->IsInvestigatingLure(), TEXT("D: the hound comes to the whistle"));
			Next(State, 5);
			return true;
		}
		case 5:
		{
			// D. Its walk round the wall: no scent through it, a sniff (not a detection) once in open air, then he slips away.
			if (!Hound || !Commander)
			{
				Check(State, false, TEXT("D: hound / commander"));
				return Finish(State);
			}
			if (Detected(World, Hound))
			{
				Check(State, false, FString::Printf(TEXT("D: the hound that came to the whistle did not detect the whistler (%.1f s, %.0f cm, cause %d)"),
					State.Time, Distance, Hound->HasBrokenPatrol() ? static_cast<int32>(Hound->GetLastPatrolBreakCause()) : -1));
				return Finish(State);
			}
			if (!State.bEverOnScent && Hound->IsOnScent())
			{
				State.bEverOnScent = true;
				State.SniffAt = State.Time;
				State.ScentStartWalls = Hound->CountScentWallsTo(*Commander);
				State.ScentStartDistance = Distance;
				State.bScentThroughWall = State.ScentStartWalls > 0 && Distance > 230.f;
			}
			if (!State.bEverOnScent)
			{
				if (State.Time > 20.f)
				{
					Check(State, false, TEXT("D: the hound catches his scent once it is round the wall"));
					return Finish(State);
				}
				return true;
			}
			if (!State.bSlippedAway && State.Time - State.SniffAt >= 0.5f)
			{
				Check(State, State.SniffAt >= 1.f, FString::Printf(TEXT("D: not at once - the scent came %.1f s after the whistle"), State.SniffAt));
				Check(State, !State.bScentThroughWall, FString::Printf(TEXT("D: the scent was caught in open air, not through the wall (%d wall(s), %.0f cm)"),
					State.ScentStartWalls, State.ScentStartDistance));
				Check(State, Hound->GetScentPhase() == EHoundScentPhase::Sniffing, TEXT("D: it sniffs first (time to back off)"));
				Place(World, Commander, State.Far - State.R * 300.f, EOperativeStance::Prone);
				State.bSlippedAway = true;
				Next(State, 6);
			}
			return true;
		}
		case 6:
		{
			if (!Hound || Detected(World, Hound))
			{
				Check(State, false, TEXT("D: still undetected after he slipped away"));
				return Finish(State);
			}
			const float Off = FVector::Dist2D(Hound->GetActorLocation(), State.Post);
			const bool bHome = !Hound->IsSearching() && !Hound->IsOnScent() && Hound->IsGuardingPost() && Off < 210.f
				&& Hound->GetVelocity().Size2D() < 10.f; // settled at its post (a guard walks back from beyond 2 m, to 0.8 m)
			State.SettleTime = bHome ? State.SettleTime + 0.1f : 0.f;
			const bool bBack = State.SettleTime >= 1.f;
			if (!bBack && State.Time < 45.f)
			{
				return true;
			}
			Check(State, bBack, FString::Printf(TEXT("D: investigation over (%.1f s), back at its post (%.0f cm off)"), State.Time, Off));
			Check(State, Flow->GetPhase() == ECodexGamePhase::Exploration && Encounter->GetAmbushStarts() == 0, TEXT("D: no fight"));
			// E. In the open at 5 m: it sniffs; a whistle elsewhere during the sniff.
			Place(World, Commander, Hound->GetActorLocation() + State.R * 500.f - FVector(0.f, 0.f, Hound->GetSimpleCollisionHalfHeight()),
				EOperativeStance::Standing);
			Next(State, 60);
			return true;
		}
		case 60:
		{
			AOperativeCharacter* Engineer = Squad->GetMembers().IsValidIndex(1) ? Squad->GetMembers()[1] : nullptr;
			if (!Hound || !Commander || !Engineer || Detected(World, Hound))
			{
				Check(State, false, TEXT("E: hound, commander, engineer, nobody detected"));
				return Finish(State);
			}
			if (Hound->GetScentPhase() != EHoundScentPhase::Sniffing)
			{
				if (State.Time > 3.f)
				{
					Check(State, false, FString::Printf(TEXT("E: the hound sniffs him in the open (phase %d, %.0f cm)"), static_cast<int32>(Hound->GetScentPhase()), Distance));
					return Finish(State);
				}
				return true;
			}
			if (State.SniffAt < 0.f)
			{
				State.SniffAt = State.Time;
				// The engineer 10 m away on the other side (open air, far from the commander's scent).
				Place(World, Engineer, Hound->GetActorLocation() - State.R * 1000.f - FVector(0.f, 0.f, Hound->GetSimpleCollisionHalfHeight()), EOperativeStance::Standing);
				return true;
			}
			if (State.Time - State.SniffAt < 0.5f)
			{
				return true;
			}
			Check(State, Hound->GetScentPhase() == EHoundScentPhase::Sniffing, TEXT("E: 0.5 s into the sniff"));
			State.WhistleAt = Feet(Engineer);
			Check(State, Lure->Whistle(Engineer), TEXT("E: the engineer whistles 10 m away during the sniff"));
			Check(State, !Hound->IsOnScent() && Hound->IsInvestigatingLure() && FVector::Dist2D(Hound->GetSearchOrigin(), State.WhistleAt) < 200.f,
				FString::Printf(TEXT("E: the noise wins over the sniff: scent dropped, it investigates the whistle (phase %d, origin %.0f cm off)"),
					static_cast<int32>(Hound->GetScentPhase()), FVector::Dist2D(Hound->GetSearchOrigin(), State.WhistleAt)));
			Place(World, Engineer, State.Far + State.R * 900.f, EOperativeStance::Prone); // he slips away; the commander stays in the open
			Next(State, 61);
			return true;
		}
		case 61:
		{
			// E. On its way to the whistle point its nose is held: no second sniff of the commander standing in open air.
			if (!Hound || Detected(World, Hound))
			{
				Check(State, false, TEXT("E: not detected on the way to the whistle"));
				return Finish(State);
			}
			State.bResniffed |= Hound->IsOnScent();
			if (!Hound->HasReachedSearchOrigin() && State.Time < 12.f)
			{
				return true;
			}
			Check(State, Hound->IsInvestigatingLure() && Hound->HasReachedSearchOrigin(),
				FString::Printf(TEXT("E: it reached the whistle point and searches there (%.1f s, %.0f cm off it)"), State.Time,
					FVector::Dist2D(Hound->GetActorLocation(), State.WhistleAt)));
			Check(State, !State.bResniffed, TEXT("E: no second sniff on the way (its nose is held until it reaches the noise)"));
			Place(World, Commander, State.Far - State.R * 300.f, EOperativeStance::Prone);
			Next(State, 62);
			return true;
		}
		case 62:
		{
			if (!Hound || Detected(World, Hound))
			{
				Check(State, false, TEXT("E: still undetected after the lure"));
				return Finish(State);
			}
			const float Off = FVector::Dist2D(Hound->GetActorLocation(), State.Post);
			const bool bHome = !Hound->IsSearching() && !Hound->IsOnScent() && Hound->IsGuardingPost() && Off < 210.f && Hound->GetVelocity().Size2D() < 10.f;
			State.SettleTime = bHome ? State.SettleTime + 0.1f : 0.f;
			const bool bBack = State.SettleTime >= 1.f;
			if (!bBack && State.Time < 45.f)
			{
				return true;
			}
			Check(State, bBack, FString::Printf(TEXT("E: investigation over (%.1f s), back at its post (%.0f cm off)"), State.Time, Off));
			Check(State, Flow->GetPhase() == ECodexGamePhase::Exploration && Encounter->GetAmbushStarts() == 0, TEXT("E: no fight"));
			// B. In the open at 5 m; this time he stays.
			Place(World, Commander, Hound->GetActorLocation() + State.R * 500.f - FVector(0.f, 0.f, Hound->GetSimpleCollisionHalfHeight()),
				EOperativeStance::Standing);
			Next(State, 7);
			return true;
		}
		case 7:
		{
			// B. Sniff -> follow slowly -> detection only when close.
			if (!Hound || !Commander)
			{
				Check(State, false, TEXT("B: hound / commander"));
				return Finish(State);
			}
			const EHoundScentPhase Phase = Hound->GetScentPhase();
			if (State.SniffAt < 0.f && Phase == EHoundScentPhase::Sniffing)
			{
				State.SniffAt = State.Time;
				State.ScentStartDistance = Distance;
			}
			if (State.FollowAt < 0.f && Phase == EHoundScentPhase::Following)
			{
				State.FollowAt = State.Time;
			}
			if (Phase == EHoundScentPhase::Sniffing && State.SniffAt >= 0.f && State.Time - State.SniffAt > 0.4f)
			{
				State.MaxSniffSpeed = FMath::Max(State.MaxSniffSpeed, static_cast<float>(Hound->GetVelocity().Size2D()));
			}
			if (Phase == EHoundScentPhase::Following && State.Time - State.FollowAt > 0.5f)
			{
				State.MaxFollowSpeed = FMath::Max(State.MaxFollowSpeed, static_cast<float>(Hound->GetVelocity().Size2D()));
			}
			// A whistle while it follows him within 3 m: the nose wins (it keeps following).
			AOperativeCharacter* Medic = Squad->GetMembers().IsValidIndex(2) ? Squad->GetMembers()[2] : nullptr;
			if (Phase == EHoundScentPhase::Following && !State.bWhistledInFollow && Medic && Distance > 0.f && Distance <= 290.f)
			{
				State.bWhistledInFollow = true;
				Place(World, Medic, Hound->GetActorLocation() - State.R * 800.f - FVector(0.f, 0.f, Hound->GetSimpleCollisionHalfHeight()), EOperativeStance::Prone);
				const bool bWhistled = Lure->Whistle(Medic);
				State.bKeptScent = bWhistled && Hound->GetScentPhase() == EHoundScentPhase::Following && !Hound->IsInvestigatingLure()
					&& !Lure->GetLastListeners().Contains(State.Hound);
				Check(State, State.bKeptScent, FString::Printf(TEXT("B: a whistle 8 m away while it follows him at %.0f cm: it keeps following (whistled %d, phase %d, lure %d)"),
					Distance, bWhistled ? 1 : 0, static_cast<int32>(Hound->GetScentPhase()), Hound->IsInvestigatingLure() ? 1 : 0));
			}
			if (State.bShot && !State.bShotTaken && State.SniffAt >= 0.f)
			{
				APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
				if (ATacticalCameraPawn* Camera = PC ? PC->GetPawn<ATacticalCameraPawn>() : nullptr)
				{
					Camera->SnapFocusOnTarget(Hound, 1600.f);
				}
				if (State.Time - State.SniffAt > 0.8f)
				{
					const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("hound_scent_sniff.png");
					FScreenshotRequest::RequestScreenshot(Path, true, false);
					UE_LOG(LogCodexTactics, Display, TEXT("Smoke screenshot %s"), *Path);
					State.bShotTaken = true;
				}
			}
			if (Hound->IsOnPatrol())
			{
				if (State.Time > 20.f)
				{
					Check(State, false, FString::Printf(TEXT("B: the hound detects him in the end (phase %d, %.0f cm)"), static_cast<int32>(Phase), Distance));
					return Finish(State);
				}
				return true;
			}
			// Broke its patrol now.
			const float Since = State.SniffAt >= 0.f ? State.Time - State.SniffAt : -1.f;
			Check(State, State.SniffAt >= 0.f && State.ScentStartDistance > 400.f,
				FString::Printf(TEXT("B: at %.0f cm in the open it sniffed first"), State.ScentStartDistance));
			Check(State, State.MaxSniffSpeed < 40.f, FString::Printf(TEXT("B: stood still while sniffing (max %.0f cm/s)"), State.MaxSniffSpeed));
			Check(State, State.FollowAt >= 0.f && State.FollowAt - State.SniffAt >= 2.f && State.FollowAt - State.SniffAt < 3.5f,
				FString::Printf(TEXT("B: then followed the scent after %.1f s of sniffing"), State.FollowAt - State.SniffAt));
			Check(State, State.MaxFollowSpeed > 50.f && State.MaxFollowSpeed < 230.f,
				FString::Printf(TEXT("B: a cautious approach, not a charge (max %.0f cm/s; the hound runs ~540)"), State.MaxFollowSpeed));
			Check(State, Hound->HasBrokenPatrol() && Hound->GetLastPatrolBreakCause() == EPatrolAlertCause::Smell,
				FString::Printf(TEXT("B: detected by its nose (cause %d)"), static_cast<int32>(Hound->GetLastPatrolBreakCause())));
			Check(State, Distance >= 0.f && Distance <= 260.f, FString::Printf(TEXT("B: only when close (%.0f cm, %.1f s after the sniff began)"), Distance, Since));
			Check(State, State.bWhistledInFollow, TEXT("B: the whistle during the close follow was tried"));
			Next(State, 8);
			return true;
		}
		case 8:
		{
			if (Flow->GetPhase() != ECodexGamePhase::WaveCombat && State.Time < 5.f)
			{
				return true;
			}
			Check(State, Flow->GetPhase() == ECodexGamePhase::WaveCombat && Encounter->GetAmbushStarts() == 1,
				FString::Printf(TEXT("B: the nose's detection starts the ambush fight (%s)"), *UEnum::GetValueAsString(Flow->GetPhase())));
			return Finish(State);
		}
		default:
			return Finish(State);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		State->bShot = Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("Shot"), ESearchCase::IgnoreCase); });
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), 0.1f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.HoundScentSmoke"),
		TEXT("Dev check (L_PatrolTest): hound scent - no smell through a solid wall at 3 m; in the open it sniffs (marker, feed), follows slowly, detects only close; backing off during the sniff = it searches the last scent point and returns; a hound lured by a whistle does not smell the whistler through the wall; a whistle during the sniff wins, during a close follow it does not. 'Shot' saves hound_scent_sniff.png."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
