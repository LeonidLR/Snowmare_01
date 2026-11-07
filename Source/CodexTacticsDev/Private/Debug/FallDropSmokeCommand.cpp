// Dev-only headless check of falls from heights, running drops and knockback falls on the generated map L_Gen_Depot_1 (game designer items
// 6 / 10 / 11a, 2026-11-03; docs/systems/stances-and-movement.md "Falls and drops"; ULedgeFallComponent, UDropLinkSubsystem, FallRules).
// With the pressure off and the immortality cheat off:
//  0. the drop links were generated (UDropLinkSubsystem: a link from the X_ne bridge into the cut, the ruin roof (+2) -> the annex roof (+1),
//     the ruin roof -> the ground);
//  1. cut: the whole squad on the cut's north rim by the X_ne bridge is ordered to the cut floor east of the bridge: the leader's path runs
//     through a drop link (the HUD's drop marker shows it), everybody drops off the bridge into the cut (3 m: no damage) and arrives; no two
//     men in the air over one landing spot at once (one at a time);
//  2. walk-off: the leader alone on the ruin roof is ordered to the ground below its edge (the path's 6 m drop): he walks off the edge, falls,
//     takes the fall damage of the ladder curve (one level free: (600 - 320) x 0.1 = 28) and walks on;
//  3. roof to roof: the leader SPRINTS off the ruin roof to the annex roof 2.5 m across and 3 m down: lands on it (no damage), keeping his run
//     speed in the air, the fall clip playing;
//  4. falls short: the same drop with his run-up cut to a quarter (TakeoffSpeedScale 0.1 - a slow man): the marker says TOO FAR, he misses the
//     annex roof and falls 6 m to the ground in the gap (damage);
//  5. knockback: standing on the ruin roof's open edge, facing in, a heavy blow knocks him down backwards: he is thrown over the edge, falls 6 m
//     (damage) and the knockdown plays on landing;
//  6. parapet: the same blow next to the cut's 2.2 m wall: no throw, the knockdown plays where he stands;
//  7. enemy: a Frostbitten on the ruin roof's edge is knocked down the same way: it falls too.
// Follow-up (user decisions 2026-11-03):
//  3. (above) the operative ABP's FallStates machine is in "Falling" in mid-air and "Landing" after it (no C++ slot clip);
//  8. heavy wound: the leader walks off a test platform spawned 10.5 m up (no roof that high here): 60..80 HP (70), knocked down (lies in
//     the knockdown pose for its downed time), gets up, alive;
//  9. enemy heights: two Frostbittens off platforms 10.5 m (heavy wound: knocked down, alive) and 13 m (dead);
// 10. enemy chase: a Frostbitten (free drops) on the ruin roof sent after the leader on the annex roof: its path takes a drop link, it drops
//     (no damage) and reaches him;
// 11. jumper: a Frost Hound (jumper) the same way across the roof gap;
// 12. Brute: never paths through a drop link (its filter keeps them closed), stays on the ruin roof;
// 13. lethal: a follower (not the commander) walks off a 13 m platform: dead.
// PASS / FAIL, then exits.
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands CodexTactics.FallDropSmoke -ExtraArgs "-Multiprocess"
//   (map /Game/Maps/Generated/L_Gen_Depot_1, "-LevelJson=L_Gen_Depot_1.json" - Scripts/test_map.json smokeOptions)
// Arguments: only=<stage number>. Godot reference: none (UE-only).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AIController.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/KnockdownComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Characters/OperativeAnimInstance.h"
#include "Debug/FallSmokeUtils.h"
#include "Engine/StaticMeshActor.h"
#include "Falls/DropLinkNavigation.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Falls/DropLinkSubsystem.h"
#include "Falls/FallRules.h"
#include "Falls/LedgeFallComponent.h"
#include "GameFlow/LevelExitSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Ladders/LadderClimbComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace FallDropSmoke
{
	constexpr float StepSeconds = 0.05f;
	constexpr float TotalLimitSeconds = 1500.f;
	const TCHAR* const LayoutFile = TEXT("Data/LevelGen/accepted/gen_ref_01_02_depot_1.json");
	const TCHAR* const FrostbittenPath = TEXT("/Game/Characters/Enemies/Frostbitten/BP_Enemy_Frostbitten.BP_Enemy_Frostbitten_C");
	const TCHAR* const HoundPath = TEXT("/Game/Characters/Enemies/Hound/BP_Enemy_Hound.BP_Enemy_Hound_C");
	const TCHAR* const BrutePath = TEXT("/Game/Characters/Enemies/Brute/BP_Enemy_Brute.BP_Enemy_Brute_C");
	/** The test platforms' heights: inside the heavy-wound band (9-12 m) and above the lethal height (12 m). */
	constexpr float HeavyPlatformCm = 1050.f;
	constexpr float LethalPlatformCm = 1300.f;

	enum class EStage : uint8 { Cut, WalkOff, RoofLand, RoofMiss, Knockback, Parapet, Enemy, HeavyFall, EnemyHeights, EnemyChase, JumperGap, BruteNoDrops, LethalFall, Count };
	enum class EPhase : uint8 { WaitLinks, Setup, Order, Run, Done };

	const TCHAR* StageName(EStage Stage)
	{
		switch (Stage)
		{
		case EStage::Cut: return TEXT("1 cut: the squad drops off the bridge into the cut");
		case EStage::WalkOff: return TEXT("2 walk-off: the ruin roof (+2) to the ground, 6 m");
		case EStage::RoofLand: return TEXT("3 roof to roof: a sprint off the ruin roof onto the annex roof");
		case EStage::RoofMiss: return TEXT("4 falls short: the same drop with a slow run-up");
		case EStage::Knockback: return TEXT("5 knockback: thrown over the ruin roof's edge");
		case EStage::Parapet: return TEXT("6 parapet: the blow next to the cut wall throws nobody");
		case EStage::Enemy: return TEXT("7 enemy: a Frostbitten thrown over the ruin roof's edge");
		case EStage::HeavyFall: return TEXT("8 heavy wound: the leader off a 10.5 m platform");
		case EStage::EnemyHeights: return TEXT("9 enemy heights: Frostbittens off 10.5 m (knocked down) and 13 m (dead)");
		case EStage::EnemyChase: return TEXT("10 enemy chase: a Frostbitten drops off the ruin roof after the leader");
		case EStage::JumperGap: return TEXT("11 jumper: a Frost Hound crosses the roof gap after the leader");
		case EStage::BruteNoDrops: return TEXT("12 Brute: no drop links");
		case EStage::LethalFall: return TEXT("13 lethal: a follower off a 13 m platform");
		default: return TEXT("?");
		}
	}

	struct FBox2DCm
	{
		FVector2D Min = FVector2D::ZeroVector;
		FVector2D Max = FVector2D::ZeroVector;
		bool Contains(const FVector& P, float Pad = 0.f) const
		{
			return P.X >= Min.X - Pad && P.X <= Max.X + Pad && P.Y >= Min.Y - Pad && P.Y <= Max.Y + Pad;
		}
	};

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		EPhase Phase = EPhase::WaitLinks;
		int32 Stage = 0;
		int32 Only = -1;
		float Time = 0.f;
		float PhaseTime = 0.f;
		float StillTime = 0.f;
		float DoneFor = 0.f;
		int32 Failures = 0;
		TArray<FString> Summary;
		// The layout.
		FBox2DCm Ruin;
		FBox2DCm Annex;
		FBox2DCm BridgeNE;
		float GroundZ = 0.f;
		// The stage.
		int32 RoofLink = INDEX_NONE;
		int32 HurtLink = INDEX_NONE;
		FVector Goal = FVector::ZeroVector;
		bool bMarkerSeen = false;
		FString MarkerLabel;
		bool bMarkerDanger = false;
		bool bPathHasDrop = false;
		int32 LandingsBefore = 0;
		int32 DropsBefore = 0;
		int32 KnockbacksBefore = 0;
		float HealthBefore = 0.f;
		float LandedAt = -1.f;
		bool bKnockedDownAfter = false;
		bool bLeaseViolated = false;
		int32 MaxAirborne = 0;
		TMap<TWeakObjectPtr<AOperativeCharacter>, int32> MemberDrops;
		TWeakObjectPtr<AEnemyCharacter> Enemy;
		// Follow-up stages (2026-11-03).
		TWeakObjectPtr<AEnemyCharacter> Enemy2;
		TArray<TWeakObjectPtr<AStaticMeshActor>> Platforms;
		TWeakObjectPtr<AOperativeCharacter> Victim;
		FVector PlatformDir = FVector::ForwardVector;
		bool bPushed = false;
		bool bHadWalkOff = false;
		bool bAbpFalling = false;
		bool bAbpLanding = false;
		bool bDownedSeen = false;
		int32 RecoveriesBefore = 0;
		int32 HeavyBefore = 0;
		float MinEnemyFeet = 1e9f;
		FString AbpStates;
		/** The 13 m Frostbitten's landing (its body may be removed after its death). */
		int32 DeadOutcome = -1;
		bool bDeadDied = false;
		FString DeadNote;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	void ClearPlatforms(FState& State)
	{
		for (const TWeakObjectPtr<AStaticMeshActor>& Platform : State.Platforms)
		{
			if (AStaticMeshActor* Actor = Platform.Get())
			{
				Actor->Destroy();
			}
		}
		State.Platforms.Reset();
	}

	bool Finish(FState& State, bool bComplete)
	{
		ClearPlatforms(State);
		for (const FString& Line : State.Summary)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke summary: %s"), *Line);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: FallDropSmoke %d failures"), State.Failures);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		State.Phase = EPhase::Done;
		FPlatformMisc::RequestExit(false, TEXT("FallDropSmoke"));
		return false;
	}

	void EndStage(FState& State, bool bOk, const FString& What)
	{
		const FString Line = FString::Printf(TEXT("%s: %s"), StageName(static_cast<EStage>(State.Stage)), *What);
		Check(State, bOk, Line);
		State.Summary.Add(FString::Printf(TEXT("%-4s %s"), bOk ? TEXT("ok") : TEXT("FAIL"), *Line));
		++State.Stage;
		State.Phase = EPhase::Setup;
		State.PhaseTime = 0.f;
	}

	float FeetZ(const ACharacter& Character)
	{
		return static_cast<float>(Character.GetActorLocation().Z) - Character.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	}

	FVector OnNav(UWorld* World, const FVector& P, float Height = 60.f)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		FNavLocation Out;
		return Nav && Nav->ProjectPointToNavigation(P, Out, FVector(80.f, 80.f, Height)) ? Out.Location : P;
	}

	void PutAt(ACharacter& Character, const FVector& Feet, float Yaw)
	{
		if (AOperativeCharacter* Operative = Cast<AOperativeCharacter>(&Character); Operative && Operative->LadderClimb && Operative->LadderClimb->IsBusy())
		{
			Operative->LadderClimb->AbortClimb(ELadderAbortReason::Teleported);
		}
		Character.TeleportTo(Feet + FVector(0.f, 0.f, Character.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 3.f), FRotator(0.f, Yaw, 0.f), false, true);
		if (AAIController* AI = Cast<AAIController>(Character.GetController()))
		{
			AI->StopMovement();
		}
		Character.GetCharacterMovement()->StopMovementImmediately();
	}

	bool ReadBox(const TSharedPtr<FJsonObject>& Layout, const FString& Id, FBox2DCm& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Buildings = nullptr;
		if (!Layout->TryGetArrayField(TEXT("buildings"), Buildings))
		{
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Buildings)
		{
			const TSharedPtr<FJsonObject> Building = Value->AsObject();
			const TArray<TSharedPtr<FJsonValue>>* Footprint = nullptr;
			if (Building && Building->GetStringField(TEXT("id")) == Id && Building->TryGetArrayField(TEXT("footprint"), Footprint) && Footprint->Num() == 2)
			{
				const TArray<TSharedPtr<FJsonValue>>& A = (*Footprint)[0]->AsArray();
				const TArray<TSharedPtr<FJsonValue>>& B = (*Footprint)[1]->AsArray();
				Out.Min = FVector2D(A[0]->AsNumber(), A[1]->AsNumber());
				Out.Max = FVector2D(B[0]->AsNumber(), B[1]->AsNumber());
				return true;
			}
		}
		return false;
	}

	bool ReadLayout(FState& State)
	{
		FString Text;
		TSharedPtr<FJsonObject> Layout;
		if (!FFileHelper::LoadFileToString(Text, *(FPaths::ProjectContentDir() / LayoutFile)) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Layout)
			|| !Layout.IsValid())
		{
			return false;
		}
		State.GroundZ = static_cast<float>(Layout->GetNumberField(TEXT("ground_z")));
		const TSharedPtr<FJsonObject>* Cut = nullptr;
		bool bBridge = false;
		if (Layout->TryGetObjectField(TEXT("cut"), Cut))
		{
			for (const TSharedPtr<FJsonValue>& Value : (*Cut)->GetArrayField(TEXT("crossings")))
			{
				const TSharedPtr<FJsonObject> Crossing = Value->AsObject();
				if (Crossing && Crossing->GetStringField(TEXT("id")) == TEXT("X_ne"))
				{
					State.BridgeNE.Min = FVector2D(1e9, 1e9);
					State.BridgeNE.Max = FVector2D(-1e9, -1e9);
					for (const TSharedPtr<FJsonValue>& Point : Crossing->GetArrayField(TEXT("poly")))
					{
						const TArray<TSharedPtr<FJsonValue>>& XY = Point->AsArray();
						State.BridgeNE.Min = FVector2D(FMath::Min(State.BridgeNE.Min.X, XY[0]->AsNumber()), FMath::Min(State.BridgeNE.Min.Y, XY[1]->AsNumber()));
						State.BridgeNE.Max = FVector2D(FMath::Max(State.BridgeNE.Max.X, XY[0]->AsNumber()), FMath::Max(State.BridgeNE.Max.Y, XY[1]->AsNumber()));
					}
					bBridge = true;
				}
			}
		}
		return bBridge && ReadBox(Layout, TEXT("B_ruin"), State.Ruin) && ReadBox(Layout, TEXT("B_ruin_annex"), State.Annex);
	}

	/** The drop links a path from From to To (the operative's query) runs through. */
	TArray<int32> LinksOnPath(UWorld* World, const FVector& From, const FVector& To, AActor* Context)
	{
		TArray<int32> Out;
		const UDropLinkSubsystem* Links = World->GetSubsystem<UDropLinkSubsystem>();
		UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, From, To, Context);
		if (Links && Path && Path->GetPath().IsValid())
		{
			Links->FindLinksOnPath(*Path->GetPath(), Out);
		}
		return Out;
	}

	void HealAll(USquadSubsystem& Squad)
	{
		for (AOperativeCharacter* Member : Squad.GetMembers())
		{
			if (Member && Member->HealthComponent)
			{
				Member->HealthComponent->Heal(Member->HealthComponent->GetMaxHealth());
			}
		}
	}

	/** Solo with the others parked on the ground by the spawn (out of the way). */
	void SoloLeader(UWorld* World, USquadSubsystem& Squad)
	{
		if (!Squad.IsSoloMode())
		{
			Squad.EnterSoloMode();
		}
		Squad.SetFollowersHolding(true);
		int32 Slot = 0;
		for (AOperativeCharacter* Member : Squad.GetMembers())
		{
			if (Member && Member != Squad.GetLeader())
			{
				PutAt(*Member, OnNav(World, FVector(-1500.f + 200.f * Slot++, -6000.f, 0.f)), 0.f);
			}
		}
	}

	/** Picks the ruin-roof links: to the annex roof, and a hurting one to the ground that a path from its roof side really takes. */
	void PickLinks(FState& State, UWorld* World, AActor* Context)
	{
		const UDropLinkSubsystem* Links = World->GetSubsystem<UDropLinkSubsystem>();
		const float RoofZ = State.GroundZ + 600.f;
		float BestRoof = -1.f;
		for (int32 Index = 0; Links && Index < Links->GetLinks().Num(); ++Index)
		{
			const FDropLink& Link = Links->GetLinks()[Index];
			if (FMath::Abs(Link.Start.Z - RoofZ) > 60.f || !State.Ruin.Contains(Link.Start, 10.f))
			{
				continue;
			}
			if (FMath::Abs(Link.End.Z - (RoofZ - 300.f)) < 60.f && State.Annex.Contains(Link.End))
			{
				// The one nearest the middle of the annex's long side (room to run up on the ruin roof).
				const float Score = -FMath::Abs(static_cast<float>(Link.Start.X) - static_cast<float>(State.Annex.Min.X + State.Annex.Max.X) * 0.5f);
				if (State.RoofLink == INDEX_NONE || Score > BestRoof)
				{
					State.RoofLink = Index;
					BestRoof = Score;
				}
			}
			else if (State.HurtLink == INDEX_NONE && Link.bHurt && FMath::Abs(Link.End.Z - State.GroundZ) < 60.f && !State.Annex.Contains(Link.End, 300.f))
			{
				const TArray<int32> OnPath = LinksOnPath(World, Link.Start - Link.Dir * 150.f, Link.End + Link.Dir * 250.f, Context);
				if (OnPath.Contains(Index))
				{
					State.HurtLink = Index;
				}
			}
		}
	}

	/** Spawns an enemy of ClassPath at Feet with no AI of its own (the smoke orders its moves), out of the registry (the squad does not shoot it). */
	AEnemyCharacter* SpawnQuietEnemy(UWorld* World, const TCHAR* ClassPath, const FVector& Feet, float Yaw)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		UClass* Class = LoadClass<AEnemyCharacter>(nullptr, ClassPath);
		AEnemyCharacter* Enemy = Class ? World->SpawnActor<AEnemyCharacter>(Class, Feet + FVector(0.f, 0.f, 120.f), FRotator(0.f, Yaw, 0.f), Params) : nullptr;
		if (!Enemy)
		{
			return nullptr;
		}
		Enemy->SetActorTickEnabled(false);
		if (UEnemyRegistrySubsystem* Registry = World->GetSubsystem<UEnemyRegistrySubsystem>())
		{
			Registry->UnregisterEnemy(Enemy);
		}
		Enemy->Tags.Remove(FName(TEXT("Enemy")));
		Enemy->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		PutAt(*Enemy, Feet, Yaw);
		return Enemy;
	}

	/** A test platform HeightCm up on open ground (a new spot each time); null when none. */
	AStaticMeshActor* SpawnTestPlatform(FState& State, UWorld* World, float HeightCm)
	{
		TArray<FVector2D> Spots = FallSmokeUtils::DepotOpenSpots();
		for (const TWeakObjectPtr<AStaticMeshActor>& Old : State.Platforms)
		{
			if (const AStaticMeshActor* Used = Old.Get())
			{
				Spots.RemoveAll([Used](const FVector2D& XY) { return FVector2D::Distance(XY, FVector2D(Used->GetActorLocation())) < 50.f; });
			}
		}
		FVector Feet;
		if (!FallSmokeUtils::FindOpenGround(*World, Spots, State.GroundZ, HeightCm + 200.f, Feet))
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("Smoke: no open ground for a %.0f cm test platform"), HeightCm);
			return nullptr;
		}
		AStaticMeshActor* Platform = FallSmokeUtils::SpawnPlatform(*World, Feet, HeightCm);
		State.Platforms.Add(Platform);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: a test platform %.0f cm up at %s"), HeightCm, *Feet.ToCompactString());
		return Platform;
	}

	/** The ABP's fall state now (the operative's FallStates machine). */
	FName AbpFallState(const AOperativeCharacter& Operative)
	{
		const UOperativeAnimInstance* Anim = Operative.GetMesh() ? Cast<UOperativeAnimInstance>(Operative.GetMesh()->GetAnimInstance()) : nullptr;
		return Anim ? Anim->GetFallStateName() : NAME_None;
	}

	void Setup(FState& State, UWorld* World, USquadSubsystem& Squad)
	{
		AOperativeCharacter* Leader = Squad.GetLeader();
		UDropLinkSubsystem* Links = World->GetSubsystem<UDropLinkSubsystem>();
		const EStage Stage = static_cast<EStage>(State.Stage);
		HealAll(Squad);
		State.Victim = Leader;
		Leader->LedgeFall->TakeoffSpeedScale = 1.f;
		State.bMarkerSeen = false;
		State.MarkerLabel.Empty();
		State.bMarkerDanger = false;
		State.bPathHasDrop = false;
		State.LandingsBefore = Leader->LedgeFall->GetLandingCount();
		State.DropsBefore = Leader->LedgeFall->GetDropCount();
		State.KnockbacksBefore = Leader->LedgeFall->GetKnockbackFallCount();
		State.HealthBefore = Leader->HealthComponent->GetCurrentHealth();
		State.LandedAt = -1.f;
		State.bKnockedDownAfter = false;
		State.bLeaseViolated = false;
		State.MaxAirborne = 0;
		State.MemberDrops.Reset();
		State.StillTime = 0.f;
		State.DoneFor = 0.f;
		State.bPushed = false;
		State.bAbpFalling = false;
		State.bAbpLanding = false;
		State.bDownedSeen = false;
		State.MinEnemyFeet = 1e9f;
		State.AbpStates.Empty();
		State.DeadOutcome = -1;
		State.bDeadDied = false;
		State.DeadNote.Empty();
		State.RecoveriesBefore = Leader->KnockdownComponent ? Leader->KnockdownComponent->GetRecoveryCount() : 0;
		State.HeavyBefore = Leader->LedgeFall->GetHeavyWoundCount();
		ClearPlatforms(State);
		const FDropLink* Roof = Links->GetLink(State.RoofLink);
		const FDropLink* Hurt = Links->GetLink(State.HurtLink);
		switch (Stage)
		{
		case EStage::Cut:
		{
			if (Squad.IsSoloMode())
			{
				Squad.ExitSoloMode();
			}
			Squad.SetFollowersHolding(false);
			// The cut's north rim beside the X_ne bridge; the goal on the cut floor east of the bridge.
			const float BridgeMidY = static_cast<float>(State.BridgeNE.Min.Y + State.BridgeNE.Max.Y) * 0.5f;
			const FVector Rim(static_cast<float>(State.BridgeNE.Max.X) + 300.f, BridgeMidY, State.GroundZ);
			int32 Slot = 0;
			for (AOperativeCharacter* Member : Squad.GetMembers())
			{
				if (!Member)
				{
					continue;
				}
				const FVector At = Member == Leader ? Rim : Rim + FVector(250.f + 120.f * (Slot / 2), (Slot % 2 == 0 ? 1.f : -1.f) * 150.f, 0.f);
				Slot += Member == Leader ? 0 : 1;
				PutAt(*Member, OnNav(World, At), 180.f);
				State.MemberDrops.Add(Member, Member->LedgeFall ? Member->LedgeFall->GetDropCount() : 0);
			}
			State.Goal = FVector(0.f, static_cast<float>(State.BridgeNE.Max.Y) + 400.f, State.GroundZ - 300.f);
			break;
		}
		case EStage::WalkOff:
			SoloLeader(World, Squad);
			PutAt(*Leader, OnNav(World, Hurt->Start - Hurt->Dir * 150.f), Hurt->Dir.Rotation().Yaw);
			State.Goal = Hurt->End + Hurt->Dir * 250.f;
			break;
		case EStage::RoofLand:
		case EStage::RoofMiss:
			SoloLeader(World, Squad);
			PutAt(*Leader, OnNav(World, Roof->Start - Roof->Dir * 400.f), Roof->Dir.Rotation().Yaw);
			State.Goal = Roof->End + Roof->Dir * 150.f;
			Leader->LedgeFall->TakeoffSpeedScale = Stage == EStage::RoofMiss ? 0.1f : 1.f;
			break;
		case EStage::Knockback:
		case EStage::Enemy:
			SoloLeader(World, Squad);
			if (Stage == EStage::Knockback)
			{
				PutAt(*Leader, Hurt->Start, (-Hurt->Dir).Rotation().Yaw); // on the open edge, facing in: a blow from the front throws him back over it
			}
			else
			{
				PutAt(*Leader, OnNav(World, FVector(-1500.f, -5800.f, State.GroundZ)), 0.f);
				FActorSpawnParameters Params;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
				UClass* Class = LoadClass<AEnemyCharacter>(nullptr, FrostbittenPath);
				AEnemyCharacter* Enemy = Class ? World->SpawnActor<AEnemyCharacter>(Class, Hurt->Start + FVector(0.f, 0.f, 100.f), (-Hurt->Dir).Rotation(), Params) : nullptr;
				State.Enemy = Enemy;
				if (Enemy)
				{
					Enemy->SetActorTickEnabled(false); // no AI: the smoke knocks it down
					if (UEnemyRegistrySubsystem* Registry = World->GetSubsystem<UEnemyRegistrySubsystem>())
					{
						Registry->UnregisterEnemy(Enemy);
					}
					Enemy->Tags.Remove(FName(TEXT("Enemy")));
					PutAt(*Enemy, Hurt->Start, (-Hurt->Dir).Rotation().Yaw);
				}
			}
			break;
		case EStage::HeavyFall:
		case EStage::LethalFall:
		{
			SoloLeader(World, Squad);
			AStaticMeshActor* Platform = SpawnTestPlatform(State, World, Stage == EStage::HeavyFall ? HeavyPlatformCm : LethalPlatformCm);
			AOperativeCharacter* Body = Leader;
			if (Stage == EStage::LethalFall)
			{
				Body = nullptr;
				for (AOperativeCharacter* Member : Squad.GetMembers())
				{
					Body = !Body && Member && Member != Leader && Member->SquadRole != EOperativeRole::Commander && Member->HealthComponent->IsAlive() ? Member : Body;
				}
			}
			State.Victim = Body;
			State.PlatformDir = FVector(1.f, 0.f, 0.f);
			if (Platform && Body)
			{
				FallSmokeUtils::StandOnPlatformEdge(*Body, *Platform, State.PlatformDir);
				Body->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				State.HealthBefore = Body->HealthComponent->GetCurrentHealth();
				State.LandingsBefore = Body->LedgeFall->GetLandingCount();
				State.HeavyBefore = Body->LedgeFall->GetHeavyWoundCount();
			}
			break;
		}
		case EStage::EnemyHeights:
		{
			SoloLeader(World, Squad);
			PutAt(*Leader, OnNav(World, FVector(-800.f, -6000.f, State.GroundZ)), 0.f); // out of the landing zones (the last stage left him there)
			AStaticMeshActor* High = SpawnTestPlatform(State, World, HeavyPlatformCm);
			AStaticMeshActor* Higher = SpawnTestPlatform(State, World, LethalPlatformCm);
			State.PlatformDir = FVector(1.f, 0.f, 0.f);
			if (High && Higher)
			{
				State.Enemy = SpawnQuietEnemy(World, FrostbittenPath, High->GetActorLocation() + FVector(0.f, 0.f, 20.f), 0.f);
				State.Enemy2 = SpawnQuietEnemy(World, FrostbittenPath, Higher->GetActorLocation() + FVector(0.f, 0.f, 20.f), 0.f);
				if (State.Enemy.IsValid() && State.Enemy2.IsValid())
				{
					FallSmokeUtils::StandOnPlatformEdge(*State.Enemy, *High, State.PlatformDir);
					FallSmokeUtils::StandOnPlatformEdge(*State.Enemy2, *Higher, State.PlatformDir);
				}
			}
			break;
		}
		case EStage::EnemyChase:
		case EStage::JumperGap:
		case EStage::BruteNoDrops:
		{
			SoloLeader(World, Squad);
			PutAt(*Leader, OnNav(World, Roof->End + Roof->Dir * 350.f), (-Roof->Dir).Rotation().Yaw);
			const TCHAR* Path = Stage == EStage::EnemyChase ? FrostbittenPath : Stage == EStage::JumperGap ? HoundPath : BrutePath;
			State.Enemy = SpawnQuietEnemy(World, Path, OnNav(World, Roof->Start - Roof->Dir * 450.f), Roof->Dir.Rotation().Yaw);
			State.Goal = Leader->GetActorLocation();
			break;
		}
		case EStage::Parapet:
		{
			SoloLeader(World, Squad);
			// 60 cm in front of the cut's north wall (its face at the bridge's +X end), facing away from it: the blow throws him at the wall.
			const FVector Spot(static_cast<float>(State.BridgeNE.Max.X) + 35.f + 25.f, static_cast<float>(State.BridgeNE.Min.Y) - 1500.f, State.GroundZ);
			PutAt(*Leader, OnNav(World, Spot), 0.f);
			break;
		}
		default:
			break;
		}
		State.Phase = EPhase::Order;
		State.PhaseTime = 0.f;
	}

	void Order(FState& State, UWorld* World, USquadSubsystem& Squad)
	{
		AOperativeCharacter* Leader = Squad.GetLeader();
		const EStage Stage = static_cast<EStage>(State.Stage);
		switch (Stage)
		{
		case EStage::Cut:
		case EStage::WalkOff:
		case EStage::RoofLand:
		case EStage::RoofMiss:
		{
			const bool bSprint = Stage == EStage::RoofLand;
			const EOperativeOrderResult Result = Leader->OrderMoveTo(State.Goal, bSprint);
			const AAIController* AI = Cast<AAIController>(Leader->GetController());
			const UPathFollowingComponent* Following = AI ? AI->GetPathFollowingComponent() : nullptr;
			TArray<int32> OnPath;
			if (Following && Following->GetPath().IsValid())
			{
				World->GetSubsystem<UDropLinkSubsystem>()->FindLinksOnPath(*Following->GetPath(), OnPath);
			}
			State.bPathHasDrop = OnPath.Num() > 0;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s - the leader at %s ordered to %s%s (result %d), drop links on his path: %d"), StageName(Stage),
				*Leader->GetActorLocation().ToCompactString(), *State.Goal.ToCompactString(), bSprint ? TEXT(" sprinting") : TEXT(""), static_cast<int32>(Result), OnPath.Num());
			break;
		}
		case EStage::Knockback:
		case EStage::Parapet:
			Leader->KnockdownComponent->ForceKnockDown(EKnockdownDirection::Back, EKnockdownCause::HeavyHit);
			break;
		case EStage::Enemy:
			if (AEnemyCharacter* Enemy = State.Enemy.Get(); Enemy && Enemy->KnockdownComponent)
			{
				Enemy->KnockdownComponent->bCanBeKnockedDown = true;
				Enemy->KnockdownComponent->ForceKnockDown(EKnockdownDirection::Back, EKnockdownCause::HeavyHit);
			}
			break;
		case EStage::HeavyFall:
		case EStage::LethalFall:
			if (AOperativeCharacter* Body = State.Victim.Get(); Body && !State.Platforms.IsEmpty())
			{
				State.bHadWalkOff = FallSmokeUtils::PushOffEdge(*Body, State.PlatformDir);
				State.bPushed = true;
			}
			break;
		case EStage::EnemyHeights:
			for (AEnemyCharacter* Enemy : { State.Enemy.Get(), State.Enemy2.Get() })
			{
				if (Enemy)
				{
					FallSmokeUtils::PushOffEdge(*Enemy, State.PlatformDir);
					State.bPushed = true;
				}
			}
			break;
		case EStage::EnemyChase:
		case EStage::JumperGap:
		case EStage::BruteNoDrops:
			if (AEnemyCharacter* Enemy = State.Enemy.Get())
			{
				AAIController* AI = Cast<AAIController>(Enemy->GetController());
				TArray<int32> OnPath;
				if (AI)
				{
					UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, Enemy->GetActorLocation(), Leader->GetActorLocation(), Enemy,
						AI->GetDefaultNavigationFilterClass());
					if (Path && Path->GetPath().IsValid())
					{
						World->GetSubsystem<UDropLinkSubsystem>()->FindLinksOnPath(*Path->GetPath(), OnPath);
					}
					AI->MoveToActor(Leader, 120.f);
				}
				State.bPathHasDrop = OnPath.Num() > 0;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s - %s (filter %s, drop use %d) sent after the leader: drop links on its path %d"), StageName(Stage),
					*Enemy->GetName(), AI ? *GetNameSafe(AI->GetDefaultNavigationFilterClass().Get()) : TEXT("-"), static_cast<int32>(Enemy->GetDropLinkUse()), OnPath.Num());
			}
			break;
		default:
			break;
		}
		State.Phase = EPhase::Run;
		State.PhaseTime = 0.f;
	}

	/** One-at-a-time watch of the squad stage: nobody in the air over another man's landing spot. */
	void WatchAirborne(FState& State, UWorld* World, USquadSubsystem& Squad)
	{
		const UDropLinkSubsystem* Links = World->GetSubsystem<UDropLinkSubsystem>();
		TArray<FVector> Landings;
		for (AOperativeCharacter* Member : Squad.GetMembers())
		{
			const ULedgeFallComponent* Fall = Member ? Member->LedgeFall.Get() : nullptr;
			const FDropLink* Link = Fall && Fall->IsAirborne() ? Links->GetLink(Fall->GetActiveLink()) : nullptr;
			if (Link)
			{
				for (const FVector& Other : Landings)
				{
					State.bLeaseViolated |= FallRules::LandingsConflict(Other, Link->End, FallRules::GetConfig().LeaseRadiusCm * 0.9f);
				}
				Landings.Add(Link->End);
			}
		}
		State.MaxAirborne = FMath::Max(State.MaxAirborne, Landings.Num());
	}

	void SeeMarker(FState& State, UWorld* World)
	{
		TArray<FDropMarker> Markers;
		World->GetSubsystem<UDropLinkSubsystem>()->GatherSquadMarkers(Markers);
		if (!Markers.IsEmpty() && !State.bMarkerSeen)
		{
			State.bMarkerSeen = true;
			State.MarkerLabel = Markers[0].Label;
			State.bMarkerDanger = Markers[0].bDanger;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: the drop marker shows \"%s\"%s"), *State.MarkerLabel, State.bMarkerDanger ? TEXT(" (red)") : TEXT(""));
		}
	}

	FString FallNote(const ULedgeFallComponent& Fall)
	{
		return FString::Printf(TEXT("fell %.0f cm, %.1f damage, run-up %.0f cm/s, air speed %.0f cm/s, %s, fall clip %s"), Fall.GetLastFallHeight(), Fall.GetLastFallDamage(),
			Fall.GetPlannedSpeed(), Fall.GetLastAirSpeed(), Fall.DidLastDropLandOnTarget() ? TEXT("on the link's lower surface") : TEXT("off the link's lower surface"),
			Fall.PlayedFallClip() ? TEXT("played") : TEXT("none"));
	}

	void Run(FState& State, UWorld* World, USquadSubsystem& Squad, float Dt)
	{
		AOperativeCharacter* Leader = Squad.GetLeader();
		const ULedgeFallComponent& Fall = *Leader->LedgeFall;
		const EStage Stage = static_cast<EStage>(State.Stage);
		const FallRules::FFallConfig& Config = FallRules::GetConfig();
		SeeMarker(State, World);
		// The operative ABP's fall states (2026-11-03): "Falling" in the air, "Landing" after it.
		if (const AOperativeCharacter* Watched = State.Victim.IsValid() ? State.Victim.Get() : Leader)
		{
			const FName AbpState = AbpFallState(*Watched);
			State.bAbpFalling |= AbpState == TEXT("Falling");
			State.bAbpLanding |= AbpState == TEXT("Landing");
			if (!AbpState.IsNone() && !State.AbpStates.EndsWith(AbpState.ToString()))
			{
				State.AbpStates += (State.AbpStates.IsEmpty() ? TEXT("") : TEXT(">")) + AbpState.ToString();
			}
		}
		const bool bLanded = Fall.GetLandingCount() > State.LandingsBefore;
		if (bLanded && State.LandedAt < 0.f)
		{
			State.LandedAt = State.PhaseTime;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: the leader landed at %s: %s"), *Fall.GetLastLandingLocation().ToCompactString(), *FallNote(Fall));
		}
		const float Dist = static_cast<float>(FVector::Dist2D(Leader->GetActorLocation(), State.Goal));
		switch (Stage)
		{
		case EStage::Cut:
		{
			WatchAirborne(State, World, Squad);
			bool bAllIn = true;
			FString Where;
			int32 Dropped = 0;
			float Damage = 0.f;
			for (AOperativeCharacter* Member : Squad.GetMembers())
			{
				if (!Member)
				{
					continue;
				}
				const float Feet = FeetZ(*Member);
				const bool bIn = Feet < State.GroundZ - 200.f && !Member->LedgeFall->IsBusy();
				bAllIn &= bIn;
				const int32* Before = State.MemberDrops.Find(Member);
				Dropped += Member->LedgeFall->GetDropCount() > (Before ? *Before : 0) ? 1 : 0;
				Damage += Member->HealthComponent->GetMaxHealth() - Member->HealthComponent->GetCurrentHealth();
				Where += FString::Printf(TEXT(" %s feet %.0f%s"), *Member->GetName(), Feet, bIn ? TEXT("") : TEXT(" (out)"));
			}
			State.DoneFor = bAllIn && Dist <= 300.f ? State.DoneFor + Dt : 0.f;
			if (State.DoneFor >= 0.5f)
			{
				const bool bOk = State.bPathHasDrop && State.bMarkerSeen && Dropped >= 1 && Damage < 0.5f && !State.bLeaseViolated;
				EndStage(State, bOk, FString::Printf(TEXT("in %.1f s: everybody in the cut, %d dropped, the leader's path had a drop %d, marker \"%s\", total damage %.1f, one at a time %s (max %d in the air);%s"),
					State.PhaseTime, Dropped, State.bPathHasDrop ? 1 : 0, *State.MarkerLabel, Damage, State.bLeaseViolated ? TEXT("VIOLATED") : TEXT("kept"), State.MaxAirborne, *Where));
			}
			else if (State.PhaseTime > 150.f)
			{
				EndStage(State, false, FString::Printf(TEXT("TIMED OUT: leader %.0f cm from the goal, path drop %d, marker %d;%s"), Dist, State.bPathHasDrop ? 1 : 0,
					State.bMarkerSeen ? 1 : 0, *Where));
			}
			return;
		}
		case EStage::WalkOff:
		case EStage::RoofLand:
		case EStage::RoofMiss:
		{
			const bool bSettled = bLanded && !Fall.IsBusy() && State.PhaseTime - State.LandedAt > 1.f;
			if (bSettled && (Stage == EStage::RoofMiss || Dist <= 250.f || State.PhaseTime - State.LandedAt > 25.f))
			{
				const float Lost = State.HealthBefore - Leader->HealthComponent->GetCurrentHealth();
				const float Height = Fall.GetLastFallHeight();
				bool bOk = State.bPathHasDrop && State.bMarkerSeen && Fall.GetDropCount() > State.DropsBefore && Fall.GetLastFallKind() == ELedgeFallKind::DropLink;
				if (Stage == EStage::WalkOff)
				{
					bOk &= Height > 540.f && Height < 680.f && FMath::IsNearlyEqual(Lost, FallRules::FallDamage(Height, Config), 0.6f) && Lost > 15.f && State.bMarkerDanger
						&& Dist <= 250.f;
				}
				else if (Stage == EStage::RoofLand)
				{
					bOk &= Fall.DidLastDropLandOnTarget() && Height > 240.f && Height < 360.f && Lost < 0.5f && Fall.GetLastAirSpeed() >= 400.f && !State.bMarkerDanger
						&& Dist <= 250.f && State.bAbpFalling && State.bAbpLanding && !Fall.PlayedFallClip();
				}
				else
				{
					bOk &= !Fall.DidLastDropLandOnTarget() && Height > 540.f && Lost > 15.f && State.bMarkerDanger && State.MarkerLabel.Contains(TEXT("TOO FAR"));
				}
				EndStage(State, bOk, FString::Printf(TEXT("in %.1f s, the leader %.0f cm from the goal at feet %.0f: %s; health -%.1f; path drop %d, marker \"%s\"%s; ABP %s"),
					State.PhaseTime, Dist, FeetZ(*Leader), *FallNote(Fall), Lost, State.bPathHasDrop ? 1 : 0, *State.MarkerLabel, State.bMarkerDanger ? TEXT(" (red)") : TEXT(""),
					*State.AbpStates));
				if (Stage == EStage::RoofMiss)
				{
					Leader->StopOperative();
				}
			}
			else if (State.PhaseTime > 60.f)
			{
				EndStage(State, false, FString::Printf(TEXT("TIMED OUT: the leader at %s (feet %.0f) %.0f cm from the goal, drops %d, landings %d, fall state %d, path drop %d"),
					*Leader->GetActorLocation().ToCompactString(), FeetZ(*Leader), Dist, Fall.GetDropCount() - State.DropsBefore, Fall.GetLandingCount() - State.LandingsBefore,
					static_cast<int32>(Fall.GetState()), State.bPathHasDrop ? 1 : 0));
			}
			return;
		}
		case EStage::Knockback:
		case EStage::Parapet:
		{
			const bool bDown = Leader->IsKnockedDown();
			State.bKnockedDownAfter |= bDown && (Stage == EStage::Parapet || bLanded);
			const bool bThrown = Fall.GetKnockbackFallCount() > State.KnockbacksBefore;
			if (State.PhaseTime > 1.5f && !bDown && (Stage == EStage::Parapet || bLanded))
			{
				const float Lost = State.HealthBefore - Leader->HealthComponent->GetCurrentHealth();
				const bool bOk = Stage == EStage::Knockback
					? bThrown && bLanded && Fall.GetLastFallKind() == ELedgeFallKind::Knockback && Fall.GetLastFallHeight() > 540.f
						&& FMath::IsNearlyEqual(Lost, FallRules::FallDamage(Fall.GetLastFallHeight(), Config), 0.6f) && State.bKnockedDownAfter
					: !bThrown && State.bKnockedDownAfter && FeetZ(*Leader) > State.GroundZ - 60.f;
				EndStage(State, bOk, FString::Printf(TEXT("in %.1f s: thrown %d, knocked down %d, feet %.0f, health -%.1f%s"), State.PhaseTime, bThrown ? 1 : 0,
					State.bKnockedDownAfter ? 1 : 0, FeetZ(*Leader), Lost, bThrown ? *(TEXT("; ") + FallNote(Fall)) : TEXT("")));
			}
			else if (State.PhaseTime > 30.f)
			{
				EndStage(State, false, FString::Printf(TEXT("TIMED OUT: thrown %d, landed %d, down %d, feet %.0f"), bThrown ? 1 : 0, bLanded ? 1 : 0, bDown ? 1 : 0, FeetZ(*Leader)));
			}
			return;
		}
		case EStage::Enemy:
		{
			AEnemyCharacter* Enemy = State.Enemy.Get();
			const ULedgeFallComponent* EnemyFall = Enemy ? Enemy->LedgeFall.Get() : nullptr;
			if (!EnemyFall)
			{
				EndStage(State, false, TEXT("no Frostbitten spawned"));
				return;
			}
			if (EnemyFall->GetLandingCount() > 0 && State.PhaseTime > 1.f)
			{
				const bool bOk = EnemyFall->GetKnockbackFallCount() == 1 && EnemyFall->GetLastFallKind() == ELedgeFallKind::Knockback && EnemyFall->GetLastFallHeight() > 540.f;
				EndStage(State, bOk, FString::Printf(TEXT("in %.1f s: the Frostbitten %s"), State.PhaseTime, *FallNote(*EnemyFall)));
				Enemy->Destroy();
			}
			else if (State.PhaseTime > 15.f)
			{
				EndStage(State, false, FString::Printf(TEXT("TIMED OUT: thrown %d, feet %.0f, can be knocked down %d"), EnemyFall->GetKnockbackFallCount(), FeetZ(*Enemy),
					Enemy->KnockdownComponent && Enemy->KnockdownComponent->bCanBeKnockedDown ? 1 : 0));
				Enemy->Destroy();
			}
			return;
		}
		case EStage::HeavyFall:
		case EStage::LethalFall:
		{
			AOperativeCharacter* Body = State.Victim.Get();
			if (!Body || !State.bPushed)
			{
				EndStage(State, false, FString::Printf(TEXT("no platform / body (platforms %d, body %d)"), State.Platforms.Num(), Body ? 1 : 0));
				return;
			}
			const ULedgeFallComponent& BodyFall = *Body->LedgeFall;
			const bool bBodyLanded = BodyFall.GetLandingCount() > State.LandingsBefore;
			const UKnockdownComponent* Knockdown = Body->KnockdownComponent;
			State.bDownedSeen |= Knockdown && Knockdown->GetPhase() == EKnockdownPhase::Downed;
			const bool bAlive = Body->HealthComponent->IsAlive();
			const float Lost = State.HealthBefore - Body->HealthComponent->GetCurrentHealth();
			if (Stage == EStage::HeavyFall)
			{
				const bool bUp = bBodyLanded && State.bDownedSeen && Knockdown && Knockdown->GetPhase() == EKnockdownPhase::None
					&& Knockdown->GetRecoveryCount() > State.RecoveriesBefore;
				if (bUp && State.PhaseTime > 2.f)
				{
					const float Expected = FallRules::FallDamage(BodyFall.GetLastFallHeight(), Config);
					const bool bOk = bAlive && BodyFall.GetLastFallOutcome() == FallRules::EFallOutcome::HeavyWound && BodyFall.GetHeavyWoundCount() == State.HeavyBefore + 1
						&& BodyFall.GetLastFallHeight() >= 900.f && BodyFall.GetLastFallHeight() < 1200.f && FMath::IsNearlyEqual(Lost, Expected, 0.6f)
						&& Lost >= Config.HeavyDamageMin - 0.5f && Lost <= Config.HeavyDamageMax + 0.5f && State.bAbpFalling;
					EndStage(State, bOk, FString::Printf(TEXT("in %.1f s: %s; health -%.1f (expected %.1f), alive %d, lay in the knockdown pose %d and got up; ABP %s"),
						State.PhaseTime, *FallNote(BodyFall), Lost, Expected, bAlive ? 1 : 0, State.bDownedSeen ? 1 : 0, *State.AbpStates));
					Body->GetCharacterMovement()->bCanWalkOffLedges = State.bHadWalkOff;
				}
				else if (State.PhaseTime > 40.f)
				{
					EndStage(State, false, FString::Printf(TEXT("TIMED OUT: landed %d, downed seen %d, phase %d, alive %d, health -%.1f, feet %.0f"), bBodyLanded ? 1 : 0,
						State.bDownedSeen ? 1 : 0, Knockdown ? static_cast<int32>(Knockdown->GetPhase()) : -1, bAlive ? 1 : 0, Lost, FeetZ(*Body)));
					Body->GetCharacterMovement()->bCanWalkOffLedges = State.bHadWalkOff;
				}
				return;
			}
			if (bBodyLanded && State.PhaseTime > 2.f)
			{
				const bool bOk = !bAlive && BodyFall.GetLastFallOutcome() == FallRules::EFallOutcome::Lethal && BodyFall.GetLastFallHeight() >= 1200.f;
				EndStage(State, bOk, FString::Printf(TEXT("in %.1f s: %s %s; outcome %d, dead %d"), State.PhaseTime, *Body->GetName(), *FallNote(BodyFall),
					static_cast<int32>(BodyFall.GetLastFallOutcome()), bAlive ? 0 : 1));
			}
			else if (State.PhaseTime > 30.f)
			{
				EndStage(State, false, FString::Printf(TEXT("TIMED OUT: landed %d, alive %d, feet %.0f"), bBodyLanded ? 1 : 0, bAlive ? 1 : 0, FeetZ(*Body)));
			}
			return;
		}
		case EStage::EnemyHeights:
		{
			AEnemyCharacter* Heavy = State.Enemy.Get();
			AEnemyCharacter* Dead = State.Enemy2.Get();
			if (Dead && Dead->LedgeFall->GetLandingCount() > 0 && State.DeadOutcome < 0)
			{
				State.DeadOutcome = static_cast<int32>(Dead->LedgeFall->GetLastFallOutcome());
				State.bDeadDied = !Dead->GetHealthComponent()->IsAlive();
				State.DeadNote = FallNote(*Dead->LedgeFall);
			}
			if (!Heavy || !State.bPushed || (!Dead && State.DeadOutcome < 0))
			{
				EndStage(State, false, FString::Printf(TEXT("no Frostbittens / platforms (platforms %d, 10.5 m %d, 13 m %d)"), State.Platforms.Num(), Heavy ? 1 : 0, Dead ? 1 : 0));
				return;
			}
			State.bDownedSeen |= Heavy->KnockdownComponent && Heavy->KnockdownComponent->GetPhase() == EKnockdownPhase::Downed;
			const bool bBothLanded = Heavy->LedgeFall->GetLandingCount() > 0 && State.DeadOutcome >= 0;
			if (bBothLanded && State.PhaseTime > 3.f && State.bDownedSeen)
			{
				const bool bOk = Heavy->GetHealthComponent()->IsAlive() && Heavy->LedgeFall->GetLastFallOutcome() == FallRules::EFallOutcome::HeavyWound
					&& State.bDeadDied && State.DeadOutcome == static_cast<int32>(FallRules::EFallOutcome::Lethal);
				EndStage(State, bOk, FString::Printf(TEXT("in %.1f s: 10.5 m - %s, alive %d, knocked down %d; 13 m - %s, dead %d"), State.PhaseTime, *FallNote(*Heavy->LedgeFall),
					Heavy->GetHealthComponent()->IsAlive() ? 1 : 0, State.bDownedSeen ? 1 : 0, *State.DeadNote, State.bDeadDied ? 1 : 0));
				Heavy->Destroy();
				if (Dead)
				{
					Dead->Destroy();
				}
			}
			else if (State.PhaseTime > 30.f)
			{
				EndStage(State, false, FString::Printf(TEXT("TIMED OUT: 10.5 m landings %d at feet %.0f, downed %d, alive %d; 13 m outcome %d, dead %d"), Heavy->LedgeFall->GetLandingCount(),
					FeetZ(*Heavy), State.bDownedSeen ? 1 : 0, Heavy->GetHealthComponent()->IsAlive() ? 1 : 0, State.DeadOutcome, State.bDeadDied ? 1 : 0));
				Heavy->Destroy();
				if (Dead)
				{
					Dead->Destroy();
				}
			}
			return;
		}
		case EStage::EnemyChase:
		case EStage::JumperGap:
		case EStage::BruteNoDrops:
		{
			AEnemyCharacter* Enemy = State.Enemy.Get();
			if (!Enemy)
			{
				EndStage(State, false, TEXT("no enemy spawned"));
				return;
			}
			const AAIController* AI = Cast<AAIController>(Enemy->GetController());
			const UClass* Filter = AI ? AI->GetDefaultNavigationFilterClass().Get() : nullptr;
			const ULedgeFallComponent& EnemyFall = *Enemy->LedgeFall;
			State.MinEnemyFeet = State.PhaseTime > 1.f ? FMath::Min(State.MinEnemyFeet, FeetZ(*Enemy)) : State.MinEnemyFeet; // after the spawn settled
			const float ToLeader = static_cast<float>(FVector::Dist2D(Enemy->GetActorLocation(), Leader->GetActorLocation()));
			if (Stage == EStage::BruteNoDrops)
			{
				if (State.PhaseTime > 10.f)
				{
					const bool bOk = Filter == UNavFilter_NoVault::StaticClass() && Enemy->GetDropLinkUse() == FallRules::EEnemyDropUse::None && !State.bPathHasDrop
						&& EnemyFall.GetDropCount() == 0 && EnemyFall.GetLandingCount() == 0 && State.MinEnemyFeet > State.GroundZ + 450.f;
					EndStage(State, bOk, FString::Printf(TEXT("filter %s, drop use %d, path drop %d, drops %d, falls %d, lowest feet %.0f (the ruin roof at %.0f)"), *GetNameSafe(Filter),
						static_cast<int32>(Enemy->GetDropLinkUse()), State.bPathHasDrop ? 1 : 0, EnemyFall.GetDropCount(), EnemyFall.GetLandingCount(), State.MinEnemyFeet, State.GroundZ + 600.f));
					Enemy->Destroy();
				}
				return;
			}
			const UClass* Wanted = Stage == EStage::JumperGap ? UNavFilter_EnemyDropJumper::StaticClass() : UNavFilter_EnemyDropFree::StaticClass();
			if (EnemyFall.GetDropCount() > 0 && EnemyFall.GetLandingCount() > 0 && !EnemyFall.IsBusy() && ToLeader <= 350.f)
			{
				const bool bOk = Filter == Wanted && State.bPathHasDrop && EnemyFall.GetLastFallKind() == ELedgeFallKind::DropLink && EnemyFall.DidLastDropLandOnTarget()
					&& EnemyFall.GetLastFallDamage() < 0.5f && Enemy->GetHealthComponent()->IsAlive();
				EndStage(State, bOk, FString::Printf(TEXT("in %.1f s: %s (filter %s) %.0f cm from the leader: %s; path drop %d"), State.PhaseTime, *Enemy->GetName(), *GetNameSafe(Filter),
					ToLeader, *FallNote(EnemyFall), State.bPathHasDrop ? 1 : 0));
				Enemy->Destroy();
			}
			else if (State.PhaseTime > 45.f)
			{
				EndStage(State, false, FString::Printf(TEXT("TIMED OUT: %s (filter %s) at %s, %.0f cm from the leader, drops %d, landings %d, fall state %d, path drop %d"), *Enemy->GetName(),
					*GetNameSafe(Filter), *Enemy->GetActorLocation().ToCompactString(), ToLeader, EnemyFall.GetDropCount(), EnemyFall.GetLandingCount(),
					static_cast<int32>(EnemyFall.GetState()), State.bPathHasDrop ? 1 : 0));
				Enemy->Destroy();
			}
			return;
		}
		default:
			return;
		}
	}

	bool Step(TSharedRef<FState> StateRef)
	{
		FState& State = *StateRef;
		UWorld* World = State.World.Get();
		if (!World || State.Phase == EPhase::Done)
		{
			return false;
		}
		const float Dt = World->IsPaused() ? 0.f : StepSeconds;
		State.Time += Dt;
		State.PhaseTime += Dt;
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		UDropLinkSubsystem* Links = World->GetSubsystem<UDropLinkSubsystem>();
		if (State.Time > TotalLimitSeconds)
		{
			Check(State, false, TEXT("the smoke ran out of time"));
			return Finish(State, false);
		}
		switch (State.Phase)
		{
		case EPhase::WaitLinks:
		{
			const ULevelExitSubsystem* Exit = World->GetSubsystem<ULevelExitSubsystem>();
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			if (!Leader || !Links || !Links->HasGenerated() || (Nav && Nav->IsNavigationBuildInProgress()) || State.PhaseTime < 4.f
				|| (Exit && Exit->IsStartFadeResolved() && Exit->GetStartFadeAlpha() > 0.f && State.PhaseTime < 20.f))
			{
				if (State.PhaseTime > 120.f)
				{
					Check(State, false, FString::Printf(TEXT("the drop links were generated (subsystem %d, generated %d)"), Links ? 1 : 0, Links && Links->HasGenerated() ? 1 : 0));
					return Finish(State, false);
				}
				return true;
			}
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member)
				{
					Member->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				}
			}
			Check(State, ReadLayout(State), TEXT("the accepted layout gives the ruin, its annex and the X_ne bridge"));
			PickLinks(State, World, Leader);
			int32 Hurt = 0;
			int32 IntoCut = 0;
			for (const FDropLink& Link : Links->GetLinks())
			{
				Hurt += Link.bHurt ? 1 : 0;
				IntoCut += FMath::Abs(Link.End.Z - (State.GroundZ - 300.f)) < 60.f && State.BridgeNE.Contains(Link.Start, 50.f) ? 1 : 0;
			}
			Check(State, Links->GetLinks().Num() > 0, FString::Printf(TEXT("%d drop links generated (%d hurt) in %.0f ms, %d off the X_ne bridge into the cut"),
				Links->GetLinks().Num(), Hurt, Links->GetGenerationMs(), IntoCut));
			Check(State, IntoCut > 0, TEXT("a drop link off the X_ne bridge into the cut"));
			Check(State, State.RoofLink != INDEX_NONE, TEXT("a drop link from the ruin roof (+2) onto the annex roof (+1) across the gap"));
			Check(State, State.HurtLink != INDEX_NONE, TEXT("a hurting drop link from the ruin roof to the ground that a path takes"));
			if (IConsoleVariable* Immortal = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Cheat.Immortal")))
			{
				Immortal->Set(0, ECVF_SetByConsole); // the falls must hurt
			}
			if (State.RoofLink == INDEX_NONE || State.HurtLink == INDEX_NONE)
			{
				return Finish(State, false);
			}
			State.Stage = State.Only >= 0 ? State.Only : 0;
			State.Phase = EPhase::Setup;
			State.PhaseTime = 0.f;
			return true;
		}
		case EPhase::Setup:
			if (State.Stage >= static_cast<int32>(EStage::Count) || (State.Only >= 0 && State.Stage != State.Only))
			{
				return Finish(State, true);
			}
			if (!Leader || !Leader->LedgeFall)
			{
				Check(State, false, TEXT("a squad leader with a fall component"));
				return Finish(State, false);
			}
			if (Leader->IsKnockedDown() && State.PhaseTime < 15.f)
			{
				return true; // the last stage's knockdown gets up first
			}
			Setup(State, World, *Squad);
			return true;
		case EPhase::Order:
			if (State.PhaseTime < 1.2f)
			{
				return true; // the teleport settles
			}
			Order(State, World, *Squad);
			return true;
		case EPhase::Run:
			Run(State, World, *Squad, Dt);
			return true;
		default:
			return false;
		}
	}

	void RunCommand(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TSharedRef<FState> State = MakeShared<FState>();
		State->World = World;
		for (const FString& Arg : Args)
		{
			if (Arg.StartsWith(TEXT("only=")))
			{
				State->Only = FMath::Clamp(FCString::Atoi(*Arg.Mid(5)) - 1, 0, static_cast<int32>(EStage::Count) - 1);
			}
		}
		if (IConsoleVariable* Pressure = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Pressure.Enabled")))
		{
			Pressure->Set(0, ECVF_SetByConsole); // a movement check, not a fight
		}
		// Nor the Depot's level-JSON PMC squads (30 s / 120 s), whose rounds made the men drop prone / step aside under fire (#43) in the
		// scripted knockback / fall stages.
		for (const TCHAR* Name : { TEXT("Codex.Pmc.Encounters"), TEXT("Codex.Combat.UnderFireManoeuvre") })
		{
			if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
			{
				Variable->Set(0, ECVF_SetByConsole);
			}
		}
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float)
		{
			return Step(State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.FallDropSmoke"),
		TEXT("Dev check (2026-11-03, docs/systems/stances-and-movement.md 'Falls and drops') on L_Gen_Depot_1: the generated drop links, the squad dropping off ")
		TEXT("the bridge into the cut by a move order (one at a time, the HUD marker), a 6 m walk-off with fall damage, a sprint roof-to-roof drop that lands and a ")
		TEXT("slow one that falls short, a knockback throw over the roof edge, none at the cut wall, an enemy thrown too; the ABP fall states; 9-12 m heavy wounds ")
		TEXT("and 12 m+ deaths off test platforms (operatives, enemies); enemies chasing through drop links (a Frostbitten, a jumping hound; never a Brute); ")
		TEXT("[only=<1..13>]. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunCommand));
}

#endif // !UE_BUILD_SHIPPING
