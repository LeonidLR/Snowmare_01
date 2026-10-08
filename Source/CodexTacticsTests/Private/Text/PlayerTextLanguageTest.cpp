// CodexTactics.Text.NoCyrillicInPlayerText — guard for the user decision 2026-10-08: every in-game text is English for
// now (Russian returns later through a proper localization pass). Two checks:
//  * Static: every string literal compiled into the CodexTactics runtime module (Source/CodexTactics/**/*.cpp|h) is
//    free of Cyrillic, except debug console commands (Private/Debug), log lines, reflection metadata (editor-only
//    categories / tooltips) and lines explicitly marked `cyrillic-ok` (literals that match Russian DATA, e.g. LevelJson
//    lane names). The narrative files owned by the dialogue / mission integration are on a temporary skip list until
//    their English pass lands (see kNarrativeSkipList).
//  * Runtime: the label helpers the HUD draws every frame (combat mode, fire posture, stance, personal items) return
//    English text.

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Characters/FirePostureRules.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/PersonalItemRules.h"
#include "Combat/CombatTimeModeRules.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PlayerTextLanguageTest
{
	/** Narrative files are English since 2026-10-08 (dialogue / mission / quests pass); the skip list is now empty. */
	static const TCHAR* const kNarrativeSkipList[] = { TEXT("/__no_such_path__/") };

	/** Debug console commands / smokes are developer-only text. */
	static const TCHAR* const kDeveloperOnlyPaths[] = { TEXT("/Private/Debug/") };

	/** Lines that never reach the player: logs and reflection metadata. */
	static const TCHAR* const kNonPlayerLineMarkers[] = {
		TEXT("UE_LOG("), TEXT("UE_VLOG"), TEXT("UPROPERTY("), TEXT("UFUNCTION("), TEXT("UCLASS("), TEXT("USTRUCT("),
		TEXT("UENUM("), TEXT("UMETA("), TEXT("meta ="), TEXT("meta="), TEXT("cyrillic-ok"),
	};

	bool IsCyrillic(TCHAR Ch)
	{
		return Ch >= 0x0400 && Ch <= 0x04FF;
	}

	bool ContainsCyrillic(const FString& Text)
	{
		for (const TCHAR Ch : Text)
		{
			if (IsCyrillic(Ch))
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * The string literals of one source line that contain Cyrillic. Stops at a `//` comment outside a literal; the caller
	 * tracks block comments. Character literals ('"') and escapes are handled.
	 */
	TArray<FString> CyrillicLiterals(const FString& Line)
	{
		TArray<FString> Out;
		bool bInString = false;
		FString Current;
		for (int32 Index = 0; Index < Line.Len(); ++Index)
		{
			const TCHAR Ch = Line[Index];
			if (bInString)
			{
				if (Ch == TEXT('\\') && Index + 1 < Line.Len())
				{
					Current.AppendChar(Ch);
					Current.AppendChar(Line[++Index]);
				}
				else if (Ch == TEXT('"'))
				{
					bInString = false;
					if (ContainsCyrillic(Current))
					{
						Out.Add(Current);
					}
				}
				else
				{
					Current.AppendChar(Ch);
				}
				continue;
			}
			if (Ch == TEXT('/') && Index + 1 < Line.Len() && (Line[Index + 1] == TEXT('/') || Line[Index + 1] == TEXT('*')))
			{
				break;
			}
			if (Ch == TEXT('\'') && Index + 2 < Line.Len())
			{
				// Skip a character literal such as '"' or '\''.
				const int32 Close = Line.Find(TEXT("'"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Index + (Line[Index + 1] == TEXT('\\') ? 3 : 2));
				Index = Close == INDEX_NONE ? Index : Close;
				continue;
			}
			if (Ch == TEXT('"'))
			{
				bInString = true;
				Current.Reset();
			}
		}
		return Out;
	}

	bool PathMatchesAny(const FString& Path, TArrayView<const TCHAR* const> Needles)
	{
		for (const TCHAR* Needle : Needles)
		{
			if (Path.Contains(Needle))
			{
				return true;
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNoCyrillicInPlayerTextTest, "CodexTactics.Text.NoCyrillicInPlayerText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNoCyrillicInPlayerTextTest::RunTest(const FString&)
{
	using namespace PlayerTextLanguageTest;

	// --- Static scan of the runtime module's string literals.
	const FString Root = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::GameSourceDir(), TEXT("CodexTactics")));
	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *Root, TEXT("*.cpp"), true, false);
	TArray<FString> Headers;
	IFileManager::Get().FindFilesRecursive(Headers, *Root, TEXT("*.h"), true, false);
	Files.Append(Headers);
	if (!TestTrue(TEXT("Runtime module sources are present (run from a source checkout)"), Files.Num() > 50))
	{
		return false;
	}

	int32 Offenders = 0;
	int32 Scanned = 0;
	for (FString Path : Files)
	{
		FPaths::NormalizeFilename(Path);
		if (PathMatchesAny(Path, kNarrativeSkipList) || PathMatchesAny(Path, kDeveloperOnlyPaths))
		{
			continue;
		}
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *Path))
		{
			AddError(FString::Printf(TEXT("Cannot read %s"), *Path));
			continue;
		}
		++Scanned;
		bool bInBlockComment = false;
		for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
		{
			FString Line = Lines[LineIndex].TrimStart();
			if (bInBlockComment)
			{
				const int32 End = Line.Find(TEXT("*/"));
				if (End == INDEX_NONE)
				{
					continue;
				}
				bInBlockComment = false;
				Line.RightChopInline(End + 2);
			}
			if (Line.StartsWith(TEXT("/*")) && !Line.Contains(TEXT("*/")))
			{
				bInBlockComment = true;
				continue;
			}
			if (Line.StartsWith(TEXT("//")) || Line.StartsWith(TEXT("*")) || PathMatchesAny(Line, kNonPlayerLineMarkers))
			{
				continue;
			}
			for (const FString& Literal : CyrillicLiterals(Line))
			{
				if (++Offenders <= 40)
				{
					AddError(FString::Printf(TEXT("Cyrillic player text %s:%d: \"%s\""),
						*FPaths::GetCleanFilename(Path), LineIndex + 1, *Literal));
				}
			}
		}
	}
	TestTrue(TEXT("Scanned the runtime sources"), Scanned > 50);
	TestEqual(TEXT("Cyrillic string literals in player-facing runtime code"), Offenders, 0);

	// --- Runtime label helpers drawn by the HUD.
	for (const ECodexCombatMode Mode : { ECodexCombatMode::RealTime, ECodexCombatMode::TacticalPause, ECodexCombatMode::TurnBased })
	{
		const FString Label = FCombatTimeModeRules::GetModeLabel(ECodexGamePhase::WaveCombat, Mode);
		TestFalse(FString::Printf(TEXT("Mode label '%s' is English"), *Label), Label.IsEmpty() || ContainsCyrillic(Label));
	}
	for (const ESquadFirePosture Posture : { ESquadFirePosture::Passive, ESquadFirePosture::Defensive, ESquadFirePosture::Aggressive })
	{
		for (const FString& Label : { FirePostureRules::GetLabel(Posture), FirePostureRules::GetShortLabel(Posture),
				 FirePostureRules::GetLetter(Posture), FirePostureRules::GetKeyHint(Posture) })
		{
			TestFalse(FString::Printf(TEXT("Posture label '%s' is English"), *Label), ContainsCyrillic(Label));
		}
	}
	for (const EOperativeStance Stance : { EOperativeStance::Standing, EOperativeStance::Crouching, EOperativeStance::Prone })
	{
		const FString Label = AOperativeCharacter::GetStanceDisplayName(Stance).ToString();
		TestFalse(FString::Printf(TEXT("Stance label '%s' is English"), *Label), ContainsCyrillic(Label));
	}
	for (const EPersonalItem Item : { EPersonalItem::Medkit, EPersonalItem::CannedFood, EPersonalItem::Bread, EPersonalItem::Chocolate })
	{
		TestFalse(TEXT("Item name is English"), ContainsCyrillic(PersonalItemRules::GetName(Item).ToString()));
		TestFalse(TEXT("Missing-item name is English"), ContainsCyrillic(PersonalItemRules::GetMissingName(Item).ToString()));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCyrillicLiteralScannerTest, "CodexTactics.Text.LiteralScanner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCyrillicLiteralScannerTest::RunTest(const FString&)
{
	using namespace PlayerTextLanguageTest;
	// The scanner finds Cyrillic only inside literals and ignores trailing comments and escaped quotes.
	TestEqual(TEXT("English literal"), CyrillicLiterals(TEXT("Post(TEXT(\"Medkit\"));")).Num(), 0);
	TestEqual(TEXT("Cyrillic literal"), CyrillicLiterals(TEXT("Post(TEXT(\"Аптечка\"));")).Num(), 1);
	TestEqual(TEXT("Comment only"), CyrillicLiterals(TEXT("Post(TEXT(\"Medkit\")); // \"Аптечка\"")).Num(), 0);
	TestEqual(TEXT("Escaped quote"), CyrillicLiterals(TEXT("TEXT(\"a \\\"Ж\\\" b\")")).Num(), 1);
	TestEqual(TEXT("Char literal quote"), CyrillicLiterals(TEXT("Line.Contains('\"') && TEXT(\"ok\")")).Num(), 0);
	TestEqual(TEXT("Two literals"), CyrillicLiterals(TEXT("A(TEXT(\"Ж\"), TEXT(\"ok\"), TEXT(\"ЖЖ\"))")).Num(), 2);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
