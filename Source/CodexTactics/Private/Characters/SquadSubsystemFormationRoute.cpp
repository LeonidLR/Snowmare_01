// USquadSubsystem: one route round an obstacle for the group move into the wall layout (the game designer's play-test 2026-11-01 on
// L_FormationLab, approved fixes d / e; behind Codex.Formation.Sectors + Codex.Formation.V21 + Codex.Formation.WallRoute). Godot reference:
// none (UE-only). The pure rule is FormationWallRoute (Formation/FormationWallRouteRules): a follower whose straight way to his wall slot is
// cut by a wall walks a column ON the leader's path since the order until he is round the obstacle.

#include "Characters/SquadSubsystem.h"

#include "Characters/OperativeCharacter.h"
#include "CodexTactics.h"
#include "Formation/FormationGuardRuntimeRules.h"
#include "Formation/FormationRuntimeRules.h"
#include "Formation/FormationTuning.h"
#include "Formation/FormationWallRouteRules.h"
#include "HAL/IConsoleManager.h"

namespace SquadSubsystemFormationRouteLocal
{
	TAutoConsoleVariable<int32> CVarFormationWallRoute(TEXT("Codex.Formation.WallRoute"), 1,
		TEXT("Game designer play-test 2026-11-01 (fixes d / e): 1 = a follower whose straight way to his wall-layout slot is cut by a wall walks the leader's route (a column on his path) until he is round it - the squad never splits round different ends; 0 = each walks his own navmesh path to his slot (the old rule, A/B)"));
}

bool USquadSubsystem::IsFormationWallRouted(const AOperativeCharacter* Operative) const
{
	EFormationRole Role;
	return Operative && Sectors.WallRouteRoles.Num() > 0 && Operative != Leader.Get() && GetFormationRole(Operative, Role) && Sectors.WallRouteRoles.Contains(Role);
}

bool USquadSubsystem::IsFormationWallRouteEnabled()
{
	return SquadSubsystemFormationRouteLocal::CVarFormationWallRoute.GetValueOnGameThread() != 0;
}

void USquadSubsystem::ResetFormationWallRoute()
{
	Sectors.WallRouteRoles.Reset();
	Sectors.WallRouteTrail.Reset();
	Sectors.WallRouteColumn.Reset();
	Sectors.WallRouteRouted.Reset();
	Sectors.WallRouteReleased.Reset();
	Sectors.bWallRouteDealt = false;
	Sectors.bWallRouteStarted = false;
}

void USquadSubsystem::ApplyFormationWallRoute(const AOperativeCharacter& LeaderRef, const FVector2D& LeaderPos, int32 Count)
{
	using namespace SquadSubsystemFormationRouteLocal;
	Sectors.WallRouteRoles.Reset();
	if (!Sectors.bWallOrder || Sectors.bSlide || CVarFormationWallRoute.GetValueOnGameThread() == 0 || !FormationGuardRuntime::IsEnabled()
		|| !Sectors.bWorldWallsValid)
	{
		return;
	}
	// The leader's path since the order (a teleport starts it afresh, as the guard's trail).
	if (Sectors.WallRouteTrail.Num() > 0
		&& FVector2D::Distance(Sectors.WallRouteTrail.Last(), LeaderPos) > FormationRuntime::MaxStepDistanceCm + FormationGuardRuntime::TrailStepCm)
	{
		Sectors.WallRouteTrail.Reset();
	}
	FormationGuardRuntime::PushTrail(Sectors.WallRouteTrail, LeaderPos);

	// Who still has a wall between him and his slot (the released stay released: no flip back at a corner).
	TArray<TWeakObjectPtr<AOperativeCharacter>> Routed;
	TMap<TWeakObjectPtr<AOperativeCharacter>, EFormationRole> RoutedRole;
	TArray<FString> Released;
	for (const FFollowerState& Follower : Followers)
	{
		AOperativeCharacter* Operative = Follower.Operative.Get();
		const TOptional<EFormationRole> Role = SectorRoleOf(Follower);
		if (!Operative || !Role.IsSet() || IsFormationPeeker(Operative) || Sectors.WallRouteReleased.Contains(Follower.Operative))
		{
			continue;
		}
		const EFormationRole Want = Role.GetValue();
		const FFormationSlot* Slot = Sectors.WallSlots.FindByPredicate([Want](const FFormationSlot& Each) { return Each.Role == Want; });
		if (!Slot)
		{
			continue;
		}
		if (FormationWallRoute::NeedsRoute(FVector2D(Operative->GetActorLocation()), Slot->Position, Sectors.WorldWalls))
		{
			Routed.Add(Follower.Operative);
			RoutedRole.Add(Follower.Operative, Want);
			Sectors.WallRouteRouted.Add(Follower.Operative);
		}
		else if (Sectors.WallRouteRouted.Contains(Follower.Operative))
		{
			Sectors.WallRouteReleased.Add(Follower.Operative);
			Released.Add(Operative->DisplayName.ToString());
		}
	}
	for (const FString& Name : Released)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Formation] wall route: %s is round the obstacle - into his wall slot"), *Name);
	}
	if (Routed.IsEmpty())
	{
		// Everyone who walked the route is clear: the wall places re-dealt by travel from where they stand now (the order the column
		// arrived in), K keeps his - nobody crosses another at the far face.
		if (Sectors.WallRouteRouted.Num() > 0 && !Sectors.bWallRouteDealt)
		{
			Sectors.bWallRouteDealt = true;
			TArray<TWeakObjectPtr<AOperativeCharacter>> Who;
			TArray<FVector2D> From;
			TArray<FVector2D> To;
			TArray<EFormationRole> PlaceRoles;
			for (const FFormationSlot& Slot : Sectors.WallSlots)
			{
				if (Slot.Role != EFormationRole::Commander)
				{
					To.Add(Slot.Position);
					PlaceRoles.Add(Slot.Role);
				}
			}
			for (const TPair<TWeakObjectPtr<AOperativeCharacter>, EFormationRole>& Pair : Sectors.WallOrderRoles)
			{
				if (Pair.Key.IsValid() && Who.Num() < To.Num())
				{
					Who.Add(Pair.Key);
					From.Add(FVector2D(Pair.Key->GetActorLocation()));
				}
			}
			const TArray<int32> Assignment = FormationRuntime::AssignByTravel(From, To);
			if (Assignment.Num() == Who.Num())
			{
				FString Line;
				for (int32 Index = 0; Index < Who.Num(); ++Index)
				{
					Sectors.WallOrderRoles.Add(Who[Index], PlaceRoles[Assignment[Index]]);
					Line += FString::Printf(TEXT(" %s -> %s;"), *Who[Index]->DisplayName.ToString(), FormationNames::ToString(PlaceRoles[Assignment[Index]]));
				}
				UE_LOG(LogCodexTactics, Display, TEXT("[Formation] wall route over: the wall places by travel from the column:%s"), *Line);
			}
		}
		return;
	}
	// The column on the leader's path: place i at the column's distance back along it (never past where he started).
	const FFormationConfig& Config = FormationTuning::Get();
	const TArray<FormationWallRoute::FPlace> Places = FormationWallRoute::PlacesAlongTrail(Sectors.WallRouteTrail, FMath::Max(Count - 1, Routed.Num()),
		Config.ColumnBaseBack, Config.ColumnSpacing);
	if (Places.Num() < Routed.Num())
	{
		return;
	}
	// The column order: fixed when the route begins (by travel to the places then), a man routed later joins at its tail - no swapping.
	if (Sectors.WallRouteColumn.IsEmpty())
	{
		TArray<FVector2D> From;
		TArray<FVector2D> To;
		for (const TWeakObjectPtr<AOperativeCharacter>& Each : Routed)
		{
			From.Add(FVector2D(Each->GetActorLocation()));
		}
		for (const FormationWallRoute::FPlace& Place : Places)
		{
			To.Add(Place.Pos);
		}
		const TArray<int32> Assignment = FormationWallRoute::AssignColumnPlaces(From, To);
		TArray<TPair<int32, TWeakObjectPtr<AOperativeCharacter>>> Order;
		for (int32 Index = 0; Index < Routed.Num(); ++Index)
		{
			Order.Add(TPair<int32, TWeakObjectPtr<AOperativeCharacter>>(Assignment.IsValidIndex(Index) ? Assignment[Index] : Index, Routed[Index]));
		}
		Order.Sort([](const TPair<int32, TWeakObjectPtr<AOperativeCharacter>>& A, const TPair<int32, TWeakObjectPtr<AOperativeCharacter>>& B)
		{
			return A.Key < B.Key;
		});
		for (const TPair<int32, TWeakObjectPtr<AOperativeCharacter>>& Each : Order)
		{
			Sectors.WallRouteColumn.Add(Each.Value);
		}
	}
	for (const TWeakObjectPtr<AOperativeCharacter>& Each : Routed)
	{
		Sectors.WallRouteColumn.AddUnique(Each);
	}
	FString Line;
	int32 Next = 0;
	for (const TWeakObjectPtr<AOperativeCharacter>& Each : Sectors.WallRouteColumn)
	{
		const EFormationRole* Role = RoutedRole.Find(Each);
		if (!Role)
		{
			continue; // released / gone
		}
		const EFormationRole Want = *Role;
		FFormationSlot* Frame = Sectors.FrameSlots.FindByPredicate([Want](const FFormationSlot& Slot) { return Slot.Role == Want; });
		if (Frame && Places.IsValidIndex(Next))
		{
			Frame->Position = Places[Next].Pos;
			Frame->Yaw = Places[Next].Yaw;
			Sectors.WallRouteRoles.Add(Want);
			Line += FString::Printf(TEXT(" %s (%s)"), *Each->DisplayName.ToString(), FormationNames::ToString(Want));
		}
		++Next;
	}
	if (!Sectors.bWallRouteStarted)
	{
		Sectors.bWallRouteStarted = true;
		++Sectors.WallRouteCount;
		UE_LOG(LogCodexTactics, Display, TEXT("[Formation] wall route: a wall between%s and their wall slots - they walk the leader's route (a column on his path) until round it"),
			*Line);
	}
}
