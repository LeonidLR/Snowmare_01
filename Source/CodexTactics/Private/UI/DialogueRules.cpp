#include "UI/DialogueRules.h"

#define LOCTEXT_NAMESPACE "DialogueRules"

FDialogueSpeakerStyle DialogueRules::GetSpeakerStyle(const FString& Speaker)
{
	// Speaker names are English (narrative_manifest.json speaker_en); matching is case-insensitive on substrings.
	const FString Low = Speaker.ToLower();
	FDialogueSpeakerStyle Style;
	if (Low.Contains(TEXT("susanin")) || Low.Contains(TEXT("ivan")) || Low.Contains(TEXT("local")))
	{
		Style = { LOCTEXT("PortraitLocal", "LOC"), LOCTEXT("RoleLocal", "Local Resident"), FLinearColor(0.4f, 0.95f, 0.6f),
			FLinearColor(0.08f, 0.16f, 0.12f, 0.9f), FLinearColor(0.3f, 0.85f, 0.5f, 0.8f) };
	}
	else if (Low.Contains(TEXT("commander")) || Low.Contains(TEXT("severov")) || Low.Contains(TEXT("player")))
	{
		Style = { LOCTEXT("PortraitCommander", "CMD"), LOCTEXT("RoleCommander", "Squad Commander"), FLinearColor(0.3f, 0.85f, 1.f),
			FLinearColor(0.08f, 0.13f, 0.22f, 0.9f), FLinearColor(0.2f, 0.7f, 1.f, 0.8f) };
	}
	else if (Low.Contains(TEXT("engineer")) || Low.Contains(TEXT("vetrov")))
	{
		Style = { LOCTEXT("PortraitEngineer", "ENG"), LOCTEXT("RoleEngineer", "Field Engineer"), FLinearColor(1.f, 0.75f, 0.2f),
			FLinearColor(0.18f, 0.14f, 0.08f, 0.9f), FLinearColor(1.f, 0.75f, 0.2f, 0.8f) };
	}
	else if (Low.Contains(TEXT("medic")) || Low.Contains(TEXT("sapper")) || Low.Contains(TEXT("sokolova")) || Low.Contains(TEXT("bykov")))
	{
		Style = { LOCTEXT("PortraitMedic", "MED"), LOCTEXT("RoleMedic", "Medic-Sapper"), FLinearColor(0.35f, 0.9f, 0.6f),
			FLinearColor(0.08f, 0.16f, 0.14f, 0.9f), FLinearColor(0.3f, 0.85f, 0.65f, 0.8f) };
	}
	else
	{
		Style = { LOCTEXT("PortraitOther", "?"), LOCTEXT("RoleOther", "Speaker"), FLinearColor(0.8f, 0.85f, 0.9f),
			FLinearColor(0.1f, 0.12f, 0.16f, 0.9f), FLinearColor(0.5f, 0.55f, 0.65f, 0.8f) };
	}
	return Style;
}

FText DialogueRules::GetNextButtonText(bool bLastLine, const FString& CustomFinishText, bool bRecruitment, bool bCombatPhase)
{
	if (!bLastLine)
	{
		return LOCTEXT("Next", "Next ▶");
	}
	if (!CustomFinishText.IsEmpty())
	{
		return FText::FromString(CustomFinishText);
	}
	if (bRecruitment)
	{
		return LOCTEXT("Recruit", "🤝 Join the Squad");
	}
	return bCombatPhase ? LOCTEXT("ToBattle", "To Battle! ▶") : LOCTEXT("Understood", "Understood! ▶");
}

FText DialogueRules::GetProgressText(int32 LineIndex, int32 LineCount)
{
	return FText::FromString(FString::Printf(TEXT("[%d / %d]"), LineIndex + 1, LineCount));
}

#undef LOCTEXT_NAMESPACE
