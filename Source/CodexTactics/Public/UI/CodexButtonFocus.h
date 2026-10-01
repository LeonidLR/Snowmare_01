#pragma once

#include "CoreMinimal.h"
#include "Components/Button.h"
#include "UObject/UnrealType.h"

namespace CodexButtonFocus
{
	/**
	 * Makes a HUD button non-focusable before its Slate widget exists: a clicked focusable button keeps the keyboard focus
	 * in GameAndUI mode and swallows the game keys (1-4, Z / C / V, ...). UButton::InitIsFocusable is protected, so the
	 * IsFocusable property is set the way the details panel does.
	 */
	inline void Disable(UButton* Button)
	{
		if (const FBoolProperty* Property = Button ? CastField<FBoolProperty>(UButton::StaticClass()->FindPropertyByName(TEXT("IsFocusable"))) : nullptr)
		{
			Property->SetPropertyValue_InContainer(Button, false);
		}
	}
}
