// Dev-only headless check of the game designer's play-test bug 2026-11-09 (L_Gen_Depot_1, fix/corner-peek-standoff): "a PMC fighter and our
// fighter both peek out from behind a corner, but they don't shoot at each other". On L_MovementTest in a runtime arena:
//   Scripts/smoke_parallel.ps1 -Commands CornerStandoffSmoke -ExtraArgs "-Multiprocess"
// A 20 m x 6 m x 3 m block; our man (AGGRESSIVE) in cover on its south face at the west end, a PMC M16 fighter in cover on its north face
// at the west end, 21 m apart - each peeks round his corner along the block's west face (the designer's picture: both lean out, each inside
// the other's cone). Both are tough (no knockdown out of the peek) and nobody else can fire.
//  A "we see him first": our man peeks, then the fighter leans out -> both fire at each other within 4 s of both peeking; each round at a
//    peeking man is rolled at x0.5 (Codex.Cover.LeanHitMultiplier) - the smaller target;
//  B "he sees us first": the same with the fighter peeking first;
//  C (run first, before any round: a man fired at seeks cover by himself, #43) the corner still hides the rest: the fighter standing just behind his corner, nothing of him showing -> no round either way (4 s)
//    (in cover AT the corner he would lean out by himself in a fight - the corner hold - and the exchange of A follows);
//  D control: the fighter standing in the open by the west face -> both fire.
// Every 0.5 s a [Standoff] line per shooter: seen, his target, the shot line's verdict (AOperativeCharacter::DescribeShotLine: from / to /
// the blocker), posture, cover state, the fighter's order.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PmcCommanderRules.h"
#include "AI/PmcSquadSubsystem.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Data/WeaponDataAsset.h"
#include "Debug/SmokeUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Tactics/CoverRules.h"
#include "Tactics/CoverTraceRules.h"
#include "TimerManager.h"

namespace CornerStandoffSmoke
{
	constexpr float StepSeconds = 0.05f;
	constexpr float BlockNear = 200.f;   // the south face, along F
	constexpr float BlockFar = 2200.f;   // the north face
	constexpr float BlockDepth = 600.f;  // the west face at R 0, the east face at R 600
	constexpr float BlockHeight = 300.f;
	constexpr float ExchangeSeconds = 4.f;

	/** Run order: C first - before any round was fired (a man fired at seeks cover by himself, #43, and then holds his corner leaned out). */
	enum class EPhase : uint8
	{
		Hidden,     // C
		WeFirst,    // A
		HeFirst,    // B
		OpenControl // D
	};

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		FVector O = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		TWeakObjectPtr<AOperativeCharacter> Ours;
		TWeakObjectPtr<AOperativeCharacter> Hostile;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Others;
		TWeakObjectPtr<AStaticMeshActor> Block;
		FDelegateHandle OursHandle;
		FDelegateHandle HostileHandle;
		EPhase Phase = EPhase::Hidden;
		int32 Step = 0;
		float StepTime = 0.f;
		float NextLog = 0.f;
		/** Rounds fired at each other since both peek (this phase), and the worst incoming multiplier rolled on a peeking man. */
		int32 OursRounds = 0;
		int32 HostileRounds = 0;
		float OursFirstShot = -1.f;
		float HostileFirstShot = -1.f;
		float OursMultiplier = -1.f;
		float HostileMultiplier = -1.f;
		bool bCounting = false;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		if (AOperativeCharacter* Ours = State.Ours.Get(); Ours && State.OursHandle.IsValid())
		{
			Ours->OnWeaponFiredNative.Remove(State.OursHandle);
		}
		if (AOperativeCharacter* Hostile = State.Hostile.Get(); Hostile && State.HostileHandle.IsValid())
		{
			Hostile->OnWeaponFiredNative.Remove(State.HostileHandle);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("CornerStandoffSmoke"));
		return false;
	}

	FVector At(const FState& State, float Forward, float Right)
	{
		return State.O + State.F * Forward + State.R * Right;
	}

	void MakeTough(AOperativeCharacter* Operative)
	{
		if (Operative && Operative->HealthComponent)
		{
			Operative->HealthComponent->SetMaxHealth(100000.f, false);
			Operative->HealthComponent->Heal(100000.f);
		}
		if (USkeletalMeshComponent* Body = Operative ? Operative->GetMesh() : nullptr)
		{
			Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		}
	}

	const TCHAR* PhaseName(EPhase Phase)
	{
		switch (Phase)
		{
		case EPhase::WeFirst: return TEXT("A (we peek first)");
		case EPhase::HeFirst: return TEXT("B (he peeks first)");
		case EPhase::Hidden: return TEXT("C (he stands behind his corner)");
		default: return TEXT("D (he stands in the open, control)");
		}
	}

	/** One decision line of a shooter at his opponent. */
	void LogShooter(UWorld* World, const FState& State, const TCHAR* Who, AOperativeCharacter& Shooter, AOperativeCharacter& Target)
	{
		const UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
		const bool bSeen = Shooter.IsSeenByOwnSide(&Target);
		const FString Line = Shooter.DescribeShotLine(&Target);
		const AActor* Current = Shooter.GetCurrentCombatTarget();
		auto Local = [&State](const FVector& P) { const FVector D = P - State.O; return FString::Printf(TEXT("(F %.0f, R %.0f, Z %.0f)"), FVector::DotProduct(D, State.F), FVector::DotProduct(D, State.R), D.Z); };
		FVector Exposed = FVector::ZeroVector;
		const bool bExposed = Target.GetPeekExposedPoint(Exposed);
		UE_LOG(LogCodexTactics, Display, TEXT("[Standoff] %s %s at %s, cover fire origin %s, muzzle %s | target at %s, exposed %s"), Who, *Shooter.DisplayName.ToString(),
			*Local(Shooter.GetActorLocation()), *Local(Shooter.GetCoverFireOrigin()), *Local(Shooter.GetMuzzleLocation()), *Local(Target.GetActorLocation()),
			bExposed ? *Local(Exposed) : TEXT("-"));
		UE_LOG(LogCodexTactics, Display,
			TEXT("[Standoff] %s t=%.1f %s %s: seen %d, target %s, line {%s}, dist %.1f m, posture %d auto %d, hold %d, cover %d corner hold %d peeking %d aim %d leaning %d, corner target %d, residual %.0f, timer %.2f, clip %d, order %s, incoming x%.2f"),
			PhaseName(State.Phase), State.Time, Who, *Shooter.DisplayName.ToString(), bSeen ? 1 : 0, Current ? *Current->GetName() : TEXT("-"), *Line,
			FVector::Dist(Shooter.GetActorLocation(), Target.GetActorLocation()) / 100.f, static_cast<int32>(Shooter.GetFirePosture()), Shooter.MayAutoFireNow() ? 1 : 0,
			Shooter.bCoverHoldFire ? 1 : 0, Shooter.bInCover ? 1 : 0, Shooter.IsCornerHoldSpot() ? 1 : 0, Shooter.IsPeekingRoundCorner() ? 1 : 0,
			Shooter.IsCornerAimActive() ? 1 : 0, Shooter.bIsCornerLeaning ? 1 : 0, Shooter.IsCornerShotTarget(Target.GetActorLocation()) ? 1 : 0,
			Shooter.GetShotAimResidualDeg(Target.GetActorLocation()), Shooter.ShootTimer, Shooter.CurrentClip,
			Pmc && !Shooter.IsPlayerTeam() ? PmcCommanderRules::OrderName(Pmc->GetFighterOrder(&Shooter)) : TEXT("-"),
			Target.GetIncomingHitChanceMultiplier(&Shooter));
	}

	/** Orders Op into cover at the block's west end of the face at FaceF (Normal = out of that face), then a peek round the west corner. */
	bool OrderCover(UWorld* World, const FState& State, AOperativeCharacter& Op, float FaceF, const FVector& Out)
	{
		FCoverSlot Slot;
		const FVector Click = At(State, FaceF, 80.f) + Out * 30.f + FVector(0.f, 0.f, 90.f);
		return CoverTraceRules::FindCoverSlotAt(World, Click, -Out, Slot) && Op.OrderTakeCover(Slot, false) == EOperativeOrderResult::Accepted;
	}

	bool OrderPeek(UWorld* World, const FState& State, AOperativeCharacter& Op, float FaceF, const FVector& Out)
	{
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController());
		AStaticMeshActor* Block = State.Block.Get();
		if (!PC || !Block || !Op.bInCover)
		{
			return false;
		}
		const FHitResult EdgeHit(Block, Block->GetStaticMeshComponent(), At(State, FaceF, 20.f) + FVector(0.f, 0.f, 150.f), Out);
		ECoverFacing Side = ECoverFacing::Left;
		float Edge = 0.f;
		return PC->FindCornerPeekForHit(&Op, EdgeHit, Side, Edge) && Op.OrderCornerPeek(Side, Edge) == EOperativeOrderResult::Accepted;
	}

	bool IsPeekReady(const AOperativeCharacter& Op)
	{
		const UOperativeAnimInstance* Anim = Op.GetMesh() ? Cast<UOperativeAnimInstance>(Op.GetMesh()->GetAnimInstance()) : nullptr;
		return Op.IsPeekingRoundCorner() && Op.IsCornerAimActive() && (!Anim || Anim->IsCoverInFirePose());
	}

	void ResetCounters(FState& State)
	{
		State.OursRounds = 0;
		State.HostileRounds = 0;
		State.OursFirstShot = -1.f;
		State.HostileFirstShot = -1.f;
		State.OursMultiplier = -1.f;
		State.HostileMultiplier = -1.f;
		State.bCounting = false;
	}

	/** Out of cover and back to his spawn spot (the next phase starts from scratch). */
	void Reset(UWorld* World, AOperativeCharacter& Op, const FVector& Spot, const FVector& Facing)
	{
		if (Op.bInCover)
		{
			Op.LeaveCover(TEXT("CornerStandoffSmoke: next phase"));
		}
		Op.StopOperative();
		const FVector Free = SmokeUtils::FreeSpot(World, Spot, &Op);
		Op.TeleportTo(FVector(Free.X, Free.Y, Free.Z + 4.f), Facing.Rotation(), false, true);
	}

	bool Step(UWorld* World, FState& State)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
		if (!Squad || !Flow || !Pmc)
		{
			Check(State, false, TEXT("squad, game flow and the PMC subsystem present"));
			return Finish(State, false);
		}
		AOperativeCharacter* Ours = State.Ours.Get();
		AOperativeCharacter* Hostile = State.Hostile.Get();
		auto OurSpot = [&State]() { return At(State, BlockNear - 250.f, 120.f); };
		auto HisSpot = [&State]() { return At(State, BlockFar + 250.f, 120.f); };
		if (State.Stage == 0)
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			TArray<AOperativeCharacter*> Roster = Squad->GetMembers();
			Roster.Sort([](const AOperativeCharacter& A, const AOperativeCharacter& B) { return A.SquadIndex < B.SquadIndex; });
			if (Roster.Num() < 1)
			{
				Check(State, false, TEXT("a squad"));
				return Finish(State, false);
			}
			TArray<const AActor*> Ignore;
			for (const AOperativeCharacter* Member : Roster)
			{
				Ignore.Add(Member);
			}
			const FVector Feet = Roster[0]->GetActorLocation() - FVector(0.f, 0.f, Roster[0]->GetSimpleCollisionHalfHeight());
			const SmokeUtils::FClearArena Arena = SmokeUtils::FindClearArena(World, Feet, Roster[0]->GetActorForwardVector(),
				FBox2D(FVector2D(-600.f, -900.f), FVector2D(2800.f, 900.f)), Ignore);
			Check(State, Arena.bFound, FString::Printf(TEXT("an empty arena found (tried %d)"), Arena.Tried));
			if (!Arena.bFound)
			{
				return Finish(State, false);
			}
			State.O = Arena.Origin;
			State.F = Arena.Forward;
			State.R = Arena.Right;
			SmokeUtils::StartWaveFight(Flow);
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				SmokeUtils::ParkEnemyFarAway(*It);
				It->SetActorTickEnabled(false);
			}
			Squad->SetSquadPosture(ESquadFirePosture::Aggressive);
			Squad->SetFollowersHolding(true);
			for (int32 Index = 0; Index < Roster.Num(); ++Index)
			{
				AOperativeCharacter* Member = Roster[Index];
				Member->StopOperative();
				Member->ColdLevel = 0.f;
				MakeTough(Member);
				if (Index == 0)
				{
					State.Ours = Member;
					Reset(World, *Member, OurSpot(), State.F);
				}
				else
				{
					// The rest of the squad behind, out of the fight (only the corner man may fire).
					Member->bTacticalCeaseFire = true;
					const FVector Spot = SmokeUtils::FreeSpot(World, At(State, -450.f, 300.f + Index * 150.f), Member);
					Member->TeleportTo(FVector(Spot.X, Spot.Y, Spot.Z + 4.f), State.F.Rotation(), false, true);
					State.Others.Add(Member);
				}
			}
			const FVector Centre = At(State, (BlockNear + BlockFar) * 0.5f, BlockDepth * 0.5f);
			UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
			const FTransform Transform(State.F.Rotation(), FVector(Centre.X, Centre.Y, State.O.Z + BlockHeight * 0.5f),
				FVector(BlockFar - BlockNear, BlockDepth, BlockHeight) / 100.f);
			if (AStaticMeshActor* Block = Cube ? World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn) : nullptr)
			{
				Block->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
				Block->GetStaticMeshComponent()->SetStaticMesh(Cube);
				Block->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
				Block->GetStaticMeshComponent()->SetCanEverAffectNavigation(true);
				Block->FinishSpawning(Transform);
				State.Block = Block;
			}
			const TArray<AOperativeCharacter*> Spawned = Pmc->SpawnSquad(1, SmokeUtils::FreeSpot(World, HisSpot(), nullptr), (-State.F).Rotation(), 1, 61);
			if (!Spawned.IsEmpty())
			{
				AOperativeCharacter* Fighter = Spawned[0];
				Pmc->SetTeamCommanded(1, false); // no commander orders: the smoke places him; he fires by the honest-fire rules
				if (!Fighter->CurrentWeapon || Fighter->CurrentWeapon->WeaponId != TEXT("m16"))
				{
					Fighter->SwitchToWeaponById(TEXT("m16"));
				}
				MakeTough(Fighter);
				State.Hostile = Fighter;
			}
			Ours = State.Ours.Get();
			Hostile = State.Hostile.Get();
			Check(State, State.Block.IsValid() && Ours && Hostile, FString::Printf(TEXT("a 20 m x 6 m x 3 m block, our %s south of it, a PMC fighter (%s) north of it"),
				Ours ? *Ours->DisplayName.ToString() : TEXT("-"), Hostile && Hostile->CurrentWeapon ? *Hostile->CurrentWeapon->WeaponId : TEXT("-")));
			if (!Ours || !Hostile || !State.Block.IsValid())
			{
				return Finish(State, false);
			}
			// Infinite rounds both ways: a reload never hides a missing shot.
			if (IConsoleVariable* Ammo = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Cheat.InfiniteAmmo")))
			{
				Ammo->Set(1, ECVF_SetByConsole);
			}
			if (IConsoleVariable* Trace = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Debug.FireTrace")))
			{
				Trace->Set(1, ECVF_SetByConsole);
			}
			State.OursHandle = Ours->OnWeaponFiredNative.AddLambda([&State](AOperativeCharacter* Shooter, AActor* Target, bool)
			{
				if (State.bCounting && Target == State.Hostile.Get())
				{
					State.OursFirstShot = State.OursFirstShot < 0.f ? State.StepTime : State.OursFirstShot;
					++State.OursRounds;
					if (State.Hostile->IsPeekingRoundCorner())
					{
						State.HostileMultiplier = FMath::Max(State.HostileMultiplier, State.Hostile->GetIncomingHitChanceMultiplier(Shooter));
					}
				}
			});
			State.HostileHandle = Hostile->OnWeaponFiredNative.AddLambda([&State](AOperativeCharacter* Shooter, AActor* Target, bool)
			{
				if (State.bCounting && Target == State.Ours.Get())
				{
					State.HostileFirstShot = State.HostileFirstShot < 0.f ? State.StepTime : State.HostileFirstShot;
					++State.HostileRounds;
					if (State.Ours->IsPeekingRoundCorner())
					{
						State.OursMultiplier = FMath::Max(State.OursMultiplier, State.Ours->GetIncomingHitChanceMultiplier(Shooter));
					}
				}
			});
			State.Stage = 1;
			State.StageTime = 0.f;
			State.Step = 0;
			State.StepTime = 0.f;
			return true;
		}
		if (!Ours || !Hostile)
		{
			Check(State, false, TEXT("both shooters alive"));
			return Finish(State, false);
		}
		State.StepTime += StepSeconds;
		for (const TWeakObjectPtr<AOperativeCharacter>& Other : State.Others)
		{
			if (AOperativeCharacter* Mate = Other.Get())
			{
				Mate->bTacticalCeaseFire = true;
			}
		}
		if (State.Time >= State.NextLog)
		{
			State.NextLog = State.Time + 0.5f;
			LogShooter(World, State, TEXT("ours"), *Ours, *Hostile);
			LogShooter(World, State, TEXT("PMC"), *Hostile, *Ours);
		}
		const bool bHePeeks = State.Phase == EPhase::WeFirst || State.Phase == EPhase::HeFirst;
		AOperativeCharacter* First = State.Phase == EPhase::HeFirst ? Hostile : Ours;
		AOperativeCharacter* Second = State.Phase == EPhase::HeFirst ? Ours : Hostile;
		auto FaceOf = [Ours](const AOperativeCharacter* Op) { return Op == Ours ? BlockNear : BlockFar; };
		auto OutOf = [&State, Ours](const AOperativeCharacter* Op) { return Op == Ours ? -State.F : State.F; };
		switch (State.Step)
		{
		case 0:
		{
			// Both into cover at their west ends (the hostile in the open for the control D), nobody fires yet.
			Ours->bTacticalCeaseFire = true;
			Hostile->bTacticalCeaseFire = true;
			ResetCounters(State);
			bool bOk = OrderCover(World, State, *Ours, BlockNear, -State.F);
			if (State.Phase == EPhase::OpenControl || State.Phase == EPhase::Hidden)
			{
				// D: in the open by the west face; C: just behind his corner (60 cm past the north face, 50 cm inside the west face), out of
				// cover - at a corner in cover he would lean out by himself in a fight (the corner hold), here nothing of him shows.
				const FVector Open = State.Phase == EPhase::OpenControl ? At(State, BlockFar - 300.f, -90.f) : At(State, BlockFar + 60.f, 50.f);
				const FVector Free = SmokeUtils::FreeSpot(World, Open, Hostile);
				Hostile->StopOperative();
				Hostile->TeleportTo(FVector(Free.X, Free.Y, Free.Z + 4.f), (-State.F).Rotation(), false, true);
			}
			else
			{
				bOk &= OrderCover(World, State, *Hostile, BlockFar, State.F);
			}
			Check(State, bOk, FString::Printf(TEXT("%s: both take cover at the block's west end"), PhaseName(State.Phase)));
			if (!bOk)
			{
				return Finish(State, false);
			}
			State.Step = 1;
			State.StepTime = 0.f;
			return true;
		}
		case 1:
		{
			const bool bHisCover = State.Phase == EPhase::OpenControl || State.Phase == EPhase::Hidden || (Hostile->bInCover && !Hostile->bShimmying);
			if ((!(Ours->bInCover && !Ours->bShimmying && bHisCover) || State.StepTime < 2.f) && State.StepTime < 20.f)
			{
				return true;
			}
			Check(State, Ours->bInCover && bHisCover, FString::Printf(TEXT("%s: both in cover at the corners (ours %d, his %d) after %.1f s"), PhaseName(State.Phase),
				Ours->bInCover ? 1 : 0, Hostile->bInCover ? 1 : 0, State.StepTime));
			// The first peek (C / D: only ours).
			const bool bOk = OrderPeek(World, State, *First, FaceOf(First), OutOf(First));
			Check(State, bOk, FString::Printf(TEXT("%s: %s peeks round the west corner"), PhaseName(State.Phase), *First->DisplayName.ToString()));
			if (!bOk)
			{
				return Finish(State, false);
			}
			State.Step = 2;
			State.StepTime = 0.f;
			return true;
		}
		case 2:
		{
			if (!IsPeekReady(*First) && State.StepTime < 12.f)
			{
				return true;
			}
			Check(State, IsPeekReady(*First), FString::Printf(TEXT("%s: %s holds the corner aim (%.1f s)"), PhaseName(State.Phase), *First->DisplayName.ToString(), State.StepTime));
			// Fire allowed now; the second leans out 1 s later.
			Ours->bTacticalCeaseFire = false;
			Hostile->bTacticalCeaseFire = false;
			State.Step = 6;
			State.StepTime = 0.f;
			return true;
		}
		case 6:
		{
			if (bHePeeks)
			{
				if (State.StepTime < 1.f)
				{
					return true;
				}
				const bool bOk = OrderPeek(World, State, *Second, FaceOf(Second), OutOf(Second));
				Check(State, bOk, FString::Printf(TEXT("%s: %s peeks round his west corner"), PhaseName(State.Phase), *Second->DisplayName.ToString()));
				if (!bOk)
				{
					return Finish(State, false);
				}
			}
			State.Step = 3;
			State.StepTime = 0.f;
			return true;
		}
		case 3:
		{
			// Both peeking (C: ours only, D: ours, the hostile in the open): the exchange window starts.
			if (bHePeeks && !IsPeekReady(*Second) && State.StepTime < 12.f)
			{
				return true;
			}
			if (bHePeeks)
			{
				Check(State, IsPeekReady(*Second), FString::Printf(TEXT("%s: %s holds the corner aim too (%.1f s)"), PhaseName(State.Phase), *Second->DisplayName.ToString(), State.StepTime));
			}
			ResetCounters(State);
			State.bCounting = true;
			State.Step = 4;
			State.StepTime = 0.f;
			return true;
		}
		case 4:
		{
			const bool bWantBoth = State.Phase != EPhase::Hidden;
			if (State.StepTime < ExchangeSeconds && !(bWantBoth && State.OursRounds > 0 && State.HostileRounds > 0))
			{
				return true;
			}
			State.bCounting = false;
			const FString Summary = FString::Printf(TEXT("ours %d rounds (first %.2f s), his %d rounds (first %.2f s) in %.1f s"), State.OursRounds, State.OursFirstShot,
				State.HostileRounds, State.HostileFirstShot, State.StepTime);
			if (State.Phase == EPhase::Hidden)
			{
				Check(State, State.OursRounds == 0 && State.HostileRounds == 0, FString::Printf(TEXT("%s: no round either way while he stands behind his corner, nothing of him showing - %s"), PhaseName(State.Phase), *Summary));
			}
			else
			{
				Check(State, State.OursRounds > 0 && State.HostileRounds > 0, FString::Printf(TEXT("%s: the two exchange fire within %.0f s - %s"), PhaseName(State.Phase), ExchangeSeconds, *Summary));
				const float Lean = CoverRules::GetConfig().LeanExposedHitMultiplier;
				if (State.OursMultiplier >= 0.f)
				{
					Check(State, FMath::IsNearlyEqual(State.OursMultiplier, Lean, 0.01f), FString::Printf(TEXT("%s: his rounds at our peeking man rolled at x%.2f (the lean multiplier x%.2f)"),
						PhaseName(State.Phase), State.OursMultiplier, Lean));
				}
				if (bHePeeks && State.HostileMultiplier >= 0.f)
				{
					Check(State, FMath::IsNearlyEqual(State.HostileMultiplier, Lean, 0.01f), FString::Printf(TEXT("%s: our rounds at the peeking fighter rolled at x%.2f (x%.2f)"),
						PhaseName(State.Phase), State.HostileMultiplier, Lean));
				}
			}
			Ours->bTacticalCeaseFire = true;
			Hostile->bTacticalCeaseFire = true;
			Reset(World, *Ours, OurSpot(), State.F);
			Reset(World, *Hostile, HisSpot(), -State.F);
			State.Step = 5;
			State.StepTime = 0.f;
			return true;
		}
		case 5:
		{
			if (State.StepTime < 1.5f)
			{
				return true;
			}
			if (State.Phase == EPhase::OpenControl)
			{
				return Finish(State, true);
			}
			State.Phase = static_cast<EPhase>(static_cast<uint8>(State.Phase) + 1);
			State.Step = 0;
			State.StepTime = 0.f;
			return true;
		}
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
		{
			FTimerHandle PlaceHandle;
			TWeakObjectPtr<UWorld> PlaceWorld(World);
			World->GetTimerManager().SetTimer(PlaceHandle, FTimerDelegate::CreateLambda([PlaceWorld]()
			{
				SmokeUtils::PlaceSquadAtTestStart(PlaceWorld.Get());
			}), 0.5f, false);
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			State->Time += StepSeconds;
			State->StageTime += StepSeconds;
			UWorld* Current = WeakWorld.Get();
			if (!Current || State->Time > 150.f)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke stopped in stage %d, phase %s, step %d"), State->Stage, PhaseName(State->Phase), State->Step);
				return Current ? Finish(*State, false) : false;
			}
			return Step(Current, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.CornerStandoffSmoke"),
		TEXT("Dev check (designer play-test 2026-11-09, fix/corner-peek-standoff): our man and a PMC fighter peeking round the two west corners of one block exchange fire within 4 s (either peeks first; x0.5 at a peeking man); no round at a man hidden behind his corner; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
