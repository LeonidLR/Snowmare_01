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
			return Line(LOCTEXT("Terminal", "Пульт ворот"),
				LOCTEXT("TerminalNoPower", "Основная электросеть обесточена. Питание гермоворот заблокировано. Требуется запустить резервный генератор."));
		}
		if (!bIsGatePowered)
		{
			bIsGatePowered = true;
			return Line(LOCTEXT("Commander", "Командир"),
				LOCTEXT("TerminalOpen", "Подаем напряжение на сервоприводы... Замки щелкают, синие ворота открываются!"), EQuestEvent::GateOpened);
		}
		return Line(LOCTEXT("Terminal", "Пульт ворот"), LOCTEXT("TerminalDone", "Питание на ворота подано. Створки разблокированы."));

	case EInteractableType::Canister:
		if (!bHasEmptyCanister && !bHasFuelCanister)
		{
			bHasEmptyCanister = true;
			return Line(LOCTEXT("Engineer", "Инженер"),
				LOCTEXT("CanisterFound", "Найдена пустая 20-литровая канистра. Теперь есть во что слить топливо."), EQuestEvent::CanisterPickedUp);
		}
		return Line(LOCTEXT("Squad", "Отряд"), LOCTEXT("CanisterHave", "Канистра уже у нас."));

	case EInteractableType::Vehicle:
		if (bHasEmptyCanister)
		{
			bHasEmptyCanister = false;
			bHasFuelCanister = true;
			return Line(LOCTEXT("Engineer", "Инженер"),
				LOCTEXT("VehicleDrain", "Сливаем остатки дизеля из топливной системы БМП... Отлично, канистра полная под завязку!"));
		}
		if (bHasFuelCanister)
		{
			return Line(LOCTEXT("Engineer", "Инженер"),
				LOCTEXT("VehicleFull", "Канистра уже заполнена дизелем. Нужно нести её к генератору."));
		}
		return Line(LOCTEXT("Medic", "Медик"),
			LOCTEXT("VehicleNoCanister", "В баке брошенной техники остался дизель, но слить его не во что. Нужна какая-нибудь емкость."));

	case EInteractableType::Generator:
		if (bIsGeneratorRunning)
		{
			return Line(LOCTEXT("Generator", "Генератор"),
				LOCTEXT("GeneratorRunning", "Дизель-генератор стабильно гудит, вырабатывая энергию и тепло."));
		}
		if (!bHasFuelCanister)
		{
			return Line(LOCTEXT("Engineer", "Инженер"),
				LOCTEXT("GeneratorDry", "Резервный генератор исправен, но бак сухой. Нужно залить дизельное топливо."));
		}
		bHasFuelCanister = false;
		bIsGeneratorRunning = true;
		return Line(LOCTEXT("Commander", "Командир"),
			LOCTEXT("GeneratorStart", "Заливаем дизель и дергаем стартер... Генератор с ревом оживает! Напряжение пошло в сеть КПП."),
			EQuestEvent::GeneratorStarted);

	case EInteractableType::Gate:
	default:
		if (!bIsGatePowered)
		{
			return Line(LOCTEXT("Gate", "Гермоворота"),
				LOCTEXT("GateLocked", "Тяжелые бронированные ворота заблокированы магнитным замком. С пульта нет питания."));
		}
		return Line(LOCTEXT("Commander", "Командир"),
			LOCTEXT("GateOpen", "Ворота открыты. Отряду приготовиться к входу во внутренний двор!"));
	}
}

FText FQuestChainState::GetObjective() const
{
	if (!bHasEmptyCanister && !bHasFuelCanister && !bIsGeneratorRunning)
	{
		return LOCTEXT("ObjCanister", "Найти пустую канистру для топлива");
	}
	if (bHasEmptyCanister)
	{
		return LOCTEXT("ObjDrain", "Слить дизель из брошенного БМП в канистру");
	}
	if (bHasFuelCanister)
	{
		return LOCTEXT("ObjGenerator", "Заправить и запустить резервный генератор (создаст тепловую зону)");
	}
	if (!bIsGatePowered)
	{
		return LOCTEXT("ObjTerminal", "Подать питание на пульте управления гермоворотами");
	}
	return LOCTEXT("ObjOpening", "Ворота открываются...");
}

#undef LOCTEXT_NAMESPACE
