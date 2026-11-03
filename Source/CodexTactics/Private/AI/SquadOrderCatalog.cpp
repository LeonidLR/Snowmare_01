#include "AI/SquadOrderCatalog.h"

#include "Characters/OperativeCharacter.h"
#include "Combat/HealthComponent.h"
#include "Data/WeaponDataAsset.h"

namespace SquadOrderCatalog
{
	namespace SquadOrderCatalogLocal
	{
		inline bool Gate(const FSquadOrderContext& Context)
		{
			return IsOperativeOrderable(Context.Operative);
		}

		inline bool HasWeapon(const AOperativeCharacter& Operative, const FString& WeaponId)
		{
			for (const TObjectPtr<UWeaponDataAsset>& Weapon : Operative.AvailableWeapons)
			{
				if (Weapon && Weapon->WeaponId == WeaponId)
				{
					return true;
				}
			}
			return false;
		}

		TArray<FSquadOrderEntry> Build()
		{
			TArray<FSquadOrderEntry> Entries;
			Entries.SetNum(static_cast<int32>(ESquadOrder::Count));
			auto Add = [&Entries](ESquadOrder Id, const TCHAR* Name, const TCHAR* Description, TFunction<bool(const FSquadOrderContext&)> CanDo,
				TFunction<bool(const FSquadOrderContext&)> Execute)
			{
				FSquadOrderEntry& Entry = Entries[static_cast<int32>(Id)];
				Entry.Id = Id;
				Entry.Name = Name;
				Entry.Description = Description;
				Entry.CanDo = MoveTemp(CanDo);
				Entry.Execute = MoveTemp(Execute);
			};

			Add(ESquadOrder::Move, TEXT("Move"), TEXT("Walk / sprint to a point over the navmesh."),
				[](const FSquadOrderContext& C) { return Gate(C) && !C.Location.IsNearlyZero(); },
				[](const FSquadOrderContext& C)
				{
					if (C.Operative->bInCover)
					{
						C.Operative->LeaveCoverToMove(TEXT("order catalog: move"));
					}
					return C.Operative->OrderMoveTo(C.Location, C.bSprint) == EOperativeOrderResult::Accepted;
				});

			Add(ESquadOrder::TakeCover, TEXT("TakeCover"), TEXT("Walk to a cover slot and press against the wall."),
				[](const FSquadOrderContext& C) { return Gate(C) && C.Cover.IsValid(); },
				[](const FSquadOrderContext& C)
				{
					// The operative's own cover order: the walk, then EnterCover on arrival (the player's click does the same).
					return C.Operative->OrderTakeCover(C.Cover, C.bSprint) == EOperativeOrderResult::Accepted;
				});

			Add(ESquadOrder::LeaveCover, TEXT("LeaveCover"), TEXT("Step off the wall."),
				[](const FSquadOrderContext& C) { return Gate(C) && (C.Operative->bInCover || C.Operative->HasPendingCover()); },
				[](const FSquadOrderContext& C)
				{
					C.Operative->ClearPendingCover();
					if (C.Operative->bInCover)
					{
						C.Operative->LeaveCover(TEXT("order catalog: leave cover"));
					}
					return true;
				});

			Add(ESquadOrder::Stance, TEXT("Stance"), TEXT("Stand / crouch / prone."),
				[](const FSquadOrderContext& C) { return Gate(C); },
				[](const FSquadOrderContext& C)
				{
					C.Operative->SetStance(C.Stance);
					return true;
				});

			Add(ESquadOrder::FirePosture, TEXT("FirePosture"), TEXT("The fighter's own fire posture (Passive / Defensive / Aggressive)."),
				[](const FSquadOrderContext& C) { return C.Operative != nullptr; },
				[](const FSquadOrderContext& C)
				{
					C.Operative->bHasPostureOverride = true;
					C.Operative->PostureOverride = C.Posture;
					return true;
				});

			Add(ESquadOrder::PriorityTarget, TEXT("PriorityTarget"), TEXT("Fire at this target first while it can be hit."),
				[](const FSquadOrderContext& C) { return Gate(C) && IsValid(C.Target); },
				[](const FSquadOrderContext& C)
				{
					C.Operative->SetManualPriorityTarget(C.Target);
					return true;
				});

			Add(ESquadOrder::Suppress, TEXT("Suppress"), TEXT("Suppressive fire on a target / point."),
				[](const FSquadOrderContext& C) { return Gate(C) && (IsValid(C.Target) || !C.Location.IsNearlyZero()) && C.Operative->UsesAmmo(); },
				[](const FSquadOrderContext& C)
				{
					const FVector Point = C.Location.IsNearlyZero() && C.Target ? C.Target->GetActorLocation() : C.Location;
					return C.Operative->OrderSuppress(C.Target, Point);
				});

			Add(ESquadOrder::CeaseSuppress, TEXT("CeaseSuppress"), TEXT("Stop the suppressive fire."),
				[](const FSquadOrderContext& C) { return C.Operative && C.Operative->HasSuppressOrder(); },
				[](const FSquadOrderContext& C)
				{
					C.Operative->CancelSuppressFire(TEXT("order catalog"));
					return true;
				});

			Add(ESquadOrder::Heal, TEXT("Heal"), TEXT("Spend a medkit on a mate within reach."),
				[](const FSquadOrderContext& C)
				{
					return Gate(C) && C.Operative->MedkitsCount > 0 && IsValid(C.Patient) && C.Patient != C.Operative && C.Patient->HealthComponent
						&& C.Patient->HealthComponent->IsAlive() && C.Patient->HealthComponent->GetCurrentHealth() < C.Patient->HealthComponent->GetMaxHealth();
				},
				[](const FSquadOrderContext& C) { return C.Operative->HealAlly(*C.Patient); });

			Add(ESquadOrder::Reload, TEXT("Reload"), TEXT("Reload the weapon in hands."),
				[](const FSquadOrderContext& C)
				{
					return Gate(C) && C.Operative->UsesAmmo() && !C.Operative->bIsReloading && C.Operative->ReserveAmmo > 0 && C.Operative->CurrentWeapon
						&& C.Operative->CurrentClip < C.Operative->CurrentWeapon->MaxClipSize;
				},
				[](const FSquadOrderContext& C)
				{
					C.Operative->StartReload();
					return C.Operative->bIsReloading;
				});

			Add(ESquadOrder::SwitchWeapon, TEXT("SwitchWeapon"), TEXT("Switch to an arsenal weapon by id."),
				[](const FSquadOrderContext& C) { return Gate(C) && !C.WeaponId.IsEmpty() && HasWeapon(*C.Operative, C.WeaponId); },
				[](const FSquadOrderContext& C) { return C.Operative->SwitchToWeaponById(C.WeaponId); });

			Add(ESquadOrder::Hold, TEXT("Hold"), TEXT("Stop where he stands (his combat facing turns him to the target by itself)."),
				[](const FSquadOrderContext& C) { return Gate(C); },
				[](const FSquadOrderContext& C)
				{
					C.Operative->StopOperative();
					return true;
				});
			return Entries;
		}
	}

	const TArray<FSquadOrderEntry>& All()
	{
		static const TArray<FSquadOrderEntry> Entries = SquadOrderCatalogLocal::Build();
		return Entries;
	}

	const FSquadOrderEntry* Find(ESquadOrder Order)
	{
		const int32 Index = static_cast<int32>(Order);
		return All().IsValidIndex(Index) ? &All()[Index] : nullptr;
	}

	const TCHAR* Name(ESquadOrder Order)
	{
		const FSquadOrderEntry* Entry = Find(Order);
		return Entry ? Entry->Name : TEXT("?");
	}

	ESquadOrder Parse(const FString& InName)
	{
		for (const FSquadOrderEntry& Entry : All())
		{
			if (InName.Equals(Entry.Name, ESearchCase::IgnoreCase))
			{
				return Entry.Id;
			}
		}
		return ESquadOrder::Count;
	}

	bool IsOperativeOrderable(const AOperativeCharacter* Operative)
	{
		return IsValid(Operative) && Operative->HealthComponent && Operative->HealthComponent->IsAlive() && !Operative->IsKilledInAction()
			&& !Operative->IsKnockedDown() && !Operative->IsRaging() && !Operative->IsPanicking();
	}

	bool CanDo(ESquadOrder Order, const FSquadOrderContext& Context)
	{
		const FSquadOrderEntry* Entry = Find(Order);
		return Entry && Entry->CanDo && Entry->CanDo(Context);
	}

	bool Execute(ESquadOrder Order, const FSquadOrderContext& Context)
	{
		const FSquadOrderEntry* Entry = Find(Order);
		return Entry && Entry->CanDo && Entry->Execute && Entry->CanDo(Context) && Entry->Execute(Context);
	}
}
