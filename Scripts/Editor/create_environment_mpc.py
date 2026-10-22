# Creates the material parameter collection of the environment (environment phase 4a, docs/systems/environment-weather.md section 12):
# /Game/Environment/Weather/MPC/MPC_Environment with
#   scalars: SnowAmount (persistent snow cover, FEnvironmentState::SnowAmount), FrostAmount (blended preset), WindStrength (blended
#            preset), IsSnowing (0..1, smooth: the blended preset SnowIntensity, saturating at any real snowfall)
#   vector:  WindDirection (xyz = world direction, w = wind strength)
# AWeatherPresenterActor writes it every application (UMaterialParameterCollectionInstance). The migrated MPC_WeatherSettings is
# left untouched. NEW asset only: an existing MPC_Environment is checked (missing parameters are reported, nothing is changed or
# re-saved).
# Run (holding the build lock, $env:CODEX_AGENT = "claude"):
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<abs path to this file>
# Result: Saved/Logs/create_environment_mpc.txt
import os
import unreal

PATH = "/Game/Environment/Weather/MPC"
NAME = "MPC_Environment"
SCALARS = [("SnowAmount", 0.0), ("FrostAmount", 0.0), ("WindStrength", 0.0), ("IsSnowing", 0.0)]
VECTORS = [("WindDirection", unreal.LinearColor(1.0, 0.0, 0.0, 0.0))]

log_lines = []


def log(text):
    log_lines.append(text)
    unreal.log(text)


def names_of(collection, prop):
    return [str(p.get_editor_property("parameter_name")) for p in collection.get_editor_property(prop)]


full = PATH + "/" + NAME
if unreal.EditorAssetLibrary.does_asset_exist(full):
    existing = unreal.EditorAssetLibrary.load_asset(full)
    scalars = names_of(existing, "scalar_parameters")
    vectors = names_of(existing, "vector_parameters")
    missing = [n for n, _ in SCALARS if n not in scalars] + [n for n, _ in VECTORS if n not in vectors]
    log("kept (exists): %s scalars=%s vectors=%s missing=%s" % (full, scalars, vectors, missing))
else:
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    collection = tools.create_asset(NAME, PATH, unreal.MaterialParameterCollection, unreal.MaterialParameterCollectionFactoryNew())
    if collection is None:
        log("FAILED to create " + full)
    else:
        scalar_params = []
        for name, value in SCALARS:
            param = unreal.CollectionScalarParameter()  # the C++ constructor gives every parameter its own GUID
            param.set_editor_property("parameter_name", name)
            param.set_editor_property("default_value", value)
            scalar_params.append(param)
        vector_params = []
        for name, value in VECTORS:
            param = unreal.CollectionVectorParameter()
            param.set_editor_property("parameter_name", name)
            param.set_editor_property("default_value", value)
            vector_params.append(param)
        collection.set_editor_property("scalar_parameters", scalar_params)
        collection.set_editor_property("vector_parameters", vector_params)
        # The parameter ids are protected from Python; WeatherSmoke checks that they are valid and distinct.
        saved = unreal.EditorAssetLibrary.save_loaded_asset(collection, only_if_is_dirty=False)
        log("created %s scalars=%s vectors=%s saved=%s" % (full, names_of(collection, "scalar_parameters"),
            names_of(collection, "vector_parameters"), saved))

out = os.path.join(unreal.Paths.project_saved_dir(), "Logs", "create_environment_mpc.txt")
with open(out, "w", encoding="utf-8") as handle:
    handle.write("\n".join(log_lines) + "\n")
