#include "Core/PlayerCommands.h"
#include "CodexTactics.h"
#include "GameFramework/Actor.h"
#include "Subsystems/CodexEventBus.h"

namespace PlayerCommands
{
	const FName CameraPan(TEXT("Camera.Pan"));
	const FName CameraRotate(TEXT("Camera.Rotate"));
	const FName CameraZoom(TEXT("Camera.Zoom"));

	const FName Move(TEXT("Order.Move"));
	const FName Sprint(TEXT("Order.Sprint"));
	const FName WatchSector(TEXT("Order.WatchSector"));
	const FName SelectMember(TEXT("Select.Member"));
	const FName SelectBox(TEXT("Select.Box"));
	const FName Interact(TEXT("Order.Interact"));

	const FName StanceStand(TEXT("Stance.Stand"));
	const FName StanceCrouch(TEXT("Stance.Crouch"));
	const FName StanceProne(TEXT("Stance.Prone"));
	const FName SquadStanceStand(TEXT("Stance.Squad.Stand"));
	const FName SquadStanceCrouch(TEXT("Stance.Squad.Crouch"));
	const FName SquadStanceProne(TEXT("Stance.Squad.Prone"));
	const FName SoloOn(TEXT("Squad.Solo"));
	const FName Regroup(TEXT("Squad.Regroup"));
	const FName Guard(TEXT("Squad.Guard"));
	const FName Autonomy(TEXT("Squad.Autonomy"));
	const FName HoldLine(TEXT("Squad.HoldLine"));
	const FName PosturePassive(TEXT("Posture.Passive"));
	const FName PostureDefensive(TEXT("Posture.Defensive"));
	const FName PostureAggressive(TEXT("Posture.Aggressive"));

	const FName CoverPreview(TEXT("Cover.Preview"));
	const FName CoverEnter(TEXT("Cover.Enter"));
	const FName CoverCorner(TEXT("Cover.Corner"));
	const FName CoverShimmy(TEXT("Cover.Shimmy"));
	const FName CoverFireMode(TEXT("Cover.FireMode"));

	const FName Relocate(TEXT("Object.Relocate"));
	const FName LightBarrel(TEXT("Object.LightBarrel"));
	const FName UseObject(TEXT("Object.Use"));
	const FName UseHeatPack(TEXT("Item.HeatPack"));
	const FName UseItem(TEXT("Item.Use"));
	const FName LootCrate(TEXT("Loot.Open"));
	const FName LootTake(TEXT("Loot.Take"));
	const FName DisarmMine(TEXT("Trap.DisarmMine"));
	const FName DisarmTrap(TEXT("Trap.DisarmObject"));
	const FName DisarmTripwire(TEXT("Trap.DisarmTripwire"));
	const FName SetTrap(TEXT("Trap.SetOnObject"));
	const FName DeployStart(TEXT("Deploy.Start"));
	const FName DeployMine(TEXT("Deploy.Mine"));
	const FName DeployBarricade(TEXT("Deploy.Barricade"));
	const FName DeployTurret(TEXT("Deploy.Turret"));
	const FName DeployTripwire(TEXT("Deploy.Tripwire"));
	const FName DeployOther(TEXT("Deploy.Other"));

	const FName TargetedShot(TEXT("Attack.TargetedShot"));
	const FName PriorityTarget(TEXT("Attack.Priority"));
	const FName GrenadeAim(TEXT("Grenade.Aim"));
	const FName GrenadeThrow(TEXT("Grenade.Throw"));
	const FName FlareAim(TEXT("Flare.Aim"));
	const FName CycleWeapon(TEXT("Weapon.Cycle"));
	const FName Reload(TEXT("Weapon.Reload"));

	const FName TacticalPause(TEXT("Time.Pause"));
	const FName PauseRelease(TEXT("Time.Resume"));
	const FName TurnBasedEnter(TEXT("Time.TurnBased"));
	const FName TurnBasedExit(TEXT("Time.RealTime"));
	const FName EndTurn(TEXT("TB.EndTurn"));
	const FName NextUnit(TEXT("TB.NextUnit"));
	const FName TurnBasedAttack(TEXT("TB.Attack"));
	const FName TurnBasedOrder(TEXT("TB.Order"));

	const FName QuickSave(TEXT("System.QuickSave"));
	const FName QuickLoad(TEXT("System.QuickLoad"));

	const TArray<FPlayerCommandInfo>& GetRegistry()
	{
		static const TArray<FPlayerCommandInfo> Registry = {
			{ CameraPan, TEXT("W/A/S/D"), TEXT("Camera"), TEXT("Pan the camera (also the screen edge or MMB drag)") },
			{ CameraRotate, TEXT("Q/E"), TEXT("Camera"), TEXT("Rotate the camera (also RMB drag)") },
			{ CameraZoom, TEXT("Wheel"), TEXT("Camera"), TEXT("Zoom in / out") },
			{ Move, TEXT("LMB"), TEXT("Orders"), TEXT("Move to the clicked spot") },
			{ Sprint, TEXT("Double-click"), TEXT("Orders"), TEXT("Sprint to the clicked spot") },
			{ WatchSector, TEXT("Shift+Click"), TEXT("Orders"), TEXT("Watch a sector") },
			{ SelectMember, TEXT("1-4"), TEXT("Orders"), TEXT("Select an operative") },
			{ SelectBox, TEXT("Drag"), TEXT("Orders"), TEXT("Box-select a group") },
			{ Interact, TEXT("LMB"), TEXT("Orders"), TEXT("Walk up to an object") },
			{ StanceStand, TEXT("Z"), TEXT("Orders"), TEXT("Stand up") },
			{ StanceCrouch, TEXT("C"), TEXT("Orders"), TEXT("Crouch") },
			{ StanceProne, TEXT("V"), TEXT("Orders"), TEXT("Go prone") },
			{ SquadStanceStand, TEXT("Alt+Z"), TEXT("Orders"), TEXT("Whole squad: stand up") },
			{ SquadStanceCrouch, TEXT("Alt+C"), TEXT("Orders"), TEXT("Whole squad: crouch") },
			{ SquadStanceProne, TEXT("Alt+V"), TEXT("Orders"), TEXT("Whole squad: go prone") },
			{ SoloOn, TEXT("B"), TEXT("Orders"), TEXT("Solo: the others hold their spots") },
			{ Regroup, TEXT("B"), TEXT("Orders"), TEXT("Regroup into the formation") },
			{ Guard, TEXT("T"), TEXT("Orders"), TEXT("Guard this spot / rejoin") },
			{ Autonomy, TEXT("Ctrl+T"), TEXT("Orders"), TEXT("Commander Mode (autonomous squad)") },
			{ HoldLine, TEXT("Shift+RMB"), TEXT("Orders"), TEXT("Hold this line at all costs") },
			{ PosturePassive, TEXT(","), TEXT("Combat"), TEXT("Fire posture: passive (Alt = squad)") },
			{ PostureDefensive, TEXT("."), TEXT("Combat"), TEXT("Fire posture: defensive (Alt = squad)") },
			{ PostureAggressive, TEXT("/"), TEXT("Combat"), TEXT("Fire posture: aggressive (Alt = squad)") },
			{ CoverPreview, TEXT("Click wall"), TEXT("Combat"), TEXT("Preview a cover slot") },
			{ CoverEnter, TEXT("Click again"), TEXT("Combat"), TEXT("Take the cover slot") },
			{ CoverCorner, TEXT("Click corner"), TEXT("Combat"), TEXT("Take a corner slot") },
			{ CoverShimmy, TEXT("Click along wall"), TEXT("Combat"), TEXT("Shimmy along the wall") },
			{ CoverFireMode, TEXT("N"), TEXT("Combat"), TEXT("Corner lean / blind fire") },
			{ Relocate, TEXT("Click > Push"), TEXT("Survival"), TEXT("Push / relocate an object") },
			{ LightBarrel, TEXT("Click > Light"), TEXT("Survival"), TEXT("Light a barrel with a match") },
			{ UseObject, TEXT("Click > Use"), TEXT("Survival"), TEXT("Use an object") },
			{ UseHeatPack, TEXT("U"), TEXT("Survival"), TEXT("Use a heat pack") },
			{ UseItem, TEXT("H/J/K/L"), TEXT("Survival"), TEXT("Medkit / food") },
			{ LootCrate, TEXT("Click crate"), TEXT("Survival"), TEXT("Open a supply crate") },
			{ LootTake, TEXT("Take"), TEXT("Survival"), TEXT("Take supplies") },
			{ DisarmMine, TEXT("Click > Defuse"), TEXT("Survival"), TEXT("Defuse a mine") },
			{ DisarmTrap, TEXT("Click > Defuse"), TEXT("Survival"), TEXT("Defuse a trapped object") },
			{ DisarmTripwire, TEXT("Click > Disarm"), TEXT("Survival"), TEXT("Disarm a tripwire") },
			{ SetTrap, TEXT("Click > Set trap"), TEXT("Combat"), TEXT("Booby-trap an object with a grenade") },
			{ DeployStart, TEXT("F"), TEXT("Combat"), TEXT("Set up from the supply") },
			{ DeployMine, TEXT("F"), TEXT("Combat"), TEXT("Place a mine") },
			{ DeployBarricade, TEXT("F"), TEXT("Combat"), TEXT("Place a barricade") },
			{ DeployTurret, TEXT("F"), TEXT("Combat"), TEXT("Place a turret") },
			{ DeployTripwire, TEXT("Inventory"), TEXT("Combat"), TEXT("Rig a tripwire") },
			{ DeployOther, TEXT("Inventory"), TEXT("Survival"), TEXT("Set up a campfire / stove") },
			{ TargetedShot, TEXT("Ctrl+Click"), TEXT("Combat"), TEXT("Targeted shot") },
			{ PriorityTarget, TEXT("Click enemy"), TEXT("Combat"), TEXT("Priority target") },
			{ GrenadeAim, TEXT("G"), TEXT("Combat"), TEXT("Grenade") },
			{ GrenadeThrow, TEXT("LMB"), TEXT("Combat"), TEXT("Throw the grenade") },
			{ FlareAim, TEXT("Shift+G"), TEXT("Combat"), TEXT("Signal flare") },
			{ CycleWeapon, TEXT("X"), TEXT("Combat"), TEXT("Next weapon") },
			{ Reload, TEXT("R"), TEXT("Combat"), TEXT("Reload") },
			{ TacticalPause, TEXT("Space"), TEXT("Time"), TEXT("Tactical pause") },
			{ PauseRelease, TEXT("Space"), TEXT("Time"), TEXT("Release the pause: orders run") },
			{ TurnBasedEnter, TEXT("Hold Space"), TEXT("Time"), TEXT("Turn-based combat") },
			{ TurnBasedExit, TEXT("Hold Space"), TEXT("Time"), TEXT("Back to real time") },
			{ EndTurn, TEXT("Enter"), TEXT("Time"), TEXT("End the squad turn") },
			{ NextUnit, TEXT("Tab"), TEXT("Time"), TEXT("Next operative") },
			{ TurnBasedAttack, TEXT("Ctrl+Click"), TEXT("Time"), TEXT("Turn-based attack") },
			{ TurnBasedOrder, TEXT("LMB"), TEXT("Time"), TEXT("Turn-based select / walk") },
			{ QuickSave, TEXT("F5"), TEXT("System"), TEXT("Quick save") },
			{ QuickLoad, TEXT("F9"), TEXT("System"), TEXT("Quick load") },
		};
		return Registry;
	}

	const FPlayerCommandInfo* Find(FName Id)
	{
		return GetRegistry().FindByPredicate([Id](const FPlayerCommandInfo& Info) { return Info.Id == Id; });
	}

	bool Matches(const FString& Pattern, FName CommandId)
	{
		const FString Trimmed = Pattern.TrimStartAndEnd();
		if (Trimmed.IsEmpty() || CommandId.IsNone())
		{
			return false;
		}
		if (Trimmed == TEXT("*"))
		{
			return true;
		}
		const FString Id = CommandId.ToString();
		if (Trimmed.EndsWith(TEXT("*")))
		{
			return Id.StartsWith(Trimmed.LeftChop(1), ESearchCase::IgnoreCase);
		}
		return Id.Equals(Trimmed, ESearchCase::IgnoreCase);
	}

	FString FormatKeyToken(const FString& Token)
	{
		const FString Trimmed = Token.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			return FString();
		}
		if (const FPlayerCommandInfo* Info = Find(FName(*Trimmed)))
		{
			return FString::Printf(TEXT("[%s]"), Info->KeyLabel);
		}
		return FString::Printf(TEXT("[%s]"), *Trimmed);
	}

	FString ResolveKeyTokens(const FString& Text)
	{
		static const FString Open(TEXT("{key:"));
		FString Result;
		int32 Cursor = 0;
		while (Cursor < Text.Len())
		{
			const int32 Start = Text.Find(Open, ESearchCase::IgnoreCase, ESearchDir::FromStart, Cursor);
			if (Start == INDEX_NONE)
			{
				break;
			}
			const int32 End = Text.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start);
			if (End == INDEX_NONE)
			{
				break;
			}
			Result += Text.Mid(Cursor, Start - Cursor);
			Result += FormatKeyToken(Text.Mid(Start + Open.Len(), End - Start - Open.Len()));
			Cursor = End + 1;
		}
		Result += Text.Mid(Cursor);
		return Result;
	}

	void Report(const UObject* WorldContext, FName CommandId, const FPlayerCommandContext& Context)
	{
		if (CommandId.IsNone())
		{
			return;
		}
		UE_LOG(LogCodexTactics, Verbose, TEXT("[Command] %s%s%s"), *CommandId.ToString(), Context.bSquadWide ? TEXT(" (squad)") : TEXT(""),
			Context.bPlanned ? TEXT(" (planned)") : TEXT(""));
		if (UCodexEventBus* Bus = WorldContext ? UCodexEventBus::Get(WorldContext) : nullptr)
		{
			Bus->OnPlayerCommand.Broadcast(CommandId, Context);
		}
	}

	void ReportBy(const UObject* WorldContext, FName CommandId, AActor* Operative, AActor* Target, bool bPlanned)
	{
		FPlayerCommandContext Context;
		Context.Operative = Operative;
		Context.Target = Target;
		Context.bPlanned = bPlanned;
		if (Target)
		{
			Context.Location = Target->GetActorLocation();
		}
		Report(WorldContext, CommandId, Context);
	}
}

namespace GameplayEvents
{
	const FName RelocationDone(TEXT("RelocationDone"));
	const FName RelocationRefused(TEXT("RelocationRefused"));
	const FName BarrelLit(TEXT("BarrelLit"));
	const FName MineSpotted(TEXT("MineSpotted"));
	const FName ItemUsed(TEXT("ItemUsed"));
	const FName ColdTierChanged(TEXT("ColdTier"));
	const FName SoldierDowned(TEXT("SoldierDowned"));
	const FName LessonDowned(TEXT("LessonDowned"));
	const FName DialogueFinished(TEXT("DialogueFinished"));
	const FName CombatStarted(TEXT("CombatStarted"));
	const FName CombatEnded(TEXT("CombatEnded"));

	void Raise(const UObject* WorldContext, FName EventName, AActor* Subject)
	{
		if (EventName.IsNone())
		{
			return;
		}
		UE_LOG(LogCodexTactics, Verbose, TEXT("[Event] %s"), *EventName.ToString());
		if (UCodexEventBus* Bus = WorldContext ? UCodexEventBus::Get(WorldContext) : nullptr)
		{
			Bus->OnGameplayEvent.Broadcast(EventName, Subject);
		}
	}
}
