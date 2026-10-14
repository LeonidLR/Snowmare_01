#include "Core/PlayerCommands.h"
#include "CodexTactics.h"
#include "GameFramework/Actor.h"
#include "Subsystems/CodexEventBus.h"

namespace PlayerCommands
{
	const FName CameraPanKeys(TEXT("Camera.Pan.Keys"));
	const FName CameraPanEdge(TEXT("Camera.Pan.Edge"));
	const FName CameraPanDrag(TEXT("Camera.Pan.Drag"));
	const FName CameraRotateKeys(TEXT("Camera.Rotate.Keys"));
	const FName CameraRotateMouse(TEXT("Camera.Rotate.Mouse"));
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
	const FName CoverCornerPeek(TEXT("Cover.CornerPeek"));
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

	const FName VehicleBoard(TEXT("Vehicle.Board"));
	const FName VehicleDisembark(TEXT("Vehicle.Disembark"));
	const FName VehicleDrive(TEXT("Vehicle.Drive"));
	const FName VehicleTurretFire(TEXT("Vehicle.TurretFire"));
	const FName VehicleRepair(TEXT("Vehicle.Repair"));

	const FName QuickSave(TEXT("System.QuickSave"));
	const FName QuickLoad(TEXT("System.QuickLoad"));
	const FName ControlsScreen(TEXT("System.Controls"));
	const FName PauseMenu(TEXT("System.PauseMenu"));
	const FName InventoryOpen(TEXT("Inventory.Open"));
	const FName GiveItem(TEXT("Item.Give"));
	const FName ToggleVisionCones(TEXT("View.VisionCones"));
	const FName DialogueNext(TEXT("Dialogue.Next"));
	const FName DialogueSkip(TEXT("Dialogue.Skip"));

	const TArray<const TCHAR*>& GetSections()
	{
		static const TArray<const TCHAR*> Sections = {
			TEXT("Movement & camera"), TEXT("Squad"), TEXT("Stances"), TEXT("Combat"), TEXT("Tactical pause & turn-based"),
			TEXT("Items & traps"), TEXT("Vehicles"), TEXT("Saving & menus"),
		};
		return Sections;
	}

	namespace
	{
		TMap<FName, FString>& KeyLabelOverrides()
		{
			static TMap<FName, FString> Overrides;
			return Overrides;
		}
	}

	void SetKeyLabelOverride(FName Id, const FString& KeyLabel)
	{
		if (KeyLabel.IsEmpty())
		{
			KeyLabelOverrides().Remove(Id);
		}
		else
		{
			KeyLabelOverrides().Add(Id, KeyLabel);
		}
	}

	FString GetKeyLabel(FName Id)
	{
		if (const FString* Override = KeyLabelOverrides().Find(Id))
		{
			return *Override;
		}
		const FPlayerCommandInfo* Info = Find(Id);
		return Info ? FString(Info->KeyLabel) : FString();
	}

	const TArray<FPlayerCommandInfo>& GetRegistry()
	{
		static const TArray<FPlayerCommandInfo> Registry = {
			{ CameraPanKeys, TEXT("W/A/S/D"), TEXT("Movement & camera"), TEXT("Pan the camera") },
			{ CameraPanEdge, TEXT("Screen edge"), TEXT("Movement & camera"), TEXT("Pan the camera by pushing the cursor to the screen edge") },
			{ CameraPanDrag, TEXT("MMB drag"), TEXT("Movement & camera"), TEXT("Pan the camera by dragging the ground") },
			{ CameraRotateKeys, TEXT("Q/E"), TEXT("Movement & camera"), TEXT("Turn the camera in 45 degree steps") },
			{ CameraRotateMouse, TEXT("RMB drag"), TEXT("Movement & camera"), TEXT("Turn the camera freely with the mouse") },
			{ CameraZoom, TEXT("Wheel"), TEXT("Movement & camera"), TEXT("Zoom in / out") },
			{ Move, TEXT("LMB"), TEXT("Movement & camera"), TEXT("Move to the clicked spot") },
			{ Sprint, TEXT("Double-click"), TEXT("Movement & camera"), TEXT("Sprint to the clicked spot") },
			{ WatchSector, TEXT("Shift+Click"), TEXT("Movement & camera"), TEXT("Watch a sector") },
			{ SelectMember, TEXT("1-4"), TEXT("Squad"), TEXT("Select an operative") },
			{ SelectBox, TEXT("Drag"), TEXT("Squad"), TEXT("Box-select a group") },
			{ Interact, TEXT("LMB"), TEXT("Movement & camera"), TEXT("Walk up to an object") },
			{ StanceStand, TEXT("Z"), TEXT("Stances"), TEXT("Stand up") },
			{ StanceCrouch, TEXT("C"), TEXT("Stances"), TEXT("Crouch") },
			{ StanceProne, TEXT("V"), TEXT("Stances"), TEXT("Go prone") },
			{ SquadStanceStand, TEXT("Alt+Z"), TEXT("Stances"), TEXT("Whole squad: stand up") },
			{ SquadStanceCrouch, TEXT("Alt+C"), TEXT("Stances"), TEXT("Whole squad: crouch") },
			{ SquadStanceProne, TEXT("Alt+V"), TEXT("Stances"), TEXT("Whole squad: go prone") },
			{ SoloOn, TEXT("B"), TEXT("Squad"), TEXT("Solo: the others hold their spots") },
			{ Regroup, TEXT("B"), TEXT("Squad"), TEXT("Regroup into the formation") },
			{ Guard, TEXT("T"), TEXT("Squad"), TEXT("Guard this spot / rejoin") },
			{ Autonomy, TEXT("Ctrl+T"), TEXT("Squad"), TEXT("Commander Mode (autonomous squad)") },
			{ HoldLine, TEXT("Shift+RMB"), TEXT("Squad"), TEXT("Hold this line at all costs") },
			{ PosturePassive, TEXT(","), TEXT("Combat"), TEXT("Fire posture: passive (Alt = squad)") },
			{ PostureDefensive, TEXT("."), TEXT("Combat"), TEXT("Fire posture: defensive (Alt = squad)") },
			{ PostureAggressive, TEXT("/"), TEXT("Combat"), TEXT("Fire posture: aggressive (Alt = squad)") },
			{ CoverPreview, TEXT("Click wall"), TEXT("Combat"), TEXT("Preview a cover slot") },
			{ CoverEnter, TEXT("Click again"), TEXT("Combat"), TEXT("Take the cover slot") },
			{ CoverCorner, TEXT("Click corner"), TEXT("Combat"), TEXT("Take a corner slot") },
			{ CoverShimmy, TEXT("Click along wall"), TEXT("Combat"), TEXT("Shimmy along the wall") },
			{ CoverCornerPeek, TEXT("Click wall edge"), TEXT("Combat"), TEXT("In cover (high, or crouched at low cover): go to that corner and hold the aim round it") },
			{ CoverFireMode, TEXT("N"), TEXT("Combat"), TEXT("Corner lean / blind fire") },
			{ Relocate, TEXT("Click > Push"), TEXT("Items & traps"), TEXT("Push / relocate an object") },
			{ LightBarrel, TEXT("Click > Light"), TEXT("Items & traps"), TEXT("Light a barrel with a match") },
			{ UseObject, TEXT("Click > Use"), TEXT("Items & traps"), TEXT("Use an object") },
			{ UseHeatPack, TEXT("U"), TEXT("Items & traps"), TEXT("Use a heat pack") },
			{ InventoryOpen, TEXT("I"), TEXT("Items & traps"), TEXT("Open / close the inventory drawer (drag items onto a mate)") },
			{ GiveItem, TEXT("Shift+Drag"), TEXT("Items & traps"), TEXT("Hand an item to a mate (drag onto him; Shift: a heat pack is kept, not used)") },
			{ UseItem, TEXT("H/J/K/L"), TEXT("Items & traps"), TEXT("Medkit / food") },
			{ LootCrate, TEXT("Click crate"), TEXT("Items & traps"), TEXT("Open a supply crate") },
			{ LootTake, TEXT("Take"), TEXT("Items & traps"), TEXT("Take supplies") },
			{ DisarmMine, TEXT("Click > Defuse"), TEXT("Items & traps"), TEXT("Defuse a mine") },
			{ DisarmTrap, TEXT("Click > Defuse"), TEXT("Items & traps"), TEXT("Defuse a trapped object") },
			{ DisarmTripwire, TEXT("Click > Disarm"), TEXT("Items & traps"), TEXT("Disarm a tripwire") },
			{ SetTrap, TEXT("Click > Set trap"), TEXT("Items & traps"), TEXT("Booby-trap an object with a grenade") },
			{ DeployStart, TEXT("F"), TEXT("Items & traps"), TEXT("Set up from the supply") },
			{ DeployMine, TEXT("F"), TEXT("Items & traps"), TEXT("Place a mine") },
			{ DeployBarricade, TEXT("F"), TEXT("Items & traps"), TEXT("Place a barricade") },
			{ DeployTurret, TEXT("F"), TEXT("Items & traps"), TEXT("Place a turret") },
			{ DeployTripwire, TEXT("Inventory"), TEXT("Items & traps"), TEXT("Rig a tripwire") },
			{ DeployOther, TEXT("Inventory"), TEXT("Items & traps"), TEXT("Set up a campfire / stove") },
			{ TargetedShot, TEXT("Ctrl+Click"), TEXT("Combat"), TEXT("Targeted shot") },
			{ PriorityTarget, TEXT("Click enemy"), TEXT("Combat"), TEXT("Priority target") },
			{ ToggleVisionCones, TEXT("O"), TEXT("Combat"), TEXT("Show / hide the enemies' vision cones on the ground") },
			{ GrenadeAim, TEXT("G"), TEXT("Combat"), TEXT("Grenade") },
			{ GrenadeThrow, TEXT("LMB"), TEXT("Combat"), TEXT("Throw the grenade") },
			{ FlareAim, TEXT("Shift+G"), TEXT("Combat"), TEXT("Signal flare") },
			{ CycleWeapon, TEXT("X"), TEXT("Combat"), TEXT("Next weapon") },
			{ Reload, TEXT("R"), TEXT("Combat"), TEXT("Reload") },
			{ TacticalPause, TEXT("Space"), TEXT("Tactical pause & turn-based"), TEXT("Tactical pause") },
			{ PauseRelease, TEXT("Space"), TEXT("Tactical pause & turn-based"), TEXT("Release the pause: orders run") },
			{ TurnBasedEnter, TEXT("Hold Space"), TEXT("Tactical pause & turn-based"), TEXT("Turn-based combat") },
			{ TurnBasedExit, TEXT("Hold Space"), TEXT("Tactical pause & turn-based"), TEXT("Back to real time") },
			{ EndTurn, TEXT("Enter"), TEXT("Tactical pause & turn-based"), TEXT("End the squad turn") },
			{ NextUnit, TEXT("Tab"), TEXT("Tactical pause & turn-based"), TEXT("Next operative") },
			{ TurnBasedAttack, TEXT("Ctrl+Click"), TEXT("Tactical pause & turn-based"), TEXT("Turn-based attack") },
			{ TurnBasedOrder, TEXT("LMB"), TEXT("Tactical pause & turn-based"), TEXT("Turn-based select / walk") },
			{ VehicleBoard, TEXT("Y"), TEXT("Vehicles"), TEXT("Board the nearest vehicle (Alt+Y: whole squad)") },
			{ VehicleDisembark, TEXT("Y"), TEXT("Vehicles"), TEXT("Get out of the vehicle (Alt+Y: whole squad)") },
			{ VehicleDrive, TEXT("LMB"), TEXT("Vehicles"), TEXT("Drive there while aboard (double-click: full speed; small enemies are run over)") },
			{ VehicleTurretFire, TEXT("Ctrl+Click"), TEXT("Vehicles"), TEXT("Turret target while aboard (the gunner fires by his posture , . /); a barrel / trapped object gets a cannon shell") },
			{ VehicleRepair, TEXT("Y"), TEXT("Vehicles"), TEXT("Repair a disabled vehicle (the Engineer, ~5 s at the hull)") },
			{ QuickSave, TEXT("F5"), TEXT("Saving & menus"), TEXT("Quick save") },
			{ QuickLoad, TEXT("F9"), TEXT("Saving & menus"), TEXT("Quick load") },
			{ ControlsScreen, TEXT("F1"), TEXT("Saving & menus"), TEXT("This controls reference (F1 or Esc closes it)") },
			{ PauseMenu, TEXT("Esc"), TEXT("Saving & menus"), TEXT("Pause menu: save, load, options, tutorial") },
			{ DialogueNext, TEXT("Enter"), TEXT("Saving & menus"), TEXT("Dialogue / radio: next line") },
			{ DialogueSkip, TEXT("Hold Enter"), TEXT("Saving & menus"), TEXT("Dialogue / radio: skip the whole conversation (or click SKIP on the radio strip)") },
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
		if (Find(FName(*Trimmed)))
		{
			return FString::Printf(TEXT("[%s]"), *GetKeyLabel(FName(*Trimmed)));
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

	void ReportHold(const UObject* WorldContext, FName CommandId, float DeltaSeconds)
	{
		if (CommandId.IsNone() || DeltaSeconds <= 0.f)
		{
			return;
		}
		if (UCodexEventBus* Bus = WorldContext ? UCodexEventBus::Get(WorldContext) : nullptr)
		{
			FPlayerCommandContext Context;
			Context.HeldSeconds = DeltaSeconds;
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
	const FName TripwireSpotted(TEXT("TripwireSpotted"));
	const FName TripwireDisarmed(TEXT("TripwireDisarmed"));
	const FName TripwireTriggered(TEXT("TripwireTriggered"));
	const FName CoverEntered(TEXT("CoverEntered"));
	const FName CoverLeft(TEXT("CoverLeft"));
	const FName CoverShimmy(TEXT("CoverShimmy"));
	const FName CornerReached(TEXT("CornerReached"));
	const FName CornerPeek(TEXT("CornerPeek"));
	const FName VehicleBoarded(TEXT("VehicleBoarded"));
	const FName VehicleDisembarked(TEXT("VehicleDisembarked"));
	const FName VehicleDriveStarted(TEXT("VehicleDriveStarted"));
	const FName VehicleArrived(TEXT("VehicleArrived"));
	const FName VehiclePathBlocked(TEXT("VehiclePathBlocked"));
	const FName TutorialLocked(TEXT("Tutorial.Locked"));
	const FName ItemLooted(TEXT("ItemLooted"));
	const FName ItemGiven(TEXT("ItemGiven"));
	const FName VehicleTurretFired(TEXT("VehicleTurretFired"));
	const FName VehicleTurretKill(TEXT("VehicleTurretKill"));
	const FName VehicleRunOver(TEXT("VehicleRunOver"));
	const FName VehicleDamaged(TEXT("VehicleDamaged"));
	const FName VehicleDisabled(TEXT("VehicleDisabled"));
	const FName VehicleRepaired(TEXT("VehicleRepaired"));
	const FName BridgeEntered(TEXT("BridgeEntered"));
	const FName BridgeRefused(TEXT("BridgeRefused"));
	const FName BridgeCrossed(TEXT("BridgeCrossed"));
	const FName CrawlGapRefused(TEXT("CrawlGapRefused"));
	const FName CrawlGapPassed(TEXT("CrawlGapPassed"));
	const FName VisionConesToggled(TEXT("VisionConesToggled"));

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

	void RaiseWithContext(const UObject* WorldContext, FName EventName, const FGameplayEventContext& Context)
	{
		if (EventName.IsNone())
		{
			return;
		}
		FGameplayEventContext Filled = Context;
		if (Filled.SubjectId.IsNone() && Filled.Subject)
		{
			Filled.SubjectId = Filled.Subject->GetFName();
		}
		if (Filled.TargetId.IsNone() && Filled.Target)
		{
			Filled.TargetId = Filled.Target->GetFName();
		}
		UE_LOG(LogCodexTactics, Verbose, TEXT("[Event] %s subject %s target %s kind %s"), *EventName.ToString(), *Filled.SubjectId.ToString(),
			*Filled.TargetId.ToString(), *Filled.Kind.ToString());
		if (UCodexEventBus* Bus = WorldContext ? UCodexEventBus::Get(WorldContext) : nullptr)
		{
			Bus->OnGameplayEvent.Broadcast(EventName, Filled.Subject);
			Bus->OnGameplayEventDetailed.Broadcast(EventName, Filled);
		}
	}

	void RaiseItemLooted(const UObject* WorldContext, AActor* Collector, AActor* Container, FName ItemKind, int32 Count)
	{
		if (Count <= 0 || ItemKind.IsNone())
		{
			return;
		}
		FGameplayEventContext Context;
		Context.Subject = Collector;
		Context.Target = Container;
		Context.Kind = ItemKind;
		if (Container)
		{
			Context.Location = Container->GetActorLocation();
		}
		RaiseWithContext(WorldContext, ItemLooted, Context);
		RaiseWithContext(WorldContext, FName(*(ItemLooted.ToString() + TEXT(".") + ItemKind.ToString())), Context);
	}

	void RaiseItemGiven(const UObject* WorldContext, AActor* Giver, AActor* Recipient, FName ItemKind, int32 Count)
	{
		if (Count <= 0 || ItemKind.IsNone())
		{
			return;
		}
		FGameplayEventContext Context;
		Context.Subject = Giver;
		Context.Target = Recipient;
		Context.Kind = ItemKind;
		if (Recipient)
		{
			Context.Location = Recipient->GetActorLocation();
		}
		RaiseWithContext(WorldContext, ItemGiven, Context);
		RaiseWithContext(WorldContext, FName(*(ItemGiven.ToString() + TEXT(".") + ItemKind.ToString())), Context);
	}
}

