# Creates the material parameter collection of the environment (environment phase 4a, docs/systems/environment-weather.md section 12):
# /Game/Environment/Weather/MPC/MPC_Environment with
#   scalars: SnowAmount (persistent snow cover, FEnvironmentState::SnowAmount), FrostAmount (blended preset), WindStrength (blended
#            preset), IsSnowing (0..1, smooth: the blended preset SnowIntensity, saturating at any real snowfall)
#   vector:  WindDirection (xyz = world direction, w = wind strength)
# Interactive snow phase 1 (docs/systems/environment-weather.md section 14, USnowInteractionSubsystem writes them):
#   vector:  SnowTrackWindow (x, y = window origin in world cm, z = 1 / window size in cm, w = enabled 0/1);
#            UV in RT_SnowInteraction = (WorldXY - origin.xy) * (1 / size)
#   scalars: SnowRefill (the blended preset SnowIntensity, speeds up the track fade), FreshSnow (rises while it snows, fades ~10 min
#            after; visual only)
# Directional snow (docs/systems/environment-weather.md section 15, AWeatherPresenterActor writes them from FEnvironmentState::ObjectSnow):
# AWeatherPresenterActor writes it every application (UMaterialParameterCollectionInstance). The migrated MPC_WeatherSettings is
# left untouched. IDEMPOTENT: an existing MPC_Environment keeps every parameter it has (names, defaults and ids) and only gets the
# missing ones appended (then it is saved); nothing else is changed.
# Run (holding the build lock, $env:CODEX_AGENT = "claude"):
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<abs path to this file>
# Result: Saved/Logs/create_environment_mpc.txt
import os
import unreal

PATH = "/Game/Environment/Weather/MPC"
NAME = "MPC_Environment"
SCALARS = [("SnowAmount", 0.0), ("FrostAmount", 0.0), ("WindStrength", 0.0), ("IsSnowing", 0.0), ("SnowRefill", 0.0), ("FreshSnow", 0.0)]
VECTORS = [("WindDirection", unreal.LinearColor(1.0, 0.0, 0.0, 0.0)), ("SnowTrackWindow", unreal.LinearColor(0.0, 0.0, 0.0, 0.0))]

log_lines = []


def log(text):
    log_lines.append(text)
    unreal.log(text)


def names_of(collection, prop):
    return [str(p.get_editor_property("parameter_name")) for p in collection.get_editor_property(prop)]


def log_ids(collection, when):
    """Every parameter's full text (its id included): the ids must not change across a run."""
    for prop in ("scalar_parameters", "vector_parameters"):
        for param in collection.get_editor_property(prop):
            log("%s %s: %s" % (when, prop, param.export_text()))


full = PATH + "/" + NAME
if unreal.EditorAssetLibrary.does_asset_exist(full):
    existing = unreal.EditorAssetLibrary.load_asset(full)
    scalars = names_of(existing, "scalar_parameters")
    vectors = names_of(existing, "vector_parameters")
    missing_scalars = [(n, v) for n, v in SCALARS if n not in scalars]
    missing_vectors = [(n, v) for n, v in VECTORS if n not in vectors]
    log("exists: %s scalars=%s vectors=%s missing=%s" % (full, scalars, vectors,
        [n for n, _ in missing_scalars] + [n for n, _ in missing_vectors]))
    log_ids(existing, "before")
    if missing_scalars or missing_vectors:
        # The arrays come back as copies of the C++ structs (their protected ids included) and go back whole: the existing
        # parameters keep their ids; the appended ones get a new GUID from the C++ constructor.
        scalar_params = list(existing.get_editor_property("scalar_parameters"))
        for name, value in missing_scalars:
            param = unreal.CollectionScalarParameter()
            param.set_editor_property("parameter_name", name)
            param.set_editor_property("default_value", value)
            scalar_params.append(param)
        vector_params = list(existing.get_editor_property("vector_parameters"))
        for name, value in missing_vectors:
            param = unreal.CollectionVectorParameter()
            param.set_editor_property("parameter_name", name)
            param.set_editor_property("default_value", value)
            vector_params.append(param)
        existing.set_editor_property("scalar_parameters", scalar_params)
        existing.set_editor_property("vector_parameters", vector_params)
        saved = unreal.EditorAssetLibrary.save_loaded_asset(existing, only_if_is_dirty=False)
        log("appended: scalars=%s vectors=%s saved=%s" % (names_of(existing, "scalar_parameters"),
            names_of(existing, "vector_parameters"), saved))
        log_ids(existing, "after")
    else:
        log("nothing to add (not re-saved)")
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
