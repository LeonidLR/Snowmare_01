// #51 probe (the game designer's video 2026-11-06: PMC fighters are thrown back while firing): watches every live operative / PMC fighter's
// torso in its own actor space during a real fight and logs each sudden torso jump with what the body was doing (the montages playing, stance,
// speed, the last shot / hit), then a per-team summary. Run it next to anything that makes them fight, headless:
//   -ExecCmds="CodexTactics.RecoilProbe seconds=90,CodexTactics.PmcArena rounds=1 seed=7 opponent=pmc time=90"
// Args: seconds=<s> (default 60), jump=<cm> (default 18: spine_03 moving this far within 0.2 s in the actor's frame), pop=<cm> (default 15: the
// torso moving this far in the world in one frame - faster than a sprint), exit=1 (quit after the summary).
// Not a smoke (no PASS / FAIL): a measuring tool, listed in Scripts/test_map.json nonSmokeDebugCommands.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/OperativeAnimInstance.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/WeaponDataAsset.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CoreDelegates.h"

namespace RecoilProbe
{
	struct FSample
	{
		double Time = 0.0;
		FVector Torso = FVector::ZeroVector;
		FVector MeshRel = FVector::ZeroVector;
		int32 Stance = 0;
		bool bInCover = false;
	};

	struct FTrack
	{
		TWeakObjectPtr<AOperativeCharacter> Who;
		TArray<FSample> History;
		float MaxJump = 0.f;
		int32 Events = 0;
		double LastEvent = -10.0;
		int32 LastClip = -1;
		double LastFireTime = -10.0;
		FVector LastWorld = FVector::ZeroVector;
		bool bHasWorld = false;
		float MaxPop = 0.f;
		int32 Pops = 0;
		double LastPop = -10.0;
	};

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		FDelegateHandle Handle;
		double Start = 0.0;
		float Seconds = 60.f;
		float JumpCm = 18.f;
		float PopCm = 15.f;
		bool bExit = false;
		TMap<const AOperativeCharacter*, FTrack> Tracks;
		TMap<FString, int32> CauseCount;
	};

	FString ClipName(const UAnimMontage* Montage)
	{
		if (!Montage)
		{
			return TEXT("?");
		}
		for (const FSlotAnimationTrack& Track : Montage->SlotAnimTracks)
		{
			for (const FAnimSegment& Segment : Track.AnimTrack.AnimSegments)
			{
				if (const UAnimSequenceBase* Clip = Segment.GetAnimReference())
				{
					return FString::Printf(TEXT("%s@%s"), *Clip->GetName(), *Track.SlotName.ToString());
				}
			}
		}
		return Montage->GetName();
	}

	FString Montages(const AOperativeCharacter& Who)
	{
		const UAnimInstance* Anim = Who.GetMesh() ? Who.GetMesh()->GetAnimInstance() : nullptr;
		FString Out;
		if (Anim)
		{
			for (const FAnimMontageInstance* Instance : Anim->MontageInstances)
			{
				if (Instance && Instance->Montage && Instance->GetWeight() > 0.05f)
				{
					Out += FString::Printf(TEXT("%s%s w%.2f"), Out.IsEmpty() ? TEXT("") : TEXT(", "), *ClipName(Instance->Montage), Instance->GetWeight());
				}
			}
		}
		return Out.IsEmpty() ? TEXT("none") : Out;
	}

	void Sample(FState& State)
	{
		UWorld* World = State.World.Get();
		if (!World)
		{
			return;
		}
		const double Now = World->GetTimeSeconds();
		static const FName Torso(TEXT("spine_03"));
		for (TActorIterator<AOperativeCharacter> It(World); It; ++It)
		{
			AOperativeCharacter* Who = *It;
			USkeletalMeshComponent* Mesh = Who ? Who->GetMesh() : nullptr;
			if (!Mesh || !Who->HealthComponent || !Who->HealthComponent->IsAlive() || Mesh->GetBoneIndex(Torso) == INDEX_NONE)
			{
				continue;
			}
			FTrack& Track = State.Tracks.FindOrAdd(Who);
			if (!Track.Who.IsValid())
			{
				Track.Who = Who;
				// Headless meshes are never rendered: without this their pose is not evaluated.
				Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			}
			const int32 Clip = Who->CurrentClip;
			if (Track.LastClip >= 0 && Clip < Track.LastClip)
			{
				Track.LastFireTime = Now; // a round left the magazine
			}
			Track.LastClip = Clip;
			const FVector TorsoWorld = Mesh->GetBoneLocation(Torso);
			const FVector Local = Who->GetActorTransform().InverseTransformPosition(TorsoWorld);
			// A pop: the torso moving further in ONE frame than a sprint carries it (550 cm/s = 9 cm a frame at 60 fps) - what the eye sees.
			const float Pop = Track.bHasWorld ? static_cast<float>(FVector::Dist(TorsoWorld, Track.LastWorld)) : 0.f;
			const FVector PopFrom = Track.LastWorld;
			Track.LastWorld = TorsoWorld;
			Track.bHasWorld = true;
			Track.MaxPop = FMath::Max(Track.MaxPop, Pop);
			if (Pop >= State.PopCm && Now - Track.LastPop > 0.25)
			{
				Track.LastPop = Now;
				++Track.Pops;
				const bool bFiringNow = Now - Track.LastFireTime < 0.4;
				State.CauseCount.FindOrAdd(FString::Printf(TEXT("POP team %d, %s, stance %d, cover %d, %s"), Who->TeamId, bFiringNow ? TEXT("firing") : TEXT("not firing"),
					static_cast<int32>(Who->GetStance()), Who->bInCover ? 1 : 0, *Montages(*Who)))++;
				UE_LOG(LogCodexTactics, Display, TEXT("RecoilProbe POP: t=%.2f %s (team %d, %s) torso moved %.1f cm in one frame (world %s <- %s, local %s); %s; stance %d, cover %d, speed %.0f; mesh rel %s; montages: %s"),
					Now - State.Start, *Who->GetName(), Who->TeamId, Who->CurrentWeapon ? *Who->CurrentWeapon->WeaponId : TEXT("-"), Pop, *TorsoWorld.ToCompactString(), *PopFrom.ToCompactString(),
					*Local.ToCompactString(), bFiringNow ? TEXT("FIRING") : TEXT("not firing"), static_cast<int32>(Who->GetStance()), Who->bInCover ? 1 : 0, Who->GetVelocity().Size2D(),
					*Mesh->GetRelativeLocation().ToCompactString(), *Montages(*Who));
			}
			Track.History.Add({ Now, Local, Mesh->GetRelativeLocation(), static_cast<int32>(Who->GetStance()), Who->bInCover });
			while (Track.History.Num() > 2 && Now - Track.History[0].Time > 0.2)
			{
				Track.History.RemoveAt(0);
			}
			float Jump = 0.f;
			const FSample* From = nullptr;
			for (const FSample& Old : Track.History)
			{
				const float D = static_cast<float>(FVector::Dist(Old.Torso, Local));
				if (D > Jump)
				{
					Jump = D;
					From = &Old;
				}
			}
			Track.MaxJump = FMath::Max(Track.MaxJump, Jump);
			if (Jump >= State.JumpCm && Now - Track.LastEvent > 0.5)
			{
				Track.LastEvent = Now;
				++Track.Events;
				const FString Playing = Montages(*Who);
				const UOperativeAnimInstance* Anim = Cast<UOperativeAnimInstance>(Mesh->GetAnimInstance());
				const bool bFiring = Now - Track.LastFireTime < 0.4;
				State.CauseCount.FindOrAdd(FString::Printf(TEXT("team %d, %s, %s"), Who->TeamId, bFiring ? TEXT("firing") : TEXT("not firing"), *Playing))++;
				UE_LOG(LogCodexTactics, Display, TEXT("RecoilProbe: t=%.2f %s (team %d, %s) torso jumped %.1f cm in 0.2 s (local %s <- %s, stance %d <- %d, cover %d <- %d, mesh rel <- %s); %s; stance %d, speed %.0f, aiming %d, "
					"knocked down %d, HP %.0f%%; mesh rel %s, root cs %s, pelvis cs %s; montages: %s"), Now - State.Start, *Who->GetName(), Who->TeamId, Who->CurrentWeapon ? *Who->CurrentWeapon->WeaponId : TEXT("-"),
					Jump, *Local.ToCompactString(), From ? *From->Torso.ToCompactString() : TEXT("-"), static_cast<int32>(Who->GetStance()), From ? From->Stance : -1,
					Who->bInCover ? 1 : 0, From && From->bInCover ? 1 : 0, From ? *From->MeshRel.ToCompactString() : TEXT("-"), bFiring ? TEXT("FIRING (a round < 0.4 s ago)") : TEXT("not firing"), static_cast<int32>(Who->GetStance()),
					Who->GetVelocity().Size2D(), Anim && Anim->bIsAiming ? 1 : 0, Who->IsKnockedDown() ? 1 : 0, Who->HealthComponent->GetHealthFraction() * 100.f, *Mesh->GetRelativeLocation().ToCompactString(),
					*Mesh->GetBoneLocation(TEXT("root"), EBoneSpaces::ComponentSpace).ToCompactString(), *Mesh->GetBoneLocation(TEXT("pelvis"), EBoneSpaces::ComponentSpace).ToCompactString(), *Playing);
			}
		}
		if (Now - State.Start < State.Seconds)
		{
			return;
		}
		FCoreDelegates::OnEndFrame.Remove(State.Handle);
		TMap<int32, TPair<float, int32>> PerTeam;
		TMap<int32, TPair<float, int32>> PopTeam;
		for (const TPair<const AOperativeCharacter*, FTrack>& Pair : State.Tracks)
		{
			const AOperativeCharacter* Who = Pair.Value.Who.Get();
			const int32 Team = Who ? Who->TeamId : -1;
			TPair<float, int32>& Row = PerTeam.FindOrAdd(Team, TPair<float, int32>(0.f, 0));
			Row.Key = FMath::Max(Row.Key, Pair.Value.MaxJump);
			Row.Value += Pair.Value.Events;
			TPair<float, int32>& PopRow = PopTeam.FindOrAdd(Team, TPair<float, int32>(0.f, 0));
			PopRow.Key = FMath::Max(PopRow.Key, Pair.Value.MaxPop);
			PopRow.Value += Pair.Value.Pops;
		}
		for (const TPair<int32, TPair<float, int32>>& Row : PopTeam)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("RecoilProbe summary: team %d: worst one-frame torso pop %.1f cm, %d pops >= %.0f cm"), Row.Key, Row.Value.Key, Row.Value.Value, State.PopCm);
		}
		for (const TPair<int32, TPair<float, int32>>& Row : PerTeam)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("RecoilProbe summary: team %d: worst torso jump %.1f cm / 0.2 s, %d jumps >= %.0f cm"), Row.Key, Row.Value.Key, Row.Value.Value, State.JumpCm);
		}
		for (const TPair<FString, int32>& Cause : State.CauseCount)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("RecoilProbe cause: %dx %s"), Cause.Value, *Cause.Key);
		}
		if (State.bExit && GEngine)
		{
			FPlatformMisc::RequestExit(false, TEXT("RecoilProbe"));
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TSharedRef<FState> State = MakeShared<FState>();
		State->World = World;
		State->Start = World->GetTimeSeconds();
		for (const FString& Arg : Args)
		{
			FString Key;
			FString Value;
			if (Arg.Split(TEXT("="), &Key, &Value))
			{
				if (Key.Equals(TEXT("seconds"), ESearchCase::IgnoreCase))
				{
					State->Seconds = FCString::Atof(*Value);
				}
				else if (Key.Equals(TEXT("jump"), ESearchCase::IgnoreCase))
				{
					State->JumpCm = FCString::Atof(*Value);
				}
				else if (Key.Equals(TEXT("pop"), ESearchCase::IgnoreCase))
				{
					State->PopCm = FCString::Atof(*Value);
				}
				else if (Key.Equals(TEXT("exit"), ESearchCase::IgnoreCase))
				{
					State->bExit = Value == TEXT("1");
				}
			}
		}
		UE_LOG(LogCodexTactics, Display, TEXT("RecoilProbe: watching the torsos for %.0f s (a jump = spine_03 moving >= %.0f cm in 0.2 s)"), State->Seconds, State->JumpCm);
		State->Handle = FCoreDelegates::OnEndFrame.AddLambda([State]() { Sample(*State); });
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.RecoilProbe"),
		TEXT("#51 probe: logs every sudden torso jump of the operatives / PMC fighters in a fight with what they were doing. Args: seconds=60 jump=18 pop=15 exit=0|1."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
