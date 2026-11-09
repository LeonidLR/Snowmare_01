// Dev-only console command: give a weapon to the selected operative (the squad leader) and put it in his hands (2026-11-09; the RPG-7 is in
// the roster as "next" and in no loadout yet - any fighter can be the grenadier, the designer decides later):
//   CodexTactics.GiveWeapon rpg7            -> the leader
//   CodexTactics.GiveWeapon rpg7 2          -> squad member #2 (0-based)
// Loads /Game/Data/Weapons/DA_Weapon_<id> (its numbers already tuned from weapons_tuning.json at game start), adds it to the arsenal with
// its reserve (AOperativeCharacter::AddArsenalWeapon; an out-of-play weapon is refused) and switches to it.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Data/WeaponDataAsset.h"
#include "Data/WeaponRoster.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

namespace GiveWeaponCommand
{
	void Run(const TArray<FString>& Args, UWorld* World)
	{
		USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
		if (!Squad || Args.Num() < 1)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("GiveWeapon: usage CodexTactics.GiveWeapon <weapon_id> [squad index]"));
			return;
		}
		const FString WeaponId = Args[0].ToLower();
		AOperativeCharacter* Member = Squad->GetLeader();
		if (Args.Num() > 1)
		{
			const int32 Index = FCString::Atoi(*Args[1]);
			const TArray<AOperativeCharacter*> Members = Squad->GetMembers();
			Member = Members.IsValidIndex(Index) ? Members[Index] : nullptr;
		}
		if (!Member)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("GiveWeapon: no such operative"));
			return;
		}
		if (WeaponRoster::IsOutOfPlay(WeaponId))
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("GiveWeapon: \"%s\" is out of play (roster)"), *WeaponId);
			return;
		}
		const FString Name = FString::Printf(TEXT("DA_Weapon_%s"), *WeaponId);
		UWeaponDataAsset* Weapon = LoadObject<UWeaponDataAsset>(nullptr, *FString::Printf(TEXT("/Game/Data/Weapons/%s.%s"), *Name, *Name));
		if (!Weapon)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("GiveWeapon: no weapon asset %s"), *Name);
			return;
		}
		Member->AddArsenalWeapon(Weapon);
		const bool bInHands = Member->SwitchToWeaponById(WeaponId);
		UE_LOG(LogCodexTactics, Display, TEXT("GiveWeapon: %s now carries %s (in hands %d, clip %d / %d)"), *Member->DisplayName.ToString(), *WeaponId,
			bInHands ? 1 : 0, Member->CurrentClip, Member->ReserveAmmo);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.GiveWeapon"),
		TEXT("Dev: gives a weapon (DA_Weapon_<id>, e.g. rpg7) to the squad leader or squad member #N and puts it in his hands."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
