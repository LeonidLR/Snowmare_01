#include "Quests/QuestChain.h"

#define LOCTEXT_NAMESPACE "QuestChain"

namespace
{
	FQuestInteractionResult Line(const FText& Speaker, const FText& Text, EQuestEvent Event = EQuestEvent::None)
	{
		FQuestInteractionResult Result;
		Result.Speaker = Speaker;
		Result.Text = Text;
		Result.Event = Event;
		return Result;
	}
}

FQuestInteractionResult FQuestChainState::Interact(EInteractableType Type)
{
	switch (Type)
	{
	case EInteractableType::GateTerminal:
		if (!bIsGeneratorRunning)
		{
			return Line(LOCTEXT("Terminal", "Gate Terminal"),
				LOCTEXT("TerminalNoPower", "Main grid is offline. Blast gate power is locked out. The backup generator must be started."));
		}
		if (!bIsGatePowered)
		{
			bIsGatePowered = true;
			return Line(LOCTEXT("Commander", "Commander"),
				LOCTEXT("TerminalOpen", "Applying voltage to the servo drives... The locks click, the blue gates swing open!"), EQuestEvent::GateOpened);
		}
		return Line(LOCTEXT("Terminal", "Gate Terminal"), LOCTEXT("TerminalDone", "Gate power is on. The leaves are unlocked."));

	case EInteractableType::Canister:
		if (!bHasEmptyCanister && !bHasFuelCanister)
		{
			bHasEmptyCanister = true;
			return Line(LOCTEXT("Engineer", "Engineer"),
				LOCTEXT("CanisterFound", "Found an empty 20L Fuel Canister. Now we have something to drain the fuel into."), EQuestEvent::CanisterPickedUp);
		}
		return Line(LOCTEXT("Squad", "Squad"), LOCTEXT("CanisterHave", "We already have the canister."));

	case EInteractableType::Vehicle:
		if (bHasEmptyCanister)
		{
			bHasEmptyCanister = false;
			bHasFuelCanister = true;
			return Line(LOCTEXT("Engineer", "Engineer"),
				LOCTEXT("VehicleDrain", "Draining the remaining diesel from the APC fuel system... Excellent, the canister is full to the brim!"));
		}
		if (bHasFuelCanister)
		{
			return Line(LOCTEXT("Engineer", "Engineer"),
				LOCTEXT("VehicleFull", "The canister is already full of diesel. Carry it to the generator."));
		}
		return Line(LOCTEXT("Medic", "Medic"),
			LOCTEXT("VehicleNoCanister", "The abandoned vehicle's tank still holds diesel, but there is nothing to drain it into. We need some kind of container."));

	case EInteractableType::Generator:
		if (bIsGeneratorRunning)
		{
			return Line(LOCTEXT("Generator", "Generator"),
				LOCTEXT("GeneratorRunning", "The Backup Diesel Generator hums steadily, producing power and heat."));
		}
		if (!bHasFuelCanister)
		{
			return Line(LOCTEXT("Engineer", "Engineer"),
				LOCTEXT("GeneratorDry", "The backup generator is intact, but the tank is dry. It needs diesel fuel."));
		}
		bHasFuelCanister = false;
		bIsGeneratorRunning = true;
		return Line(LOCTEXT("Commander", "Commander"),
			LOCTEXT("GeneratorStart", "Pouring in the diesel and pulling the starter... The generator roars to life! Power flows into the checkpoint grid."),
			EQuestEvent::GeneratorStarted);

	case EInteractableType::Gate:
	default:
		if (!bIsGatePowered)
		{
			return Line(LOCTEXT("Gate", "Blast Gate"),
				LOCTEXT("GateLocked", "The heavy armored gate is held by a magnetic lock. The terminal has no power."));
		}
		return Line(LOCTEXT("Commander", "Commander"),
			LOCTEXT("GateOpen", "The gate is open. Squad, prepare to enter the inner courtyard!"));
	}
}

FText FQuestChainState::GetObjective() const
{
	if (!bHasEmptyCanister && !bHasFuelCanister && !bIsGeneratorRunning)
	{
		return LOCTEXT("ObjCanister", "Find an empty fuel canister");
	}
	if (bHasEmptyCanister)
	{
		return LOCTEXT("ObjDrain", "Drain diesel from the abandoned APC into the canister");
	}
	if (bHasFuelCanister)
	{
		return LOCTEXT("ObjGenerator", "Refuel and start the Backup Diesel Generator (creates a heat zone)");
	}
	if (!bIsGatePowered)
	{
		return LOCTEXT("ObjTerminal", "Power up the blast gate control terminal");
	}
	return LOCTEXT("ObjOpening", "The gate is opening...");
}

#undef LOCTEXT_NAMESPACE
