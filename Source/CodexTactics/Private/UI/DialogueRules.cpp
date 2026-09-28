#include "UI/DialogueRules.h"

#define LOCTEXT_NAMESPACE "DialogueRules"

FDialogueSpeakerStyle DialogueRules::GetSpeakerStyle(const FString& Speaker)
{
	// FString::ToLower is ASCII-only; FText::ToLower lowers Cyrillic too (Godot String.to_lower).
	const FString Low = FText::FromString(Speaker).ToLower().ToString();
	FDialogueSpeakerStyle Style;
	if (Low.Contains(TEXT("сусанин")) || Low.Contains(TEXT("иван")))
	{
		Style = { LOCTEXT("PortraitLocal", "ЖИТ"), LOCTEXT("RoleLocal", "Местный житель"), FLinearColor(0.4f, 0.95f, 0.6f),
			FLinearColor(0.08f, 0.16f, 0.12f, 0.9f), FLinearColor(0.3f, 0.85f, 0.5f, 0.8f) };
	}
	else if (Low.Contains(TEXT("командир")) || Low.Contains(TEXT("северов")) || Low.Contains(TEXT("player")))
	{
		Style = { LOCTEXT("PortraitCommander", "КОМ"), LOCTEXT("RoleCommander", "Командир отряда"), FLinearColor(0.3f, 0.85f, 1.f),
			FLinearColor(0.08f, 0.13f, 0.22f, 0.9f), FLinearColor(0.2f, 0.7f, 1.f, 0.8f) };
	}
	else if (Low.Contains(TEXT("инженер")) || Low.Contains(TEXT("ветров")))
	{
		Style = { LOCTEXT("PortraitEngineer", "ИНЖ"), LOCTEXT("RoleEngineer", "Инженер-техник"), FLinearColor(1.f, 0.75f, 0.2f),
			FLinearColor(0.18f, 0.14f, 0.08f, 0.9f), FLinearColor(1.f, 0.75f, 0.2f, 0.8f) };
	}
	else if (Low.Contains(TEXT("медик")) || Low.Contains(TEXT("сапёр")) || Low.Contains(TEXT("сапер")) || Low.Contains(TEXT("соколова"))
		|| Low.Contains(TEXT("быков")))
	{
		Style = { LOCTEXT("PortraitMedic", "МЕД"), LOCTEXT("RoleMedic", "Медик-сапёр"), FLinearColor(0.35f, 0.9f, 0.6f),
			FLinearColor(0.08f, 0.16f, 0.14f, 0.9f), FLinearColor(0.3f, 0.85f, 0.65f, 0.8f) };
	}
	else
	{
		Style = { LOCTEXT("PortraitOther", "?"), LOCTEXT("RoleOther", "Собеседник"), FLinearColor(0.8f, 0.85f, 0.9f),
			FLinearColor(0.1f, 0.12f, 0.16f, 0.9f), FLinearColor(0.5f, 0.55f, 0.65f, 0.8f) };
	}
	return Style;
}

FText DialogueRules::GetNextButtonText(bool bLastLine, const FString& CustomFinishText, bool bRecruitment, bool bCombatPhase)
{
	if (!bLastLine)
	{
		return LOCTEXT("Next", "Далее ▶");
	}
	if (!CustomFinishText.IsEmpty())
	{
		return FText::FromString(CustomFinishText);
	}
	if (bRecruitment)
	{
		return LOCTEXT("Recruit", "🤝 Вступить в отряд");
	}
	return bCombatPhase ? LOCTEXT("ToBattle", "В бой! ▶") : LOCTEXT("Understood", "Понял! ▶");
}

FText DialogueRules::GetProgressText(int32 LineIndex, int32 LineCount)
{
	return FText::FromString(FString::Printf(TEXT("[%d / %d]"), LineIndex + 1, LineCount));
}

#undef LOCTEXT_NAMESPACE
