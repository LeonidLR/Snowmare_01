# Creates the four weather preset data assets of the environment presenter (environment phase 3,
# docs/systems/environment-weather.md section 11): /Game/Environment/Weather/Presets/DA_Weather_Clear / _Overcast / _Snowfall / _Blizzard
# (class UWeatherPresetAsset). Each starts from the C++ default of its weather (UWeatherPresetAsset::ResetToDefaults ->
# EnvironmentVisualRules::GetDefaultPreset), so the numbers live in one place. NEW assets only: an existing asset is left untouched
# (the user tunes them in the editor). AWeatherPresenterActor reads them through soft references and falls back to the C++
# defaults while they are missing.
# Run (holding the build lock, $env:CODEX_AGENT = "claude"):
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<abs path to this file>
# Result: Saved/Logs/create_weather_presets.txt
import os
import unreal

PATH = "/Game/Environment/Weather/Presets"
PRESETS = [
    ("DA_Weather_Clear", unreal.WeatherState.CLEAR),
    ("DA_Weather_Overcast", unreal.WeatherState.OVERCAST),
    ("DA_Weather_Snowfall", unreal.WeatherState.SNOWFALL),
    ("DA_Weather_Blizzard", unreal.WeatherState.BLIZZARD),
]

log_lines = []


def log(text):
    log_lines.append(text)
    unreal.log(text)


tools = unreal.AssetToolsHelpers.get_asset_tools()
for name, weather in PRESETS:
    full = PATH + "/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(full):
        log("kept (exists): " + full)
        continue
    asset = tools.create_asset(name, PATH, unreal.WeatherPresetAsset, unreal.DataAssetFactory())
    if asset is None:
        log("FAILED to create " + full)
        continue
    asset.set_editor_property("weather", weather)
    asset.reset_to_defaults()
    saved = unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False)
    visual = asset.get_editor_property("visual")
    log("created %s (weather %s, fog density %.3f, sun x%.2f) saved=%s" % (full, weather, visual.get_editor_property("fog_density"),
        visual.get_editor_property("sun_intensity_scale"), saved))

out = os.path.join(unreal.Paths.project_saved_dir(), "Logs", "create_weather_presets.txt")
with open(out, "w", encoding="utf-8") as handle:
    handle.write("\n".join(log_lines) + "\n")
