#include "GameFlow/PauseOrderRules.h"

bool PauseOrderRules::IsRadiusLimited(bool bFreePause)
{
	return !bFreePause;
}

float PauseOrderRules::GetOrderRadius(bool bFreePause, float PauseRadius)
{
	return IsRadiusLimited(bFreePause) ? PauseRadius : UnlimitedRadius;
}
