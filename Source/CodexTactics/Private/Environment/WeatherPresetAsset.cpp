#include "Environment/WeatherPresetAsset.h"

#include "Environment/EnvironmentVisualRules.h"

void UWeatherPresetAsset::ResetToDefaults()
{
	Modify();
	Visual = EnvironmentVisualRules::GetDefaultPreset(Weather);
}
